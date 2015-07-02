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
#include <array>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Support/raw_ostream.h> 
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "types.h"
#include "debugoutput.h"
#include "type_creator.h"
#include "user_functions.h"
#include "cs11.h"
#include "orc.h"
#include "helperfunctions.h"



namespace l = llvm;

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = false;
bool CONSOLE = false;
bool TIMER = false;
bool CONTINUOUS = false;
bool OLD_AST_OUTPUT = false;
bool FUZZTESTER_NO_COMPILE = false;
bool DONT_ADD_MODULE_TO_ORC = false;
bool DELETE_MODULE_IMMEDIATELY = false;

basic_onullstream<char> null_stream;
std::ostream& console = std::cerr;
l::raw_null_ostream llvm_null_stream;
l::raw_ostream* llvm_console = &l::outs();
thread_local compiler_host* c;
thread_local llvm::LLVMContext* context;
thread_local llvm::IRBuilder<>* builder;
thread_local uint64_t finiteness;
std::unique_ptr<llvm::TargetMachine> TM(llvm::EngineBuilder().selectTarget());

#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count(); //one seed for everything is a bad idea
thread_local std::mt19937_64 mersenne(seed);
uint64_t generate_random() { return mersenne(); }

//generates an approximately exponential distribution using bit twiddling
uint64_t generate_exponential_dist()
{
	uint64_t cutoff = generate_random();
	cutoff ^= cutoff - 1; //1s starting from the lowest digit of cutoff
	return generate_random() & cutoff;
}


#include <llvm/Transforms/Utils/Cloning.h>
//return value is the error code, which is 0 if successful
unsigned compiler_object::compile_AST(uAST* target)
{
	llvm::IRBuilder<> new_builder(*new_context);
	std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), *new_context)); //should be unique ptr because ownership will be transferred
	struct builder_context_stack
	{
		llvm::IRBuilder<>* old_builder = builder;
		llvm::LLVMContext* old_context = context;
		builder_context_stack(llvm::IRBuilder<>* b, llvm::LLVMContext* c) {
			builder = b;
			context = c;
		}
		~builder_context_stack() {
			builder = old_builder;
			context = old_context;
		}
	}
	builder_context_stack(&new_builder, new_context.get());

	if (VERBOSE_DEBUG) console << "starting compilation\ntarget is " << target << '\n'; //necessary in case it crashes here
	if (target == nullptr) return IRgen_status::null_AST;
	using namespace llvm;
	FunctionType *dummy_type = FunctionType::get(void_type(), false);;

	Function *dummy_func = Function::Create(dummy_type, Function::ExternalLinkage, "dummy_func");

	BasicBlock *BB = BasicBlock::Create(*context, "entry", dummy_func);
	new_builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code) return return_object.error_code; //error

	return_type = return_object.type;
	check(return_type == get_unique_type(return_type, false), "compilation returned a non-unique type");
	//can't be u::does_not_return, because goto can't go forward past the end of a function.

	auto size_of_return = get_size(return_object.type);
	FunctionType* FT = FunctionType::get(llvm_type_including_void(size_of_return), false);
	if (VERBOSE_DEBUG) console << "Size of return is " << size_of_return << '\n';
	std::string function_name = GenerateUniqueName("");
	Function *F = Function::Create(FT, Function::ExternalLinkage, function_name, M.get()); //marking this private linkage seems to fail
	F->addFnAttr(Attribute::NoUnwind); //7% speedup

	F->getBasicBlockList().splice(F->begin(), dummy_func->getBasicBlockList());
	delete dummy_func;
	if (size_of_return) builder->CreateRet(return_object.IR);
	else builder->CreateRetVoid();

	M->print(*llvm_console, nullptr);
	check(!l::verifyFunction(*F, llvm_console), "verification failed");
	if (OPTIMIZE)
	{
		console << "optimized code: \n";
		l::legacy::FunctionPassManager FPM(M.get());
		M->setDataLayout(*TM->getDataLayout());

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

		M->print(*llvm_console, nullptr);
	}

	if (!DONT_ADD_MODULE_TO_ORC)
	{
		if (VERBOSE_DEBUG) console << "adding module...\n";
		auto H = J.addModule(std::move(M));

		// Get the address of the JIT'd function in memory.
		//this seems unreasonably expensive. even having a few functions around makes it cost 5% total run time.
		auto ExprSymbol = J.findUnmangledSymbol(function_name);

		fptr = (void*)(intptr_t)(ExprSymbol.getAddress());

		if (DELETE_MODULE_IMMEDIATELY)
			J.removeModule(H);
		else
			result_module = H;
	}
	else
	{
		fptr = (void*)2222222ull;
		//std::unique_ptr<llvm::Module> a(C.takeM()); //delete the module
	}
	return 0;
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

