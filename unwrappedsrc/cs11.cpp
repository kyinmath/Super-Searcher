/* see README.md for how to run this program.
Functions which create ASTs:
main() handles the commandline arguments. if console input is requested, it will handle input too. however, it won't parse the input.
fuzztester() generates random, possibly malformed ASTs.
for console input, read_single_AST() parses a single AST and its field's ASTs, then creates a tree of ASTs out of it.
	then, create_single_basic_block() parses a list of ASTs, then chains them in a linked list using AST.preceding_BB_element

Things which compile and run ASTs:
compiler_object holds state for a compilation. you make a new compiler_state for each compilation, and call compile_AST() on the last AST in your function. compile_AST automatically wraps the AST in a function and runs it; the last AST is assumed to hold the return object.
compile_AST() is mainly a wrapper. it generates the llvm::Functions and llvm::Modules that contain the main code. it calls generate_IR() to do the main work.
generate_IR() is the main AST conversion tool. it turns ASTs into llvm::Values, recursively. these llvm::Values are automatically inserted into a llvm::Module.
	to do this, generate_IR first runs itself on a target's dependencies. then, it looks at the target's tag, and then uses a switch-case to determine how it should use those dependencies.
*/
#include <cstdint>
#include <iostream>
#include <array>
#include <stack>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Support/raw_ostream.h> 
#include "types.h"
#include "debugoutput.h"
#include "helperfunctions.h"
#include "unique_type_creator.h"
#include "user_facing_functions.h"
#include "cs11.h"

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = false;
bool CONSOLE = false;
bool TIMER = false;
bool OLD_AST_OUTPUT = false;
const uint64_t FINITENESS_LIMIT = 10;

basic_onullstream<char> null_stream;
std::ostream& outstream = std::cerr;
llvm::raw_null_ostream llvm_null_stream;
llvm::raw_ostream* llvm_outstream = &llvm::outs();


//todo: threading. create non-global contexts
thread_local llvm::LLVMContext& thread_context = llvm::getGlobalContext();
thread_local uint64_t finiteness;

#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::mt19937_64 mersenne(seed); //this should probably be thread_local
uint64_t generate_random() { return mersenne(); }

//generates an approximately exponential distribution using bit twiddling
uint64_t generate_exponential_dist()
{
	uint64_t cutoff = generate_random();
	cutoff ^= cutoff - 1; //1s starting from the lowest digit of cutoff
	return generate_random() & cutoff;
}



compiler_object::compiler_object() : error_location(nullptr), Builder(thread_context),
TheModule(new llvm::Module("temp module", thread_context))
{
	if (VERBOSE_DEBUG) outstream << "creating compiler object\n";

	std::string ErrStr;
	
	//todo: memleak with our memory manager
	engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(TheModule))
		.setErrorStr(&ErrStr)
		.setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(new llvm::SectionMemoryManager))
		.create();
	check(engine, "Could not create ExecutionEngine: " + ErrStr + "\n"); //check engine! :D
}

template<size_t array_num> void cout_array(std::array<uint64_t, array_num> object)
{
	outstream << "Evaluated to";
	for (int x = 0; x < array_num; ++x) outstream << ' ' << object[x];
	outstream << '\n';
}

//return value is the error code, which is 0 if successful
unsigned compiler_object::compile_AST(AST* target)
{

	if (VERBOSE_DEBUG)
	{
		outstream << "starting compilation\n";
		outstream << "target is " << target << '\n'; //necessary in case it crashes here
	}
	if (target == nullptr)
	{
		return IRgen_status::null_AST;
	}
	using namespace llvm;
	FunctionType *FT;

	auto size_of_return = get_size(target);
	if (size_of_return == 0) FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
	else if (size_of_return == 1) FT = FunctionType::get(int64_type, false);
	else FT = FunctionType::get(llvm_array(size_of_return), false);
	if (VERBOSE_DEBUG) outstream << "Size of return is " << size_of_return << '\n';

	Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

	BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
	Builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code) return return_object.error_code; //error

	return_type = get_unique_type(return_object.type, false);
	if (size_of_return) Builder.CreateRet(return_object.IR);
	else Builder.CreateRetVoid();

	TheModule->print(*llvm_outstream, nullptr);
	debugcheck(!llvm::verifyFunction(*F, llvm_outstream), "verification failed");
	if (OPTIMIZE)
	{
		outstream << "optimized code: \n";
		llvm::legacy::FunctionPassManager FPM(TheModule);
		TheModule->setDataLayout(engine->getDataLayout());

		FPM.add(createBasicAliasAnalysisPass()); //Provide basic AliasAnalysis support for GVN.
		FPM.add(createInstructionCombiningPass()); //Do simple "peephole" optimizations and bit-twiddling optzns.
		FPM.add(createReassociatePass()); //Reassociate expressions.
		FPM.add(createGVNPass()); //Eliminate Common SubExpressions.
		FPM.add(createCFGSimplificationPass()); //Simplify the control flow graph (deleting unreachable blocks, etc).
		FPM.add(createPromoteMemoryToRegisterPass()); //Promote allocas to registers.
		FPM.add(createInstructionCombiningPass()); //Do simple "peephole" optimizations and bit-twiddling optzns.
		FPM.add(createReassociatePass()); //Reassociate expressions.
		FPM.add(createScalarReplAggregatesPass()); //I added this.

		FPM.doInitialization();
		FPM.run(*TheModule->begin());

		TheModule->print(*llvm_outstream, nullptr);
	}

	if (VERBOSE_DEBUG) outstream << "finalizing object...\n";
	engine->finalizeObject();
	void* fptr = engine->getPointerToFunction(F);
	if (VERBOSE_DEBUG) outstream << "running function:\n";
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		outstream <<  "Evaluated to " << FP() << '\n';
	}
	else if (size_of_return > 1)
	{
		using std::array;
		switch (size_of_return)
		{
		case 2: cout_array(((array<uint64_t, 2>(*)())(intptr_t)fptr)()); break; //theoretically, this ought to break. array<2> = {int, int}
		case 3: cout_array(((array<uint64_t, 3>(*)())(intptr_t)fptr)()); break;
		case 4: cout_array(((array<uint64_t, 4>(*)())(intptr_t)fptr)()); break;
		case 5: cout_array(((array<uint64_t, 5>(*)())(intptr_t)fptr)()); break;
		case 6: cout_array(((array<uint64_t, 6>(*)())(intptr_t)fptr)()); break;
		case 7: cout_array(((array<uint64_t, 7>(*)())(intptr_t)fptr)()); break;
		case 8: cout_array(((array<uint64_t, 8>(*)())(intptr_t)fptr)()); break;
		case 9: cout_array(((array<uint64_t, 9>(*)())(intptr_t)fptr)()); break;
		default: outstream << "function return size is " << size_of_return << " and C++ seems to only take static return types\n"; break;

		}
	}
	else
	{
		void(*FP)() = (void(*)())(intptr_t)fptr;
		FP();
	}
	return 0;
}


