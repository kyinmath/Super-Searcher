#include "datatypes.h"
#include "constants.h"
#include <set>
#include <array>
#include <multimap>
#include <bitset>
#include <memory>

//todo: actually figure out memory size according to the computer specs
//problem: the constexpr objects are outside of our memory pool. they're quite exceptional, since they can't be GC'd. do we really need to have special cases, just for them?
	//maybe not. in the future, we'll just make sure to wrap them in a unique() function, so that the user only ever sees GC-handled objects.

constexpr uint64_t memory_size = 100000000Ui64; //number of uint64_ts
constexpr uint64_t AST_number = 1000000Ui64; //memory explicitly for ASTs

uint64_t big_memory_pool[memory_size];
uint64_t function_pool[AST_number];
std::bitset<AST_number> AST_pool_flags; //each bit refers to a AST, which is made up of multiple slots. it's marked 1 if it's occupied

std::multimap 

uint64_t* allocate(uint64_t size)
{
	
}


void mark_AST_target(AST_T* location)
{
	if (uintptr_t(location) < uintptr_t(&AST_pool) || uintptr_t(location) >= uintptr_t(&AST_pool[AST_number]))
		assert("AST outside of bounds");

	if (AST_pool_flags[location - AST_pool])
		return; //it's already set.
	AST_pool_flags.set(location - AST_pool);
	mark_nonAST_target(type_pointer::AST, uintptr_t(location->AST));
}

void mark_nonAST_target(immut_type_T* type, uintptr_t location)
{
	//check if the location is actually located in the memory pool, or if it's a global. and I think you CAN get pointers to globals, using "get pointer to TU type N"
	if (location < uintptr_t(&big_memory_pool) || location >= uintptr_t(&big_memory_pool[memory_size]))
		return; //mark nothing, it's a global

	//dyn pointer: mark the type first, then mark the target.

}

//todo: shut down threads first
void collect_garbage()
{
	clear_pool_flags();
	for (auto& k : system_threads)
	{
		mark_AST_target(k.the_AST);
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

int main(int argc, const char * argv[]) {
	test1();
	test2();
	test3();
	test4();
	perfTest();

	return 0;
}
