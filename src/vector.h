#pragma once
#include "types.h"
#include "memory.h"

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
	//std::array<uint64_t, 0> contents;
};

inline svector* new_vector(Tptr type)
{
	uint64_t default_initial_size = 3;
	uint64_t* new_location = allocate(sizeof(svector) / sizeof(uint64_t) + default_initial_size);
	new_location[0] = type;
	new_location[1] = 0;
	new_location[2] = default_initial_size;
	return (svector*)new_location;
}