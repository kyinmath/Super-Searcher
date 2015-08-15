#include <ctime>
#include <string>
#include <cstdio>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <cstdint>
#include <iostream>
#include <string>
#include <atomic>
#include "../src/generic_ipc.h"
#include <thread>

bool TRUEINIT = false;
bool TRUERUN = false;
bool I_AM_KING = false;
uint64_t shm_ID; //some random number
uint64_t mmap_size; //same number for every single IPC receiver
uint64_t user_number; //0-N


multiple_sender_queue citizen_receive_mem; //only used if you are a citizen. your own receiver.
std::vector<multiple_sender_queue> citizen_mem; //only used if you are the king. the country's receivers.
multiple_sender_queue king_receive_mem;
multiple_sender_queue admin_receive_mem;

//call only when it is already known that an event must be processed
template<class T> void handle_queue_event(T memory)
{
	//std::cerr << "handling queue at user " << user_number << '\n';
	switch (memory.get_next_value())
	{
	case file_ID_of_admin:
		{
			uint64_t admin_ID = memory.get_next_value();
			admin_receive_mem.memory_location = (std::atomic<uint64_t>*)(open_memory(admin_ID));
			//std::cerr << "initial pingback " << admin_ID << " at " << admin_receive_mem.memory_location << '\n';
			admin_receive_mem.write_values({existence_ping, user_number}); //pingback!
			break;
		}
	case file_ID_of_king:
		king_receive_mem.memory_location = (std::atomic<uint64_t>*)(open_memory(memory.get_next_value()));
		break;
	case file_ID_of_user:
		{
			uint64_t citizen_ID = memory.get_next_value();
			check(citizen_mem.size() == citizen_ID - 1, "failed to write in order");
			citizen_mem.push_back(open_memory(memory.get_next_value()));
			break;
		}
	case aliveness_check:
		{
			admin_receive_mem.write_values({existence_ping, user_number}); //pingback!
			break;
		}
	default:
		error("unknown event enum");
		break;
	}
}

int main(int argc, char* argv[])
{
	std::cout << "I'm the exec'd child!";
	std::cout << "arguments are ";
	for (int x = 0; x < argc; ++x)
		std::cout << argv[x] << ' ';
	for (int x = 1; x < argc; ++x)
	{
		if (strcmp(argv[x], "trueinit") == 0) TRUEINIT = true;
		else if (strcmp(argv[x], "truerun") == 0 || strcmp(argv[x], "trueking") == 0)
		{
			if (strcmp(argv[x], "trueking") == 0)
				I_AM_KING = true;
			{
				bool isNumber = true;
				std::string next_token = argv[++x];
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, std::string("tried to input non-number ") + next_token);
				check(next_token.size(), "no digits in the number");
				user_number = std::stoull(next_token);
			}
			{
				bool isNumber = true;
				std::string next_token = argv[++x];
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, std::string("tried to input non-number ") + next_token);
				check(next_token.size(), "no digits in the number");
				shm_ID = std::stoull(next_token);
			}
			{
				bool isNumber = true;
				std::string next_token = argv[++x];
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, std::string("tried to input non-number ") + next_token);
				check(next_token.size(), "no digits in the number");
				mmap_size = std::stoull(next_token);
			}
		}
	}

	if (I_AM_KING)
	{
		king_receive_mem.memory_location = (std::atomic<uint64_t>*)open_memory(shm_ID);
		
		while (1)
		{
			while (king_receive_mem.should_I_load()) handle_queue_event(king_receive_mem);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	else
	{
		citizen_receive_mem.memory_location = (std::atomic<uint64_t>*)open_memory(shm_ID);
		while (1)
		{
			while (citizen_receive_mem.should_I_load()) handle_queue_event(citizen_receive_mem);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

}