/** mem-to-reg only works on entry block variables.
thus, this function builds llvm::Allocas in the entry block. it should be preferred over trying to create allocas directly.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
we call this "create_alloca" instead of "create_alloca_in_entry_block", because it's the general alloca mechanism. if we said, "in_entry_block", then the user would be confused as to when to use this. by not having that, it's clear that this should be the default.
*/
llvm::AllocaInst* compiler_object::create_alloca(uint64_t size) {
	debugcheck(size > 0, "tried to create a 0-size alloca");
	llvm::BasicBlock& first_block = Builder.GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());

	//we explicitly create the array type instead of allocating multiples, because that's what clang does for C++ arrays.
	if (size > 1) return TmpB.CreateAlloca(llvm_array(size));
	else return TmpB.CreateAlloca(int64_type);
}

void compiler_object::emit_dtors(uint64_t desired_stack_size)
{
	//todo
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{
	while (object_stack.size() != desired_stack_size)
	{
		object_stack.pop_back();
	}
}

/** generate_IR() is the main code that transforms AST into LLVM IR. it is called with the AST to be transformed, which must not be nullptr.
'
storage_location is for RVO. if stack_degree says that the return object should be stored somewhere, the location will be storage_location. storage is done by the returning function.
ASTs that are directly inside basic blocks should allocate their own stack_memory, so they are given stack_degree = 2.
	this tells them to create a memory location and to place the return value inside it. the memory location is returned.
ASTs that should place their return object inside an already-created memory location are given stack_degree = 1, and storage_location.
	then, they store the return value into storage_location. storage_location is returned.
ASTs that return SSA objects are given stack_degree = 0.
	minor note: we use create_alloca() so that the llvm optimization pass mem2reg can recognize it.

Steps of generate_IR:
check if generate_IR will be in an infinite loop, by using loop_catcher to store the previously seen ASTs.
run generate_IR on the previous AST in the basic block, if such an AST exists.
	after this previous AST is generated, we can figure out where the current AST lives on the stack. this uses incrementor_for_lifetime.
find out what the size of the return object is, using get_size()
generate_IR is run on fields of the AST. it knows how many fields are needed by using fields_to_compile from AST_descriptor[] 
then, the main IR generation is done using a switch-case on a tag.
the return value is created using the finish() macros. these automatically pull in any miscellaneous necessary information, such as stack lifetimes, and bundles them into a Return_Info. furthermore, if the return value needs to be stored in a stack location, finish() does this automatically using the move_to_stack lambda.

note that while we have a rich type system, the only types that llvm sees are int64s and arrays of int64s. pointers, locks, etc. have their information stripped, so llvm only sees the size of each object. we forcefully cast things to integers.
	for an object of size 1, its llvm-type is an integer
	for an object of size N>=2, the llvm-type is an alloca'd array of N integers, which is [12 x i64]*. note the * - it's a pointer to an array, not the array itself.
example: the llvm::Value* of an object that had stack_degree = 2 is a pointer to the memory location, where the pointer is casted to an integer.
currently, the finish() macro takes in the array itself, not the pointer-to-array.
*/
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
#define return_code(X, Y) do { error_location = target; error_field = Y; return Return_Info(IRgen_status::X, nullptr, T::null, false, 0, 0, 0); } while (0)


	if (VERBOSE_DEBUG)
	{
		outstream << "generate_IR single AST start ^^^^^^^^^\n";
		if (target) output_AST(target);
		outstream << "stack degree " << stack_degree;
		outstream << ", storage location is ";
		if (storage_location) storage_location->print(*llvm_outstream);
		else outstream << "null\n";
	}

	if (stack_degree == 2) debugcheck(storage_location == nullptr, "if stack degree is 2, we should have a nullptr storage_location");
	if (stack_degree == 1) debugcheck((storage_location != nullptr) || (get_size(target) == 0), "if stack degree is 1, we should have a storage_location");

	//I decided to let generate_IR take nullptr. because if it cannot, we need an extra check beforehand. this extra check creates code duplication, which leads to typos when indices aren't changed.
	//debugcheck(target != nullptr, "generate_IR should never receive a nullptr target");
	if (target == nullptr) return Return_Info();

	//if we've seen this AST before, we're stuck in an infinite loop. return an error.
	if (this->loop_catcher.find(target) != this->loop_catcher.end()) return_code(infinite_loop, 10);
	loop_catcher.insert(target); //we've seen this AST now.

	//after we're done with this AST, we remove it from loop_catcher.
	struct loop_catcher_destructor_cleanup
	{
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

	uint64_t size_result = -1; //note: it's only active when stack_degree = 1. otherwise, you must have special cases.
	//-1 is a debug choice, since having it at 0 led to invisible errors.
	//note that it's -1, not -1ull.
	//whenever you use create_alloca(), make sure size_result is actually set.

	uint64_t final_stack_position = object_stack.size();


	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.

	for (unsigned x = 0; x < AST_descriptor[target->tag].fields_to_compile; ++x)
	{
		Return_Info result; //default constructed for null object
		if (target->fields[x].ptr)
		{
			result = generate_IR(target->fields[x].ptr, false);
			if (result.error_code) return result;
		}

		if (VERBOSE_DEBUG)
		{
			outstream << "type checking. result type is \n";
			output_type_and_previous(result.type);
			outstream << "desired type is \n";
			output_type_and_previous(AST_descriptor[target->tag].parameter_types[x]);
		}
		//check that the type matches.
		if (AST_descriptor[target->tag].parameter_types[x] != T::parameter_no_type_check)
			if (AST_descriptor[target->tag].parameter_types[x] != T::missing_field)
				if (type_check(RVO, result.type, AST_descriptor[target->tag].parameter_types[x]) != 3) return_code(type_mismatch, x);

		field_results.push_back(result);
	}


	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	//if it's necessary to create and write to storage_location, we do so.
	//if move_to_stack == true, it writes into a previously created storage_location
	auto finish_internal = [&](llvm::Value* return_value, Type* type, uint64_t upper_life, uint64_t lower_life, bool move_to_stack) -> Return_Info
	{
		check(type != T::special, "didn't specify return type when necessary");
		if (stack_degree >= 1 && size_result == -1) size_result = get_size(target); //note that it's -1, not -1ull.
		if (stack_degree == 2 && size_result != 0 && storage_location == nullptr)
		{
			if (storage_location == nullptr)
			{
				storage_location = create_alloca(size_result);
				Builder.CreateStore(return_value, storage_location);
				return_value = storage_location;
			}
			else debugcheck(move_to_stack == false, "if move_to_stack is true, then we shouldn't have already created the storage_location");
		}
		if (stack_degree == 1 && move_to_stack)
		{
			if (size_result >= 1) //we don't do it for stack_degree = 2, because if storage_location =/= nullptr, we shouldn't write
			{
				Builder.CreateStore(return_value, storage_location);
				return_value = storage_location;
			}
		}

		if (VERBOSE_DEBUG && return_value != nullptr)
		{
			outstream << "finish() in generate_IR, called value is ";
			return_value->print(*llvm_outstream);
			outstream << "\nand the llvm type is ";
			return_value->getType()->print(*llvm_outstream);
			outstream << "\nand the internal type is ";
			output_type_and_previous(type);
			outstream << '\n';
		}
		else if (VERBOSE_DEBUG) outstream << "finish() in generate_IR, null value\n";
		if (VERBOSE_DEBUG)
		{
			TheModule->print(*llvm_outstream, nullptr);
			outstream << "generate_IR single AST " << target << " " << AST_descriptor[target->tag].name << " vvvvvvvvv\n";
		}


		//we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for justification.
		if (stack_degree == 2) clear_stack(final_stack_position);

		if (stack_degree >= 1)
		{
			if (size_result >= 1)
			{
				object_stack.push_back(target);
				auto insert_result = objects.insert({ target, Return_Info(IRgen_status::no_error, return_value, type, true, lifetime_of_return_value, upper_life, lower_life) });
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication, 10);
			}
			//type_scratch_space.push_back(Type("cheap pointer", type)); //stack objects are always pointers, just like in llvm.
			//type = &type_scratch_space.back();
		}
		return Return_Info(IRgen_status::no_error, return_value, type, false, lifetime_of_return_value, upper_life, lower_life);
	};

	//call the finish macros when you've constructed something.
	//_pointer suffix is when target lifetime information is relevant (for pointers).
	//_stack_handled if your AST has already created a stack alloca whenever necessary. also, size_result must be already known.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.
	//remember: pass the value itself if stack_degree == 0, and pass a pointer to the value if stack_degree == 1 or 2.

	//these are for when we need to specify the return type.
#define finish_special_pointer(X, type, u, l) do {return finish_internal(X, type, u, l, true); } while (0)
#define finish_special(X, type) do { finish_special_pointer(X, type, 0, -1ull); } while (0)
#define finish_special_stack_handled(X, type) do { return finish_internal(X, type, 0, -1ull, false); } while (0)
#define finish_special_stack_handled_pointer(X, type, u, l) do { return finish_internal(X, type, u, l, false); } while (0)

	//make sure not to duplicate X in the expression.
#define finish_pointer(X, u, l) do {finish_special_pointer(X, AST_descriptor[target->tag].return_object, u, l); } while (0)
#define finish(X) do { finish_pointer(X, 0, -1ull); } while (0)
#define finish_stack_handled(X) do { return finish_internal(X, AST_descriptor[target->tag].return_object, 0, -1ull, false); } while (0)
#define finish_stack_handled_pointer(X, u, l) do { return finish_internal(X, AST_descriptor[target->tag].return_object, u, l, false); } while (0)


	//we generate code for the AST, depending on its tag.
	switch (target->tag)
	{
	case ASTn("integer"):
		finish(llvm_integer(target->fields[0].num));
	case ASTn("add"): //add two integers.
		finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"):
		finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("hello"):
		{
			llvm::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!");

			//create the function type
			std::vector<llvm::Type*> putsArgs{Builder.getInt8Ty()->getPointerTo()};
			llvm::FunctionType *putsType = llvm::FunctionType::get(Builder.getInt32Ty(), putsArgs, false);

			//get the actual function
			llvm::Constant *putsFunc = TheModule->getOrInsertFunction("puts", putsType);

			finish(Builder.CreateCall(putsFunc, helloWorld, s("hello world")));
		}

	case ASTn("print_int"):
		{
			llvm::Value* printer = llvm_function(Builder, print_uint64_t, llvm::Type::getVoidTy(thread_context), int64_type);
			finish(Builder.CreateCall(printer, std::vector<llvm::Value*>{field_results[0].IR}));
		}
	case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
		finish(Builder.CreateCall(llvm_function(Builder, generate_random, int64_type)));
	case ASTn("if"): //todo: you can see the condition's return object in the branches.
		//we could have another version where the condition's return object is invisible.
		//this lets us goto the inside of the if statement.
		{
			//the condition statement
			if (target->fields[0].ptr == nullptr) return_code(null_AST, 0);
			auto condition = generate_IR(target->fields[0].ptr, 0);
			if (condition.error_code) return condition;

			if (type_check(RVO, condition.type, T::integer) != 3) return_code(type_mismatch, 0);

			if (stack_degree == 2)
			{
				size_result = get_size(target);
				if (size_result >= 1) storage_location = create_alloca(size_result);
			}

			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			uint64_t if_stack_position = object_stack.size();

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Value* comparison = Builder.CreateICmpNE(condition.IR, llvm_integer(0), s("if() condition"));
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, s("then"), TheFunction);
			llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context, s("else"));
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context, s("merge"));

			Builder.CreateCondBr(comparison, ThenBB, ElseBB);

			//start inserting code in the "then" block
			//this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are already commands inside the "Then" basic block, we would be in trouble
			Builder.SetInsertPoint(ThenBB);

			//calls with stack_degree as 1 in the called function, if it is 2 outside.
			Return_Info then_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
			if (then_IR.error_code) return then_IR;

			//get rid of any temporaries
			clear_stack(if_stack_position);

			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			Builder.SetInsertPoint(ElseBB); //WATCH OUT: same here.

			Return_Info else_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, storage_location);
			if (else_IR.error_code) return else_IR;

			Type* result_type = then_IR.type; //first, we assume then_IR is the main type.
			//RVO, because that's what defines the slot.
			if (type_check(RVO, else_IR.type, then_IR.type) != 3)
			{
				result_type = else_IR.type; //if it didn't work, try seeing if else_IR can be the main type.
				if (type_check(RVO, then_IR.type, else_IR.type) != 3)
					return_code(type_mismatch, 2);
			}

			//for the second branch
			Builder.CreateBr(MergeBB);
			ElseBB = Builder.GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);

			uint64_t result_target_life_guarantee = std::max(then_IR.target_life_guarantee, else_IR.target_life_guarantee);
			uint64_t result_target_hit_contract = std::min(then_IR.target_hit_contract, else_IR.target_hit_contract);
			if (stack_degree == 0)
			{
				llvm::Value* endPN = llvm_create_phi(Builder, then_IR.IR, else_IR.IR, then_IR.type, else_IR.type, ThenBB, ElseBB);
				finish_special_pointer(endPN, result_type, result_target_life_guarantee, result_target_hit_contract);
			}
			else finish_special_stack_handled_pointer(storage_location, then_IR.type, result_target_life_guarantee, result_target_hit_contract);
			//even though finish_pointer returns, the else makes it clear from first glance that it's not a continued statement.
		}

	case ASTn("label"):
		{
			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			llvm::BasicBlock *label = llvm::BasicBlock::Create(thread_context, "");
			auto label_insertion = labels.insert(std::make_pair(target, label_info(label, final_stack_position, true)));
			if (label_insertion.second == false) return_code(label_duplication, 0);

			Return_Info scoped = generate_IR(target->fields[0].ptr, 0);
			if (scoped.error_code) return scoped;

			//this expires the label, so that goto knows that the label is behind. with finiteness, this means the label isn't guaranteed.
			label_insertion.first->second.is_forward = false;

			Builder.CreateBr(label);
			TheFunction->getBasicBlockList().push_back(label);
			Builder.SetInsertPoint(label);
			finish(nullptr);
		}

	case ASTn("goto"):
		{
			auto labelsearch = labels.find(target->fields[0].ptr);
			if (labelsearch == labels.end()) return_code(missing_label, 0);
			auto info = labelsearch->second;

			//unwind destructors such as locks.
			if (info.is_forward)
			{
				emit_dtors(info.stack_size);
				Builder.CreateBr(labelsearch->second.block);
				//Builder.ClearInsertionPoint();
				//this is bugged. try [label [goto a]]a. note that the label has two predecessors, one which is a null operand
				//so when you emit the branch for the label, even though the insertion point is clear, it still adds a predecessor to the block.
				//instead, we have to emit a junk block

				llvm::BasicBlock *JunkBB = llvm::BasicBlock::Create(thread_context, s("never going to be reached"), Builder.GetInsertBlock()->getParent());
				Builder.SetInsertPoint(JunkBB);

				finish_special(nullptr, T::nonexistent);
			}

			//check finiteness
			llvm::AllocaInst* finiteness_pointer = (llvm::AllocaInst*)Builder.CreateIntToPtr(llvm_integer((uint64_t)&finiteness), int64_type->getPointerTo());
			llvm::Value* current_finiteness = Builder.CreateLoad(finiteness_pointer);
			llvm::Value* comparison = Builder.CreateICmpNE(current_finiteness, llvm_integer(0), s("finiteness comparison"));


			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();
			llvm::BasicBlock *SuccessBB = llvm::BasicBlock::Create(thread_context, s("success"), TheFunction);
			llvm::BasicBlock *FailureBB = llvm::BasicBlock::Create(thread_context, s("failure"));
			Builder.CreateCondBr(comparison, SuccessBB, FailureBB);

			//start inserting code in the "then" block
			//this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are already commands inside the "Then" basic block, we would be in trouble
			Builder.SetInsertPoint(SuccessBB);

			//decrease finiteness.
			llvm::Value* finiteness_minus_one = Builder.CreateSub(current_finiteness, llvm_integer(1));
			Builder.CreateStore(finiteness_minus_one, finiteness_pointer);

			emit_dtors(info.stack_size);
			Builder.CreateBr(labelsearch->second.block);
			Builder.ClearInsertionPoint();


			TheFunction->getBasicBlockList().push_back(FailureBB);
			Builder.SetInsertPoint(FailureBB);

			Return_Info Failure_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
			if (Failure_IR.error_code) return Failure_IR;

			if (stack_degree == 0) finish_special_pointer(Failure_IR.IR, Failure_IR.type, Failure_IR.target_life_guarantee, Failure_IR.target_hit_contract);
			else finish_special_stack_handled_pointer(storage_location, Failure_IR.type, Failure_IR.target_life_guarantee, Failure_IR.target_hit_contract);
		}
	case ASTn("scope"):
		finish(nullptr);
	case ASTn("pointer"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			if (found_AST->second.on_stack == false) return_code(pointer_to_temporary, 0);
			//our new pointer type
			type_scratch_space.push_back(Type("cheap pointer", found_AST->second.type));

			///we force cast all llvm pointer types to integers. this makes it easy to represent types inside llvm, since they're described by a single number - their size.
			llvm::Value* final_result = Builder.CreatePtrToInt(found_AST->second.IR, int64_type, s("flattening pointer"));

			finish_special_pointer(final_result, &type_scratch_space.back(), found_AST->second.self_lifetime, found_AST->second.self_lifetime);
		}
	case ASTn("load"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			//if on_stack is false, it's not an alloca so we should copy instead of loading.
			Return_Info AST_to_load = found_AST->second;
			if (AST_to_load.on_stack == false) finish_special_pointer(AST_to_load.IR, AST_to_load.type, AST_to_load.target_life_guarantee, AST_to_load.target_hit_contract);
			else finish_special(Builder.CreateLoad(AST_to_load.IR), AST_to_load.type);
		}
	case ASTn("concatenate"):
		{
			//TODO: if stack_degree == 1, then you've already gotten the size. look it up in a special location.

			uint64_t size[2] = {get_size(target->fields[0].ptr), get_size(target->fields[1].ptr)};
			size_result = size[0] + size[1];
			if (size[0] > 0 && size[1] > 0)
			{
				//we want the return objects to RVO. thus, we create a memory slot.
				if (stack_degree == 0 || stack_degree == 2) storage_location = create_alloca(size_result);

				llvm::AllocaInst* location[2];
				llvm::Value* pointer[2];
				Return_Info half[2];
				for (int x : {0, 1})
				{
					uint64_t offset = (x == 1) ? size[0] : 0;
					if (size[x] == 1) location[x] = (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(storage_location, 0, offset, s("half of concatenate as an integer") + s(string{(char)x}));
					else
					{
						llvm::Type* pointer_to_array = llvm_array(size[x])->getPointerTo();
						pointer[x] = Builder.CreateConstInBoundsGEP2_64(storage_location, 0, offset, s("half of concatenate") + s(string{(char)x}));
						location[x] = (llvm::AllocaInst*)Builder.CreatePointerCast(pointer[x], pointer_to_array, s("pointer cast to array"));
					}

					half[x] = generate_IR(target->fields[x].ptr, 1, location[x]);
					if (half[x].error_code) return half[x];
				}
				type_scratch_space.push_back(Type("concatenate", half[0].type, half[1].type));

				llvm::Value* final_value;
				if (stack_degree == 0) final_value = Builder.CreateLoad(storage_location, s("load concatenated object"));
				else final_value = storage_location;

				uint64_t result_target_life_guarantee = std::max(half[0].target_life_guarantee, half[1].target_life_guarantee);
				uint64_t result_target_hit_contract = std::min(half[0].target_hit_contract, half[1].target_hit_contract);

				finish_special_stack_handled_pointer(final_value, &type_scratch_space.back(), result_target_life_guarantee, result_target_hit_contract);
			}
			else //it's convenient having a pass-through special case, because pass-through means we don't need to have special cases for size-1 objects.
			{
				//we only have to RVO if stack_degree is 1, because otherwise we can return the result directly.
				//that means we don't have to create a storage_location here.
				Return_Info half[2];
				for (int x : {0, 1})
				{
					half[x] = generate_IR(target->fields[x].ptr, (stack_degree == 1) && (size[x] > 0), storage_location);
					if (half[x].error_code) return half[x];
				}

				if (half[0].type == T::nonexistent || half[1].type == T::nonexistent)
					finish_special(nullptr, T::nonexistent);
				if (size[0] > 0) finish_special_stack_handled_pointer(half[0].IR, half[0].type, half[0].target_life_guarantee, half[0].target_hit_contract);
				else finish_special_stack_handled_pointer(half[1].IR, half[1].type, half[1].target_life_guarantee, half[1].target_hit_contract);
				//even if half[1] is 0, we return it anyway.
			}
		}

	case ASTn("dynamic"):
		{
			uint64_t size_of_object = get_size(target->fields[0].ptr);
			if (size_of_object >= 1)
			{
				llvm::Function* allocator = llvm_function(Builder, allocate_memory, int64_type, int64_type);
				llvm::Value* dynamic_object_address = Builder.CreateCall(allocator, std::vector < llvm::Value* > {llvm_integer(size_of_object)}, s("allocate memory"));

				//can't use ?: because the return objects have different types, unless we cast
				//this represents either an integer or an array of integers.
				llvm::Type* target_type = int64_type;
				if (size_of_object > 1) target_type = llvm_array(size_of_object);
				llvm::Type* target_pointer_type = target_type->getPointerTo();

				//store the returned value into the acquired address
				llvm::Value* dynamic_object = Builder.CreateIntToPtr(dynamic_object_address, target_pointer_type);
				Builder.CreateStore(field_results[0].IR, dynamic_object);

				//create a pointer to the type of the dynamic pointer. but we serialize the pointer to be an integer.
				Type* type_of_dynamic_object = get_unique_type(field_results[0].type, false);
				llvm::Constant* integer_type_pointer = llvm_integer((uint64_t)type_of_dynamic_object);


				//we now serialize both objects to become integers.
				llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(2));
				llvm::Value* first_value = Builder.CreateInsertValue(undef_value, dynamic_object_address, std::vector < unsigned > { 0 });
				llvm::Value* full_value = Builder.CreateInsertValue(first_value, integer_type_pointer, std::vector < unsigned > { 1 });
				finish(full_value);
			}
			else
			{
				//zero array, for null dynamic object.
				llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(2));
				llvm::Value* first_value = Builder.CreateInsertValue(undef_value, llvm_integer(0), std::vector < unsigned > { 0 });
				llvm::Value* full_value = Builder.CreateInsertValue(first_value, llvm_integer(0), std::vector < unsigned > { 1 });
				finish(full_value);
			}
		}
	case ASTn("dynamic_conc"):
		{
			llvm::Value* pointer[2];
			llvm::Value* type[2];
			for (int x : {0, 1})
			{
				//can't gep because it's not in memory
				pointer[x] = Builder.CreateExtractValue(field_results[x].IR, std::vector<unsigned>{0}, s("object pointer of dynamic"));
				type[x] = Builder.CreateExtractValue(field_results[x].IR, std::vector<unsigned>{1}, s("type pointer of dynamic"));
			}

			std::vector<llvm::Type*> argument_types{int64_type, int64_type, int64_type, int64_type};
			//llvm::Type* return_type = llvm_array(2);

			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			//if ever the optimization changes, we'll be in trouble.
			std::vector<llvm::Type*> return_types{int64_type, int64_type};
			llvm::Type* return_type = llvm::StructType::get(thread_context, return_types);
			llvm::FunctionType* dynamic_conc_type = llvm::FunctionType::get(return_type, argument_types, false);

			llvm::PointerType* dynamic_conc_type_pointer = dynamic_conc_type->getPointerTo();
			llvm::Constant *twister_address = llvm_integer((uint64_t)&concatenate_dynamic);
			llvm::Value* dynamic_conc_function = Builder.CreateIntToPtr(twister_address, dynamic_conc_type_pointer, s("convert integer address to actual function"));

			std::vector<llvm::Value*> arguments{pointer[0], type[0], pointer[1], type[1]};
			llvm::Value* result_of_compile = Builder.CreateCall(dynamic_conc_function, arguments, s("dynamic concatenate"));




			//this transformation is necessary because array<uint64_t, 2> becomes {i64, i64}
			llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(2));

			llvm::Value* first_return = Builder.CreateExtractValue(result_of_compile, std::vector<unsigned>{0});
			llvm::Value* first_value = Builder.CreateInsertValue(undef_value, first_return, std::vector<unsigned>{0});

			llvm::Value* second_return = Builder.CreateExtractValue(result_of_compile, std::vector<unsigned>{1});
			llvm::Value* full_value = Builder.CreateInsertValue(first_value, second_return, std::vector<unsigned>{1});

			finish(full_value);
		}
	case ASTn("compile"):
		{
			//llvm::Type* return_type = llvm_array(2);


			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			//if ever the optimization changes, we'll be in trouble.
			std::vector<llvm::Type*> return_type_members{int64_type, int64_type};
			llvm::Type* return_type = llvm::StructType::get(thread_context, return_type_members);

			llvm::Value* compile_function = llvm_function(Builder, compile_user_facing, return_type, int64_type);
			//Builder.CreateIntToPtr(twister_address, compile_type_pointer, s("convert integer address to actual function"));

			std::vector<llvm::Value*> arguments{field_results[0].IR};
			llvm::Value* result_of_compile = Builder.CreateCall(compile_function, arguments, s("compile"));



			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(2));

			llvm::Value* first_return = Builder.CreateExtractValue(result_of_compile, std::vector < unsigned > { 0 });
			llvm::Value* first_value = Builder.CreateInsertValue(undef_value, first_return, std::vector < unsigned > { 0 });

			llvm::Value* second_return = Builder.CreateExtractValue(result_of_compile, std::vector < unsigned > { 1 });
			llvm::Value* full_value = Builder.CreateInsertValue(first_value, second_return, std::vector < unsigned > { 1 });

			finish(full_value);
		}
	case ASTn("convert_to_AST"):
		{
			llvm::IntegerType* boolean = llvm::IntegerType::get(thread_context, 1);
			llvm::Value* checker_function = llvm_function(Builder, is_AST_user_facing, boolean, int64_type, int64_type);

			llvm::Value* pointer = Builder.CreateExtractValue(field_results[2].IR, std::vector < unsigned > {0}, s("fields of hopeful AST"));
			llvm::Value* type = Builder.CreateExtractValue(field_results[2].IR, std::vector < unsigned > {1}, s("type of hopeful AST"));

			llvm::Value* previous_AST;
			if (type_check(RVO, field_results[1].type, nullptr) == 3) //previous AST is nullptr.
				previous_AST = llvm_integer(0);
			else if (type_check(RVO, field_results[1].type, T::AST_pointer) == 3) //previous AST actually exists
				previous_AST = field_results[1].IR;
			else return_code(type_mismatch, 1);

			std::vector<llvm::Value*> arguments{field_results[0].IR, type};
			llvm::Value* check_result = Builder.CreateCall(checker_function, arguments, s("checker"));


			llvm::Value* comparison = Builder.CreateICmpNE(check_result, llvm::ConstantInt::getFalse(thread_context), s("if() condition"));
			llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();
			llvm::BasicBlock *SuccessBB = llvm::BasicBlock::Create(thread_context, s("success"), TheFunction);
			llvm::BasicBlock *FailureBB = llvm::BasicBlock::Create(thread_context, s("failure"));
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context, s("merge"));
			Builder.CreateCondBr(comparison, SuccessBB, FailureBB);
			Builder.SetInsertPoint(SuccessBB);

			llvm::Value* converter_function = llvm_function(Builder, make_AST_pointer_from_dynamic, int64_type, int64_type, int64_type, int64_type);
			std::vector<llvm::Value*> arguments2{field_results[0].IR, previous_AST, pointer};
			llvm::Value* success_result = Builder.CreateCall(converter_function, arguments2, s("AST pointer conversion"));

			Builder.CreateBr(MergeBB);
			SuccessBB = Builder.GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(FailureBB);
			Builder.SetInsertPoint(FailureBB);

			//calls with stack_degree as 1 in the called function, if it is 2 outside.
			Return_Info failure_IR = generate_IR(target->fields[3].ptr, stack_degree != 0, storage_location);
			if (failure_IR.error_code) return failure_IR;
			if (type_check(RVO, failure_IR.type, T::AST_pointer)) return_code(type_mismatch, 3);

			Builder.CreateBr(MergeBB);
			FailureBB = Builder.GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);

			if (stack_degree == 0)
			{
				llvm::Value* endPN = llvm_create_phi(Builder, success_result, failure_IR.IR, T::AST_pointer, T::AST_pointer, SuccessBB, FailureBB);
				finish_special(endPN, T::AST_pointer);
			}
			else finish_special_stack_handled(storage_location, T::AST_pointer);

		}
	case ASTn("temp_generate_AST"):
		{
			llvm::Value* generator = llvm_function(Builder, ASTmaker, int64_type);
			//llvm::Constant *twister_function = TheModule->getOrInsertFunction("ASTmaker", AST_maker_type);
			finish(Builder.CreateCall(generator, s("ASTmaker")));
		}
	case ASTn("static_object"):
		{
			llvm::Value* address_of_object = llvm_integer(target->fields[0].num);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				llvm::Type* pointer_to_array = llvm_array(size_of_object)->getPointerTo();
				storage_location = (llvm::AllocaInst*)Builder.CreatePointerCast(address_of_object, pointer_to_array, s("pointer cast to array"));

				llvm::Value* final_value = stack_degree == 0 ? (llvm::Value*)Builder.CreateLoad(storage_location, s("load concatenated object")) : storage_location;
				finish_special_stack_handled_pointer(final_value, type_of_object, full_lifetime, full_lifetime);
			}
			else finish_special(nullptr, nullptr);
		}
	case ASTn("load_object"): //bakes in the value into the compiled function. changes by the function are temporary.
		{
			//problem: what to do about pointers? especially things like functions and unique pointers. does a simply memory copy really copy the object?
			uint64_t* array_of_integers = (uint64_t*)(target->fields[0].ptr);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				std::vector<llvm::Constant*> values;
				values.reserve(size_of_object);
				for (int x = 0; x < size_of_object; ++x)
				{
					values.push_back(llvm_integer(array_of_integers[x]));
				}

				llvm::Constant* object = llvm::ConstantArray::get(llvm_array(size_of_object), values);
				finish_special(object, type_of_object);
			}
			else finish_special(nullptr, nullptr);
		}
	}
	error("fell through switches");
}

