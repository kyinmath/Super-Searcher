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
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Support/raw_ostream.h> 
#include "types.h"

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = true;
bool CONSOLE = false;

//todo: threading. create non-global contexts
#ifndef _MSC_VER
thread_local
#endif
llvm::LLVMContext& thread_context = llvm::getGlobalContext();


#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::mt19937_64 mersenne(seed);
uint64_t generate_random() { return mersenne(); }

Type T_int_internal("integer");
Type T_nonexistent_internal("integer");
Type T_special_internal("integer");


enum codegen_status {
	no_error,
	infinite_loop, //ASTs have pointers in a loop
	active_object_duplication, //an AST has two return values active simultaneously, making the AST-to-object map ambiguous
	type_mismatch, //in an if statement, the two branches had different types.
	null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
	pointer_without_target, //tried to get a pointer to an AST, but it was not found at all
	pointer_to_temporary, //tried to get a pointer to an AST, but it was not placed on the stack.
	after_this_are_compiler_bugs, //the bugs before this are benign malformed AST errors.
	fell_through_switches, //our switch case is missing a value. generate_IR() is bugged
	null_AST_compiler_bug, //we tried to generate_IR() a nullptr, which means generate_IR() is bugged
};

struct Return_Info
{
	codegen_status error_code;

	llvm::Value* IR;
		
	Type* type;
	uint64_t size; //we memorize the size, so we don't have to recalculate it later.
	bool on_stack; //if the return value refers to an alloca.
	//in that case, the type is actually a pointer.

	//this lifetime information is only used when a cheap pointer is in the type. see pointers.txt
	//to write a pointer into a pointed-to memory location, we must have upper of pointer < lower of memory location
	//there's only one pair of target lifetime values per object, which can be a problem for objects which concatenate many cheap pointers.
	//however, we need concatenation only for heap objects, parameters, function return; in these cases, the memory order doesn't matter
	uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a deletion time, not a creation time.
	//it's important that it is a deletion time, because deletion and creation are not in perfect stack configuration.
	//because temporaries are created before the base object, and die just after.

	uint64_t target_upper_lifetime; //higher is stricter. the target must last longer than this.
	//measures the pointer's target's lifetime, for when the pointer wants to be written into a memory location

	uint64_t target_lower_lifetime; //lower is stricter. the target must die faster than this.
	//measures the pointer's target's lifetime, for when the pointed-to memory location wants something to be written into it.
	
	Return_Info(codegen_status err, llvm::Value* b, Type* t, uint64_t si, bool o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(b), type(t), size(si), on_stack(o), self_lifetime(s), target_upper_lifetime(u), target_lower_lifetime(l) {}

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(codegen_status::no_error), IR(nullptr), type(T_null), size(0), on_stack(false), self_lifetime(0), target_upper_lifetime(0), target_lower_lifetime(-1ull) {}
};

/** main compiler. holds state.
to compile an AST, default construct a compiler_object, and then run compile_AST() on the last AST in the main basic block.
this last AST is assumed to contain the return object.
*/



constexpr uint64_t get_size(Type* target)
{
	if (target == nullptr)
		return 0;
	if (type_descriptor[target->tag].size != -1) return type_descriptor[target->tag].size;
	else if (target->tag == Typen("concatenate"))
	{
		return get_size(target->fields[0].ptr) + get_size(target->fields[1].ptr);
	}
	std::cerr << "couldn't get size of type tag " << target->tag;
	llvm_unreachable("panic in get_size");
}


constexpr uint64_t get_size(AST* target)
{
	if (target == nullptr)
		return 0; //for example, if you try to get the size of an if statement with nullptr fields as the return object.
	if (AST_descriptor[target->tag].return_object != T_special) return get_size(AST_descriptor[target->tag].return_object);
	else if (target->tag == ASTn("if")) return get_size(target->fields[1].ptr);
	else if (target->tag == ASTn("pointer")) return 1;
	else if (target->tag == ASTn("copy")) return get_size(target->fields[0].ptr);
	else if (target->tag == ASTn("concatenate")) return get_size(target->fields[0].ptr) + get_size(target->fields[1].ptr);
	std::cerr << "couldn't get size of tag " << target->tag;
	llvm_unreachable("panic in get_size");
}


class compiler_object
{
	llvm::Module *TheModule;
	llvm::IRBuilder<> Builder;
	llvm::ExecutionEngine* engine;

	//lists the ASTs we're currently looking at. goal is to prevent infinite loops.
	std::unordered_set<AST*> loop_catcher;

