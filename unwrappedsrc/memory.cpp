#include <array>
#include <map>
#include <bitset>
#include <memory>
#include <stack>
#include "console.h"
#include "types.h"
#include "user_facing_functions.h"
#include "function.h"

//todo: replace all new()

//todo: actually figure out memory size according to the computer specs
//problem: the constexpr objects are outside of our memory pool. they're quite exceptional, since they can't be GC'd. do we really need to have special cases, just for them?
//maybe not. in the future, we'll just make sure to wrap them in a unique() function, so that the user only ever sees GC-handled objects.

bool BENCHMARK_GC = true;
bool VERBOSE_GC = true;

constexpr const uint64_t pool_size = 1000000ull;
constexpr const uint64_t function_pool_size = 2000ull * 64;
uint64_t big_memory_pool[pool_size];

function function_pool[function_pool_size]; //some of the items in here can be casted to doubly_linked_lists.
uint64_t function_pool_flags[function_pool_size / 64] = {0}; //each bit is marked 0 if free, 1 if occupied. the {0} is necessary by https://stackoverflow.com/questions/629017/how-does-array100-0-set-the-entire-array-to-0#comment441685_629023
uint64_t* sweep_function_pool_flags; //we need this to be able to run finalizers on the functions.

//todo: a lock on these things. maybe using Lo<>
//first value = size of slot. second value = address.
std::multimap<uint64_t, uint64_t*> free_memory = {{pool_size, big_memory_pool}};
std::map<uint64_t*, uint64_t> living_objects;
uint64_t first_possible_empty_function_block;
std::stack < std::pair<uint64_t*, Type*>> to_be_marked;

void marky_mark(uint64_t* memory, Type* t);
void initialize_roots();
void sweepy_sweep();

constexpr uint64_t header_size = 1;

uint64_t* allocate(uint64_t size)
{
	uint64_t true_size = size + header_size;
	auto k = free_memory.upper_bound(true_size);
	if (k == free_memory.end())
	{
		error("OOM");
		//we can't reallocate, since we have no way to figure out the pointers on the stack.
		//you must have fucked up to fill up everything.
		//that means we can either allocate another pool temporarily, or we can just give up. for now, we give up completely.
	}
	uint64_t found_size = k->first; //we have to do this, because we'll be deleting k.
	uint64_t* found_place = k->second;
	if (found_size == true_size) //lucky! got the exact size we needed
		free_memory.erase(k); //block is no longer free
	else	if (found_size >= true_size) //it's a bit too big
	{
		if (found_size < size + 2 * header_size + 1) //if you get the element, there'll be a dead space that's too small for any allocation
		{
			k = free_memory.upper_bound(size + 2 * header_size + 1); //find a better spot
			if (k == free_memory.end()) error("OOM");
			found_size = k->first;
			found_place = k->second;
		}
		//reduce the block size
		free_memory.erase(k);
		free_memory.insert(std::make_pair(found_size - true_size, found_place + true_size));
	}
	else error("what damn kind of allocated object did I just find");

	*found_place = 0; //mark and sweep allocator, so just mark it 0. we never use this though.
	return found_place + header_size;
}

function* allocate_function()
{
	//future: lock
	for (uint64_t x = first_possible_empty_function_block; x < function_pool_size / 64; ++x)
	{
		uint64_t mask = function_pool_flags[x];
		if (mask != -1ull)
		{
			int offset = __builtin_ctzll(mask);
			uint64_t bit_mask = 1 << offset;
			function_pool_flags[x] |= bit_mask;
			if (VERBOSE_DEBUG) console << "returning allocation " << &function_pool[x * 64 + mask] << " with number " << x * 64 + mask << '\n';
			return &function_pool[x * 64 + mask];
		}
	}
	error("OOM function");
}

void start_GC()
{
	if (BENCHMARK_GC)
	{
		uint64_t total_memory_use = 0;
		for (auto& x : free_memory)
		{
			console << "memory slot " << x.second << " size " << x.first << '\n';
			total_memory_use += x.first;
		}
		console << "total free before GC " << total_memory_use << '\n';
	}
	sweep_function_pool_flags = new uint64_t[function_pool_size / 64]();
	living_objects.clear();
	initialize_roots();
	while (!to_be_marked.empty())
	{
		auto k = to_be_marked.top();
		if (VERBOSE_GC)
		{
			console << "GC marking " << k.first << " ";
			output_type(k.second);
			console << "as a type pointer, it represents "; output_type((Type*)(k.first));
			console << "the size of the marking stack before marking is " << to_be_marked.size() << '\n';
		}
		marky_mark(k.first, k.second);
		to_be_marked.pop();
	}
	if (VERBOSE_GC)
	{
		for (auto& x : living_objects)
		{
			console << "living object " << x.first << " size " << x.second << '\n';
		}
	}

	sweepy_sweep();
	delete[] sweep_function_pool_flags;

	if (BENCHMARK_GC)
	{
		uint64_t total_memory_use = 0;
		for (auto& x : free_memory)
		{
			console << "memory slot " << x.second << " size " << x.first << '\n';
			total_memory_use += x.first;
		}
		console << "total free after GC " << total_memory_use << '\n';
	}
}