/** generate_IR() is the main code that transforms AST into LLVM IR. it is called with the AST to be transformed
'
storage_location is for RVO. if stack_degree says that the return object should be stored somewhere, the location will be "memory_location desired". storage is done by the returning function.
ASTs that are directly inside basic blocks should allocate their own stack memory, so they are given stack_degree = 2.
	this tells them to create a memory location and to place the return value inside it. the memory location is returned.
ASTs that should place their return object inside an already-created memory location are given stack_degree = 1.
	then, they store the return value into "desired". storage_location is returned.
ASTs that return SSA objects are given stack_degree = 0. these return the value directly.

to create alloca elements, we first create a dummy array. after all the components are built, we find what the size is, and then change the array size.
we need the dummy array because we can't figure out what the size is beforehand: the ASTs need to be immut before they can be read.
we need it to be an array always, because GEP must have a valid type. we can't allocate an integer for the dummy array, or else GEP will try to dereference an integer, then segfault.

Steps of generate_IR:
check if generate_IR will be in an infinite loop, by using loop_catcher to store the previously seen ASTs.
run generate_IR on the previous AST in the basic block, if such an AST exists.
	after this previous AST is generated, we can figure out where the current AST lives on the stack. this uses incrementor_for_lifetime.
generate_IR is run on fields of the AST. it knows how many fields are needed by using fields_to_compile from AST_descriptor[] 
then, the main IR generation is done using a switch-case on a tag.
the return value is created using the finish() macros. these automatically pull in any miscellaneous necessary information, such as stack lifetimes, and bundles them into a Return_Info. furthermore, if the return value needs to be stored in a stack location, finish() does this automatically using the move_to_stack lambda.

note that while we have a rich type system, the only types that llvm sees are int64s and arrays of int64s. pointers, locks, etc. have their information stripped, so llvm only sees the size of each object. we forcefully cast things to integers.
	for an object of size 1, its llvm-type is an integer
	for an object of size N>=2, the llvm-type is an alloca'd array of N integers, which is [12 x i64]*. note the * - it's a pointer to an array, not the array itself.
for objects that end up on the stack, their return value is either i64* or [S x i64]*.
the actual allocas are always [S x i64]*, even when S = 1. but, getting active objects returns i64*.
currently, the finish() macro takes in the array itself, not the pointer-to-array.
*/
Return_Info compiler_object::generate_IR(uAST* user_target, unsigned stack_degree, memory_location desired)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
#define return_code(X, Y) do { error_location = user_target; error_field = Y; return Return_Info(IRgen_status::X, nullptr, u::null, stack_state::temp, 0, 0, 0); } while (0)


	if (VERBOSE_DEBUG)
	{
		console << "generate_IR single AST start ^^^^^^^^^\n";
		if (user_target) output_AST(user_target);
		console << "stack degree " << stack_degree;
		console << ", storage location is ";
		if (desired.base != nullptr)
		{
			desired.base->print(*llvm_console);
			console << " offset " << desired.offset << '\n';
		}
		else console << "null";
		console << '\n';
	}

	if (stack_degree == 2) check(desired.base == nullptr, "if stack degree is 2, we should have a nullptr storage location");
	if (stack_degree == 1) check(desired.base != nullptr, "if stack degree is 1, we should have a storage location");

	//generate_IR is allowed to take nullptr. otherwise, we need an extra check beforehand. this extra check creates code duplication, which leads to typos when indices aren't changed.
	//check(target != nullptr, "generate_IR should never receive a nullptr target");
	if (user_target == nullptr) return Return_Info();

	//if we've seen this AST before, we're stuck in an infinite loop. return an error.
	if (loop_catcher.find(user_target) != loop_catcher.end()) return_code(infinite_loop, 10); //for now, 10 is a special value, and means not any of the fields
	loop_catcher.insert(user_target); //we've seen this AST now.

	//handle the copying of the AST.
	auto search_for_copy = copy_mapper.find(user_target);
	uAST* target;
	bool made_a_copy;
	if (search_for_copy == copy_mapper.end())
	{
		if (VERBOSE_GC) console << "copying AST\n";
		target = copy_AST(user_target); //make a bit copy. the fields will still point to the old ASTs; that will be corrected in finish().
		//this relies on loads in x64 being atomic, which may not be wholly true.
		copy_mapper.insert({user_target, target});
		made_a_copy = true;
	}
	else
	{
		target = search_for_copy->second;
		made_a_copy = false;
	}

	//we'll need to eventually replace the fields in our copy with their correct versions. this does that for us.
	auto replace_field_pointer_with_immut_version = [&](uAST*& pointer)
	{
		if (pointer == nullptr) return;
		auto immut_pointer = copy_mapper.find(pointer);
		check(immut_pointer != copy_mapper.end(), "where did my pointer go");
		pointer = immut_pointer->second;
	};

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

	///NOTE: these are the set of default values. they become hidden parameters to finish().

	if (stack_degree == 2) desired.base = create_empty_alloca();

	//after compiling the previous elements in the basic block, we find the lifetime of this element
	uint64_t lifetime_of_return_value = incrementor_for_lifetime++;

	uint64_t final_stack_position = object_stack.size();

	stack_state final_stack_state = stack_state::temp;
	if (stack_degree != 0) final_stack_state = stack_state::cheap_alloca; //the default. unless any further information appears.

	///end default values

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.


	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	//if it's necessary to create and write to storage_location, we do so.
	//if move_to_stack == true, it writes into a previously created storage_location
	//if stack_degree >= 1, this will ignore return_value and use "memory_location desired" instead
	auto finish_internal = [&](l::Value* return_value, Type* type, uint64_t self_validity_guarantee, uint64_t target_hit_contract, bool move_to_stack) -> Return_Info
	{
		check(type == get_unique_type(type, false), "returned non unique type in finish()");

		if (made_a_copy)
		{
			replace_field_pointer_with_immut_version(target->preceding_BB_element);
			for (uint64_t x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x)
				replace_field_pointer_with_immut_version(target->fields[x].ptr);
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
			builder->GetInsertBlock()->getParent()->print(*llvm_console, nullptr);
			console << "generate_IR single AST " << target << " " << AST_descriptor[target->tag].name << " vvvvvvvvv\n";
		}

		memory_location stack_return_location; //for stack_degree >= 1, this takes the place of return_value

		uint64_t size_of_return = get_size(type);
		if (stack_degree == 2)
		{
			if (size_of_return >= 1)
			{
				if (size_of_return > 1) desired.cast_base(size_of_return);
				if (move_to_stack) desired.store(return_value, size_of_return);

				stack_return_location = desired;
				stack_return_location.set_stored_size(size_of_return);
			}
			else desired.base->eraseFromParent();
		}
		if (stack_degree == 1 && move_to_stack) //otherwise, do nothing.
		{
			if (size_of_return >= 1)
			{
				//we can't defer RVO all the way to the stack_degree = 2. but we can move it up one to concatenate() if we want. maybe we won't bother.

				desired.store(return_value, size_of_return);
				stack_return_location = desired;
				stack_return_location.set_stored_size(size_of_return);
			}
		}
		//we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for justification.
		if (stack_degree == 2) clear_stack(final_stack_position);

		if (size_of_return >= 1)
		{
			if (stack_degree >= 1)
			{
				object_stack.push({user_target, true});
				auto insert_result = objects.insert({user_target, Return_Info(IRgen_status::no_error, stack_return_location, type, final_stack_state, lifetime_of_return_value, self_validity_guarantee, target_hit_contract)});
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication, 10);
				return Return_Info(IRgen_status::no_error, stack_return_location, type, final_stack_state, lifetime_of_return_value, self_validity_guarantee, target_hit_contract);
			}
			else //stack_degree == 0
			{
				object_stack.push(std::make_pair(user_target, false));
				return Return_Info(IRgen_status::no_error, return_value, type, final_stack_state, lifetime_of_return_value, self_validity_guarantee, target_hit_contract);
			}
		}
		else return Return_Info();
	};

	//call the finish macros when you've constructed something.
	//_pointer suffix is when target lifetime information is relevant (for cheap pointers).
	//_stack_handled if your AST has already written in the object into its memory location, for stack_degree == 1. for example, any function that passes through "desired" is a pretty good candidate.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.
	//remember: pass the value itself if stack_degree == 0, and pass a pointer to the value if stack_degree == 1 or 2.

	//these are for when we need to specify the return type.
