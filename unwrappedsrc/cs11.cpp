/* see README.md for how to run this program.
Functions which create ASTs:
main() handles the commandline arguments. if console input is requested, it will handle input too. however, it won't parse the input.
fuzztester() generates random, possibly malformed ASTs.
for console input, read_single_AST() parses a single AST and its field's ASTs, then creates a tree of ASTs out of it.
	then, create_single_basic_block() parses a list of ASTs, then chains them in a linked list using AST.preceding_BB_element

Things which compile and run ASTs:
compiler_object holds state for a compilation. you make a new compiler_state for each compilation, and call compile_AST() on the last AST in your function. compile_AST automatically wraps the AST in a function and runs it; the last AST is assumed to hold the return object.
compile_AST() is mainly a wrapper. it generates the l::Functions and l::Modules that contain the main code. it calls generate_IR() to do the main work.
generate_IR() is the main AST conversion tool. it turns ASTs into l::Values, recursively. these l::Values are automatically inserted into a l::Module.
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
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "types.h"
#include "debugoutput.h"
#include "helperfunctions.h"
#include "unique_type_creator.h"
#include "user_facing_functions.h"
#include "cs11.h"
#include "orc.h"



namespace l = llvm;

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = false;
bool CONSOLE = false;
bool TIMER = false;
bool OLD_AST_OUTPUT = false;

basic_onullstream<char> null_stream;
std::ostream& console = std::cerr;
l::raw_null_ostream llvm_null_stream;
l::raw_ostream* llvm_console = &l::outs();


//later: threading. create non-global contexts
thread_local l::LLVMContext& thread_context = l::getGlobalContext();
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



compiler_object::compiler_object() : S(thread_context), J(S), C(S), error_location(nullptr)
{
}

template<size_t array_num> void cout_array(std::array<uint64_t, array_num> object)
{
	console << "Evaluated to";
	for (int x = 0; x < array_num; ++x) console << ' ' << object[x];
	console << '\n';
}

//return value is the error code, which is 0 if successful
unsigned compiler_object::compile_AST(uAST* target)
{


	if (VERBOSE_DEBUG) console << "starting compilation\ntarget is " << target << '\n'; //necessary in case it crashes here
	if (target == nullptr) return IRgen_status::null_AST;
	using namespace llvm;
	FunctionType *FT;

	auto size_of_return = return_size(target);
	if (size_of_return == 0) FT = FunctionType::get(l::Type::getVoidTy(thread_context), false);
	else FT = FunctionType::get(llvm_type(size_of_return), false);
	if (VERBOSE_DEBUG) console << "Size of return is " << size_of_return << '\n';

	Function *F = Function::Create(FT, Function::ExternalLinkage, "__anon_expr", &C.getM());

	BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
	C.getBuilder().SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code) return return_object.error_code; //error

	return_type = get_unique_type(return_object.type, false);
	if (size_of_return) C.getBuilder().CreateRet(return_object.IR);
	else C.getBuilder().CreateRetVoid();

	C.getM().print(*llvm_console, nullptr);
	check(!l::verifyFunction(*F, llvm_console), "verification failed");
	if (OPTIMIZE)
	{
		console << "optimized code: \n";
		l::legacy::FunctionPassManager FPM(&C.getM());
		C.getM().setDataLayout(*S.getTarget().getDataLayout());

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
		FPM.run(*F);

		C.getM().print(*llvm_console, nullptr);
	}

	if (VERBOSE_DEBUG) console << "finalizing object...\n";

	auto H = J.addModule(C.takeM());

	// Get the address of the JIT'd function in memory.
	auto ExprSymbol = J.findUnmangledSymbol("__anon_expr");

	// Cast it to the right type (takes no arguments, returns a double) so we
	// can call it as a native function.
	void *fptr = (void*)(intptr_t)ExprSymbol.getAddress();
	if (VERBOSE_DEBUG) console << "running function:\n";
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		console <<  "Evaluated to " << FP() << '\n';
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
		default: console << "function return size is " << size_of_return << " and C++ seems to only take static return types\n"; break;
		}
	}
	else
	{
		void(*FP)() = (void(*)())(intptr_t)fptr;
		FP();
	}


	// Remove the function.
	J.removeModule(H);
	return 0;
}


/** mem-to-reg only works on entry block variables.
thus, this function builds l::Allocas in the entry block. it should be preferred over trying to create allocas directly.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
we call this "create_alloca" instead of "create_alloca_in_entry_block", because it's the general alloca mechanism. if we said, "in_entry_block", then the user would be confused as to when to use this. by not having that, it's clear that this should be the default.
*/
l::AllocaInst* compiler_object::create_alloca(uint64_t size) {
	check(size > 0, "tried to create a 0-size alloca");
	l::BasicBlock& first_block = C.getBuilder().GetInsertBlock()->getParent()->getEntryBlock();
	l::IRBuilder<> TmpB(&first_block, first_block.begin());

	//we explicitly create the array type instead of allocating multiples, because that's what clang does for C++ arrays.
	return TmpB.CreateAlloca(llvm_type(size));
}

