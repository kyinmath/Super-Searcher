#include "vector.h"
#include "runtime.h"

std::vector<uint64_t> numbers;
uint64_t number_of_randoms = 5; //these introduce noise
uint64_t number_of_hiddens = 10; //these persist state between rounds
uint64_t number_of_visibles = 10; //these output information.
constexpr uint64_t starting_value = 1024 * 256; //2^16
svector* initialize(uint64_t seed)
{
	//if we initialize anything with the starting value, we're going to be screwed, because they'll get a lot of information from the very first read.
	for (uint64_t x = 0; x < number_of_randoms; ++x) numbers.push_back(generate_random() >> 48);
	for (uint64_t x = 0; x < number_of_hiddens; ++x) numbers.push_back(generate_random() >> 48);
	for (uint64_t x = 0; x < number_of_visibles; ++x) numbers.push_back(generate_random() >> 48);

	/*
	possible operations:

	1. add/subtract
	2. swap
	3. multiply, then divide by by starting_value / 2.
	4. branch on comparison, lteq.
	5. increment/decrement
	6. multiply by starting_value / 2, then divide.

	note: we want to avoid letting the user infer huge signals about operations by just estimating magnitude. that's why we divide by starting_value after multiplying.

	*/
}