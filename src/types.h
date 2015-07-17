/*
A "user" will be defined as a program that is interpreted by the CS11 backend. That is, the "user" is the program itself, and not a human.
Apart from minor types like pointers and integers, there are two heavy-duty types that a user can see - Types, and ASTs.

A Type object consists of a tag and two fields, and describes another object. The two fields depend on the tag. For example, if the tag is "pointer", then the first field will be a pointer to the Type of the object that is being pointed to, and the second field is nullptr. If the tag is "integer", then both fields are nullptr. If the tag is "AST", then the first field will be a pointer to the Type of the return object, and the second field will be a pointer to the Type of the parameter object.
For large structures, the "con_vec" Type is used. A structure consisting of three elements will be described by a "con_vec" Type, which has four fields: 3, Type*, Type*, Type*. The "3" says that there are 3 elements. The subfield of a con_vec type cannot be another con_vec.
The subfield of a pointer cannot be nullptr.

An AST object represents an expression, and consists of:
1. "tag", signifying what type of AST it is.
2. "preceding_BB_element", which is a pointer that points to the previous element in its basic block.
3. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all. For example, the "add" AST has two non-zero fields, which should be pointers to ASTs that return integers. The "if" AST takes three pointers to ASTs, which are a condition AST, a then-statement AST, and an if-statement AST. The maximum number of fields is four; unneeded fields can be left blank.
Each AST can be compiled by itself, as long as it is wrapped in a function. This function-wrapping currently happens automatically, so ASTs are compiled directly. Since ASTs have pointers to previous ASTs using the "preceding_BB_element" pointer, the compiler is simply directed to the last AST, and then the compiler finds the previous ASTs automatically. The last AST becomes the return statement. Thus, the basic block structure is embedded in the ASTs as a linked list, by means of the "preceding_BB_element" pointer.

Descriptions of the ASTs and Types are in AST_descriptor[] and Type_descriptor[], respectively. First, we should note that that AST_descriptor is of type "AST_info". "AST_info" is a special enum-like class. Its constructor takes in a string as its first argument, which is the enum name. The second constructor argument is the return type. The remaining four arguments are the parameter types for the AST's four fields; unused fields can be left blank.
What AST_descriptor[] does is utilize this constructor to build a rich description of each AST. For example, one of the elements in AST_descriptor[] is { "add", T::integer, T::integer, T::integer }. This tells us that there's an AST whose name is "add" (the first argument). Moreover, it returns an integer (the second argument), and it takes two fields whose return Types should be integers (the third and fourth arguments).

Type_descriptor[] works similarly, using the enum-like class Type_info. Looking at the constructor for Type_info, we see that the first argument is the type name, the second type is the number of pointer fields each Type requires, and the third type is the size of the object that the Type describes.
For example, { "cheap pointer", 1, 1 } tells us that there is a Type that has name "cheap pointer" (the first argument), and its first field is a pointer to a Type that describes the object that is being pointed to (the second argument), and the size of the pointer is 1 (the third argument).

The class AST_info has some other functions, which are mainly for special cases. For example, the "if" AST should not have its fields automatically converted to IR, because it needs to build basic blocks before it can emit the IR in the correct basic block. Moreover, its return value is not known beforehand - it depends on its "then" and "else" fields. Therefore, its return value is "special_return", indicating that it cannot be treated in the usual way. Since its fields cannot be automatically compiled, its parameter types are left blank, and the function "make_pointer_fields" is applied instead. This says that the fields are there, but that IR should not be automatically generated for them.

Later, we'll have some ASTs that let the user actually query this information.
*/

#pragma once
#include <cstdint>
#include <array>
#include <llvm/ADT/ArrayRef.h>
#include "globalinfo.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
//why is this necessary? what is setting NDEBUG

template<typename target_type>
union int_or_ptr
{
	uint64_t num;
	target_type* ptr;
	constexpr int_or_ptr() : num(0) {}
	constexpr int_or_ptr(uint64_t x) : num(x) {}
	constexpr int_or_ptr(target_type* x) : ptr(x) {}
};


