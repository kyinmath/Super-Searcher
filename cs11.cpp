#include <iostream>
#include <cstdint>
#include <mutex>
#include <array>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <stack>
#include <tuple>
#include <algorithm>
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "AST commands.h"

/*
structure: each AST has:
1. a tag, which says what kind of AST it is
2. 
uint64_t is my only object type.
*/

//todo: threading.
llvm::LLVMContext& thread_context = llvm::getGlobalContext();

template<typename target_type>
union int_or_ptr
{
	uint64_T integer;
	target_type* pointer;
	int_or_ptr(uint64_T x) : integer(x) {}
	int_or_ptr(const target_type* x) : pointer(x) {}
};

const uint64_t max_fields_in_AST = 4; //should accomodate the largest possible AST
struct AST
{
	//std::mutex lock;
	uint64_t tag;
	AST* preceding_BB_element; //this object survives on the stack and can be referenced. it's the previous basic block element.
	//we use a reverse basic block order so that all pointers point backwards in the stack, instead of having some pointers going forward, and some pointers going backwards.
	//AST* braced_element; //this object does not survive on the stack.
			//for now, we don't worry about 3-tree memory locality.
	std::array<int_or_ptr<AST>, max_fields_in_AST> pointers;
};

struct Type
{
	uint64_t size; //for now, it's just the number of uint64_t in an object. no complex objects allowed.
};

namespace compile_state
{
	enum {
		no_error,
		infinite_loop,
	};
};

struct Compile_Return_Value
{
	llvm::Value* llvm_return_object;
	Compile_Return_Value(llvm::Value* b)
		: llvm_return_object(b) {}
};


struct compiler_state
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;

	std::unordered_set<AST*>  loop_catcher; //lists the existing ASTs in a chain. goal is to prevent infinite loops.

	//these are the labels which we just passed by. you can jump to them without checking finiteness, but you still have to check the type stack
	std::stack<AST*> future_labels;

	compiler_state() : error_location(nullptr), the_return_value(nullptr), Builder(thread_context)
	{
		using namespace llvm;
		TheModule = new Module("temp module", thread_context);
		FunctionType *FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
		Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

		BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
		Builder.SetInsertPoint(BB);
	}

public:


	AST* error_location;
	Compile_Return_Value the_return_value;
	//exists when compile_state == no_error. the purpose of this is so that we don't have to copy a return object over and over if we fail to compile


	llvm::AllocaInst* CreateEntryBlockAlloca(uint64_t size);

	unsigned AST_gen(AST* target, bool on_stack);
	unsigned initiate_compilation(AST* target);
};


