#pragma once
#include <set>
#include "datatypes.h"

//k is probably an address.
//let's make it so. therefore, k cannot be 0.
template<class k>
struct partial_order_set
{
	mutex_lock lock;

	struct descriptor
	{
		std::set<k> incoming;
		std::set<k> outgoing;
		uint64_t number;
	};
private:
	std::map<k, descriptor> object;
};
