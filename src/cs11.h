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

extern std::mt19937_64 mersenne;
extern uint64_t finiteness;
constexpr uint64_t FINITENESS_LIMIT = 10;
uint64_t generate_exponential_dist();


extern KaleidoscopeJIT* c;

class compiler_object
{
	KaleidoscopeJIT& J;

	std::deque<memory_allocation> allocations; //deque, because pointers need to remain valid. must be above std::unordered_map objects, because the Return_Info has dtors that reference this

	//lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::unordered_set<uAST*> loop_catcher;

	//a stack for bookkeeping lifetimes; keeps track of when objects are alive.
	//we insert a stack element here whenever we succeed at adding an AST to <>objects.
	std::stack<uAST*> object_stack;

	//maps ASTs to their generated IR and return type.
	std::unordered_map<uAST*, Return_Info> objects;

	//pair with clear_stack().
	void new_living_object(uAST* target, Return_Info r)
	{
		check((uint64_t)r.type != ASTn("pointer"), "what the fuck");
		auto insert_result = objects.insert({target, r});
		if (!insert_result.second) //collision: AST is already there
			return;
		object_stack.push(target);
		return;
	}

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
	memory_allocation* new_reference(llvm::Value* IR)
	{
		allocations.push_back(IR);
		return &allocations.back();
	}

	using IRemitter = std::function<llvm::Value*()>;
	//using error_in_check = std::function<bool()>; //returns 1 if it's time to get out

	//if errors can occur, you must catch them separately. try having a local Return_Info.
	//note: that means that even if an error occurs in the first branch, second branch will still run.
	//thus, we mandate that erroring out of generate_IR() must still leave the IR builders in valid states.
	//the size of all values that enter the phi must be 1. this is baked in when we call llvm_create_phi. I don't know how to get a size another way.
	inline llvm::Value* create_if_value(llvm::Value* comparison, IRemitter true_case, IRemitter false_case)//, error_in_check should_I_bail = [](){return false;})
	{
		check(comparison != nullptr, "pass in a real comparison please");
		uint64_t if_stack_position = object_stack.size();

		//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else

		llvm::Function *TheFunction = builder->GetInsertBlock()->getParent();
		llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*context, s("merge"), TheFunction);
		llvm::Value* iftype[2];
		llvm::BasicBlock* dynshuffleBB[2];
		for (uint64_t x : {0, 1}) dynshuffleBB[x] = llvm::BasicBlock::Create(*context, s(""), TheFunction);
		builder->CreateCondBr(comparison, dynshuffleBB[0], dynshuffleBB[1]);
		for (uint64_t x : {0, 1})
		{
			builder->SetInsertPoint(dynshuffleBB[x]);
			if (x == 0)
			{
				iftype[x] = true_case();
			}
			else if (x == 1)
			{
				iftype[x] = false_case();
			}
			builder->CreateBr(MergeBB);
			dynshuffleBB[x] = builder->GetInsertBlock();

			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			clear_stack(if_stack_position);
		}
		builder->SetInsertPoint(MergeBB);
		//if (should_I_bail()) return 0;
		llvm::Value* result = llvm_create_phi({iftype[0], iftype[1]}, {dynshuffleBB[0], dynshuffleBB[1]});
		return result;
	}

	Return_Info generate_IR(uAST* user_target, uint64_t stack_degree);
	
public:
	compiler_object() : J(*c), error_location(nullptr), return_type(0), new_context(new llvm::LLVMContext()) {}
	uint64_t compile_AST(uAST* target); //we can't combine this with the ctor, because it needs to return an int

	void* fptr; //the end fptr.

	//exists when IRgen_status has an error.
	uAST* error_location;
	uint64_t error_field; //which field in error_location has the error

	//these exist on successful compilation. guaranteed to be uniqued and in the heap.
	//currently, parameters are not implemented
	Tptr return_type;
	Tptr parameter_type = 0;
	KaleidoscopeJIT::ModuleHandleT result_module;

	std::unique_ptr<llvm::LLVMContext> new_context;
};