/**
The fuzztester generates random ASTs and attempts to compile them.
the output "Malformed AST" is fine. not all randomly-generated ASTs will be well-formed.

fuzztester has a vector of pointers to working, compilable ASTs (initially just a nullptr).
it chooses a random AST tag.
then, it chooses pointers randomly from the vector of working ASTs, which go in the first few fields.
each remaining field is chosen randomly according to an exponential distribution
finally, if the created AST successfully compiles, it is added to the vector of working ASTs.

todo: this scheme can't produce forward references, which are necessary for goto. that is, a goto points to an AST that's created after it.
*/
void fuzztester(unsigned iterations)
{
	std::vector<AST*> AST_list{ nullptr }; //start with nullptr as the default referenceable AST
	while (iterations--)
	{
		//create a random AST
		unsigned tag = mersenne() % (ASTn("never reached") - 1) + 1; //we skip 0 because that's a null AST
		unsigned pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST pointers. they will come at the beginning
		unsigned prev_AST = generate_exponential_dist() % AST_list.size(); //todo: prove that exponential_dist is desired.
		//birthday collisions is the problem. a concatenate with two branches will almost never appear, because it'll result in an active object duplication.
		//but does exponential falloff solve this problem in the way we want?


		int_or_ptr<AST> fields[4]{nullptr, nullptr, nullptr, nullptr};
		int incrementor = 0;
		for (; incrementor < pointer_fields; ++incrementor)
			fields[incrementor] = AST_list.at(mersenne() % AST_list.size()); //get pointers to previous ASTs
		for (; incrementor < pointer_fields + AST_descriptor[tag].additional_special_fields; ++incrementor)
		{
			if (AST_descriptor[tag].parameter_types[incrementor] == T::integer)
				fields[incrementor] = generate_exponential_dist(); //get random integers and fill in the remaining fields
			else
			{
				outstream << "fuzztester doesn't know how to make this special type, so it's going to be 0's.\n";
				check(incrementor == pointer_fields + AST_descriptor[tag].additional_special_fields - 1, "egads! I need to build functionality for tracking the current parameter field offset and the current parameter type separately, because there are extra fields after the special field, and the special field may have size different from 1\n");
			}
		}
		AST* test_AST= new AST(tag, AST_list.at(prev_AST), fields[0], fields[1], fields[2], fields[3]);
		if (OLD_AST_OUTPUT)
			output_AST_and_previous(test_AST);
		output_AST_console_version a(test_AST);

		finiteness = FINITENESS_LIMIT;
		compiler_object compiler;
		unsigned error_code = compiler.compile_AST(test_AST);
		if (error_code)
		{
			outstream << "Malformed AST: code " << error_code << " at AST " << compiler.error_location << " " << AST_descriptor[compiler.error_location->tag].name << " field " << compiler.error_field << "\n";
			delete test_AST;
		}
		else
		{
			AST_list.push_back(test_AST);
			outstream << "Successful compile " << AST_list.size() - 1 << "\n";
		}
		if (INTERACTIVE)
		{
			outstream << "Press enter to continue\n";
			std::cin.get();
		}
		outstream << "\n";
	}
}



