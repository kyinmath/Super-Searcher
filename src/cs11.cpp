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
#include "runtime.h"
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
KaleidoscopeJIT* c;
llvm::LLVMContext* context;
llvm::IRBuilder<>* builder;
uint64_t finiteness;
llvm::TargetMachine* TM;

#include <chrono>
#include <random>
uint64_t seed = std::chrono::system_clock::now().time_since_epoch().count(); //one seed for everything is a bad idea
std::mt19937_64 mersenne(seed);



#include <llvm/Transforms/Utils/Cloning.h>
//return value is the error code, which is 0 if successful
uint64_t compiler_object::compile_AST(uAST* target)
{
	llvm::IRBuilder<> new_builder(*new_context);
	std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), *new_context)); //should be unique ptr because ownership will be transferred

	builder_context_stack b(&new_builder, new_context.get());

	if (VERBOSE_DEBUG) console << "starting compilation\ntarget is " << target << '\n'; //in case it crashes here because target is not valid
	if (target == nullptr) return IRgen_status::null_AST;
	using namespace llvm;
	FunctionType *dummy_type(FunctionType::get(llvm_void(), false));

	Function *dummy_func(Function::Create(dummy_type, Function::ExternalLinkage, "dummy_func", M.get())); //we have to insert the function in the Module so that it doesn't leak when generate_IR fails. things like instructions, basic blocks, and functions are not part of a context.

	BasicBlock *BB(BasicBlock::Create(*context, "entry", dummy_func));
	new_builder.SetInsertPoint(BB);

	auto return_object = generate_IR(target, false);
	if (return_object.error_code) return return_object.error_code;

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
				llvm::Value* integer_transfer = builder->CreateExtractValue(return_object.IR, {(unsigned)a});
				undef_value = builder->CreateInsertValue(undef_value, integer_transfer, {(unsigned)a});
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
	//we can make a basic block with the instructions, then copy it over when needed.
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{
	while (object_stack.size() != desired_stack_size)
	{
		auto to_be_removed = object_stack.top();
		object_stack.pop();
		if (to_be_removed.second) //it's a memory location
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
Return_Info compiler_object::generate_IR(uAST* target, uint64_t stack_degree, memory_location desired)
{
	//an error has occurred. mark the target, return the error code, and don't construct a return object.
#define return_code(X, Y) do { error_location = target; error_field = Y; return Return_Info(IRgen_status::X, nullptr, memory_location(), u::null, false); } while (0)
	//if (stack_degree == 2) stack_degree = 0; //we're trying to diagnose the memory problems with label. this does nothing.

	if (VERBOSE_DEBUG)
	{
		console << "generate_IR single AST start ^^^^^^^^^\n";
		if (target) output_AST(target);
		console << "stack degree " << stack_degree;
		console << ", storage location is ";
		if (desired.base != nullptr)
		{
			desired.base->allocation->print(*llvm_console);
			console << " offset " << desired.offset << '\n';
		}
		else console << "null";
		console << '\n';
	}

	//these are required, because full_lifetime_handler relies on desired being nullptr in the concatenate() switch case
	if (stack_degree == 2) check(desired.base == nullptr, "if stack degree is 2, we should have a nullptr storage location");
	if (stack_degree == 1) check(desired.base != nullptr, "if stack degree is 1, we should have a storage location");

	//generate_IR is allowed to take nullptr. otherwise, we need an extra check beforehand. this extra check creates code duplication, which leads to typos when indices aren't changed.
	//check(target != nullptr, "generate_IR should never receive a nullptr target");
	if (target == nullptr) return Return_Info();

	//if we've seen this AST before, we're stuck in an infinite loop. return an error.
	if (loop_catcher.find(target) != loop_catcher.end()) return_code(infinite_loop, 10); //for now, 10 is a special value, and means not any of the fields
	loop_catcher.insert(target); //we've seen this AST now.


	//after we're done with this AST, we remove it from loop_catcher.
	struct loop_catcher_destructor_cleanup
	{
		//compiler_state can only be used if it is passed in.
		compiler_object* object; uAST* targ;
		loop_catcher_destructor_cleanup(compiler_object* x, uAST* t) : object(x), targ(t) {}
		~loop_catcher_destructor_cleanup() { object->loop_catcher.erase(targ); }
	} temp_object(this, target);

	//compile the previous elements in the basic block.
	if (target->preceding_BB_element)
	{
		auto previous_elements = generate_IR(target->preceding_BB_element, 2);
		if (previous_elements.error_code) return previous_elements;
	}

	///default values. they become hidden parameters to finish().
	memory_allocation* base = nullptr;
	if (stack_degree == 2)
	{
		allocations.push_back(memory_allocation()); //for stack_degree >= 1, this takes the place of return_value
		base = &allocations.back();
		desired = memory_location(base, 0);
	}

	struct allocation_handler
	{
		memory_allocation* k;
		allocation_handler(memory_allocation* kl) : k(kl) { }
		~allocation_handler()
		{
			if (k != nullptr) //memory allocation is active
			{
				if (k->size == 0)
				{
					k->allocation->eraseFromParent();
					return;
				}
				k->cast_base();
			}
		}
	} dtor_handle(base);

	//after compiling the previous elements in the basic block, we find the lifetime of this element
	//this actually starts at 2, but that's fine.
	uint64_t final_stack_position = object_stack.size();

	bool passthrough = false; //if true: I'm not constructing anything; I'm just passing through values. for example, if() and concatenate(), vs random()

	///end default values

	//generated IR of the fields of the AST
	std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it anyway.


	//internal: do not call this directly. use the finish macro instead
	//clears dead objects off the stack, and makes your result visible to other ASTs
	//if it's necessary to create and write to storage_location, we do so.
	//if stack_degree >= 1, this will ignore return_value and use "memory_location desired" instead
	//note the hidden default values that are captured; they're listed above in the ///default values section.
	auto finish_internal = [&](l::Value* return_value, Type* type) -> Return_Info
	{
		check(type == get_unique_type(type, false), "returned non unique type in finish()");


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

		memory_location stack_return_location;
		uint64_t size_of_return = get_size(type);
		if (size_of_return >= 1)
		{
			stack_return_location = desired;
			
			if (!passthrough && stack_degree >= 1)
			{
				desired.store(value_collection(return_value, size_of_return));
				stack_return_location.base->size += size_of_return;
			}
		}
		//we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for justification.
		if (stack_degree == 2) clear_stack(final_stack_position);

		if (size_of_return >= 1)
		{
			//console << "pushing to objects, its size is " << objects.size() << '\n';
			//console << "I'm pushing "; output_AST(target); console << '\n';
			//console << "and my stack degree is " << stack_degree << '\n';
			if (stack_degree >= 1)
			{
				object_stack.push({target, true});
				auto insert_result = objects.insert({target, Return_Info(IRgen_status::no_error, nullptr, stack_return_location, type, true)});
				if (!insert_result.second) //collision: AST is already there
					return_code(active_object_duplication, 10);
				return Return_Info(IRgen_status::no_error, nullptr, stack_return_location, type, true);
			}
			else //stack_degree == 0
			{
				object_stack.push({target, false});
				return Return_Info(IRgen_status::no_error, return_value, memory_location(), type, false);
			}
		}
		else return Return_Info();
	};

	//call the finish macros when you've constructed something.
	//_stack_handled if your AST has already written in the object into its memory location, for stack_degree == 1. for example, any function that passes through "desired" is a pretty good candidate.
	//for example, concatenate() and if() use previously constructed return objects, and simply pass them through.
	//remember: pass the value itself if stack_degree == 0, and pass a pointer to the value if stack_degree == 1 or 2.

	//these are for when we need to specify the return type.
	//maybe later, we'll separate everything out. so if you specify the type and the return isn't special_pointer, it'll error as well.
#define finish_special(X, type) do {return finish_internal(X, type); } while (0)

	//make sure not to duplicate X in the expression.
#define finish(X) do {check(AST_descriptor[target->tag].return_object.state != special_return, "need to specify type"); finish_special(X, get_unique_type(AST_descriptor[target->tag].return_object.type, false)); } while (0)


	for (uint64_t x = 0; x < AST_descriptor[target->tag].fields_to_compile; ++x)
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
	case ASTn("zero"): finish(llvm_integer(0));
	case ASTn("increment"): finish(builder->CreateAdd(field_results[0].IR, llvm_integer(1), s("increment")));
	case ASTn("decrement"): finish(builder->CreateSub(field_results[0].IR, llvm_integer(1)));
	case ASTn("add"): finish(builder->CreateAdd(field_results[0].IR, field_results[1].IR, s("add")));
	case ASTn("subtract"): finish(builder->CreateSub(field_results[0].IR, field_results[1].IR, s("subtract")));
	case ASTn("multiply"): finish(builder->CreateMul(field_results[0].IR, field_results[1].IR, s("multiply")));
	case ASTn("udiv"):
		{
			l::Value* comparison = builder->CreateICmpNE(field_results[1].IR, llvm_integer(0), s("division by zero check"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("finiteness success"), TheFunction);
			l::BasicBlock *ZeroBB = l::BasicBlock::Create(*context, s("finiteness failure"), TheFunction);
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"), TheFunction);
			builder->CreateCondBr(comparison, SuccessBB, ZeroBB);

			builder->SetInsertPoint(SuccessBB);
			llvm::Value* division = builder->CreateUDiv(field_results[0].IR, field_results[1].IR);
			builder->CreateBr(MergeBB);
			SuccessBB = builder->GetInsertBlock();

			builder->SetInsertPoint(ZeroBB);
			llvm::Value* default_val = llvm_integer(0);
			builder->CreateBr(MergeBB);
			ZeroBB = builder->GetInsertBlock();

			builder->SetInsertPoint(MergeBB);
			finish(llvm_create_phi({division, default_val}, {u::integer, u::integer}, {SuccessBB, ZeroBB}));
		}

	case ASTn("urem"): //copied from divu. except that we have URem, and the default value is the value itself instead of zero. this is to preserve (x / k) * k + x % k = x, and to preserve mod agreeing with equivalence classes on Z.
		{
			l::Value* comparison = builder->CreateICmpNE(field_results[1].IR, llvm_integer(0), s("division by zero check"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();
			l::BasicBlock *SuccessBB = l::BasicBlock::Create(*context, s("finiteness success"), TheFunction);
			l::BasicBlock *ZeroBB = l::BasicBlock::Create(*context, s("finiteness failure"), TheFunction);
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"), TheFunction);
			builder->CreateCondBr(comparison, SuccessBB, ZeroBB);

			builder->SetInsertPoint(SuccessBB);
			llvm::Value* division = builder->CreateURem(field_results[0].IR, field_results[1].IR);
			builder->CreateBr(MergeBB);
			SuccessBB = builder->GetInsertBlock();

			builder->SetInsertPoint(ZeroBB);
			llvm::Value* default_val = field_results[0].IR;
			builder->CreateBr(MergeBB);
			ZeroBB = builder->GetInsertBlock();

			builder->SetInsertPoint(MergeBB);
			finish(llvm_create_phi({division, default_val}, {u::integer, u::integer}, {SuccessBB, ZeroBB}));
		}
	case ASTn("print_int"):
		{
			l::Value* printer = llvm_function(print_uint64_t, llvm_void(), llvm_i64());
			finish(builder->CreateCall(printer, {field_results[0].IR})); //, s("print"). can't give name to void-return functions
		}
	case ASTn("if"): //it's vitally important that this can check pointers, so that we can tell if they're nullptr.
		{
			//since the fields are conditionally executed, the temporaries generated in each branch are not necessarily referenceable.
			//therefore, we must clear the stack between each branch.
			uint64_t if_stack_position = object_stack.size();

			//see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
			l::Value* comparison = builder->CreateICmpNE(field_results[0].IR, llvm_integer(0), s("if() condition"));
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the blocks at the end of the function; this is ok because the builder position is the thing that affects instruction insertion. it's necessary to insert them into the function so that we can avoid memleak on errors
			l::BasicBlock *ThenBB = l::BasicBlock::Create(*context, s("then"), TheFunction);
			l::BasicBlock *ElseBB = l::BasicBlock::Create(*context, s("else"), TheFunction);
			l::BasicBlock *MergeBB = l::BasicBlock::Create(*context, s("merge"), TheFunction);

			builder->CreateCondBr(comparison, ThenBB, ElseBB);
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

			builder->SetInsertPoint(MergeBB);
			if (stack_degree == 0)
			{
				l::Value* endPN = llvm_create_phi({then_IR.IR, else_IR.IR}, {then_IR.type, else_IR.type}, {ThenBB, ElseBB});
				finish_special(endPN, result_type);
			}
			else
			{
				passthrough = true;
				desired.base->size += get_size(result_type);

				if (else_IR.type == u::does_not_return) finish_special(then_IR.IR, result_type);
				else if (then_IR.type == u::does_not_return) finish_special(else_IR.IR, result_type);

				finish_special(then_IR.IR, result_type);
			}
		}

	case ASTn("label"):
		{
			l::Function *TheFunction = builder->GetInsertBlock()->getParent();

			// Create blocks for the then and else cases.  Insert the block into the function, or else it'll leak when we return_code
			l::BasicBlock *label = l::BasicBlock::Create(*context, "", TheFunction);
			auto label_insertion = labels.insert(std::make_pair(target, label_info(label, final_stack_position, true)));
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
			l::Value* finiteness_pointer = builder->CreateIntToPtr(llvm_integer((uint64_t)&finiteness), llvm_i64()->getPointerTo());
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
			builder->SetInsertPoint(FailureBB);

			Return_Info Failure_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, desired);
			if (Failure_IR.error_code) return Failure_IR;

			if (stack_degree >= 1)
			{
				passthrough = true;
				desired.base->size += get_size(Failure_IR.type);
			}
			finish_special(Failure_IR.IR, Failure_IR.type);
		}
	case ASTn("pointer"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			if (found_AST->second.memory_location_active == false) return_code(pointer_to_temporary, 0);
			//bool is_full_pointer = false;
			Type* new_pointer_type = get_non_convec_unique_type(Typen("pointer"), found_AST->second.type);

			l::Value* final_result = builder->CreatePtrToInt(found_AST->second.place.get_location(), llvm_i64(), s("flattening pointer"));
			objects.find(target->fields[0].ptr)->second.place.base->turn_full();
			finish_special(final_result, new_pointer_type);
		}
	case ASTn("copy"):
		{
			auto found_AST = objects.find(target->fields[0].ptr);
			if (found_AST == objects.end()) return_code(pointer_without_target, 0);
			//if memory_location_active is false, we should copy instead of loading.
			Return_Info AST_to_load = found_AST->second;
			if (AST_to_load.memory_location_active == false)
				finish_special(AST_to_load.IR, AST_to_load.type);
			else finish_special(load_from_memory(AST_to_load.place.get_location(), get_size(AST_to_load.type)), AST_to_load.type);
		}
	case ASTn("concatenate"):
		{
			//optimization: if stack_degree == 1, then you've already gotten the size. look it up in a special location.

			if (stack_degree == 0)
			{
				//we want the return objects to RVO. thus, we create a memory slot. warning: this is super fragile, because if we finish early, we have to clean it up.
				allocations.push_back(memory_allocation()); //for stack_degree >= 1, this takes the place of return_value
				memory_allocation* minibase = &allocations.back();
				desired = memory_location(minibase, 0);
			}
			allocation_handler stack_degree_zero(stack_degree == 0 ? desired.base : nullptr);
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
				desired.base->size = final_size;
				desired.base->cast_base();
				final_value = load_from_memory(desired.get_location(), final_size);
			}
			else
			{
				passthrough = true;
				desired.base->size += get_size(half[0].type) + get_size(half[1].type);
				final_value = nullptr;
			}

			Type* final_type = get_unique_type(concatenate_types({half[0].type, half[1].type}), true);
			finish_special(final_value, final_type);
		}

	case ASTn("store"):
		{
			if (field_results[0].type == nullptr) return_code(type_mismatch, 0);
			if (field_results[0].type->tag != Typen("pointer")) return_code(type_mismatch, 0);
			if (type_check(RVO, field_results[1].type, field_results[0].type->fields[0].ptr) != type_check_result::perfect_fit) return_code(type_mismatch, 1);
			llvm::Value* memory_location = builder->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());

			write_into_place(value_collection(field_results[1].IR, get_size(field_results[1].type)), memory_location);
			finish(0);
		}
	case ASTn("dynamify"):
		{
			if (field_results[0].type == 0)
				finish(l::ConstantArray::get(llvm_array(2), {llvm_integer(0), llvm_integer(0)}));
			if (field_results[0].type->tag == Typen("pointer"))
			{
				l::Value* dynamic_object_address = field_results[0].IR;
				l::Constant* integer_type_pointer = llvm_integer((uint64_t)field_results[0].type->fields[0].ptr);

				l::Value* undef_value = l::UndefValue::get(llvm_array(2));
				l::Value* first_value = builder->CreateInsertValue(undef_value, integer_type_pointer, { 0 });
				l::Value* full_value = builder->CreateInsertValue(first_value, dynamic_object_address,  { 1 });
				finish(full_value);
			}
			else return_code(type_mismatch, 0);
		}
	case ASTn("dynamic_conc"):
		{
			l::Value* type;
			l::Value* pointer;
				type = builder->CreateExtractValue(field_results[0].IR, {0}, s("type pointer of dynamic"));
				pointer = builder->CreateExtractValue(field_results[0].IR, {1}, s("object pointer of dynamic"));

			//this is a very fragile transformation, which is necessary because array<uint64_t, 2> becomes {i64, i64}
			//if ever the optimization changes, we might be in trouble.
			std::vector<l::Type*> return_types{llvm_i64(), llvm_i64()};
			l::Type* return_type = l::StructType::get(*context, return_types);
			l::Value* dynamic_conc_function = llvm_function(concatenate_dynamic, return_type, llvm_i64(), llvm_i64(), llvm_i64());

			l::Value* static_type = llvm_integer((uint64_t)field_results[1].type);

			std::vector<l::Value*> arguments{type, pointer, static_type};
			l::CallInst* result_of_conc = builder->CreateCall(dynamic_conc_function, arguments, s("dynamic concatenate"));
			//result_of_conc->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::ReadNone);
			result_of_conc->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);

			l::Value* integer_pointer_to_object = builder->CreateExtractValue(result_of_conc, {1}, s("pointer to object"));
			l::Value* pointer_to_object = builder->CreateIntToPtr(integer_pointer_to_object, llvm_i64()->getPointerTo());
			write_into_place({field_results[1].IR, get_size(field_results[1].type)}, pointer_to_object);
			finish(result_of_conc);
		}
	case ASTn("compile"):
		{
			l::Value* compile_function = llvm_function(compile_returning_legitimate_object, llvm_void(), llvm_i64()->getPointerTo(), llvm_i64());

			llvm::AllocaInst* return_holder = create_actual_alloca(3);
			llvm::Value* forcing_return_type = builder->CreatePointerCast(return_holder, llvm_i64()->getPointerTo(), "forcing return type");
			builder->CreateCall(compile_function, {forcing_return_type, field_results[0].IR}); //, s("compile"). void type means no name allowed
			auto return_object = builder->CreateLoad(return_holder);
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


			l::Value* type = builder->CreateExtractValue(field_results[2].IR, {0}, s("type of hopeful AST"));
			l::Value* pointer = builder->CreateExtractValue(field_results[2].IR, {1}, s("fields of hopeful AST"));

			l::Value* converter = llvm_function(dynamic_to_AST, llvm_i64(), llvm_i64(), llvm_i64(), llvm_i64(), llvm_i64());
			std::vector<l::Value*> arguments{field_results[0].IR, previous_AST, type, pointer};
			l::Value* AST_result = builder->CreateCall(converter, arguments, s("converter"));

			finish(AST_result);
		}
	case ASTn("run_function"):
		{
			llvm::Type* agg_type = llvm::StructType::get(*context,  {{llvm_i64(), llvm_i64()}});
			llvm::Value* runner = llvm_function(run_null_parameter_function, agg_type, llvm_i64());
			l::Value* run_result = builder->CreateCall(runner,  {field_results[0].IR});

			finish(run_result);
		}
	case ASTn("imv"): //bakes in the value into the compiled function. changes by the function are temporary.
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
	case ASTn("load_subobj"):
		{
			Type* type_of_pointer = field_results[0].type;
			if (type_of_pointer == nullptr) return_code(type_mismatch, 0);
			switch (type_of_pointer->tag)
			{
			case Typen("pointer"):
				{
					Type* type_of_object = type_of_pointer->fields[0].ptr;
					if (llvm::ConstantInt* k = llvm::dyn_cast<llvm::ConstantInt>(field_results[1].IR)) //we need the second field to be a constant.
					{
						uint64_t offset = k->getZExtValue();
						if (offset == 0)
						{
							//no concatenation
							finish_special(load_from_memory(field_results[0].IR, get_size(type_of_object)), type_of_object);
						}
						else //yes concatenation
						{
							if (offset >= type_of_object->fields[0].num)
								return_code(oversized_offset, 1);
							uint64_t skip_this_many;
							uint64_t size_of_load = get_size_conc(type_of_object, offset, &skip_this_many);
							llvm::Value* pointer_cast = builder->CreateIntToPtr(field_results[0].IR, llvm_i64()->getPointerTo());
							llvm::Value* place = skip_this_many ? builder->CreateConstInBoundsGEP1_64(pointer_cast, skip_this_many, "offset") : pointer_cast;
							finish_special(load_from_memory(place, size_of_load), type_of_object->fields[offset + 1].ptr);
						}
					}
					return_code(requires_constant, 1);
				}
			case Typen("AST pointer"):
				{
					llvm::Value* AST_offset = llvm_function(AST_subfield, llvm_i64(), llvm_i64(), llvm_i64());
					llvm::Value* result_AST = builder->CreateCall(AST_offset, {field_results[0].IR, field_results[1].IR});
					finish_special(result_AST, u::AST_pointer);
				}
			case Typen("type pointer"):
				{
					llvm::Value* type_offset = llvm_function(type_subfield, llvm_i64(), llvm_i64(), llvm_i64());
					llvm::Value* result_type = builder->CreateCall(type_offset, {field_results[0].IR, field_results[1].IR});
					finish_special(result_type, u::type_pointer);
				}
			case Typen("function pointer"):
				if (llvm::ConstantInt* k = llvm::dyn_cast<llvm::ConstantInt>(field_results[1].IR)) //we need the second field to be a constant.
				{
					uint64_t offset = k->getZExtValue();
					if (offset == 0)
					{
						llvm::Value* result = builder->CreateCall(llvm_function(AST_from_function, llvm_i64(), llvm_i64()), {field_results[0].IR});
						finish_special(result, u::AST_pointer);
					}
					else if (offset == 1)
					{
						llvm::Value* result = builder->CreateCall(llvm_function(type_from_function, llvm_i64(), llvm_i64()), {field_results[0].IR});
						finish_special(result, u::type_pointer);
					}
					else return_code(oversized_offset, 1);
				}
				return_code(requires_constant, 1);
			default:
				return_code(type_mismatch, 0);
			}
		}
	case ASTn("system1"):
		{
			llvm::CallInst* systemquery = builder->CreateCall(llvm_function(system1, llvm_i64(), llvm_i64()), {field_results[0].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::ReadOnly);
			finish(systemquery);
		}
	case ASTn("system2"):
		{
			llvm::CallInst* systemquery = builder->CreateCall(llvm_function(system2, llvm_i64(), llvm_i64(), llvm_i64()), {field_results[0].IR, field_results[1].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::ReadOnly);
			finish(systemquery);
		}
	case ASTn("agency1"):
		{
			llvm::CallInst* systemquery = builder->CreateCall(llvm_function(agency1, llvm_void(), llvm_i64()), {field_results[0].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			finish(systemquery);
		}
	case ASTn("agency2"):
		{
			llvm::CallInst* systemquery = builder->CreateCall(llvm_function(agency2, llvm_void(), llvm_i64(), llvm_i64()), {field_results[0].IR, field_results[1].IR});
			systemquery->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
			finish(systemquery);
		}
	}
	error("fell through switches");
}