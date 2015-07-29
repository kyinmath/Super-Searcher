#pragma once
#include "types.h"
#include "memory.h"

constexpr bool VECTOR_DEBUG = true;

//vector containing a single object per element.
struct svector
{
	Tptr type; //must be exactly one element.
	uint64_t size;
	uint64_t reserved_size;
	uint64_t& operator [](uint64_t i)
	{
		return *((uint64_t*)(&reserved_size) + i + 1);
	}
	//after this come the contents
	//std::array<uint64_t, 0> contents; this causes segfaults with asan/ubsan. don't do it.
};

constexpr uint64_t vector_header_size = sizeof(svector) / sizeof(uint64_t);

inline svector* new_vector(Tptr type)
{
	uint64_t default_initial_size = 3;
	uint64_t* new_location = allocate(vector_header_size + default_initial_size);
	new_location[0] = type;
	new_location[1] = 0;
	new_location[2] = default_initial_size;
	if (VECTOR_DEBUG) print("new location at ", new_location, '\n');
	return (svector*)new_location;
}

inline void pushback_int(svector*& s, uint64_t value)
{
	if (VECTOR_DEBUG) print("pushing back vector at ", s, " value ", value, '\n');
	if (s->size == s->reserved_size)
	{
		uint64_t new_size = s->reserved_size + (s->reserved_size >> 1);

		uint64_t* new_location = allocate(vector_header_size + new_size);
		new_location[0] = s->type;
		new_location[1] = s->size;
		new_location[2] = new_size;
		for (uint64_t x = 0; x < s->size; ++x)
			new_location[2 + x] = (*s)[x];
		s = (svector*)new_location;
	}
	(*s)[s->size++] = value;
}

inline void pushback(svector** s, uint64_t value)
{
	pushback_int(*s, value);
}

inline uint64_t vector_size(svector* s)
{
	return s->size;
}