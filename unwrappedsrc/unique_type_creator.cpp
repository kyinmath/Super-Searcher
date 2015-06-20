/*
For any Type, get_unique_type() generates a unique version of it. These unique versions are stored in type_hash_table.
First, it generates unique versions of any Types in its fields.
Then, it checks type_hash_table if there is already a unique type that is the same, byte-for-byte.
	It can do a byte comparison because the fields are already unique. If we didn't make the fields unique, two copies of [pointer [integer]] would register as different, since they would refer to different copies of [integer]. But since [integer] is first made unique, the bytes of the [pointer [integer]] type are exactly the same.
*/

#include "types.h"
#include "debugoutput.h"
#include "unique_type_creator.h"
#include "cs11.h"
#include <unordered_set>

bool UNIQUE_VERBOSE_DEBUG = false;

//this wrapper type is used for the hash table. we want equality and hashing to occur on a Type, not a Type*, since hashing on pointers is dumb. but we want the hash table to store pointers to types, so that references to them stay valid forever.

struct Type_wrapper_pointer
{
	Type* ptr;
	Type& operator*() { return *ptr; }
	//Type* operator->() { return ptr; }
	//I have no idea how the ->() operator is supposed to be overloaded. see https://stackoverflow.com/questions/21569483/c-overloading-dereference-operators
	Type_wrapper_pointer(Type* x) : ptr(x) {}
};

namespace std {
	template <> struct hash < Type_wrapper_pointer >
	{
		size_t operator () (const Type_wrapper_pointer& f) const
		{
			uint64_t hash = f.ptr->tag;
			for (int x = 0; x < total_valid_fields(f.ptr); ++x)
				hash ^= f.ptr->fields[x].num;
			//we're ignoring con_vec, but that's probably ok

			if (UNIQUE_VERBOSE_DEBUG)
			{
				console << "hash is" << hash << '\n';
			}
			return hash;
		}
	};

	//does a bit comparison
	template <> struct equal_to < Type_wrapper_pointer >
	{
		size_t operator () (const Type_wrapper_pointer& l, const Type_wrapper_pointer& r) const
		{
			if (UNIQUE_VERBOSE_DEBUG)
			{
				console << "testing equal.\n";
				output_type(l.ptr);
				output_type(r.ptr);
			}
			if ((l.ptr == nullptr) != (r.ptr == nullptr)) return false;
			if ((l.ptr == nullptr) && (r.ptr == nullptr)) return true;
			if (l.ptr->tag != r.ptr->tag)
				return false;

			uint64_t no_of_fields = total_valid_fields(l.ptr);
			uint64_t* left = (uint64_t*)l.ptr;
			uint64_t* right = (uint64_t*)r.ptr;
			for (int x = 0; x < no_of_fields; ++x)
				if (left[x] != right[x])
					return false;

			if (UNIQUE_VERBOSE_DEBUG) console << "true\n";
			return true;
		}
	};
}

std::unordered_set<Type_wrapper_pointer> type_hash_table; //a hash table of all the unique types.


//an internal function with a bool for speedup.
//the bool is true if you created a type instead of finding a type.
//this means that any types pointing to this type must necessarily not already exist in the type hash table.
//rule: for now, the user is prohibited from seeing any non-unique types. that means we can mess with the original model however we like.
std::pair<Type*, bool> get_unique_type_internal(Type* model, bool can_reuse_parameter)
{

	if (UNIQUE_VERBOSE_DEBUG)
	{
		console << "type of original model ";
		output_type(model);
	}

	bool create_new_for_sure = false; //it's true if one of the subfields created a type instead of finding it.
	//in that case, we don't have to check if this object is in the hash table. we know it is new.
	//but if it's false, it still might not be new.

	//note: we can't find the index with pointer arithmetic and range-for, because range for makes a move copy of the element.
	//this function finds unique versions of any pointer subfields.
	uint64_t counter = model->tag == Typen("con_vec") ? 1 : 0; //this counter starting value is a bit fragile.
	for (Type*& pointer : Type_pointer_range(model))
	{
		if (UNIQUE_VERBOSE_DEBUG)
		{
			console << "uniquefying subfield " << counter << '\n';
		}
		auto result = get_unique_type_internal(pointer, can_reuse_parameter);
		create_new_for_sure |= result.second;
		model->fields[counter] = result.first;
		++counter;
	}
	if (UNIQUE_VERBOSE_DEBUG)
	{
		console << "type of temporary model ";
		output_type(model);
	}

	if (create_new_for_sure)
	{
		Type* handle = copy_type(model);
		type_hash_table.insert(handle);
		return std::make_pair(handle, true);
	}
	auto existing_type = type_hash_table.find(model);
	if (existing_type == type_hash_table.end())
	{
		if (!can_reuse_parameter)
			model = copy_type(model);
		type_hash_table.insert(model);
		return std::make_pair(model, true);
	}
	else return std::make_pair(existing_type->ptr, false);

}

Type* get_unique_type(Type* model, bool can_reuse_parameter)
{
	if (model == nullptr) return nullptr;
	else return get_unique_type_internal(model, can_reuse_parameter).first;
}


void test_unique_types()
{
	Type zero("integer");
	Type zero_two("integer");
	Type dynamic("dynamic pointer");

	Type* unique_zero = get_unique_type(&zero, false);
	Type* second_zero = get_unique_type(&zero, false);
	check(unique_zero == second_zero, "duplicated type doesn't even unique");

	Type pointer_zero("pointer", &zero);
	Type pointer_zero_second("pointer", &zero_two);
	Type* unique_pointer_zero = get_unique_type(&pointer_zero, false);
	Type* unique_pointer_zero_second = get_unique_type(&pointer_zero_second, false);
	check(unique_pointer_zero == unique_pointer_zero_second, "pointers don't unique");
	check(unique_pointer_zero != unique_zero, "pointers uniqeuing to integers");

	
	Type* unique_pointer_dynamic = get_non_convec_unique_type(Type("pointer", &dynamic));
	check(unique_pointer_zero != unique_pointer_dynamic, "different pointers unique");
}