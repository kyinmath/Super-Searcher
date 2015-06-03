#pragma once
#include "types.h"

inline uint64_t make_AST_pointer_from_dynamic(uint64_t tag, uint64_t intpointer)
{
	uint64_t* pointer = (uint64_t*)intpointer;
	std::vector<uint64_t> fields{ 0, 0, 0, 0 };
	for (int x = 0; x < AST_descriptor[tag].pointer_fields + AST_descriptor[tag].additional_special_fields; ++x)
		fields[x] = pointer[x];
	return (uint64_t)(new AST(tag, nullptr, fields[0], fields[1], fields[2], fields[3])); //TODO: we set the previous pointer as nullptr.
}