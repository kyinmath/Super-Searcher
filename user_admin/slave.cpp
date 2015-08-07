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
#include <string>
#include <atomic>

bool TRUEINIT = false;
bool TRUERUN = false;
uint64_t user_id;
uint64_t mmap_size;

using std::string;

[[noreturn]] inline void error(const string& Str) { std::cerr << "Error: " << Str << '\n'; abort(); }
inline void check(bool condition, const string& Str = "") { if (!condition) error(Str); }


int main(int argc, char* argv[])
{
	std::cout << "I'm the exec'd child!";
	std::cout << "arguments are ";
	for (int x = 0; x < argc; ++x)
		std::cout << argv[x];
	const char* file_ID;
	for (int x = 1; x < argc; ++x)
	{
		if (strcmp(argv[x], "trueinit") == 0) TRUEINIT = true;
		else if (strcmp(argv[x], "truerun") == 0) 
		{
			{
				bool isNumber = true;
				std::string next_token = argv[++x];
				for (auto& k : next_token)
					isNumber = isNumber && isdigit(k);
				check(isNumber, std::string("tried to input non-number ") + next_token);
				check(next_token.size(), "no digits in the number");
				user_id = std::stoull(next_token);
				file_ID = (std::string("/") + std::to_string(user_id)).c_str();
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

	int descriptor = -1;
	int mmap_flags = MAP_SHARED;
	std::cerr << "file ID in slave is " << file_ID << '\n';
	// Open the shared memory.
	descriptor = shm_open(file_ID, O_RDWR, S_IRUSR | S_IWUSR);
	perror("descriptor");
	check(descriptor != -1, "descriptor fail");

	void *mmap_result = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ, mmap_flags, descriptor, 0);
	check (mmap_result != MAP_FAILED, "map failed");
	std::atomic<uint64_t>* mapped_area = (std::atomic<uint64_t>*)mmap_result;

	for (uint64_t x = 1; x < 81;)
	{
		if (mapped_area[0].load() > x - 1)
		{
			std::cerr << "int " << mapped_area[x + 1].load();
			++x;
		}
	}
	std::cerr << "last size was " << mapped_area[0].load();
	return 0;
}