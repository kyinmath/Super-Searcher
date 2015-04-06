#include <string>
#include <cstdint>
#include <cassert>

#ifdef _MSC_VER
#define constexpr
#endif


struct AST_info
{
	const char* name;
	const uint64_t number_of_AST_fields; //how many elements we want to compile

	//for just a series of ASTs
	constexpr AST_info(const char a[], unsigned number_of_AST_pointers = 0) : name(a), number_of_AST_fields(number_of_AST_pointers)
	{
	}
};


//must be a vector, because we have to return the position.
constexpr AST_info AST_vector[] =
{
	{ "static integer"},
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
	{ "never reached", 0 }
};



constexpr uint64_t ASTsize(uint64_t tag)
{
	return AST_vector[uint32_t(tag)].number_of_AST_fields;
}

//for whatever reason, strcmp is not constexpr. so we do this
/*
constexpr bool static_strequal_helper(const char * a, const char * b, unsigned len) {
	return (len == 0) ? true : ((*a == *b) ? static_strequal_helper(a + 1, b + 1, len - 1) : false);
}

template <unsigned N1, unsigned N2>
constexpr bool static_strequal(const char(&str1)[N1], const char(&str2)[N2]) {
	return (N1 == N2) ? static_strequal_helper(&(str1[0]), &(str2[0]), N1) : false;
}*/
/*

constexpr bool static_strl(char const*s1, char const*s2)
{
return *s1 < *s2 || *s1 == *s2 && *s1 && static_strl(s1 + 1, s2 + 1);
}*/

constexpr bool static_strequal(const char str1[], const char str2[]) {
	return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false;
}

constexpr uint64_t ASTn(const char AST_name[])
{
	unsigned k = 0;
	while (1)
	{
		if (static_strequal(AST_vector[k].name, AST_name))
			return k;
		//static_assert(static_strequal("never reached", AST_name), "tried to get a nonexistent AST name"); //this fails, but I don't know why.
		++k;
	}
}