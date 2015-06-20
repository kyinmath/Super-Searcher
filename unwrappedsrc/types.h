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

The class AST_info has some other functions, which are mainly for special cases. For example, the "if" AST should not have its fields automatically converted to IR, because it needs to build basic blocks before it can emit the IR in the correct basic block. Moreover, its return value is not known beforehand - it depends on its "then" and "else" fields. Therefore, its return value is "T::special_return", indicating that it cannot be treated in the usual way. Since its fields cannot be automatically compiled, its parameter types are left blank, and the function "make_pointer_fields" is applied instead. This says that the fields are there, but that IR should not be automatically generated for them.

Later, we'll have some ASTs that let the user actually query this information.
*/

#pragma once
#include <cstdint>
#include <array>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/ADT/ArrayRef.h>
#include "console.h"

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
#ifdef _MSC_VER
#define constexpr
#define thread_local
#endif

//for whatever reason, strcmp is not constexpr. so we do this
constexpr bool static_strequal(const char str1[], const char str2[])
{ return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false; }

struct Type_info
{
	const char* name;
	const unsigned pointer_fields; //how many field elements of the type object must be pointers

	//size of the actual object. -1ll if special
	const uint64_t size;

	unsigned additional_special_fields = 0; //these fields come after the pointer fields. they are REAL types, not AST return types.
	//for example, if you have a Type* in the second field and a normal AST* in the first field, then this would be 1.
	//then, in the argument list, you'd put a Type* in the second parameter slot.

	//this DOES NOT reduce the number of pointer fields. this is because we aren't specifying types. take note of this.
	//todo: we aren't using this at all.
	constexpr Type_info make_special_fields(uint64_t x)
	{
		Type_info new_copy(*this);
		new_copy.additional_special_fields = x;
		return new_copy;
	}

	constexpr Type_info(const char a[], uint64_t n, uint64_t s) : name(a), pointer_fields(n), size(s) {}
};

constexpr uint64_t special = -1ll; //note that a -1 literal, without specifying ll, can either be int32 or int64, so we could be in great trouble.

using _t = Type_info;
/* Guidelines for new Types:
you must create an entry in type_check.
full_validity.
*/
constexpr Type_info Type_descriptor[] =
{
	{"con_vec", special, special}, //concatenate a vector of types. the first field is the size of the array, so there are (fields[0] + 1) total fields. requires at least two types.
	{"integer", 0, 1}, //64-bit integer
	_t("pointer", 1, 1).make_special_fields(1), //pointer to anything, except the target type must be non-nullptr. second field is 1 if it's a full pointer, and 0 otherwise
	_t("dynamic pointer", 0, 1).make_special_fields(1), //dynamic pointer. a double-indirection. points to two elements: first field is the pointer, second field is a pointer to the type. the purpose of double indirection is lock safety
	{"AST pointer", 0, 1}, //just a pointer. (a full pointer)
	//the actual object has 6: tag, then previous, then 4 fields.
	{"function in clouds", 2, 2}, //points to: return AST, then parameter AST, then compiled area. we don't embed the AST along with the function signature, in order to keep them separate.
	{"nonexistent", 0, 0}, //a special value
	{"never reached", 0, 0},
	//except: if we have two ASTs, it's going to bloat our type object. and meanwhile, we want the parameter AST to be before everything that might use it, so having it as a first_pointer is not good, since the beginning of the function might change.
	{"lock", 0, 1},
	{"type", 0, 3},
};

#include <iostream>
template<class X, X vector_name[]> constexpr uint64_t get_enum_from_name(const char name[])
{
	for (unsigned k = 0; 1; ++k)
	{
		if (static_strequal(vector_name[k].name, name)) return k;
		if (static_strequal(vector_name[k].name, "never reached")) //this isn't recursive, because the previous if statement returns.
			error(string("tried to get a nonexistent name: ") + name);
		//llvm_unreachable doesn't give proper errors for run time mistakes, only when it's compile time.

	}
}
constexpr uint64_t Typen(const char name[]) { return get_enum_from_name<const Type_info, Type_descriptor>(name); }

#include "lock.h"
//Lo = Lockable
template<class T>
struct Lo : private T
{
	using T::T;
	//void perm_lock() {lock.guaranteed_grab();}
	//the type held here is only valid for as long as the lock is alive. don't try to extract x unless you know the lock will be alive for longer than that.
	//don't call get_interior except for very short periods of time! it's not meant to be kept locked.
	struct holder
	{
		T* x;
		rw_lock& lock;
		holder(T* t, rw_lock& l) : x(t), lock(l) {}
		~holder() { lock.release_read(); };
	};
	rw_lock lock;
	holder get_read()
	{
		lock.grab_read();
		return holder((T*)this, lock);
	}

	T* bypass()
	{
		return (T*)this;
	}
};

