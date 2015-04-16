#include <cstdint>
#include <iostream>
//#include <mutex>
#include <array>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Support/raw_ostream.h> 
#include "types.h"

#define OPTIMIZE 0
#define VERBOSE_DEBUG 0

//todo: threading. create non-global contexts
#ifndef _MSC_VER
thread_local
#endif
llvm::LLVMContext& thread_context = llvm::getGlobalContext();


#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::mt19937_64 mersenne(seed);
uint64_t generate_random()
{
	return mersenne();
}
Type* T_uint64 = new Type("integer"); //describes an integer type
Type* T_null = nullptr; //describes an object consisting of nothing


enum codegen_status {
	no_error,
	infinite_loop, //ASTs point to each other in a loop
	active_object_duplication, //tried to codegen an AST while it was already codegened from another branch, thus resulting in a stack duplication
	fell_through_switches, //our switch case is missing a value. generate_IR() is bugged
	type_mismatch, //in an if statement, the two branches had different types.
	null_AST_compiler_bug, //we tried to generate_IR() a nullptr, which means generate_IR() is bugged
	null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
	pointer_without_stack_target, //tried to get a pointer to an AST, but it was not found. either the AST was not compiled, or it was not placed on the stack.
};

struct Return_Info
{
	codegen_status error_code;
	llvm::Value* IR;
	Type* type;

	//this lifetime information is only used when a pointer is in the type. see pointers.txt
	//higher number = dies sooner
	//2^64 - 1 = temp. 0 = on the heap.
	uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a deletion time, not a creation time. these are not in perfect stack configuration, because temporaries are created before the base object, and die just after.
	uint64_t target_upper_lifetime; //higher is stricter. must last longer than this.
	//measures the pointer's target's lifetime, for when the pointer wants to be written into a memory location
	uint64_t target_lower_lifetime; //lower is stricter. must die faster than this.
	//measures the pointer's target's lifetime, for when the pointed-to memory location wants something to be written into it.
	//to write a pointer into a pointed-to memory location, we must have upper of pointer < lower of memory location
	//there's only one pair of values per object, which can be a problem for objects which concatenate many cheap pointers. but those objects can be split up because the only times we need concatenation is heap and parameters and function return; in these cases, the memory order doesn't matter

	Return_Info(codegen_status err, llvm::Value* b, Type* t, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(b), type(t), self_lifetime(s), target_upper_lifetime(u), target_lower_lifetime(l) {}

	//default constructor for a null object. make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(codegen_status::no_error), IR(nullptr), type(T_null), self_lifetime(0), target_upper_lifetime(0), target_lower_lifetime(-1ull) {}
};


struct compiler_object
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;
	llvm::ExecutionEngine* engine;

	std::unordered_set<AST*> loop_catcher; //lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::stack<AST*> object_stack; //a stack for bookkeeping lifetimes; keeps track of when objects are alive. find the actual object information from the "objects" member.
	std::unordered_map<AST*, Return_Info> objects; //from an AST pointer, find its generated IR and its return type.
	//object_stack and objects don't just keep track of objects that belong on the stack - they keep track of temporaries too.
	//the reason is that we want to be able to refer to these temporaries.

	//these are the labels which we just passed by. you can jump to them without checking finiteness, but you still have to check the type stack
	//std::stack<AST*> future_labels;

	std::deque<Type> type_scratch_space; //for storing temporary types. will be cleared at the end of compilation.
	//basically a memory pool. even though the constant impact of deque (memory) is bad for small compilations, it MUST be a deque so that memory locations stay valid after push_back().

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

	//to determine a pointer's lifetime, we get this incrementor just before looking at superdependencies to see where the pointer lives on the stack
	uint64_t incrementor_for_lifetime = 0;

public:


	AST* error_location;
	//exists when codegen_status has an error.
	void clear_stack(uint64_t desired_stack_size);

	llvm::AllocaInst* CreateEntryBlockAlloca(uint64_t size);
	uint64_t get_size(AST* target);

