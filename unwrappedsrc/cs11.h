#pragma once
extern bool OPTIMIZE;
extern bool VERBOSE_DEBUG;
extern bool INTERACTIVE;
extern bool CONSOLE;
extern bool TIMER;
#include <iostream>

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <random>
#include "types.h"
#include "ASTs.h"
#include "orc.h"

#ifdef _MSC_VER
#define thread_local
#endif
extern thread_local llvm::LLVMContext& thread_context;
extern thread_local std::mt19937_64 mersenne;
extern thread_local uint64_t finiteness;
constexpr uint64_t FINITENESS_LIMIT = 10;
uint64_t generate_exponential_dist();


enum IRgen_status {
	no_error,
	infinite_loop, //ASTs have pointers in a loop
	active_object_duplication, //an AST has two return values active simultaneously, making the AST-to-object map ambiguous
	type_mismatch, //in an if statement, the two branches had different types.
	null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
	pointer_without_target, //tried to get a pointer to an AST, but it was not found at all
	pointer_to_temporary, //tried to get a pointer to an AST, but it was not placed on the stack.
	missing_label, //tried to goto a label, but the label was not found
	label_incorrect_stack, //tried to goto a label, but the stack elements didn't match
	label_duplication, //a label was pointed to in the tree twice
	store_pointer_lifetime_mismatch, //tried to store a short-lasting object in a long-lasting memory location
};

enum class stack_state
{
	temp,
	cheap_alloca,
	might_be_visible, //to other threads. better use atomic operations
	full_might_be_visible, //when you load, you get a full_pointer
	full_isolated, //it's a full pointer. but no need for atomic operations
};
inline bool requires_atomic(stack_state x)
{
	if (x == stack_state::full_might_be_visible || x == stack_state::might_be_visible)
		return true;
	else return false;
}

inline bool is_full(stack_state x)
{
	if (x == stack_state::full_might_be_visible || x == stack_state::full_isolated)
		return true;
	else return false;
}

constexpr uint64_t full_lifetime = -1ll;

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include "helperfunctions.h"
struct memory_location //used for GEP. good for delaying emission of instructions unless they're necessary.
{
	llvm::AllocaInst* base;
	uint64_t offset;
	llvm::AllocaInst* spot;
	uint64_t cached_size; //when you get this object, it caches the size, so that AllocaInst* spot can be cached. if the size is different, I need to reengineer things.
	uint64_t stored_size = 0; //used if you want to embed the entire information in the memory_location. good for storing memory locations in active_objects, but without emitting instructions.
	memory_location(llvm::AllocaInst* f1, uint64_t o) : base(f1), offset(o), spot(nullptr) {}
	memory_location() : base(nullptr), offset(0), spot(nullptr) {}

	void set_stored_size(uint64_t size)
	{
		stored_size = size;
	}

