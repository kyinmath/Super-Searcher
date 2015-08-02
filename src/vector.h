#pragma once
#include "types.h"
#include "memory.h"

//vector containing a single object per element.
struct svector
{
	uint64_t size;
	uint64_t reserved_size; //after this come the contents. [] relies on reserved_size being the last.
	//std::array<uint64_t, 0> contents; this causes segfaults with asan/ubsan. don't do it.


	uint64_t& operator [](uint64_t i)
	{
		return *((uint64_t*)(&reserved_size) + i + 1);
	}
	template<class T>
	operator llvm::ArrayRef<T>() { return llvm::ArrayRef<T>((T*)&(*this)[0], size); }
};

constexpr uint64_t vector_header_size = sizeof(svector) / sizeof(uint64_t);


template<class T>
inline svector* vector_build(llvm::ArrayRef<T> elements)
{
	uint64_t reserved_size = 3 + elements.size() + (elements.size() >> 1); //pushback() relies on there being enough space after a relocation.
	svector* new_location = (svector*)allocate(vector_header_size + reserved_size);
	new_location->size = elements.size();
	new_location->reserved_size = reserved_size;
	for (uint64_t x = 0; x < elements.size(); ++x)
		(*new_location)[x] = (uint64_t)elements[x];
	if (VECTOR_DEBUG) print("new vector at ", new_location, '\n');
	return new_location;
}

inline svector* new_vector() { return vector_build(llvm::ArrayRef<uint64_t>{}); }

inline void pushback_int(svector*& s, uint64_t value)
{
	if (VECTOR_DEBUG) print("pushing back vector at ", s, " value ", value, '\n');
	check(s->size <= s->reserved_size, "more elements than reserved");
	if (s->size == s->reserved_size)
	{
		if (VECTOR_DEBUG) print("reallocating vector after pushback");
		s = vector_build(llvm::ArrayRef<uint64_t>(*s)); //specify the ArrayRef type to force the uint64_t template to work
	}
	(*s)[s->size++] = value;
}

inline void pushback(svector** s, uint64_t value)
{
	pushback_int(*s, value);
}

inline uint64_t vector_size(svector* s)
{
	return s->size;
}

//returns either pointer to the element, or 0.
inline uint64_t* reference_at(svector* s, uint64_t offset)
{
	if (offset < s->size) return &(*s)[offset];
	else return 0;
}


struct Vector_range
{
	svector* t;
	Vector_range(svector* t_ ) : t(t_) {}
	uint64_t* begin() { return &(*t)[0]; }
	uint64_t* end() { return &(*t)[t->size]; }
};