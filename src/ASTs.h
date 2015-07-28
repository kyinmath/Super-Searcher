#pragma once
#include "types.h"

enum special_type
{
	normal, //the return type is correctly specified in the constexpr array. get it from there
	missing_field, //the parameter field is entirely missing
	compile_without_type_check, //the parameter type is not to be type checked using the default mechanism. however, does_not_return is still checked for.
	special_return //indicates that a return type is to be handled differently
};

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

	struct compile_time_typeinfo
	{
		special_type state;
		const Tptr type;
		constexpr compile_time_typeinfo(Tptr t) : state(normal), type(t) {}
		constexpr compile_time_typeinfo(uint64_t t) : state(normal), type(t) {}
		constexpr compile_time_typeinfo(special_type s) : state(s), type(0) {}
		constexpr compile_time_typeinfo() : state(missing_field), type(0) {}
	};
	typedef compile_time_typeinfo ctt;
	ctt return_object; //if this type is null, do not check it using the generic method - check it specially.
	ctt parameter_types[max_fields_in_AST];

	uint64_t pointer_fields; //how many field elements are pointers.
	//for ASTs, this means pointers to other ASTs. for Types, this means pointers to other Types.
	//this forces fields_to_compile downward if it is too high. by default, they are equal.
	//if we remove this, we better fix up the difference between compiled fields and non-compiled fields. no more size... for fields to compile.
	constexpr AST_info add_pointer_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.pointer_fields += x;
		if (new_copy.fields_to_compile >= new_copy.pointer_fields)
			new_copy.fields_to_compile = new_copy.pointer_fields;
		return new_copy;
	}

	uint64_t fields_to_compile; //we may not always compile all pointers, such as with "copy".

	uint64_t additional_special_fields = 0; //these fields come after the pointer fields. they are REAL types, not AST return types.
	//for example, if you have a Tptr in the second field and a normal AST* in the first field, then this would be 1.
	//then, in the argument list, you'd put a Tptr in the second parameter slot.

	//this reduces the number of pointer fields, because those are special fields instead.
	constexpr AST_info make_special_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.additional_special_fields = x;
		return new_copy.add_pointer_fields(-x);
	}

	template<typename... Args> constexpr AST_info(const char a[], ctt r, Args... incoming_fields)
		: name(a), return_object(r), parameter_types{incoming_fields...}, pointer_fields(sizeof...(incoming_fields)), fields_to_compile(sizeof...(incoming_fields)) {}
	//in AST_descriptor[], fields_to_compile and pointer_fields are normally set to the number of parameter types specified.
	//	however, they can be overridden by make_fields_to_compile() and make_pointer_fields()
};