	Return_Info generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location = nullptr);

	unsigned compile_AST(AST* target) //we can't combine this with the ctor, because it needs to return an int
	{
		using namespace llvm;
		FunctionType *FT;

		auto size_of_return = get_size(target);
		if (size_of_return == 0)
			FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
		else if (size_of_return == 1)
			FT = FunctionType::get(llvm::Type::getInt64Ty(thread_context), false);
		else
			FT = FunctionType::get(llvm::ArrayType::get(llvm::Type::getInt64Ty(thread_context), size_of_return), false);
		Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

		BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
		Builder.SetInsertPoint(BB);

		auto return_object = generate_IR(target, false);
		if (return_object.error_code)
			return return_object.error_code; //error


		//todo: also relies on the dumb types
		if (size_of_return)
			Builder.CreateRet(return_object.IR);
		else Builder.CreateRetVoid();

		TheModule->dump();
		// Validate the generated code, checking for consistency.
		if (llvm::verifyFunction(*F, &llvm::outs())) std::cerr << "verification failed\n";

#if OPTIMIZE
		std::cerr << "optimized code: \n";
		//optimization
		FunctionPassManager FPM(TheModule);
		TheModule->setDataLayout(engine->getDataLayout());
		// Provide basic AliasAnalysis support for GVN.
		FPM.add(createBasicAliasAnalysisPass());
		// Do simple "peephole" optimizations and bit-twiddling optzns.
		FPM.add(createInstructionCombiningPass());
		// Reassociate expressions.
		FPM.add(createReassociatePass());
		// Eliminate Common SubExpressions.
		FPM.add(createGVNPass());
		// Simplify the control flow graph (deleting unreachable blocks, etc).
		FPM.add(createCFGSimplificationPass());

		// Promote allocas to registers.
		FPM.add(createPromoteMemoryToRegisterPass());
		// Do simple "peephole" optimizations and bit-twiddling optzns.
		FPM.add(createInstructionCombiningPass());
		// Reassociate expressions.
		FPM.add(createReassociatePass());

		FPM.doInitialization();
		FPM.run(*TheModule->begin());

		TheModule->dump();

#endif

		engine->finalizeObject();
		void* fptr = engine->getPointerToFunction(F);
		if (size_of_return)
		{
			uint64_t(*FP)() = (uint64_t(*)())(intptr_t)fptr;
			fprintf(stderr, "Evaluated to %lu\n", FP());
		}
		else
		{
			void(*FP)() = (void(*)())(intptr_t)fptr;
			FP();
		}
		return 0;
	}
};


uint64_t compiler_object::get_size(AST* target)
{
	if (target == nullptr)
		return 0; //for example, if you try to get the size of an if statement with nullptr fields as the return object.
	if (AST_descriptor[target->tag].size_of_return != -1) return AST_descriptor[target->tag].size_of_return;
	else if (target->tag == ASTn("if"))
	{
		//todo: do some type checking? we can defer that to compilation though.
		return get_size(target->fields[1].ptr);
	}
	std::cerr << "couldn't get size of tag " << target->tag;
	llvm_unreachable("panic here!");
}


//mem-to-reg only works on entry block variables, so we allocate all stack objects in the entry block.
//this function does that for us by building llvm::Allocas in the beginning
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

void output_AST(AST* target)
{
	std::cerr << "AST " //<< target << " with tag " <<
		<< AST_descriptor[target->tag].name << "(" << target->tag << "), Addr " << target << ", prev " << target->preceding_BB_element << '\n';
	//std::cerr << "fields are " << target->fields[0].num << ' ' << target->fields[1].num << ' ' << target->fields[2].num << ' ' << target->fields[3].num << '\n';
	std::cerr << "fields: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
}


