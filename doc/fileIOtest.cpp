#include <fstream>
#include <iostream>

void make_sample_file()
{
	std::ofstream sample_file("awesomeness", std::ios::binary);

	std::cout << sizeof(unsigned) << '\n';
	unsigned integer_list[] = {46, 525336, 351523, 582357};

	char* char_interp = reinterpret_cast<char*>(integer_list);

	sample_file.write(char_interp, sizeof(integer_list));
	sample_file.close();
}

int main()
{
	make_sample_file();
	std::ifstream sample_file("awesomeness", std::ios::binary | std::ios::ate);
	unsigned long long size_of_file = sample_file.tellg();
	unsigned long long no_of_integers = size_of_file >> 2;
	if (4 * no_of_integers != size_of_file)
	{
		std::cerr << "File doesn't consist of integers (number of bytes not a multiple of 4).\n";
		return 0;
	}
	char* memblock = new char[size_of_file];
	sample_file.seekg(0, std::ios::beg);
	sample_file.read(memblock, size_of_file);
	sample_file.close();

	unsigned* int_interp = reinterpret_cast<unsigned*>(memblock);
	for (unsigned output_incrementor = 0; output_incrementor < no_of_integers; ++output_incrementor)
	{
		std::cout << int_interp[output_incrementor] << ' ';
	}
	//std::cin.get();
}