using a = AST_info;
/*this is an enum which has extra information for each element. it is constexpr so that it can be used in a switch-case statement.

Guidelines for new ASTs:
first field is the AST name. second field is the return type. remaining optional fields are the parameter types.
return type is "special_return" if it can't be determined automatically from the AST tag. that means a deeper inspection is necessary.
then, write the compilation code for the AST in generate_IR().
if you're using the heap, set final_stack_state.
if you don't want a subfield to be compiled, use make_pointer_fields() and don't specify the parameter type.
if a subfield isn't actually an AST pointer, but is some other special object like a dynamic object or integer, use make_special_fields() and specify the parameter type.

the number of parameter types determines the number of subfields to be compiled and type-checked automatically. the instructions in these subfields are always run; there's no branching.
if you want to compile but not type-check a field, use compile_without_type_check.
if you want to neither compile or type-check a field, use make_pointer_fields().
in these cases, you must handle the type-checking/compilation manually if it's not done automatically and you want it to be done.
remember to handle null types and u::does_not_return
if the AST branches, then make sure to clear temporaries from the stack list before running the other branch.
keep AST names to one word only, because our console input takes a single word for the name.
*/
constexpr AST_info AST_descriptor[] =
{
	a("imv", special_return, T::dynamic_object).make_special_fields(1), //immediate value, like "int x = 40". loaded value can be modified in-function, but any changes are temporary.
	{"zero", T::integer}, //Peano axioms zero
	{"decrement", T::integer, T::integer}, //Peano axioms increment operation.
	{"increment", T::integer, T::integer}, //Peano axioms increment operation.
	{"random", T::integer},
	a("if", special_return, T::integer).add_pointer_fields(2), //test, first branch, fields[0] branch. passes through the return object of each branch; the return objects must be the same.
	{"add", T::integer, T::integer, T::integer}, //adds two integers
	{"subtract", T::integer, T::integer, T::integer},
	{"multiply", T::integer, T::integer, T::integer},
	{"udiv", T::integer, T::integer, T::integer},
	{"urem", T::integer, T::integer, T::integer},
	a("pointer", special_return).add_pointer_fields(1), //creates a pointer to an alloca'd element. takes a pointer to the AST, but does not compile it; it treats it like a variable name
	a("concatenate", special_return, compile_without_type_check, compile_without_type_check), //squashes two objects together to make a big object
	a("nvec", special_return, T::type), //makes a new vector, type of interior can't be chosen by a constant int, because pointers need further types.
	{"dynamify", T::dynamic_object, compile_without_type_check}, //turns a regular pointer into a dynamic pointer. this is necessary for things like trees, where you undynamify to use, then redynamify to store.
	a("compile", T::function_pointer, T::AST_pointer), //compiles an AST, returning a dynamic AST. the two other fields are branches to be run on success or failure. these two fields see the error code, then a dynamic object, when loading the compilation AST. thus, they can't be created in a single pass, because pointers point in both directions.
	{"run_function", T::dynamic_object, T::function_pointer},
	//{"dynamic_conc", T::dynamic_pointer, T::dynamic_pointer, compile_without_type_check}, //concatenate the interiors of two dynamic pointers
	a("goto", special_return).add_pointer_fields(3), //first field is label, second field is success, third field is failure
	a("label", T::null).add_pointer_fields(1), //the field is like a brace. anything inside the label can goto() out of the label. the purpose is to enforce that no extra stack elements are created.
	a("convert_to_AST", T::AST_pointer, T::integer, compile_without_type_check, compile_without_type_check), //returns 0 on failure, the null AST. the purpose is to solve bootstrap issues; this is guaranteed to successfully create an AST.
	{"store", T::null, compile_without_type_check, compile_without_type_check}, //pointer, then value.
	{"load_subobj", special_return, compile_without_type_check, T::integer}, //if AST, gives Nth subtype. if pointer, gives Nth subobject. if Type, gives Nth subtype. cannot handle dynamics, because those split into having 6 AST parameter fields.
	a("dyn_subobj", T::type, T::dynamic_object, T::integer).add_pointer_fields(Typen("pointer") + 1), //if it's a pointer, we make a special dynamic pointer instead. returns the type obtained.
	a("vector_push", T::null, compile_without_type_check, compile_without_type_check), //push an object.
	{"typeof", T::type, compile_without_type_check}, //returns the type of an object. necessary to create new vectors.
	{"system1", T::integer, T::integer}, //find a system value, using only one parameter. this is a read-only operation, with no effects.
	{"system2", T::integer, T::integer, T::integer},
	{"agency1", T::null, T::integer}, //this has side effects. it's split out because system reading is not dangerous, and this is. the user generally should worry about running this AST.
	{"agency2", T::null, T::integer, T::integer},
	//we need a specific "overwrite function" AST, that can't be the usual store. because it might fail, and so we must return an error code.
	a("load_tag", T::integer, compile_without_type_check), //takes AST or type.
	{"load_imv_from_AST", T::dynamic_object, T::AST_pointer}, //makes a copy of the imv as well, since it's const.
	//a("do_after", T::special_return, compile_without_type_check).make_pointer_fields(2),
	//{"return", T::special_return, T::compile_without_type_check}, have to check that the type matches the actual return type. call all dtors. we can take T::does_not_return, but that just disables the return.
	//{"snapshot", T::dynamic_pointer, T::dynamic pointer}, //makes a deep copy. ought to return size as well, since size is a way to cheat, by growing larger.
	{"never reached", special_return}, //marks the end of the currently-implemented ASTs. beyond this is rubbish.
	/*
	{ "divss", 3 }, warning: integer division by -1 must be considered. (1, 31) / -1 is segfault.
	{ "less", 2 },
	{ "lteq", 2 },
	{ "less signed", 2 },
	{ "lteq signed", 2 },
	//{ "equal", 2 }, this is useless; subtraction does it.
	{ "logical not", 1 },
	{ "logical and", 2 },
	{ "logical or", 2 },
	{ "logical xor", 2 },
	{ "bitwise not", 1 },
	{ "bitwise and", 2 },
	{ "bitwise or", 2 },

	//since we have guaranteed success, it's super easy for the user to make null ASTs by letting tag >= "never reached"
	{ "bitwise xor", 2 },*/
};

