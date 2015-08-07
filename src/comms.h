#include "globalinfo.h"
#include <atomic>

//sender position, then user read position, then memory start.
//each user has one receiver and one sender.
extern std::atomic<uint64_t>* receive_mem;
extern std::atomic<uint64_t>* send_mem;
extern uint64_t mmap_size;
constexpr uint64_t header_size = 2;

inline std::atomic<uint64_t>& sender_position(std::atomic<uint64_t>* k)
{
	return k[0];
}

inline std::atomic<uint64_t>& user_position(std::atomic<uint64_t>* k)
{
	return k[1];
}

inline uint64_t get_next_value()
{
	uint64_t offset = user_position(receive_mem).load();
	uint64_t* raw_area = (uint64_t*)receive_mem; //bypass the locks; the only atomic elements are the first two.
	uint64_t loaded_integer = raw_area[offset];
	if (offset > mmap_size) offset = header_size; //the starting position.
	else ++offset;
	user_position(receive_mem).store(offset);
	return loaded_integer;
}

inline bool should_I_load()
{
	uint64_t offset = user_position(receive_mem).load();
	return (sender_position(receive_mem) != offset);
}

//allocates memory to be sent. the memory region might be split in half.
//for us, it's important to have sender and receiver agree on where the next relevant element is located.
inline void allocate_memory(uint64_t elements)
{
	check(elements < mmap_size);
	uint64_t starting_position = sender_position(send_mem).load();
	uint64_t user_GC_boundary = user_position(send_mem).load();
	if (user_GC_boundary < starting_position)
		user_GC_boundary += mmap_size - header_size; //here, we reorder things so that they're in a straight line, instead of modulo.
	if ((user_GC_boundary > starting_position) && (user_GC_boundary < starting_position + elements))
	{
		//we've run out of memory. here, the admin would kill the process for not receiving memory.
		//but the user should abort(), for stuffing too much information.
	}
	starting_position += elements;
	if (starting_position >= mmap_size)
	{
		starting_position -= (mmap_size - header_size); //wraparound
	}
}