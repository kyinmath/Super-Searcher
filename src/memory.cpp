#include <array>
#include <map>
#include <memory>
#include <stack>
#include "globalinfo.h"
#include "types.h"
#include "runtime.h"
#include "function.h"
#include "vector.h"

//found_living_object and found_function depend on these pools being contiguous
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

//this is initialized through a function called by main(), so there's no worries about static fiasco where the u:: types are initialized after this.
std::vector< Tptr > type_roots; //exactly the u::things that aren't a simple integer. initialized by initialize(). will only contain the vector_of_ASTs for now.
std::vector< function*> event_roots;
type_htable_t type_hash_table; //a hash table of all the unique types. don't touch this unless you're the memory allocation

bool found_living_object(uint64_t* memory, uint64_t size); //adds the object onto the list of living objects.

//marks any further objects found in a possibly-concatenated object.
//does not create any new types. this lets it work for unserialization, where the unique type hash table isn't populated.
void mark_target(uint64_t& memory, Tptr t); //note that this takes a reference instead of a pointer. this was to get rid of the horrible *(vector**) casts which I didn't understand.


void initialize_roots();
void sweepy_sweep();

bool UNSERIALIZATION_MODE;
uint64_t GC_IS_RUNNING = false; //used to catch allocations while GC is running

uint64_t* allocate(uint64_t size)
{
	check(size != 0, "allocating 0 elements means nothing");
	check(!GC_IS_RUNNING, "no allocating while GCing");
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

void initialize_roots()
{
	for (auto& root_type : type_roots)
	{
		if (VERBOSE_GC) print("gc root type at ", root_type, '\n');
		if (Type_descriptor[root_type.ver()].pointer_fields != 0)
		{
			mark_target((uint64_t&)root_type.val, u::type);
			if (UNSERIALIZATION_MODE) uniquefy_premade_type(root_type, true);
		}
	}
	for (auto& root_function : event_roots)
	{
		if (VERBOSE_GC) print("gc root function at ", root_function, '\n');
		mark_target((uint64_t&)root_function, Typen("function pointer"));
	}

	//add in the event-driven ASTs
}
void trace_objects();

void start_GC()
{
	UNSERIALIZATION_MODE = false;
	trace_objects();
}


void trace_objects()
{
	GC_IS_RUNNING = true;
	if (SUPER_VERBOSE_GC)
	{
		print("listing all memory\n");
		for (uint64_t x = 0; x < pool_size; ++x)
		{
			if (big_memory_pool[x] != initial_special_value && big_memory_pool[x] != collected_special_value)
			{
				print("writ ", &big_memory_pool[x]);
				while (big_memory_pool[x] != initial_special_value && big_memory_pool[x] != collected_special_value && x < pool_size)
				{
					print(" ", big_memory_pool[x]);
					++x;
				}
				print('\n');
			}
		}
	}
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
	/*if (SUPER_VERBOSE_GC)
	{
		print("outputting all types in hash table\n");
		for (auto& type : type_hash_table)
			output_type(type);
	}*/
	sweep_function_pool_flags = new uint64_t[function_pool_size / 64]();
	check(living_objects.empty(), "need to start GC with an empty tree");
	type_hash_table.clear(); //clear the unique table, we'll rebuild it.
	initialize_roots();
	if (VERBOSE_GC)
	{
		for (auto& x : living_objects)
			print("living object ", x.first, " size ", x.second, '\n');
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
	GC_IS_RUNNING = false;

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

//steps: 1. you correct the pointer.
//2. you call found_living_object/found_living function. this comes after 1, because dynamic objects need to learn their type to learn their size, and found_living requires knowing the size.
//3. you mark the targets in the section you just found, recursing. this comes after 2, to prevent infinite recursion.

//memory is a reference to a single 1-size object.
void mark_target(uint64_t& memory, Tptr t)
{
	if (VERBOSE_GC)
	{
		print("marking single ", &memory, ' ');
		output_type(t);
	}
	if (memory) print(""); //force a memory access. if it's a 0 reference, it should segfault
	check(t != 0, "passed 0 type pointer to mark_single");

	//interpreting the memory as a pointer.
	uint64_t*& int_pointer = (uint64_t*&)memory;

	if (UNSERIALIZATION_MODE)
	{
		if (t.ver() == Typen("integer")); //do nothing
		else if (t.ver() == Typen("function pointer")) correct_function_pointer(int_pointer); //note: corrections should consider 0 as well.
		else if (t.ver() == Typen("con_vec")); // do nothing
		else correct_pointer(int_pointer);
	}

	switch (t.ver())
	{
	case Typen("con_vec"):
		{
			uint64_t* increm = &memory;
			for (auto& subt : Type_pointer_range(t))
			{
				mark_target(*increm, subt);
				++increm;
				check(get_size(subt) == 1, "type not of size 1");
			}
		}
		break;
	case Typen("integer"):
		break;
	case Typen("pointer"):
		if (memory == 0) break;
		if (found_living_object(int_pointer, get_size(t))) break;
		mark_target(*int_pointer, t.field(0)); //this is better for debugging. we get a stack trace instead of flattening the stack.

		break;
	case Typen("dynamic object"):
		if (memory != 0)
		{
			dynobj*& dynamic_obj = (dynobj*&)memory;
			mark_target((uint64_t&)dynamic_obj->type, u::type); //correct the type pointer first. since types can't lead to dynamic objects, we can't infinite loop, even though we're marking the element before putting it in the living objects list.
			check(dynamic_obj->type != 0, "when making a dynamic object, only the base pointer may be 0, not the type inside");

			if (found_living_object(int_pointer, get_size(dynamic_obj->type) + 1)) break; //we can use the type pointer, since it's corrected.
			mark_target(*(int_pointer + 1), dynamic_obj->type);
		}
		break;

	case Typen("vector"):
		{
			svector*& the_vector = (svector*&)memory;
			if (found_living_object(int_pointer, vector_header_size + the_vector->reserved_size)) break;
			for (auto& x : Vector_range(the_vector))
				mark_target(x, t.field(0));
		}
		break;
	case Typen("AST pointer"):
		if (memory != 0)
		{
			uAST*& the_AST = (uAST*&)memory;

			uint64_t tag = the_AST->tag;
			check(tag < ASTn("never reached"), "get_AST_type is a sandboxed function, use the user facing version instead");
			if (found_living_object(int_pointer, get_full_size_of_AST(the_AST->tag))) break;

			if (tag == ASTn("imv")) mark_target((uint64_t&)the_AST->fields[0], u::dynamic_object);
			if (tag == ASTn("basicblock")) mark_target((uint64_t&)the_AST->fields[0], u::vector_of_ASTs);
			for (uAST*& x : AST_range(the_AST))
				mark_target((uint64_t&)x, u::AST_pointer);
		}
		break;
	case Typen("type pointer"):
		if (memory != 0)
		{
			Tptr& the_type = (Tptr&)memory;
			if (VERBOSE_GC) print("uniquefying in GC ", the_type, '\n');

			if (Type_descriptor[the_type.ver()].pointer_fields != 0)
			{
				if (found_living_object(int_pointer, total_valid_fields(the_type) + 1)) break;
				for (Tptr& subtype : Type_pointer_range(the_type))
					mark_target((uint64_t&)subtype, u::type);
			}
			uniquefy_premade_type(the_type, true); //we can only uniquefy after marking, not before, because the type hash table relies on subfields being unique. otherwise, it'll make a new copy.
		}
		break;
	case Typen("function pointer"):
		function* func = (function*)memory;
		if (found_function(func)) break;

		
		mark_target((uint64_t&)(func->the_AST), u::AST_pointer);
		mark_target((uint64_t&)(func->return_type), u::type);
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
	if (memory_incrementor < &big_memory_pool[pool_size]) //the final bit of memory at the end
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
	else check(memory_incrementor == &big_memory_pool[pool_size], "memory incrementor is past the pool");
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
	Tptr does_not_return = uniquefy_premade_type(T::does_not_return, true); //it's false, because the constexpr types are not in the memory pool. except with the Tptr optimization, it's all constant.
	Tptr integer = uniquefy_premade_type(T::integer, true);
	Tptr dynamic_object = uniquefy_premade_type(T::dynamic_object, true);
	Tptr type = uniquefy_premade_type(T::type, true);
	Tptr AST_pointer = uniquefy_premade_type(T::AST_pointer, true);
	Tptr function_pointer = uniquefy_premade_type(T::function_pointer, true);
	Tptr vector_of_ASTs = new_unique_type(Typen("vector"), AST_pointer);
	Tptr pointer_to_something = Typen("pointer to something");
	Tptr vector_of_something = Typen("vector of something");
};
