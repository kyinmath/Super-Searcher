#include <fstream>
#include <iostream>
#include <string>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "globalinfo.h"



uint64_t open_file(uint64_t id)
{
	int i;
	int fd;
	int result;
	uint64_t *map;  /* mmapped array of uint64_t */
	const char* FILEPATH = std::to_string(id).c_str();

	uint64_t NUMINTS = pool_size;
	uint64_t FILESIZE(NUMINTS * sizeof(uint64_t));
	/* Open a file for writing.
	*  - Creating the file if it doesn't exist.
	*  - Truncating it to 0 size if it already exists. (not really needed)
	*
	* Note: "O_WRONLY" mode is not sufficient when mmaping.
	*/
	fd = open(FILEPATH, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
	check (fd != -1, "Error opening file for writing");
	struct file_close
	{
		int fd_;
		file_close(int f) : fd_(f) {}
		~file_close() { close(fd_); }
	} temp_destructor(fd);

	/* Stretch the file size to the size of the (mmapped) array of ints */
	result = lseek(fd, FILESIZE - 1, SEEK_SET);
	if (result == -1) {
		error("Error calling lseek() to 'stretch' the file");
	}

	/* Something needs to be written at the end of the file to
	* have the file actually have the new size.
	* Just writing an empty string at the current file position will do.
	*
	* Note:
	*  - The current position in the file is at the end of the stretched
	*    file due to the call to lseek().
	*  - An empty string is actually a single '\0' character, so a zero-byte
	*    will be written at the last byte of the file.
	*/
	result = write(fd, "", 1);
	if (result != 1) {
		error("Error writing last byte of the file");
	}

	/* Now the file is ready to be mmapped.
	*/
	map = (uint64_t*)mmap(0, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		error("Error mmapping the file");
	}

	/* Now write int's to the file as if it were memory (an array of ints).
	*/
	for (i = 1; i <= NUMINTS; ++i) {
		map[i] = 2 * i;
	}

	/* Don't forget to free the mmapped memory
	*/
	if (munmap(map, FILESIZE) == -1) {
		error("Error un-mmapping the file");
	}
	return 0;
}
#include "ASTs.h"

uint64_t* file_mempool;
uAST* file_functionpool;
//remember to set the 0 vector inside. and remember to ignore it when unserializing.
void set_up_file_header()
{
	//open file
	//return the location of the function pool, and the location of the object pool.
}

//returns an offset number.
inline uint64_t allocate_in_file(uint64_t size)
{

}

inline uint64_t allocate_function_in_file(uAST* target)
{

}