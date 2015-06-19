#pragma once
#include "types.h"
#include "cs11.h"

//warning: array<uint64_t, 2> becomes {i64, i64}
//using our current set of optimization passes, the undef+insertvalue operations aren't optimized to anything better.

inline uint64_t make_dynamic(uint64_t pointer_to_object, uint64_t pointer_to_type)
{
	return (uint64_t)(new uint64_t[2]{pointer_to_object, pointer_to_type});
}

//return type is actually AST*
inline uint64_t make_AST_pointer_from_dynamic(uint64_t tag, uint64_t previous_AST, uint64_t intpointer)
{
	if (tag == ASTn("null_AST")) return 0;

	uint64_t* pointer = (uint64_t*)intpointer;
	std::vector<uint64_t> fields{ 0, 0, 0, 0 };
	for (int x = 0; x < AST_descriptor[tag].pointer_fields + AST_descriptor[tag].additional_special_fields; ++x)
		fields[x] = pointer[x];
	return (uint64_t)(new Lo<uAST>(tag, (Lo<uAST>*)previous_AST, fields[0], fields[1], fields[2], fields[3]));
}


/* this is a function that the compile() AST will call.
returns a pointer to the return object, which is either:
1. a working pointer to AST
2. a failure object to be read
the bool is TRUE if the compilation is successful, and FALSE if not.
this determines what the type of the pointer is.

the type of target is actually of type AST*. it's stated as uint64_t for current convenience
Note: memory allocations that interact with the user should use new, not malloc.
we might need realloc. BUT, the advantage of new is that it throws an exception on failure.

NOTE: std::array<2> becomes {uint64_t, uint64_t}. but array<4> becomes [i64 x 4].
*/
inline uint64_t compile_user_facing(uint64_t target)
{
	compiler_object a;
	unsigned error = a.compile_AST((Lo<uAST>*)target);

	void* return_object_pointer;
	Type* return_type_pointer;
	if (error)
	{
		uint64_t* error_return = new uint64_t[3];
		error_return[0] = error;
		error_return[1] = (uint64_t)a.error_location;
		error_return[2] = a.error_field;

		return_object_pointer = error_return;
		return_type_pointer = T::error_object;
	}
	else
	{
		return_object_pointer = nullptr;
		//todo: make this actually work.

		//Type* function_return_type = a.return_type;
		return_type_pointer = new Type("function in clouds", a.return_type, a.parameter_type);
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
	uint64_t* new_dynamic = new uint64_t[size[0] + size[1]];
	for (int idx = 0; idx < size[0]; ++idx) new_dynamic[idx] = pointer[0][idx];
	for (int idx = 0; idx < size[1]; ++idx) new_dynamic[idx + size[0]] = pointer[1][idx];
	return make_dynamic((uint64_t)new_dynamic, (uint64_t)concatenate_types(std::vector<Type*>{type[0], type[1]}));
}


uint64_t ASTmaker()
{
	return 0; //we don't want any trouble. and the AST maker is a constant source of problems as it diverges from fuzztester().
	//right now, it's not going to create special parameter fields correctly. and it'll try to create a 0-tag AST, which is bad.
}

//return value is the address
inline uint64_t allocate_memory(uint64_t size)
{
	return (uint64_t)(new uint64_t[size]);
}


//we construct a model type, then run type_check on it.
//checks if they match exactly
inline bool is_AST(uint64_t tag, Type* reference)
{
	std::vector<Type*> fields;
	if (tag >= ASTn("never reached")) return false;
	int number_of_AST_pointers = AST_descriptor[tag].pointer_fields;
	for (int x = 0; x < number_of_AST_pointers; ++x)
		fields.push_back(T::AST_pointer);
	for (int x = 0; x < AST_descriptor[tag].additional_special_fields; ++x)
		fields.push_back(AST_descriptor[tag].parameter_types[number_of_AST_pointers + x]);

	return type_check(RVO, reference, concatenate_types(fields)) == type_check_result::perfect_fit;
	//this is RVO because we're copying the dynamic object over.
}

inline bool is_AST_user_facing(uint64_t tag, uint64_t reference)
{
	return is_AST(tag, (Type*)reference);
}

inline void print_uint64_t(uint64_t x) {console << x << '\n';}