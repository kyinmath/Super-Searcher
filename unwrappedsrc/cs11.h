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
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/LLVMContext.h>
#include "types.h"
#ifdef _MSC_VER
#define thread_local
#endif
extern thread_local llvm::LLVMContext& thread_context;
extern std::mt19937_64 mersenne;
extern thread_local uint64_t finiteness;
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
};

constexpr uint64_t full_lifetime = -1ll;
//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
	IRgen_status error_code;

	llvm::Value* IR;

	Type* type;
	bool on_stack; //if the return value refers to an alloca.
	//in that case, the llvm type is actually a pointer. but the internal type doesn't change.

	//this lifetime information is only used when a cheap pointer is in the type. see pointers.txt
	//to write a pointer into a pointed-to memory location, we must have guarantee of pointer >= hit contract of memory location
	//there's only one pair of target lifetime values per object, which can be a problem for objects which concatenate many cheap pointers.
	//however, we need concatenation only for heap objects, parameters, function return; in these cases, the memory order doesn't matter
	uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a deletion time, not a creation time.
	//it's important that it is a deletion time, because deletion and creation are not in perfect stack configuration.
	//because temporaries are created before the base object, and die just after.

	uint64_t target_life_guarantee; //higher is stricter. the target must last longer than this.
	//measures the pointer's target's lifetime, for when the pointer wants to be written into a memory location

	uint64_t target_hit_contract; //lower is stricter. the target must die faster than this.
	//measures the pointer's target's lifetime, for when the pointed-to memory location wants something to be written into it.

	Return_Info(IRgen_status err, llvm::Value* b, Type* t, bool o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(b), type(t), on_stack(o), self_lifetime(s), target_life_guarantee(u), target_hit_contract(l) {}

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), type(T::null), on_stack(false), self_lifetime(0), target_life_guarantee(0), target_hit_contract(-1ull) {}
};


class compiler_object
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;
	llvm::ExecutionEngine* engine;

	//lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::unordered_set<AST*> loop_catcher;

	//a stack for bookkeeping lifetimes; keeps track of when objects are alive.
	//it's only a vector because we have to go through it during "goto()" while checking if the stacks match up
	std::vector<AST*> object_stack;

	//maps ASTs to their generated IR and return type.
	std::unordered_map<AST*, Return_Info> objects;

	//these are the labels which are later in the basic block. you can jump to them without checking finiteness, but you must check the type stack
	//std::stack<AST*> future_labels;

	struct label_info
	{
		llvm::BasicBlock* block;
		uint64_t stack_size;
		bool is_forward; //set to 1 on creation, then to 0 after you finish the label's interior.
		label_info(llvm::BasicBlock* l, uint64_t s, bool f) : block(l), stack_size(s), is_forward(f) {}
	};
	//these are labels which can be jumped to. the basic block, and the object stack.
	std::map<AST*, label_info> labels;

	//a memory pool for storing temporary types. will be cleared at the end of compilation.
	//it must be a deque so that its memory locations stay valid after push_back().
	//even though the constant impact of deque (memory) is bad for small compilations.
	std::deque<Type> type_scratch_space;

	//increases by 1 every time an object is created. imposes an ordering on stack object lifetimes.
	uint64_t incrementor_for_lifetime = 0;

	//some objects have expired - this clears them
	void clear_stack(uint64_t desired_stack_size);
	//this runs the dtors. it's called by clear_stack, but also called by goto, which jumps stacks.
	void emit_dtors(uint64_t desired_stack_size);

	llvm::AllocaInst* create_alloca(uint64_t size);

	Return_Info generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location = nullptr);

public:
	compiler_object();
	unsigned compile_AST(AST* target); //we can't combine this with the ctor, because it needs to return an int
	//todo: right now, compile_AST runs the function that it creates. that's not what we want.

	//exists when IRgen_status has an error.
	AST* error_location;
	unsigned error_field; //which field in error_location has the error

	//these exist on successful compilation. guaranteed to be uniqued and in the heap.
	//currently, parameters are not implemented
	Type* return_type;
	Type* parameter_type = nullptr;
};

