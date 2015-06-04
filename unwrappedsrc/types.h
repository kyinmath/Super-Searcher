/*
A "user" will be defined as a program that is interpreted by the CS11 backend. That is, the "user" is the program itself, and not a human.
Apart from minor types like pointers and integers, there are two heavy-duty types that a user can see - Types, and ASTs.

A Type object consists of a tag and two fields, and describes another object. The two fields depend on the tag. For example, if the tag is "pointer", then the first field will be a pointer to the Type of the object that is being pointed to, and the second field is nullptr. If the tag is "integer", then both fields are nullptr. If the tag is "AST", then the first field will be a pointer to the Type of the return object, and the second field will be a pointer to the Type of the parameter object.
For large structures, the "concatenate" Type is used. A structure consisting of two elements will be described by a "concatenate" Type, which points its two fields at the two elements.
If we need a structure consisting of three elements, we use a "concatenate" Type for the first two elements. Then, we have another "concatenate" Type with its first field pointing to the first concatenate, and its second field pointing to the third element. In this way, we get a binary tree. The overall Type of the structure is represented the root node, which contains pointers to the remaining parts of the Type.

An AST object represents an expression, and consists of:
1. "tag", signifying what type of AST it is.
2. "preceding_BB_element", which is a pointer that points to the previous element in its basic block.
3. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all. For example, the "add" AST has two non-zero fields, which should be pointers to ASTs that return integers. The "if" AST takes three pointers to ASTs, which are a condition AST, a then-statement AST, and an if-statement AST. The maximum number of fields is four; unneeded fields can be left blank.
Each AST can be compiled by itself, as long as it is wrapped in a function. This function-wrapping currently happens automatically, so ASTs are compiled directly. Since ASTs have pointers to previous ASTs using the "preceding_BB_element" pointer, the compiler is simply directed to the last AST, and then the compiler finds the previous ASTs automatically. The last AST becomes the return statement. Thus, the basic block structure is embedded in the ASTs as a linked list, by means of the "preceding_BB_element" pointer.

Descriptions of the ASTs and Types are in AST_descriptor[] and Type_descriptor[], respectively. First, we should note that that AST_descriptor is of type "AST_info". "AST_info" is a special enum-like class. Its constructor takes in a string as its first argument, which is the enum name. The second constructor argument is the return type. The remaining four arguments are the parameter types for the AST's four fields; unused fields can be left blank.
What AST_descriptor[] does is utilize this constructor to build a rich description of each AST. For example, one of the elements in AST_descriptor[] is { "add", T::integer, T::integer, T::integer }. This tells us that there's an AST whose name is "add" (the first argument). Moreover, it returns an integer (the second argument), and it takes two fields whose return Types should be integers (the third and fourth arguments).

Type_descriptor[] works similarly, using the enum-like class Type_info. Looking at the constructor for Type_info, we see that the first argument is the type name, the second type is the number of pointer fields each Type requires, and the third type is the size of the object that the Type describes.
For example, { "cheap pointer", 1, 1 } tells us that there is a Type that has name "cheap pointer" (the first argument), and its first field is a pointer to a Type that describes the object that is being pointed to (the second argument), and the size of the pointer is 1 (the third argument).

The class AST_info has some other functions, which are mainly for special cases. For example, the "if" AST should not have its fields automatically converted to IR, because it needs to build basic blocks before it can emit the IR in the correct basic block. Moreover, its return value is not known beforehand - it depends on its "then" and "else" fields. Therefore, its return value is "T::special", indicating that it cannot be treated in the usual way. Since its fields cannot be automatically compiled, its parameter types are left blank, and the function "make_pointer_fields" is applied instead. This says that the fields are there, but that IR should not be automatically generated for them.

Later, we'll have some ASTs that let the user actually query this information.
*/

#pragma once
#include <cstdint>
#include <array>
#include <llvm/Support/ErrorHandling.h>
#include "errorhandling.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
//why is this necessary? what is setting NDEBUG

template<typename target_type>
union int_or_ptr
{
	uint64_t num;
	target_type* ptr;
	constexpr int_or_ptr(uint64_t x) : num(x) {}
	constexpr int_or_ptr(target_type* x) : ptr(x) {}
};


//Visual Studio doesn't support constexpr. we use this to make the Intellisense errors go away.
#ifdef _MSC_VER
#define constexpr
#endif

