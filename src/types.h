#pragma once
#include <cstdint>
#include <array>
#include <llvm/Support/ErrorHandling.h>

template<typename target_type>
union int_or_ptr
{
	uint64_t num;
	target_type* ptr;
	constexpr int_or_ptr(uint64_t x) : num(x) {}
	constexpr int_or_ptr(target_type* x) : ptr(x) {}
};

#ifdef _MSC_VER
#define constexpr
#endif

struct Type;
extern Type T_int_internal;
extern Type T_nonexistent_internal;
extern Type T_special_internal;
constexpr Type* T_nonexistent = &T_nonexistent_internal; //nothing at all. used for ctors for parameter fields, or for goto.
constexpr Type* T_special = &T_special_internal; //indicates that a return type is to be handled differently
constexpr Type* T_int = &T_int_internal; //describes an integer type
constexpr Type* T_null = nullptr;

#define max_fields_in_AST 4u
//should accomodate the largest possible AST
struct AST;


/**
this is a cool C++ hack to attach information to enums.
first, we construct a vector with strings and integers (information).
then, get_enum_from_name() associates numbers to the strings.
this lets us switch-case on the strings.
since the information is in the vector, we are guaranteed not to forget it.
this reduces the potential for careless errors.
*/
struct AST_info
{
	const char* name;

	//in ASTs, this is the size of the return object. -1 if it must be handled in a special way.
	//in the case of types, the size of the actual object.
	const int size_of_return;

	Type* parameter_types[4];
	Type* return_object; //if this type is null, do not check it using the generic method - check it specially.

	unsigned fields_to_compile; //we may not always compile all pointers, such as with "copy".
	//number_to_compile < pointer_fields. by default, they are equal.
	constexpr AST_info set_fields_to_compile(int x)
	{
		AST_info new_copy(*this);
		new_copy.fields_to_compile = x;
		if (new_copy.pointer_fields <= new_copy.fields_to_compile)
			new_copy.pointer_fields = new_copy.fields_to_compile;
		return new_copy;
	}

	unsigned pointer_fields; //how many field elements are pointers.
	//for ASTs, this means pointers to other ASTs. for Types, this means pointers to other Types.
	constexpr AST_info set_pointer_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.pointer_fields = x;
		return new_copy;
	}
	//unsigned non_pointer_fields; //how many fields are not pointers. but maybe not necessary.
	constexpr int field_count(Type* f1, Type* f2, Type* f3, Type* f4)
	{
		int number_of_fields = 4; //by default, both pointer_fields and number_of_fields will be equal to this.
		if (f4 == nullptr)
			number_of_fields = 3;
		if (f3 == nullptr)
			number_of_fields = 2;
		if (f2 == nullptr)
			number_of_fields = 1;
		if (f1 == nullptr)
			number_of_fields = 0;
		return number_of_fields;
	};

	constexpr AST_info(const char a[], Type* r, Type* f1 = nullptr, Type* f2 = nullptr, Type* f3 = nullptr, Type* f4 = nullptr)
		: name(a), return_object(r), parameter_types{ f1, f2, f3, f4 }, pointer_fields(field_count(f1, f2, f3, f4)), fields_to_compile(field_count(f1, f2, f3, f4)), size_of_return(1)
	{
		//TODOOOOO. don't leave size_of_return as 1! it's wrong. we need a get_type_size() function
	}
};

//TODO: our types.h is in the middle of changing. we want to automatically process parameter fields of ASTs. however, scope() can take any parameter type, so we want it to be treated specially. how?
//the problem is that its return value is regular, but its parameters are not.

