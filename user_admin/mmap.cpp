#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <cstdint>
#include <iostream>
#include <atomic>
#include <random>
#include <string>
#include <thread>
#include "../src/generic_ipc.h"

constexpr uint64_t header_size = 2;

int main() {

	std::random_device random;
	int integerSize = 1024 * 10; //10 kB

	int number_of_users = std::thread::hardware_concurrency();
	assert(number_of_users > 0, "hardware_concurrency() returned 0");

	std::vector<uint64_t> id_list;
	for (unsigned x = number_of_users; x; --x) //make the king last
	{
		id_list.push_back(random() + random() << 32);
	}

	for (unsigned x = 0; x < number_of_users; ++x) //make the king last
	{
		const char* file_ID = (std::string("/") + std::to_string(id_list[x])).c_str();

		std::cerr << "file ID in parent is " << file_ID << '\n';


		int descriptor = -1;
		int rdescriptor = -1;
		int mmap_flags = MAP_SHARED;


		// Open the shared memory.
		descriptor = shm_open(file_ID, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

		// Size up the shared memory. it'll be 0's.
		ftruncate(descriptor, integerSize);
		ftruncate(rdescriptor, integerSize);

		void *mmap_result = mmap(NULL, integerSize, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
		if (mmap_result == MAP_FAILED) abort();

		if (x)
		{
			single_sender_queue user_receiver(mmap_result);
			user_receiver.initialize();
		}
		else
		{
			multiple_sender_queue user_receiver(mmap_result);
			user_receiver.initialize();
		}


		//todo: this is wrong.
		for (uint64_t x = 0; x < 40; ++x)
			mapped_area[x + 1].store(x);
		mapped_area[0].store(40);



		pid_t child_pid = fork();

		switch (child_pid) {
		case 0:
			//std::cerr << "I'm the child!"; //endl necessary to flush the stream before the process gets exec killed
			//I'm the child!
			execl("slave", "slave", "truerun", std::to_string(id).c_str(), std::to_string(integerSize).c_str(), nullptr); //last nullptr is required to terminate a list of arguments, because of Linux variadic nonsense. first "slave" argument is to make the argc work; otherwise, the list of argv[] starts at 0 instead of 1, which is inconsistent with when we run the program from console
			//arguments: mmap place, mmap size.
			break;
		case -1:
			abort(); //fail -- super rare
			break;
		default:
			//I'm the parent. now I know what the child's PID is.
			//let the Kernel GC this object once no process sees it anymore (the mapped file will still stay alive for now)
			//std::cerr << "I'm the parent!"; //cerr necessary to flush, so that the stream doesn't go afk while we sleep()
			sleep(1);
			if (shm_unlink(file_ID)) abort();

			for (uint64_t x = 40; x < 80; ++x)
				mapped_area[x + 1].store(x);
			mapped_area[0].store(80);
		}
	}


	if (munmap(mmap_result, integerSize) == -1) abort();


	return 0;
}