#include "globalinfo.h"
#include <atomic>
#include "generic_ipc.h"

//sender position, then user read position, then memory start.
//each user has one receiver and one sender.
extern std::atomic<uint64_t>* receive_mem;
extern std::atomic<uint64_t>* send_mem;

inline uint64_t get_next_value()
{
	return get_next_value_choice(receive_mem);
}

inline bool should_I_load()
{
	return should_I_load_choice(send_mem);
}

//allocates memory to be sent. the memory region might be split in half.
//for us, it's important to have sender and receiver agree on where the next relevant element is located.
//returns the offset of the start of the allocated memory.
inline uint64_t allocate_memory(uint64_t elements)
{
	return allocate_memory_choice(send_mem, elements);
}