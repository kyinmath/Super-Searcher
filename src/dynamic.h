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
