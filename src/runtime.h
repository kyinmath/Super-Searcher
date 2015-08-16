#pragma once
#include "types.h"
#include "cs11.h"
#include "function.h"
#include "memory.h"

//warning: array<uint64_t, 2> becomes {i64, i64}
//using our current set of optimization passes, the undef+insertvalue operations aren't optimized to anything better.

//you won! time to fork yourself
inline void copy_self()
{
	//we could:
	//fork
	//reinitialize the random number generator
	//signal to the admin, so you can get a new map.
	//make an event...but only for the new user, probably.

	//however, signaling to the admin is hard. thus, we'll just serialize and unserialize.
}
inline uint64_t generate_random() { return mersenne(); }

//generates an approximately exponential distribution using bit twiddling
inline uint64_t generate_exponential_dist()
{
	uint64_t cutoff = generate_random();
	cutoff ^= cutoff - 1; //1s starting from the lowest digit of cutoff
	return generate_random() & cutoff;
}

//if there isn't a preallocated location, then pass in nullptr for the second argument.
//this copies the AST if and only if the second argument is nullptr
inline function* compile_specifying_location(uAST* target, function* pre_allocated_location)
{

	compiler_object a;
	uint64_t error = a.compile_AST(target);
	if (error) return 0;
	else
	{
		pre_allocated_location = new(pre_allocated_location ? pre_allocated_location : allocate_function()) function(pre_allocated_location ? target : deep_AST_copier(target).result, a.return_type, a.parameter_type, a.fptr, a.result_module, std::move(a.new_context));

		if (VERBOSE_GC) print(*pre_allocated_location);
		return pre_allocated_location;
	}
}

inline function* compile_returning_just_function(uAST* target)
{
	return compile_specifying_location(target, nullptr);
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

//return value is a dynamic object to the return value. it's just the object pointer, not the type.
//on failure, we can't get the type. since this requires a branch, we should get the type here.
inline dynobj* run_null_parameter_function(function* func)
{
	if (func == 0) return 0;
	if (finiteness == 0) return 0;
	else --finiteness;
	void* fptr = func->fptr;
	Tptr return_type = func->return_type;
	if (return_type == u::dynamic_object) return ((dynobj*(*)())fptr)(); //special case: if it already returns a dynamic object, don't wrap it again.
	uint64_t size_of_return = get_size(return_type);
	//print("return type of run function is: "); output_type(return_type); print('\n');
	if (size_of_return == 1)
	{
		uint64_t(*FP)() = (uint64_t(*)())fptr;
		return (dynobj*)new_object_value((uint64_t)return_type, FP());
	}
	else if (size_of_return == 0)
	{
		void(*FP)() = (void(*)())fptr;
		FP();
		return 0;
	}
/*	else if (size_of_return >= 4) //probably is so rare, not even worth making a case for.
	{
		auto FP = ((uint64_t*)(*)(uint64_t*))(void*)fptr;
		uint64_t* k = FP(new uint64_t[size_of_return]);
		output_array(&k[0], size_of_return);
		return std::array < uint64_t, 2 > {{(uint64_t)return_type, (uint64_t)k}};
	}*/
	else //make a trampoline. this is definitely a bad solution, but too bad for us. later, we'll want a persistent trampoline that we can reuse and pass in function pointers to.
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
		Value* target_function = llvm_function((uint64_t(*)())fptr, llvm_type(size_of_return)); //cast the function to a fake fptr type.
		Value* result_of_call = new_builder.CreateCall(target_function, {});

		//START DYNAMIC. writes in both the type and the object.
		llvm::Value* dynamic_allocator = llvm_function(new_dynamic_obj, llvm_i64()->getPointerTo(), llvm_i64());
		llvm::Value* dynamic_object_raw = IRB->CreateCall(dynamic_allocator, llvm_integer(return_type));

		llvm::Value* dynamic_actual_object_address = IRB->CreateGEP(dynamic_object_raw, llvm_integer(1));
		llvm::Type* target_pointer_type = llvm_type(size_of_return)->getPointerTo();
		llvm::Value* dynamic_object = IRB->CreatePointerCast(dynamic_actual_object_address, target_pointer_type);

		//store the returned value into the acquired address
		IRB->CreateStore(result_of_call, dynamic_object);
		IRB->CreateRet(IRB->CreatePtrToInt(dynamic_object_raw, llvm_i64()));
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
			new_AST->fields[x] = 0;
		else if (vector_of_ASTs->size > x)
			new_AST->fields[x] = (uAST*)((*vector_of_ASTs)[x]);
		else new_AST->fields[x] = 0;
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
		return (uAST**)reference_at((svector*)a->fields[0], offset);
	}
	else
	{
		uint64_t size = AST_descriptor[a->tag].pointer_fields; //we're not using the total fields type, because if it's a imv, we don't grab it.
		if (offset >= size) return 0;
		else return &a->fields[offset];
	}
}

//returns the desired element, or 0 if too far
inline uAST* AST_subfield_guarantee(uAST* a, uint64_t offset)
{
	uAST** result = AST_subfield(a, offset);
	return result ? *result : 0;
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

inline uint64_t overwrite_func(function* first, function* second)
{
	if (first == nullptr || second == nullptr) return 0;
	if (first->return_type != second->return_type) return 0;
	first->~function();
	compile_specifying_location(second->the_AST, first);
	return 1;
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
		//if (VERBOSE_DEBUG) print("dynamic_subtype is returning ", type.field(offset + 1), " and ", skip_this_many, '\n');
		return{{type.field(offset + 1), type.field(offset + 1).ver()}};
	}
}


inline uint64_t* load_imv_from_AST(uAST* p)
{
	if (p == 0) return 0;
	else if (p->tag != ASTn("imv")) return 0;
	else return (uint64_t*)(&p->fields[0]);
}

inline uint64_t* load_BB_from_AST(uAST* p)
{
	if (p == 0) return 0;
	else if (p->tag != ASTn("basicblock")) return 0;
	else return (uint64_t*)(&p->fields[0]);
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