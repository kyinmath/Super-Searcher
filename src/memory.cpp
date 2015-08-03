#include <array>
#include <map>
#include <memory>
#include <stack>
#include "globalinfo.h"
#include "types.h"
#include "runtime.h"
#include "function.h"


//found_living_object and found_function depend on these pools being contiguous
constexpr const uint64_t pool_size = 100000ull;
constexpr const uint64_t function_pool_size = 2000ull * 64;
constexpr const uint64_t initial_special_value = 21212121ull;
constexpr const uint64_t collected_special_value = 1234567ull;
std::vector<uint64_t> big_memory_allocation(pool_size, initial_special_value);
uint64_t* big_memory_pool = big_memory_allocation.data();
//designated initializers don't work, because they add 7MB memory to the executable

std::vector<uint64_t> function_memory_allocation(function_pool_size * sizeof(function) / sizeof(uint64_t), initial_special_value);
function* function_pool = (function*)function_memory_allocation.data(); //we use a backing memory pool to prevent the array from running dtors at the end of execution
uint64_t function_pool_flags[function_pool_size / 64] = {0}; //each bit is marked 0 if free, 1 if occupied. the {0} is necessary by https://stackoverflow.com/questions/629017/how-does-array100-0-set-the-entire-array-to-0#comment441685_629023
uint64_t* sweep_function_pool_flags; //we need this to be able to run finalizers on the functions.

//if we did want to multi-thread, we'd eventually want a lock on these things.
std::multimap<uint64_t, uint64_t*> free_memory = {{pool_size, big_memory_pool}}; //first value = size of slot. second value = address
std::map<uint64_t*, uint64_t> living_objects; //first value = address of user-seen memory. second slot = size of user-seen memory. ignores headers. must be ordered to find new memory.
uint64_t first_possible_empty_function_block; //where to start looking for an empty function block.
std::stack < std::pair<uint64_t*, Tptr>> to_be_marked; //stack of things to be marked. this is good because it lets us avoid recursion.

void new_mark_element(uint64_t* memory, Tptr t);
bool found_living_object(uint64_t* memory, uint64_t size); //adds the object onto the list of living objects.
void marky_mark(uint64_t* memory, Tptr t); //marks any further objects found in a possibly-concatenated object. you must add it to the list of living objects separately; marky_mark doesn't do that. the reason is that some things like dynamic objects want to use marky_mark, but want to add a different object to the living_objects stack. they don't want to create a whole new concatenation.
void mark_single_element(uint64_t& memory, Tptr t); //note that mark_single takes a reference instead of a pointer. this was to get rid of the horrible *(vector**) casts which I didn't understand. however, this means that marky_mark and mark_single are different in their arguments. marky_mark() takes a pointer so it can handle Tptr.
void initialize_roots();
void sweepy_sweep();

uint64_t* allocate(uint64_t size)
{
	check(size != 0, "allocating 0 elements means nothing");
	uint64_t true_size = size;
	auto k = free_memory.lower_bound(true_size);
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
		if (found_size < size + 1) //if you get the element, there'll be a dead space that's too small for any allocation
		{
			k = free_memory.upper_bound(size); //find a better spot. must be >= size + 2 header + 1
			if (k == free_memory.end()) error("OOM");
			found_size = k->first;
			found_place = k->second;
		}
		//reduce the block size
		free_memory.erase(k);
		free_memory.insert(std::make_pair(found_size - true_size, found_place + true_size));
	}
	else error("what damn kind of allocated object did I just find");

	if (DEBUG_GC)
	{
		for (uint64_t* check_value = found_place; check_value < found_place; ++check_value)
		{
			if ((*check_value != collected_special_value) && (*check_value != initial_special_value))
			{
				print("tried to allocate still-living memory with value ", *check_value, " at ", check_value, '\n');
				error("");
			}
		}
	}

	if (VERBOSE_GC) print("allocating region ", found_place, " size, ", size, '\n');
	return found_place;
}

int first_zero(uint64_t mask)
{
	for (int x = 0; x < 64; ++x)
		if (((mask >> x) % 2) == 0) return x;
	error("no zeros found");
}

