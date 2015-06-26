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

constexpr const uint64_t pool_size = 1000000ull;
uint64_t big_memory_pool[pool_size];
function function_pool[10000ull];
//std::bitset<AST_number> AST_pool_flags; //each bit refers to a AST, which is made up of multiple slots. it's marked 1 if it's occupied

//todo: a lock on this. maybe using Lo<>
//first value = size of slot. second value = address.
std::multimap<uint64_t, uint64_t*> free_memory = {{pool_size, big_memory_pool}};
std::map<uint64_t*, uint64_t> living_objects;
std::unordered_set<uint64_t*> living_functions;
std::stack < std::pair<uint64_t*, Type*>> to_be_marked;

void marky_mark(uint64_t* memory, Type* t);
void initialize_roots();
void sweepy_sweep();

uint64_t* allocate(uint64_t size)
{
	uint64_t true_size = size + 1; //including the header
	auto k = free_memory.upper_bound(true_size);
	uint64_t found_size = k->first; //we have to do this, because we'll be deleting k.
	uint64_t* found_place = k->second;
	if (k == free_memory.end())
	{
		error("OOM");
		//we can't reallocate, since we have no way to figure out the pointers on the stack.
		//you must have fucked up to fill up everything.
		//that means we can either allocate another pool temporarily, or we can just give up. for now, we give up completely.
	}
	if (k->first == true_size) //lucky! got the exact size we needed
		free_memory.erase(k); //block is no longer free
	else	if (k->first >= true_size) //it's a bit too big
	{
		//reduce the block size
		free_memory.erase(k);
		free_memory.insert(std::make_pair(found_size - true_size, found_place + true_size));
	}
	else error("what damn kind of allocated object did I just find");

	*found_place = 0; //mark and sweep allocator, so just mark it 0. we never use this though.
	return found_place + 1;
}

uint64_t* allocate_function(uint64_t size)
{
	//future: implement
	return nullptr;
}

void start_GC()
{
	initialize_roots();
	while (!to_be_marked.empty())
	{
		auto k = to_be_marked.top();
		marky_mark(k.first, k.second);
	}

	sweepy_sweep();
}
void initialize_roots()
{
	//add in the special types which we need to know about
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
			living_objects.insert({*(uint64_t**)memory, 2});
			to_be_marked.push({*(uint64_t**)memory, *(Type**)(memory + 1)}); //pushing the object
			to_be_marked.push({*(uint64_t**)(memory + 1), T::type}); //pushing the type
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
			Type* type_of_type = get_Type_full_type(the_type);
			to_be_marked.push({*(uint64_t**)memory, type_of_type});
		}
		break;
	case Typen("function in clouds"):
		living_functions.insert(memory);
		mark_single_element(*(uint64_t**)memory, T::AST_pointer);
		mark_single_element(*(uint64_t**)(memory+1), T::type);
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
		if (next_memory->first != memory_incrementor)
			free_memory.insert({next_memory->first - memory_incrementor, memory_incrementor});
		memory_incrementor = next_memory->first + next_memory->second;
		if (next_memory == living_objects.end())
			break;
	}
	if (memory_incrementor != big_memory_pool + pool_size) //the final bit of memory at the end
		free_memory.insert({big_memory_pool + pool_size - memory_incrementor, memory_incrementor});
	living_objects.clear();

	//todo: allocation for functions. sweeping for functions.
}

//future: test suite for GC.
//make sure that functions go away when necessary, and don't go away when not necessary.
//test that some objects are collected, especially ones in loops
//test that some objects are not collected, especially ones nested deeply