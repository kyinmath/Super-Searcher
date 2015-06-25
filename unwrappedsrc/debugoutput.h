/*
output_AST_and_previous() shows an AST and everything it depends on.
output_Type_and_previous() is similar.
Module->print(*llvm_console, nullptr) prints the generated llvm code.
*/
#pragma once
#include "types.h"
#include "ASTs.h"
#include <unordered_set>

//s("test") returns "test" if debug_names is true, and an empty string if debug_names is false.
//#define debug_names
#ifdef debug_names
inline std::string s(std::string k) { return k; }
#else
inline std::string s(std::string k) { return ""; }
#endif

template <class cT, class traits = std::char_traits<cT> >
class basic_nullbuf : public std::basic_streambuf<cT, traits> {
	typename traits::int_type overflow(typename traits::int_type c)
	{ return traits::not_eof(c); // indicate success
	}
};

template <class cT, class traits = std::char_traits<cT> >
class basic_onullstream : public std::basic_ostream<cT, traits> {
public:
	basic_onullstream() :
		std::basic_ios<cT, traits>(&m_sbuf),
		std::basic_ostream<cT, traits>(&m_sbuf)
	{ // note: the original code is missing the required this->
		this->init(&m_sbuf);
	}

private:
	basic_nullbuf<cT, traits> m_sbuf;
};




//only call on a non-nullptr target. outputs a single Type. doesn't have the right number of fields, but that's probably ok.
//for debug purposes, we don't want to limit it to the number of pointer fields.
inline void output_type(Type* target)
{
	if (target == nullptr)
	{
		console << "null type\n";
		return;
	}
	if (target == T::special_return) { console << "special\n"; return; }
	console << "type " << Type_descriptor[target->tag].name << "(" << target->tag << "), addr " << target << ", ";
	console << "fields";
	for (auto& x : Type_everything_range(target))
		console << ' ' << x;
	console << '\n';
}

//for debugging. outputs a Type and everything it points to, recursively.
inline void output_type_and_previous(Type* target)
{
	if (target == nullptr)
	{
		console << "type is null\n";
		return;
	}
	output_type(target);
	for (auto& x : Type_pointer_range(target))
		output_type_and_previous(x);
}


//only call on a non-nullptr target. outputs a single AST.
inline void output_AST(uAST* target)
{
	//uAST* target = locked_target->bypass(); //ignore all locks
	if (target == nullptr)
	{
		console << "null AST\n";
		return;
	}
	console << "AST " << AST_descriptor[target->tag].name << "(" << target->tag << "), addr " << target <<
		", prev " << target->preceding_BB_element << ", ";
	console << "fields " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
}

#include <set>
//for debugging. outputs an AST and everything it can see, recursively.
inline void output_AST_and_previous(uAST* target)
{
	if (target == nullptr)
	{
		console << "null AST\n";
		return;
	}

	//this makes you not output any AST that has been output before. it prevents infinite loops
	static thread_local std::set<uAST*> AST_list;
	if (AST_list.find(target) != AST_list.end())
	{
		console << target << '\n';
		return;
	}
	AST_list.insert(target);

	output_AST(target);
	if (target->preceding_BB_element != nullptr)
		output_AST_and_previous(target->preceding_BB_element);
	unsigned number_of_further_ASTs = AST_descriptor[target->tag].pointer_fields;
	//boost::irange may be useful, but pulling in unnecessary (possibly-buggy) code is bad
	for (unsigned x = 0; x < number_of_further_ASTs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_AST_and_previous(target->fields[x].ptr);
}


//outputs the AST in a form that can be input into the console
//any time it may start a new basic block, it outputs braces if necessary
//might_be_end_in_BB is to check if you may be starting a new basic block, which really means that your AST is a field in another AST.
//be careful: an AST and its previous basic block elements may need braces when referenced one way, but may not need them when referenced another way (for example, if it's referenced as dependencies of two different ASTs, but is not the primary element in the first). this isn't an issue when reading, but is handled appropriately when outputting.
struct output_AST_console_version
{
	std::unordered_set<uAST*> AST_list = { nullptr }; //nullptr = 0 is a special value.
	std::unordered_set<uAST*> reference_necessary; //list of ASTs that have references to them later. these ASTs need to print a name.

	output_AST_console_version(uAST* target)
	{
		determine_references(target);
		AST_list = { nullptr }; //determine_references changes this, so we must reset it
		output_console(target, false); //we call it with "false", because the overall function has no braces.

		console << '\n';
	}

	void determine_references(uAST* target)
	{
		if (AST_list.find(target) != AST_list.end()) //already seen
		{
			if (reference_necessary.find(target) == reference_necessary.end()) //reference not yet added
				reference_necessary.insert(target);
			return;
		}
		else AST_list.insert(target);

		if (target->preceding_BB_element != nullptr) determine_references(target->preceding_BB_element);
		for (unsigned x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x) determine_references(target->fields[x].ptr);
	}

	//the purpose of "might be end in BB" is to decide whether to output {} or not.
	void output_console(uAST* target, bool might_be_end_in_BB)
	{
		if (AST_list.find(target) != AST_list.end())
		{
			console << target;
			return;
		}
		else AST_list.insert(target);

		bool output_braces = false;
		if (target->preceding_BB_element != nullptr)
		{
			if (might_be_end_in_BB)
			{
				output_braces = true;
				console << "{";
			}
			output_console(target->preceding_BB_element, false);
			console << ' ';
		}
		console << "[" << AST_descriptor[target->tag].name;
		unsigned x = 0;
		for (; x < AST_descriptor[target->tag].pointer_fields; ++x)
		{
			console << ' ';
			output_console(target->fields[x].ptr, true);
		}

		//any additional fields that aren't pointers
		for (; x < AST_descriptor[target->tag].pointer_fields + AST_descriptor[target->tag].additional_special_fields; ++x)
		{
			console << ' ' << target->fields[x].num;
		}
		unsigned first_zero_field = x;
		for (unsigned check_further_nonzero_fields = x + 1; check_further_nonzero_fields < max_fields_in_AST; ++check_further_nonzero_fields)
			if (target->fields[check_further_nonzero_fields].num) first_zero_field = check_further_nonzero_fields;
		for (; x < first_zero_field; ++x)
		{
			console << ' ';
			console << target->fields[x].num;
		}
		//console << "final x is " << x << '\n';
		//console << "final nonzero is " << first_zero_field << '\n';
		console << ']';

		if (reference_necessary.find(target) != reference_necessary.end()) console << target;

		if (output_braces)
			console << "}";
	}
};