#ifdef NDEBUG
panic time!
#endif
#include <iostream>
#include <sstream>
#include <istream>
//parses console input and generates ASTs.
using std::string; //why can't this go inside source_reader?
class source_reader
{

	std::unordered_map<string, AST*> ASTmap = {{"0", nullptr}}; //from name to location. starts with 0 = nullptr.
	std::unordered_map<AST**, string> goto_delay; //deferred binding of addresses, for goto. we can only see the labels at the end.
	AST* delayed_binding_special_value = new AST(1); //memleak
	string delayed_binding_name; //a backdoor return value.
	std::istream& input;

	//grabs a token. characters like [, ], {, } count as tokens by themselves.
	//otherwise, get characters until you hit whitespace or a special character.
	//if the last char is \n, then it doesn't consume it.
	string get_token()
	{
		while (input.peek() == ' ')
			input.ignore(1);

		char next_char = input.peek();
		switch (next_char)
		{
		case '[':
		case ']':
		case '{':
		case '}':
			input.ignore(1);
			return string(1, next_char);
		case '\n':
			return string();
		default:
			string word;
			while (next_char != '[' && next_char != ']' && next_char != '{' && next_char != '}' && next_char != '\n' && next_char != ' ')
			{
				word.push_back(next_char);
				input.ignore(1);
				next_char = input.peek();
			}
			//outstream << "word is " << word << "|||\n";
			return word;
		}
	}