	llvm::Value* get_location(llvm::IRBuilder<>& Builder)
	{
		check(stored_size != 0, "no stored size");
		determine_spot(Builder, stored_size);
		return spot;
	}
	void cast_base(uint64_t size)
	{
		if (size > 1)
		{
			llvm::AllocaInst* new_alloca = new llvm::AllocaInst(llvm_array(size));
			llvm::BasicBlock::iterator ii(base);
			ReplaceInstWithInst(base->getParent()->getInstList(), ii, new_alloca);
			//base->setAllocatedType(llvm_array(size));
			//base->mutateType(llvm_array(size)->getPointerTo()); //this is potentially very very dangerous. maybe we should use ReplaceInstWithInst

			//we can only do this when size > 1, because if it's smaller, we'd need to do a GetElementPtr.
			base = new_alloca;
			spot = base;
			cached_size = size;
		}
		check(offset == 0, "this isn't expected, I'm relying on this to cache base in spot which I probably shouldn't be doing");
	}
	//assigns spot to be the correct pointer. i64* or [size x i64]*.
	void determine_spot(llvm::IRBuilder<>& Builder, uint64_t size)
	{
		check(size != 0, "meaningless to get a 0 size location");
		//this is a caching mechanism. we find spot once, and then store it.
		if (spot == nullptr)
		{
			cached_size = size;
			spot = base;
			if (size > 1)
			{
				//if offset == 0, we don't need to GEP, because it will be casted anyway.
				if (offset) spot = llvm::cast<llvm::AllocaInst>(Builder.CreateConstInBoundsGEP2_64(base, 0, offset));
				spot = llvm::cast<llvm::AllocaInst>(Builder.CreatePointerCast(spot, llvm_array(size)->getPointerTo()));
			}
			else //size is 1
			{
				spot = llvm::cast<llvm::AllocaInst>(Builder.CreateConstInBoundsGEP2_64(base, 0, offset));
			}
		}
		check(size == cached_size, "caching mechanism failed");
	}
	void store(llvm::IRBuilder<>& Builder, llvm::Value* val, uint64_t size)
	{
		//val = int64 if size = 1, val = [int64 x size] if size > 1.
		//AllocaInst* a = [int64 x size] for size >= 1.
		//the reason for the difference is that we can't GEP on an integer alloca.
		//we can't change the type halfway through either, because there are old CreateStores that relied on it being an int.
		//seems like we basically have to know before-hand what the end type will be, since we can't convert an Int alloca into an Array alloca or vice-versa, without screwing up the GEP or lack of GEP in all the functions.
		//thus, we need all allocas to be arrays.
		
		check(size != 0, "tried to store 0 size object");
		determine_spot(Builder, size);
		Builder.CreateStore(val, spot);
	}

	llvm::Value* get_location(llvm::IRBuilder<>& Builder, uint64_t size)
	{
		determine_spot(Builder, size);
		return spot;
	}
};

//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
	IRgen_status error_code;

	//either IR or place is active, not both.
	//if on_stack == temp, then IR is active. otherwise, place is active.
	//place is only ever active if you passed in stack_degree >= 1, because that's when on_stack != temp.
	llvm::Value* IR;
	memory_location place;

	Type* type;
	stack_state on_stack; //if on_stack =/= temp, the llvm type is actually a pointer. but the internal type doesn't change.

	//to write an object into a pointed-to memory location, we must have guarantee of object <= hit contract of memory location
	//there's only one hit contract value per object, which can be a problem for concatenations of many cheap pointers.
	//however, we need concatenation only for heap objects, parameters, function return; in these cases, the memory order doesn't matter
	uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a deletion time, not a creation time.
	//it's important that it is a deletion time, because deletion and creation are not in perfect stack configuration.
	//because temporaries are created before the base object, and die just after.

	//low number = longer life.
	uint64_t self_validity_guarantee; //higher is less lenient (validity is shorter). the object's values must be valid for at least this time.
	//for when an object wants to be written into a memory location

	uint64_t target_hit_contract; //lower is less lenient. the reference will disappear after this time. used for pointers
	//measures the pointer's target's lifetime, for when the pointed-to memory location wants something to be written into it.

	Return_Info(IRgen_status err, llvm::Value* b, Type* t, stack_state o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(b), type(t), on_stack(o), self_lifetime(s), self_validity_guarantee(u), target_hit_contract(l) {}
	Return_Info(IRgen_status err, memory_location b, Type* t, stack_state o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), place(b), type(t), on_stack(o), self_lifetime(s), self_validity_guarantee(u), target_hit_contract(l) {}

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), on_stack(stack_state::temp), self_lifetime(0), self_validity_guarantee(0), target_hit_contract(full_lifetime) {}
};


struct compiler_host
{
	SessionContext S; //seems these can't be thread_local. maybe we should split them off into a different class.
	KaleidoscopeJIT J;
	compiler_host() : S(thread_context), J(S) {}
};
extern thread_local compiler_host* c;

class compiler_object
{

	SessionContext& S;
	KaleidoscopeJIT& J;
	IRGenContext C;

	//lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::unordered_set<uAST*> loop_catcher;

	//maps user ASTs to copied ASTs. note that it won't handle 0
	std::unordered_map<uAST*, uAST*> copy_mapper;

