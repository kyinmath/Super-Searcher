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
			print(*new_location);
		}
		*return_location = std::array < uint64_t, 3 > {{(uint64_t)new_location, error, 0ull}};
		return;
	}
}

inline void output_array(uint64_t* mem, uint64_t number)
{
	for (uint64_t idx = 0; idx < number; ++idx)
		print(mem[idx], ' ');
	print('\n');
}

template<size_t array_num> inline void cout_array(std::array<uint64_t, array_num> object)
{
	print("Evaluated to");
	for (uint64_t x = 0; x < array_num; ++x) print(' ', object[x]);
	print('\n');
}
#include "dynamic.h"

//return value is a dynamic pointer to the return value. it's just the object pointer, not the type.
//on failure, we can't get the type. since this requires a branch, we should get the type here.
inline dynobj* run_null_parameter_function(function* func)
{
	if (func == 0) return 0;
	if (finiteness == 0) return 0;
	else --finiteness;
	void* fptr = func->fptr;
	Tptr return_type = func->return_type;
	uint64_t size_of_return = get_size(return_type);
	print("return type of run function is: "); output_type(return_type); print('\n');
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
		return (dynobj*)new_object_value((uint64_t)return_type, FP());
	}
	else if (size_of_return == 0)
	{
		void(*FP)() = (void(*)())(intptr_t)fptr;
		FP();
		return 0;
	}
/*	else if (size_of_return >= 4)
	{
		auto FP = ((uint64_t*)(*)(uint64_t*))(void*)fptr;
		uint64_t* k = FP(new uint64_t[size_of_return]);
		output_array(&k[0], size_of_return);
		return std::array < uint64_t, 2 > {{(uint64_t)return_type, (uint64_t)k}};
	}*/
	else //make a trampoline. this is definitely a bad solution, but too bad for us.
	{
		llvm::LLVMContext mini_context;
		llvm::IRBuilder<> new_builder(mini_context);
		std::unique_ptr<llvm::Module> M(new llvm::Module(GenerateUniqueName("jit_module_"), mini_context));
		builder_context_stack b(&new_builder, &mini_context); //for safety
		using namespace llvm;
		FunctionType *func_type(FunctionType::get(llvm_i64(), false));

		std::string function_name = GenerateUniqueName("");
		Function *trampoline(Function::Create(func_type, Function::ExternalLinkage, function_name, M.get()));
		trampoline->addFnAttr(Attribute::NoUnwind); //7% speedup. required to stop Orc from leaking memory, because it doesn't unregister EH frames


		BasicBlock *BB(BasicBlock::Create(*context, "entry", trampoline));
		new_builder.SetInsertPoint(BB);
		Value* target_function = llvm_function(fptr, llvm_type(size_of_return));
		Value* result_of_call = new_builder.CreateCall(target_function, {});



		//START DYNAMIC. writes in both the type and the object.
		llvm::Value* allocator = llvm_function(allocate, llvm_i64()->getPointerTo(), llvm_i64());
		llvm::Value* dynamic_object_raw = builder->CreateCall(allocator, {llvm_integer(size_of_return + 1)});
		auto integer_type_pointer = llvm_integer((uint64_t)return_type);
		write_into_place(integer_type_pointer, dynamic_object_raw);

		llvm::Value* dynamic_actual_object_address = builder->CreateGEP(dynamic_object_raw, llvm_integer(1));
		llvm::Type* target_pointer_type = llvm_type(size_of_return)->getPointerTo();
		llvm::Value* dynamic_object = builder->CreatePointerCast(dynamic_actual_object_address, target_pointer_type);

		//store the returned value into the acquired address
		builder->CreateStore(result_of_call, dynamic_object);
		builder->CreateRet(builder->CreatePtrToInt(dynamic_object_raw, llvm_i64()));
		///FINISH DYNAMIC

#ifndef NO_CONSOLE
		check(!llvm::verifyFunction(*trampoline, &llvm::outs()), "verification failed");
#endif

		auto H = c->addModule(std::move(M));
		auto ExprSymbol = c->findUnmangledSymbol(function_name);

		auto trampfptr = (uint64_t(*)())(ExprSymbol.getAddress());
		dynobj* result = (dynobj*)trampfptr();
		c->removeModule(H);
		return result;
	}
}
#include "vector.h"
//returns pointer-to-AST. if the vector_of_ASTs is nullptr (which only happens when the object passed in is null, use no_vector_to_AST), then it's assumed to be empty
inline uAST* vector_to_AST(uint64_t tag, svector* vector_of_ASTs)
{
	if (tag >= ASTn("never reached")) return 0; //you make a null AST if the tag is too high
	if (tag == ASTn("imv")) return 0; //no making imvs this way.
	if (tag == ASTn("basicblock"))
	{
		if (vector_of_ASTs == nullptr)
			return new_AST(tag, {});
		else return new_AST(tag, llvm::ArrayRef<uAST*>(*vector_of_ASTs));
	}
	uint64_t AST_field_size = get_field_size_of_AST(tag);
	uint64_t AST_size = get_full_size_of_AST(tag);
	uAST* new_AST = (uAST*)allocate(AST_size); //we need to do this raw business, instead of using new_AST(), because we might have empty fields.
	new_AST->tag = tag;
	for (uint64_t x = 0; x < AST_field_size; ++x)
	{
		if (vector_of_ASTs == nullptr)
			new_AST->fields[x].num = 0;
		else if (vector_of_ASTs->size > x)
			new_AST->fields[x].num = (*vector_of_ASTs)[x];
		else new_AST->fields[x].num = 0;
	}
	if (VERBOSE_GC) print("making new AST ", new_AST, '\n');
	return new_AST;
}