	//continued_string is in case we already read the first token. then, that's the first thing to start with.
	AST* read_single_AST(AST* previous_AST, string continued_string = "")
	{
		string first = continued_string == "" ? get_token() : continued_string;
		AST* new_type = new AST(-1); //we have to make it now, so that we know where the AST will be. this lets us specify where our reference must be resolved.

		if (first != string(1, '[')) //it's a name reference.
		{
			//outstream << "first was " << first << '\n';
			auto AST_search = ASTmap.find(first);
			//check(AST_search != ASTmap.end(), string("variable name not found: ") + first);
			if (AST_search == ASTmap.end())
			{
				delayed_binding_name = first;
				return delayed_binding_special_value;
			}
			return AST_search->second;
		}

		string tag = get_token();

		uint64_t AST_type = ASTn(tag.c_str());
		if (VERBOSE_DEBUG) outstream << "AST tag was " << AST_type << "\n";
		uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;

		int_or_ptr<AST> fields[max_fields_in_AST] = { nullptr, nullptr, nullptr, nullptr };

		//field_num is which field we're on
		int field_num = 0;
		for (string next_token = get_token(); next_token != "]"; next_token = get_token(), ++field_num)
		{
			check(field_num < max_fields_in_AST, "more fields than possible");
			if (field_num < pointer_fields) //it's a pointer!
			{
				if (next_token == "{") fields[field_num] = create_single_basic_block(true);
				else fields[field_num] = read_single_AST(nullptr, next_token);
				if (fields[field_num].ptr == delayed_binding_special_value)
				{
					goto_delay.insert(make_pair(&new_type->fields[field_num].ptr, delayed_binding_name));
				}
			}
			else //it's an integer
			{
				bool isNumber = true;
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, string("tried to input non - number") + next_token);
				check(next_token.size(), "token is empty, probably a missing ]");
				fields[field_num] = std::stoull(next_token);
			}
		}
		//check(next_token == "]", "failed to have ]");
		*new_type = AST(AST_type, previous_AST, fields[0], fields[1], fields[2], fields[3]);

