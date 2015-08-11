#pragma once
#include "types.h"
#include "vector.h"
#include "dynamic.h"

enum special_type
{
	normal, //the return type is correctly specified in the constexpr array. get it from there
	missing_field, //the parameter field is entirely missing
	compile_without_type_check, //the parameter type is not to be type checked using the default mechanism. however, does_not_return is still checked for.
	special_return, //indicates that a return type is to be handled differently
	something_strange, //when the AST object is a special type, like imv, or a vector basic block.
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
	//for ASTs, this means pointers to other ASTs.
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

	uint64_t additional_special_fields = 0; //these fields are REAL types, not AST return types.

	//this reduces the number of pointer fields, because those are special fields instead.
	constexpr AST_info special_fields(int x)
	{
		AST_info new_copy(*this);
		new_copy.additional_special_fields = x;
		return new_copy;
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
	a("imv", special_return).special_fields(1), //immediate value, like "int x = 40". loaded value can be modified in-function, but any changes are temporary.
	a("basicblock", special_return).special_fields(1), //contains a svector* as the first field.
	{"zero", T::integer}, //Peano axioms zero
	{"decrement", T::integer, T::integer}, //Peano axioms increment operation.
	{"increment", T::integer, T::integer}, //Peano axioms increment operation.
	{"random", T::integer},
	a("if", special_return, T::integer).add_pointer_fields(2), //test, first branch, fields[0] branch. passes through the return object of each branch; the return object types must be the same.
	{"lessu", T::integer, T::integer, T::integer}, //less than comparison
	{"lesss", T::integer, T::integer, T::integer}, //less than signed comparison
	{"add", T::integer, T::integer, T::integer}, //adds two integers
	{"subtract", T::integer, T::integer, T::integer},
	{"multiply", T::integer, T::integer, T::integer},
	{"ushr", T::integer, T::integer, T::integer},
	{"udiv", T::integer, T::integer, T::integer},
	{"urem", T::integer, T::integer, T::integer},
	a("pointer", special_return).add_pointer_fields(1), //creates a pointer to an alloca'd element. takes a pointer to the AST, but does not compile it; it treats it like a variable name
	{"tmp_pointer", special_return, compile_without_type_check}, //for now, this turns a regular pointer into a temp pointer, for use with the stack. later, it'll probably be removed, and this will be done automatically instead.
	a("concatenate", special_return, compile_without_type_check, compile_without_type_check), //squashes two objects together to make a big object
	a("nvec", special_return, T::type), //makes a new vector, type of interior can't be chosen by a constant int, because pointers need further types.
	{"dynamify", T::dynamic_object, compile_without_type_check}, //turns a regular pointer into a dynamic object. this is necessary for things like trees, where you undynamify to use, then redynamify to store.
	a("compile", T::function_pointer, compile_without_type_check), //compiles an AST, returning a dynamic AST. for now, we don't return an error code. can also take nothing, to type bootstrap.
	a("overfunc", T::integer, T::function_pointer, T::function_pointer), //overwrites the first function with the second. return 1 on success.
	{"run_function", T::dynamic_object, T::function_pointer},
	//{"dynamic_conc", T::dynamic_pointer, T::dynamic_pointer, compile_without_type_check}, //concatenate the interiors of two dynamic objects
	a("goto", T::null).add_pointer_fields(1), //first field is label. for success and failure, user must test finiteness manually.
	a("label", T::null).add_pointer_fields(1), //the field is like a brace. anything inside the label can goto() out of the label. the purpose is to enforce that no extra stack elements are created.
	a("convert_to_AST", T::AST_pointer, T::integer, compile_without_type_check), //returns 0 on failure, the null AST. the purpose is to solve bootstrap issues; this is guaranteed to successfully create an AST. integer, then a vector/nothing.
	a("imv_AST", T::AST_pointer, T::dynamic_object), //makes a new dynamic object
	{"store", T::null, compile_without_type_check, compile_without_type_check}, //pointer, then value. or reference, then value. will use overloading to determine which is needed.
	{"try_store", T::integer, compile_without_type_check, compile_without_type_check}, //attempts to write the second object into the first. returns 1 on success. works with vector_of_something and pointer_to_something
	{"load_subobj", special_return, compile_without_type_check, T::integer}, //if pointer, gives Nth subobject. if Type, gives Nth subtype. cannot handle dynamics, because those split into having 6 AST parameter fields. this is where the loading is guaranteed to succeed. if function, gives either a copy of the ASTs, or the return type. if AST, gives Nth subAST, or 0. if vector, and 0 is an acceptable failure value, it gives the Nth object.
	a("load_subobj_ref", T::null, compile_without_type_check, T::integer).add_pointer_fields(1), //creates a reference and places it in the last field, instead of returning it. this is necessary if the loading can fail. used for ASTs, and for vectors.
	a("dyn_subobj", T::type, compile_without_type_check, T::integer).add_pointer_fields(Typen("pointer") + 1), //if the proper type is a pointer/vector, we return the "pointer to something"/"vector of something". returns the type obtained. can take in either a dynamic object, a pointer to something, or a vector of something.
	{"vecpb", T::null, compile_without_type_check, compile_without_type_check}, //push an object.
	{"vecsz", T::integer, compile_without_type_check}, //maybe in the future, this would work for both vectors and ASTs, to find the number of fields.
	{"typeof", T::type, compile_without_type_check}, //returns the type of an object. necessary to create new vectors.
	{"system1", T::integer, T::integer}, //find a system value, using only one parameter. this is a read-only operation, with no effects.
	{"system2", T::integer, T::integer, T::integer},
	{"agency1", T::null, T::integer}, //this has side effects. it's split out because system reading is not dangerous, and this is. the user generally should worry about running this AST.
	{"agency2", T::null, T::integer, T::integer},
	//we need a specific "overwrite function" AST, that can't be the usual store. because it might fail, and so we must return an error code.
	a("load_tag", T::integer, compile_without_type_check), //takes AST or type.
	a("load_imv_from_AST", T::null, T::AST_pointer).add_pointer_fields(1),
	a("load_vector_from_BB", T::null, T::AST_pointer).add_pointer_fields(1),
	//a("do_after", T::special_return, compile_without_type_check).make_pointer_fields(2),
	//{"return", T::special_return, T::compile_without_type_check}, have to check that the type matches the actual return type. call all dtors. we can take T::does_not_return, but that just disables the return.
	//{"snapshot", T::dynamic_pointer, T::dynamic object}, //makes a deep copy. ought to return size as well, since size is a way to cheat, by growing larger.
	//{"optimize", T::AST_pointer, T::AST_pointer}, //optimizes an AST chain into a better AST chain that produces the same code.
	{"never reached", special_return}, //marks the end of the currently-implemented ASTs. beyond this is rubbish.
	/*
	{ "divss", 3 }, warning: integer division by -1 must be considered. (1, 31) / -1 is segfault.
	{ "lteq", 2 },
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

constexpr uint64_t ASTn(const char name[]) { return get_enum_from_name<const AST_info, AST_descriptor>(name); }

//there's zero point in having "well-formed" ASTs.
struct uAST
{
	//std::mutex lock;
	uint64_t tag;
	std::array<uAST*, max_fields_in_AST> fields;
	svector*& BBvec() { return (svector*&)fields[0]; }
private:
	template<typename... Args> constexpr uAST(const char name[], Args... args)
		: tag(ASTn(name)), fields{{args...}} {}
};

#include "type_creator.h"
//pass in a valid AST tag, 0 <= x < "never reached"
inline Tptr get_AST_fields_type(uint64_t tag)
{
	std::vector<Tptr> fields;
	check(tag < ASTn("never reached"), "get_AST_type is a sandboxed function, use the user facing version instead");
	if (tag == ASTn("imv")) return u::dynamic_object;
	if (tag == ASTn("basicblock")) return u::vector_of_ASTs;
	uint64_t number_of_AST_pointers = AST_descriptor[tag].pointer_fields;
	for (uint64_t x = 0; x < number_of_AST_pointers; ++x)
		fields.push_back(u::AST_pointer);
	return concatenate_types(fields);
}

//this isn't usable for memory anyway.
inline Tptr get_AST_full_type(uint64_t tag)
{
	return concatenate_types({u::integer, get_AST_fields_type(tag)});
}

//for the AST flat object; doesn't look through deeper pointers.
inline uint64_t get_field_size_of_AST(uint64_t tag)
{
	if (tag == ASTn("imv")) return 1;
	if (tag == ASTn("basicblock")) return 1;
	return AST_descriptor[tag].pointer_fields;
}


inline uint64_t get_full_size_of_AST(uint64_t tag)
{
	return get_field_size_of_AST(tag) + 1;
}
#include "memory.h"
//handles "basicblock" intelligently
inline uAST* new_AST(uint64_t tag, llvm::ArrayRef<uAST*> fields)
{
	check(tag < ASTn("never reached"), "tag is huge");
	uint64_t field_size = get_field_size_of_AST(tag);
	uint64_t total_full_size = get_full_size_of_AST(tag);
	uAST* new_home = (uAST*)allocate(total_full_size);
	new_home->tag = tag;
	if (tag != ASTn("basicblock"))
	{
		check(field_size == fields.size(), "passed the wrong number of fields to new_AST, wanted " + std::to_string(field_size) + ", got " + std::to_string(fields.size()));
		for (uint64_t x = 0; x < field_size; ++x)
			new_home->fields[x] = (uAST*)fields[x];
	}
	if (tag == ASTn("basicblock"))
	{
		new_home->fields[0] = (uAST*)vector_build(fields);
	}
	if (VERBOSE_GC) print("new AST ", new_home, '\n');
	return (uAST*)new_home;
}

void output_AST(uAST* target);
//doesn't make a deep copy. however, for basic blocks, this will copy the interior, because otherwise there's zero point to copying an AST. same for imv.
//copying imv and basicblock also makes the deep_AST_copier much nicer.
inline uAST* copy_AST(uAST* t)
{
	if (t == nullptr) return 0;
	uint64_t tag = t->tag;
	check(tag < ASTn("never reached"), "tag is huge");
	uint64_t total_field_size = get_field_size_of_AST(tag);
	uAST* new_home;
	if (tag == ASTn("imv"))
	{
		auto k = (dynobj*)t->fields[0];
		new_home = new_AST(tag, (uAST*)copy_dynamic(k));
	}
	else if (tag == ASTn("basicblock"))
	{
		svector* k = (svector*)t->fields[0];
		llvm::ArrayRef<uAST*> fields(*k);
		new_home = new_AST(tag, fields);
	}
	else
	{
		llvm::ArrayRef<uAST*> fields(&t->fields[0], total_field_size);
		new_home = new_AST(tag, fields);
	}
	if (VERBOSE_GC) print("copy AST from ", t, " to ", new_home, '\n');
	return (uAST*)new_home;
}

inline uAST** AST_field(uAST* t, uint64_t offset)
{
	if (t == 0) return 0;
	if (t->tag == ASTn("imv")) return 0;
	if (t->tag == ASTn("basicblock"))
	{
		auto k = (svector*)(t->fields[0]);
		if (offset < k->size)
			return (uAST**)&(*k)[offset];
		else return 0;
	}
	if (offset < get_field_size_of_AST(t->tag)) return &(t->fields[offset]);
	else return 0;
}

//for AST pointers only. is smart enough to handle basic blocks.
struct AST_range
{
	uAST* t;
	AST_range(uAST* t_) : t(t_) {}
	uAST** begin() {
		if (t == 0) return 0;
		if (t->tag == ASTn("basicblock"))
		{
			svector* vec = (svector*)t->fields[0];
			return (uAST**)Vector_range(vec).begin();
		}
		else return &t->fields[0];
	}
	uAST** end() {
		if (t == 0) return 0;
		if (t->tag == ASTn("basicblock"))
		{
			svector* vec = (svector*)t->fields[0];
			return (uAST**)Vector_range(vec).end();
		}
		else return &t->fields[AST_descriptor[t->tag].pointer_fields];
	}
};

struct ou
{
	uAST* a;
	ou(uAST* t) : a(t) {}
};
//prints just the immediate AST, doesn't.a look further. takes pointers instead of objects, because pointer might be 0.
//but, we can't overload it... so we're boned basically. thus, for now, we overload it with struct ou.
inline std::ostream& operator<< (std::ostream& o, ou t)
{
	if (t.a == 0) return o << "null AST\n";
	o << "AST " << AST_descriptor[t.a->tag].name << "(" << t.a->tag << ") at " << t.a << ", ";
	bool have_printed_field = false;
	for (uAST*& k : AST_range(t.a))
	{
		if (!have_printed_field) o << "fields";
		have_printed_field = true;
		o << ' ' << k;
	}
	if (!have_printed_field) o << "no fields";
	return o;
}
