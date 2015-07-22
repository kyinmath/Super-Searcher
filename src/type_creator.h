#pragma once
#include <unordered_set>
#include "types.h"

void output_type(const Tptr target);
extern bool UNIQUE_VERBOSE_DEBUG;

/* "Aside: if your hash function cannot throw then it's quite important to give it a noexcept exception-specification, otherwise the hash table needs to store every element's hash code alongside the element itself (which increases memory usage and affects performance) so that container operations that must not throw do not have to recalculate the hash code."
*/

namespace std {
	template <> struct hash < Tptr >
	{
		size_t operator () (const Tptr f) const noexcept
		{
			uint64_t hash = f.ver();
			for (uint64_t x = 0; x < total_valid_fields(f); ++x)
				hash ^= f.field(x);
			//we're ignoring con_vec, but that's probably ok

			if (UNIQUE_VERBOSE_DEBUG) print("hash is", hash, '\n');
			return hash;
		}
	};

	//does a bit comparison
	template <> struct equal_to < Tptr >
	{
		size_t operator () (const Tptr l, const Tptr r) const noexcept
		{
			if (UNIQUE_VERBOSE_DEBUG)
			{
				print("testing equal: await \"true\" response \n");
				output_type(l);
				output_type(r);
			}
			if ((l == 0) != (r == 0)) return false;
			if ((l == 0) && (r == 0)) return true;

			if (l.ver() != r.ver())
				return false;

			uint64_t no_of_fields = total_valid_fields(l);
			for (uint64_t x = 0; x < no_of_fields; ++x)
				if (l.field(x) != r.field(x))
					return false;

			if (UNIQUE_VERBOSE_DEBUG) print("equal to returned true\n");
			return true;
		}
	};
}

typedef std::unordered_set<Tptr> type_htable_t;

//if the model is in the heap, can_reuse_parameter = true.
//otherwise, if it has limited lifetime (for example, it's on the stack), can_reuse_parameter = false, because we need to create a new model in the heap before making it unique
Tptr get_unique_type(Tptr model, bool can_reuse_parameter);