#pragma once
#include <unordered_set>
#include "types.h"

void output_type(const Type* target);
extern bool UNIQUE_VERBOSE_DEBUG;

/* "Aside: if your hash function cannot throw then it's quite important to give it a noexcept exception-specification, otherwise the hash table needs to store every element's hash code alongside the element itself (which increases memory usage and affects performance) so that container operations that must not throw do not have to recalculate the hash code."
*/

namespace std {
	template <> struct hash < Type* >
	{
		size_t operator () (const Type* f) const noexcept
		{
			uint64_t hash = f->tag;
			for (uint64_t x = 0; x < total_valid_fields(f); ++x)
				hash ^= f->fields[x].num;
			//we're ignoring con_vec, but that's probably ok

			if (UNIQUE_VERBOSE_DEBUG) console << "hash is" << hash << '\n';
			return hash;
		}
	};

	//does a bit comparison
	template <> struct equal_to < Type* >
	{
		size_t operator () (const Type* l, const Type* r) const noexcept
		{
			if (UNIQUE_VERBOSE_DEBUG)
			{
				console << "testing equal: await \"true\" response \n";
				output_type(l);
				output_type(r);
			}
			if ((l == nullptr) != (r == nullptr)) return false;
			if ((l == nullptr) && (r == nullptr)) return true;

			if (l->tag != r->tag)
				return false;

			uint64_t no_of_fields = total_valid_fields(l);
			for (uint64_t x = 0; x < no_of_fields; ++x)
				if (l->fields[x].num != r->fields[x].num)
					return false;

			if (UNIQUE_VERBOSE_DEBUG) console << "equal to returned true\n";
			return true;
		}
	};
}

typedef std::unordered_set<Type*> type_htable_t;

//if the model is in the heap, can_reuse_parameter = true.
//otherwise, if it has limited lifetime (for example, it's on the stack), can_reuse_parameter = false, because we need to create a new model in the heap before making it unique
Type* get_unique_type(Type* model, bool can_reuse_parameter);


template<typename... should_be_type_ptr> inline Type* get_non_convec_unique_type(uint64_t tag, should_be_type_ptr... fields) 
{
	std::vector<Type*> field_vector{fields...};
	Type* model = new_type(tag, field_vector);
	return get_unique_type(model, true);
}

//call this to test the uniqueing function.
void test_unique_types();