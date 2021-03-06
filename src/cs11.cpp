/*
main() handles the commandline arguments. if console input is requested, it will handle input too. however, it won't parse the input.
fuzztester() generates random, possibly malformed ASTs.
for console input, read_single_AST() parses a single AST and its field's ASTs, then creates a tree of ASTs out of it.
	then, create_single_basic_block() parses a list of ASTs, and chains them into a basic block.

Things which compile and run ASTs:
compiler_object holds state for a compilation. you make a new compiler_state for each compilation, and call compile_AST() on the last AST in your function. compile_AST automatically wraps the AST in a function and runs it; the last AST is assumed to hold the return object.
compile_AST() is mainly a wrapper. it generates the llvm::Functions and llvm::Modules that contain the main code. it calls generate_IR() to do the main work.
generate_IR() is the main AST conversion tool. it turns ASTs into llvm::Values, recursively. these llvm::Values are automatically inserted into a llvm::Module.
	to do this, generate_IR first runs itself on a target's dependencies. then, it looks at the target's tag, and then uses a switch-case to determine how it should use those dependencies.
*/
#include <cstdint>
#include <array>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "types.h"
#include "debugoutput.h"
#include "type_creator.h"
#include "runtime.h"
#include "cs11.h"
#include "orc.h"
#include "vector.h"
#include "helperfunctions.h"




bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool VERBOSE_GENERATE = false;
bool DONT_ADD_MODULE_TO_ORC = false;
bool DELETE_MODULE_IMMEDIATELY = false;

llvm::raw_ostream* llvm_console = &llvm::outs();
KaleidoscopeJIT* c;
llvm::LLVMContext* context;
llvm::IRBuilder<>* IRB;
uint64_t finiteness;
llvm::TargetMachine* TM;

#include <random>
std::random_device seeder; //this better be nondeterministic. on Linux, it uses /dev/urandom. on Windows, it sometimes isn't.
std::mt19937_64 mersenne(seeder() + ((uint64_t)seeder() << 32));

#include <llvm/Transforms/Utils/Cloning.h>
//return value is the error code, which is 0 if successful
uint64_t compiler_object::compile_AST(uAST* target)
{
	llvm::IRBuilder<> new_builder(*new_context);
	std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), *new_context)); //should be unique ptr because ownership will be transferred

	builder_context_stack b(&new_builder, new_context.get());

	if (VERBOSE_DEBUG)
	{
		print("starting compilation "); //in case it crashes here because target is not valid
		output_AST_console_version(target);
		print('\n');
	}
	if (target == nullptr) return IRgen_status::null_AST;
	using namespace llvm;
	FunctionType *dummy_type(FunctionType::get(llvm_void(), false));

	Function *dummy_func(Function::Create(dummy_type, Function::ExternalLinkage, "dummy_func", M.get())); //we have to insert the function in the Module so that it doesn't leak when generate_IR fails. things like instructions, basic blocks, and functions are not part of a context.

	BasicBlock *BB(BasicBlock::Create(*context, "entry", dummy_func));
	new_builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target);
	if (return_object.error_code) return return_object.error_code;

	return_type = return_object.type;
	//check(return_type == uniquefy_premade_type(return_type, false), "compilation returned a non-unique type");
	//can't be u::does_not_return, because goto can't go forward past the end of a function.
	//we can't do this checking anymore, because not all return types are in the type hash table after GC.

	auto size_of_return = get_size(return_object.type);
	FunctionType* FT(FunctionType::get(llvm_type_including_void(size_of_return), false));
	if (VERBOSE_GENERATE) print("Size of return is ", size_of_return, '\n');
	std::string function_name = GenerateUniqueName("");
	Function *F(Function::Create(FT, Function::ExternalLinkage, function_name, M.get())); //marking this private linkage seems to fail
	F->addFnAttr(Attribute::NoUnwind); //7% speedup. and required to get Orc not to leak memory, because it doesn't unregister EH frames

	F->getBasicBlockList().splice(F->begin(), dummy_func->getBasicBlockList());
	dummy_func->eraseFromParent();
	if (size_of_return)
	{
		if (size_of_return == 1) IRB->CreateRet(return_object.IR);
		else if (return_object.IR->getType()->isArrayTy()) IRB->CreateRet(return_object.IR);
		else //since we permit {i64, i64}, we have to canonicalize the type here.
		{
			check(size_of_return < ~0u, "load object not equipped to deal with large objects, because CreateInsertValue has a small index");
			llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(size_of_return));
			for (uint64_t a = 0; a < size_of_return; ++a)
			{
				llvm::Value* integer_transfer = IRB->CreateExtractValue(return_object.IR, {(unsigned)a});
				undef_value = IRB->CreateInsertValue(undef_value, integer_transfer, {(unsigned)a});
			};
			IRB->CreateRet(undef_value);
		}
	}
	else IRB->CreateRetVoid();
#ifndef NO_CONSOLE
	if (OUTPUT_MODULE)
		M->print(*llvm_console, nullptr);
	check(!llvm::verifyFunction(*F, &llvm::outs()), "verification failed");
#endif
	if (OPTIMIZE)
	{
		print("optimized code: \n");
		llvm::legacy::FunctionPassManager FPM(M.get());
		M->setDataLayout(c->DL);

		FPM.add(createCFLAliasAnalysisPass()); //Provide basic AliasAnalysis support for GVN.
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
		if (VERBOSE_DEBUG) print("adding module...\n");
		auto H = J.addModule(std::move(M));

		// Get the address of the JIT'd function in memory.
		auto ExprSymbol = J.findUnmangledSymbol(function_name);

		fptr = (void*)(intptr_t)(ExprSymbol.getAddress());

		if (DELETE_MODULE_IMMEDIATELY) J.removeModule(H);
		else result_module = H;
	}
	else
	{
		fptr = (void*)2222222ull;
	}
	return 0;
}