function* allocate_function()
{
	for (uint64_t x = first_possible_empty_function_block; x < function_pool_size / 64; ++x)
	{
		uint64_t mask = function_pool_flags[x];
		if (mask != ~0ull)
		{
			int offset = first_zero(mask);
			uint64_t bit_mask = 1ull << offset; //we need to shift 1ull, not 1. this has a high potential for bugs (bitten twice now).
			function_pool_flags[x] |= bit_mask;
			if (VERBOSE_GC)
			{
				print("returning allocation ", &function_pool[x * 64 + offset], " with number ", x * 64 + offset, '\n');
				print("the mask was ", mask, '\n');
				print("the bit mask was ", bit_mask, '\n');
				print("offset was ", offset, '\n');
			}
			return &function_pool[x * 64 + offset];
		}
	}
	error("OOM function");
}

std::deque< function*> fuzztester_roots;
type_htable_t type_hash_table; //a hash table of all the unique types. don't touch this unless you're the memory allocation
//this is extern, because of static initialization fiasco. but we still want to use it at the end of GC, so it should be global. thus, the actual objects are at the bottom of the file.
extern std::vector< Tptr > unique_type_roots;
void initialize_roots()
{
	for (auto& root_type : unique_type_roots)
	{
		if (Type_descriptor[root_type.ver()].pointer_fields != 0)
			to_be_marked.push(std::make_pair((uint64_t*)root_type.val, get_Type_full_type(root_type)));
	}
	//print("outputting all types in hash table\n");
	//for (auto& type : type_hash_table)
	//	output_type(type);

	for (auto& root_function : fuzztester_roots)
	{
		print("gc root function at ", root_function, '\n');
		mark_single_element((uint64_t&)root_function, Typen("function pointer"));
	}

	//add in the event-driven ASTs
}


void start_GC()
{
	if (VERBOSE_GC)
	{
		uint64_t total_memory_use = 0;
		for (auto& x : free_memory)
		{
			print("before GC: memory slot ", x.second, " size ", x.first, '\n');
			total_memory_use += x.first;
		}
		print("total free before GC ", total_memory_use, '\n');
	}
	sweep_function_pool_flags = new uint64_t[function_pool_size / 64]();
	check(living_objects.empty(), "need to start GC with an empty tree");
	initialize_roots();
	while (!to_be_marked.empty())
	{
		auto k = to_be_marked.top();
		to_be_marked.pop(); //this happens before marky_mark(), because marky_mark() might insert things on the top of the stack. then, those things are killed off by this pop().
		//instead, after we grab a task, we take it off the stack.
		if (VERBOSE_GC)
		{
			print("GC marking ", k.first, " ");
			output_type(k.second);
			print("the size of the marking stack before marking is ", to_be_marked.size(), '\n');
		}
		if (found_living_object(k.first, get_size(k.second))) continue;
		marky_mark(k.first, k.second);
	}
	if (VERBOSE_GC)
	{
		for (auto& x : living_objects)
			print("living object ", x.first, " size ", x.second, '\n');
	}

	//can't use range-based for loops, since the erasure invalidates the iterator completely.
	for (auto it = type_hash_table.begin(); it != type_hash_table.end();)
	{
		if (living_objects.find((uint64_t*)it->val) == living_objects.end()) it = type_hash_table.erase(it);
		else ++it;
	}

	sweepy_sweep();
	delete[] sweep_function_pool_flags;

	if (VERBOSE_GC)
	{
		uint64_t total_memory_use = 0;
		for (auto& x : free_memory)
		{
			print("after GC: memory slot ", x.second, " size ", x.first, '\n');
			total_memory_use += x.first;
		}
		print("total free after GC ", total_memory_use, '\n');

		for (auto& x : living_objects) print("living object after GC ", x.first, " size ", x.second, '\n');
	}

}