//Visual Studio doesn't support constexpr. we use this to make the Intellisense errors go away.
//don't mark it const, because Visual Studio starts complaining about const correctness.
#ifdef _MSC_VER
#define constexpr
#endif

//for whatever reason, strcmp is not constexpr. so we do this
constexpr bool static_strequal(const char str1[], const char str2[])
{ return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false; }

struct Type_info
{
	const char* name;
	const uint64_t pointer_fields; //how many field elements of the type object must be pointers

	//size of the actual object. ~0ull if special
	const uint64_t size;

	constexpr Type_info(const char a[], uint64_t n, uint64_t s) : name(a), pointer_fields(n), size(s) {}
};

constexpr uint64_t minus_one = ~0ull; //note that a -1 literal, without specifying ll, can either be int32 or int64, so we could be in great trouble.

/* Guidelines for new Types:
you must create an entry in type_check.
marky_mark.
dynamic offset AST.
*/
constexpr Type_info Type_descriptor[] =
{
	{"con_vec", minus_one, minus_one}, //concatenate a vector of types. the first field is the size of the array, so there are (fields[0] + 1) total fields. requires at least two types.
	{"integer", 0, 1}, //64-bit integer
	{"pointer", 1, 1}, //nullptr prohibited
	{"dynamic pointer", 0, 2}, //dynamic pointer. first field is type, second field object. if either is null, both are null together. this order fixes the type position, because it must always be read first, even if we optimize the dynamic object to be an array occasionally, such as with ASTs
	{"AST pointer", 0, 1}, //can be nullptr. the actual object has tag, then previous, then some fields.
	{"type pointer", 0, 1}, //can be nullptr. actual object is tag + fields.
	{"function pointer", 0, 1}, //can be nullptr. the purpose of not specifying the return/parameter type is given in "doc/AST pointers"
	{"does not return", 0, 0}, //a special value
	{"never reached", 0, 0},
	//we want the parameter AST to be before everything that might use it, so having it as a first_pointer is not good, since the beginning of the function might change.
};

//gcc 5 doesn't support loops in constexpr
#if __GNUC__ == 5
template<class X, X vector_name[]> constexpr uint64_t get_enum_from_name(const char name[], uint64_t number)
{
	if (static_strequal(vector_name[number].name, name)) return number;
	else if (static_strequal(vector_name[number].name, "never reached")) //this isn't recursive, because the previous if statement returns.
		error(string("tried to get a nonexistent name: ") + name);
	else return get_enum_from_name<X, vector_name>(name, number + 1);
}

template<class X, X vector_name[]> constexpr uint64_t get_enum_from_name(const char name[])
{
	return get_enum_from_name<X, vector_name>(name, 0);
}
#else
template<class X, X vector_name[]> constexpr uint64_t get_enum_from_name(const char name[])
{
	for (uint64_t k = 0; 1; ++k)
	{
		if (static_strequal(vector_name[k].name, name)) return k;
		if (static_strequal(vector_name[k].name, "never reached")) //this isn't recursive, because the previous if statement returns.
			error(string("tried to get a nonexistent name: ") + name);
		//llvm_unreachable doesn't give proper errors for run time mistakes, only when it's compile time.
	}
}
#endif
constexpr uint64_t Typen(const char name[]) { return get_enum_from_name<const Type_info, Type_descriptor>(name); }

#define fields_in_Type 3u
//this is for types which are known to be unique and well-behaved (no loops).
//WARNING: don't use the ctor or copy ctor for creating user objects! use the copy_type() and new_type() functions instead, which handle sizes correctly.
//the Type does not properly represent its true size.
//todo: add const to all of these things. and to AST tag as well.
struct Type;
inline uint64_t total_valid_fields(const Type* t);
struct Type
{
	uint64_t tag;
	using iop = int_or_ptr<Type>;
	std::array<iop, fields_in_Type> fields;

