#pragma once
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include "globalinfo.h"
#include "types.h"

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


//solely for convenience
inline llvm::IntegerType* int64_type() { return llvm::Type::getInt64Ty(*context); }
inline llvm::Type* void_type() { return llvm::Type::getVoidTy(*context); }

inline llvm::Constant* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(int64_type(), value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm array");
	return llvm::ArrayType::get(int64_type(), size);
}

inline llvm::Type* llvm_type(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm type");
	if (size > 1) return llvm_array(size);
	else return int64_type();
}

inline llvm::Type* llvm_type_including_void(uint64_t size)
{
	if (size == 0) return void_type();
	if (size > 1) return llvm_array(size);
	else return int64_type();
}


//return type is not a llvm::Function*, because it's a pointer to a function.
template<typename... should_be_type_ptr, typename fptr> inline llvm::Value* llvm_function(fptr* function, llvm::Type* return_type, should_be_type_ptr... argument_types)
{
	std::vector<llvm::Type*> argument_type{argument_types...};
	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, argument_type, false);
	llvm::PointerType* function_pointer_type = function_type->getPointerTo();
	llvm::Constant *function_address = llvm_integer((uint64_t)function);
	return builder->CreateIntToPtr(function_address, function_pointer_type, s("convert address to function"));
}

//we already typechecked and received 3. then, they're the same size, unless one of them is T::does_not_return
//thus, we check for T::nonexistent
inline llvm::Value* llvm_create_phi(llvm::Value* first, llvm::Value* second, Type* first_t, Type* second_t, llvm::BasicBlock* firstBB, llvm::BasicBlock* secondBB)
{
	uint64_t size1 = get_size(first_t);
	uint64_t size2 = get_size(second_t);

	//these are in case one of them is T::does_not_return
	if (size2 == 0) return first;
	if (size1 == 0) return second;

	llvm::PHINode* PN = builder->CreatePHI(size1 == 1 ? (llvm::Type*)int64_type() : llvm_array(size1), 2); //phi with two incoming variables
	PN->addIncoming(first, firstBB);
	PN->addIncoming(second, secondBB);
	return PN;
}




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

	llvm::Value* get_location()
	{
		check(stored_size != 0, "no stored size");
		determine_spot(stored_size);
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
	void determine_spot(uint64_t size)
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
				if (offset) spot = llvm::cast<llvm::AllocaInst>(builder->CreateConstInBoundsGEP2_64(base, 0, offset));
				spot = llvm::cast<llvm::AllocaInst>(builder->CreatePointerCast(spot, llvm_array(size)->getPointerTo()));
			}
			else //size is 1
			{
				spot = llvm::cast<llvm::AllocaInst>(builder->CreateConstInBoundsGEP2_64(base, 0, offset));
			}
		}
		check(size == cached_size, "caching mechanism failed");
	}
	void store(llvm::Value* val, uint64_t size)
	{
		//val = int64 if size = 1, val = [int64 x size] if size > 1.
		//AllocaInst* a = [int64 x size] for size >= 1.
		//the reason for the difference is that we can't GEP on an integer alloca.
		//we can't change the type halfway through either, because there are old CreateStores that relied on it being an int.
		//seems like we basically have to know before-hand what the end type will be, since we can't convert an Int alloca into an Array alloca or vice-versa, without screwing up the GEP or lack of GEP in all the functions.
		//thus, we need all allocas to be arrays.

		check(size != 0, "tried to store 0 size object");
		determine_spot(size);
		builder->CreateStore(val, spot);
	}

	llvm::Value* get_location(uint64_t size)
	{
		determine_spot(size);
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
		: error_code(err), IR(0), place(b), type(t), on_stack(o), self_lifetime(s), self_validity_guarantee(u), target_hit_contract(l) {}
	//IR should be initialized to 0 in this ctor to avoid uninitialized-value errors from valgrind. however, it is otherwise unnecessary as the uninitialized value is never used except when trying to output it for debug purposes.

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), on_stack(stack_state::temp), self_lifetime(0), self_validity_guarantee(0), target_hit_contract(full_lifetime) {}
};



/** mem-to-reg only works on entry block variables.
thus, this function builds llvm::Allocas in the entry block. it should be preferred over trying to create allocas directly.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
we call this "create_alloca" instead of "create_alloca_in_entry_block", because it's the general alloca mechanism. if we said, "in_entry_block", then the user would be confused as to when to use this. by not having that, it's clear that this should be the default.

we create an alloca. it's a placeholder for dependencies, in case we need a place to store things.
if we don't need it, we can use eraseFromParent()
if we do need it, then we use ReplaceInstWithInst.
*/
inline llvm::AllocaInst* create_empty_alloca() {
	llvm::BasicBlock& first_block = builder->GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());
	return TmpB.CreateAlloca(llvm_array(1));
}

inline llvm::AllocaInst* create_actual_alloca(uint64_t size) {
	llvm::BasicBlock& first_block = builder->GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());
	return TmpB.CreateAlloca(llvm_array(size));
}
