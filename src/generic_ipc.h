#include "globalinfo.h"
#include <atomic>

//sender position, then user read position, then memory start.
//each user has one receiver and one sender.
extern uint64_t mmap_size;
constexpr uint64_t ssqh_size = 2; //single sender queue header size

struct single_sender_queue
{
	single_sender_queue(void* k) : memory_location((std::atomic<uint64_t>*)k) {}
	void initialize() {
		memory_location[0].store(ssqh_size);
		memory_location[1].store(ssqh_size);
	}
	std::atomic<uint64_t>* memory_location;

	//these are offsets from the base. so starting value gives 2.
	std::atomic<uint64_t>& sender_position()
	{
		return memory_location[0];
	}

	std::atomic<uint64_t>& user_position()
	{
		return memory_location[1];
	}

	//use the receiver
	uint64_t get_next_value_choice()
	{
		uint64_t offset = user_position().load();
		uint64_t* raw_area = (uint64_t*)memory_location; //bypass the locks; the only atomic elements are the first two.
		uint64_t loaded_integer = raw_area[offset];
		if (offset > mmap_size) offset = ssqh_size; //the starting position.
		else ++offset;
		user_position().store(offset);
		return loaded_integer;
	}

	bool should_I_load_choice()
	{
		uint64_t offset = user_position().load();
		return (sender_position() != offset);
	}

	//allocates memory to be sent. the memory region might be split in half. thus, you must use next_offset.
	//for us, it's important to have sender and receiver agree on where the next relevant element is located.
	uint64_t allocate_memory_choice(uint64_t elements)
	{
		check(elements < mmap_size);
		uint64_t starting_position = sender_position().load();
		uint64_t user_GC_boundary = user_position().load();
		if (user_GC_boundary < starting_position)
			user_GC_boundary += mmap_size - ssqh_size; //here, we reorder things so that they're in a straight line, instead of modulo.
		if ((user_GC_boundary > starting_position) && (user_GC_boundary < starting_position + elements))
		{
			//we've run out of memory. here, the admin would kill the process for not receiving memory.
			//but the user should abort(), for stuffing too much information.
		}
		uint64_t ending_location = starting_position + elements;
		if (ending_location >= mmap_size)
		{
			ending_location -= (mmap_size - ssqh_size); //wraparound
		}
		sender_position().store(ending_location);
		return starting_position;
	}
	//performs the wraparound. get offsets one by one.
	inline uint64_t next_offset(uint64_t offset)
	{
		return (offset + 1 == mmap_size) ? ssqh_size : offset + 1;
	}
};


constexpr uint64_t msqh_size = 3; //_multiple_ sender queue header size. sender reserve, then sender size, then user size.

struct multiple_sender_queue
{
	multiple_sender_queue(void* k) : memory_location((std::atomic<uint64_t>*)k) {}
	void initialize() {
		memory_location[0].store(msqh_size);
		memory_location[1].store(msqh_size);
		memory_location[2].store(msqh_size);
	}
	std::atomic<uint64_t>* memory_location;

	//these are offsets from the base. so starting value gives 2.
	std::atomic<uint64_t>& sender_reserve_position() { return memory_location[0]; }
	std::atomic<uint64_t>& sender_done_position() { return memory_location[1]; }
	std::atomic<uint64_t>& user_position() { return memory_location[2]; }

	//use the receiver
	uint64_t get_next_value_choice()
	{
		uint64_t offset = user_position().load();
		uint64_t* raw_area = (uint64_t*)memory_location; //bypass the locks; the only atomic elements are the first two.
		uint64_t loaded_integer = raw_area[offset];
		if (offset > mmap_size) offset = msqh_size; //the starting position.
		else ++offset;
		user_position().store(offset);
		return loaded_integer;
	}

	bool should_I_load_choice()
	{
		uint64_t offset = user_position().load();
		return (sender_done_position() != offset);
	}

	//allocates memory to be sent. the memory region might be split in half. thus, you must use next_offset.
	//rationale: it's important to have sender and receiver agree on where the next relevant element is located.
	uint64_t allocate_memory_choice(uint64_t elements)
	{
		check(elements < mmap_size);
		uint64_t starting_position = sender_reserve_position().load();
		try_again: //starting position was loaded by the compxchg, so we don't need to reload.
		uint64_t user_GC_boundary = user_position().load();
		if (user_GC_boundary < starting_position)
			user_GC_boundary += mmap_size - msqh_size; //here, we reorder things so that they're in a straight line, instead of modulo.
		if ((user_GC_boundary > starting_position) && (user_GC_boundary < starting_position + elements))
		{
			//we've run out of memory. here, the admin would kill the process for not receiving memory.
			//but the user should abort(), for stuffing too much information.
		}
		uint64_t ending_location = starting_position + elements;
		if (ending_location >= mmap_size)
		{
			ending_location -= (mmap_size - msqh_size); //wraparound
		}
		if (!sender_reserve_position().compare_exchange_weak(starting_position, ending_location))
			goto try_again;
		return starting_position;
	}
	//performs the wraparound. get offsets one by one.
	inline uint64_t next_offset(uint64_t offset)
	{
		return (offset + 1 == mmap_size) ? msqh_size : offset + 1;
	}

	void commit_memory_choice(uint64_t initial_offset, uint64_t size_of_memory)
	{
		uint64_t final_offset = initial_offset + size_of_memory;
		uint64_t initial_offset_temp = initial_offset;
		if (final_offset > mmap_size) final_offset -= (mmap_size - msqh_size);
		while (!sender_done_position().compare_exchange_weak(initial_offset_temp, final_offset))
		{
			initial_offset_temp = initial_offset;
		};
	}
};
