//compile with clang++ -g cs11.cpp `llvm-config --cppflags --libs core --ldflags --system-libs` -o toy -std=c++1y
//we need the packages libedit-dev and zlib1g-dev 
#include <cstdint>
//#include <mutex>
#include <array>
#include <stack>
#include <cassert>
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
#include "llvm/Analysis/Verifier.h"
#include "AST commands.h"

/*
structure: each AST has:
1. a tag, which says what kind of AST it is
2. 
uint64_t is my only object type.
*/

//todo: threading. create non-global contexts
#ifndef _MSC_VER
thread_local
#endif
llvm::LLVMContext& thread_context = llvm::getGlobalContext();

template<typename target_type>
union int_or_ptr
{
	uint64_t integer;
	target_type* pointer;
	int_or_ptr(uint64_t x) : integer(x) {}
	int_or_ptr(target_type* x) : pointer(x) {}
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
	std::array<int_or_ptr<AST>, max_fields_in_AST> fields;
	AST(const char name[], AST* preceding = nullptr, int_or_ptr<AST> f1 = nullptr, int_or_ptr<AST> f2 = nullptr, int_or_ptr<AST> f3 = nullptr, int_or_ptr<AST> f4 = nullptr)
		: tag(ASTn(name)), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 }
	{}//VS complains about aggregate initialization, but I don't care
	//watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};

struct Type
{
	uint64_t size; //for now, it's just the number of uint64_t in an object. no complex objects allowed.
};

namespace codegen_status
{
	enum {
		no_error,
		infinite_loop,
	};
};

struct Compile_Return_Value
{
	llvm::Value* llvm_IR_object;
	Compile_Return_Value(llvm::Value* b)
		: llvm_IR_object(b) {}
};


struct compiler_state
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;

	std::unordered_set<AST*> loop_catcher; //lists the existing ASTs in a chain. goal is to prevent infinite loops.

	//these are the labels which we just passed by. you can jump to them without checking finiteness, but you still have to check the type stack
	std::stack<AST*> future_labels;

	compiler_state() : error_location(nullptr), the_return_value(nullptr), Builder(thread_context)
	{
		TheModule = new llvm::Module("temp module", thread_context); //todo: memleak
	}

public:


	AST* error_location;
	Compile_Return_Value the_return_value;
	//exists when codegen_status == no_error. the purpose of this is so that we don't have to copy a return object over and over if we fail to compile


	llvm::AllocaInst* CreateEntryBlockAlloca(uint64_t size);

	unsigned generate_IR(AST* target, bool on_stack);
	unsigned compile_AST(AST* target)
	{
		using namespace llvm;
		FunctionType *FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
		Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

		BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
		Builder.SetInsertPoint(BB);

		if (generate_IR(target, false))
			return 1; //error
		Builder.CreateRetVoid();

		// Validate the generated code, checking for consistency.
		llvm::verifyFunction(*F);

		TheModule->dump();
		return 0;
	}
};


//generate llvm code. propre return value goes into Compile_Return_Value/error_location; the unsigned return is an error code.
unsigned compiler_state::generate_IR(AST* target, bool on_stack)
{
	if (target == nullptr)
	{
		the_return_value = nullptr;
		return codegen_status::no_error;
	}

	if (this->loop_catcher.find(target) != this->loop_catcher.end())
	{
		this->error_location = target;
		return codegen_status::infinite_loop;
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
	unsigned previous_elements = generate_IR(target->preceding_BB_element, true);
	if (!previous_elements) return previous_elements;
	//auto first_pointer = the_return_value;

	//storage for superdependency compilation
	std::vector<Compile_Return_Value> field_results;

	auto generate_internal = [&](unsigned position)
	{
		unsigned result = generate_IR(target->fields[position].pointer, false);
		if (!result) return result; //error
		field_results.push_back(the_return_value);
		return 0u;
	};
	if (target->tag != ASTn("if")) //if statement requires special handling
	{
		for (unsigned x = 0; x < AST_vector[target->tag].number_of_AST_fields; ++x)
		{
			unsigned result = generate_internal(x);
			if (result != codegen_status::no_error) return result;

			//normally we'd also want to verify the types here. but we're assuming that every return value is a uint64 for now.
		}
	}
	auto finish = [&](llvm::Value* produced_IR) -> uint64_t
	{
		the_return_value = Compile_Return_Value(produced_IR);
		return codegen_status::no_error;
	};


	switch (target->tag)
	{
	case ASTn("static integer"):
	{
		llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
		the_return_value = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].integer));
		return codegen_status::no_error;
	}
	case ASTn("add"): //add two integers.
		return finish(Builder.CreateAdd(field_results[0].llvm_IR_object, field_results[1].llvm_IR_object));
	case ASTn("label"):
		//remove the label from future_labels, because it's no longer in front of anything
		//assert(future_labels.top() == target, "ordering issue in generate_IR");
		future_labels.pop();
	case ASTn("if"): //you can see the condition's return object in the branches.
		//otherwise, the condition is missing, which could be another if function that may be implemented later, but probably not.
#ifdef _MSC_VER
		1; //intellisense autoindent won't work if the brace-open{ is right after a :
#endif
		{
			//the condition statement
			unsigned result = generate_internal(0);
			if (result != codegen_status::no_error) return result;
		}

		{
			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Value* comparison = Builder.CreateICmpNE(field_results[0].llvm_IR_object,
				llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)));
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, "", TheFunction);
			llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context);
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context); //todo: is this what we're returning? is there a better name?


			Builder.CreateCondBr(comparison, ThenBB, ElseBB);

			//first branch.
			Builder.SetInsertPoint(ThenBB);

			unsigned result = generate_internal(1);
			if (result != codegen_status::no_error) return result;

			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			Builder.SetInsertPoint(ElseBB);

			{
				unsigned result = generate_internal(2);
				if (result != codegen_status::no_error) return result;
			}

			//for the second branch
			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);
			llvm::PHINode *PN = Builder.CreatePHI(llvm::Type::getDoubleTy(thread_context), 2);

			PN->addIncoming(field_results[1].llvm_IR_object, ThenBB);
			PN->addIncoming(field_results[2].llvm_IR_object, ElseBB);
			return finish(PN);
		}
	}

	llvm_unreachable("should have returned before here");
}

int main()
{
	AST get1("static integer", 0, 3);
	AST get2("static integer", 0, 5);
	AST addthem("add", 0, &get1, &get2);
	compiler_state compiler;
	compiler.compile_AST(&addthem);
}