//returns 0 if it really is a new object, 1 if the object already exists.
bool found_living_object(uint64_t* memory, uint64_t size)
{
	check(size != 0, "no null types in GC allowed");
	check(memory != 0, "no null pointers in GC allowed");
	if (HEURISTIC) check(size < 1000000, "object seems large?");
	if (DEBUG_GC)
	{
		check(memory >= big_memory_pool, "memory out of bounds");
		check(memory + size <= big_memory_pool + pool_size, "memory out of bounds");
	}
	if (living_objects.find(memory) != living_objects.end()) return 1; //it's already there. nothing needs to be done, since we assume that full pointers point to the entire object.
	if (SUPER_VERBOSE_GC)
	{
		print("found ", memory, " v");
		for (uint64_t x = 0; x < size; ++x)
			print(" ", std::hex, memory[x]);
		print('\n');
	}
	living_objects.insert({memory, size});
	return 0;
}

bool found_function(function* func)
{
	uint64_t number = func - function_pool;
	if (sweep_function_pool_flags[number / 64] & (1ull << (number % 64))) //bitwise
		return true;
	sweep_function_pool_flags[number / 64] |= (1ull << (number % 64));
	return false;
}


void new_mark_element(uint64_t* memory, Tptr t)
{
	check(t != 0, "marking a null type");
	//to_be_marked.push({memory, t});

	if (found_living_object(memory, get_size(t))) return;
	marky_mark(memory, t); //this is better for debugging. we get a stack trace instead of flattening the stack.
}