void output_AST_and_previous(AST* target)
{
	output_AST(target);
	unsigned further_outputs = AST_descriptor[target->tag].pointer_fields;
	//I kind of want to use boost's irange here, but pulling in a big library may not be the best idea
	for (int x = 0; x < further_outputs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_AST_and_previous(target->fields[x].ptr);
}

//generate llvm code. stack_degree measures how stacky the AST is. 0 = the return value won't arrive on the stack. 1 = the return value will arrive on the stack. 2 = the AST is an element in a basic block: it's not a field of another AST, so it's directly on the stack.
//if stack_degree = 1, you should Store your final result in storaget_location. if stack_degree = 2, that means you should create your own storage_location instead.
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location)
{
	//return_code: an error has occurred. return the error code, and don't construct a return object.
#define return_code(X) do { error_location = target; return Return_Info(codegen_status::X, nullptr, T_null, 0, 0, 0); } while (0)

#if VERBOSE_DEBUG
	output_AST(target);
#endif

	//this should never happen, because we always check if a pointer is nullptr before calling generate_IR on it.
	if (target == nullptr) return_code(null_AST_compiler_bug);

#if VERBOSE_DEBUG
	TheModule->dump();
#endif

	if (this->loop_catcher.find(target) != this->loop_catcher.end())
	{
		error_location = target;
		return_code(infinite_loop);
	}
	this->loop_catcher.insert(target);

#if VERBOSE_DEBUG
	std::cerr << "got through loop catcher\n";
#endif

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

	}

	uint64_t lifetime_of_return_value = incrementor_for_lifetime++;

#if VERBOSE_DEBUG
	std::cerr << "got through previous elements\n";
#endif

	uint64_t size_result = 0;
	uint64_t final_stack_position = object_stack.size();
	if (stack_degree == 2)
	{
		size_result = get_size(target);
		if (size_result) storage_location = CreateEntryBlockAlloca(size_result);
		//++final_stack_position;
	}

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.

	if (target->tag != ASTn("if") && target->tag != ASTn("pointer")) //these statements require special handling. todo: concatenate also.
	{

		for (unsigned x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x)
		{
			Return_Info result; //default constructed for null object
			if (target->fields[x].ptr)
			{
				result = generate_IR(target->fields[x].ptr, false); //todo: if it's concatenate(), this would be true.
				if (result.error_code) return result; //error
			}
			field_results.push_back(result);
			//todo: normally we'd also want to verify the types here. 
		}
	}

	//when we add a type system, we better start checking if types work out.
	llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);

	//internally used. after you've constructed something, this stores it on the stack.
	auto move_into_stack_position = [&](llvm::Value* return_value)
	{
		//the type of the stack object is an integer when 1 slot, or an array when multiple slots
		if (size_result > 1)
		{
			for (int i = 0; i < size_result; ++i)
				Builder.CreateStore(Builder.CreateConstGEP2_64(return_value, 0, i), Builder.CreateConstGEP2_64(storage_location, 0, i));
			return_value = storage_location;
		}
		else if (size_result == 1)
		{
			Builder.CreateStore(return_value, storage_location);
			return_value = storage_location;
		}
	};

	//internally used. after you've constructed something, this handles stack logic such as stack clearing and making your result visible to other ASTs
	auto finish_internal = [&](llvm::Value* return_value, Type* type, uint64_t upper_life, uint64_t lower_life) -> Return_Info
	{
		if (stack_degree == 2)
			clear_stack(final_stack_position);

		if (stack_degree >= 1)
		{
			if (size_result >= 1)
			{
				object_stack.push(target);
				auto insert_result = objects.insert({ target, Return_Info(codegen_status::no_error, return_value, type, lifetime_of_return_value, upper_life, lower_life) });
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication);
			}
			type_scratch_space.push_back(Type("cheap pointer", type)); //stack objects are always pointers, just like in llvm.
			type = &type_scratch_space.back();
		}
		return Return_Info(codegen_status::no_error, return_value, type, lifetime_of_return_value, upper_life, lower_life);
	};

	//call these when you've constructed something. the p suffix is when you are returning a pointer, and need lifetime information.