extern type_htable_t type_hash_table; //a hash table of all the unique types. don't touch this unless you're the memory allocation
void initialize_roots()
{
	type_hash_table.clear();
	type_hash_table.insert(u::does_not_return);
	type_hash_table.insert(u::integer);
	type_hash_table.insert(u::cheap_dynamic_pointer);
	type_hash_table.insert(u::full_dynamic_pointer);
	type_hash_table.insert(u::type);
	type_hash_table.insert(u::AST_pointer);
	type_hash_table.insert(u::function_pointer);
	to_be_marked.push(std::make_pair((uint64_t*)u::does_not_return, get_Type_full_type(u::does_not_return)));
	to_be_marked.push(std::make_pair((uint64_t*)u::integer, get_Type_full_type(u::integer)));
	to_be_marked.push(std::make_pair((uint64_t*)u::cheap_dynamic_pointer, get_Type_full_type(u::cheap_dynamic_pointer)));
	to_be_marked.push(std::make_pair((uint64_t*)u::full_dynamic_pointer, get_Type_full_type(u::full_dynamic_pointer)));
	to_be_marked.push(std::make_pair((uint64_t*)u::type, get_Type_full_type(u::type)));
	to_be_marked.push(std::make_pair((uint64_t*)u::AST_pointer, get_Type_full_type(u::AST_pointer)));
	to_be_marked.push(std::make_pair((uint64_t*)u::function_pointer, get_Type_full_type(u::function_pointer)));


	//add in the thread ASTs
}

void mark_single_element(uint64_t* memory, Type* t);
void marky_mark(uint64_t* memory, Type* t)
{
	if (t == nullptr) return; //handle a null type. this might appear from dynamic objects. although later we might require dynamic objects to have non-null type?
	if (living_objects.find(memory) != living_objects.end()) return; //it's already there. nothing needs to be done, if we assume that full pointers point to the entire object. todo.
	living_objects.insert({memory, get_size(t)});
	if (t->tag == Typen("con_vec"))
	{
		uint64_t* current_pointer = memory;
		for (auto& t : Type_pointer_range(t))
		{
			mark_single_element(current_pointer, t);
			current_pointer += get_size(t);
		}
	}
	else mark_single_element(memory, t);
}

void mark_single_element(uint64_t* memory, Type* t)
{
	check(memory != nullptr, "passed 0 memory pointer to mark_single");
	check(t != nullptr, "passed 0 type pointer to mark_single");
	switch (t->tag)
	{
	case Typen("con_vec"):
		error("no nested con_vecs allowed");
	case Typen("integer"):
		break;
	case Typen("pointer"):
		to_be_marked.push({*(uint64_t**)memory, t->fields[0].ptr});
		break;
	case Typen("dynamic pointer"):
		if (*memory != 0)
		{
			if (living_objects.find(*(uint64_t**)memory) != living_objects.end()) break;
			to_be_marked.push({*(uint64_t**)memory, (Type*)(memory + 1)}); //pushing the object
			to_be_marked.push({*(uint64_t**)(memory + 1), get_Type_full_type(*(Type**)(memory + 1))}); //pushing the type
		}
		break;
	case Typen("AST pointer"):
		if (*memory != 0)
		{
			uAST* the_AST = *(uAST**)memory;
			Type* type_of_AST = get_AST_full_type(the_AST->tag);
			to_be_marked.push({*(uint64_t**)memory, type_of_AST});
		}
		break;
	case Typen("type pointer"):
		if (*memory != 0)
		{
			Type* the_type = *(Type**)memory;
			get_unique_type(the_type, true); //put it in the type hash table.
			Type* type_of_type = get_Type_full_type(the_type);
			to_be_marked.push({*(uint64_t**)memory, type_of_type});
		}
		break;
	case Typen("function pointer"):
		uint64_t number = (function*)memory - function_pool;
		sweep_function_pool_flags[number / 64] |= (1 << number % 64);
		mark_single_element(*(uint64_t**)memory, u::AST_pointer); //mark_single is good. don't use marky_mark, because that will attempt to add the function to the existing-items list
		mark_single_element(*(uint64_t**)(memory+1), u::type);
		//future: maybe the parameter
		break;
	}
}

void sweepy_sweep()
{
	free_memory.clear(); //we're constructing the free memory set all over again.
	uint64_t* memory_incrementor = big_memory_pool; //we're trying to find the first bit of memory that is free

	while (1)
	{
		auto next_memory = living_objects.upper_bound(memory_incrementor);
		if (next_memory == living_objects.end())
			break;
		uint64_t* ending_location = next_memory->first - header_size;
		if (VERBOSE_GC) console << "memory available from " << memory_incrementor << " to " << ending_location << '\n';
		if (ending_location != memory_incrementor)
			free_memory.insert({ending_location - memory_incrementor, memory_incrementor});
		memory_incrementor = next_memory->first + next_memory->second;
	}
	if (memory_incrementor != &big_memory_pool[pool_size]) //the final bit of memory at the end
		free_memory.insert({&big_memory_pool[pool_size] - memory_incrementor, memory_incrementor});
	living_objects.clear();

	for (uint64_t x = 0; x < function_pool_size / 64; ++x)
	{
		uint64_t diffmask = function_pool_flags[x] - sweep_function_pool_flags[x];
		if (diffmask)
		{
			console << "diffmask " << diffmask << '\n';
			for (unsigned offset = 0; offset < 64; ++offset)
				if (diffmask & (1ull << offset))
				{
					console << "offset is " << offset << '\n';
					//function_pool[x * 64 + offset].result_module
					function_pool[x * 64 + offset].erase(); //we're literally 1 away from the correct function. why are we offset by one?
				}
			function_pool_flags[x] = sweep_function_pool_flags[x];
		}
	}
	first_possible_empty_function_block = 0;
}

//future: test suite for GC.
//make sure that functions go away when necessary, and don't go away when not necessary.
//test that some objects are collected, especially ones in loops
//test that some objects are not collected, especially ones nested deeply