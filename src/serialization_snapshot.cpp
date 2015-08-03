#pragma once
#include <fstream>
#include <iostream>

void make_sample_file(uint64_t id)
{
	std::ofstream sample_file(to_string(id), std::ios::binary);
	uint64_t integer_list[] = {46, 525336, 351523, 582357};

	char* char_interp = reinterpret_cast<char*>(integer_list);

	sample_file.write(char_interp, sizeof(integer_list));
	sample_file.close();
}

int main()
{
	make_sample_file(400);
	std::ifstream sample_file(to_string(400), std::ios::binary | std::ios::ate);
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


void serialize(uint64_t id)
{
	std::ofstream sample_file(to_string(id), std::ios::binary);
	
	uint64_t header[] = {123, 345, 666666, 949494}; //we need a header to get past Typen("never reached")

	char* char_interp = reinterpret_cast<char*>(integer_list);

	sample_file.write(char_interp, sizeof(integer_list));
	sample_file.close();
}

//returns an offset number.
inline uint64_t allocate_in_file(uint64_t size)
{

}