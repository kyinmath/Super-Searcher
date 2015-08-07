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

#define SHM_FILE "/apc.shm.kashyap"

void lg(const char *oper, int result) {
	std::cout << oper << result;
	if (result < 0) {
		perror(oper);
	}
}

void child(char *result) {
	for (int i = 0; i < 50; ++i) {
		strcpy(result, "child ::: hello parent\n");
		usleep(2);
		std::cout << "child ::: " <<  result;
	}
	usleep(5);
}

void parent(char *result) {
	usleep(1);
	for (int i = 0; i < 50; ++i) {
		strcpy(result, "parent ::: hello child\n");
		usleep(2);
		std::cout << "parent ::: " << result;
	}
	usleep(5);
}

int main() {
	int integerSize = 1024 * 1024 * 20; //20 mb

	int descriptor = -1;
	int mmap_flags = MAP_SHARED;

	// Open the shared memory.
	descriptor = shm_open(SHM_FILE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	// Size up the shared memory.
	ftruncate(descriptor, integerSize);

	void *mmap_result = mmap(NULL, integerSize, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
	if (mmap_result == MAP_FAILED) abort();
	uint64_t* mapped_area = (uint64_t*)mmap_result;

	std::cout << mapped_area << '\n';

	pid_t child_pid = fork();

	switch (child_pid) {
	case 0:
		child((char*)mmap_result);
		break;
	case -1:
		abort(); //failed
		break;
	default:
		parent((char*)mmap_result);
	}

	lg("msync", msync(mmap_result, integerSize, MS_SYNC));
	lg("munmap", munmap(mmap_result, integerSize));

	usleep(500000);

	if (child_pid != 0) {
		lg("shm_unlink", shm_unlink(SHM_FILE));
	}

	return 0;
}