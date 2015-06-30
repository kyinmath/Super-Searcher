#pragma once
#include <unordered_set>
#include "types.h"

void output_type(const Type* target);
extern bool UNIQUE_VERBOSE_DEBUG;
namespace std {
	template <> struct hash < Type* >
	{
		size_t operator () (const Type* f) const
		{
			uint64_t hash = f->tag;
			for (int x = 0; x < total_valid_fields(f); ++x)
				hash ^= f->fields[x].num;
			//we're ignoring con_vec, but that's probably ok

			if (UNIQUE_VERBOSE_DEBUG)
			{
				console << "hash is" << hash << '\n';
			}
			return hash;
		}
	};

	//does a bit comparison
	template <> struct equal_to < Type* >
	{
		size_t operator () (const Type* l, const Type* r) const
		{
			if (UNIQUE_VERBOSE_DEBUG)
			{
				console << "testing equal.\n";
				output_type(l);
				output_type(r);
			}
			if ((l == nullptr) != (r == nullptr)) return false;
			if ((l == nullptr) && (r == nullptr)) return true;
			if (l->tag != r->tag)
				return false;

			uint64_t no_of_fields = total_valid_fields(l);
			uint64_t* left = (uint64_t*)l;
			uint64_t* right = (uint64_t*)r;
			for (int x = 0; x < no_of_fields; ++x)
				if (left[x] != right[x])
					return false;

			if (UNIQUE_VERBOSE_DEBUG) console << "true\n";
			return true;
		}
	};
}

typedef std::unordered_set<Type*> type_htable_t;

//if the model is in the heap, can_reuse_parameter = true.
//otherwise, if it has limited lifetime (for example, it's on the stack), can_reuse_parameter = false, because we need to create a new model in the heap before making it unique
Type* get_unique_type(Type* model, bool can_reuse_parameter);
inline Type* get_non_convec_unique_type(Type model) {return get_unique_type(&model, false);}

//call this to test the uniqueing function.
void test_unique_types();