//for whatever reason, strcmp is not constexpr. so we do this
constexpr bool static_strequal(const char str1[], const char str2[])
{ return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false; }

struct Type_info
{
	const char* name;
	const unsigned pointer_fields; //how many field elements of the type object must be pointers

	//size of the actual object. -1 if special
	const int size;

	constexpr Type_info(const char a[], unsigned n, int s) : name(a), pointer_fields(n), size(s) {}
};

/* Guidelines for new Types:
you must create an entry in type_check.
*/
constexpr Type_info Type_descriptor[] =
{
	{ "concatenate", 2, -1 }, //concatenate two types
	{ "integer", 0, 1 }, //64-bit integer
	{ "cheap pointer", 1, 1 }, //pointer to anything
	{ "dynamic pointer", 0, 2 }, //dynamic pointer. first field is the pointer, second field is a pointer to the type
	{ "AST pointer", 0, 1 }, //just a pointer. (a full pointer)
	//the actual object has 6: tag, then previous, then 4 fields.
	{ "function in clouds", 2, 2 }, //points to: return AST, then parameter AST, then compiled area. we don't embed the AST along with the function signature, in order to keep them separate.
	{ "nonexistent", 0, 0 }, //a special value
	{ "never reached", 0, 0 },
	//except: if we have two ASTs, it's going to bloat our type object. and meanwhile, we want the parameter AST to be before everything that might use it, so having it as a first_pointer is not good, since the beginning of the function might change.
	{ "lock", 0, 1 },
	{ "type", 0, 3 },
};

#include <iostream>
template<class X, X vector_name[]> constexpr uint64_t get_enum_from_name(const char name[])
{
	for (unsigned k = 0; 1; ++k)
	{
		if (static_strequal(vector_name[k].name, name)) return k;
		if (static_strequal(vector_name[k].name, "never reached")) //this isn't recursive, because the previous if statement returns.
			error(string("tried to get a nonexistent name") + name);
		//llvm_unreachable doesn't give proper errors for run time mistakes, only when it's compile time.

	}
}
constexpr uint64_t Typen(const char name[])
{ return get_enum_from_name<const Type_info, Type_descriptor>(name); }

#define fields_in_Type 2u
struct Type
{
	uint64_t tag;
	using iop = int_or_ptr<Type>;
	std::array<iop, fields_in_Type> fields;
	constexpr Type(const char name[], const iop a = nullptr, const iop b = nullptr) : tag(Typen(name)), fields{ a, b } {}
	constexpr Type(const uint64_t t, const iop a = nullptr, const iop b = nullptr) : tag(t), fields{ a, b } {}
};
#define max_fields_in_AST 4u
//should accomodate the largest possible AST
struct AST;

namespace T
{
	namespace internal
	{
		static constexpr Type int_("integer");
		static constexpr Type nonexistent("nonexistent");
		static constexpr Type missing_field("nonexistent");
		static constexpr Type special("integer");
		static constexpr Type dynamic_pointer("dynamic pointer");
		static constexpr Type AST_pointer("AST pointer");
		static constexpr Type conca1("concatenate", const_cast<Type* const>(&int_), const_cast<Type* const>(&AST_pointer));
		static constexpr Type error_object("concatenate", const_cast<Type* const>(&conca1), const_cast<Type* const>(&int_));
		//error_object is int, pointer to AST, int. it's what is returned when compilation fails: the error code, then the AST, then the field.
	}
	namespace i = internal;
	constexpr Type* nonexistent = const_cast<Type* const>(&i::nonexistent); //nothing at all. used for goto. effectively disables type checking for that field.
	constexpr Type* missing_field = const_cast<Type* const>(&i::missing_field); //used to say that a parameter field is missing
	constexpr Type* special = const_cast<Type* const>(&i::special); //indicates that a return type is to be handled differently
	constexpr Type* integer = const_cast<Type* const>(&i::int_); //describes an integer type
	constexpr Type* dynamic_pointer = const_cast<Type* const>(&i::dynamic_pointer);
	constexpr Type* null = nullptr;
	constexpr Type* error_object = const_cast<Type* const>(&i::error_object);
	constexpr Type* AST_pointer = const_cast<Type* const>(&i::AST_pointer);
	//later: noreturn. for goto.
};


