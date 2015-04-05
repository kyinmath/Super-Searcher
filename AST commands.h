#include <string>
#include <cstdint>
struct AST_info
{
	std::string name;
	uint64_t further_ASTs; //how many elements we want to compile

	//for just a series of ASTs
	constexpr AST_info(char a[], unsigned number_of_AST_pointers = 0) : name(a), further_ASTs(number_of_AST_pointers)
	{
	}
};
constexpr uint64_t ASTn(const char AST_name[]);