#define fields_in_Type 3u
//this is for types which are known to be unique and well-behaved (no loops).
//WARNING: don't use the ctor for this 
struct Type;
inline uint64_t total_valid_fields(const Type* t);
struct Type
{
	uint64_t tag;
	using iop = int_or_ptr<Type>;
	std::array<iop, fields_in_Type> fields;
	template<typename... Args> constexpr Type(const char name[], Args... args) : tag(Typen(name)), fields{{args...}} { if (tag == Typen("con_vec")) console << ("make it another way"); } //this only works because 0 is the proper default value
	template<typename... Args> constexpr Type(const uint64_t t, Args... args) : tag(t), fields{{args...}} { if (tag == Typen("con_vec")) console << ("make it another way"); }
	constexpr Type(const Type& other) : tag(other.tag)
	{
		if (tag == Typen("con_vec")) console << ("make it another way");
		for (uint64_t x = 0; x < total_valid_fields(&other); ++x)
			((uint64_t*)this)[1 + x] = ((uint64_t*)&other)[1 + x];
	}
};
#define max_fields_in_AST 4u
//should accomodate the largest possible AST
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
	Type_everything_range(Type* t_) : t(t_) {}
	uint64_t tag;
	Type* fields;
	Type** begin() { return &(t->fields[0].ptr); }
	Type** end() { return &(t->fields[0].ptr) + total_valid_fields(t);
	}
};

inline uint64_t total_valid_fields(const Type* t)
{
	return (t->tag == Typen("con_vec")) ? t->fields[0].num + 1 : Type_descriptor[t->tag].pointer_fields + Type_descriptor[t->tag].additional_special_fields;
}

inline Type* copy_type(const Type* t)
{
	uint64_t fields = total_valid_fields(t);
	uint64_t* new_type = new uint64_t[fields + 1];
	uint64_t* old_type = (uint64_t*)t;
	for (int idx = 0; idx < fields + 1; ++idx)
		new_type[idx] = old_type[idx];
	return (Type*)(new_type);
}

//when creating a new element here, remember to instantiate it in types.cpp.
//the reason these are static members of struct internal is so that the address is the same across translation units.
//otherwise, it's not. see https://stackoverflow.com/questions/7368330/static-members-and-the-default-constructor-c
namespace T
{
	struct internal
	{
		static constexpr Type int_{"integer"};
		static constexpr Type nonexistent{("nonexistent")};
		static constexpr Type missing_field{("nonexistent")};
		static constexpr Type special_return{("integer")};
		static constexpr Type parameter_no_type_check{("integer")};
		static constexpr Type cheap_dynamic_pointer{("dynamic pointer")};
		static constexpr Type full_dynamic_pointer{"dynamic pointer", 1};
		static constexpr Type AST_pointer{("AST pointer")};
		//static constexpr Type error_object{concatenate_types(std::vector<Type*>{const_cast<Type* const>(&int_), const_cast<Type* const>(&AST_pointer), const_cast<Type* const>(&int_)})};
		//error_object is int, pointer to AST, int. it's what is returned when compilation fails: the error code, then the AST, then the field.
	};
	typedef internal i;
	constexpr Type* nonexistent = const_cast<Type* const>(&i::nonexistent); //nothing at all. used for goto. effectively disables type checking for that field.
	constexpr Type* missing_field = const_cast<Type* const>(&i::missing_field); //used to say that a parameter field is missing
	constexpr Type* special_return = const_cast<Type* const>(&i::special_return); //indicates that a return type is to be handled differently
	constexpr Type* parameter_no_type_check = const_cast<Type* const>(&i::parameter_no_type_check); //indicates that a parameter type is not to be type checked using the default mechanism
	constexpr Type* integer = const_cast<Type* const>(&i::int_); //describes an integer type
	constexpr Type* cheap_dynamic_pointer = const_cast<Type* const>(&i::cheap_dynamic_pointer);
	constexpr Type* full_dynamic_pointer = const_cast<Type* const>(&i::full_dynamic_pointer);
	constexpr Type* null = nullptr;
	constexpr Type* AST_pointer = const_cast<Type* const>(&i::AST_pointer);
};


/*
all object sizes are integer multiples of 64 bits.
this function returns the size of an object as (number of bits/64).
thus, a uint64 has size "1". a struct of two uint64s has size "2". etc.
todo: what about loops? for now, it's ok, because the user doesn't have access to Type creation functions.
	we'll mark this as the "good" function. it takes in Types that are known to be unique.
*/
constexpr uint64_t get_size(Type* target)
{
	if (target == nullptr) return 0;
	else if (Type_descriptor[target->tag].size != special) return Type_descriptor[target->tag].size;
	else if (target->tag == Typen("con_vec"))
	{
		uint64_t total_size = 0;
		for (auto& x : Type_pointer_range(target)) total_size += get_size(x);
		return total_size;
	}
	error("couldn't get size of type tag, check backtrace for target->tag");
}