	//these ctors are necessary for the constexpr types.
	template<typename... Args> constexpr Type(const char name[], Args... args) : tag(Typen(name)), fields{{args...}} { if (tag == Typen("con_vec")) error("make it another way"); }
private:
	Type(const Type& other) = delete;
};
#define max_fields_in_AST 40u
//should accomodate the largest possible AST. necessary for AST_descriptor[]
struct Type_pointer_range
{
	Type* t;
	Type_pointer_range(Type* t_) : t(t_) {}
	Type** begin() { return (t->tag == Typen("con_vec")) ? &(t->fields[1].ptr) : &(t->fields[0].ptr); }
	Type** end() { return (t->tag == Typen("con_vec")) ? &(t->fields[1].ptr) + (uint64_t)t->fields[0].num :
		&(t->fields[0].ptr) + Type_descriptor[t->tag].pointer_fields;
	}
};

//range operator. gets all the fields, but not the tag.
struct Type_everything_range
{
	Type* t;
	Type_everything_range(const Type* t_) : t((Type*)t_) {}
	uint64_t tag;
	Type* fields;
	Type** begin() { return &(t->fields[0].ptr); }
	Type** end() { return &(t->fields[0].ptr) + total_valid_fields(t);
	}
};

inline uint64_t total_valid_fields(const Type* t)
{
	return (t->tag == Typen("con_vec")) ? t->fields[0].num + 1 : Type_descriptor[t->tag].pointer_fields;
}

#include "memory.h"
//warning: takes fields directly. so concatenate should worry.
inline Type* new_type(uint64_t tag, llvm::ArrayRef<Type*> fields)
{
	uint64_t total_field_size = (tag == Typen("con_vec")) ? (uint64_t)fields[0] + 1 : Type_descriptor[tag].pointer_fields;
	uint64_t* new_home = allocate(total_field_size + 1);
	new_home[0] = tag;
	for (uint64_t x = 0; x < total_field_size; ++x)
		new_home[x + 1] = (uint64_t)fields[x];
	return (Type*)new_home;
}

inline Type* copy_type(const Type* t)
{
	uint64_t fields = total_valid_fields(t);
	uint64_t* new_type = allocate(fields + 1);
	uint64_t* old_type = (uint64_t*)t;
	for (uint64_t idx = 0; idx < fields + 1; ++idx)
		new_type[idx] = old_type[idx];
	return (Type*)(new_type);
}

/*when creating a new element here:
1. instantiate the T:: version in types.cpp.
2. make a u:: version just below
3. instantiate the u::version in memory.cpp
4. add the u::version to the gc roots in memory.cpp
the reason these are static members of struct internal is so that the address is the same across translation units.
otherwise, it's not. see https://stackoverflow.com/questions/7368330/static-members-and-the-default-constructor-c

T::type are all internal types to fill out the constexpr vector. don't let the user see any T::type! instead, use the u::type versions, which are uniqued versions.
*/
namespace T
{
	struct internal
	{
		static constexpr Type int_{"integer"};
		static constexpr Type does_not_return{"does not return"};
		static constexpr Type dynamic_pointer{"dynamic pointer"};
		static constexpr Type type{"type pointer"};
		static constexpr Type AST_pointer{"AST pointer"};
		static constexpr Type function_pointer{"function pointer"};
		static constexpr Type type_pointer{"type pointer"};
		//static constexpr Type error_object{concatenate_types(std::vector<Type*>{const_cast<Type* const>(&int_), const_cast<Type* const>(&AST_pointer), const_cast<Type* const>(&int_)})};
		//error_object is int, pointer to AST, int. it's what is returned when compilation fails: the error code, then the AST, then the field.
	};
	typedef internal i;
	constexpr Type* does_not_return = const_cast<Type* const>(&i::does_not_return);  //the field can't return. used for goto. effectively makes type checking always pass.
	constexpr Type* integer = const_cast<Type* const>(&i::int_); //describes an integer type
	constexpr Type* dynamic_pointer = const_cast<Type* const>(&i::dynamic_pointer);
	constexpr Type* type = const_cast<Type* const>(&i::type);
	constexpr Type* AST_pointer = const_cast<Type* const>(&i::AST_pointer);
	constexpr Type* function_pointer = const_cast<Type* const>(&i::function_pointer);
	constexpr Type* type_pointer = const_cast<Type* const>(&i::type_pointer);
	constexpr Type* null = nullptr;
};