	//a stack for bookkeeping lifetimes; keeps track of when objects are alive.
	//bool is true if the object is a memory location that can have pointers to it, instead of a temporary.
	std::stack<std::pair<uAST*, bool>> object_stack;

	//maps ASTs to their generated IR and return type.
	std::unordered_map<uAST*, Return_Info> objects;

	struct label_info
	{
		llvm::BasicBlock* block;
		uint64_t stack_size;
		bool is_forward; //set to 1 on creation, then to 0 after you finish the label's interior.
		label_info(llvm::BasicBlock* l, uint64_t s, bool f) : block(l), stack_size(s), is_forward(f) {}
	};
	//these are labels which can be jumped to. the basic block, and the object stack.
	std::map<uAST*, label_info> labels;

	//increases by 1 every time an object is created. imposes an ordering on stack object lifetimes, such that if two objects exist simultaneously, the lower one will survive longer.
	//but two objects don't necessarily exist simultaneously. for example, two temporary objects that live separately. then this number is useless in that case.
	uint64_t incrementor_for_lifetime = 0;

	//some objects have expired - this clears them
	void clear_stack(uint64_t desired_stack_size);
	//this runs the dtors. it's called by clear_stack, but also called by goto, which jumps stacks.
	void emit_dtors(uint64_t desired_stack_size);

	llvm::AllocaInst* create_empty_alloca();
	llvm::AllocaInst* create_actual_alloca(uint64_t size);

	Return_Info generate_IR(uAST* user_target, unsigned stack_degree, memory_location desired = memory_location());

public:
	compiler_object() : S(c->S), J(c->J), C(S), error_location(nullptr) {}
	unsigned compile_AST(uAST* target); //we can't combine this with the ctor, because it needs to return an int

	void* fptr; //the end fptr.

	//exists when IRgen_status has an error.
	uAST* error_location;
	unsigned error_field; //which field in error_location has the error

	//these exist on successful compilation. guaranteed to be uniqued and in the heap.
	//currently, parameters are not implemented
	Type* return_type;
	Type* parameter_type = nullptr;
	KaleidoscopeJIT::ModuleHandleT result_module;
};


/*
//return true on success. this is a shim, kind of messy. no module cleanup or anything.
inline bool compile_and_run(uAST* ast)
{
	finiteness = FINITENESS_LIMIT;
	compiler_object j;
	unsigned error_code = j.compile_AST(ast);
	if (error_code)
	{
		console << "Malformed AST: code " << error_code << " at AST " << j.error_location << " " << AST_descriptor[j.error_location->tag].name << " field " << j.error_field << "\n\n";
		return 0;
	}
	if (VERBOSE_DEBUG) console << "running function:\n";
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		console << "Evaluated to " << FP() << '\n';
	}
	else if (size_of_return > 1)
	{
		using std::array;
		switch (size_of_return)
		{
		case 2: cout_array(((array<uint64_t, 2>(*)())(intptr_t)fptr)()); break; //theoretically, this ought to break. array<2> = {int, int}
		case 3: cout_array(((array<uint64_t, 3>(*)())(intptr_t)fptr)()); break; //this definitely does break.
		case 4: cout_array(((array<uint64_t, 4>(*)())(intptr_t)fptr)()); break;
		case 5: cout_array(((array<uint64_t, 5>(*)())(intptr_t)fptr)()); break;
		case 6: cout_array(((array<uint64_t, 6>(*)())(intptr_t)fptr)()); break;
		case 7: cout_array(((array<uint64_t, 7>(*)())(intptr_t)fptr)()); break;
		case 8: cout_array(((array<uint64_t, 8>(*)())(intptr_t)fptr)()); break;
		case 9: cout_array(((array<uint64_t, 9>(*)())(intptr_t)fptr)()); break;
		default: console << "function return size is " << size_of_return << " and C++ seems to only take static return types\n"; break;
		}
	}
	else
	{
		void(*FP)() = (void(*)())(intptr_t)fptr;
		FP();
	}
	else console << "Successful compile\n\n";
	return true;
}*/