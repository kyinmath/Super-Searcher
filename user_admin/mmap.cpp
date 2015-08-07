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



int main() {

	std::random_device random;
	uint64_t id = random();

	const char* file_ID = (std::string("/") + std::to_string(id)).c_str();

	std::cerr << "file ID in parent is " << file_ID << '\n';

	int integerSize = 1024 * 1; //1 kB

	int descriptor = -1;
	int mmap_flags = MAP_SHARED;

	// Open the shared memory.
	descriptor = shm_open(file_ID, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	// Size up the shared memory. it'll be 0's.
	ftruncate(descriptor, integerSize);

	void *mmap_result = mmap(NULL, integerSize, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
	if (mmap_result == MAP_FAILED) abort();
	std::atomic<uint64_t>* mapped_area = (std::atomic<uint64_t>*)mmap_result;
	for (uint64_t x = 0; x < 40; ++x)
		mapped_area[x + 1].store(x);
	mapped_area[0].store(40);

	

	pid_t child_pid = fork();

	switch (child_pid) {
	case 0:
		std::cerr << "I'm the child!"; //endl necessary to flush the stream before the process gets exec killed
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
		std::cerr << "I'm the parent!"; //cerr necessary to flush, so that the stream doesn't go afk while we sleep(4)
		sleep(1);
		if (shm_unlink(file_ID)) abort();

		for (uint64_t x = 40; x < 80; ++x)
			mapped_area[x + 1].store(x);
		mapped_area[0].store(80);
	}


	if (munmap(mmap_result, integerSize) == -1) abort();


	return 0;
}