//the actual objects are placed in type_creator.cpp, to avoid static initialization fiasco
namespace u
{
	extern Type* does_not_return;
	extern Type* integer;
	extern Type* dynamic_pointer;
	extern Type* type;
	extern Type* AST_pointer;
	extern Type* function_pointer;
	extern Type* type_pointer;
	constexpr Type* null = nullptr;
};
Type* concatenate_types(llvm::ArrayRef<Type*> components);

/*
all object sizes are integer multiples of 64 bits.
this function returns the size of an object as (number of bits/64).
thus, a uint64 has size "1". a struct of two uint64s has size "2". etc.
there can't be loops because Types are immut. the user doesn't have access to Type creation functions.
	we'll mark this as the "good" function. it takes in Types that are known to be unique.
*/
constexpr uint64_t get_size(Type* target) __attribute__((pure));
constexpr uint64_t get_size(Type* target)
{
	if (target == nullptr) return 0;
	else if (Type_descriptor[target->tag].size != minus_one) return Type_descriptor[target->tag].size;
	else if (target->tag == Typen("con_vec"))
	{
		check(target->fields[0].num > 1, "concatenation with insufficient types"); //this is a meaningful check because pointer to nowhere gives tag = 0 (concat), fields = 0.
		uint64_t total_size = 0;
		for (auto& x : Type_pointer_range(target)) total_size += get_size(x);
		return total_size;
	}
	error("couldn't get size of type tag, check backtrace for target->tag");
}

//finds one element in a concatenate. the size offset is returns in return_offset. the size of the object is returned normally.
inline uint64_t get_size_conc(Type* target, uint64_t offset, uint64_t* return_offset) __attribute__((pure));
inline uint64_t get_size_conc(Type* target, uint64_t offset, uint64_t* return_offset)
{
	check(target != nullptr, "conc");
	check(target->tag == Typen("con_vec"), "need conc");
	check(target->fields[0].num > offset, "error: you must check that there are enough types in the conc to handle the offset beforehand");
	uint64_t initial_offset = 0;
	for (uint64_t idx = 0; idx < offset; ++idx)
		initial_offset += get_size(target->fields[idx + 1].ptr);
	*return_offset = initial_offset;
	return get_size(target->fields[offset + 1].ptr);
}


enum type_status { RVO, reference }; //distinguishes between RVOing an object, or just creating a reference


//from experience, we can't have the return value automatically work as a bool, because that causes errors.
//note that if it's a perfect fit, the sizes may still be different, because of u::nonexistent.
enum class type_check_result
{
	different,
	new_reference_too_large,
	existing_reference_too_large,
	perfect_fit
};

//includes the tag in the result type.
inline Type* get_Type_full_type(Type* t)
{
	std::vector<Type*> fields{u::integer}; //the tag
	if (t->tag != Typen("con_vec"))
	{
		uint64_t number_of_pointers = Type_descriptor[t->tag].pointer_fields;
		for (uint64_t x = 0; x < number_of_pointers; ++x) fields.push_back(u::type);
		return concatenate_types(fields);
	}
	else
	{
		fields.push_back(u::integer);
		uint64_t number_of_pointers = t->fields[0].num;
		for (uint64_t x = 0; x < number_of_pointers; ++x) fields.push_back(u::type);
		return concatenate_types(fields);
	}
}


void debugtypecheck(Type* test);
type_check_result type_check(type_status version, Type* existing_reference, Type* new_reference);
extern Type* concatenate_types(llvm::ArrayRef<Type*> components);