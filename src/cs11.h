#pragma once
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <random>
#include "types.h"
#include "ASTs.h"
#include "orc.h"
#include "globalinfo.h"
#include "helperfunctions.h"

#ifdef _MSC_VER
#define thread_local
#endif
extern thread_local std::mt19937_64 mersenne;
extern thread_local uint64_t finiteness;
constexpr uint64_t FINITENESS_LIMIT = 10;
uint64_t generate_exponential_dist();


extern thread_local KaleidoscopeJIT* c;

class compiler_object
{
	KaleidoscopeJIT& J;

	std::deque<memory_allocation> allocations; //deque, because pointers need to remain valid. must be above std::unordered_map objects, because the Return_Info has dtors that reference this

	//lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::unordered_set<uAST*> loop_catcher;

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

	//some objects have expired - this clears them
	void clear_stack(uint64_t desired_stack_size);
	//this runs the dtors. it's called by clear_stack, but also called by goto, which jumps stacks.
	void emit_dtors(uint64_t desired_stack_size);



	Return_Info generate_IR(uAST* user_target, unsigned stack_degree, memory_location desired = memory_location());


	
public:
	compiler_object() : J(*c), error_location(nullptr), new_context(new llvm::LLVMContext()) {}
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


	std::unique_ptr<llvm::LLVMContext> new_context;



};