	//a stack for bookkeeping lifetimes; keeps track of when objects are alive.
	std::stack<AST*> object_stack;

	//maps ASTs to their generated IR and return type.
	std::unordered_map<AST*, Return_Info> objects;

	//these are the labels which are later in the basic block. you can jump to them without checking finiteness, but you must check the type stack
	//std::stack<AST*> future_labels;

	std::deque<Type> type_scratch_space;
	//a memory pool for storing temporary types. will be cleared at the end of compilation.
	//it must be a deque so that its memory locations stay valid after push_back().
	//even though the constant impact of deque (memory) is bad for small compilations.

	//increases by 1 every time an object is created. imposes an ordering on stack object lifetimes.
	uint64_t incrementor_for_lifetime = 0;

	//some objects have expired - this clears them
	void clear_stack(uint64_t desired_stack_size);

	llvm::AllocaInst* create_alloca_in_entry_block(uint64_t size);

	Return_Info generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location = nullptr);

public:
	compiler_object();
	unsigned compile_AST(AST* target); //we can't combine this with the ctor, because it needs to return an int

	//exists when codegen_status has an error.
	AST* error_location;

};

compiler_object::compiler_object() : error_location(nullptr), Builder(thread_context)
{
	TheModule = new llvm::Module("temp module", thread_context);

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	std::string ErrStr;
	
	//todo: memleak with our memory manager
	engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(TheModule))
		.setErrorStr(&ErrStr)
		.setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(new llvm::SectionMemoryManager))
		.create();
	if (!engine) {
		std::cerr << "Could not create ExecutionEngine: " << ErrStr.c_str() << '\n';
		exit(1);
	}
}

unsigned compiler_object::compile_AST(AST* target)
{
	using namespace llvm;
	FunctionType *FT;

	auto size_of_return = get_size(target);
	if (size_of_return == 0) FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
	else if (size_of_return == 1) FT = FunctionType::get(llvm::Type::getInt64Ty(thread_context), false);
	else FT = FunctionType::get(llvm::ArrayType::get(llvm::Type::getInt64Ty(thread_context), size_of_return), false);

	Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

	BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
	Builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code)
		return return_object.error_code; //error

	if (size_of_return)
		Builder.CreateRet(return_object.IR);
	else Builder.CreateRetVoid();

	TheModule->dump();
	// Validate the generated code, checking for consistency.
	if (llvm::verifyFunction(*F, &llvm::outs()))
	{
		std::cerr << "\nverification failed\n";
		abort();
	}

	if (OPTIMIZE)
	{
		std::cerr << "optimized code: \n";
		llvm::legacy::FunctionPassManager FPM(TheModule);
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
	}

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


/** mem-to-reg only works on entry block variables.
thus, this function builds llvm::Allocas in the entry block.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
*/
llvm::AllocaInst* compiler_object::create_alloca_in_entry_block(uint64_t size) {
	if (size == 0)
		llvm_unreachable("tried to create a 0-size alloca");
	llvm::BasicBlock& first_block = Builder.GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());

	//we explicitly create the array type instead of allocating multiples, because that's what clang does for C++ arrays.
	llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
	if (size > 1) return TmpB.CreateAlloca(llvm::ArrayType::get(int64_type, size));
	else return TmpB.CreateAlloca(int64_type);
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
	std::cerr << "AST " << AST_descriptor[target->tag].name << "(" << target->tag << "), Addr " << target <<
		", prev " << target->preceding_BB_element << '\n';
	std::cerr << "fields: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
}