		if (VERBOSE_DEBUG)
			output_AST_console_version a(new_type);

		//get an AST name if any.
		if (input.peek() != ' ' && input.peek() != '\n' && input.peek() != ']' && input.peek() != '[' && input.peek() != '{' && input.peek() != '}')
		{
			string thisASTname = get_token();
			//seems that once we consume the last '\n', the cin stream starts blocking again.
			if (!thisASTname.empty())
			{
				check(ASTmap.find(thisASTname) == ASTmap.end(), string("duplicated variable name: ") + thisASTname);
				ASTmap.insert(std::make_pair(thisASTname, new_type));
			}
		}

		if (VERBOSE_DEBUG)
		{
			outstream << "next char is" << (char)input.peek() << input.peek();
			outstream << '\n';
		}
		return new_type;


	}

	AST* create_single_basic_block(bool create_brace_at_end = false)
	{
		AST* current_previous = nullptr;
		if (create_brace_at_end == false)
		{
			if (std::cin.peek() == '\n') std::cin.ignore(1);
			for (string next_word = get_token(); next_word != ""; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference. note that forward references in basic blocks are not allowed");
			}
			return current_previous; //which is the very last.
		}
		else
		{
			for (string next_word = get_token(); next_word != "}"; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference. note that forward references in basic blocks are not allowed");
			}
			return current_previous; //which is the very last
		}
	}