/*
all object sizes are integer multiples of 64 bits.
this function returns the size of an object as (number of bits/64).
thus, a uint64 has size "1". a struct of two uint64s has size "2". etc.
*/
constexpr uint64_t get_size(Type* target)
{
	if (target == nullptr)
		return 0;
	if (Type_descriptor[target->tag].size != -1) return Type_descriptor[target->tag].size;
	else if (target->tag == Typen("concatenate"))
	{
		return get_size(target->fields[0].ptr) + get_size(target->fields[1].ptr);
	}
	error("couldn't get size of type tag, check backtrace for target->tag");
}

/**
this is a cool C++ hack to attach information to enums.
first, we construct a vector, where each element has a string and some information attached (in the form of integers)
then, get_enum_from_name() associates numbers to the strings.
this lets us switch-case on the strings that are the first element of each enum. since the information is attached to the string, we are guaranteed not to forget to implement it, which reduces the potential for careless errors.
the total number of fields for the AST is pointer_fields + additional_special_fields.
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
	//number_to_compile < pointer_fields, so this also forces pointer_fields upward if it is too low. by default, they are equal.
	constexpr AST_info make_fields_to_compile(int x)
	{
		AST_info new_copy(*this);
		new_copy.fields_to_compile = x;
		if (new_copy.pointer_fields <= new_copy.fields_to_compile)
			new_copy.pointer_fields = new_copy.fields_to_compile;
		return new_copy;
	}

	unsigned pointer_fields; //how many field elements are pointers.
	//for ASTs, this means pointers to other ASTs. for Types, this means pointers to other Types.
	//this forces fields_to_compile downward if it is too high. by default, they are equal.
	constexpr AST_info make_pointer_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.pointer_fields = x;
		if (new_copy.pointer_fields <= new_copy.fields_to_compile)
			new_copy.fields_to_compile = new_copy.pointer_fields;
		return new_copy;
	}

	unsigned additional_special_fields = 0; //these fields come after the pointer fields. they are REAL types, not AST return types.
	//for example, if you have a Type* in the second field and a normal AST* in the first field, then this would be 1.
	//then, in the argument list, you'd put a Type* in the second parameter slot.

	//this reduces the number of pointer fields, because those are special fields instead.
	constexpr AST_info make_special_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.additional_special_fields = x;
		return new_copy.make_pointer_fields(pointer_fields - x);
	}

	//unsigned non_pointer_fields; //how many fields are not pointers. but maybe not necessary.
	constexpr int field_count(Type* f1, Type* f2, Type* f3, Type* f4)
	{
		int number_of_fields = 4; //by default, both pointer_fields and number_of_fields will be equal to this.
		if (f4 == T::missing_field) number_of_fields = 3;
		if (f3 == T::missing_field) number_of_fields = 2;
		if (f2 == T::missing_field) number_of_fields = 1;
		if (f1 == T::missing_field) number_of_fields = 0;
		return number_of_fields;
	};

	//in AST_descriptor[], fields_to_compile and pointer_fields are normally set to the number of parameter types specified.
	//	however, they can be overridden by make_fields_to_compile() and make_pointer_fields()
	constexpr AST_info(const char a[], Type* r, Type* f1 = T::missing_field, Type* f2 = T::missing_field, Type* f3 = T::missing_field, Type* f4 = T::missing_field)
		: name(a), return_object(r), parameter_types{ f1, f2, f3, f4 }, pointer_fields(field_count(f1, f2, f3, f4)), fields_to_compile(field_count(f1, f2, f3, f4)), size_of_return(get_size(r)) { }

	/*
	template<typename T, typename... Args>
	constexpr AST_info(const char a[], Type* r, Type* f1, Args... args) // recursive variadic function
	{
		std::cout << t << std::endl;

		func(args...);
	}
	*/
};