#define finish(X, type) do { if (stack_degree >= 1) move_into_stack_position(X); return finish_internal(X, type, 0, -1ull); } while (0)
#define finishp(X, type, u, l) do { if (stack_degree >= 1) move_into_stack_position(X); return finish_internal(X, type, u, l); } while (0)
#define finish_previously_constructed(X, type) do { return finish_internal(X, type, 0, -1ull); } while (0)
#define finish_previously_constructedp(X, type, u, l) do { return finish_internal(X, type, u, l); } while (0)
//finish_previously_constructed is if your AST is not the one constructing the return object. for example, concatenate() and if() do not, so they should use this.

#if VERBOSE_DEBUG
	std::cerr << "tag is " << target->tag << '\n';
#endif
	switch (target->tag)
	{
	case ASTn("integer"):
	{
		finish(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].num)), T_uint64);
	}
	case ASTn("add"): //add two integers.
		//todo: why does this comparison work? we must replace it with type_check.
		if (type_check(RVO, field_results[0].type, T_uint64)) return_code(type_mismatch);
		if (type_check(RVO, field_results[1].type, T_uint64)) return_code(type_mismatch);
		finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR), T_uint64);
	case ASTn("subtract"):
		if (type_check(RVO, field_results[0].type, T_uint64)) return_code(type_mismatch);
		if (type_check(RVO, field_results[1].type, T_uint64)) return_code(type_mismatch);
		finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR), T_uint64);
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
	case ASTn("random"):
	{
		llvm::FunctionType *twister_type = llvm::FunctionType::get(int64_type, false);
		llvm::PointerType *twister_ptr_type = llvm::PointerType::getUnqual(twister_type);
		llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, (uint64_t)&generate_random));
		llvm::Value *twister_function = Builder.CreateBitOrPointerCast(twister_address, twister_ptr_type);
		finish(Builder.CreateCall(twister_function), T_uint64);
	}
	case ASTn("if"): //you can see the condition's return object in the branches.
		//otherwise, the condition is missing, which could be another if function that may be implemented later, but probably not.
		{
			//the condition statement
			if (target->fields[0].ptr == nullptr)
				return_code(null_AST);
			auto condition = generate_IR(target->fields[0].ptr, 0);
			if (condition.error_code) return condition;

			if (type_check(RVO, condition.type, T_uint64))
				return_code(type_mismatch);

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Value* comparison = Builder.CreateICmpNE(condition.IR, llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)));
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, "then", TheFunction);
			llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context, "else");
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context, "ifcont");

			Builder.CreateCondBr(comparison, ThenBB, ElseBB);

			//start inserting code in the "then" block
			Builder.SetInsertPoint(ThenBB); //WATCH OUT: this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty. might cause trouble when we want to make labels.
			//if there are labels inside, we could be in big trouble.

			//calls with stack_degree as 1 in the called function, if it is 2 outside.
			Return_Info then_IR; //default constructor for null object
			if (target->fields[1].ptr != nullptr)
				then_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
			if (then_IR.error_code) return then_IR;

			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			Builder.SetInsertPoint(ElseBB); //WATCH OUT: same here.

			Return_Info else_IR; //default constructor for null object
			if (target->fields[2].ptr != nullptr)
				else_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, storage_location);
			if (else_IR.error_code) return else_IR;

			//RVO, because that's what defines the slot.
			if (type_check(RVO, then_IR.type, else_IR.type))
				return_code(type_mismatch);

			//for the second branch
			Builder.CreateBr(MergeBB);
			ElseBB = Builder.GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);

			if (stack_degree == 0)
			{
				if (size_result == 0)
					return_code(no_error);

				llvm::PHINode *PN;
				if (size_result == 1)
					PN = Builder.CreatePHI(int64_type, 2);
				else
				{
					llvm::Type* array_type = llvm::ArrayType::get(int64_type, size_result);
					PN = Builder.CreatePHI(array_type, 2);
				}
				PN->addIncoming(then_IR.IR, ThenBB);
				PN->addIncoming(else_IR.IR, ElseBB);
				finishp(PN, then_IR.type, std::max(then_IR.target_upper_lifetime, else_IR.target_upper_lifetime), std::min(then_IR.target_lower_lifetime, else_IR.target_lower_lifetime));
			}
			finish_previously_constructedp(storage_location, then_IR.type, std::max(then_IR.target_upper_lifetime, else_IR.target_upper_lifetime), std::min(then_IR.target_lower_lifetime, else_IR.target_lower_lifetime));
	}
	case ASTn("scope"):
		finish(nullptr, T_null);
	case ASTn("pointer"):
		auto found_AST = objects.find(target->fields[0].ptr);
		if (found_AST == objects.end())
		{
			error_location = target;
			return_code(pointer_without_stack_target);
		}
		finishp(found_AST->second.IR, found_AST->second.type, found_AST->second.self_lifetime, found_AST->second.self_lifetime);
	/*case ASTn("get 0"):
		finish(llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)), T_uint64);*/
	}

	return_code(fell_through_switches);
}

