#include <iostream>
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
#include "llvm/IR/Verifier.h"
#include "AST commands.h"
#include "llvm/PassManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Support/TargetSelect.h"

//todo: threading. create non-global contexts
#ifndef _MSC_VER
thread_local
#endif
llvm::LLVMContext& thread_context = llvm::getGlobalContext();

template<typename target_type>
union int_or_ptr
{
	uint64_t num;
	target_type* ptr;
	int_or_ptr(uint64_t x) : num(x) {}
	int_or_ptr(target_type* x) : ptr(x) {}
};
struct Object_Descriptor
{
	uint64_t size;
	Object_Descriptor(uint64_t s) : size(s) {}
};
bool operator==(const Object_Descriptor& lhs, const Object_Descriptor& rhs){ return lhs.size == rhs.size; }
Object_Descriptor T_uint64(1); //describes an integer, used internally. later we'll want more information than just its size
Object_Descriptor T_null(0); //describes nothing, used internally. later we'll want more information than just its size


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
	{} //VS complains about aggregate initialization, but I don't care
	//watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};

struct Type
{
	uint64_t size; //for now, it's just the number of uint64_t in an object. no complex objects allowed.
};

enum codegen_status {
	no_error,
	infinite_loop,
	active_object_duplication, //we tried to codegen an AST while it was already codegened from another branch, thus resulting in a stack duplication
	fell_through_switches, //if our switch case is missing a value. todo: add a unit test to check this.
	type_mismatch, //in an if statement, the two branches had different types.
	null_AST_compiler_bug, //we tried to generate_IR() a nullptr, which means generate_IR() is bugged
	null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
};

struct Return_Info
{
	codegen_status error_code;
	llvm::Value* llvm_IR_object;
	Object_Descriptor type;
	Return_Info(codegen_status err, llvm::Value* b, Object_Descriptor t)
		: error_code(err), llvm_IR_object(b), type(t) {}
};


struct compiler_object
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;
	llvm::ExecutionEngine* engine;

	std::unordered_set<AST*> loop_catcher; //lists the existing ASTs in a chain. goal is to prevent infinite loops.
	std::stack<AST*> object_stack; //it's just a stack for bookkeeping lifetimes. find the actual object information from the "objects" member.
	std::unordered_map<AST*, std::pair<llvm::Value*, Object_Descriptor>> objects;

	//these are the labels which we just passed by. you can jump to them without checking finiteness, but you still have to check the type stack
	std::stack<AST*> future_labels;

	compiler_object() : error_location(nullptr), Builder(thread_context)
	{
		TheModule = new llvm::Module("temp module", thread_context); //todo: memleak

		llvm::InitializeNativeTarget();
		llvm::InitializeNativeTargetAsmPrinter();
		llvm::InitializeNativeTargetAsmParser();

		std::string ErrStr;
		engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(TheModule))
			.setErrorStr(&ErrStr)
			.setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(
			new llvm::SectionMemoryManager))
			.create();
		if (!engine) {
			fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
			exit(1);
		}
	}

public:


	AST* error_location;
	//exists when codegen_status has an error.
	void clear_stack(uint64_t desired_stack_size);

	llvm::AllocaInst* CreateEntryBlockAlloca(uint64_t size);
	uint64_t get_size(AST* target);

	Return_Info generate_IR(AST* target, unsigned stack_degree);
	unsigned compile_AST(AST* target)
	{
		using namespace llvm;
		FunctionType *FT = FunctionType::get(llvm::Type::getInt64Ty(thread_context), false);
		Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

		BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
		Builder.SetInsertPoint(BB);

		auto return_object = generate_IR(target, false);
		if (return_object.error_code)
			return return_object.error_code; //error

		Builder.CreateRet(return_object.llvm_IR_object);

		// Validate the generated code, checking for consistency.
		llvm::verifyFunction(*F);

		TheModule->dump();
		/*
		auto *FPM = new llvm::FunctionPassManager(TheModule);
		Module::iterator it;
		Module::iterator end = TheModule->end();
		for (it = TheModule->begin(); it != end; ++it) {
			// Run the FPM on this function
			FPM->run(*it);
		}*/
		engine->finalizeObject();
		void* fptr = engine->getPointerToFunction(F);
		uint64_t(*FP)() = (uint64_t(*)())(intptr_t)fptr;
		fprintf(stderr, "Evaluated to %lu\n", FP());
		return 0;
	}
};


