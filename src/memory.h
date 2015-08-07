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

//parameter takes a pointer so addition does the *sizeof(uint64_t) automatically.
inline void correct_function_pointer(uint64_t*& memory) { if (memory != 0) memory += function_pointer_offset; }
inline void correct_pointer(uint64_t*& memory) { if (memory != 0) memory += pointer_offset; }

extern uint64_t* big_memory_pool;


void serialize(uint64_t id);
void unserialize(uint64_t id);