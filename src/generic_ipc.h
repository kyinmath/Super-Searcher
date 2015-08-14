#include "globalinfo.h" //for check()
#include <llvm/ADT/ArrayRef.h>
#include <atomic>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */

//sender position, then user read position, then memory start.
//each user has one receiver and one sender.
//king and admin have multi-sender queues. individuals have single-sender queues.
//we assume that users cannot crash in the middle of writing to a queue. that's very optimistic. what if the admin decides to send a kill-message?
extern uint64_t mmap_size;
constexpr uint64_t ssqh_size = 2; //single sender queue header size

enum events
{
	file_ID_of_king, //address of shared memory of king
	file_ID_of_user, //address of shared memory of user N
	file_ID_of_admin, //address of shared memory of admin
	existence_ping, //from a user to the admin, telling the admin that the user still exists.
	aliveness_check, //from admin to user. requesting a ping.
};

struct single_sender_queue
{
	single_sender_queue() : memory_location((std::atomic<uint64_t>*)53535) {}
	single_sender_queue(void* k) : memory_location((std::atomic<uint64_t>*)k) {}
	void initialize() {
		memory_location[0].store(ssqh_size);
		memory_location[1].store(ssqh_size);
	}
	std::atomic<uint64_t>* memory_location;

	//these are offsets from the base. so starting value gives 2.
	std::atomic<uint64_t>& sender_position()
	{ return memory_location[0]; }

	std::atomic<uint64_t>& user_position()
	{ return memory_location[1]; }

	//use the receiver
	uint64_t get_next_value()
	{
		uint64_t offset = user_position().load();
		uint64_t* raw_area = (uint64_t*)memory_location; //bypass the locks; the only atomic elements are the first two.
		uint64_t loaded_integer = raw_area[offset];
		if (offset > mmap_size) offset = ssqh_size; //the starting position.
		else ++offset;
		user_position().store(offset);
		return loaded_integer;
	}

	bool should_I_load()
	{
		uint64_t offset = user_position().load();
		return (sender_position() != offset);
	}

	//allocates memory to be sent. the memory region might be split in half. thus, you must use next_offset.
	//for us, it's important to have sender and receiver agree on where the next relevant element is located.
	//this is called AFTER you are done writing.
	void commit_memory(uint64_t elements)
	{
		check(elements < mmap_size - ssqh_size - 20, "trying to commit overlarge section"); //this is a really bad check. if it is called, then the message queue is left in a bad state.
		uint64_t starting_position = sender_position().load();
		uint64_t ending_location = starting_position + elements;
		if (ending_location >= mmap_size)
		{
			ending_location -= (mmap_size - ssqh_size); //wraparound
		}
		sender_position().store(ending_location);
	}
	//performs the wraparound. get offsets one by one.
	inline uint64_t next_offset(uint64_t offset)
	{
		return (offset + 1 == mmap_size) ? ssqh_size : offset + 1;
	}

	void write_values(llvm::ArrayRef<uint64_t> values)
	{
		check(values.size() > 0, "writing no values");
		check(values.size() < mmap_size - ssqh_size - 20, "trying to commit overlarge section");


		uint64_t starting_position = sender_position().load();
		uint64_t user_GC_boundary = user_position().load();
		if (user_GC_boundary < starting_position)
			user_GC_boundary += mmap_size - ssqh_size; //here, we reorder things so that they're in a straight line, instead of modulo.
		if ((user_GC_boundary > starting_position) && (user_GC_boundary < starting_position + values.size()))
		{
			//todo.
			//we've run out of memory. here, the admin would kill the process for not receiving memory.
			//but the user should abort(), for stuffing too much information.
		}

		uint64_t new_mem_offset = (uint64_t&)sender_position();
		*(uint64_t*)(&memory_location[new_mem_offset]) = values[0];
		for (uint64_t x = 1; x < values.size(); ++x)
		{
			new_mem_offset = next_offset(new_mem_offset);
			*(uint64_t*)(&memory_location[new_mem_offset]) = values[x];
		}
		commit_memory(values.size());
	}
};