uint64_t compiler_object::get_size(AST* target)
{

	if (AST_vector[target->tag].size_of_return != -1) return AST_vector[target->tag].size_of_return;
	else if (target->tag == ASTn("if"))
	{
		//todo: do some type checking? we can defer that to compilation though.
		return get_size(target->fields[1].ptr);
	}
	std::cerr << "couldn't get size of tag " << target->tag;
	llvm_unreachable("panic here!");
}


//mem-to-reg only works on entry block variables because it's easier.
//maybe scalarrepl is more useful for us.
//clang likes to allocate everything in the beginning
llvm::AllocaInst* compiler_object::CreateEntryBlockAlloca(uint64_t size) {
	llvm::BasicBlock& first_block = Builder.GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());

	//we explicitly create the array type instead of allocating multiples, because that's what clang does for C++ arrays.
	llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
	if (size > 1)
	{
		llvm::Type* array_type = llvm::ArrayType::get(int64_type, size);
		return TmpB.CreateAlloca(array_type);
	}
	else
	{
		return TmpB.CreateAlloca(int64_type);
	}
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{

	while (object_stack.size() != desired_stack_size)
	{
		//todo: run dtors if necessary, such as for lock elements
		object_stack.pop();
	}
}


//generate llvm code. stack_degree measures how stacky the AST is. 0 = the return value won't arrive on the stack. 1 = the return value will arrive on the stack. 2 = the AST is an element in a basic block: it's not a field of another AST, so it's directly on the stack.
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree)
{
#define return_code(X) return Return_Info(codegen_status::X, nullptr, T_null);

	//maybe this ought to be checked beforehand.
	if (target == nullptr)
	{
		return_code(null_AST_compiler_bug);
	}

#ifdef trace_through_generate_IR
	TheModule->dump();
	std::cerr << "entering generate_IR with address " << target << " with tag " << target->tag << '\n';
	std::cerr << "fields are " << target->fields[0].num << ' ' << target->fields[1].num << ' ' << target->fields[2].num << ' ' << target->fields[3].num << '\n';
	std::cerr << "otherwise: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
#endif
	if (this->loop_catcher.find(target) != this->loop_catcher.end())
	{
		error_location = target;
		return_code(infinite_loop);
	}
	this->loop_catcher.insert(target);

	//this creates a dtor that takes care of loop_catcher removal.
	struct temp_RAII{
		//compiler_state can only be used if it is passed in.
		compiler_object* object; AST* targ;
		temp_RAII(compiler_object* x, AST* t) : object(x), targ(t) {}
		~temp_RAII() { object->loop_catcher.erase(targ); }
	} temp_object(this, target);


	//compile the previous elements in the basic block.
	if (target->preceding_BB_element)
	{
		auto previous_elements = generate_IR(target->preceding_BB_element, 2);
		if (previous_elements.error_code) return previous_elements;

	}//auto first_pointer = the_return_value;

	llvm::AllocaInst* storage_location = nullptr;
	uint64_t size_result = 0;
	uint64_t final_stack_position = object_stack.size();
	if (stack_degree == 2)
	{
		size_result = get_size(target);
		if (size_result) storage_location = CreateEntryBlockAlloca(size_result);
		//++final_stack_position;
	}

	//generated IR of the fields of the AST
	std::vector<llvm::Value*> field_results;

	auto generate_internal = [&](unsigned position)
	{
		Return_Info result(codegen_status::no_error, nullptr, T_null); //default
		if (target) result = generate_IR(target->fields[position].ptr, false); //todo: if it's concatenate(), this should be true.
		if (result.error_code) return result.error_code; //error
		field_results.push_back(result.llvm_IR_object);
		return codegen_status::no_error;
	};
	if (target->tag != ASTn("if")) //if statement requires special handling
	{

		for (unsigned x = 0; x < AST_vector[target->tag].number_of_AST_fields; ++x)
		{
			codegen_status result = generate_internal(x);
			if (result) return Return_Info(result, nullptr, T_null);

			//normally we'd also want to verify the types here. but we're assuming that every return value is a uint64 for now.
		}
	}

	//when we add a type system, we better start checking if types work out.
	llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);

	auto finish_internal = [&](llvm::Value* return_value, Object_Descriptor type) -> Return_Info
	{
		if (stack_degree == 2)
		{
			//the type of the stack object is an integer when 1 slot, or an array when multiple slots
			if (size_result > 1)
			{
				for (int i = 0; i < size_result; ++i)
					Builder.CreateStore(Builder.CreateConstGEP2_64(return_value, 0, i), Builder.CreateConstGEP2_64(storage_location, 0, i));
				return_value = storage_location;
			}
			else
			{
				Builder.CreateStore(return_value, storage_location);
				return_value = storage_location;
			}

			clear_stack(final_stack_position);

			object_stack.push(target);
			auto insert_result = this->objects.insert({ target, std::make_pair(return_value, type) });
			if (!insert_result.second) //collision: AST is already there
				return_code(active_object_duplication);

		}
		return Return_Info(codegen_status::no_error, return_value, type);
	};