#define finish_special_pointer(X, type, u, l) do {return finish_internal(X, type, u, l, true); } while (0)
#define finish_special(X, type) do { finish_special_pointer(X, type, 0, -1ll); } while (0)
#define finish_special_stack_handled(X, type) do { return finish_internal(X, type, 0, -1ll, false); } while (0)
#define finish_special_stack_handled_pointer(X, type, u, l) do { return finish_internal(X, type, u, l, false); } while (0)

	//make sure not to duplicate X in the expression.
#define finish_pointer(X, u, l) do {check(AST_descriptor[target->tag].return_object.state != special_return, "need to specify type"); finish_special_pointer(X, get_unique_type(AST_descriptor[target->tag].return_object.type, false), u, l); } while (0)
#define finish(X) do { finish_pointer(X, 0, -1ll); } while (0)
#define finish_stack_handled(X) do {check(AST_descriptor[target->tag].return_object.state != special_return, "need to specify type"); return finish_internal(X, get_unique_type(AST_descriptor[target->tag].return_object.type, false), 0, -1ll, false); } while (0)
#define finish_stack_handled_pointer(X, u, l) do {check(AST_descriptor[target->tag].return_object.state != special_return, "need to specify type"); return finish_internal(X, get_unique_type(AST_descriptor[target->tag].return_object.type, false), u, l, false); } while (0)


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
			output_type_and_previous(AST_descriptor[target->tag].parameter_types[x].type);
		}

		//check that the type matches.
		if (AST_descriptor[target->tag].parameter_types[x].state != parameter_no_type_check) //for fields that are compiled but not type checked
		{
			if (get_unique_type(AST_descriptor[target->tag].parameter_types[x].type, false) == u::does_not_return)
				finish_special(nullptr, u::does_not_return); //just get out of here, since we're never going to run the current command anyway.
			//this is fine with labels even though labels require emission whether reached or not, because labels don't compile using the default mechanism
			//even if there are labels skipped over by this escape, nobody can see them because of our try-catch goto scheme.
			if (type_check(RVO, result.type, get_unique_type(AST_descriptor[target->tag].parameter_types[x].type, false)) != type_check_result::perfect_fit) return_code(type_mismatch, x);
		}

		field_results.push_back(result);
	}

	//we generate code for the AST, depending on its tag.
	switch (target->tag)
	{
	case ASTn("integer"): finish(llvm_integer(target->fields[0].num));
	case ASTn("add"): finish(builder->CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"): finish(builder->CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("increment"): finish(builder->CreateAdd(field_results[0].IR, llvm_integer(1), s("increment")));
	/*case ASTn("hello"):
		{
			l::Value *helloWorld = builder->CreateGlobalStringPtr("hello world!");

			//create the function type
			std::vector<l::Type*> putsArgs{builder->getInt8Ty()->getPointerTo()};
			l::FunctionType *putsType = l::FunctionType::get(builder->getInt32Ty(), putsArgs, false);

			//get the actual function
			l::Constant *putsFunc = builder->GetInsertBlock()->getModule()->getOrInsertFunction("puts", putsType);

			finish(builder->CreateCall(putsFunc, helloWorld, s("hello world")));
		}*/
	case ASTn("print_int"):
		{
			l::Value* printer = llvm_function(print_uint64_t, void_type(), int64_type());
			finish(builder->CreateCall(printer, std::vector<l::Value*>{field_results[0].IR})); //, s("print"). can't give name to void-return functions
		}
	case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
		finish(builder->CreateCall(llvm_function(generate_random, int64_type()), std::vector<l::Value*>{}, s("random")));
	case ASTn("if"):
		{
			//the condition statement
			if (target->fields[0].ptr == nullptr) return_code(null_AST, 0);
			auto condition = generate_IR(target->fields[0].ptr, 0);
			if (condition.error_code) return condition;

			if (type_check(RVO, condition.type, u::integer) != type_check_result::perfect_fit) return_code(type_mismatch, 0);


			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			uint64_t if_stack_position = object_stack.size();

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			l::Value* comparison = builder->CreateICmpNE(condition.IR, llvm_integer(0), s("if() condition"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the 'then' block at the end of the function.
			l::BasicBlock *ThenBB = l::BasicBlock::Create(*context, s("then"), TheFunction);
			l::BasicBlock *ElseBB = l::BasicBlock::Create(*context, s("else"));
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"));

			builder->CreateCondBr(comparison, ThenBB, ElseBB);

			//sets the insert point to be the end of the "Then" basic block.
			builder->SetInsertPoint(ThenBB);

			//calls with stack_degree as 1 in the called function, if it is 2 outside.
			Return_Info then_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, desired);
			if (then_IR.error_code) return then_IR;

			//get rid of any temporaries
			clear_stack(if_stack_position);

			builder->CreateBr(MergeBB);
			ThenBB = builder->GetInsertBlock();

			TheFunction->getBasicBlockList().push_back(ElseBB);
			builder->SetInsertPoint(ElseBB);

			Return_Info else_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, desired);
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
			builder->CreateBr(MergeBB);
			ElseBB = builder->GetInsertBlock();

			// Emit merge block.
			TheFunction->getBasicBlockList().push_back(MergeBB);
			builder->SetInsertPoint(MergeBB);
			if (else_IR.type == u::does_not_return)
				finish_special_stack_handled_pointer(then_IR.IR, then_IR.type, then_IR.self_validity_guarantee, then_IR.target_hit_contract);
			else if (then_IR.type == u::does_not_return)
				finish_special_stack_handled_pointer(else_IR.IR, else_IR.type, else_IR.self_validity_guarantee, else_IR.target_hit_contract);

			uint64_t result_self_validity_guarantee = std::max(then_IR.self_validity_guarantee, else_IR.self_validity_guarantee);
			uint64_t result_target_hit_contract = std::min(then_IR.target_hit_contract, else_IR.target_hit_contract);
			if (stack_degree == 0)
			{
				l::Value* endPN = llvm_create_phi(then_IR.IR, else_IR.IR, then_IR.type, else_IR.type, ThenBB, ElseBB);
				finish_special_pointer(endPN, result_type, result_self_validity_guarantee, result_target_hit_contract);
			}
			else finish_special_stack_handled_pointer(then_IR.IR, then_IR.type, result_self_validity_guarantee, result_target_hit_contract); //todo: use the result type, not then_IR's type.
			//even though finish_pointer returns, the else makes it clear from first glance that it's not a continued statement.
		}

	case ASTn("label"):
		{
			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the block into the function, or else it'll leak when we return_code
			l::BasicBlock *label = l::BasicBlock::Create(*context, "", TheFunction);
			auto label_insertion = labels.insert(std::make_pair(user_target, label_info(label, final_stack_position, true)));
			if (label_insertion.second == false) return_code(label_duplication, 0);

			Return_Info scoped = generate_IR(target->fields[0].ptr, 0);
			if (scoped.error_code) return scoped;

			//this expires the label, so that goto knows that the label is behind. with finiteness, this means the label isn't guaranteed.
			label_insertion.first->second.is_forward = false;

			builder->CreateBr(label);
			builder->SetInsertPoint(label);
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
				builder->CreateBr(labelsearch->second.block);
				//builder->ClearInsertionPoint() is bugged. try [label [goto a]]a. note that the label has two predecessors, one which is a null operand
				//so when you emit the branch for the label, even though the insertion point is clear, it still adds a predecessor to the block.
				//instead, we have to emit a junk block

				l::BasicBlock *JunkBB = l::BasicBlock::Create(*context, s("never reached"), builder->GetInsertBlock()->getParent());
				builder->SetInsertPoint(JunkBB);

				finish_special(nullptr, u::does_not_return);
			}

			//check finiteness
			l::AllocaInst* finiteness_pointer = l::cast<l::AllocaInst>(builder->CreateIntToPtr(llvm_integer((uint64_t)&finiteness), int64_type()->getPointerTo()));
			l::Value* current_finiteness = builder->CreateLoad(finiteness_pointer);
			l::Value* comparison = builder->CreateICmpNE(current_finiteness, llvm_integer(0), s("finiteness comparison"));


			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("finiteness success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(*context, s("finiteness failure"));
			builder->CreateCondBr(comparison, SuccessBB, FailureBB);

			//start inserting code in the "then" block
			//this actually sets the insert point to be the end of the "Then" basic block. we're relying on the block being empty.
			//if there are already commands inside the "Then" basic block, we would be in trouble
			builder->SetInsertPoint(SuccessBB);

			//decrease finiteness. this comes before emission of the success branch.
			l::Value* finiteness_minus_one = builder->CreateSub(current_finiteness, llvm_integer(1));
			builder->CreateStore(finiteness_minus_one, finiteness_pointer);

			Return_Info Success_IR = generate_IR(target->fields[1].ptr, 0);
			if (Success_IR.error_code) return Success_IR;

			emit_dtors(info.stack_size);
			builder->CreateBr(labelsearch->second.block);
			builder->ClearInsertionPoint();


			TheFunction->getBasicBlockList().push_back(FailureBB);
			builder->SetInsertPoint(FailureBB);

			Return_Info Failure_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, desired);
			if (Failure_IR.error_code) return Failure_IR;

			finish_special_stack_handled_pointer(Failure_IR.IR, Failure_IR.type, Failure_IR.self_validity_guarantee, Failure_IR.target_hit_contract);
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

			l::Value* final_result = builder->CreatePtrToInt(found_AST->second.place.get_location(), int64_type(), s("flattening pointer"));

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
				llvm::Value* final_value = builder->CreateLoad(AST_to_load.place.get_location()); //loads the int.
				//note that the IR value is either i64* or [S x i64]*. if it's a single integer, the array is already removed.
				//if (get_size(AST_to_load.type) == 1) final_value = builder->CreateExtractValue(final_value, std::vector < unsigned > {0}); //it's an integer
				
				uint64_t lifetime = (is_full(AST_to_load.type)) ? 0 : AST_to_load.self_lifetime;
				finish_special_pointer(final_value, AST_to_load.type, lifetime, 0);
			}
		}
	case ASTn("concatenate"):
		{
			//optimization: if stack_degree == 1, then you've already gotten the size. look it up in a special location.

			if (stack_degree == 0) desired.base = create_empty_alloca(); //we want the return objects to RVO. thus, we create a memory slot
			uint64_t original_offset = desired.offset;
			Return_Info half[2];
			for (int x : {0, 1})
			{
				half[x] = generate_IR(target->fields[x].ptr, 1, desired);
				if (half[x].error_code) return half[x];
				if (half[x].type == u::does_not_return) finish_special(nullptr, u::does_not_return);
				desired.offset += get_size(half[x].type);
			}
			uint64_t final_size = desired.offset - original_offset;
			desired.offset -= final_size;
			if (final_size == 0) finish_special(nullptr, u::null);
			l::Value* final_value;
			if (stack_degree == 0)
			{
				desired.cast_base(final_size);
				final_value = builder->CreateLoad(desired.get_location(final_size));
			}
			else
			{
				final_value = nullptr;
				desired.set_stored_size(final_size); //automatically becomes the proper return value, like a hidden parameter to finish()
			}

			uint64_t result_target_life_guarantee = std::max(half[0].self_validity_guarantee, half[1].self_validity_guarantee);
			uint64_t result_target_hit_contract = std::min(half[0].target_hit_contract, half[1].target_hit_contract);
			Type* final_type = get_unique_type(concatenate_types(std::vector < Type* > {half[0].type, half[1].type}), true);
			//console << "final type "; output_type(final_type);
			finish_special_stack_handled_pointer(final_value, final_type, result_target_life_guarantee, result_target_hit_contract);
		}

	case ASTn("store"):
		{
			if (field_results[0].type == nullptr) return_code(type_mismatch, 0);
			if (field_results[0].type->tag != Typen("pointer")) return_code(type_mismatch, 0);
			if (type_check(RVO, field_results[1].type, field_results[0].type->fields[0].ptr) != type_check_result::perfect_fit) return_code(type_mismatch, 1);
			if (field_results[0].target_hit_contract < field_results[1].self_validity_guarantee) return_code(store_pointer_lifetime_mismatch, 0);
			llvm::Value* memory_location = builder->CreateIntToPtr(field_results[0].IR, llvm_type(get_size(field_results[1].type))->getPointerTo());
			if (requires_atomic(field_results[0].on_stack))
				builder->CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, memory_location, field_results[1].IR, llvm::AtomicOrdering::Release); //todo: I don't think this works for large objects; memory location must match exactly. in any case, all we want is piecewise atomic.
			else builder->CreateStore(field_results[1].IR, memory_location);
			finish(0);
			//todo: this doesn't work properly. we need to consider the difference between [int64 x 1] on stack and int64 return type.
		}
	case ASTn("dynamic"):
		{
			uint64_t size_of_object = get_size(field_results[0].type);
			if (size_of_object >= 1)
			{
				l::Value* allocator = llvm_function(user_allocate_memory, int64_type(), int64_type());
				l::Value* dynamic_object_address = builder->CreateCall(allocator, std::vector<l::Value*>{llvm_integer(size_of_object)}, s("dyn_allocate_memory"));

				//this represents either an integer or an array of integers.
				l::Type* target_pointer_type = llvm_type(size_of_object)->getPointerTo();

				//store the returned value into the acquired address
				l::Value* dynamic_object = builder->CreateIntToPtr(dynamic_object_address, target_pointer_type);
				builder->CreateStore(field_results[0].IR, dynamic_object);

				//create a pointer to the type of the dynamic pointer. but we serialize the pointer to be an integer.
				Type* type_of_dynamic_object = get_unique_type(field_results[0].type, false);
				l::Constant* integer_type_pointer = llvm_integer((uint64_t)type_of_dynamic_object);

				//we now serialize both objects to become integers.
				l::Value* undef_value = l::UndefValue::get(llvm_array(2));
				l::Value* first_value = builder->CreateInsertValue(undef_value, dynamic_object_address, std::vector < unsigned > { 0 });
				l::Value* full_value = builder->CreateInsertValue(first_value, integer_type_pointer, std::vector < unsigned > { 1 });
				finish(full_value);
			}
			else
			{
				//zero array, for null dynamic object.
				finish(l::ConstantArray::get(llvm_array(2), std::vector<l::Constant*>{llvm_integer(0), llvm_integer(0)}));
			}
		}
	case ASTn("dynamic_conc"):
		{
			l::Value* pointer[2];
			l::Value* type[2];
			for (int x : {0, 1})
			{
				//can't gep because it's not in memory
				pointer[x] = builder->CreateExtractValue(field_results[x].IR, std::vector<unsigned>{0}, s("object pointer of dynamic"));
				type[x] = builder->CreateExtractValue(field_results[x].IR, std::vector<unsigned>{1}, s("type pointer of dynamic"));
			}

			std::vector<l::Type*> argument_types{int64_type(), int64_type(), int64_type(), int64_type()};
			//l::Type* return_type = llvm_array(2);

			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			//if ever the optimization changes, we might be in trouble.
			std::vector<l::Type*> return_types{int64_type(), int64_type()};
			l::Type* return_type = l::StructType::get(*context, return_types);
			l::FunctionType* dynamic_conc_type = l::FunctionType::get(return_type, argument_types, false);

			l::PointerType* dynamic_conc_type_pointer = dynamic_conc_type->getPointerTo();
			l::Constant *twister_address = llvm_integer((uint64_t)&concatenate_dynamic);
			l::Value* dynamic_conc_function = builder->CreateIntToPtr(twister_address, dynamic_conc_type_pointer, s("convert integer address to actual function"));

			std::vector<l::Value*> arguments{pointer[0], type[0], pointer[1], type[1]};
			l::Value* result_of_compile = builder->CreateCall(dynamic_conc_function, arguments, s("dynamic concatenate"));




			//this transformation is necessary because array<uint64_t, 2> becomes {i64, i64}
			//using our current set of optimization passes, this operation isn't optimized to anything better.
			l::Value* undef_value = l::UndefValue::get(llvm_array(2));

			l::Value* first_return = builder->CreateExtractValue(result_of_compile, std::vector<unsigned>{0});
			l::Value* first_value = builder->CreateInsertValue(undef_value, first_return, std::vector<unsigned>{0});

			l::Value* second_return = builder->CreateExtractValue(result_of_compile, std::vector<unsigned>{1});
			l::Value* full_value = builder->CreateInsertValue(first_value, second_return, std::vector<unsigned>{1});

			bool is_full_dynamic = is_full(field_results[0].type) && is_full(field_results[1].type);
			Type* dynamic_type = get_non_convec_unique_type(Type(Typen("dynamic pointer"), is_full_dynamic));

			finish_special(full_value, dynamic_type);
		}
	case ASTn("compile"):
		{
			l::Value* compile_function = llvm_function(compile_returning_legitimate_object, void_type(), int64_type()->getPointerTo(), int64_type());

			llvm::AllocaInst* return_holder = create_actual_alloca(3);
			llvm::Value* forcing_return_type = builder->CreatePointerCast(return_holder, int64_type()->getPointerTo(), "forcing return type");
			builder->CreateCall(compile_function, std::vector<l::Value*>{forcing_return_type, field_results[0].IR}); //, s("compile"). void type means no name allowed
			auto error_object = memory_location(return_holder, 1);
			Type* error_type = concatenate_types(std::vector < Type* > {u::integer, u::cheap_dynamic_pointer});
			object_stack.push({user_target, true});
			auto insert_result = objects.insert({user_target, Return_Info(IRgen_status::no_error, error_object, error_type, final_stack_state, lifetime_of_return_value, lifetime_of_return_value, lifetime_of_return_value)});
			if (!insert_result.second) return_code(active_object_duplication, 10);


			auto error_code = memory_location(return_holder, 1);
			auto condition = builder->CreateLoad(error_code.get_location(1));


			l::Value* comparison = builder->CreateICmpNE(condition, llvm_integer(0), s("compile branching"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(*context, s("failure"));
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"));
			builder->CreateCondBr(comparison, SuccessBB, FailureBB);
			builder->SetInsertPoint(SuccessBB);


			Return_Info success_branch = generate_IR(target->fields[1].ptr, 0);
			if (success_branch.error_code) return success_branch;

			builder->CreateBr(MergeBB);
			SuccessBB = builder->GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(FailureBB);
			builder->SetInsertPoint(FailureBB);

			Return_Info error_branch = generate_IR(target->fields[2].ptr, 0);
			if (error_branch.error_code) return error_branch;

			builder->CreateBr(MergeBB);
			FailureBB = builder->GetInsertBlock();
			TheFunction->getBasicBlockList().push_back(MergeBB);
			builder->SetInsertPoint(MergeBB);


			clear_stack(final_stack_position);
			auto return_location = memory_location(return_holder, 0);
			auto return_object = builder->CreateLoad(return_location.get_location(1));
			finish(return_object);
		}
	case ASTn("convert_to_AST"):
		{
			l::Value* converter = llvm_function(dynamic_to_AST, int64_type(), int64_type(), int64_type(), int64_type(), int64_type());

			l::Value* previous_AST;
			if (type_check(RVO, field_results[1].type, nullptr) == type_check_result::perfect_fit) //previous AST is nullptr.
				previous_AST = llvm_integer(0);
			else if (type_check(RVO, field_results[1].type, u::AST_pointer) == type_check_result::perfect_fit) //previous AST actually exists
				previous_AST = field_results[1].IR;
			else return_code(type_mismatch, 1);


			l::Value* pointer = builder->CreateExtractValue(field_results[2].IR, std::vector < unsigned > {0}, s("fields of hopeful AST"));
			l::Value* type = builder->CreateExtractValue(field_results[2].IR, std::vector < unsigned > {1}, s("type of hopeful AST"));

			std::vector<l::Value*> arguments{field_results[0].IR, previous_AST, pointer, type};
			l::Value* AST_result = builder->CreateCall(converter, arguments, s("converter"));

			finish(AST_result);

		}
		/*
	case ASTn("temp_generate_AST"):
		{
			l::Value* generator = llvm_function(ASTmaker, int64_type());
			//l::Constant *twister_function = TheModule->getOrInsertFunction("ASTmaker", AST_maker_type);
			finish(builder->CreateCall(generator, std::vector<l::Value*>{}, s("ASTmaker")));
		}*/
	/*case ASTn("static_object"):
		{
			//what does this even mean? we load a pointer, but if it's embedded, then we load the internal values instead?
			//the problem is that we can't choose the memory location. but if stack_degree = 1, memory location is already fixed.
			//we should instead have the type be a pointer. you're loading the pointer.
			//then, is this even necessary? why not require the other one instead?
				//but, is it possible to embed a cheap pointer in a dynamic poitner? how do you do that instead of dynamic-in-dynamic?
			l::Value* address_of_object = llvm_integer(target->fields[0].num);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				l::Type* pointer_to_array = llvm_type(size_of_object)->getPointerTo();
				storage_location = l::cast<l::AllocaInst>(builder->CreatePointerCast(address_of_object, pointer_to_array, s("pointer cast to array")));
				final_stack_state = stack_state::full_might_be_visible;
				//whenever storage_location is set, we start to worry about on_stack.

				l::Value* final_value = stack_degree == 0 ? (l::Value*)builder->CreateLoad(storage_location, s("load concatenated object")) : storage_location;
				finish_special_stack_handled_pointer(final_value, type_of_object, full_lifetime, full_lifetime);
			}
			else finish_special(nullptr, nullptr);
		} */
	case ASTn("load_object"): //bakes in the value into the compiled function. changes by the function are temporary.
		{
			uint64_t* array_of_integers = (uint64_t*)(target->fields[0].ptr);
			Type* type_of_object = (Type*)target->fields[1].ptr;
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				std::vector<l::Constant*> values;
				values.reserve(size_of_object);
				for (uint64_t x = 0; x < size_of_object; ++x)
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

std::array<uint64_t, ASTn("never reached")> hitcount;
extern std::vector< uAST*> fuzztester_roots; //for use in garbage collecting. each valid AST that the fuzztester has a reference to must be kept in here
/**
The fuzztester generates random ASTs and attempts to compile them.
the output "Malformed AST" is fine. not all randomly-generated ASTs will be well-formed.

fuzztester has a vector of pointers to working, compilable ASTs (initially just a nullptr).
it chooses a random AST tag.
then, it chooses pointers randomly from the vector of working ASTs, which go in the first few fields.
each remaining field is chosen randomly according to an exponential distribution
finally, if the created AST successfully compiles, it is added to the vector of working ASTs.

todo: this scheme can't produce forward references, which are necessary for goto. that is, a goto points to an AST that's created after it.
and, it can't produce [concatenate [int]a [load a]]
*/
void fuzztester(unsigned iterations)
{
	std::vector<uAST*> AST_list{nullptr}; //start with nullptr as the default referenceable AST
	while (iterations--)
	{
		//create a random AST
		unsigned tag = mersenne() % ASTn("never reached");
		unsigned pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST pointers. they will come at the beginning
		unsigned prev_AST = generate_exponential_dist() % AST_list.size(); //perhaps: prove that exponential_dist is desired.
		//birthday collisions is the problem. a concatenate with two branches will almost never appear, because it'll result in an active object duplication.
		//but does exponential falloff solve this problem in the way we want?


		std::vector<uAST*> fields;
		uint64_t incrementor = 0;
		for (; incrementor < pointer_fields; ++incrementor)
			fields.push_back(AST_list.at(mersenne() % AST_list.size())); //get pointers to previous ASTs
		for (; incrementor < pointer_fields + AST_descriptor[tag].additional_special_fields; ++incrementor)
		{
			if (AST_descriptor[tag].parameter_types[incrementor].type == T::integer) //we're working with the AST_descriptor type directly. use T::types.
				fields.push_back((uAST*)generate_exponential_dist()); //get random integers and fill in the remaining fields
			else if (AST_descriptor[tag].parameter_types[incrementor].type == T::full_dynamic_pointer)
			{
				fields.push_back(nullptr);
				fields.push_back(nullptr); //make a zero dynamic object
			}
			else error("fuzztester doesn't know how to make this special type, so I'm going to panic");
		}
		uAST* test_AST = new_AST(tag, AST_list.at(prev_AST), fields);
		if (OLD_AST_OUTPUT) output_AST_and_previous(test_AST);
		output_AST_console_version a(test_AST);


		if (!FUZZTESTER_NO_COMPILE)
		{
			uint64_t result[3];
			compile_returning_legitimate_object(result, (uint64_t)test_AST);
			auto func = (function*)result[0];
			if (result[1] == 0)
			{
				run_null_parameter_function(result[0]);
				AST_list.push_back((uAST*)func->the_AST); //we need the cast to get rid of the const
				fuzztester_roots.push_back((uAST*)func->the_AST); //we absolutely shouldn't keep the type here, because it gets removed.
				//theoretically, this action is disallowed. these ASTs are pointing to already-immuted ASTs, which can't happen. however, it's safe as long as we isolate these ASTs from the user
				console << AST_list.size() - 1 << "\n";
				++hitcount[tag];
			}
		}
		else
		{
			//don't bother compiling. just shove everything in there.
			AST_list.push_back(test_AST);
			fuzztester_roots.push_back(test_AST);
			console << AST_list.size() - 1 << "\n";
			++hitcount[tag];
		}
		//else delete test_AST;
		if (INTERACTIVE)
		{
			console << "Press enter to continue\n";
			std::cin.get();
		}
		console << "\n";
		if ((generate_random() % 30) == 0)
			start_GC();
	}
	fuzztester_roots.clear();
}


bool READER_VERBOSE_DEBUG = false;

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
		if (READER_VERBOSE_DEBUG) console << "AST tag was " << AST_type << "\n";
		uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;

		int_or_ptr<uAST> fields[max_fields_in_AST] = { nullptr, nullptr, nullptr, nullptr };

		//field_num is which field we're on
		uint64_t field_num = 0;
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

		if (READER_VERBOSE_DEBUG)
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

		if (READER_VERBOSE_DEBUG)
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
	source_reader(std::istream& stream, char end) : input(stream) {} //char end can't be used, because the char needs to go in a switch case
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

	c = new compiler_host;
	thread_local std::unique_ptr<compiler_host> c_holder(c); //purpose is to make valgrind happy by deleting the compiler_host at the end of execution. however, later we'll need to move this into each thread.
	
	//console << "compiler host is at " << c << '\n';
	//console << "its JIT is at " << &(c->J) << '\n';

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
		else if (strcmp(argv[x], "continuous") == 0) CONTINUOUS = true;
		else if (strcmp(argv[x], "oldoutput") == 0) OLD_AST_OUTPUT = true;
		else if (strcmp(argv[x], "fuzznocompile") == 0) FUZZTESTER_NO_COMPILE = true;
		else if (strcmp(argv[x], "noaddmodule") == 0)  DONT_ADD_MODULE_TO_ORC = true;
		else if (strcmp(argv[x], "deletemodule") == 0)  DELETE_MODULE_IMMEDIATELY = true;
		else if (strcmp(argv[x], "quiet") == 0)
		{
			console.setstate(std::ios_base::failbit);
			console.rdbuf(nullptr);
			llvm_console = &llvm_null_stream;
		}
		else error(string("unrecognized flag ") + argv[x]);
	}

	test_unique_types();
	debugtypecheck(T::does_not_return);

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

	if (CONTINUOUS)
	{
		while (1)
		{
			std::clock_t start = std::clock();
			for (int x = 0; x < 400; ++x) fuzztester(100);
			std::cout << "400/100 time: " << (std::clock() - start) / (double)CLOCKS_PER_SEC << '\n';
			for (unsigned x = 0; x < ASTn("never reached"); ++x)
				std::cout << "tag " << x << " " << AST_descriptor[x].name << ' ' << hitcount[x] << '\n';
		}
		return 0;
	}

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
		for (unsigned x = 0; x < ASTn("never reached"); ++x)
			std::cout << "tag " << x << " " << AST_descriptor[x].name << ' ' << hitcount[x] << '\n';
		return 0;
	}
	if (CONSOLE)
	{
		while (1)
		{
			source_reader k(std::cin, '\n'); //have to reinitialize to clear the ASTmap
			console << "Input AST:\n";
			uAST* end = k.read();
			if (READER_VERBOSE_DEBUG) console << "Done reading.\n";
			if (end == nullptr)
			{
				console << "it's nullptr\n";
				std::cin.get();
				continue;
			}
			output_AST_and_previous(end);
			uint64_t result[3];
			compile_returning_legitimate_object(result, (uint64_t)end);
			if (result[1] == 0)
			{
				run_null_parameter_function(result[0]);
			}
			else std::cout << "wrong!\n";
		}
	}

	fuzztester(-1);
}