void output_AST_and_previous(AST* target)
{
	output_AST(target);
	if (target->preceding_BB_element != nullptr)
		output_AST_and_previous(target->preceding_BB_element);
	unsigned number_of_further_ASTs = AST_descriptor[target->tag].pointer_fields;
	//boost::irange may be useful, but pulling in unnecessary (possibly-buggy) code is bad
	for (int x = 0; x < number_of_further_ASTs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_AST_and_previous(target->fields[x].ptr);
}

void output_type(Type* target)
{
	std::cerr << "type " << type_descriptor[target->tag].name << "(" << target->tag << "), Addr " << target << "\n";
	std::cerr << "fields: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << '\n';
}


void output_type_and_previous(Type* target)
{
	output_type(target);
	unsigned further_outputs = type_descriptor[target->tag].pointer_fields;
	//I kind of want to use boost's irange here, but pulling in a big library may not be the best idea
	for (int x = 0; x < further_outputs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_type_and_previous(target->fields[x].ptr);
}
/** generate_IR() is the main code that transforms AST into LLVM IR. it is called with the AST to be transformed, which must not be nullptr.

ASTs that are directly inside basic blocks should allocate their own stack_memory, so they are given stack_degree = 2.
	this tells them to create a memory location and to place the return value inside it.
ASTs that should place their return object inside an already-created memory location are given stack_degree = 1, and storage_location.
	then, they store the return value into storage_location
ASTs that return SSA objects are given stack_degree = 0.

create_alloca_in_entry_block() is used to create the memory locations.
*/
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
#define return_code(X) do { error_location = target; return Return_Info(codegen_status::X, nullptr, T_null, 0, false, 0, 0, 0); } while (0)
	if (VERBOSE_DEBUG)
	{
		std::cout << "starting generate_IR\n";
		output_AST(target);
		TheModule->dump();
	}

	//target should never be nullptr.
	if (target == nullptr) return_code(null_AST_compiler_bug);


	//if we've seen this AST before, we're stuck in an infinite loop
	if (this->loop_catcher.find(target) != this->loop_catcher.end())
		return_code(infinite_loop);
	loop_catcher.insert(target); //we've seen this AST now.

	//after we're done with this AST, we remove it from loop_catcher.
	struct loop_catcher_destructor_cleanup{
		//compiler_state can only be used if it is passed in.
		compiler_object* object; AST* targ;
		loop_catcher_destructor_cleanup(compiler_object* x, AST* t) : object(x), targ(t) {}
		~loop_catcher_destructor_cleanup() { object->loop_catcher.erase(targ); }
	} temp_object(this, target);

	//compile the previous elements in the basic block.
	if (target->preceding_BB_element)
	{
		auto previous_elements = generate_IR(target->preceding_BB_element, 2);
		if (previous_elements.error_code) return previous_elements;
	}

	//after compiling the previous elements in the basic block, we find the lifetime of this element
	uint64_t lifetime_of_return_value = incrementor_for_lifetime++;

	uint64_t size_result = 0;
	uint64_t final_stack_position = object_stack.size();
	if (stack_degree == 2)
	{
		size_result = get_size(target);
		if (size_result) storage_location = create_alloca_in_entry_block(size_result);
	}

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.

	//these statements require special handling. todo: concatenate also.
	if (target->tag != ASTn("if") && target->tag != ASTn("pointer") && target->tag != ASTn("copy"))
	{

		for (unsigned x = 0; x < AST_descriptor[target->tag].fields_to_compile; ++x)
		{
			Return_Info result; //default constructed for null object
			if (target->fields[x].ptr)
			{
				result = generate_IR(target->fields[x].ptr, false);
				if (result.error_code) return result;
				if (AST_descriptor[target->tag].parameter_types[x] != T_nonexistent)
					if (type_check(RVO, result.type, AST_descriptor[target->tag].parameter_types[x])) return_code(type_mismatch);
			}
			field_results.push_back(result);
			//todo: normally we'd also want to verify the types here. 
		}
	}

	//when we add a type system, we better start checking if types work out.
	llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);

	//internal: do not call this directly. use the finish macro instead
	//places a constructed object into storage_location.
	auto move_to_stack = [&](llvm::Value* return_value) -> llvm::Value*
	{
		if (size_result > 1)
		{
			for (int i = 0; i < size_result; ++i)
				Builder.CreateStore(Builder.CreateConstGEP2_64(return_value, 0, i), Builder.CreateConstGEP2_64(storage_location, 0, i));
			return storage_location;
		}
		else if (size_result == 1)
		{
			Builder.CreateStore(return_value, storage_location);
			return storage_location;
		}
		return return_value;
	};

	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	auto finish_internal = [&](llvm::Value* return_value, Type* type, uint64_t upper_life, uint64_t lower_life) -> Return_Info
	{
		if (VERBOSE_DEBUG)
		{
			std::cerr << "finishing IR generation, called value is";
			return_value->print(llvm::outs());
			std::cerr << "\nand the llvm type is ";
			return_value->getType()->print(llvm::outs());
			std::cerr << "\nand the internal type is ";
			output_type_and_previous(type);
		}
		if (stack_degree == 2)
			clear_stack(final_stack_position);

		if (stack_degree >= 1)
		{
			if (size_result >= 1)
			{
				object_stack.push(target);
				auto insert_result = objects.insert({ target, Return_Info(codegen_status::no_error, return_value, type, size_result, true, lifetime_of_return_value, upper_life, lower_life) });
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication);
			}
			type_scratch_space.push_back(Type("cheap pointer", type)); //stack objects are always pointers, just like in llvm.
			type = &type_scratch_space.back();
		}
		return Return_Info(codegen_status::no_error, return_value, type, size_result, false, lifetime_of_return_value, upper_life, lower_life);
	};

	//call these when you've constructed something. the _pointer suffix is when you are returning a pointer, and need lifetime information.
	//use finish_previously_constructed if your AST is not the one constructing the return object.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.

