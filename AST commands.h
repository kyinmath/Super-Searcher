#include <string>
#include <cstdint>

#ifdef _MSC_VER
#define constexpr
#endif


struct AST_info
{
	char* name;
	uint64_t number_of_AST_fields; //how many elements we want to compile

	//for just a series of ASTs
	constexpr AST_info(char a[], unsigned number_of_AST_pointers = 0) : name(a), number_of_AST_fields(number_of_AST_pointers)
	{
	}
};
constexpr uint64_t ASTn(const char AST_name[]);
extern AST_info AST_vector[];