/*
the fuzztester generates random ASTs, and attempts to compile them. some invalid ASTs will be rejected. some ASTs will succeed. this is normal.
however, segfaults are errors, and some error_codes indicate compiler errors. see codegen_status to see which errors are acceptable or not
to do this, fuzztester starts with a vector of pointers to working, compilable ASTs (originally just a nullptr).
then, it checks AST_descriptor[] for how many AST pointers it needs, then chooses these pointers randomly from the vector of working ASTs.
then, each remaining field has a 50-50 chance of either being a random number, or 0.
finally, it attempts to compile this created AST. if successful, it adds the created AST to the vector of working ASTs.
if successful, it tries again.
*/
void fuzztester(unsigned iterations)
{
	std::vector<AST*> AST_list{ nullptr }; //we start with nullptr as the default referenceable AST
	while (iterations--)
	{
		//create a random AST
		unsigned tag = mersenne() % ASTn("never reached");
		unsigned pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST pointers. they will come at the beginning
		unsigned prev_AST = mersenne() % AST_list.size();
		int_or_ptr<AST> fields[4]{nullptr, nullptr, nullptr, nullptr};
		int incrementor = 0;
		for (; incrementor < pointer_fields; ++incrementor)
			fields[incrementor] = AST_list.at(mersenne() % AST_list.size()); //get pointers to previous ASTs
		for (; incrementor < max_fields_in_AST; ++incrementor)
			fields[incrementor] = (mersenne() % 2) ? mersenne() : 0; //get random integers and fill in the remaining fields
		AST* test_AST= new AST(tag, AST_list.at(prev_AST), fields[0], fields[1], fields[2], fields[3]);
		output_AST_and_previous(test_AST);

		compiler_object compiler;
		unsigned error_code = compiler.compile_AST(test_AST);
		if (error_code)
		{
			std::cerr << "Malformed AST: code " << error_code << " at AST " << compiler.error_location << '\n';
			delete test_AST;
		}
		else
		{
			AST_list.push_back(test_AST);
			std::cerr << "success with ID " << AST_list.size() - 1 << "\n";
		}
		std::cerr << "Press enter to continue\n";
		std::cin.get();
	}
}

int main()
{
	/*
	AST get1("integer", nullptr, 1); //get the integer 1
	AST get2("integer", nullptr, 2);
	AST get3("integer", nullptr, 3);
	AST addthem("add", nullptr, &get1, &get2); //add integers 1 and 2


	AST getif("if", &addthem, &get1, &get2, &get3); //first, execute addthem. then, if get1 is non-zero, return get2. otherwise, return get3.
	AST helloworld("Hello World!", &getif); //first, execute getif. then output "Hello World!"
	compiler_object compiler;
	compiler.compile_AST(&helloworld);
	*/

	fuzztester(-1);
}