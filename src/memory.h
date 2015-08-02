#pragma once
#include <cstdint>
uint64_t* allocate(uint64_t size);

template<typename... Args> inline void write_single(uint64_t* memory_location) {}
template<typename... Args, typename T> inline void write_single(uint64_t* memory_location, T x, Args... args)
{
	*memory_location = (uint64_t)x;
	write_single(memory_location + 1, args...);
}

//writes the given values into a new object.
template<typename... Args> inline uint64_t* new_object_value(Args... args)
{
	uint64_t* new_memory_slot = allocate(sizeof...(args));
	write_single(new_memory_slot, args...);
	return new_memory_slot;
}


void start_GC();