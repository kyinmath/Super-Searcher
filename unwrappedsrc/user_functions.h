#pragma once
#include "types.h"
#include "cs11.h"
#include "function.h"
#include "memory.h"

//warning: array<uint64_t, 2> becomes {i64, i64}
//using our current set of optimization passes, the undef+insertvalue operations aren't optimized to anything better.


inline uint64_t generate_random() { return mersenne(); }

//generates an approximately exponential distribution using bit twiddling
inline uint64_t generate_exponential_dist()
{
	uint64_t cutoff = generate_random();
	cutoff ^= cutoff - 1; //1s starting from the lowest digit of cutoff
	return generate_random() & cutoff;
}

//return value is the address
inline uint64_t user_allocate_memory(uint64_t size)
{
	return (uint64_t)(allocate(size));
}

/*first argument is the location of the return object. if we didn't do this, we'd be forced to anyway, by http://www.uclibc.org/docs/psABI-i386.pdf P13. that's the way structs are returned, when 3 or larger.
takes in AST.
returns: the fptr, then the error code, then the dynamic object. for now, we let the dynamic error object be 0.
*/
inline void compile_returning_legitimate_object(uint64_t* memory_location, uint64_t int_target)
{
	auto return_location = (std::array<uint64_t, 3>*) memory_location;
	uAST* target = (uAST*)int_target;
	compiler_object a;
	unsigned error = a.compile_AST(target);

	if (error)
	{
		*return_location = std::array<uint64_t, 3>{{0ull, error, 0ull}};
		return;
	}
	else
	{
		function* new_location = new(allocate_function()) function(target, a.return_type, a.parameter_type, a.fptr, c, a.result_module, std::move(a.new_context));
		if (VERBOSE_GC)
		{
			console << *new_location;
		}
		*return_location = std::array < uint64_t, 3 > {{(uint64_t)new_location, error, 0ull}};
		return;
	}
}

template<size_t array_num> inline void cout_array(std::array<uint64_t, array_num> object)
{
	console << "Evaluated to";
	for (uint64_t x = 0; x < array_num; ++x) console << ' ' << object[x];
	console << '\n';
}

//parameter is a function*
//return value is a dynamic pointer to the return value. it's just the object pointer, not the type.
//todo: the problem is that on failure, we can't get the type. since this requires a switch, we should get the type here.
inline std::array<uint64_t, 2> run_null_parameter_function(uint64_t func_int)
{
	if (func_int == 0) return std::array < uint64_t, 2 > {{0, 0}};
	if (finiteness == 0) return std::array < uint64_t, 2 > {{0, 0}};
	else --finiteness;
	auto func = (function*)func_int;
	void* fptr = func->fptr;
	Type* return_type = func->return_type;
	uint64_t size_of_return = get_size(return_type);
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		return std::array < uint64_t, 2 > {{FP(), (uint64_t)return_type}};
	}
	else if (size_of_return == 0)
	{
		void(*FP)() = (void(*)())(intptr_t)fptr;
		FP();
		return std::array < uint64_t, 2 > {{0, 0}};
	}
	else //make a trampoline. this is definitely a bad solution, but too bad for us.
	{
		llvm::LLVMContext mini_context;
		llvm::IRBuilder<> new_builder(mini_context);
		std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), mini_context));
		builder_context_stack b(&new_builder, &mini_context); //for safety
		using namespace llvm;
		FunctionType *func_type(FunctionType::get(int64_type(), false));

		std::string function_name = GenerateUniqueName("");
		Function *trampoline(Function::Create(func_type, Function::ExternalLinkage, function_name, M.get()));
		trampoline->addFnAttr(Attribute::NoUnwind); //7% speedup. and required to get Orc not to leak memory, because it doesn't unregister EH frames


		BasicBlock *BB(BasicBlock::Create(*context, "entry", trampoline));
		new_builder.SetInsertPoint(BB);
		Value* target_function = llvm_function(fptr, llvm_type_including_void(size_of_return));
		Value* result_of_call = new_builder.CreateCall(target_function, std::vector < llvm::Value* > {});

		//START DYNAMIC
		llvm::Value* allocator = llvm_function(user_allocate_memory, int64_type(), int64_type());
		llvm::Value* dynamic_object_address = builder->CreateCall(allocator, std::vector < llvm::Value* > {llvm_integer(size_of_return)});
		llvm::Type* target_pointer_type = llvm_type(size_of_return)->getPointerTo();

		//store the returned value into the acquired address
		llvm::Value* dynamic_object = builder->CreateIntToPtr(dynamic_object_address, target_pointer_type);
		builder->CreateStore(result_of_call, dynamic_object);
		builder->CreateRet(dynamic_object_address);
		///FINISH DYNAMIC

		check(!llvm::verifyFunction(*trampoline, &llvm::outs()), "verification failed");

		auto H = c->addModule(std::move(M));

		auto ExprSymbol = c->findUnmangledSymbol(function_name);

		auto trampfptr = (uint64_t(*)())(ExprSymbol.getAddress());
		uint64_t result = trampfptr();
		c->removeModule(H);
		return std::array < uint64_t, 2 > {{result, (uint64_t)return_type}};
	}
}