void compiler_object::emit_dtors(uint64_t desired_stack_size)
{
	//todo
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{
	while (object_stack.size() != desired_stack_size)
	{
		auto to_be_removed = object_stack.top();
		object_stack.pop();
		if (to_be_removed.second) //it's on the stack
		{
			uint64_t number_of_erased = objects.erase(to_be_removed.first);
			check(number_of_erased == 1, "erased too many or too few elements"); //we shouldn't put the erase operation in the check, because check() might be no-op'd, and erase has side effects
		}
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
example: the l::Value* of an object that had stack_degree = 2 is a pointer to the memory location, where the pointer is casted to an integer.
currently, the finish() macro takes in the array itself, not the pointer-to-array.
*/
Return_Info compiler_object::generate_IR(uAST* user_target, unsigned stack_degree, l::AllocaInst* storage_location)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
#define return_code(X, Y) do { error_location = user_target; error_field = Y; return Return_Info(IRgen_status::X, nullptr, T::null, stack_state::temp, 0, 0, 0); } while (0)

	llvm::IRBuilder<>& Builder = C.getBuilder();

	if (VERBOSE_DEBUG)
	{
		console << "generate_IR single AST start ^^^^^^^^^\n";
		if (user_target) output_AST(user_target);
		console << "stack degree " << stack_degree;
		console << ", storage location is ";
		if (storage_location) storage_location->print(*llvm_console);
		else console << "null";
		console << '\n';
	}

	if (stack_degree == 2) check(storage_location == nullptr, "if stack degree is 2, we should have a nullptr storage_location");
	if (stack_degree == 1) check((storage_location != nullptr) || (return_size(user_target) == 0), "if stack degree is 1, we should have a storage_location");

	//generate_IR is allowed to take nullptr. otherwise, we need an extra check beforehand. this extra check creates code duplication, which leads to typos when indices aren't changed.
	//check(target != nullptr, "generate_IR should never receive a nullptr target");
	if (user_target == nullptr) return Return_Info();

	//if we've seen this AST before, we're stuck in an infinite loop. return an error.
	if (this->loop_catcher.find(user_target) != this->loop_catcher.end()) return_code(infinite_loop, 10); //for now, 10 is a special value, and means not any of the fields
	uAST* target = copy_AST(user_target); //make a copy
	loop_catcher.insert({user_target, target}); //we've seen this AST now.
	

	//after we're done with this AST, we remove it from loop_catcher.
	struct loop_catcher_destructor_cleanup
	{
		//compiler_state can only be used if it is passed in.
		compiler_object* object; uAST* targ;
		loop_catcher_destructor_cleanup(compiler_object* x, uAST* t) : object(x), targ(t) {}
		~loop_catcher_destructor_cleanup() { object->loop_catcher.erase(targ); }
	} temp_object(this, user_target);

	//compile the previous elements in the basic block.
	if (target->preceding_BB_element)
	{
		auto previous_elements = generate_IR(target->preceding_BB_element, 2);
		if (previous_elements.error_code) return previous_elements;
	}

	//NOTE: these are the set of default values.

	//after compiling the previous elements in the basic block, we find the lifetime of this element
	uint64_t lifetime_of_return_value = incrementor_for_lifetime++;

	uint64_t size_result = special; //note: it's only active when stack_degree = 1. otherwise, you must have special cases.
	//-1ll is a debug choice, since having it at 0 led to invisible errors.
	//whenever you use create_alloca(), make sure size_result is actually set.

	uint64_t final_stack_position = object_stack.size();

	stack_state final_stack_state = stack_state::temp;
	if (stack_degree != 0) final_stack_state = stack_state::cheap_alloca; //the default. unless any further information appears.

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.


	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	//if it's necessary to create and write to storage_location, we do so.
	//if move_to_stack == true, it writes into a previously created storage_location
	auto finish_internal = [&](l::Value* return_value, Type* type, uint64_t self_validity_guarantee, uint64_t target_hit_contract, bool move_to_stack) -> Return_Info
	{
		check(type != T::special_return, "didn't specify return type when necessary");

		if (stack_degree >= 1 && size_result == special) size_result = return_size(target);
		if (stack_degree == 2 && size_result != 0 && storage_location == nullptr)
		{
			if (storage_location == nullptr)
			{
				storage_location = create_alloca(size_result);
				Builder.CreateStore(return_value, storage_location);
				return_value = storage_location;
			}
			else check(move_to_stack == false, "if move_to_stack is true, then we shouldn't have already created the storage_location");
		}
		if (stack_degree == 1 && move_to_stack)
		{
			if (size_result >= 1) //we don't do it for stack_degree = 2, because if storage_location =/= nullptr, we shouldn't write
			{
				//RVO must happen here, before returning. because of do_after. when you call do_after, the existing things must already be put in place. you can't put them in place after returning.

				Builder.CreateStore(return_value, storage_location);
				return_value = storage_location;
			}
		}

		if (VERBOSE_DEBUG && return_value != nullptr)
		{
			console << "finish() in generate_IR, called value is ";
			return_value->print(*llvm_console);
			console << "\nand the llvm type is ";
			return_value->getType()->print(*llvm_console);
			console << "\nand the internal type is ";
			output_type_and_previous(type);
			console << '\n';
		}
		else if (VERBOSE_DEBUG) console << "finish() in generate_IR, null value\n";
		if (VERBOSE_DEBUG)
		{
			Builder.GetInsertBlock()->getModule()->print(*llvm_console, nullptr);
			console << "generate_IR single AST " << target << " " << AST_descriptor[target->tag].name << " vvvvvvvvv\n";
		}

		//we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for justification.
		if (stack_degree == 2) clear_stack(final_stack_position);

		if (size_result >= 1)
		{
			if (stack_degree >= 1)
			{
				object_stack.push({user_target, true});
				auto insert_result = objects.insert({user_target, Return_Info(IRgen_status::no_error, return_value, type, final_stack_state, lifetime_of_return_value, self_validity_guarantee, target_hit_contract)});
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication, 10);
			}
			else //stack_degree == 0
				object_stack.push(std::make_pair(user_target, false));
		}
		return Return_Info(IRgen_status::no_error, return_value, type, final_stack_state, lifetime_of_return_value, self_validity_guarantee, target_hit_contract);
	};

	//call the finish macros when you've constructed something.
	//_pointer suffix is when target lifetime information is relevant (for cheap pointers).
	//_stack_handled if your AST has already created a stack alloca whenever necessary. also, size_result must be already known.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.
	//remember: pass the value itself if stack_degree == 0, and pass a pointer to the value if stack_degree == 1 or 2.

	//these are for when we need to specify the return type.
#define finish_special_pointer(X, type, u, l) do {return finish_internal(X, type, u, l, true); } while (0)
#define finish_special(X, type) do { finish_special_pointer(X, type, 0, -1ll); } while (0)
#define finish_special_stack_handled(X, type) do { return finish_internal(X, type, 0, -1ll, false); } while (0)
#define finish_special_stack_handled_pointer(X, type, u, l) do { return finish_internal(X, type, u, l, false); } while (0)

	//make sure not to duplicate X in the expression.
#define finish_pointer(X, u, l) do {finish_special_pointer(X, AST_descriptor[target->tag].return_object, u, l); } while (0)
#define finish(X) do { finish_pointer(X, 0, -1ll); } while (0)
#define finish_stack_handled(X) do { return finish_internal(X, AST_descriptor[target->tag].return_object, 0, -1ll, false); } while (0)
#define finish_stack_handled_pointer(X, u, l) do { return finish_internal(X, AST_descriptor[target->tag].return_object, u, l, false); } while (0)


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
			console << "type checking. result type is \n";
			output_type_and_previous(result.type);
			console << "desired type is \n";
			output_type_and_previous(AST_descriptor[target->tag].parameter_types[x]);
		}

		//check that the type matches.
		if (AST_descriptor[target->tag].parameter_types[x] != T::parameter_no_type_check) //for fields that are 
			//if (AST_descriptor[target->tag].parameter_types[x] != T::missing_field) //this is for fields that are not specified, but created through make_fields_to_compile. it's kind of redundant with parameter_no_type_check. we'll probably delete make_fields_to_compile, except that the convert_to_AST() function wants it.
			{
				if (AST_descriptor[target->tag].parameter_types[x] == T::nonexistent)
					finish_special(nullptr, T::nonexistent); //just get out of here, since we're never going to run the current command anyway.
				//this is fine with labels even though labels require emission whether reached or not, because labels don't compile using the default mechanism
				//even if there are labels skipped over by this escape, nobody can see them because of our try-catch goto scheme.
				if (type_check(RVO, result.type, AST_descriptor[target->tag].parameter_types[x]) != type_check_result::perfect_fit) return_code(type_mismatch, x);
			}

		field_results.push_back(result);
	}

	//we generate code for the AST, depending on its tag.
	switch (target->tag)
	{
	case ASTn("integer"): finish(llvm_integer(target->fields[0].num));
	case ASTn("add"): finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"): finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("increment"): finish(Builder.CreateAdd(field_results[0].IR, llvm_integer(1), s("increment")));
	/*case ASTn("hello"):
		{
			l::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!");

			//create the function type
			std::vector<l::Type*> putsArgs{Builder.getInt8Ty()->getPointerTo()};
			l::FunctionType *putsType = l::FunctionType::get(Builder.getInt32Ty(), putsArgs, false);

			//get the actual function
			l::Constant *putsFunc = Builder.GetInsertBlock()->getModule()->getOrInsertFunction("puts", putsType);

			finish(Builder.CreateCall(putsFunc, helloWorld, s("hello world")));
		}*/
	case ASTn("print_int"):
		{
			l::Value* printer = llvm_function(Builder, print_uint64_t, void_type, int64_type);
			finish(Builder.CreateCall(printer, std::vector<l::Value*>{field_results[0].IR}));
		}
	case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
		finish(Builder.CreateCall(llvm_function(Builder, generate_random, int64_type), std::vector<l::Value*>{}));
	case ASTn("if"):
		{
			//the condition statement
			if (target->fields[0].ptr == nullptr) return_code(null_AST, 0);
			auto condition = generate_IR(target->fields[0].ptr, 0);
			if (condition.error_code) return condition;

			if (type_check(RVO, condition.type, T::integer) != type_check_result::perfect_fit) return_code(type_mismatch, 0);

			if (stack_degree == 2)
			{
				size_result = return_size(target);
				if (size_result >= 1) storage_location = create_alloca(size_result);
			}

			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			uint64_t if_stack_position = object_stack.size();

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			l::Value* comparison = Builder.CreateICmpNE(condition.IR, llvm_integer(0), s("if() condition"));
			l::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			l::BasicBlock *ThenBB = l::BasicBlock::Create(thread_context, s("then"), TheFunction);
			l::BasicBlock *ElseBB = l::BasicBlock::Create(thread_context, s("else"));
			l::BasicBlock *MergeBB = l::BasicBlock::Create(thread_context, s("merge"));

			Builder.CreateCondBr(comparison, ThenBB, ElseBB);

			//sets the insert point to be the end of the "Then" basic block.
			Builder.SetInsertPoint(ThenBB);

			//calls with stack_degree as 1 in the called function, if it is 2 outside.
			Return_Info then_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
			if (then_IR.error_code) return then_IR;

			//get rid of any temporaries
			clear_stack(if_stack_position);

			Builder.CreateBr(MergeBB);
			ThenBB = Builder.GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			Builder.SetInsertPoint(ElseBB);

			Return_Info else_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, storage_location);
			if (else_IR.error_code) return else_IR;

			Type* result_type = then_IR.type; //first, we assume then_IR is the main type.
			//RVO, because that's what defines the slot.
			if (type_check(RVO, else_IR.type, then_IR.type) != type_check_result::perfect_fit)
			{
				result_type = else_IR.type; //if it didn't work, try seeing if else_IR can be the main type.
				if (type_check(RVO, then_IR.type, else_IR.type) != type_check_result::perfect_fit)
					return_code(type_mismatch, 2);
			}

			//for the second branch
			Builder.CreateBr(MergeBB);
			ElseBB = Builder.GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);

			uint64_t result_self_validity_guarantee = std::max(then_IR.self_validity_guarantee, else_IR.self_validity_guarantee);
			uint64_t result_target_hit_contract = std::min(then_IR.target_hit_contract, else_IR.target_hit_contract);
			if (stack_degree == 0)
			{
				l::Value* endPN = llvm_create_phi(Builder, then_IR.IR, else_IR.IR, then_IR.type, else_IR.type, ThenBB, ElseBB);
				finish_special_pointer(endPN, result_type, result_self_validity_guarantee, result_target_hit_contract);
			}
			else finish_special_stack_handled_pointer(storage_location, then_IR.type, result_self_validity_guarantee, result_target_hit_contract);
			//even though finish_pointer returns, the else makes it clear from first glance that it's not a continued statement.
		}

	case ASTn("label"):
		{
			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			l::Function *TheFunction = Builder.GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			l::BasicBlock *label = l::BasicBlock::Create(thread_context, "");
			auto label_insertion = labels.insert(std::make_pair(user_target, label_info(label, final_stack_position, true)));
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
				//Builder.ClearInsertionPoint() is bugged. try [label [goto a]]a. note that the label has two predecessors, one which is a null operand
				//so when you emit the branch for the label, even though the insertion point is clear, it still adds a predecessor to the block.
				//instead, we have to emit a junk block

				l::BasicBlock *JunkBB = l::BasicBlock::Create(thread_context, s("never reached"), Builder.GetInsertBlock()->getParent());
				Builder.SetInsertPoint(JunkBB);

				finish_special(nullptr, T::nonexistent);
			}

			//check finiteness
			l::AllocaInst* finiteness_pointer = l::cast<l::AllocaInst>(Builder.CreateIntToPtr(llvm_integer((uint64_t)&finiteness), int64_type->getPointerTo()));
			l::Value* current_finiteness = Builder.CreateLoad(finiteness_pointer);
			l::Value* comparison = Builder.CreateICmpNE(current_finiteness, llvm_integer(0), s("finiteness comparison"));


			l::Function *TheFunction = Builder.GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(thread_context, s("finiteness success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(thread_context, s("finiteness failure"));
			Builder.CreateCondBr(comparison, SuccessBB, FailureBB);

			//start inserting code in the "then" block
			//this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are already commands inside the "Then" basic block, we would be in trouble
			Builder.SetInsertPoint(SuccessBB);

			//decrease finiteness.
			l::Value* finiteness_minus_one = Builder.CreateSub(current_finiteness, llvm_integer(1));
			Builder.CreateStore(finiteness_minus_one, finiteness_pointer);

			emit_dtors(info.stack_size);
			Builder.CreateBr(labelsearch->second.block);
			Builder.ClearInsertionPoint();


			TheFunction->getBasicBlockList().push_back(FailureBB);
			Builder.SetInsertPoint(FailureBB);

			Return_Info Failure_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
			if (Failure_IR.error_code) return Failure_IR;

			if (stack_degree == 0) finish_special_pointer(Failure_IR.IR, Failure_IR.type, Failure_IR.self_validity_guarantee, Failure_IR.target_hit_contract);
			else finish_special_stack_handled_pointer(storage_location, Failure_IR.type, Failure_IR.self_validity_guarantee, Failure_IR.target_hit_contract);
		}
	case ASTn("scope"):
		finish(nullptr);
	case ASTn("pointer"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			if (found_AST->second.on_stack == stack_state::temp) return_code(pointer_to_temporary, 0);
			bool is_full_pointer = is_full(found_AST->second.on_stack);
			Type* new_pointer_type = get_non_convec_unique_type(Type("pointer", found_AST->second.type, is_full_pointer));

			l::Value* final_result = Builder.CreatePtrToInt(found_AST->second.IR, int64_type, s("flattening pointer"));

			finish_special_pointer(final_result, new_pointer_type, found_AST->second.self_lifetime, found_AST->second.self_lifetime);
		}
	case ASTn("load"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			//if on_stack is temp, it's not an alloca so we should copy instead of loading.
			Return_Info AST_to_load = found_AST->second;
			//todo: atomic load? maybe it's automatic.
			if (AST_to_load.on_stack == stack_state::temp) finish_special_pointer(AST_to_load.IR, AST_to_load.type, AST_to_load.self_validity_guarantee, AST_to_load.target_hit_contract);
			else
			{
				if (is_full(AST_to_load.type)) //the object has full lifetime
					finish_special_pointer(Builder.CreateLoad(AST_to_load.IR), AST_to_load.type, 0, 0);
				else finish_special_pointer(Builder.CreateLoad(AST_to_load.IR), AST_to_load.type, AST_to_load.self_lifetime, 0); //todo: atomic load? maybe it's automatic.
			}
		}
	case ASTn("concatenate"):
		{
			//optimization: if stack_degree == 1, then you've already gotten the size. look it up in a special location.

			uint64_t size[2] = {return_size(target->fields[0].ptr), return_size(target->fields[1].ptr)};
			size_result = size[0] + size[1];
			if (size[0] > 0 && size[1] > 0)
			{
				//we want the return objects to RVO. thus, we create a memory slot.
				if (stack_degree == 0 || stack_degree == 2) storage_location = create_alloca(size_result);

				l::AllocaInst* location[2];
				l::Value* pointer[2];
				Return_Info half[2];
				for (int x : {0, 1})
				{
					uint64_t offset = (x == 1) ? size[0] : 0;
					if (size[x] == 1) location[x] = l::cast<l::AllocaInst>(Builder.CreateConstInBoundsGEP2_64(storage_location, 0, offset, s("half of concatenate as an integer") + s(string{(char)x})));
					else
					{
						l::Type* pointer_to_array = llvm_array(size[x])->getPointerTo();
						pointer[x] = Builder.CreateConstInBoundsGEP2_64(storage_location, 0, offset, s("half of concatenate ") + s(string{(char)x}));
						location[x] = l::cast<l::AllocaInst>(Builder.CreatePointerCast(pointer[x], pointer_to_array, s("pointer cast to array")));
					}

					half[x] = generate_IR(target->fields[x].ptr, 1, location[x]);
					if (half[x].error_code) return half[x];
				}

				l::Value* final_value;
				if (stack_degree == 0) final_value = Builder.CreateLoad(storage_location, s("load concatenated object"));
				else final_value = storage_location;

				uint64_t result_target_life_guarantee = std::max(half[0].self_validity_guarantee, half[1].self_validity_guarantee);
				uint64_t result_target_hit_contract = std::min(half[0].target_hit_contract, half[1].target_hit_contract);
				//console << "concatenation " << concatenation->fields[0].num << '\n';
				Type* final_type = get_unique_type(concatenate_types(std::vector<Type*>{half[0].type, half[1].type}), true);
				console << "final type "; output_type(final_type);
				finish_special_stack_handled_pointer(final_value, final_type, result_target_life_guarantee, result_target_hit_contract);
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
				if (size[0] > 0) finish_special_stack_handled_pointer(half[0].IR, half[0].type, half[0].self_validity_guarantee, half[0].target_hit_contract);
				else finish_special_stack_handled_pointer(half[1].IR, half[1].type, half[1].self_validity_guarantee, half[1].target_hit_contract);
				//even if half[1] is 0, we return it anyway.
			}
		}

	case ASTn("store"):
		{
			if (field_results[0].type == nullptr) return_code(type_mismatch, 0);
			if (field_results[0].type->tag != Typen("pointer")) return_code(type_mismatch, 0);
			if (type_check(RVO, field_results[1].type, field_results[0].type->fields[0].ptr) != type_check_result::perfect_fit) return_code(type_mismatch, 1);
			if (field_results[0].target_hit_contract < field_results[1].self_validity_guarantee) return_code(store_pointer_lifetime_mismatch, 0);
			llvm::Value* memory_location = Builder.CreateIntToPtr(field_results[0].IR, llvm_type(get_size(field_results[1].type))->getPointerTo());
			if (requires_atomic(field_results[0].on_stack))
				Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, memory_location, field_results[1].IR, llvm::AtomicOrdering::Release); //todo: I don't think this works for large objects; memory location must match exactly. in any case, all we want is piecewise atomic.
			else Builder.CreateStore(field_results[1].IR, memory_location);
			finish(0);
		}
	case ASTn("dynamic"):
		{
			uint64_t size_of_object = return_size(target->fields[0].ptr);
			if (size_of_object >= 1)
			{
				l::Value* allocator = llvm_function(Builder, user_allocate_memory, int64_type, int64_type);
				l::Value* dynamic_object_address = Builder.CreateCall(allocator, std::vector<l::Value*>{llvm_integer(size_of_object)}, s("allocate memory"));

				//this represents either an integer or an array of integers.
				l::Type* target_pointer_type = llvm_type(size_of_object)->getPointerTo();

				//store the returned value into the acquired address
				l::Value* dynamic_object = Builder.CreateIntToPtr(dynamic_object_address, target_pointer_type);
				Builder.CreateStore(field_results[0].IR, dynamic_object);

				//create a pointer to the type of the dynamic pointer. but we serialize the pointer to be an integer.
				Type* type_of_dynamic_object = get_unique_type(field_results[0].type, false);
				l::Constant* integer_type_pointer = llvm_integer((uint64_t)type_of_dynamic_object);

				l::Value* indirect_object = llvm_function(Builder, make_dynamic, int64_type, int64_type, int64_type);
				l::Value* final_dynamic = Builder.CreateCall(indirect_object, std::vector<l::Value*>{dynamic_object_address, integer_type_pointer});
				finish(final_dynamic);
			}
			else
			{
				//zero array, for null dynamic object.
				finish(llvm_integer(make_dynamic(0, 0)));
			}
		}
	case ASTn("dynamic_conc"):
		{
			std::vector<l::Type*> argument_types{int64_type, int64_type};
			l::FunctionType* dynamic_conc_type = l::FunctionType::get(int64_type, argument_types, false);

			l::PointerType* dynamic_conc_type_pointer = dynamic_conc_type->getPointerTo();
			l::Constant *twister_address = llvm_integer((uint64_t)&concatenate_dynamic);
			l::Value* dynamic_conc_function = Builder.CreateIntToPtr(twister_address, dynamic_conc_type_pointer, s("convert integer address to actual function"));

			std::vector<l::Value*> arguments{field_results[0].IR, field_results[1].IR};
			l::Value* result_of_compile = Builder.CreateCall(dynamic_conc_function, arguments, s("dynamic concatenate"));

			//we're creating it in heap memory, but if we're copying pointers, we're in trouble.
			bool is_full_dynamic = is_full(field_results[0].type) && is_full(field_results[1].type);
			Type* dynamic_type = get_non_convec_unique_type(Type(Typen("dynamic pointer"), is_full_dynamic));

			finish_special(result_of_compile, dynamic_type);
		}
	case ASTn("compile"):
		{
			l::Value* compile_function = llvm_function(Builder, compile_user_facing, int64_type, int64_type);
			l::Value* result_of_compile = Builder.CreateCall(compile_function, std::vector<l::Value*>{field_results[0].IR}, s("compile"));
			finish(result_of_compile);
		}
	case ASTn("convert_to_AST"):
		{
			l::IntegerType* boolean = l::IntegerType::get(thread_context, 1);
			l::Value* checker_function = llvm_function(Builder, is_AST_user_facing, boolean, int64_type, int64_type);

			l::AllocaInst* pointer_reconstitution = l::cast<l::AllocaInst>(Builder.CreateIntToPtr(field_results[2].IR, llvm_array(2)->getPointerTo()));
			l::Value* pair_value = Builder.CreateLoad(pointer_reconstitution);

			l::Value* pointer = Builder.CreateExtractValue(pair_value, std::vector < unsigned > {0}, s("fields of hopeful AST"));
			l::Value* type = Builder.CreateExtractValue(pair_value, std::vector < unsigned > {1}, s("type of hopeful AST"));

			l::Value* previous_AST;
			if (type_check(RVO, field_results[1].type, nullptr) == type_check_result::perfect_fit) //previous AST is nullptr.
				previous_AST = llvm_integer(0);
			else if (type_check(RVO, field_results[1].type, T::AST_pointer) == type_check_result::perfect_fit) //previous AST actually exists
				previous_AST = field_results[1].IR;
			else return_code(type_mismatch, 1);

			std::vector<l::Value*> arguments{field_results[0].IR, type};
			l::Value* check_result = Builder.CreateCall(checker_function, arguments, s("checker"));


			l::Value* comparison = Builder.CreateICmpNE(check_result, l::ConstantInt::getFalse(thread_context), s("if() condition"));
			l::Function *TheFunction = Builder.GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(thread_context, s("success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(thread_context, s("failure"));
			l::BasicBlock *MergeBB = l::BasicBlock::Create(thread_context, s("merge"));
			Builder.CreateCondBr(comparison, SuccessBB, FailureBB);
			Builder.SetInsertPoint(SuccessBB);

			l::Value* converter_function = llvm_function(Builder, make_AST_pointer_from_dynamic, int64_type, int64_type, int64_type, int64_type);
			std::vector<l::Value*> arguments2{field_results[0].IR, previous_AST, pointer};
			l::Value* success_result = Builder.CreateCall(converter_function, arguments2, s("AST pointer conversion"));

			Builder.CreateBr(MergeBB);
			SuccessBB = Builder.GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(FailureBB);
			Builder.SetInsertPoint(FailureBB);

			//todo: expose the error object
			Return_Info failure_IR = generate_IR(target->fields[3].ptr, stack_degree != 0, storage_location);
			if (failure_IR.error_code) return failure_IR;
			if (type_check(RVO, failure_IR.type, T::AST_pointer) != type_check_result::perfect_fit) return_code(type_mismatch, 3);

			Builder.CreateBr(MergeBB);
			FailureBB = Builder.GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(MergeBB);
			Builder.SetInsertPoint(MergeBB);

			if (stack_degree == 0)
			{
				l::Value* endPN = llvm_create_phi(Builder, success_result, failure_IR.IR, T::AST_pointer, failure_IR.type, SuccessBB, FailureBB);
				finish_special(endPN, T::AST_pointer);
			}
			else finish_special_stack_handled(storage_location, T::AST_pointer);

		}
		/*
	case ASTn("temp_generate_AST"):
		{
			l::Value* generator = llvm_function(Builder, ASTmaker, int64_type);
			//l::Constant *twister_function = TheModule->getOrInsertFunction("ASTmaker", AST_maker_type);
			finish(Builder.CreateCall(generator, std::vector<l::Value*>{}, s("ASTmaker")));
		}*/
	case ASTn("static_object"):
		{
			l::Value* address_of_object = llvm_integer(target->fields[0].num);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				l::Type* pointer_to_array = llvm_type(size_of_object)->getPointerTo();
				storage_location = l::cast<l::AllocaInst>(Builder.CreatePointerCast(address_of_object, pointer_to_array, s("pointer cast to array")));
				final_stack_state = stack_state::full_might_be_visible;
				//whenever storage_location is set, we start to worry about on_stack.

				l::Value* final_value = stack_degree == 0 ? (l::Value*)Builder.CreateLoad(storage_location, s("load concatenated object")) : storage_location;
				finish_special_stack_handled_pointer(final_value, type_of_object, full_lifetime, full_lifetime);
			}
			else finish_special(nullptr, nullptr);
		}
	case ASTn("load_object"): //bakes in the value into the compiled function. changes by the function are temporary.
		{
			uint64_t* array_of_integers = (uint64_t*)(target->fields[0].ptr);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				std::vector<l::Constant*> values;
				values.reserve(size_of_object);
				for (int x = 0; x < size_of_object; ++x)
				{
					values.push_back(llvm_integer(array_of_integers[x]));
				}
				
				l::Constant* object;
				if (size_of_object > 1) object = l::ConstantArray::get(llvm_array(size_of_object), values);
				else object = values[0];
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
	std::vector<uAST*> AST_list{nullptr}; //start with nullptr as the default referenceable AST
	while (iterations--)
	{
		//create a random AST
		unsigned tag = mersenne() % (ASTn("never reached") - 1) + 1; //we skip 0 because that's a null AST
		unsigned pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST pointers. they will come at the beginning
		unsigned prev_AST = generate_exponential_dist() % AST_list.size(); //perhaps: prove that exponential_dist is desired.
		//birthday collisions is the problem. a concatenate with two branches will almost never appear, because it'll result in an active object duplication.
		//but does exponential falloff solve this problem in the way we want?


		int_or_ptr<uAST> fields[4]{nullptr, nullptr, nullptr, nullptr};
		int incrementor = 0;
		for (; incrementor < pointer_fields; ++incrementor)
			fields[incrementor] = AST_list.at(mersenne() % AST_list.size()); //get pointers to previous ASTs
		for (; incrementor < pointer_fields + AST_descriptor[tag].additional_special_fields; ++incrementor)
		{
			if (AST_descriptor[tag].parameter_types[incrementor] == T::integer)
				fields[incrementor] = generate_exponential_dist(); //get random integers and fill in the remaining fields
			else
			{
				console << "fuzztester doesn't know how to make this special type, so it's going to be 0's.\n";
				check(incrementor == pointer_fields + AST_descriptor[tag].additional_special_fields - 1, "egads! I need to build functionality for tracking the current parameter field offset and the current parameter type separately, because there are extra fields after the special field, and the special field may have size different from 1\n");
			}
		}
		uAST* test_AST = new uAST(tag, AST_list.at(prev_AST), fields[0], fields[1], fields[2], fields[3]);
		if (OLD_AST_OUTPUT) output_AST_and_previous(test_AST);
		output_AST_console_version a(test_AST);

		bool result = compile_and_run(test_AST);
		if (result)
		{
			AST_list.push_back(test_AST);
			console << AST_list.size() - 1 << "\n";
		}
		else delete test_AST;
		if (INTERACTIVE)
		{
			console << "Press enter to continue\n";
			std::cin.get();
		}
		console << "\n";
	}
}



#ifdef NDEBUG
panic time!
#endif
#include <iostream>
#include <sstream>
#include <istream>
//parses console input and generates ASTs.
using std::string; //this can't go inside source_reader for stupid C++ reasons
class source_reader
{

	std::unordered_map<string, uAST*> ASTmap = {{"0", nullptr}}; //from name to location. starts with 0 = nullptr.
	std::unordered_map<uAST**, string> goto_delay; //deferred binding of addresses, for goto. we can only see the labels at the end.
	uAST* delayed_binding_special_value = new uAST(1); //memleak
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
			//console << "word is " << word << "|||\n";
			return word;
		}
	}

	//continued_string is in case we already read the first token. then, that's the first thing to start with.
	uAST* read_single_AST(uAST* previous_AST, string continued_string = "")
	{
		string first = continued_string == "" ? get_token() : continued_string;
		char* new_type_location = new char[sizeof(uAST)]; //we have to make it now, so that we know where the AST will be. this lets us specify where our reference must be resolved.
		//uAST* new_type = new uAST(special);

		if (first != string(1, '[')) //it's a name reference.
		{
			//console << "first was " << first << '\n';
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
		if (VERBOSE_DEBUG) console << "AST tag was " << AST_type << "\n";
		uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;

		int_or_ptr<uAST> fields[max_fields_in_AST] = { nullptr, nullptr, nullptr, nullptr };

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
					goto_delay.insert(make_pair(&((uAST*)(new_type_location))->fields[field_num].ptr, delayed_binding_name));
				}
			}
			else //it's an integer
			{
				bool isNumber = true;
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, string("tried to input non-number ") + next_token);
				check(next_token.size(), "token is empty, probably a missing ]");
				fields[field_num] = std::stoull(next_token);
			}
		}
		//check(next_token == "]", "failed to have ]");
		uAST* new_type = new(new_type_location) uAST(AST_type, previous_AST, fields[0], fields[1], fields[2], fields[3]);

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
			console << "next char is" << (char)input.peek() << input.peek();
			console << '\n';
		}
		return new_type;


	}

	uAST* create_single_basic_block(bool create_brace_at_end = false)
	{
		uAST* current_previous = nullptr;
		if (create_brace_at_end == false)
		{
			if (std::cin.peek() == '\n') std::cin.ignore(1);
			for (string next_word = get_token(); next_word != ""; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference: " + next_word); //note that forward references in basic blocks are not allowed
			}
			return current_previous; //which is the very last.
		}
		else
		{
			for (string next_word = get_token(); next_word != "}"; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference: " + next_word);
			}
			return current_previous; //which is the very last
		}
	}

public:
	source_reader(std::istream& stream, char end) : input(stream) {}
	uAST* read()
	{
		uAST* end = create_single_basic_block();

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
	l::InitializeNativeTarget();
	l::InitializeNativeTargetAsmPrinter();
	l::InitializeNativeTargetAsmParser();



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
			console.setstate(std::ios_base::failbit);
			console.rdbuf(nullptr);
			llvm_console = &llvm_null_stream;
		}
		else error(string("unrecognized flag ") + argv[x]);
	}

	test_unique_types();
	debugtypecheck(T::nonexistent);

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
			console << "Input AST:\n";
			uAST* end = k.read();
			if (VERBOSE_DEBUG) console << "Done reading.\n";
			if (end == nullptr)
			{
				console << "it's nullptr\n";
				std::cin.get();
				continue;
			}
			output_AST_and_previous(end);
			compile_and_run(end);
			}
	}

	fuzztester(-1);
}