inline uAST* new_imv_AST(dynobj* dyn)
{
	uAST* new_AST = (uAST*)new_object_value(ASTn("imv"), dyn);
	if (VERBOSE_GC) print("making new imv AST ", new_AST, '\n');
	return new_AST;
}


inline uAST* no_vector_to_AST(uint64_t tag)
{
	return vector_to_AST(tag, nullptr);
}

inline void print_uint64_t(uint64_t x) {print("printing ", std::hex, x, '\n');}

//returns pointer to the desired element. returns 0 on failure.
inline uAST** AST_subfield(uAST* a, uint64_t offset)
{
	if (a == nullptr) return 0;
	if (a->tag == ASTn("basicblock"))
	{
		return (uAST**)reference_at((svector*)a->fields[0].ptr, offset);
	}
	else
	{
		uint64_t size = AST_descriptor[a->tag].pointer_fields; //we're not using the total fields type, because if it's a imv, we don't grab it.
		if (offset >= size) return 0;
		else return &a->fields[offset].ptr;
	}
}

inline uint64_t type_subfield(Tptr a, uint64_t offset)
{
	if (a == 0) return 0;
	if (a.ver() == Typen("con_vec"))
	{
		uint64_t size = a.field(0); //we're not using the total fields type, because if it's a imv, we don't grab it.
		if (offset >= size) return 0;
		else return a.field(offset + 1);
	}
	else return (offset == 0) ? (uint64_t)a : 0;
}


inline uAST* AST_from_function(function* a)
{
	if (a == nullptr) return nullptr;
	return deep_AST_copier(a->the_AST).result;
}

inline Tptr type_from_function(function* a)
{
	if (a == nullptr) return 0;
	return a->return_type;
}

inline uint64_t system1(uint64_t par)
{
	switch (par)
	{
	case 0:
		return ASTn("never reached");
	case 1:
		return finiteness;
	case 2:
		return mersenne();
	default:
		return 0;
	}
}
inline uint64_t system2(uint64_t first, uint64_t par)
{
	switch (first)
	{
	case 0:
		if (par < ASTn("never reached"))
			return AST_descriptor[par].pointer_fields;
		else return 0;
	default:
		return 0;
	}
}

inline void agency1(uint64_t par)
{
	switch (par)
	{
	case 0:
	default:
		return;
	}
}
inline void agency2(uint64_t first, uint64_t par)
{
	switch (first)
	{
	case 0:
		print(par, ' ');
		return;
	default:
		return;
	}
}

//relies on ABI knowledge. actual function signature is void (return*, type, offset).
//return object is: type, then branch (ver(), except 0 on null). can be called "readnone" because the types are all immut
//return 0 if nothing was received.
//we don't need to return the offset, because all types have size 1.
inline std::array<uint64_t, 2> dynamic_subtype(Tptr type, uint64_t offset)
{
	if (type == 0)
		return {{0, 0}};
	else if (type.ver() != Typen("con_vec"))
	{
		if (offset == 0)
			return{{(uint64_t)type, type.ver()}};
		else return{{0, 0}};
	}
	else
	{
		if (offset >= type.field(0))
			return{{0, 0}};
		uint64_t skip_this_many;
		get_size_conc(type, offset, &skip_this_many);
		//if (VERBOSE_DEBUG) print("dynamic_subtype is returning ", type.field(offset + 1), " and ", skip_this_many, '\n');
		return{{type.field(offset + 1), type.field(offset + 1).ver()}};
	}
}


inline uint64_t load_imv_from_AST(uAST* p)
{
	if (p == 0) return 0;
	else if (p->tag != 0) return 0;
	else return p->fields[0].num;
}


inline uint64_t AST_tag(uAST* p)
{
	if (p == 0) return ~0ull;
	else return p->tag;
}

inline uint64_t type_tag(Tptr p)
{
	return p.ver();
}