public:
	source_reader(std::istream& stream, char end) : input(stream) {}
	AST* read()
	{
		AST* end = create_single_basic_block();

		//resolve all the forward references from goto()
		for (auto& x : goto_delay)
		{
			auto AST_search = ASTmap.find(x.second);
			check(AST_search != ASTmap.end(), string("variable name not found: ") + x.second);
			*(x.first) = AST_search->second;
		}
		return end;
	}
};


int main(int argc, char* argv[])
{
	//tells LLVM the generated code should be for the native platform
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	test_unique_types();

	//these do nothing
	//assert(0);
	//assert(1);

	//used for testing ASTn's error reporting
	/*switch (2)
	{
	case ASTn("fiajewoar"):
		break;
	}*/
	for (int x = 1; x < argc; ++x)
	{
		if (strcmp(argv[x], "interactive") == 0) INTERACTIVE = true;
		else if (strcmp(argv[x], "verbose") == 0) VERBOSE_DEBUG = true;
		else if (strcmp(argv[x], "optimize") == 0) OPTIMIZE = true;
		else if (strcmp(argv[x], "console") == 0) CONSOLE = true;
		else if (strcmp(argv[x], "timer") == 0) TIMER = true;
		else if (strcmp(argv[x], "oldoutput") == 0) OLD_AST_OUTPUT = false;
		else if (strcmp(argv[x], "quiet") == 0)
		{
			outstream.setstate(std::ios_base::failbit);
			outstream.rdbuf(nullptr);
			llvm_outstream = &llvm_null_stream;
		}
		else error(string("unrecognized flag ") + argv[x]);
	}
	/*
	AST get1("integer", nullptr, 1); //get the integer 1
	AST get2("integer", nullptr, 2);
	AST get3("integer", nullptr, 3);
	AST addthem("add", nullptr, &get1, &get2); //add integers 1 and 2


	AST getif("if", &addthem, &get1, &get2, &get3); //first, execute addthem. then, if get1 is non-zero, return get2. otherwise, return get3.
	AST helloworld("hello", &getif); //first, execute getif. then output "Hello World!"
	compiler_object compiler;
	compiler.compile_AST(&helloworld);
	*/
	if (TIMER)
	{
		std::clock_t start = std::clock();
		for (int x = 0; x < 40; ++x)
		{
			std::clock_t ministart = std::clock();
			fuzztester(100);
			std::cout << "Minitime: " << (std::clock() - ministart) / (double)CLOCKS_PER_SEC << '\n';
		}
		std::cout << "Overall time: " << (std::clock() - start) / (double)CLOCKS_PER_SEC << '\n';
		return 0;
	}
	if (CONSOLE)
	{
		while (1)
		{
			source_reader k(std::cin, '\n'); //have to reinitialize to clear the ASTmap
			outstream << "Input AST:\n";
			AST* end = k.read();
			outstream << "Done reading.\n";
			if (end == nullptr)
			{
				outstream << "it's nullptr\n";
				std::cin.get();
				continue;
			}
			output_AST_and_previous(end);
			finiteness = FINITENESS_LIMIT;
			compiler_object j;
			unsigned error_code = j.compile_AST(end);
			if (error_code)
				outstream << "Malformed AST: code " << error_code << " at AST " << j.error_location << " " << AST_descriptor[j.error_location->tag].name << " field " << j.error_field << "\n\n";
			else outstream << "Successful compile\n\n";
			}
	}

	fuzztester(-1);
}