/**
this attaches information to enums.
first, we construct a vector, where each element has a string and some information attached (in the form of integers)
then, get_enum_from_name() associates numbers to the strings.
this lets us switch-case on the strings that are the first element of each enum. since the information is attached to the string, we are guaranteed not to forget to implement it, which reduces the potential for careless errors.
the total number of fields for the AST is pointer_fields + additional_special_fields.
*/
struct AST_info
{
	const char* name;

	//in ASTs, this is the size of the return object. it's special (= -1ll) if it must be handled in a special way.
	//in the case of types, the size of the actual object.
	const int size_of_return;

	Type* return_object; //if this type is null, do not check it using the generic method - check it specially.
	Type* parameter_types[max_fields_in_AST];

	unsigned pointer_fields; //how many field elements are pointers.
	//for ASTs, this means pointers to other ASTs. for Types, this means pointers to other Types.
	//this forces fields_to_compile downward if it is too high. by default, they are equal.
	constexpr AST_info make_pointer_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.pointer_fields = x;
		if (new_copy.fields_to_compile >= new_copy.pointer_fields)
			new_copy.fields_to_compile = new_copy.pointer_fields;
		return new_copy;
	}

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
		int number_of_fields = max_fields_in_AST; //by default, both pointer_fields and number_of_fields will be equal to this.
		if (f4 == T::missing_field) number_of_fields = 3;
		if (f3 == T::missing_field) number_of_fields = 2;
		if (f2 == T::missing_field) number_of_fields = 1;
		if (f1 == T::missing_field) number_of_fields = 0;
		return number_of_fields;
	};

	//in AST_descriptor[], fields_to_compile and pointer_fields are normally set to the number of parameter types specified.
	//	however, they can be overridden by make_fields_to_compile() and make_pointer_fields()
	constexpr AST_info(const char a[], Type* r, Type* f1 = T::missing_field, Type* f2 = T::missing_field, Type* f3 = T::missing_field, Type* f4 = T::missing_field)
		: name(a), size_of_return(get_size(r)), return_object(r), parameter_types{f1, f2, f3, f4}, pointer_fields(field_count(f1, f2, f3, f4)), fields_to_compile(field_count(f1, f2, f3, f4)){ }

};

