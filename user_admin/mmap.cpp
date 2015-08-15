#include <chrono>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <cstdint>
#include <iostream>
#include <atomic>
#include <random>
#include <string>
#include <thread>
#include "../src/generic_ipc.h"

uint64_t mmap_size = 1024 * 10; //10 kB

multiple_sender_queue admin_receiver;
std::vector<std::chrono::steady_clock::time_point> time_of_last_ping;
std::vector<bool> pinged_already;
std::vector<pid_t> user_PIDs;

//call only when it is already known that an event must be processed
void admin_handle_event()
{
	switch (admin_receiver.get_next_value())
	{
	case existence_ping:
		{
			uint64_t citizen_ID = admin_receiver.get_next_value();
			time_of_last_ping.at(citizen_ID) = std::chrono::steady_clock::now(); //.at() performs a bounds check since citizen_ID may not be right.
			std::cerr << "ping by " << citizen_ID << '\n';
			pinged_already[citizen_ID] = false;
			break;
		}
	default:
		error("unknown event enum");
		break;
	}
}

int main() {

	std::random_device random;

	uint64_t number_of_users = std::thread::hardware_concurrency();
	number_of_users = 5; //todo: remove this
	check(number_of_users > 0, "hardware_concurrency() returned 0");

	std::vector<uint64_t> id_list;
	std::vector<void*> mmap_list;
	unsigned number_of_mmaps = number_of_users + 1;
	for (unsigned x = number_of_mmaps; x; --x) //make the king last
	{
		id_list.push_back(random() + ((uint64_t)random() << 32));
	}

	for (unsigned x = 0; x < number_of_mmaps; ++x) //the last one is for the admin to communicate with the king.
	{
		mmap_list.push_back(allocate_shm(id_list[x]));
	}

	for (unsigned x = 0; x < number_of_mmaps; ++x)
	{
		if (x < number_of_users && x != 0) //it's a user
		{
			multiple_sender_queue user_receiver(mmap_list[x]);
			user_receiver.initialize();

			user_receiver.write_values({events::file_ID_of_king, id_list[0]});
			user_receiver.write_values({events::file_ID_of_admin, id_list[number_of_users]});
		}
		else if (x == 0) //it's the king
		{
			multiple_sender_queue user_receiver(mmap_list[x]);
			user_receiver.initialize();
			
			user_receiver.write_values({events::file_ID_of_king, id_list[0]});
			for (unsigned x = 1; x < number_of_users; ++x)
				user_receiver.write_values({events::file_ID_of_user, x, id_list[x]});
			user_receiver.write_values({events::file_ID_of_admin, id_list[number_of_users]});
		}
		else //it's the admin
		{
			admin_receiver.memory_location = (std::atomic<uint64_t>*)(mmap_list[x]);
			admin_receiver.initialize();
		}
	}
	for (uint64_t x = 0; x < number_of_users; ++x)
	{
		time_of_last_ping.push_back(std::chrono::steady_clock::now());
		pinged_already.push_back(false);
	}

	for (unsigned x = 0; x < number_of_users; ++x)
	{
		pid_t child_pid = fork();

		switch (child_pid) {
		case 0:
			//endl necessary to flush the stream before the process gets exec killed
			//I'm the child!
			execl("slave", "slave", (x == 0) ? "trueking" : "truerun", std::to_string(x).c_str(), std::to_string(id_list[x]).c_str(), std::to_string(mmap_size).c_str(), nullptr);
			//last nullptr is required to terminate a list of arguments, because of Linux variadic nonsense. first "slave" argument is to make the argc work; otherwise, the list of argv[] starts at 0 instead of 1, which is inconsistent with when we run the program from console
			//arguments: ID (0-N), mmap place, mmap size.
			break;
		case -1:
			abort(); //fail -- super rare
			break;
		default:
			user_PIDs.push_back(child_pid);
			break;
			//I'm the parent. now I know what the child's PID is.
		}
	}


	while (1)
	{
		while (admin_receiver.should_I_load()) admin_handle_event();
		for (uint64_t x = 0; x < number_of_users; ++x)
		{
			using namespace std::chrono;
			std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
			duration<double> time_span = duration_cast<duration<double>>(current_time - time_of_last_ping[x]);
			std::cerr << "time difference user " << x << " c " << time_span.count() << '\n';
			if (time_span.count() > 1) //more than one second elapsed since last aliveness ping
			{
				if (time_span.count() > 3) //more than three seconds elapsed!
				{
					error("need to kill unresponsive guy");
				}
				else if (!pinged_already[x])
				{
					pinged_already[x] = true;
					multiple_sender_queue queue(mmap_list[x]);
					queue.write_values(aliveness_check);
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}

	for (uint64_t x = 0; x < mmap_list.size(); ++x)
	{
		if (munmap(mmap_list[x], mmap_size) == -1) error("munmap failure");
		if (shm_unlink((std::string("/") + std::to_string(id_list[x])).c_str()) == -1) error("shm_unlink failure");
	}


	return 0;
}