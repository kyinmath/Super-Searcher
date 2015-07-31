#pragma once
#include "types.h"
#include "memory.h"

constexpr bool VECTOR_DEBUG = false;

//vector containing a single object per element.
struct svector
{
	Tptr type; //must be exactly one element.
	uint64_t size;
	uint64_t reserved_size;
	uint64_t& operator [](uint64_t i)
	{
		return *((uint64_t*)(&reserved_size) + i + 1);
	}
	template<class T>
	constexpr operator llvm::ArrayRef<T>() const { return llvm::ArrayRef<T>(&(*this)[0], size); }
	//after this come the contents
	//std::array<uint64_t, 0> contents; this causes segfaults with asan/ubsan. don't do it.
};

constexpr uint64_t vector_header_size = sizeof(svector) / sizeof(uint64_t);

inline svector* new_vector(Tptr type)
{
	uint64_t default_initial_size = 3;
	auto new_location = (svector*)allocate(vector_header_size + default_initial_size);
	new_location->type = type;
	new_location->size = 0;
	new_location->reserved_size = default_initial_size;
	if (VECTOR_DEBUG) print("new location at ", new_location, '\n');
	return new_location;
}

template<class T>
inline svector* new_vector(Tptr type, llvm::ArrayRef<T> elements)
{
	svector* new_location = (svector*)allocate(vector_header_size + elements.size());
	new_location->type = type;
	new_location->size = elements.size();
	new_location->reserved_size = elements.size() + (elements.size() >> 1);
	for (uint64_t x = 0; x < elements.size(); ++x)
		(*new_location)[x] = elements[x];
	if (VECTOR_DEBUG) print("new location at ", new_location, '\n');
	return new_location;
}

inline void pushback_int(svector*& s, uint64_t value)
{
	if (VECTOR_DEBUG) print("pushing back vector at ", s, " value ", value, '\n');
	if (s->size == s->reserved_size)
	{
		uint64_t new_size = s->reserved_size + (s->reserved_size >> 1);
		llvm::ArrayRef<uint64_t> old_fields(&(*s)[0], s->size);
		s = new_vector(s->type, old_fields);
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
	if (offset < s->size)
		return &(*s)[offset];
	else return 0;
}


struct Vector_range
{
	svector* t;
	Vector_range(svector* t_ ) : t(t_) {}
	uint64_t* begin() { return &(*t)[0]; }
	uint64_t* end() { return &(*t)[t->size]; }
};