constexpr uint64_t msqh_size = 3; //_multiple_ sender queue header size. sender reserve, then sender size, then user size.

struct multiple_sender_queue
{
	multiple_sender_queue() : memory_location((std::atomic<uint64_t>*)53535) {}
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
	uint64_t get_next_value()
	{
		uint64_t offset = user_position().load();
		uint64_t* raw_area = (uint64_t*)memory_location; //bypass the locks; the only atomic elements are the first two.
		uint64_t loaded_integer = raw_area[offset];
		if (offset > mmap_size) offset = msqh_size; //the starting position.
		else ++offset;
		user_position().store(offset);
		return loaded_integer;
	}

	bool should_I_load()
	{
		uint64_t offset = user_position().load();
		return (sender_done_position() != offset);
	}

	//allocates memory to be sent. the memory region might be split in half. thus, you must use next_offset.
	//rationale: it's important to have sender and receiver agree on where the next relevant element is located.
	uint64_t allocate_memory(uint64_t elements)
	{
		check(elements < mmap_size - msqh_size - 20, "allocating excessive memory");
		uint64_t starting_position = sender_reserve_position().load();
		try_again: //starting position was loaded by the compxchg, so we don't need to reload.
		uint64_t user_GC_boundary = user_position().load();
		if (user_GC_boundary < starting_position)
			user_GC_boundary += mmap_size - msqh_size; //here, we reorder things so that they're in a straight line, instead of modulo.
		if ((user_GC_boundary > starting_position) && (user_GC_boundary < starting_position + elements))
		{
			//todo. we've run out of memory. here, the admin would kill the process for not receiving memory.
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

	void commit_memory(uint64_t initial_offset, uint64_t size_of_memory)
	{
		uint64_t final_offset = initial_offset + size_of_memory;
		uint64_t initial_offset_temp = initial_offset;
		if (final_offset > mmap_size) final_offset -= (mmap_size - msqh_size);
		while (!sender_done_position().compare_exchange_weak(initial_offset_temp, final_offset))
		{
			initial_offset_temp = initial_offset;
		};
	}


	void write_values(llvm::ArrayRef<uint64_t> values)
	{
		check(values.size() > 0, "writing no values");
		check(values.size() < mmap_size - msqh_size - 20, "allocating excessive memory");


		uint64_t new_mem_offset = allocate_memory(values.size()); //I don't think this actually needs to be atomic
		uint64_t original_mem_offset = new_mem_offset;
		*(uint64_t*)(&memory_location[new_mem_offset]) = values[0];
		for (uint64_t x = 1; x < values.size(); ++x)
		{
			new_mem_offset = next_offset(new_mem_offset);
			*(uint64_t*)(&memory_location[new_mem_offset]) = values[x];
		}

		commit_memory(original_mem_offset, values.size());
	}
};

//set mmap size first
void* allocate_shm(uint64_t id)
{
	//std::cerr << "file ID in parent is " << file_ID << '\n';
	int descriptor = -1;
	int mmap_flags = MAP_SHARED;
	const char* file_ID = (std::string("/") + std::to_string(id)).c_str();

	//open the shared memory.
	descriptor = shm_open(file_ID, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	check(descriptor != -1, "descriptor fail");

	//size up the shared memory. it'll be 0's.
	ftruncate(descriptor, mmap_size);

	void *mmap_result = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
	check(mmap_result != MAP_FAILED, "map failed");
	return mmap_result;
}

//set mmap size first
void* open_memory(uint64_t id)
{
	int descriptor = -1;
	int mmap_flags = MAP_SHARED;
	const char* file_ID = (std::string("/") + std::to_string(id)).c_str();
	descriptor = shm_open(file_ID, O_RDWR, S_IRUSR | S_IWUSR);
	check(descriptor != -1, "descriptor fail");

	void *mmap_result = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
	check(mmap_result != MAP_FAILED, "map failed");
	return mmap_result;
}