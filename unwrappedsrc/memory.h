#pragma once
#include <cstdint>
uint64_t* allocate(uint64_t size);
constexpr bool MEMORY_VERBOSE_DEBUG = false;

template<typename... Args> inline void write_single(uint64_t* memory_location) {}
template<typename... Args> inline void write_single(uint64_t* memory_location, uint64_t x, Args... args)
{
	*memory_location = x;
	write_single(memory_location + 1, args...);
}

template<typename... Args> inline uint64_t* new_object(Args... args)
{
	uint64_t* new_memory_slot = allocate(sizeof...(args));
	write_single(new_memory_slot, args...);
	return new_memory_slot;
}
