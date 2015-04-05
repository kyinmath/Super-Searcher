#include <string>
#include <cstdint>
#include <cassert>
#include "AST commands.h"
	//visual studio doesn't define constexpr

//must be a vector, because we have to return the position.
constexpr AST_info AST_vector[] =
{
	{ "static integer", 0 },
	{ "goto", 2 }, //label and failure branch
	{ "label" },
	{ "basic block" },
	{ "if", 3 }, //test, first branch, second branch
	{ "get 0" },
	{ "add", 2 }, //ints
	{ "sub", 2 },
	{ "mul", 2 },
	{ "divuu", 3 }, //2x int, subAST* on failure
	{ "divus", 3 },
	{ "divsu", 3 },
	{ "divss", 3 },
	{ "incr", 1 }, //int
	{ "decr", 1 },
	{ "less", 2 },
	{ "lteq", 2 },
	{ "less signed", 2 },
	{ "lteq signed", 2 },
	{ "equal", 2 },
	{ "logical not", 1 },
	{ "logical and", 2 },
	{ "logical or", 2 },
	{ "logical xor", 2 },
	{ "bitwise not", 1 },
	{ "bitwise and", 2 },
	{ "bitwise or", 2 },
	{ "bitwise xor", 2 },
};

constexpr uint64_t ASTn(const char AST_name[])
{
	unsigned k = 0;
	while (1)
	{
		if (AST_vector[k].name == AST_name)
			return k;
		assert(AST_vector[k].name != "never reached", "tried to get a nonexistent AST name");
	}
}

constexpr uint64_t ASTsize(uint64_t tag)
{
	return AST_vector[uint32_t(tag)].number_of_AST_fields;
}