constexpr uint64_t ASTn(const char name[])
{ return get_enum_from_name<const AST_info, AST_descriptor>(name); }

//there's zero point in having "well-formed" ASTs.
//warning: the previous_BB element has dependencies from lots of things, especially get_AST_fields, and the size functions.
struct uAST
{
	//std::mutex lock;
	uint64_t tag;
	uAST* preceding_BB_element; //this object survives on the stack and can be referenced. it's the previous basic block element.
	using iop = int_or_ptr<uAST>;
	std::array<iop, max_fields_in_AST> fields;
private:
	template<typename... Args> constexpr uAST(const char name[], uAST* preceding = nullptr, Args... args)
		: tag(ASTn(name)), preceding_BB_element(preceding), fields{{args...}} {}
};

#include "type_creator.h"
//pass in a valid AST tag, 0 <= x < "never reached"
inline Tptr get_AST_fields_type(uint64_t tag)
{
	std::vector<Tptr> fields;
	//check(tag != 0, "this is so that some functions like marky_mark() don't need special cases for null objects"); //this isn't necessary anymore, because no more null ASTs
	check(tag < ASTn("never reached"), "get_AST_type is a sandboxed function, use the user facing version instead");
	uint64_t number_of_AST_pointers = AST_descriptor[tag].pointer_fields;
	for (uint64_t x = 0; x < number_of_AST_pointers; ++x)
		fields.push_back(u::AST_pointer);
	for (uint64_t x = 0; x < AST_descriptor[tag].additional_special_fields; ++x)
		fields.push_back(get_unique_type(AST_descriptor[tag].parameter_types[number_of_AST_pointers + x].type, false));
	return concatenate_types(fields);
}


inline Tptr get_AST_full_type(uint64_t tag)
{
	return concatenate_types({u::integer, u::AST_pointer, get_AST_fields_type(tag)});
}

inline uint64_t get_full_size_of_AST(uint64_t tag)
{
	if (tag == ASTn("imv")) return 3;
	return AST_descriptor[tag].pointer_fields + 2;
}

#include "memory.h"
inline uAST* new_AST(uint64_t tag, uAST* previous, llvm::ArrayRef<uAST*> fields)
{
	uint64_t total_field_size = get_size(get_AST_fields_type(tag));
	check(total_field_size == get_size(get_AST_fields_type(tag)), "passed the wrong number of fields to new_AST");
	uint64_t* new_home = allocate(total_field_size + 2);
	new_home[0] = tag;
	new_home[1] = (uint64_t)previous;
	for (uint64_t x = 0; x < total_field_size; ++x)
		new_home[x + 2] = (uint64_t)fields[x];
	return (uAST*)new_home;
}

void output_AST(uAST* target);
inline uAST* copy_AST(uAST* t)
{
	uint64_t tag = t->tag;
	uAST* previous = t->preceding_BB_element;
	uint64_t total_field_size = get_size(get_AST_fields_type(tag));
	uint64_t* new_home = allocate(total_field_size + 2);
	new_home[0] = tag;
	new_home[1] = (uint64_t)previous;
	for (uint64_t x = 0; x < total_field_size; ++x)
		new_home[x + 2] = t->fields[x].num;
	return (uAST*)new_home;
}