using a = AST_info;
/*this is an enum which has extra information for each element. it is constexpr so that it can be used in a switch-case statement.

Guidelines for new ASTs:
first field is the AST name. second field is the return type. remaining optional fields are the parameter types.
return type is T_special if it can't be determined automatically from the AST tag. that means a deeper inspection is necessary.
then, write the compilation code for the AST in generate_IR().
if you're using the heap, set final_stack_state.
if you don't want the subfields to be compiled, use make_fields_to_compile().
if subfields aren't actually AST pointers, but maybe some other special object like a dynamic object or integer, use make_special_fields().

the number of parameter types determines the number of subfields to be compiled and type-checked automatically. the instructions in these subfields are always run; there's no branching.
	if you want to compile but not type-check a field, use make_fields_to_compile() and do not list a parameter type.
	if you want to neither compile or type-check a field, use make_pointer_fields().
		in these cases, you must handle the type-checking/compilation manually if it's not done automatically and you want it to be done.
		remember to handle null types and T::nonexistent.
if the AST branches, then make sure to clear temporaries from the stack list before running the other branch.
keep AST names to one word only, because our console input takes a single word for the name.
*/
constexpr AST_info AST_descriptor[] =
{
	{"null_AST", T::null}, //tag is 0. used when you create an AST and want nullptr
	a("integer", T::integer, T::integer).make_special_fields(1), //first argument is an integer, which is the returned value.
	{"increment", T::integer, T::integer}, //Peano axioms increment operation.
	{"hello", T::null},
	{"print_int", T::null, T::integer},
	a("if", T::special_return).make_pointer_fields(3), //test, first branch, fields[0] branch. passes through the return object of each branch; the return objects must be the same.
	a("scope", T::null).make_fields_to_compile(1), //fulfills the purpose of {} from C++
	{ "add", T::integer, T::integer, T::integer }, //adds two integers
	{ "subtract", T::integer, T::integer, T::integer },
	{ "random", T::integer }, //returns a random integer
	a("pointer", T::special_return).make_pointer_fields(1), //creates a pointer to an alloca'd element. takes a pointer to the AST, but does not compile it - instead, it searches for the AST pointer in <>objects.
	a("load", T::special_return).make_pointer_fields(1), //creates a temporary copy of an element. takes one field, but does NOT compile it.
	a("concatenate", T::special_return).make_pointer_fields(2),
	a("dynamic", T::full_dynamic_pointer).make_fields_to_compile(1), //creates dynamic storage for any kind of object. moves it to the heap.
	a("compile", T::full_dynamic_pointer, T::AST_pointer), //compiles an AST, returning a dynamic object which is either the error object or the desired info.
	a("temp_generate_AST", T::AST_pointer), //hacked in, generates a random AST.
	{"dynamic_conc", T::special_return, T::cheap_dynamic_pointer, T::cheap_dynamic_pointer}, //concatenate the interiors of two dynamic pointers
	a("goto", T::special_return).make_pointer_fields(1),
	a("label", T::null).make_pointer_fields(1), //the field is like a brace. anything inside the label can goto() out of the label. the purpose is to enforce that no extra stack elements are created.
	a("convert_to_AST", T::AST_pointer, T::integer, T::parameter_no_type_check, T::cheap_dynamic_pointer, T::AST_pointer).make_fields_to_compile(3), //don't compile the nonexistent branch, because it needs to go in a special basic block.
	a("store", T::null, T::parameter_no_type_check, T::parameter_no_type_check), //pointer, then value.
	//todo: I had these parameter_no_type_check as "T::special_return" before. yet my testcase still passed. why?
	a("static_object", T::special_return, T::full_dynamic_pointer).make_special_fields(1), //loads a static object.
	a("load_object", T::special_return, T::full_dynamic_pointer).make_special_fields(1), //loads a constant. similar to "int x = 40". if the value ends up on the stack, it can still be modified, but any changes are temporary.
	{ "never reached", T::special_return }, //marks the end of the currently-implemented ASTs. beyond this is rubbish.
	{ "dereference pointer", T::special_return}, //????
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

//there's zero point in having "well-formed" ASTs.
struct uAST
{
	//std::mutex lock;
	uint64_t tag;
	Lo<uAST>* preceding_BB_element; //this object survives on the stack and can be referenced. it's the previous basic block element.
	using iop = int_or_ptr<Lo<uAST>>;
	std::array<iop, max_fields_in_AST> fields;
	uAST(const char name[], Lo<uAST>* preceding = nullptr, iop f1 = nullptr, iop f2 = nullptr, iop f3 = nullptr, iop f4 = nullptr)
		: tag(ASTn(name)), preceding_BB_element(preceding), fields{{f1, f2, f3, f4}} {}
	uAST(uint64_t direct_tag, Lo<uAST>* preceding = nullptr, iop f1 = nullptr, iop f2 = nullptr, iop f3 = nullptr, iop f4 = nullptr)
		: tag(direct_tag), preceding_BB_element(preceding), fields{{f1, f2, f3, f4}} {}
	//watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};

//todo: we're just hoping that there aren't any infinite loops.
//this should be healthier later. we still need to optimize concatenate and embed this along with the main loop_catcher.

constexpr uint64_t get_size(uAST* target);
constexpr uint64_t get_size(Lo<uAST>* lo_target) { return get_size(lo_target->bypass()); }
constexpr uint64_t get_size(uAST* target) 
{
	if (target == nullptr)
		return 0; //for example, if you try to get the size of an if statement with nullptr fields as the return object.
	if (AST_descriptor[target->tag].return_object != T::special_return) return get_size(AST_descriptor[target->tag].return_object);
	else if (target->tag == ASTn("if")) return get_size(target->fields[1].ptr);
	else if (target->tag == ASTn("pointer")) return 1;
	else if (target->tag == ASTn("load")) return get_size(target->fields[0].ptr);
	else if (target->tag == ASTn("concatenate")) return get_size(target->fields[0].ptr) + get_size(target->fields[1].ptr); //todo: this is slow. takes n^2 time.
	else if (target->tag == ASTn("static_object")) return get_size((Type*)target->fields[1].ptr);
	else if (target->tag == ASTn("load_object")) return get_size((Type*)target->fields[1].ptr);
	else if (target->tag == ASTn("goto")) return get_size(target->fields[3].ptr);
	else if (target->tag == ASTn("dynamic_conc")) return get_size(T::cheap_dynamic_pointer); //todo: maybe we'll want cheap and full dynamic pointers to be separate? no double indirection?
	error(string("couldn't get size of tag in get_size(AST*), target->tag was ") + AST_descriptor[target->tag].name);
}


enum type_status { RVO, reference }; //distinguishes between RVOing an object, or just creating a reference


//from experience, we can't have the return value automatically work as a bool, because that causes errors.
//note that if it's a perfect fit, the sizes may still be different, because of T::nonexistent.
enum class type_check_result
{
	different,
	new_reference_too_large,
	existing_reference_too_large,
	perfect_fit
};

void debugtypecheck(Type* test);
type_check_result type_check(type_status version, Type* existing_reference, Type* new_reference);
extern Type* concatenate_types(llvm::ArrayRef<Type*> components);
bool is_full(Type* t);