#define finish(X, type) do { llvm::Value* end_value = stack_degree >= 1 ? move_to_stack(X) : X; return finish_internal(end_value, type, 0, -1ull); } while (0)
#define finish_pointer(X, type, u, l) do { if (stack_degree >= 1) move_to_stack(X); return finish_internal(X, type, u, l); } while (0)
#define finish_previously_constructed(X, type) do { return finish_internal(X, type, 0, -1ull); } while (0)
#define finish_previously_constructed_pointer(X, type, u, l) do { return finish_internal(X, type, u, l); } while (0)

	//we generate code for the AST, depending on its tag.
	switch (target->tag)
	{
	case ASTn("integer"):
	{
		finish(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].num)), T_int);
	}
	case ASTn("add"): //add two integers.
		finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR), T_int);
	case ASTn("subtract"):
		finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR), T_int);
	case ASTn("Hello World!"):
	{
		llvm::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!\n");

		//create the function type
		std::vector<llvm::Type *> putsArgs;
		putsArgs.push_back(Builder.getInt8Ty()->getPointerTo());
		llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);
		llvm::FunctionType *putsType = llvm::FunctionType::get(Builder.getInt32Ty(), argsRef, false);

		//get the actual function
		llvm::Constant *putsFunc = TheModule->getOrInsertFunction("puts", putsType);

		finish(Builder.CreateCall(putsFunc, helloWorld), T_null);
	}
	case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
	{
		llvm::FunctionType *twister_type = llvm::FunctionType::get(int64_type, false);
		llvm::PointerType *twister_ptr_type = llvm::PointerType::getUnqual(twister_type);
		llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, (uint64_t)&generate_random));
		llvm::Value *twister_function = Builder.CreateBitOrPointerCast(twister_address, twister_ptr_type);
		finish(Builder.CreateCall(twister_function), T_int);
	}
	case ASTn("if"): //todo: you can see the condition's return object in the branches.
		//we could have another version where the condition's return object is invisible.
		//this lets us goto the inside of the if statement.
		{
			//the condition statement
			if (target->fields[0].ptr == nullptr)
				return_code(null_AST);
			auto condition = generate_IR(target->fields[0].ptr, 0);
			if (condition.error_code) return condition;

			if (type_check(RVO, condition.type, T_int))
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
			//WATCH OUT: this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are already labels inside the "Then" basic block, we could be in big trouble.
			Builder.SetInsertPoint(ThenBB);

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

			uint64_t result_target_upper_lifetime = std::max(then_IR.target_upper_lifetime, else_IR.target_upper_lifetime);
			uint64_t result_target_lower_lifetime = std::min(then_IR.target_lower_lifetime, else_IR.target_lower_lifetime);
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
				finish_pointer(PN, then_IR.type, result_target_upper_lifetime, result_target_lower_lifetime);
			}
			finish_previously_constructed_pointer(storage_location, then_IR.type, result_target_upper_lifetime, result_target_lower_lifetime);
	}
	case ASTn("scope"):
		finish(nullptr, T_null);
	case ASTn("pointer"):
	{
		auto found_AST = objects.find(target->fields[0].ptr);
		if (found_AST == objects.end()) return_code(pointer_without_target);
		if (found_AST->second.on_stack == false) return_code(pointer_to_temporary);
		//our new pointer type
		type_scratch_space.push_back(Type("cheap pointer", found_AST->second.type));
		finish_pointer(found_AST->second.IR, &type_scratch_space.back(), found_AST->second.self_lifetime, found_AST->second.self_lifetime);
	}
	case ASTn("copy"):
	{
		auto found_AST = objects.find(target->fields[0].ptr);
		if (found_AST == objects.end())
			return_code(pointer_without_target);
		if (found_AST->second.on_stack == false) //it's not an AllocaInst
			finish_pointer(found_AST->second.IR, found_AST->second.type, found_AST->second.self_lifetime, found_AST->second.self_lifetime);
		else finish(Builder.CreateLoad(found_AST->second.IR), found_AST->second.type);
	}
	/*case ASTn("get 0"):
		finish(llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)), T_int);*/
	}

	return_code(fell_through_switches);
}

