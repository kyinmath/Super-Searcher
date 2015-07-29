#pragma once
#include "types.h"

//purpose of this file is to get rid of those stupid type errors I'm having.

struct dynobj
{
	Tptr type;
	uint64_t& operator [](uint64_t i) 
	{
		return *((uint64_t*)(&type) + i + 1);
	}

	//std::array<uint64_t, 0> object; this doesn't work. causes an exception with asan.
};

//allocates memory, and stores the type there. but doesn't write the contents.
inline dynobj* new_dynamic_obj(Tptr t)
{
	if (t == 0) return 0;
	auto new_dynamic = (dynobj*)allocate(get_size(t) + 1);
	new_dynamic->type = t;
	return new_dynamic;
}


inline dynobj* copy_dynamic(dynobj* dyn)
{
	if (dyn == 0) return 0;
	Tptr type = dyn->type;
	uint64_t size = get_size(type);
	check(size != 0, "dynamic pointer can't have null type; only base pointer can be null");
	dynobj* mem_slot = (dynobj*)allocate(size + 1);
	mem_slot->type = dyn->type;
	for (uint64_t ind = 0; ind < size; ++ind)
		(*mem_slot)[ind] = (*dyn)[ind];
	return mem_slot;
}