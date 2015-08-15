#include <fstream>
#include <cstdint>
#include <iostream>
#include <string>
#include "globalinfo.h"
#include "function.h"
#include "memory.h"
#include "runtime.h"

//being vectors lets us write them directly into a file, since memory is contiguous.
extern std::vector< function*> event_roots;
extern std::vector< Tptr > type_roots;

void make_sample_file(uint64_t id)
{
	std::ofstream sample_file(std::to_string(id), std::ios::binary);
	uint64_t integer_list[] = {46, 525336, 351523, 582357};

	char* char_interp = reinterpret_cast<char*>(integer_list);

	sample_file.write(char_interp, sizeof(integer_list));
	sample_file.close();
}

void write_to_file(uint64_t id)
{
	make_sample_file(id);
	std::ifstream sample_file(std::to_string(id), std::ios::binary);
	unsigned long long size_of_file = sample_file.tellg();
	unsigned long long no_of_integers = size_of_file >> 3; //uint64_t is 2^3 bytes
	check(8 * no_of_integers == size_of_file, "File doesn't consist of uint64_t (number of bytes not a multiple of 8)");

	char* memblock = new char[size_of_file];
	sample_file.seekg(0, std::ios::beg);
	sample_file.read(memblock, size_of_file);
	sample_file.close();

	uint64_t* int_interp = reinterpret_cast<uint64_t*>(memblock);
	for (unsigned output_incrementor = 0; output_incrementor < no_of_integers; ++output_incrementor)
	{
		std::cout << int_interp[output_incrementor] << ' ';
	}
	//std::cin.get();
}

struct file_header
{
	uint64_t version_number; //for different versions of the file format.
	uint64_t* pool;
	uint64_t pool_size;
	function* function_pool;
	uint64_t function_pool_size;
	uint64_t number_of_type_roots; //mainly to say where the vector of ASTs is.
	uint64_t number_of_event_roots;
};
extern uint64_t* big_memory_pool;
extern function* function_pool;

void serialize(uint64_t id)
{
	std::ofstream id_file(std::to_string(id), std::ios::binary);
	check(id_file.is_open(), "stream opening failed");
	file_header header;

	header.version_number = 0;
	header.pool = big_memory_pool;
	header.pool_size = pool_size;
	header.function_pool = function_pool;
	header.function_pool_size = function_pool_size;
	header.number_of_type_roots = type_roots.size();
	header.number_of_event_roots = event_roots.size();
	
	id_file.write(reinterpret_cast<char*>(&header), sizeof(header));
	id_file.write(reinterpret_cast<char*>(type_roots.data()), type_roots.size() * sizeof(uint64_t));
	id_file.write(reinterpret_cast<char*>(event_roots.data()), event_roots.size() * sizeof(uint64_t));
	id_file.write(reinterpret_cast<char*>(big_memory_pool), pool_size * sizeof(uint64_t));
	for (uint64_t x = 0; x < function_pool_size; ++x) id_file.write(reinterpret_cast<char*>(&function_pool[x].the_AST), sizeof(uint64_t)); //we copy only the ASTs.

	id_file.close();
}

uint64_t pointer_offset; //when unserializing, the difference between the new and old pointers
uint64_t function_pointer_offset;
void trace_objects();

void unserialize(uint64_t id)
{
	std::ifstream id_file(std::to_string(id), std::ios::binary);
	check(id_file.is_open() && id_file.good(), "stream opening failed");
	file_header header;
	id_file.read(reinterpret_cast<char*>(&header), sizeof(header));

	pointer_offset = big_memory_pool - header.pool;
	check (pool_size >= header.pool_size, "for now, I don't have a way to read in");
	function_pointer_offset = header.function_pool - function_pool;
	check(function_pool_size >= header.function_pool_size, "don't have a good way to read in more elements");
	type_roots.resize(header.number_of_type_roots, 0);
	event_roots.resize(header.number_of_event_roots, nullptr);

	//I just reversed all the writes to reads. amazing.
	id_file.read(reinterpret_cast<char*>(type_roots.data()), type_roots.size() * sizeof(uint64_t));
	id_file.read(reinterpret_cast<char*>(event_roots.data()), event_roots.size() * sizeof(uint64_t));
	id_file.read(reinterpret_cast<char*>(big_memory_pool), header.pool_size * sizeof(uint64_t));
	for (uint64_t x = 0; x < function_pool_size; ++x) id_file.read(reinterpret_cast<char*>(&function_pool[x].the_AST), sizeof(uint64_t));
	id_file.close();

	UNSERIALIZATION_MODE = true;
	trace_objects(); //correct pointers, populate the type hash table, mark occupied memory, etc.
	for (uint64_t x = 0; x < function_pool_size; ++x) //compile in place
		if (function_pool[x].the_AST) compile_specifying_location(function_pool[x].the_AST, &function_pool[x]);	
	
}