using a = AST_info;
//this is an enum which has extra information for each element. it is constexpr so that it can be used in a switch-case statement.
constexpr AST_info AST_descriptor[] =
{
	{ "integer", T_int}, //first argument is an integer, which is the returned value.
	{ "Hello World!", T_null},
	a("if", T_special).set_pointer_fields(3), //test, first branch, fields[0] branch. passes through the return object of each branch; the return objects must be the same.
	a("scope", T_null).set_fields_to_compile(1), //fulfills the purpose of {} from C++
	{ "add", T_int, T_int, T_int }, //adds two integers
	{ "subtract", T_int, T_int, T_int },
	{ "random", T_int}, //returns a random integer
	a("pointer", T_special).set_pointer_fields(1), //creates a pointer to an alloca'd element. takes a pointer to the AST, but does not compile it - instead, it searches for the AST pointer in <>objects.
	a("copy", T_special).set_pointer_fields(1), //creates a copy of an element. takes one field, but does NOT compile it.
	{ "never reached", T_special }, //marks the end of the currently-implemented ASTs. beyond this is rubbish.
	//{ "dereference pointer", 0, 1 },
	a( "concatenate", T_special).set_fields_to_compile(2) ,
	/*	{ "goto", 2 }, //label and failure branch
	{ "label" },
	{ "no op", 0, 0 },
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
	{ "bitwise xor", 2 },*/
};

struct type_info
{
	const char* name;
	const unsigned pointer_fields; //how many field elements must be pointers

	//size of the actual object. -1 if special
	const int size;

	constexpr type_info(const char a[], unsigned n, int s)
		: name(a), pointer_fields(n), size(s) {}
};

constexpr type_info type_descriptor[] =
{
	{ "concatenate", 2, -1 }, //concatenate two types
	{ "integer", 0, 1 }, //64-bit integer
	{ "fixed integer", 1, 1 }, //64-bit integer whose value is fixed by the type.
	{ "cheap pointer", 1, 1 }, //pointer to anything
	//TODO: ASTs, locks, types
	{ "never reached", 0, 0 }
};

//for whatever reason, strcmp is not constexpr. so we do this
constexpr bool static_strequal(const char str1[], const char str2[])
{ return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false; }


template<constexpr type_info vector_name[]> constexpr uint64_t get_enum_from_name(const char name[])
{
	for (unsigned k = 0; 1; ++k)
	{
		if (static_strequal(vector_name[k].name, name)) return k;
		if (static_strequal(vector_name[k].name, "never reached")) //this isn't recursive, because the previous if statement returns.
			llvm_unreachable("tried to get a nonexistent name");
	}
}

constexpr uint64_t ASTn(const char name[])
{
	for (unsigned k = 0; 1; ++k)
	{
		if (static_strequal(AST_descriptor[k].name, name)) return k;
		if (static_strequal(AST_descriptor[k].name, "never reached")) //this isn't recursive, because the previous if statement returns.
			llvm_unreachable("tried to get a nonexistent name");
	}
}
constexpr uint64_t Typen(const char name[])
{ return get_enum_from_name<type_descriptor>(name); } //todo: templatize this so that ASTn also works.

struct Type
{
	uint64_t tag;
	std::array<int_or_ptr<Type>, 2> fields;
	constexpr Type(const char name[], const int_or_ptr<Type> a = nullptr, const int_or_ptr<Type> b = nullptr) : tag(Typen(name)), fields{ a, b } {}
	constexpr Type(const uint64_t t, const int_or_ptr<Type> a = nullptr, const int_or_ptr<Type> b = nullptr) : tag(t), fields{ a, b } {}
};


struct AST
{
	//std::mutex lock;
	uint64_t tag;
	AST* preceding_BB_element; //this object survives on the stack and can be referenced. it's the previous basic block element.
	std::array<int_or_ptr<AST>, max_fields_in_AST> fields;
	AST(const char name[], AST* preceding = nullptr, int_or_ptr<AST> f1 = nullptr, int_or_ptr<AST> f2 = nullptr, int_or_ptr<AST> f3 = nullptr, int_or_ptr<AST> f4 = nullptr)
		: tag(ASTn(name)), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {} //VS complains about aggregate initialization, but it is wrong.
	AST(unsigned direct_tag, AST* preceding = nullptr, int_or_ptr<AST> f1 = nullptr, int_or_ptr<AST> f2 = nullptr, int_or_ptr<AST> f3 = nullptr, int_or_ptr<AST> f4 = nullptr)
		: tag(direct_tag), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {}
	//watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};


enum type_status { RVO, reference }; //distinguishes between RVOing an object, or just creating a reference

unsigned type_check(type_status version, Type* existing_reference, Type* new_reference);