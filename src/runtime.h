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

/*first argument is the location of the return object. if we didn't do this, we'd be forced to anyway, by http://www.uclibc.org/docs/psABI-i386.pdf P13. that's the way structs are returned, when 3 or larger.
takes in AST.
returns: the fptr, then the error code, then the dynamic object. for now, we let the dynamic error object be 0.
*/
inline void compile_returning_legitimate_object(uint64_t* memory_location, uint64_t int_target)
{
	auto return_location = (std::array<uint64_t, 3>*) memory_location;
	uAST* target = (uAST*)int_target;
	compiler_object a;
	uint64_t error = a.compile_AST(target);
	
	if (error)
	{
		*return_location = std::array<uint64_t, 3>{{0ull, error, 0ull}};
		return;
	}
	else
	{
		function* new_location = new(allocate_function()) function(deep_AST_copier(target).result, a.return_type, a.parameter_type, a.fptr, c, a.result_module, std::move(a.new_context));
		if (VERBOSE_GC)
		{
			console << *new_location;
		}
		*return_location = std::array < uint64_t, 3 > {{(uint64_t)new_location, error, 0ull}};
		return;
	}
}

inline void output_array(uint64_t* mem, uint64_t number)
{
	for (uint64_t idx = 0; idx < number; ++idx)
		console << mem[idx] << ' ';
	console << '\n';
}

template<size_t array_num> inline void cout_array(std::array<uint64_t, array_num> object)
{
	console << "Evaluated to";
	for (uint64_t x = 0; x < array_num; ++x) console << ' ' << object[x];
	console << '\n';
}

//parameter is a function*
//return value is a dynamic pointer to the return value. it's just the object pointer, not the type.
//on failure, we can't get the type. since this requires a branch, we should get the type here.
inline std::array<uint64_t, 2> run_null_parameter_function(uint64_t func_int)
{
	if (func_int == 0) return std::array < uint64_t, 2 > {{0, 0}};
	if (finiteness == 0) return std::array < uint64_t, 2 > {{0, 0}};
	else --finiteness;
	auto func = (function*)func_int;
	void* fptr = func->fptr;
	Type* return_type = func->return_type;
	uint64_t size_of_return = get_size(return_type);
	console << "return type of run function is: "; output_type(return_type); console << '\n';
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		return std::array < uint64_t, 2 > {{(uint64_t)return_type, (uint64_t)new_object(FP())}};
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
		FunctionType *func_type(FunctionType::get(i64_type(), false));

		std::string function_name = GenerateUniqueName("");
		Function *trampoline(Function::Create(func_type, Function::ExternalLinkage, function_name, M.get()));
		trampoline->addFnAttr(Attribute::NoUnwind); //7% speedup. and required to get Orc not to leak memory, because it doesn't unregister EH frames


		BasicBlock *BB(BasicBlock::Create(*context, "entry", trampoline));
		new_builder.SetInsertPoint(BB);
		Value* target_function = llvm_function(fptr, llvm_type(size_of_return));
		Value* result_of_call = new_builder.CreateCall(target_function, {});

		//START DYNAMIC
		llvm::Value* allocator = llvm_function(allocate, i64_type()->getPointerTo(), i64_type());
		llvm::Value* dynamic_object_raw = builder->CreateCall(allocator, {llvm_integer(size_of_return)});
		llvm::Type* target_pointer_type = llvm_type(size_of_return)->getPointerTo();
		llvm::Value* dynamic_object = builder->CreatePointerCast(dynamic_object_raw, target_pointer_type);

		//store the returned value into the acquired address
		builder->CreateStore(result_of_call, dynamic_object);
		llvm::Value* dynamic_object_address = builder->CreatePtrToInt(dynamic_object, i64_type());
		builder->CreateRet(dynamic_object_address);
		///FINISH DYNAMIC

		check(!llvm::verifyFunction(*trampoline, &llvm::outs()), "verification failed");

		auto H = c->addModule(std::move(M));

		auto ExprSymbol = c->findUnmangledSymbol(function_name);

		auto trampfptr = (uint64_t(*)())(ExprSymbol.getAddress());
		uint64_t result = trampfptr();
		c->removeModule(H);
		return std::array < uint64_t, 2 > {{(uint64_t)return_type, result}};
	}
}

//each parameter is a pointer
inline std::array<uint64_t, 2> concatenate_dynamic(uint64_t first_type, uint64_t old_pointer, uint64_t second_type)
{
	uint64_t* pointer = (uint64_t*)old_pointer;
	Type* type[2] = {(Type*)first_type, (Type*)second_type};

	uint64_t size[2];
	for (int x : { 0, 1 })
	{
		size[x] = get_size((Type*)type[x]);
	}
	if (size[0] + size[1] == 0) return {{0, 0}};
	uint64_t* new_dynamic = allocate(size[0] + size[1]);
	for (uint64_t idx = 0; idx < size[0]; ++idx) new_dynamic[idx] = pointer[idx];
	return std::array<uint64_t, 2>{{(uint64_t)concatenate_types({type[0], type[1]}), (uint64_t)new_dynamic}};
}


inline uint64_t* copy_dynamic(Type* type, uint64_t* object)
{
	uint64_t size = get_size(type);
	if (size == 0) return 0;
	uint64_t* mem_slot = allocate(size);
	for (uint64_t ind = 0; ind < size; ++ind)
		mem_slot[ind] = object[ind];
	return mem_slot;
}

//returns pointer-to-AST
inline uint64_t dynamic_to_AST(uint64_t tag, uint64_t previous, uint64_t type, uint64_t object)
{
	if (tag >= ASTn("never reached")) return 0; //you make a null AST if the tag is too high
	if (type == 0)
	{
		if (type_check(RVO, 0, get_AST_fields_type(tag)) != type_check_result::perfect_fit)
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, {});
	}
	else if (tag != ASTn("imv"))
	{
		uAST* dyn_object = (uAST*)object;
		Type* dyn_type = (Type*)type;
		if (type_check(RVO, dyn_type, get_AST_fields_type(tag)) != type_check_result::perfect_fit) //this is RVO because we're copying the dynamic object over.
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, dyn_object);
	}
	else
	{
		uint64_t* copied_mem = copy_dynamic((Type*)type, (uint64_t*)object);
		if (copied_mem == 0) return 0;
		std::vector<uAST*> AST_members{(uAST*)type, (uAST*)copied_mem};
		return (uint64_t)new_AST(tag, (uAST*)previous, AST_members);
	}
}

inline void print_uint64_t(uint64_t x) {console << x << '\n';}

inline uint64_t AST_subfield(uAST* a, uint64_t offset)
{
	if (a == nullptr) return 0;
	uint64_t size = AST_descriptor[a->tag].pointer_fields; //we're not using the total fields type, because if it's a imv, we don't grab it.
	if (offset >= size) return 0;
	else return a->fields[offset].num;
}

inline uint64_t type_subfield(Type* a, uint64_t offset)
{
	if (a == nullptr) return 0;
	if (a->tag == Typen("con_vec"))
	{
		uint64_t size = a->fields[0].num; //we're not using the total fields type, because if it's a imv, we don't grab it.
		if (offset >= size) return 0;
		else return a->fields[offset + 1].num;
	}
	else
	{
		if (offset == 0)
			return (uint64_t)a;
		else return 0;
	}
}


inline uAST* AST_from_function(function* a)
{
	if (a == nullptr) return nullptr;
	return deep_AST_copier(a->the_AST).result;
}

inline Type* type_from_function(function* a)
{
	if (a == nullptr) return nullptr;
	return a->return_type;
}