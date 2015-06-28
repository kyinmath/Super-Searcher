#pragma once
#include "types.h"
#include "cs11.h"

//warning: array<uint64_t, 2> becomes {i64, i64}
//using our current set of optimization passes, the undef+insertvalue operations aren't optimized to anything better.

inline uint64_t make_dynamic(uint64_t pointer_to_object, uint64_t pointer_to_type)
{
	return (uint64_t)(new_object(pointer_to_object, pointer_to_type));
}


/* this is a function that the compile() AST will call.
returns a pointer to the return object, which is either:
1. a working fptr
2. a failure object to be read
the bool is TRUE if the compilation is successful, and FALSE if not.
this determines what the type of the pointer is.

the type of target is actually of type AST*. it's stated as uint64_t for current convenience

NOTE: std::array<2> becomes {uint64_t, uint64_t}. but array<4> becomes [i64 x 4].
*/
inline uint64_t compile_user_facing(uint64_t target)
{
	compiler_object a;
	unsigned error = a.compile_AST((uAST*)target);

	void* return_object_pointer;
	Type* return_type_pointer;
	if (error)
	{
		uint64_t* error_return = new_object(error, (uint64_t)a.error_location, a.error_field);

		return_object_pointer = error_return;
		return_type_pointer = concatenate_types(std::vector < Type* > {T::integer, T::AST_pointer, T::integer});
	}
	else
	{
		return_object_pointer = nullptr;
		//todo: make this actually work.

		//Type* function_return_type = a.return_type;
		return_type_pointer = new_type(Typen("function in clouds"), std::vector<Type*>{a.return_type, a.parameter_type});
	}
	return make_dynamic((uint64_t)return_object_pointer, (uint64_t)return_type_pointer);
}

#include "function.h"
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
		function* new_location = new(allocate_function()) function(target, a.return_type, a.parameter_type, a.fptr, &c->J, a.result_module);
		if (MEMORY_VERBOSE_DEBUG) console << "new location is " << new_location << '\n';
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

//first parameter is a function*
//return value is a pointer to an array of integers. except, we actually don't do anything at the moment.
inline uint64_t run_null_parameter_function(uint64_t func_int)
{
	auto func = (function*)func_int;
	void* fptr = func->fptr;
	if (MEMORY_VERBOSE_DEBUG) console << "fptr is " << fptr << '\n';
	uint64_t size_of_return = get_size(func->return_type);
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
	return 923913;
}

//each parameter is an int64[2]*
inline uint64_t concatenate_dynamic(uint64_t first_dynamic, uint64_t second_dynamic)
{

	if (first_dynamic == 0) return second_dynamic;
	else if (second_dynamic == 0) return first_dynamic;
	uint64_t* pointer[2] = {((uint64_t**)first_dynamic)[0], ((uint64_t**)second_dynamic)[0]};
	Type* type[2] = {((Type**)first_dynamic)[1], ((Type**)second_dynamic)[1]};
	uint64_t size[2];
	for (int x : { 0, 1 })
	{
		size[x] = get_size((Type*)type[x]);
	}
	check((size[0] != 0) && (size[1] != 0), "dynamic pointers should have zero in the base pointer only");
	uint64_t* new_dynamic = allocate(size[0] + size[1]);
	for (uint64_t idx = 0; idx < size[0]; ++idx) new_dynamic[idx] = pointer[0][idx];
	for (uint64_t idx = 0; idx < size[1]; ++idx) new_dynamic[idx + size[0]] = pointer[1][idx];
	return make_dynamic((uint64_t)new_dynamic, (uint64_t)concatenate_types(std::vector<Type*>{type[0], type[1]}));
}

#include "memory.h"
//return value is the address
inline uint64_t user_allocate_memory(uint64_t size)
{
	return (uint64_t)(allocate(size));
}

//returns pointer-to-AST
inline uint64_t dynamic_to_AST(uint64_t tag, uint64_t previous, uint64_t dynamic)
{
	if (tag >= ASTn("never reached")) return 0; //you make a null AST if the tag is too high
	if (dynamic == 0)
	{
		if (type_check(RVO, 0, get_AST_fields_type(tag)) != type_check_result::perfect_fit)
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, std::vector<uAST*>{});
	}
	else
	{
		uAST* object = *(uAST**)(dynamic);
		Type* dyn_type = *((Type**)(dynamic) + 1);
		if (type_check(RVO, dyn_type, get_AST_fields_type(tag)) != type_check_result::perfect_fit) //this is RVO because we're copying the dynamic object over.
			return 0;
		return (uint64_t)new_AST(tag, (uAST*)previous, object);
	}
}

inline void print_uint64_t(uint64_t x) {console << x << '\n';}