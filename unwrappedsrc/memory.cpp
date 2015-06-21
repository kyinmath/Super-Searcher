#include <array>
#include <map>
#include <bitset>
#include <memory>
#include <stack>
#include "console.h"
#include "types.h"
#include "user_facing_functions.h"

//todo: replace all new()

//todo: actually figure out memory size according to the computer specs
//problem: the constexpr objects are outside of our memory pool. they're quite exceptional, since they can't be GC'd. do we really need to have special cases, just for them?
//maybe not. in the future, we'll just make sure to wrap them in a unique() function, so that the user only ever sees GC-handled objects.

constexpr uint64_t memory_size = 100000000Ui64; //number of uint64_ts
constexpr uint64_t AST_number = 1000000Ui64; //memory explicitly for ASTs

uint64_t big_memory_pool[memory_size];
uint64_t function_pool[AST_number];
std::bitset<AST_number> AST_pool_flags; //each bit refers to a AST, which is made up of multiple slots. it's marked 1 if it's occupied

//todo: a lock on this. maybe using Lo<>
//first value = size of slot. second value = address.
std::multimap<uint64_t, uint64_t*> free_memory;
std::map<uint64_t*, uint64_t> living_objects;
std::stack < std::pair<uint64_t*, Type*>> to_be_marked;

uint64_t* allocate(uint64_t size)
{
	auto k = free_memory.upper_bound(size + 1);
	if (k == free_memory.end())
	{
		//time to reallocate!
		//then, try to allocate() again, and return the proper value. except if you fail again, then call abort().
	}
	if (k->first == size + 1) //lucky! got the exact size we needed
	{
		free_memory.erase(k); //block is no longer free
	}
	else	if (k->first >= size + 1) //it's a bit too big
	{
		//reduce the block size
		free_memory.erase(k);
		free_memory.insert(std::make_pair(k->first - size - 1, k->second + size + 1));
	}
	else error("what damn kind of allocated object did I just find");

	*(k->second) = 0; //mark and sweep allocator, so just mark it 0. we never use this though.
	return k->second + 1;
}

uint64_t* allocate_AST(uint64_t size)
{
}

void initialize_roots()
{
	//add in the special types which we need to know about
	//add in the thread ASTs
}

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
	}
}


typedef struct sObject {
	ObjectType type;
	unsigned char marked;

	/* The next object in the linked list of heap allocated objects. */
	struct sObject* next;

	union {
		/* OBJ_INT */
		int value;

		/* OBJ_PAIR */
		struct {
			struct sObject* head;
			struct sObject* tail;
		};
	};
} Object;

typedef struct {
	Object* stack[MEMORY_SIZE];
	int stackSize;

	/* The first object in the linked list of all objects on the heap. */
	Object* firstObject;

	/* The total number of currently allocated objects. */
	int numObjects;

	/* The number of objects required to trigger a GC. */
	int maxObjects;
} VM;

VM* newVM() {
	VM* vm = new VM;
	vm->stackSize = 0;
	vm->firstObject = NULL;
	vm->numObjects = 0;
	vm->maxObjects = 8;
	return vm;
}


void push(VM* vm, Object* value) {
	assert(vm->stackSize < MEMORY_SIZE, "Memory overflow!");
	vm->stack[vm->stackSize++] = value;
}


Object* pop(VM* vm) {
	assert(vm->stackSize > 0, "Memory underflow!");
	return vm->stack[--vm->stackSize];
}

void mark(Object* object) {
	/* If already marked, we're done. Check this first to avoid recursing
	on cycles in the object graph. */
	if (object->marked) return;

	object->marked = 1;

	if (object->type == OBJ_PAIR) {
		mark(object->head);
		mark(object->tail);
	}
}

void markAll(VM* vm)
{
	for (int i = 0; i < vm->stackSize; i++) {
		mark(vm->stack[i]);
	}
}

void sweep(VM* vm)
{
	Object** object = &vm->firstObject;
	while (*object) {
		if (!(*object)->marked) {
			/* This object wasn't reached, so remove it from the list and free it. */
			Object* unreached = *object;

			*object = unreached->next;
			free(unreached);

			vm->numObjects--;
		}
		else {
			/* This object was reached, so unmark it (for the next GC) and move on to
			the next. */
			(*object)->marked = 0;
			object = &(*object)->next;
		}
	}
}

void gc(VM* vm) {
	int numObjects = vm->numObjects;

	markAll(vm);
	sweep(vm);

	vm->maxObjects = vm->numObjects * 2;

	printf("Collected %d objects, %d remaining.\n", numObjects - vm->numObjects,
		vm->numObjects);
}

Object* newObject(VM* vm, ObjectType type) {
	if (vm->numObjects == vm->maxObjects) gc(vm);

	Object* object = malloc(sizeof(Object));
	object->type = type;
	object->next = vm->firstObject;
	vm->firstObject = object;
	object->marked = 0;

	vm->numObjects++;

	return object;
}

void pushInt(VM* vm, int intValue) {
	Object* object = newObject(vm, OBJ_INT);
	object->value = intValue;

	push(vm, object);
}

Object* pushPair(VM* vm) {
	Object* object = newObject(vm, OBJ_PAIR);
	object->tail = pop(vm);
	object->head = pop(vm);

	push(vm, object);
	return object;
}

void objectPrint(Object* object) {
	switch (object->type) {
	case OBJ_INT:
		printf("%d", object->value);
		break;

	case OBJ_PAIR:
		printf("(");
		objectPrint(object->head);
		printf(", ");
		objectPrint(object->tail);
		printf(")");
		break;
	}
}

void freeVM(VM *vm) {
	vm->stackSize = 0;
	gc(vm);
	free(vm);
}

void test1() {
	printf("Test 1: Objects on stack are preserved.\n");
	VM* vm = newVM();
	pushInt(vm, 1);
	pushInt(vm, 2);

	gc(vm);
	assert(vm->numObjects == 2, "Should have preserved objects.");
	freeVM(vm);
}

void test2() {
	printf("Test 2: Unreached objects are collected.\n");
	VM* vm = newVM();
	pushInt(vm, 1);
	pushInt(vm, 2);
	pop(vm);
	pop(vm);

	gc(vm);
	assert(vm->numObjects == 0, "Should have collected objects.");
	freeVM(vm);
}

void test3() {
	printf("Test 3: Reach nested objects.\n");
	VM* vm = newVM();
	pushInt(vm, 1);
	pushInt(vm, 2);
	pushPair(vm);
	pushInt(vm, 3);
	pushInt(vm, 4);
	pushPair(vm);
	pushPair(vm);

	gc(vm);
	assert(vm->numObjects == 7, "Should have reached objects.");
	freeVM(vm);
}

void test4() {
	printf("Test 4: Handle cycles.\n");
	VM* vm = newVM();
	pushInt(vm, 1);
	pushInt(vm, 2);
	Object* a = pushPair(vm);
	pushInt(vm, 3);
	pushInt(vm, 4);
	Object* b = pushPair(vm);

	a->tail = b;
	b->tail = a;

	gc(vm);
	assert(vm->numObjects == 4, "Should have collected objects.");
	freeVM(vm);
}

void perfTest() {
	printf("Performance Test.\n");
	VM* vm = newVM();

	for (int i = 0; i < 1000; i++) {
		for (int j = 0; j < 20; j++) {
			pushInt(vm, i);
		}

		for (int k = 0; k < 20; k++) {
			pop(vm);
		}
	}
	freeVM(vm);
}