//if you call marky_mark, make sure to remember to call found_living_object as well. but mainly, consider using new_mark_element instead.
//unlike mark_single(), this can handle Tptrs.
void marky_mark(uint64_t* memory, Tptr t)
{
	check(t != 0, "marking a null type");
	if (VERBOSE_GC)
	{
		print("marking memory ", memory, " ");
		output_type(t);
	}
	if (t.ver() == u::type) //handle the special type optimization
	{
		if (Type_descriptor[((Tptr&)*memory).ver()].pointer_fields == 0)
			return;
	}
	if (t.ver() == Typen("con_vec"))
	{
		//print("marking convec\n");
		for (auto& subt : Type_pointer_range(t))
		{
			mark_single_element(*memory, subt);
			memory += get_size(subt);
			check(get_size(subt) == 1, "can't use mark_single_element on large or small objects");
		}
	}
	else mark_single_element(*memory, t);
}
#include "vector.h"
//memory points to the single object.
void mark_single_element(uint64_t& memory, Tptr t)
{
	if (VERBOSE_GC)
	{
		print("marking single ", &memory, ' ');
		output_type(t);
	}
	if (memory) print(""); //force a memory access
	//check(&memory != nullptr, "passed 0 memory pointer to mark_single"); //UB for references.
	check(t != 0, "passed 0 type pointer to mark_single");
	switch (t.ver())
	{
	case Typen("con_vec"):
		error("no nested con_vecs allowed");
	case Typen("integer"):
		break;
	case Typen("pointer"):
		new_mark_element((uint64_t*)memory, t.field(0));
		break;
	case Typen("dynamic object"):
		if (memory != 0)
		{
			dynobj* dynamic_obj = (dynobj*)memory;
			uint64_t* pointer_to_the_type_and_object = (uint64_t*)memory;
			check(dynamic_obj->type != 0, "when making a dynamic object, only the base pointer may be 0, not the type inside");
			//we don't want to create a concatenation. thus, we call marky_mark directly.
			if (found_living_object(pointer_to_the_type_and_object, get_size(dynamic_obj->type) + 1)) break;
			marky_mark(pointer_to_the_type_and_object, u::type);
			marky_mark(pointer_to_the_type_and_object + 1, *pointer_to_the_type_and_object);
		}
		break;

	case Typen("vector"):
		{
			//we don't want to make a huge concatenation for the type. thus, we force the issue by marking things directly, instead of adding onto the stack. I'm worried that this might cause stack overflows, but hopefully, it won't skip over the guard page.
			svector* the_vector = (svector*)memory;
			uint64_t* int_pointer_to_vector = (uint64_t*)the_vector;
			if (found_living_object(int_pointer_to_vector, vector_header_size + the_vector->reserved_size)) break;
			for (auto& x : Vector_range(the_vector))
				marky_mark(&x, t.field(0)); //if we change t.field(0) to pointer_to_the_vector->type, it is a bug. that means the vector type is wrong.
		}
		break;
	case Typen("AST pointer"):
		if (memory != 0)
		{
			uAST* the_AST = (uAST*)memory;
			Tptr type_of_AST = get_AST_full_type(the_AST->tag);
			new_mark_element((uint64_t*)memory, type_of_AST);
		}
		break;
	case Typen("type pointer"):
		if (memory != 0)
		{
			Tptr the_type = (Tptr)memory;
			if (Type_descriptor[the_type.ver()].pointer_fields != 0)
				new_mark_element((uint64_t*)memory, get_Type_full_type(the_type));
		}
		break;
	case Typen("function pointer"):
		function* func = (function*)memory;
		if (found_function(func)) break;

		mark_single_element((uint64_t&)(func->the_AST), u::AST_pointer);
		mark_single_element((uint64_t&)(func->return_type), u::type);
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
		auto next_memory = living_objects.lower_bound(memory_incrementor);
		if (next_memory == living_objects.end())
			break;
		uint64_t* ending_location = next_memory->first;
		if (VERBOSE_GC)
		{
			if (memory_incrementor != ending_location)
				print("memory available from ", memory_incrementor, " to ", ending_location, '\n');
			else print("no mem available at ", memory_incrementor, '\n');
		}
		if (ending_location != memory_incrementor)
		{
			if (DEBUG_GC)
			{
				check((uint64_t)(ending_location - memory_incrementor) < pool_size, "more free memory than exists");
				for (uint64_t* x = memory_incrementor; x < ending_location; ++x)
					*x = collected_special_value; //any empty fields are set to a special value
			}
			free_memory.insert({ending_location - memory_incrementor, memory_incrementor});
		}
		memory_incrementor = next_memory->first + next_memory->second;
	}
	if (memory_incrementor != &big_memory_pool[pool_size]) //the final bit of memory at the end
	{
		if (VERBOSE_GC)
			print("last memory available starting from ", memory_incrementor, '\n');
		if (DEBUG_GC)
		{
			check(memory_incrementor < &big_memory_pool[pool_size], "memory incrementor past the end " + std::to_string((uint64_t)memory_incrementor) + " end " + std::to_string((uint64_t)&big_memory_pool[pool_size]));
			for (uint64_t* x = memory_incrementor; x < &big_memory_pool[pool_size]; ++x)
				*x = collected_special_value; //any empty fields are set to a special value
		}
		free_memory.insert({&big_memory_pool[pool_size] - memory_incrementor, memory_incrementor});
	}
	else if (VERBOSE_GC) error("no mem available at the end. what is wrong?");
	living_objects.clear();

	for (uint64_t x = 0; x < function_pool_size / 64; ++x)
	{
		uint64_t diffmask = function_pool_flags[x] - sweep_function_pool_flags[x];
		if (diffmask)
		{
			if (VERBOSE_GC) print("diffmask ", diffmask, '\n');
			for (uint64_t offset = 0; offset < 64; ++offset)
				if (diffmask & (1ull << offset))
				{
					if (VERBOSE_GC) print("offset is ", offset, ' ');
					function_pool[x * 64 + offset].~function();
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



//this is here to prevent static fiasco. must be below all the constants for the memory allocator. and must be below the hash table.
namespace u
{
	Tptr does_not_return = get_unique_type(T::does_not_return, false); //it's false, because the constexpr types are not in the memory pool
	Tptr integer = get_unique_type(T::integer, false);
	Tptr dynamic_object = get_unique_type(T::dynamic_object, false);
	Tptr type = get_unique_type(T::type, false);
	Tptr AST_pointer = get_unique_type(T::AST_pointer, false);
	Tptr function_pointer = get_unique_type(T::function_pointer, false);
	Tptr vector_of_ASTs = new_type(Typen("vector"), AST_pointer);
	Tptr pointer_to_something = Typen("pointer to something");
	Tptr vector_of_something = Typen("vector of something");
};

//this comes below the types, to prevent static fiasco.
std::vector< Tptr > unique_type_roots{u::does_not_return, u::integer, u::dynamic_object, u::type, u::AST_pointer, u::function_pointer, u::vector_of_ASTs, u::pointer_to_something, u::vector_of_something};