inline void run_null_parameter_function_bogus(uint64_t func_int)
{
	auto func = (function*)func_int;
	void* fptr = func->fptr;
	if (VERBOSE_GC) console << "fptr is " << fptr << '\n';
	uint64_t size_of_return = get_size(func->return_type);
	if (!DONT_ADD_MODULE_TO_ORC && !DELETE_MODULE_IMMEDIATELY)
	{
		if (size_of_return == 1)
		{
			uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
			console << "Evaluated to " << FP() << '\n';
		}
		else if (size_of_return > 1)
		{
			using std::array;
			switch (size_of_return)
			{
			case 2: cout_array(((array<uint64_t, 2>(*)())(intptr_t)fptr)()); break; //theoretically, this ought to break. array<2> = {int, int}
			case 3: cout_array(((array<uint64_t, 3>(*)())(intptr_t)fptr)()); break; //this definitely does break.
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
	}
}

//each parameter is a pointer
inline std::array<uint64_t, 2> concatenate_dynamic(uint64_t first_pointer, uint64_t first_type, uint64_t second_pointer, uint64_t second_type)
{
	uint64_t* pointer[2] = {(uint64_t*)first_pointer, (uint64_t*)second_pointer};
	Type* type[2] = {(Type*)first_type, (Type*)second_type};

	if (type[0] == 0) return std::array<uint64_t, 2>{{second_pointer, second_type}};
	else if (type[1] == 0) return std::array<uint64_t, 2>{{first_pointer, first_type}};
	uint64_t size[2];
	for (int x : { 0, 1 })
	{
		size[x] = get_size((Type*)type[x]);
	}
	check((size[0] != 0) && (size[1] != 0), "dynamic pointers should have zero in the base pointer only");
	uint64_t* new_dynamic = allocate(size[0] + size[1]);
	for (uint64_t idx = 0; idx < size[0]; ++idx) new_dynamic[idx] = pointer[0][idx];
	for (uint64_t idx = 0; idx < size[1]; ++idx) new_dynamic[idx + size[0]] = pointer[1][idx];
	return std::array<uint64_t, 2>{{(uint64_t)new_dynamic, (uint64_t)concatenate_types(std::vector<Type*>{type[0], type[1]})}};
}

//returns pointer-to-AST
inline uint64_t dynamic_to_AST(uint64_t tag, uint64_t previous, uint64_t object, uint64_t type)
{
	if (tag >= ASTn("never reached")) return 0; //you make a null AST if the tag is too high
	if (type == 0)
	{
		if (type_check(RVO, 0, get_AST_fields_type(tag)) != type_check_result::perfect_fit)
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, std::vector<uAST*>{});
	}
	else
	{
		uAST* dyn_object = (uAST*)object;
		Type* dyn_type = (Type*)type;
		if (type_check(RVO, dyn_type, get_AST_fields_type(tag)) != type_check_result::perfect_fit) //this is RVO because we're copying the dynamic object over.
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, dyn_object);
	}
}

inline void print_uint64_t(uint64_t x) {console << x << '\n';}