#define finish(X, type) do { return finish_internal(X, type); } while (0)

	switch (target->tag)
	{
	case ASTn("static integer"):
	{
		finish(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].num)), T_uint64);
	}
	case ASTn("add"): //add two integers.
		finish(Builder.CreateAdd(field_results[0], field_results[1]), T_uint64);
	case ASTn("Hello World!"):
	{
		llvm::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!\n");

		//create the function type
		std::vector<llvm::Type *> putsArgs;
		putsArgs.push_back(Builder.getInt8Ty()->getPointerTo());
		llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);
		llvm::FunctionType *putsType =
			llvm::FunctionType::get(Builder.getInt32Ty(), argsRef, false);

		llvm::Constant *putsFunc = TheModule->getOrInsertFunction("puts", putsType);

		finish(Builder.CreateCall(putsFunc, helloWorld), T_null);
	}
	case ASTn("if"): //you can see the condition's return object in the branches.
		//otherwise, the condition is missing, which could be another if function that may be implemented later, but probably not.
		{
			//the condition statement. calls with stack_degree as 1 in the called function, if it is 2 outside.
			if (target->fields[0].ptr == nullptr)
				return_code(null_AST);
			auto condition = generate_IR(target->fields[0].ptr, (stack_degree != 0));
			if (condition.error_code) return condition;

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Value* comparison = Builder.CreateICmpNE(condition.llvm_IR_object, llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)));
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, "then", TheFunction);
			llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context, "else");
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context, "ifcont"); //todo: is this what we're returning? is there a better name?

			Builder.CreateCondBr(comparison, ThenBB, ElseBB);

			//first branch.
			Builder.SetInsertPoint(ThenBB); //WATCH OUT: this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are labels inside, we could be in big trouble.

			Return_Info then_IR(codegen_status::no_error, nullptr, T_null);
			if (target->fields[1].ptr != nullptr)
				then_IR = generate_IR(target->fields[1].ptr, false);
			if (then_IR.error_code) return then_IR;

			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			Builder.SetInsertPoint(ElseBB); //WATCH OUT: same here.

			Return_Info else_IR(codegen_status::no_error, nullptr, T_null);
			if (target->fields[2].ptr != nullptr)
				else_IR = generate_IR(target->fields[2].ptr, false);
			if (else_IR.error_code) return else_IR;

			//todo: we'll want a more sophisticated type checker later.
			if (!(then_IR.type == else_IR.type))
				return_code(type_mismatch);

			//for the second branch
			Builder.CreateBr(MergeBB);
			ElseBB = Builder.GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);
			llvm::PHINode *PN = Builder.CreatePHI(llvm::Type::getInt64Ty(thread_context), 2);

			PN->addIncoming(then_IR.llvm_IR_object, ThenBB);
			PN->addIncoming(else_IR.llvm_IR_object, ElseBB);
			return Return_Info(codegen_status::no_error, PN, then_IR.type);
		}
	case ASTn("scope"):
		finish(nullptr, T_null);
	}

	return Return_Info(codegen_status::fell_through_switches, nullptr, T_null);
}

int main()
{
	AST get1("static integer", nullptr, 1); //get the integer 1
	AST get2("static integer", nullptr, 2);
	AST get3("static integer", nullptr, 3);
	AST addthem("add", nullptr, &get1, &get2); //add integers 1 and 2


	AST getif("if", &addthem, &get1, &get2, &get3); //first, execute addthem. then, if get1 is non-zero, return get2. otherwise, return get3.
	AST helloworld("Hello World!", &getif); //first, execute getif. then output "Hello World!"
	compiler_object compiler;
	compiler.compile_AST(&helloworld);
}