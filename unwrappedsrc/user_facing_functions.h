#pragma once
#include "types.h"
#include "cs11.h"

//warning: array<uint64_t, 2> becomes {i64, i64}
//using our current set of optimization passes, the undef+insertvalue operations aren't optimized to anything better.

inline uint64_t make_dynamic(uint64_t pointer_to_object, uint64_t pointer_to_type)
{
	return (uint64_t)(new_object(pointer_to_object, pointer_to_type));
}

//return type is actually AST*
inline uint64_t make_AST_pointer_from_dynamic(uint64_t tag, uint64_t previous_AST, uint64_t intpointer)
{
	if (tag == ASTn("null_AST")) return 0;
	return (uint64_t)new_AST(tag, (uAST*)previous_AST, (uAST*)intpointer);
}


/* this is a function that the compile() AST will call.
returns a pointer to the return object, which is either:
1. a working pointer to AST
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

//each parameter is an int64[2]*
inline uint64_t concatenate_dynamic(uint64_t first_dynamic, uint64_t second_dynamic)
{

	uint64_t* pointer[2] = {((uint64_t**)first_dynamic)[0], ((uint64_t**)second_dynamic)[0]};
	Type* type[2] = {((Type**)first_dynamic)[1], ((Type**)second_dynamic)[1]};
	uint64_t size[2];
	for (int x : { 0, 1 })
	{
		size[x] = get_size((Type*)type[x]);
	}
	if (size[0] == 0) return second_dynamic;
	else if (size[1] == 0) return first_dynamic;
	uint64_t* new_dynamic = allocate(size[0] + size[1]);
	for (int idx = 0; idx < size[0]; ++idx) new_dynamic[idx] = pointer[0][idx];
	for (int idx = 0; idx < size[1]; ++idx) new_dynamic[idx + size[0]] = pointer[1][idx];
	return make_dynamic((uint64_t)new_dynamic, (uint64_t)concatenate_types(std::vector<Type*>{type[0], type[1]}));
}

#include "memory.h"
//return value is the address
inline uint64_t user_allocate_memory(uint64_t size)
{
	return (uint64_t)(allocate(size));
}


inline bool is_AST_internal(uint64_t tag, Type* reference)
{
	return type_check(RVO, reference, get_AST_fields_type(tag)) == type_check_result::perfect_fit;
	//this is RVO because we're copying the dynamic object over.
}

inline bool is_AST_user_facing(uint64_t tag, uint64_t reference)
{
	return is_AST_internal(tag, (Type*)reference);
}

inline void print_uint64_t(uint64_t x) {console << x << '\n';}