/**
The fuzztester generates random ASTs and attempts to compile them.
"Malformed AST" is fine. not all randomly-generated ASTs will be well-formed.
However, "ERROR" is not fine, and means there is a bug.

fuzztester starts with a vector of pointers to working, compilable ASTs (originally just a nullptr).
it chooses a random AST tag. this tag requires AST_descriptor[tag].pointer_fields pointers
then, it chooses pointers randomly from the vector of working ASTs.
these pointers go in the first few fields.
each remaining field has a 50-50 chance of either being a random number, or 0.
finally, if the created AST successfully compiles, it is added to the vector of working ASTs.
*/
void fuzztester(unsigned iterations)
{
	std::vector<AST*> AST_list{ nullptr }; //start with nullptr as the default referenceable AST
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
			if (error_code > codegen_status::after_this_are_compiler_bugs)
			{
				std::cerr << "ERROR: code " << error_code << " at AST " << compiler.error_location << '\n';
				llvm_unreachable("fuzztester detected compiler error");
			}
			else
			{
				std::cerr << "Malformed AST: code " << error_code << " at AST " << compiler.error_location << '\n';
				delete test_AST;
			}
		}
		else
		{
			AST_list.push_back(test_AST);
			std::cerr << "Successful compile " << AST_list.size() - 1 << "\n";
		}
		if (INTERACTIVE)
		{
			std::cerr << "Press enter to continue\n";
			std::cin.get();
		}
	}
}

#include <iostream>
#include <sstream>

#include <istream>
class source_reader
{
	//we need to implement this.
	std::unordered_map<std::string, AST*> ASTmap; //from name to location
	std::istream& input;
	char ending_char;
	//call this after you've found and consumed a [.
	//doesn't get the type name. you'll have to find that out yourself.
	AST* read_single_AST(AST* previous_AST)
	{
		std::string word;
		do {
			word += input.peek();
			input.ignore(1);
		} while (input.peek() != ',' && input);
		uint64_t AST_type = ASTn(word.c_str());
		uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;

		int field_num = 0; //which field we're on
		int_or_ptr<AST> fields[max_fields_in_AST] = { nullptr, nullptr, nullptr, nullptr };
		while (input.peek() != ']')
		{
			if (field_num >= max_fields_in_AST)
			{
				std::cerr << "more fields than possible\n";
				abort();
			}
			while (input.peek() == ' ') input.ignore(1);
			if (input.peek() == '[')
			{
				input.ignore(1);
				read_single_AST(nullptr);
			}
			else if (field_num < pointer_fields) //it's a pointer!
			{
				std::string ASTname;
				while (input.peek() != ',')
				{
					ASTname.push_back(input.peek());
					input.ignore();
				}
				fields[field_num] = ASTmap.find(ASTname)->second;
			}
			else if (!(input >> fields[field_num].num))
			{
				std::cerr << "reading integer failed\n";
				abort();
			}
			++field_num;
		}
		input.ignore(1); //consume ']'
		AST* new_type = new AST(AST_type, previous_AST, fields[0], fields[1], fields[2], fields[3]);

		std::string thisASTname;
		//get an AST name if any.
		while (input.peek() != '[' && input.peek() != ' ' && input.peek() != ending_char)
		{
			thisASTname.push_back(input.peek());
			input.ignore();
		}
		if (!thisASTname.empty())
			ASTmap.insert(std::make_pair(thisASTname, new_type));

		return new_type;

	}


public:
	source_reader(std::istream& stream, char end) : input(stream), ending_char(end) {}
	AST* create_single_basic_block()
	{
		AST* current_previous;
		while (input.peek() != ending_char) current_previous = read_single_AST(current_previous);
		return current_previous; //which is the very last.
	}
};

int main(int argc, char* argv[])
{
	for (int x = 1; x < argc; ++x)
	{
		if (strcmp(argv[x], "noninteractive") == 0)
			INTERACTIVE = false;
		if (strcmp(argv[x], "verbose") == 0)
			VERBOSE_DEBUG = true;
		if (strcmp(argv[x], "optimize") == 0)
			OPTIMIZE = true;
		if (strcmp(argv[x], "console") == 0)
			CONSOLE = true;
	}
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

	if (CONSOLE)
	{
		source_reader k(std::cin, '\n');
		while (1)
		{
			AST* end = k.create_single_basic_block();
			std::cin.ignore(1); //consume '\n'
			compiler_object j;
			j.compile_AST(end);
		}
	}

	fuzztester(-1);
}