using a = AST_info;
/*this is an enum which has extra information for each element. it is constexpr so that it can be used in a switch-case statement.

Guidelines for new ASTs:
first field is the AST name. second field is the return type. remaining optional fields are the parameter types.
return type is T_special if it can't be determined automatically from the AST tag. that means a deeper inspection is necessary.
then, write the compilation code for the AST in generate_IR().

the number of parameter types determines the number of subfields to be compiled and type-checked automatically.
	if you want to compile but not type-check a field, use make_fields_to_compile() and do not list a parameter type.
	if you want to neither compile or type-check a field, use make_pointer_fields().
		in these cases, you must handle the type-checking/compilation manually if it's not done automatically and you want it to be done.
if the AST branches, then make sure to clear temporaries from the stack list before running the other branch.
keep AST names to one word only, because our console input takes a single word for the name.
*/
constexpr AST_info AST_descriptor[] =
{
	a("integer", T::integer, T::integer).make_special_fields(1), //first argument is an integer, which is the returned value.
	{ "hello", T::null },
	a("if", T::special).make_pointer_fields(3), //test, first branch, fields[0] branch. passes through the return object of each branch; the return objects must be the same.
	a("scope", T::null).make_fields_to_compile(1), //fulfills the purpose of {} from C++
	{ "add", T::integer, T::integer, T::integer }, //adds two integers
	{ "subtract", T::integer, T::integer, T::integer },
	{ "random", T::integer }, //returns a random integer
	a("pointer", T::special).make_pointer_fields(1), //creates a pointer to an alloca'd element. takes a pointer to the AST, but does not compile it - instead, it searches for the AST pointer in <>objects.
	a("load", T::special).make_pointer_fields(1), //creates a temporary copy of an element. takes one field, but does NOT compile it.
	a("concatenate", T::special).make_pointer_fields(2),
	a("dynamic", T::dynamic_pointer).make_fields_to_compile(1), //creates dynamic storage for any kind of object. moves it to the heap.
	a("compile", T::dynamic_pointer, T::AST_pointer), //compiles an AST, returning a dynamic object which is either the error object or the desired info.
	a("temp_generate_AST", T::AST_pointer), //hacked in, generates a random AST.
	{ "dynamic_conc", T::dynamic_pointer, T::dynamic_pointer, T::dynamic_pointer }, //concatenate the interiors of two dynamic pointers
	a("goto", T::nonexistent).make_pointer_fields(1),
	{ "label", T::null },
	a("convert_to_AST", T::AST_pointer, T::integer, T::dynamic_pointer, T::nonexistent).make_fields_to_compile(2), //don't compile the nonexistent branch, because it needs to go in a special basic block.
	{ "never reached", T::special }, //marks the end of the currently-implemented ASTs. beyond this is rubbish.
	{ "dereference pointer", T::special}, //????
	a("store", T::special), //????
	/*	
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

constexpr uint64_t ASTn(const char name[])
{ return get_enum_from_name<const AST_info, AST_descriptor>(name); }

struct AST
{
	//std::mutex lock;
	uint64_t tag;
	AST* preceding_BB_element; //this object survives on the stack and can be referenced. it's the previous basic block element.
	using iop = int_or_ptr<AST>;
	std::array<iop, max_fields_in_AST> fields;
	AST(const char name[], AST* preceding = nullptr, iop f1 = nullptr, iop f2 = nullptr, iop f3 = nullptr, iop f4 = nullptr)
		: tag(ASTn(name)), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {} //VS complains about aggregate initialization, but it is wrong.
	AST(unsigned direct_tag, AST* preceding = nullptr, iop f1 = nullptr, iop f2 = nullptr, iop f3 = nullptr, iop f4 = nullptr)
		: tag(direct_tag), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {}
	//watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};


constexpr uint64_t get_size(AST* target)
{
	if (target == nullptr)
		return 0; //for example, if you try to get the size of an if statement with nullptr fields as the return object.
	if (AST_descriptor[target->tag].return_object != T::special) return get_size(AST_descriptor[target->tag].return_object);
	else if (target->tag == ASTn("if")) return get_size(target->fields[1].ptr);
	else if (target->tag == ASTn("pointer")) return 1;
	else if (target->tag == ASTn("load")) return get_size(target->fields[0].ptr);
	else if (target->tag == ASTn("concatenate")) return get_size(target->fields[0].ptr) + get_size(target->fields[1].ptr); //todo: this is slow. takes n^2 time.
	error(string("couldn't get size of tag in get_size(), target->tag was") + AST_descriptor[target->tag].name);
}


enum type_status { RVO, reference }; //distinguishes between RVOing an object, or just creating a reference

unsigned type_check(type_status version, Type* existing_reference, Type* new_reference);

extern bool is_AST_user_facing(uint64_t tag, uint64_t reference);