//generate llvm code. propre return value goes into Compile_Return_Value/error_location; the unsigned return is an error code.
unsigned compiler_state::AST_gen(AST* target, bool on_stack)
{
	if (target == nullptr)
	{
		return compile_state::no_error;
	}

	if (this->loop_catcher.find(target) != this->loop_catcher.end())
	{
		this->error_location = target;
		return compile_state::infinite_loop;
	}
	this->loop_catcher.insert(target);

	//this creates a dtor that takes care of loop_catcher removal.
	struct temp_RAII{
		//compiler_state can only be used if it is passed in.
		compiler_state* object; AST* targ;
		temp_RAII(compiler_state* x, AST* t) : object(x), targ(t) {}
		~temp_RAII() { object->loop_catcher.erase(targ); }
	} temp_object(this, target);


	//compile the things that survive.
	unsigned previous_elements = AST_gen(target->preceding_BB_element, true);
	if (!previous_elements) return previous_elements;
	//auto first_pointer = the_return_value;

	//storage for superdependency compilation
	std::array<Compile_Return_Value, max_fields_in_AST> subs;

	auto generate_internal = [&](unsigned position)
	{
		unsigned result = AST_gen(target->pointers[position].pointer, false);
		if (!result) return result; //error
		subs[position] = the_return_value;
		return 0u;
	};



	switch (target->tag)
	{
	case ASTn("static_integer"):

	case ASTn("add"): //add two integers.

		//two integers
		compile_superdependency_position(0);
		compile_superdependency_position(1);

		//verify that they match the int type.
		for (unsigned x : {0, 1})
			if (type_check<RVO>(subs[x].type, type_pointer::uint64, 0, 0, true) != 3)
			{
				this->error_location = target->pointers[x].pointer;
				return compile_state::type_mismatch;
			}
		llvm::Value* first_address = Builder.CreateConstGEP2_64(subs[0].llvm_Value, 0, 0);
		llvm::Value* first_value = Builder.CreateLoad(first_address);
		llvm::Value* second_address = Builder.CreateConstGEP2_64(subs[1].llvm_Value, 0, 0);
		llvm::Value* second_value = Builder.CreateLoad(second_address);

		return finish(type_pointer::uint64, Builder.CreateAdd(subs[0].llvm_Value, subs[1].llvm_Value), 1);
	case AST("concatenate"):

		compile_superdependency_position(0);
		compile_superdependency_position(1);

		scratch_space.push_back(immut_type_T(type::concatenate, subs[0].type, subs[1].type));

		uint64_t result_size = subs[0].size + subs[1].size;
		llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
		llvm::Type* array_type = llvm::ArrayType::get(int64_type, result_size);
		llvm::AllocaInst* storage_location = Builder.CreateAlloca(array_type);

		//todo: we should be copying stuff differently. copy this code elsewhere though; the second half is useful.
		for (int incrementor = 0; incrementor < subs[0].size; ++incrementor) //copy the first array
		{
			llvm::Value* first_address = Builder.CreateConstGEP2_64(subs[0].llvm_Value, 0, incrementor);
			llvm::Value* first_value = Builder.CreateLoad(first_address);
			llvm::Value* storage_address = Builder.CreateConstGEP2_64(subs[0].llvm_Value, 0, incrementor);
			Builder.CreateStore(first_value, storage_address); //copy a single integer over
		}

		for (int incrementor = 0; incrementor < subs[1].size; ++incrementor) //copy the second array
		{
			llvm::Value* second_address = Builder.CreateConstGEP2_64(subs[0].llvm_Value, 0, incrementor);
			llvm::Value* second_value = Builder.CreateLoad(second_address);
			llvm::Value* storage_address = Builder.CreateConstGEP2_64(subs[0].llvm_Value, 0, incrementor + subs[0].size);
			Builder.CreateStore(second_value, storage_address);
		}


		pointer_lower_lifetime = std::min(subs[0].target_lower_lifetime, subs[1].target_lower_lifetime);
		pointer_upper_lifetime = std::max(subs[0].target_upper_lifetime, subs[1].target_upper_lifetime);

		return (scratch_space.back(), storage_location, result_size);


	case AST("overwrite"):

		//needs some kind of offset, an ending offset, and needs to get the target's type.
		//get the type from the registered objects list.
		//then, toggle locks and call dtors.
	case AST("label"):
		//remove the label from future_labels, because it's no longer in front of anything
		assert(future_labels.top() == target, "ordering issue in AST_gen");
		future_labels.pop();
	case AST("if"): //you can see the condition's return object in the branches.
		//otherwise, the condition is missing, which could be another if function that may be implemented later, but probably not.

		//the condition statement
		compile_superdependency_position(0);

		//find labels in the second branch
		coro second_branch(boost::bind(&compiler_state::compile_shim, this), target->pointers[2].pointer);
		if (target->pointers[2].pointer != nullptr)
		{
			unsigned error_value = second_branch.get();
			assert(error_value != compile_state::no_error); //EVERY possible AST must run either resolve_continuations or AST_gen or both, so we can't receive no_error
			if (error_value != compile_state::continuation) return error_value;
		}
		//otherwise do nothing

		//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
		llvm::Value* comparison = Builder.CreateICmpNE(subs[0].llvm_Value,
			llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)));
		llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

		// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
		llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, "", TheFunction);
		llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context);
		llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context); //todo: is this what we're returning? is there a better name?


		Builder.CreateCondBr(comparison, ThenBB, ElseBB);

		//first branch.
		Builder.SetInsertPoint(ThenBB);

		compile_superdependency_position(1);

		Builder.CreateBr(MergeBB);
		ThenBB = Builder.GetInsertBlock();

		TheFunction->getBasicBlockList().push_back(ElseBB);
		Builder.SetInsertPoint(ElseBB);

		{
			unsigned error_value = second_branch.get();
			if (error_value != compile_state::no_error) return error_value;
		}

		//for the second branch
		Builder.CreateBr(MergeBB);
		ThenBB = Builder.GetInsertBlock();

		lockable_type_T* resultant_type;
		uint64_t resultant_size;
		//if one of them is escape_type, then choose the other one
		if ((subs[1].type == escape_type) || (subs[2].type == escape_type))
		{
			resultant_type = (subs[1].type == escape_type) ? subs[2].type : subs[1].type;
			resultant_size = (subs[1].type == escape_type) ? subs[2].size : subs[1].size;
		}

		else if (type_check<RVO>(subs[1].type, subs[2].type, 0, 0, false) != 3)
		{
			error_location = target;
			return compile_state::type_mismatch;
			//todo: what error should be returned? the owning AST, or the superdependency AST?
			//definitely not the superdependency AST, because then there's not any way to get the owner. you have no idea what it should be. and there can be duplicates: different owners
			//but if the owner is reported, we probably need to report which slot, also.
			//actually, we want to report a lot of information. the types, whether it was an if branch type mismatch,
		}
		else
		{
			resultant_type = subs[2].type; //it's the more lax one
			resultant_size = subs[2].size;
		}
		pointer_lower_lifetime = std::min(subs[1].target_lower_lifetime, subs[2].target_lower_lifetime);
		pointer_upper_lifetime = std::max(subs[1].target_upper_lifetime, subs[2].target_upper_lifetime);


		// Emit merge block.
		TheFunction->getBasicBlockList().push_back(MergeBB);
		Builder.SetInsertPoint(MergeBB);
		llvm::PHINode *PN = Builder.CreatePHI(llvm::Type::getDoubleTy(thread_context), 2);

		PN->addIncoming(subs[1].llvm_Value, ThenBB);
		PN->addIncoming(subs[2].llvm_Value, ElseBB);
		return finish(resultant_type, PN, resultant_size);

	case AST("lock immutify"):
		does_target_require_dtors = 2;
		compile_superdependency_position(0);
		//todo: test if the offset's element is a lock. if so, simply change it to say "immut"
		Builder.CreateAtomicRMW(llvm::AtomicRMWInst::BinOp::Add, subs[0].llvm_Value, llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)), llvm::AtomicOrdering::Acquire);
		//todo
	case AST("dtor"):
		does_target_require_dtors = 1;
		return finish(empty_type, nullptr, 0);
	}


}


//run this function right before you, personally, actually emit any llvm code, and after any potential labels might exist.
//that means if any superdependency function emits llvm code, that's ok. as long as you're not the one emitting the code.
unsigned compiler_state::resolve_continuations()
{
	unsigned size_of_stack = this->temp_stack.size(); //note what the stack looks like now
	while (!continuations.empty())
	{
		AST* next_continuation = continuations.top();
		continuations.pop();
		auto try_continuation = AST_gen(next_continuation);
		if (!try_continuation) return try_continuation;
		clear_stack(size_of_stack);
	}
	if (!returns.empty())
		(*returns.top())(compile_state::continuation); //yield
	return unsigned(compile_state::no_error);
};
