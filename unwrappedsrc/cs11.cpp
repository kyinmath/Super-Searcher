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
bool DONT_ADD_MODULE_TO_ORC = false;
bool DELETE_MODULE_IMMEDIATELY = false;

basic_onullstream<char> null_stream;
std::ostream& console = std::cerr;
l::raw_ostream* llvm_console = &l::outs();
thread_local KaleidoscopeJIT* c;
thread_local llvm::LLVMContext* context;
thread_local llvm::IRBuilder<>* builder;
thread_local uint64_t finiteness;
llvm::TargetMachine* TM;

#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count(); //one seed for everything is a bad idea
thread_local std::mt19937_64 mersenne(seed);



#include <llvm/Transforms/Utils/Cloning.h>
//return value is the error code, which is 0 if successful
unsigned compiler_object::compile_AST(uAST* target)
{
	llvm::IRBuilder<> new_builder(*new_context);
	std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), *new_context)); //should be unique ptr because ownership will be transferred

	builder_context_stack b(&new_builder, new_context.get());

	if (VERBOSE_DEBUG) console << "starting compilation\ntarget is " << target << '\n'; //in case it crashes here because target is not valid
	if (target == nullptr) return IRgen_status::null_AST;
	using namespace llvm;
	FunctionType *dummy_type(FunctionType::get(void_type(), false));

	Function *dummy_func(Function::Create(dummy_type, Function::ExternalLinkage, "dummy_func", M.get())); //we have to insert the function in the Module so that it doesn't leak when generate_IR fails. things like instructions, basic blocks, and functions are not part of a context.

	BasicBlock *BB(BasicBlock::Create(*context, "entry", dummy_func));
	new_builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code) return return_object.error_code; //error

	return_type = return_object.type;
	check(return_type == get_unique_type(return_type, false), "compilation returned a non-unique type");
	//can't be u::does_not_return, because goto can't go forward past the end of a function.

	auto size_of_return = get_size(return_object.type);
	FunctionType* FT(FunctionType::get(llvm_type_including_void(size_of_return), false));
	if (VERBOSE_DEBUG) console << "Size of return is " << size_of_return << '\n';
	std::string function_name = GenerateUniqueName("");
	Function *F(Function::Create(FT, Function::ExternalLinkage, function_name, M.get())); //marking this private linkage seems to fail
	F->addFnAttr(Attribute::NoUnwind); //7% speedup. and required to get Orc not to leak memory, because it doesn't unregister EH frames

	F->getBasicBlockList().splice(F->begin(), dummy_func->getBasicBlockList());
	dummy_func->eraseFromParent();
	if (size_of_return)
	{
		if (size_of_return == 1)
			builder->CreateRet(return_object.IR);
		else if (return_object.IR->getType()->isArrayTy())
			builder->CreateRet(return_object.IR);
		else //since we permit {i64, i64}, we have to canonicalize the type here.
		{
			check(size_of_return < ~0u, "load object not equipped to deal with large objects, because CreateInsertValue has a small index");
			llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(size_of_return));
			for (uint64_t a = 0; a < size_of_return; ++a)
			{
				llvm::Value* integer_transfer = builder->CreateExtractValue(return_object.IR, std::vector < unsigned > { (unsigned)a });
				undef_value = builder->CreateInsertValue(undef_value, integer_transfer, std::vector < unsigned > { (unsigned)a });
			};
			builder->CreateRet(undef_value);
		}
	}
	else builder->CreateRetVoid();
	if (OUTPUT_MODULE)
		M->print(*llvm_console, nullptr);
	check(!l::verifyFunction(*F, &llvm::outs()), "verification failed");
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
	//if (stack_degree == 2) stack_degree = 0; //we're trying to diagnose the memory problems with label. this does nothing.

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
			console << "finish() in generate_IR, Value is ";
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
				desired.cast_base(size_of_return);
				if (move_to_stack) desired.store(value_collection(return_value, size_of_return));

				stack_return_location = desired;
			}
			else desired.base->eraseFromParent();
		}
		if (stack_degree == 1 && move_to_stack) //otherwise, do nothing.
		{
			if (size_of_return >= 1)
			{
				//we can't defer RVO all the way to the stack_degree = 2. but we can move it up one to concatenate() if we want. maybe we won't bother.

				desired.store(value_collection(return_value, size_of_return));
				stack_return_location = desired;
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
	//maybe later, we'll separate everything out. so if you specify the type and the return isn't special_pointer, it'll error as well.
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
	case ASTn("add"): finish(builder->CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"): finish(builder->CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("increment"): finish(builder->CreateAdd(field_results[0].IR, llvm_integer(1), s("increment")));
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

			// Create blocks for the then and else cases.  Insert the blocks at the end of the function; this is ok because the builder position is the thing that affects instruction insertion. it's necessary to insert them into the function so that we can avoid memleak on errors
			l::BasicBlock *ThenBB = l::BasicBlock::Create(*context, s("then"), TheFunction);
			l::BasicBlock *ElseBB = l::BasicBlock::Create(*context, s("else"), TheFunction);
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"), TheFunction);

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
			builder->SetInsertPoint(MergeBB);
			uint64_t result_self_validity_guarantee = std::max(then_IR.self_validity_guarantee, else_IR.self_validity_guarantee);
			uint64_t result_target_hit_contract = std::min(then_IR.target_hit_contract, else_IR.target_hit_contract);
			if (stack_degree == 0)
			{
				l::Value* endPN = llvm_create_phi(then_IR.IR, else_IR.IR, then_IR.type, else_IR.type, ThenBB, ElseBB);
				finish_special_pointer(endPN, result_type, result_self_validity_guarantee, result_target_hit_contract);
			}
			else
			{
				if (else_IR.type == u::does_not_return)
					finish_special_stack_handled_pointer(then_IR.IR, then_IR.type, then_IR.self_validity_guarantee, then_IR.target_hit_contract);
				else if (then_IR.type == u::does_not_return)
					finish_special_stack_handled_pointer(else_IR.IR, else_IR.type, else_IR.self_validity_guarantee, else_IR.target_hit_contract);

				finish_special_stack_handled_pointer(then_IR.IR, result_type, result_self_validity_guarantee, result_target_hit_contract);
			}
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

			//check finiteness. it's not an AllocaInst apparently (llvm asserts), because it wasn't allocated that way.
			l::Value* finiteness_pointer = builder->CreateIntToPtr(llvm_integer((uint64_t)&finiteness), int64_type()->getPointerTo());
			l::Value* current_finiteness = builder->CreateLoad(finiteness_pointer);
			l::Value* comparison = builder->CreateICmpNE(current_finiteness, llvm_integer(0), s("finiteness comparison"));


			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("finiteness success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(*context, s("finiteness failure"), TheFunction);
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
			//bool is_full_pointer = false;
			Type* new_pointer_type = get_non_convec_unique_type(Typen("pointer"), found_AST->second.type);

			l::Value* final_result = builder->CreatePtrToInt(found_AST->second.place.get_location(), int64_type(), s("flattening pointer"));

			finish_special_pointer(final_result, new_pointer_type, found_AST->second.self_lifetime, found_AST->second.self_lifetime);
		}
	case ASTn("load"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			//if on_stack is temp, it's not an alloca so we should copy instead of loading.
			Return_Info AST_to_load = found_AST->second;
			//I think atomic load is automatic on x64, but we don't need it anyway on a single-threaded app.
			if (AST_to_load.on_stack == stack_state::temp) finish_special_pointer(AST_to_load.IR, AST_to_load.type, AST_to_load.self_validity_guarantee, AST_to_load.target_hit_contract);
			else
			{
				uint64_t load_size = get_size(AST_to_load.type);
				llvm::Value* final_value = load_from_stack(AST_to_load.place.get_location(), load_size);
				//we load either i64* or [S x i64]*. but the address is always of form i64*.
				
				uint64_t lifetime = AST_to_load.self_lifetime; //we definitely need to figure out how out escape analysis works. this lifetime information is now nonsense.
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
				final_value = load_from_stack(desired.get_location(), final_size);
			}
			else
			{
				final_value = nullptr;
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
			llvm::Value* memory_location = builder->CreateIntToPtr(field_results[0].IR, int64_type()->getPointerTo());

			write_into_place(value_collection(field_results[1].IR, get_size(field_results[1].type)), memory_location);

			//no need for atomics on single threaded app
			//if (requires_atomic(field_results[0].on_stack))
				//builder->CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, memory_location, field_results[1].IR, llvm::AtomicOrdering::Release); //I don't think this works for large objects; memory location must match exactly. in any case, all we want is piecewise atomic.
			finish(0);
		}
	case ASTn("dynamic"):
		{
			uint64_t size_of_object = get_size(field_results[0].type);
			if (size_of_object >= 1)
			{
				l::Value* allocator = llvm_function(allocate, int64_type()->getPointerTo(), int64_type());
				l::Value* dynamic_object = builder->CreateCall(allocator, std::vector<l::Value*>{llvm_integer(size_of_object)}, s("dyn_allocate_memory"));

				//store the returned value into the acquired address
				l::Value* dynamic_object_address = builder->CreatePtrToInt(dynamic_object, int64_type());
				write_into_place(value_collection(field_results[0].IR, size_of_object), dynamic_object);

				//create a pointer to the type of the dynamic pointer. but we serialize the pointer to be an integer.
				//note that the type is already unique.
				l::Constant* integer_type_pointer = llvm_integer((uint64_t)field_results[0].type);

				//we now serialize both objects to become integers.
				l::Value* undef_value = l::UndefValue::get(llvm_array(2));
				l::Value* first_value = builder->CreateInsertValue(undef_value, integer_type_pointer, std::vector < unsigned > { 0 });
				l::Value* full_value = builder->CreateInsertValue(first_value, dynamic_object_address, std::vector < unsigned > { 1 });
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
				type[x] = builder->CreateExtractValue(field_results[x].IR, std::vector<unsigned>{0}, s("type pointer of dynamic"));
				pointer[x] = builder->CreateExtractValue(field_results[x].IR, std::vector<unsigned>{1}, s("object pointer of dynamic"));
			}

			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			//if ever the optimization changes, we might be in trouble.
			std::vector<l::Type*> return_types{int64_type(), int64_type()};
			l::Type* return_type = l::StructType::get(*context, return_types);
			l::Value* dynamic_conc_function = llvm_function(concatenate_dynamic, return_type, int64_type(), int64_type(), int64_type(), int64_type());

			std::vector<l::Value*> arguments{type[0], pointer[0], type[1], pointer[1]};
			l::Value* result_of_conc = builder->CreateCall(dynamic_conc_function, arguments, s("dynamic concatenate"));


			//bool is_full_dynamic = false;
			Type* dynamic_type = get_non_convec_unique_type(Typen("dynamic pointer"));

			finish_special(result_of_conc, dynamic_type);
		}
	case ASTn("compile"):
		{
			l::Value* compile_function = llvm_function(compile_returning_legitimate_object, void_type(), int64_type()->getPointerTo(), int64_type());

			llvm::AllocaInst* return_holder = create_actual_alloca(3);
			llvm::Value* forcing_return_type = builder->CreatePointerCast(return_holder, int64_type()->getPointerTo(), "forcing return type");
			builder->CreateCall(compile_function, std::vector<l::Value*>{forcing_return_type, field_results[0].IR}); //, s("compile"). void type means no name allowed
			auto error_object = memory_location(return_holder, 1);
			Type* error_type = concatenate_types(std::vector < Type* > {u::integer, u::dynamic_pointer});
			object_stack.push({user_target, true});
			auto insert_result = objects.insert({user_target, Return_Info(IRgen_status::no_error, error_object, error_type, final_stack_state, lifetime_of_return_value, lifetime_of_return_value, lifetime_of_return_value)});
			if (!insert_result.second) return_code(active_object_duplication, 10);


			auto error_code = memory_location(return_holder, 1);
			auto condition = builder->CreateLoad(error_code.get_location());


			l::Value* comparison = builder->CreateICmpNE(condition, llvm_integer(0), s("compile branching"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("success"), TheFunction);
			l::BasicBlock *FailureBB = l::BasicBlock::Create(*context, s("failure"), TheFunction);
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"), TheFunction);
			builder->CreateCondBr(comparison, SuccessBB, FailureBB);
			builder->SetInsertPoint(SuccessBB);


			Return_Info success_branch = generate_IR(target->fields[1].ptr, 0);
			if (success_branch.error_code) return success_branch;

			builder->CreateBr(MergeBB);
			SuccessBB = builder->GetInsertBlock();
			builder->SetInsertPoint(FailureBB);

			Return_Info error_branch = generate_IR(target->fields[2].ptr, 0);
			if (error_branch.error_code) return error_branch;

			builder->CreateBr(MergeBB);
			FailureBB = builder->GetInsertBlock();
			builder->SetInsertPoint(MergeBB);


			clear_stack(final_stack_position);
			auto return_location = memory_location(return_holder, 0);
			auto return_object = builder->CreateLoad(return_location.get_location());
			finish(return_object);
		}
	case ASTn("convert_to_AST"):
		{

			l::Value* previous_AST;
			if (type_check(RVO, field_results[1].type, nullptr) == type_check_result::perfect_fit) //previous AST is nullptr.
				previous_AST = llvm_integer(0);
			else if (type_check(RVO, field_results[1].type, u::AST_pointer) == type_check_result::perfect_fit) //previous AST actually exists
				previous_AST = field_results[1].IR;
			else return_code(type_mismatch, 1);


			l::Value* type = builder->CreateExtractValue(field_results[2].IR, std::vector < unsigned > {0}, s("type of hopeful AST"));
			l::Value* pointer = builder->CreateExtractValue(field_results[2].IR, std::vector < unsigned > {1}, s("fields of hopeful AST"));

			l::Value* converter = llvm_function(dynamic_to_AST, int64_type(), int64_type(), int64_type(), int64_type(), int64_type());
			std::vector<l::Value*> arguments{field_results[0].IR, previous_AST, type, pointer};
			l::Value* AST_result = builder->CreateCall(converter, arguments, s("converter"));

			finish(AST_result);

		}
	case ASTn("run_function"):
		{
			llvm::Type* agg_type = llvm::StructType::get(*context, std::vector < llvm::Type* > {{int64_type(), int64_type()}});
			llvm::Value* runner = llvm_function(run_null_parameter_function, agg_type, int64_type());
			l::Value* run_result = builder->CreateCall(runner, std::vector < l::Value* > {field_results[0].IR});

			finish(run_result);
		}
			
	case ASTn("load_object"): //bakes in the value into the compiled function. changes by the function are temporary.
		{
			Type* type_of_object = (Type*)target->fields[0].ptr;
			uint64_t* array_of_integers = (uint64_t*)(target->fields[1].ptr);
			uint64_t size_of_object = get_size(type_of_object);
			if (size_of_object)
			{
				std::vector<l::Constant*> values;
				values.reserve(size_of_object);
				for (uint64_t x = 0; x < size_of_object; ++x)
					values.push_back(llvm_integer(array_of_integers[x]));
				
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