void compiler_object::emit_dtors(uint64_t desired_stack_size)
{
	//we can make a basic block with the instructions, then copy it over when needed.
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{
	while (object_stack.size() > desired_stack_size) //>, because desired_stack_size might be ~0ull
	{
		auto to_be_removed = object_stack.top();
		object_stack.pop();
		uint64_t number_of_erased = objects.erase(to_be_removed);
		check(number_of_erased == 1, "erased too many or too few elements"); //we shouldn't put the erase operation in the check, because check() might be no-op'd, and erase has side effects
		(void)number_of_erased; //here to avoid unused variable warning, in case the check disappears
	}
}

/** generate_IR() transforms ASTs into LLVM IR.
'
memory_location is for RVO. if stack_degree says that the return object should be stored somewhere, the location will be "memory_location desired". storage is done by the returning function.
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
generate_IR is run on fields of the AST. it knows how many fields are needed by using fields_to_compile from AST_descriptor[] 
then, the main IR generation is done using a switch-case on a tag.
the return value is created using the finish() macros. these automatically pull in any miscellaneous necessary information, such as stack lifetimes, and bundles them into a Return_Info. furthermore, if the return value needs to be stored in a stack location, finish() does this automatically using the move_to_stack lambda.

stack_degree is only allowed to be 2 if you're in a basic block. this is because finish() requires stack_degree to know whether to make it a hidden allocation, which Return_Info() uses to determine whether to emit a load from the object.
however, there's one exception: a BB can take in stack_degree == 0, and call out stack degree = 2. this is the only possible circumventer though.
invariant: pass in stack_degree == 2 <===> can't use the returned IR

note that while we have a rich type system, the only types that llvm sees are int64s and arrays of int64s. pointers, locks, etc. have their information stripped, so llvm only sees the size of each object. we forcefully cast things to integers.
	for an object of size 1, its llvm-type is an integer
	for an object of size N>=2, the llvm-type is an alloca'd array of N integers, which is [12 x i64]*. note the * - it's a pointer to an array, not the array itself.
for objects that end up on the stack, their return value is either i64* or [S x i64]*.
the actual allocas are always [S x i64]*, even when S = 1. but, getting active objects returns i64*.
currently, the finish() macro takes in the array itself, not the pointer-to-array.
*/
Return_Info compiler_object::generate_IR(uAST* target, uint64_t stack_degree)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
	//note: early escapes must still leave the IR builder in a valid state, so that create_if_Value can do its rubbish business.
#define return_code(X, Y) do { if (VERBOSE_GENERATE) print("error at ", target, " field ", Y, " code ", X, '\n'); error_location = target; error_field = Y; return Return_Info(IRgen_status::X, (llvm::Value*)nullptr, u::null); } while (0)

	if (VERBOSE_GENERATE)
	{
		print("generate_IR single AST start ^^^^^^^^^\n");
		if (target) output_AST(target);
		print("stack degree ", stack_degree);
		print('\n');
	}

	check(stack_degree != 1, "no more stack degree 1");

	//generate_IR is allowed to take nullptr. otherwise, we need an extra check beforehand. this extra check creates code duplication, which leads to typos when indices aren't changed.
	if (target == nullptr) return Return_Info();

	//we can't use a dtor to clear the stack, because that will run after the new object is created, killing it.
	uint64_t final_stack_position = ~0ull;
	
	memory_allocation* default_allocation = nullptr;
	//this will point to the back of the deque. because the values created on the stack last a lot longer than the functions creating those values. and if you're passing through a reference, such as using goto's fail branch, it'll die too fast. so we need to put it in the deque, so it'll last longer.
	//it needs to be wrapped in a memory_allocation because it might experience a turn_full(), which will invalidate all pointers except for one. so we need to keep the pointer to the location in one place, which is the memory_location.
	llvm::Value* default_hidden_subtype = nullptr;

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.

	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	//if it's necessary to create and write to storage_location, we do so.
	//if stack_degree >= 1, this will ignore return_value and use "memory_location desired" instead
	//note the hidden default values that are captured; they're listed above in the ///default values section.
	auto finish_internal = [&](llvm::Value* return_value, Tptr type) -> Return_Info
	{
		//check(type == uniquefy_premade_type(type, false), "returned non unique type in finish()");
		//no longer possible, not all types are uniqued.

		if (VERBOSE_GENERATE && return_value != nullptr)
		{
			print("finish() in generate_IR, Value is ");
			return_value->print(*llvm_console);
			print("\nand the llvm type is ");
			return_value->getType()->print(*llvm_console);
			print("\nand the internal type is ");
			pftype(type);
			print('\n');
		}
		else if (VERBOSE_GENERATE) print("finish() in generate_IR, null value\n");
		if (VERBOSE_GENERATE)
		{
			IRB->GetInsertBlock()->getParent()->print(*llvm_console, nullptr);
			print("generate_IR single AST ", target, " ", AST_descriptor[target->tag].name, " vvvvvvvvv\n");
		}

		bool existing_hidden_location = (default_allocation != 0);
		if (stack_degree == 2) clear_stack(final_stack_position);
		uint64_t size_of_return = get_size(type);
		if (size_of_return >= 1 && stack_degree == 2)
		{
			///default values. they become hidden parameters to finish().

			if (!existing_hidden_location)
			{
				allocations.push_back(memory_allocation(size_of_return));
				default_allocation = &allocations.back();
				write_into_place(return_value, default_allocation->allocation);
			}

			//whether or not you made a new location, the Value is nullptr, which signifies that it's a reference.
			new_living_object(target, Return_Info(IRgen_status::no_error, default_allocation, type, existing_hidden_location, default_hidden_subtype));
			return Return_Info(IRgen_status::no_error, default_allocation, type, existing_hidden_location, default_hidden_subtype);
		}
		//we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for justification.
		else if (size_of_return >= 1)
		{
			check(stack_degree == 0, "stack degree 1 not allowed");
			if (default_allocation)
			{
				new_living_object(target, Return_Info(IRgen_status::no_error, default_allocation, type, existing_hidden_location, default_hidden_subtype));
				return Return_Info(IRgen_status::no_error, default_allocation, type, existing_hidden_location, default_hidden_subtype);
			}
			else
			{
				new_living_object(target, Return_Info(IRgen_status::no_error, return_value, type, existing_hidden_location, default_hidden_subtype));
				return Return_Info(IRgen_status::no_error, return_value, type, existing_hidden_location, default_hidden_subtype);
			}
		}
		else return Return_Info();
	};

	//call the finish macros when you've constructed something.
	//_stack_handled if your AST has already written in the object into its memory location, for stack_degree == 1. for example, any function that passes through "desired" is a pretty good candidate.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.
	//remember: pass the value itself if stack_degree == 0, and pass a pointer to the value if stack_degree == 1 or 2.

	//these are for when we need to specify the return type. make sure not to duplicate X in the expression.
	//maybe later, we'll separate everything out. so if you specify the type and the return isn't special_pointer, it'll error as well.
#define finish_special(X, type) do {return finish_internal(X, type); } while (0)

#define finish(X) do {check(AST_descriptor[target->tag].return_object.state != special_return, "need to specify type"); finish_special(X, uniquefy_premade_type(AST_descriptor[target->tag].return_object.type, true)); } while (0)
//I think this passthrough forces a hidden reference. but for now, we need it to convert pointers on the stack to temp pointers on the stack.
#define finish_passthrough(X) do {default_hidden_subtype = X.hidden_subtype; default_allocation = X.place; return finish_internal(X.IR, X.type);} while(0)

	//if this AST is already in the object list, return the previously-gotten value. this comes before the loop catcher.
	auto looking_for_reference = objects.find(target);
	if (looking_for_reference != objects.end())
	{
		if (VERBOSE_GENERATE)
		{
			print("I'm copying\n");
			output_AST(target);
		}
		//we're not going to copy it/make a new location for it, even though this would let us have two versions. because of name collisions: there would be absolutely no way to refer to the newly created object.
		//if you want a new object, use a dummy concatenate()
		auto refreshed_load = looking_for_reference->second;
		if (refreshed_load.place) refreshed_load.IR = load_from_memory(refreshed_load.place->allocation, get_size(refreshed_load.type)); //refresh the IR
		return refreshed_load;
	}


	//if we've seen this AST before, and we need to reprocess all its fields, then we're stuck in an infinite loop. return an error.
	if (loop_catcher.insert(target).second == false) return_code(infinite_loop, 10); //for now, 10 is a special value, and means not any of the fields
	//after we're done with this AST, we remove it from loop_catcher.
	struct loop_catcher_destructor_cleanup
	{
		compiler_object* object; uAST* targ; //values can only be used if they are passed in.
		loop_catcher_destructor_cleanup(compiler_object* x, uAST* t) : object(x), targ(t) {}
		~loop_catcher_destructor_cleanup() { object->loop_catcher.erase(targ); }
	} temp_object(this, target);



	//find the lifetime of this element
	//this actually starts at 2, but that's fine.
	final_stack_position = object_stack.size();

	for (uint64_t x = 0; x < AST_descriptor[target->tag].fields_to_compile; ++x)
	{
		Return_Info result; //default constructed for null object
		if (target->fields[x])
		{
			result = generate_IR(target->fields[x]);
			if (result.error_code) return result;
		}

		if (VERBOSE_GENERATE)
		{
			print("generate_IR() result type is ");
			pftype(result.type);
			print("desired type is ");
			pftype(AST_descriptor[target->tag].parameter_types[x].type);
		}

		if (uniquefy_premade_type(AST_descriptor[target->tag].parameter_types[x].type, true) == u::does_not_return)
			finish_special(nullptr, u::does_not_return); //just get out of here, since we're never going to run the current command anyway.

		//check that the type matches.
		if (AST_descriptor[target->tag].parameter_types[x].state != compile_without_type_check) //for fields that are compiled but not type checked
		{
			//this is fine with labels even though labels require emission whether reached or not, because labels don't compile using the default mechanism
			//even if there are labels skipped over by this escape, nobody can see them because of our try-catch goto scheme.
			if (type_check(RVO, result.type, uniquefy_premade_type(AST_descriptor[target->tag].parameter_types[x].type, true)) != type_check_result::perfect_fit) return_code(type_mismatch, x);
		}

		field_results.push_back(result);
	}
	/*
	//Note: this is a general purpose writer. it's good for things!
	llvm::Value* printer = llvm_function(print_uint64_t, llvm_void(), llvm_i64());
	IRB->CreateCall(printer, desired_type_to_store);
	*/

	//we generate code for the AST, depending on its tag.
	switch (target->tag)
	{
	case ASTn("basicblock"):
		{
			//compile basic block elements.
			svector* k = (svector*)target->fields[0];
			Return_Info final; //default value
			for (uint64_t& AST : Vector_range(k))
			{
				final = generate_IR((uAST*)AST, 2);
				if (final.error_code) return final;
			}
			if (stack_degree == 0 && final.place != nullptr) final.IR = load_from_memory(final.place->allocation, get_size(final.type));
			return final;
		}
	case ASTn("zero"): finish(llvm_integer(0));
	case ASTn("increment"): finish(IRB->CreateAdd(field_results[0].IR, llvm_integer(1), s("increment")));
	case ASTn("decrement"): finish(IRB->CreateSub(field_results[0].IR, llvm_integer(1)));
	case ASTn("add"): finish(IRB->CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"): finish(IRB->CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("multiply"): finish(IRB->CreateMul(field_results[0].IR, field_results[1].IR, s("multiply")));
	case ASTn("random"): finish(IRB->CreateCall(llvm_int_only_func(generate_random), {}, s("random")));
	case ASTn("lessu"): finish(IRB->CreateZExt(IRB->CreateICmpULT(field_results[0].IR, field_results[1].IR), llvm_i64()));
	case ASTn("lesss"): finish(IRB->CreateZExt(IRB->CreateICmpSLT(field_results[0].IR, field_results[1].IR), llvm_i64()));
	case ASTn("udiv"): //sdiv is more complicated, because we'd have to test for -1 division as well.
		finish(create_if_value(
			IRB->CreateICmpNE(field_results[1].IR, llvm_integer(0), s("div0 check")),
			[&](){ return IRB->CreateUDiv(field_results[0].IR, field_results[1].IR); },
			[](){ return llvm_integer(0); }
		));
	case ASTn("urem"): //default value is the value itself instead of zero. this is to preserve (x / k) * k + x % k = x, and to preserve mod agreeing with equivalence classes on Z.
		//when modulo 0, such as with the number of pointer fields, sometimes you want 0 instead. well, in that case you want 0 anyway.
		finish(create_if_value(
			IRB->CreateICmpNE(field_results[1].IR, llvm_integer(0), s("div0 check")),
			[&](){ return IRB->CreateURem(field_results[0].IR, field_results[1].IR); },
			[&](){ return field_results[0].IR; }
		));
	case ASTn("ushr"): //unsigned shift right
		finish(create_if_value(
			IRB->CreateICmpULT(field_results[1].IR, llvm_integer(64), s("lt64 shift")),
			[&](){ return IRB->CreateLShr(field_results[0].IR, field_results[1].IR); },
			[&](){ return llvm_integer(0); }
		));
	case ASTn("sshr"): //unsigned shift right
		finish(create_if_value(
			IRB->CreateICmpULT(field_results[1].IR, llvm_integer(64), s("lt64 shift")),
			[&](){ return IRB->CreateAShr(field_results[0].IR, field_results[1].IR); },
			[&](){ return llvm_integer(0); }
		));
	case ASTn("shl"): //shift left
		finish(create_if_value(
			IRB->CreateICmpULT(field_results[1].IR, llvm_integer(64), s("lt64 shift")),
			[&](){ return IRB->CreateShl(field_results[0].IR, field_results[1].IR); },
			[&](){ return llvm_integer(0); }
		));
	case ASTn("if"): //it's vitally important that this can check pointers, so that we can tell if they're nullptr.
		{
			//condition object is automatically pushed onto the stack.

			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			uint64_t if_stack_position = object_stack.size();

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			llvm::Value* comparison = IRB->CreateICmpNE(field_results[0].IR, llvm_integer(0), s("if() condition"));
			llvm::Function *TheFunction = IRB->GetInsertBlock()->getParent();

			// it's necessary to insert blocks into the function so that we can avoid memleak on early return
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*context, s("merge"), TheFunction);

			llvm::BasicBlock* caseBB[2];
			Return_Info case_IR[2];
			for (uint64_t x : {0, 1}) caseBB[x] = llvm::BasicBlock::Create(*context, s("ifcase"), TheFunction);
			IRB->CreateCondBr(comparison, caseBB[0], caseBB[1]);
			
			for (uint64_t x : {0, 1})
			{
				IRB->SetInsertPoint(caseBB[x]);
				case_IR[x] = generate_IR(target->fields[x + 1]);
				if (case_IR[x].error_code) return case_IR[x];
				clear_stack(if_stack_position);
				IRB->CreateBr(MergeBB);
				caseBB[x] = IRB->GetInsertBlock();
			}

			Tptr result_type = case_IR[0].type; //first, we assume then_IR is the main type.
			//RVO, because that's what defines the slot.
			if (type_check(RVO, case_IR[1].type, case_IR[0].type) != type_check_result::perfect_fit)
			{
				result_type = case_IR[1].type; //if it didn't work, try seeing if else_IR can be the main type.
				if (type_check(RVO, case_IR[0].type, case_IR[1].type) != type_check_result::perfect_fit)
				{
					result_type = u::null;
				}
			}

			IRB->SetInsertPoint(MergeBB);
			if (result_type) //we have to test it, because type_check failure can give a null result type but where the fields don't match.
			{
				llvm::Value* endPN = llvm_create_phi({case_IR[0].IR, case_IR[1].IR}, {caseBB[0], caseBB[1]});
				finish_special(endPN, result_type);
			}
			else finish_special(nullptr, u::null);



			/* 
			// problem is that even if we return an error in one branch, other branch is going to proceed creating the object.
			// fundamentally, create_if_value doesn't have a good way to handle if its lambdas return errors.

			error_in_check check_before_emit_phi = [&]() -> bool
			{
				result_type = case_IR[0].type; //first, we assume then_IR is the main type.
				//RVO, because that's what defines the slot.
				if (type_check(RVO, case_IR[1].type, case_IR[0].type) != type_check_result::perfect_fit)
				{
					result_type = case_IR[1].type; //if it didn't work, try seeing if else_IR can be the main type.
					if (type_check(RVO, case_IR[0].type, case_IR[1].type) != type_check_result::perfect_fit)
						return_code(type_mismatch, 2);
				}
			};

			//problem with this is that create_if_value() doesn't handle type checking, and we must do type checking before we actually create the phi value. to do this, we'd have to pass in a lambda returning bool, if something has gone wrong

			llvm::Value* comparison
			llvm::Value* comparison = IRB->CreateICmpNE(field_results[0].IR, llvm_integer(0), s("if() condition"));

			Return_Info case_IR[2];

			IRemitter run_field_1 = [&]() -> llvm::Value*
			{
			case_IR[0] = generate_IR(target->fields[1]); //note: the case field is shifted by 1, because there's a condition field.
			if (case_IR[0].error_code) return 0;
			else return case_IR[0].IR;
			};

			IRemitter run_field_2 = [&]() -> llvm::Value*
			{
			case_IR[1] = generate_IR(target->fields[2]);
			if (case_IR[1].error_code) return 0;
			else return case_IR[1].IR;
			};

			llvm::Value* error_value = create_if_value(
			comparison,
			run_field_1,
			run_field_2,
			);
			if (error_value == 0) return_code(error_transfer_from_if, 0);



			Tptr result_type = case_IR[0].type; //first, we assume then_IR is the main type.
			//RVO, because that's what defines the slot.
			if (type_check(RVO, case_IR[1].type, case_IR[0].type) != type_check_result::perfect_fit)
			{
			result_type = case_IR[1].type; //if it didn't work, try seeing if else_IR can be the main type.
			if (type_check(RVO, case_IR[0].type, case_IR[1].type) != type_check_result::perfect_fit)
			return_code(type_mismatch, 2);
			}

			IRB->SetInsertPoint(MergeBB);
			llvm::Value* endPN = llvm_create_phi({case_IR[0].IR, case_IR[1].IR}, {case_IR[0].type, case_IR[1].type}, {caseBB[0], caseBB[1]});
			finish_special(endPN, result_type);
			*/
		}
	case ASTn("nvec"):

		if (auto k = llvm::dyn_cast<llvm::ConstantInt>(field_results[0].IR)) //we need the second field to be a constant.
		{
			Tptr type = k->getZExtValue();
			print("nvec type is ", type, '\n');
			if (type == 0) return_code(type_mismatch, 0);
			if (type.ver() == 0) return_code(vector_cant_take_large_objects, 0);

			if (!is_full(type)) return_code(nonfull_object, 0);
			else if (type.ver() > Typen("pointer")) return_code(type_mismatch, 0);
			else if (get_size(type) != 1) return_code(type_mismatch, 0);
			finish_special(IRB->CreateCall(llvm_int_only_func(new_vector), {}), new_unique_type(Typen("vector"), type));
		}
		return_code(requires_constant, 1);

	case ASTn("label"):
		{
			llvm::Function *TheFunction = IRB->GetInsertBlock()->getParent();

			// Create blocks for the then and else cases. Insert the block into the function, or else it'll leak when we return_code
			llvm::BasicBlock *label = llvm::BasicBlock::Create(*context, s("label"), TheFunction);
			auto label_insertion = labels.insert(std::make_pair(target, label_info(label, final_stack_position, true)));
			if (label_insertion.second == false)
				return_code(label_duplication, 0);

			Return_Info scoped = generate_IR(target->fields[0]);
			if (scoped.error_code) return scoped;

			//this expires the label, so that goto knows that the label is behind. with finiteness, this means the label isn't guaranteed.
			label_insertion.first->second.is_forward = false;

			IRB->CreateBr(label);
			IRB->SetInsertPoint(label);
			finish(nullptr);
		}

	case ASTn("goto"):
		{
			auto labelsearch = labels.find(target->fields[0]);
			if (labelsearch == labels.end()) return_code(missing_label, 0);
			auto info = labelsearch->second;

			//unwind destructors such as locks.
			if (info.is_forward)
			{
				emit_dtors(info.stack_size);
				IRB->CreateBr(labelsearch->second.block);
				//IRB->ClearInsertionPoint() is bugged. try [label [goto a]]a. note that the label has two predecessors, one which is a null operand
				//so when you emit the branch for the label, even though the insertion point is clear, it still adds a predecessor to the block.
				//instead, we have to emit a junk block

				llvm::BasicBlock *JunkBB = llvm::BasicBlock::Create(*context, s("never reached"), IRB->GetInsertBlock()->getParent());
				IRB->SetInsertPoint(JunkBB);

				finish_special(nullptr, u::does_not_return);
			}

			//check finiteness. it's not an AllocaInst apparently (llvm asserts), because it wasn't allocated that way.
			llvm::Value* finiteness_pointer = IRB->CreateIntToPtr(llvm_integer((uint64_t)&finiteness), llvm_i64()->getPointerTo());
			llvm::Value* current_finiteness = IRB->CreateLoad(finiteness_pointer);
			llvm::Value* comparison = IRB->CreateICmpNE(current_finiteness, llvm_integer(0), s("finiteness comparison"));


			llvm::Function *TheFunction = IRB->GetInsertBlock()->getParent();
			llvm::BasicBlock *SuccessBB = llvm::BasicBlock::Create(*context, s("finiteness success"), TheFunction);
			llvm::BasicBlock *FailureBB = llvm::BasicBlock::Create(*context, s("finiteness failure"), TheFunction);
			IRB->CreateCondBr(comparison, SuccessBB, FailureBB);

			IRB->SetInsertPoint(SuccessBB);

			//decrease finiteness. this comes before emission of the success branch.
			llvm::Value* finiteness_minus_one = IRB->CreateSub(current_finiteness, llvm_integer(1));
			IRB->CreateStore(finiteness_minus_one, finiteness_pointer);

			emit_dtors(info.stack_size);
			IRB->CreateBr(labelsearch->second.block);
			IRB->SetInsertPoint(FailureBB);

			finish(0); //whether it's a failure or not.
		}
	case ASTn("pointer"):
		{
			auto found_AST = objects.find(target->fields[0]);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			if (found_AST->second.place == nullptr) return_code(pointer_to_temporary, 0);
			Tptr new_pointer_type(0);
			if (found_AST->second.hidden_reference == true || !is_full(found_AST->second.type))
			{
				new_pointer_type = new_unique_type(Typen("temp pointer"), found_AST->second.type);
			}
			else
			{
				new_pointer_type = new_unique_type(Typen("pointer"), found_AST->second.type);
				objects.find(target->fields[0])->second.place->turn_full();
			}
			llvm::Value* final_result = IRB->CreatePtrToInt(found_AST->second.place->allocation, llvm_i64(), s("flattening pointer"));
			finish_special(final_result, new_pointer_type);
		}
	case ASTn("tmp_pointer"):
		{
			if (field_results[0].type.ver() != Typen("pointer")) return_code(type_mismatch, 0);
			finish_special(field_results[0].IR, new_unique_type(Typen("temp pointer"), field_results[0].type.field(0)));
		}
	case ASTn("concatenate"):
		{
			uint64_t half_size[2];
			uint64_t total_size = 0;
			for (int x : {0, 1})
			{
				half_size[x] = get_size(field_results[x].type);
				total_size += half_size[x];
			}
			if (total_size == 0) finish_special(nullptr, u::null);

			//we have to handle stupid things like i64 vs [4 x i64] and that nonsense. we can't just extract values easily.
			llvm::Value* final_value = (total_size == 1) ? llvm::UndefValue::get(llvm_i64()) : llvm::UndefValue::get(llvm_array(total_size));
			write_into_place({{field_results[0].IR, field_results[1].IR}}, final_value, true);
			Tptr final_type = concatenate_types({field_results[0].type, field_results[1].type});
			finish_special(final_value, final_type);
		}

	case ASTn("store"):
		{
			if (field_results[0].type == 0) return_code(type_mismatch, 0);
			if (type_check(RVO, field_results[1].type, field_results[0].type) == type_check_result::perfect_fit) //store into reference
			{
				if (field_results[0].place == nullptr) return_code(missing_reference, 0);
				write_into_place(field_results[1].IR, field_results[0].place->allocation);
				finish(0);
			}
			//store into pointer
			if (field_results[0].type.ver() == Typen("temp pointer") || field_results[0].type.ver() == Typen("pointer"))
			{
				if (type_check(RVO, field_results[1].type, field_results[0].type.field(0)) == type_check_result::perfect_fit)
				{
					llvm::Value* pointer_cast = IRB->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());
					write_into_place(field_results[1].IR, pointer_cast);
					finish(0);
				}
			}
			return_code(type_mismatch, 1);

		}

	case ASTn("try_store"): //for now, this attempts to store a dynamic object into a static one, thus exposing the dynamic object's internals. it doesn't store statics into dynamics, for now. eventually, we'll probably want to fold this into the normal store.
		{
			if (field_results[0].type == 0) return_code(type_mismatch, 0);
			if (field_results[0].place == nullptr) return_code(missing_reference, 0);

			//check for the hidden subtype
			for (uint64_t x : {0, 1})
			{
				if (field_results[x].type.ver() == Typen("pointer to something") ||
					field_results[x].type.ver() == Typen("vector of something"))
					if (field_results[x].hidden_subtype == nullptr) return_code(lost_hidden_subtype, x);
			}

			//future: make this take temp pointers?
			//check that we're writing pointers to pointers, vectors to vectors
			switch (field_results[0].type.ver())
			{
			case Typen("pointer"):
				if (field_results[1].type != Typen("pointer to something")) return_code(type_mismatch, 1);
				break;
			case Typen("vector"):
				if (field_results[1].type != Typen("vector of something")) return_code(type_mismatch, 1);
				break;
			case Typen("vector of something"):
				if (field_results[1].type != Typen("vector of something") && field_results[1].type.ver() != Typen("vector")) return_code(type_mismatch, 1);
				break;
			case Typen("pointer to something"):
				if (field_results[1].type != Typen("pointer to something") && field_results[1].type.ver() != Typen("pointer")) return_code(type_mismatch, 1);
				break;
			default:
				return_code(type_mismatch, 0);
			}

			//extract types from either statics or dynamics
			llvm::Value* type[2];
			for (int x : {0, 1})
			{
				switch (field_results[x].type.ver())
				{
				case Typen("pointer"):
				case Typen("vector"):
					type[x] = llvm_integer(field_results[x].type.field(0));
					break;
				case Typen("vector of something"):
				case Typen("pointer to something"):
					type[x] = field_results[x].hidden_subtype;
					break;
				default:
					return_code(type_mismatch, 0);
				}
			}

			finish(create_if_value(
				IRB->CreateICmpEQ(type[0], type[1], s("runtime type check")),
				[&](){ IRB->CreateStore(field_results[1].IR, field_results[0].place->allocation); return llvm_integer(1); },
				[&](){ return llvm_integer(0); }
			));
		}
	case ASTn("dyn_subobj"):
		{
			llvm::Value* single_type; //the final single type.
			llvm::Value* switch_type; //the version, which we will switch on.
			llvm::Value* correct_pointer; //the pointer to the object we'll be loading.

			llvm::Value* offset = field_results[1].IR;
			if (type_check(RVO, field_results[0].type, u::dynamic_object) == type_check_result::perfect_fit)
			{
				llvm::Value* overall_dynamic_object = IRB->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());

				auto comparison = IRB->CreateICmpNE(field_results[0].IR, llvm_integer(0), s("special case 0 dynamic object"));
				//this block handles the case where the base of the dynamic object is 0.
				llvm::Value* overall_type = create_if_value(
					comparison,
					[&](){ return IRB->CreateLoad(overall_dynamic_object, s("type of overall dynamic")); },
					[](){ return llvm_integer(0); }
				);

				llvm::Value* offset_from_pointer = IRB->CreateAdd(offset, llvm_integer(1)); //skip over the type.
				correct_pointer = IRB->CreateGEP(overall_dynamic_object, offset_from_pointer); //might not be inbounds.

				auto dynamic_func = llvm_function(dynamic_subtype, double_int(), llvm_i64(), llvm_i64());
				auto type_data = IRB->CreateCall(dynamic_func, {overall_type, offset}, s("dynamic sub"));
				single_type = IRB->CreateExtractValue(type_data, {0}, s("type of subdynamic"));
				switch_type = IRB->CreateExtractValue(type_data, {1}, s("switch type"));
			}
			else
			{
				if (field_results[0].hidden_subtype == nullptr) return_code(lost_hidden_subtype, 0);

				llvm::Value* overall_object = IRB->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());

				if (field_results[0].type == u::pointer_to_something)
				{
					auto dynamic_func = llvm_function(dynamic_subtype, double_int(), llvm_i64(), llvm_i64());
					auto type_data = IRB->CreateCall(dynamic_func, {field_results[0].hidden_subtype, offset}, s("dynamic sub"));
					single_type = IRB->CreateExtractValue(type_data, {0}, s("type of subdynamic"));
					switch_type = IRB->CreateExtractValue(type_data, {1}, s("switch type"));

					correct_pointer = IRB->CreateGEP(overall_object, offset); //might not be inbounds.

				}
				else if (field_results[0].type == u::vector_of_something)
				{
					single_type = field_results[0].hidden_subtype;
					switch_type = IRB->CreateCall(llvm_int_only_func(type_tag), field_results[0].hidden_subtype);

					llvm::Value* offset_from_pointer = IRB->CreateAdd(offset, llvm_integer(vector_header_size)); //skip over the vector.
					correct_pointer = IRB->CreateGEP(overall_object, offset_from_pointer);
				}
				else return_code(type_mismatch, 0);
			}

			uint64_t starting_stack_position = object_stack.size();
			llvm::Function *TheFunction = IRB->GetInsertBlock()->getParent();

			//0 case is the default. llvm switch statements require a default.
			llvm::BasicBlock *failed_to_get = llvm::BasicBlock::Create(*context, "", TheFunction);

			auto switch_on_type = IRB->CreateSwitch(switch_type, failed_to_get, Typen("does not return") - 1, 0); //dependency on type
			llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*context, s("merge"), TheFunction);
			for (uint64_t x = 0; x <= Typen("pointer"); ++x) //dependency on type
			{
				if (x != 0)
				{
					llvm::BasicBlock *caseBB = llvm::BasicBlock::Create(*context, "", TheFunction);
					IRB->SetInsertPoint(caseBB);
					switch_on_type->addCase(llvm_integer(x), caseBB);
				}
				else IRB->SetInsertPoint(failed_to_get);
				if (x != 0) //if it's 0, don't put a reference on the stack, because the type is empty.
				{
					Tptr loaded_object_type = 0;
					if (x == Typen("pointer")) //dependency on type. here, we create another dynamic object, instead of returning an actual regular pointer.
					{
						loaded_object_type = Typen("pointer to something");
						new_living_object(target, Return_Info(IRgen_status::no_error, new_reference(correct_pointer), loaded_object_type, true, single_type));
					}
					else if (x == Typen("vector")) //dependency on type. here, we create another dynamic object, instead of returning an actual regular pointer.
					{
						loaded_object_type = Typen("vector of something");
						new_living_object(target, Return_Info(IRgen_status::no_error, new_reference(correct_pointer), loaded_object_type, true, single_type));
					}
					else
					{
						loaded_object_type = (Tptr)x;
						new_living_object(target, Return_Info(IRgen_status::no_error, new_reference(correct_pointer), loaded_object_type, true));
					}
				}
				Return_Info case_IR = generate_IR(target->fields[x + 2]);
				if (case_IR.error_code) return case_IR;

				clear_stack(starting_stack_position);
				IRB->CreateBr(MergeBB);
			}

			IRB->SetInsertPoint(MergeBB);
			finish(single_type);
		}

	case ASTn("load_subobj_ref"):
		{
			Tptr type_of_object(0);
			llvm::Value* reference_p; //the reference that we get

			switch (field_results[0].type.ver())
			{
			case Typen("vector"):
				{
					type_of_object = field_results[0].type.field(0);
					reference_p = IRB->CreateCall(llvm_function(reference_at, llvm_i64()->getPointerTo(), llvm_i64(), llvm_i64()), {field_results[0].IR, field_results[1].IR});
					break;
				}
			case Typen("AST pointer"):
				{
					type_of_object = u::AST_pointer;
					reference_p = IRB->CreateCall(llvm_function(AST_subfield, llvm_i64()->getPointerTo(), llvm_i64(), llvm_i64()), {field_results[0].IR, field_results[1].IR});
					break;
				}
			default:
				return_code(type_mismatch, 0);
			}

			Return_Info case_IR;
			IRemitter run_field_2 = [&]() -> llvm::Value*
			{
				case_IR = generate_IR(target->fields[2]); //error checking is moved to after the comparison.
				return nullptr; //signifying an unused phi value.
			};

			//we can use create_if while simultaneously making a new referenceable object, because there's no "else" branch.
			uint64_t starting_stack_position = object_stack.size();
			new_living_object(target, Return_Info(IRgen_status::no_error, new_reference(reference_p), type_of_object, true));

			auto reference_integer = IRB->CreatePtrToInt(reference_p, llvm_i64());
			llvm::Value* comparison = IRB->CreateICmpNE(reference_integer, llvm_integer(0), s("vector reference zero check"));
			create_if_value(comparison, run_field_2, [](){ return nullptr; });
			if (case_IR.error_code) return case_IR;
			clear_stack(starting_stack_position);

			finish(0);
		}
	case ASTn("vecpb"):
		{
			//requires place, because pushing an element may change the location of the vector
			if (field_results[0].place == nullptr) return_code(missing_reference, 0);
			if (field_results[0].type.ver() != Typen("vector")) return_code(type_mismatch, 0);
			if (type_check(RVO, field_results[1].type, field_results[0].type.field(0)) != type_check_result::perfect_fit) return_code(type_mismatch, 1);
			llvm::Value* pusher = llvm_function(pushback, llvm_void(), llvm_i64()->getPointerTo(), llvm_i64());
			IRB->CreateCall(pusher, {field_results[0].place->allocation, field_results[1].IR});
			finish(0);
		}
	case ASTn("vecsz"): //in the future, this should handle dynamic pointers too, and concatenates
		{
			if (field_results[0].type.ver() == Typen("vector") || field_results[0].type.ver() == Typen("vector of something"))
			{
				finish(IRB->CreateCall(llvm_int_only_func(vector_size), field_results[0].IR));
			}
			/*else if (field_results[0].type.ver() == Typen("AST pointer"))
			{
			}*/
			return_code(type_mismatch, 0);
		}
	case ASTn("dynamify"):
		{
			if (field_results[0].type == 0)
				finish(llvm_integer(0));
			if (!is_full(field_results[0].type))
				return_code(nonfull_object, 0);

			llvm::Value* dynamic_allocator = llvm_function(new_dynamic_obj, llvm_i64()->getPointerTo(), llvm_i64());
			llvm::Value* dynamic_object = IRB->CreateCall(dynamic_allocator, llvm_integer(field_results[0].type), s("dyn_allocate_memory"));

			//store the returned type and value into the acquired address
			llvm::Value* dynamic_actual_object_address = IRB->CreateGEP(dynamic_object, llvm_integer(1));
			write_into_place(field_results[0].IR, dynamic_actual_object_address);

			llvm::Value* dynamic_address = IRB->CreatePtrToInt(dynamic_object, llvm_i64());

			finish(dynamic_address);
		}
	case ASTn("compile"):
		{
			if (type_check(RVO, field_results[0].type, T::null) == type_check_result::perfect_fit) finish(llvm_integer(0));
			else if (type_check(RVO, field_results[0].type, T::AST_pointer) == type_check_result::perfect_fit)
				finish(IRB->CreateCall(llvm_int_only_func(compile_returning_just_function), field_results[0].IR, s("compile")));
			else return_code(type_mismatch, 0);
		}

	case ASTn("overfunc"):
		{
			finish(IRB->CreateCall(llvm_int_only_func(overwrite_func), {field_results[0].IR, field_results[1].IR}));
		}
	case ASTn("convert_to_AST"):
		{
			//this helps the bootstrapping issue. can't get an AST without a vector of ASTs; can't get a vector of ASTs without an AST.
			if (field_results[1].type == T::null)
				finish(IRB->CreateCall(llvm_int_only_func(no_vector_to_AST), field_results[0].IR, s("converter")));

			if (type_check(RVO, field_results[1].type, u::vector_of_ASTs) != type_check_result::perfect_fit)
				return_code(type_mismatch, 1);
			finish(IRB->CreateCall(llvm_int_only_func(vector_to_AST), {field_results[0].IR, field_results[1].IR}, s("converter")));
		}
	case ASTn("imv_AST"):
		{
			finish(IRB->CreateCall(llvm_int_only_func(new_imv_AST), field_results[0].IR, s("new imv")));
		}
	case ASTn("load_tag"):
		{
			Tptr type_of_pointer = field_results[0].type;
			if (type_of_pointer == 0) return_code(type_mismatch, 0);
			switch (type_of_pointer.ver())
			{
			case Typen("type pointer"): finish(IRB->CreateCall(llvm_int_only_func(type_tag), field_results[0].IR));
			case Typen("AST pointer"): finish(IRB->CreateCall(llvm_int_only_func(AST_tag), field_results[0].IR));
			default: return_code(type_mismatch, 0);
			}
		}
	case ASTn("load_imv_from_AST"):
	case ASTn("load_vector_from_BB"):
		{
			auto loader = llvm_function(target->tag == ASTn("load_imv_from_AST") ? load_imv_from_AST : load_BB_from_AST, llvm_i64()->getPointerTo(), llvm_i64());
			llvm::Value* reference_to_imv = IRB->CreateCall(loader, field_results[0].IR);
			
			Return_Info case_IR;
			IRemitter run_field = [&]() -> llvm::Value*
			{
				case_IR = generate_IR(target->fields[1]); //error checking is moved to after the comparison.
				return nullptr; //signifying an unused phi value.
			};

			uint64_t starting_stack_position = object_stack.size();
			new_living_object(target, Return_Info(IRgen_status::no_error, new_reference(reference_to_imv), target->tag == ASTn("load_imv_from_AST") ? u::dynamic_object : u::vector_of_ASTs, true));

			auto reference_integer = IRB->CreatePtrToInt(reference_to_imv, llvm_i64());
			llvm::Value* comparison = IRB->CreateICmpNE(reference_integer, llvm_integer(0), s("vector reference zero check"));
			create_if_value(comparison, run_field, [](){ return nullptr; });
			if (case_IR.error_code) return case_IR;
			clear_stack(starting_stack_position);
			finish(0);
		}
	case ASTn("run_function"): finish(IRB->CreateCall(llvm_int_only_func(run_null_parameter_function), field_results[0].IR));
	case ASTn("imv"): //bakes in the value into the compiled function. changes by the function are temporary.
		{
			uint64_t* dynamic_object = (uint64_t*)target->fields[0];
			if (dynamic_object == nullptr)
				finish_special(0, 0);
			Tptr type_of_object = *(Tptr*)dynamic_object;
			uint64_t* array_of_integers = (uint64_t*)(dynamic_object + 1);
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				std::vector<llvm::Constant*> values;
				values.reserve(size_of_object);
				for (uint64_t x = 0; x < size_of_object; ++x)
					values.push_back(llvm_integer(array_of_integers[x]));
				
				llvm::Constant* object;
				if (size_of_object > 1) object = llvm::ConstantArray::get(llvm_array(size_of_object), values);
				else object = values[0];
				finish_special(object, type_of_object);
			}
			else finish_special(0, 0);
		}
	case ASTn("load_subobj"):
		{
			Tptr type_of_pointer = field_results[0].type;
			if (type_of_pointer == 0) return_code(type_mismatch, 0);
			switch (type_of_pointer.ver())
			{
			case Typen("pointer"):
			case Typen("temp pointer"):
				{
					Tptr type_of_object = type_of_pointer.field(0);
					if (llvm::ConstantInt* k = llvm::dyn_cast<llvm::ConstantInt>(field_results[1].IR)) //we need the second field to be a constant.
					{
						llvm::Value* pointer_cast = IRB->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());
						uint64_t offset = k->getZExtValue();

						Tptr offset_type = get_offset_type(type_of_object, offset);
						if (offset_type == 0) return_code(oversized_offset, 1);

						llvm::Value* place = offset ? IRB->CreateConstInBoundsGEP1_64(pointer_cast, offset, s("offset")) : pointer_cast;
						default_allocation = new_reference(place);
						finish_special(load_from_memory(place, 1), offset_type);
					}
					return_code(requires_constant, 1);
				}
			case Typen("type pointer"):
				finish_special(IRB->CreateCall(llvm_int_only_func(type_subfield), {field_results[0].IR, field_results[1].IR}), u::type);
			case Typen("AST pointer"):
				finish_special(IRB->CreateCall(llvm_int_only_func(AST_subfield_guarantee), {field_results[0].IR, field_results[1].IR}), u::AST_pointer);
			case Typen("vector"):
				if (!is_zeroable(field_results[0].type.field(0))) return_code(type_mismatch, 0);
				finish_special(IRB->CreateCall(llvm_int_only_func(vector_load), {field_results[0].IR, field_results[1].IR}), field_results[0].type.field(0));
			case Typen("function pointer"):
				if (auto k = llvm::dyn_cast<llvm::ConstantInt>(field_results[1].IR)) //we need the second field to be a constant.
				{
					uint64_t offset = k->getZExtValue();
					if (offset == 0)
						finish_special(IRB->CreateCall(llvm_int_only_func(AST_from_function), field_results[0].IR), u::AST_pointer);
					else if (offset == 1) finish_special(IRB->CreateCall(llvm_int_only_func(type_from_function), field_results[0].IR), u::type);
					else return_code(oversized_offset, 1);
				}
				return_code(requires_constant, 1);
			case Typen("con_vec"):
				{
					Tptr type_of_object = type_of_pointer;
					if (llvm::ConstantInt* k = llvm::dyn_cast<llvm::ConstantInt>(field_results[1].IR)) //we need the second field to be a constant.
					{
						uint64_t offset = k->getZExtValue();
						Tptr offset_type = get_offset_type(type_of_object, offset);
						if (offset_type == 0) return_code(oversized_offset, 1);
						if (field_results[0].place)
						{
							llvm::Value* pointer_cast = field_results[0].place->allocation;
							llvm::Value* place = offset ? IRB->CreateConstInBoundsGEP1_64(pointer_cast, offset, s("offset")) : pointer_cast;
							default_allocation = new_reference(place);
							finish_special(load_from_memory(place, 1), offset_type);
						}
						else
						{
							llvm::Value* load_single = IRB->CreateExtractValue(field_results[0].IR, offset, s("offset"));
							finish_special(load_single, offset_type);
						}
					}
					return_code(requires_constant, 1);
				}

			default:
				return_code(type_mismatch, 0);
			}
		}
	case ASTn("typeof"):
		{
			finish(llvm_integer(field_results[0].type));
		}
	case ASTn("system1"):
		{
			llvm::CallInst* systemquery = IRB->CreateCall(llvm_int_only_func(system1), field_results[0].IR);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::ReadOnly);
			finish(systemquery);
		}
	case ASTn("system2"):
		{
			llvm::CallInst* systemquery = IRB->CreateCall(llvm_int_only_func(system2), {field_results[0].IR, field_results[1].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::ReadOnly);
			finish(systemquery);
		}
	case ASTn("agency1"):
		{
			llvm::CallInst* systemquery = IRB->CreateCall(llvm_int_only_func(agency1), field_results[0].IR);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			finish(systemquery);
		}
	case ASTn("agency2"):
		{
			llvm::CallInst* systemquery = IRB->CreateCall(llvm_int_only_func(agency2), {field_results[0].IR, field_results[1].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			finish(systemquery);
		}
	case ASTn("get_event_loop"):
		{
			finish(llvm_integer((uint64_t)event_roots.at(0)));
		}
	default:
		error("no switch");
	}
	error("fell through switches");
}