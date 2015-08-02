/*
A "user" will be defined as a program that is interpreted by the CS11 backend. That is, the "user" is the program itself, and not a human.
Apart from minor types like pointers and integers, there are two heavy-duty types that a user can see - Types, and ASTs.

A Type object consists of a tag and two fields, and describes another object. The two fields depend on the tag. For example, if the tag is "pointer", then the first field will be a pointer to the Type of the object that is being pointed to, and the second field is nullptr. If the tag is "integer", then both fields are nullptr. If the tag is "AST", then the first field will be a pointer to the Type of the return object, and the second field will be a pointer to the Type of the parameter object.
For large structures, the "con_vec" Type is used. A structure consisting of three elements will be described by a "con_vec" Type, which has four fields: 3, Tptr, Tptr, Tptr. The "3" says that there are 3 elements. The subfield of a con_vec type cannot be another con_vec.
The subfield of a pointer cannot be nullptr.

An AST object represents an expression, and consists of:
1. "tag", signifying what type of AST it is.
2. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all. For example, the "add" AST has two non-zero fields, which should be pointers to ASTs that return integers. The "if" AST takes three pointers to ASTs, which are a condition AST, a then-statement AST, and an if-statement AST. The maximum number of fields is four; unneeded fields can be left blank.
Each AST can be compiled by itself, as long as it is wrapped in a function (with parameter type). This function-wrapping currently happens automatically, so ASTs are compiled directly.

Descriptions of the ASTs and Types are in AST_descriptor[] and Type_descriptor[], respectively. First, we should note that that AST_descriptor is of type "AST_info". "AST_info" is a special enum-like class. Its constructor takes in a string as its first argument, which is the enum name. The second constructor argument is the return type. The remaining four arguments are the parameter types for the AST's four fields; unused fields can be left blank.
What AST_descriptor[] does is utilize this constructor to build a rich description of each AST. For example, one of the elements in AST_descriptor[] is { "add", T::integer, T::integer, T::integer }. This tells us that there's an AST whose name is "add" (the first argument). Moreover, it returns an integer (the second argument), and it takes two fields whose return Types should be integers (the third and fourth arguments).

Type_descriptor[] works similarly, using the enum-like class Type_info. Looking at the constructor for Type_info, we see that the first argument is the type name, the second type is the number of pointer fields each Type requires, and the third type is the size of the object that the Type describes.
For example, { "cheap pointer", 1, 1 } tells us that there is a Type that has name "cheap pointer" (the first argument), and its first field is a pointer to a Type that describes the object that is being pointed to (the second argument), and the size of the pointer is 1 (the third argument).

The class AST_info has some other functions, which are mainly for special cases. For example, the "if" AST should not have its fields automatically converted to IR, because it needs to build basic blocks before it can emit the IR in the correct basic block. Moreover, its return value is not known beforehand - it depends on its "then" and "else" fields. Therefore, its return value is "special_return", indicating that it cannot be treated in the usual way. Since its fields cannot be automatically compiled, its parameter types are left blank, and the function "add_pointer_fields" is applied instead. This says that the fields are there, but that IR should not be automatically generated for them.


NOTE: the "-1" tag is currently used for nullptr, for both Types and ASTs. this makes it appropriate to get a -1 tagged AST, which will appropriately return nullptr. I think this is safer than returning 0, which might cause collisions somewhere with the true 0-tag objects.
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
create an entry in type_check.
marky_mark.
dynamic offset AST, as well as dynamic_subtype()
change the T::Types, if you create a Type that takes more subfields, because the tag is actually meaningless much of the time.
*/
constexpr Type_info Type_descriptor[] =
{
	{"con_vec", minus_one, minus_one}, //concatenate a vector of types. the first field is the size of the array, so there are (fields[0] + 1) total fields. requires at least two types. is 0, because this is a very special case, and we'll be comparing this against 0 all the time. currently, initialization of gc roots, and marky_mark(), both depend on the number of pointer fields being non-zero.
	{"integer", 0, 1}, //64-bit integer
	{"dynamic object", 0, 1}, //in the pointed-to object, the first field is type, second field object. if the type is null, the base pointer is null. because: no matter what, you'll have to eventually check anyway. we might as well check before dereferencing. the cost is that dyn_subobj needs an extra branch in IR. if we had base pointer always non-null, then we'd return a dummy special value for 0 dynamic objects.
	{"AST pointer", 0, 1}, //can be nullptr. the actual object has tag, then previous, then some fields.
	{"type pointer", 0, 1}, //can be nullptr. actual object is tag + fields.
	{"function pointer", 0, 1}, //can be nullptr. the purpose of not specifying the return/parameter type is given in "doc/AST pointers"
	{"vector", 1, 1}, //base pointer cannot be null. interior type must be exactly size 1. todo: fix dynamic offset AST for this.
	{"pointer", 1, 1}, //nullptr prohibited. comes last, because our dynamic offset AST is fragile and relies on it. same, for our vector AST.
	{"pointer to something", 0, 1}, //an in-function type. you better be storing the actual type somewhere. this can never be stored into anything. however, we are currently enforcing that this must be a valid full pointer. so if you convert it to another valid pointer type, you actually can store it somewhere.
	{"vector of something", 0, 1}, //an in-function type. same as the pointer to something; used for dynamic_subobj.
	{"does not return", 0, 0}, //an in-function type
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

/*
type structure: if the type has no fields, the tag goes in the tptr val.
if the type does have fields, tptr's val is a pointer. it points to: {i64 tag, i64[x] fields}
if the tag is 0, then we enforce that it's a null type.
to do this, the 0 tag must always have fields. (it's concatenate for now)
concatenation: it's like a dynarray. first field is the size of the array, and the remaining fields are the types of the components.

use the copy_type() and new_type() functions instead, which handle sizes correctly.
*/
class Tptr
{
public:
	uint64_t val; //public only because copy_type() wants it
	uint64_t ver() const //returns -1 on nullptr? no, should return 0. you must check the Tptr for nullptr before calling this. benefit: easy to switch on, used for dynamic_subobj
	{
		if (val == 0) return 0;
		if (val > Typen("never reached")) return *(uint64_t*)val; //we're making a huge ABI assumption here, that pointers can't go near 0. also, 0 represents both concatenate and nullptr.
		else return val;
	}
	Tptr& field(uint64_t offset) const {
		check(val > Typen("never reached"), "trying to get the field of a tptr with no fields");
		return *(Tptr*)(val + sizeof(uint64_t) * (offset + 1));
	}
	constexpr operator uint64_t() const { return val; }
	//operator void*() const { return (void*)val; } creates problems when Tptr can degenerate either into ptr or integer
	constexpr Tptr(uint64_t t) : val(t) {}
};



#define max_fields_in_AST 40u
//should accomodate the largest possible AST. necessary for AST_descriptor[]

//only gets the pointers. misses the concatenate
struct Type_pointer_range
{
	Tptr t;
	Type_pointer_range(Tptr t_) : t(t_) {}
	Tptr* begin() {
		if (Type_descriptor[t.ver()].pointer_fields == 0) return 0;
		return (t.ver() == Typen("con_vec")) ? &(t.field(1)) : &(t.field(0));
	}
	Tptr* end() {
		if (Type_descriptor[t.ver()].pointer_fields == 0) return 0;
		return (t.ver() == Typen("con_vec")) ? &(t.field(1)) + (uint64_t)t.field(0) : &(t.field(Type_descriptor[t.ver()].pointer_fields));
	}
};

//can't use nullptr on this.
inline uint64_t total_valid_fields(const Tptr t)
{
	return (t.ver() == Typen("con_vec")) ? t.field(0) + 1 : Type_descriptor[t.ver()].pointer_fields;
}

#include "memory.h"
//warning: takes fields directly. so concatenate should worry.
inline Tptr new_nonunique_type(uint64_t tag, llvm::ArrayRef<Tptr> fields)
{
	uint64_t total_field_size = (tag == Typen("con_vec")) ? (uint64_t)fields[0] + 1 : Type_descriptor[tag].pointer_fields;
	if (tag == Typen("con_vec")) check(total_field_size >= 3, "no making small con_vecs allowed");
	if (total_field_size == 0) return (Tptr)tag;
	uint64_t* new_home = allocate(total_field_size + 1);
	new_home[0] = tag;
	for (uint64_t x = 0; x < total_field_size; ++x)
		new_home[x + 1] = (uint64_t)fields[x];
	return (uint64_t)new_home;
}

Tptr get_unique_type(Tptr model, bool can_reuse_parameter);

//warning: takes fields directly. so concatenate should worry.
inline Tptr new_type(uint64_t tag, llvm::ArrayRef<Tptr> fields)
{
	Tptr new_tptr = new_nonunique_type(tag, fields);
	return get_unique_type(new_tptr, true);
}

//doesn't return a unique version.
inline Tptr copy_type(const Tptr t)
{
	uint64_t fields = total_valid_fields(t);
	if (fields == 0) return (Tptr)t;
	uint64_t* new_type = allocate(fields + 1);
	uint64_t* old_type = (uint64_t*)t.val;
	for (uint64_t idx = 0; idx < fields + 1; ++idx)
		new_type[idx] = old_type[idx];
	return (uint64_t)(new_type);
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
	constexpr uint64_t does_not_return = Typen("does not return"); //the field can't return. used for goto. effectively makes type checking always pass.
	constexpr uint64_t integer = Typen("integer"); //describes an integer type
	constexpr uint64_t dynamic_object = Typen("dynamic object");
	constexpr uint64_t type = Typen("type pointer");
	constexpr uint64_t AST_pointer = Typen("AST pointer");
	constexpr uint64_t function_pointer = Typen("function pointer");
	//constexpr uint64_t vector = Typen("vector"); you must use the u:: version instead. making this constexpr is too hard.
	constexpr Tptr null = 0;
};

//the actual objects are placed in type_creator.cpp, to avoid static initialization fiasco
namespace u
{
	extern Tptr does_not_return;
	extern Tptr integer;
	extern Tptr dynamic_object;
	extern Tptr type;
	extern Tptr AST_pointer;
	extern Tptr function_pointer;
	extern Tptr vector_of_ASTs;
	extern Tptr pointer_to_something;
	extern Tptr vector_of_something;
	constexpr Tptr null = 0;
};
Tptr concatenate_types(llvm::ArrayRef<Tptr> components);

/*
all object sizes are integer multiples of 64 bits.
this function returns the size of an object as (number of bits/64).
thus, a uint64 has size "1". a struct of two uint64s has size "2". etc.
there can't be loops because Types are immut. the user doesn't have access to Type creation functions.
	we'll mark this as the "good" function. it takes in Types that are known to be unique.
*/
constexpr uint64_t get_size(Tptr target) __attribute__((pure));
constexpr uint64_t get_size(Tptr target)
{
	if (target == 0) return 0;
	else if (Type_descriptor[target.ver()].size != minus_one) return Type_descriptor[target.ver()].size;
	else if (target.ver() == Typen("con_vec"))
	{
		check(target.field(0) > 1, "concatenation with insufficient types"); //this is a meaningful check because pointer to nowhere gives tag = 0 (concat), fields = 0.
		//we now rely on every object being size 1
		return target.field(0);
	}
	error("couldn't get size of type tag, check backtrace for target->tag");
}

//finds one element in a concatenate. the size offset is returns in return_offset. the size of the object is returned normally.
inline uint64_t get_size_conc(Tptr target, uint64_t offset, uint64_t* return_offset)
{
	check(target != 0, "conc");
	check(target.ver() == Typen("con_vec"), "need conc");
	check(target.field(0) > offset, "error: you must check that there are enough types in the conc to handle the offset beforehand");
	uint64_t initial_offset = 0;
	for (uint64_t idx = 0; idx < offset; ++idx)
		initial_offset += get_size(target.field(idx + 1));
	*return_offset = initial_offset;
	return get_size(target.field(offset + 1));
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

//includes the tag in the result type. this is currently wrong.
inline Tptr get_Type_full_type(Tptr t)
{
	std::vector<Tptr> fields{u::integer}; //the tag
	if (t.ver() != Typen("con_vec"))
	{
		uint64_t number_of_pointers = Type_descriptor[t.ver()].pointer_fields;
		check(number_of_pointers > 0, "no getting full types of optimized types");
		for (uint64_t x = 0; x < number_of_pointers; ++x) fields.push_back(u::type);
		return concatenate_types(fields);
	}
	else
	{
		fields.push_back(u::integer);
		uint64_t number_of_pointers = t.field(0);
		for (uint64_t x = 0; x < number_of_pointers; ++x) fields.push_back(u::type);
		return concatenate_types(fields);
	}
}

//prints just the immediate type, doesn't look further.
inline std::ostream& operator<< (std::ostream& o, const Tptr t)
{
	if (t == 0) return o << "null type\n";
	o << "t " << Type_descriptor[t.ver()].name << "(" << t.ver() << ") at " << (uint64_t*)t.val << ", ";
	if (Type_descriptor[t.ver()].pointer_fields != 0) o << "fields";
	for (auto& x : Type_pointer_range(t)) o << ' ' << x;
	return o;
}

void debugtypecheck(Tptr test);
type_check_result type_check(type_status version, Tptr existing_reference, Tptr new_reference);
extern Tptr concatenate_types(llvm::ArrayRef<Tptr> components);