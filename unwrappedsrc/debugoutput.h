/*
output_AST_and_previous() shows an AST and everything it depends on.
output_Type_and_previous() is similar.
Module->print(*llvm_outstream, nullptr) prints the generated llvm code.
*/
#pragma once
#include "types.h"
#include <unordered_set>
#include "cs11.h"

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




//only call on a non-nullptr target. outputs a single Type.
inline void output_type(Type* target)
{
	if (target == T::special) { outstream << "special\n"; return; }
	outstream << "type " << Type_descriptor[target->tag].name << "(" << target->tag << "), addr " << target << ", ";
	outstream << "fields " << target->fields[0].ptr << ' ' << target->fields[1].ptr << '\n';
}

//for debugging. outputs a Type and everything it points to, recursively.
inline void output_type_and_previous(Type* target)
{
	if (target == nullptr)
	{
		outstream << "type is null\n";
		return;
	}
	output_type(target);
	unsigned further_outputs = Type_descriptor[target->tag].pointer_fields;
	for (int x = 0; x < further_outputs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_type_and_previous(target->fields[x].ptr);
}


//only call on a non-nullptr target. outputs a single AST.
inline void output_AST(AST* target)
{
	outstream << "AST " << AST_descriptor[target->tag].name << "(" << target->tag << "), addr " << target <<
		", prev " << target->preceding_BB_element << ", ";
	outstream << "fields " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
}

//for debugging. outputs an AST and everything it can see, recursively.
inline void output_AST_and_previous(AST* target)
{
	if (target == nullptr)
	{
		outstream << "AST is null\n";
		return;
	}
	output_AST(target);
	if (target->preceding_BB_element != nullptr)
		output_AST_and_previous(target->preceding_BB_element);
	unsigned number_of_further_ASTs = AST_descriptor[target->tag].pointer_fields;
	//boost::irange may be useful, but pulling in unnecessary (possibly-buggy) code is bad
	for (int x = 0; x < number_of_further_ASTs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_AST_and_previous(target->fields[x].ptr);
}


//outputs the AST in a form that can be input into the console
//any time it may start a new basic block, it outputs braces if necessary
//might_be_end_in_BB is to check if you may be starting a new basic block, which really means that your AST is a field in another AST.
//be careful: an AST and its previous basic block elements may need braces when referenced one way, but may not need them when referenced another way (for example, if it's referenced as dependencies of two different ASTs, but is not the primary element in the first). this isn't an issue when reading, but is handled appropriately when outputting.
struct output_AST_console_version
{
	std::unordered_set<AST*> AST_list = { nullptr }; //nullptr = 0 is a special value.
	std::unordered_set<AST*> reference_necessary; //list of ASTs that have references to them later. these ASTs need to print a name.

	output_AST_console_version(AST* target)
	{
		determine_references(target);
		AST_list = { nullptr }; //determine_references changes this, so we must reset it
		output_console(target, false); //we call it with "false", because the overall function has no braces.

		outstream << '\n';
	}

	void determine_references(AST* target)
	{
		if (AST_list.find(target) != AST_list.end()) //already seen
		{
			if (reference_necessary.find(target) == reference_necessary.end()) //reference not yet added
				reference_necessary.insert(target);
			return;
		}
		else AST_list.insert(target);

		if (target->preceding_BB_element != nullptr) determine_references(target->preceding_BB_element);
		for (int x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x) determine_references(target->fields[x].ptr);
	}

	//the purpose of "might be end in BB" is to decide whether to output {} or not.
	void output_console(AST* target, bool might_be_end_in_BB)
	{
		if (AST_list.find(target) != AST_list.end())
		{
			outstream << target;
			return;
		}
		else AST_list.insert(target);

		bool output_braces = false;
		if (target->preceding_BB_element != nullptr)
		{
			if (might_be_end_in_BB)
			{
				output_braces = true;
				outstream << "{";
			}
			output_console(target->preceding_BB_element, false);
			outstream << ' ';
		}
		outstream << "[" << AST_descriptor[target->tag].name;
		int x = 0;
		for (; x < AST_descriptor[target->tag].pointer_fields + AST_descriptor[target->tag].additional_special_fields; ++x)
		{
			outstream << ' ';
			output_console(target->fields[x].ptr, true);
		}
		unsigned first_zero_field = x;
		for (unsigned check_further_nonzero_fields = x + 1; check_further_nonzero_fields < max_fields_in_AST; ++check_further_nonzero_fields)
			if (target->fields[check_further_nonzero_fields].num) first_zero_field = check_further_nonzero_fields;
		for (; x < first_zero_field; ++x)
		{
			outstream << ' ';
			outstream << target->fields[x].num;
		}
		//outstream << "final x is " << x << '\n';
		//outstream << "final nonzero is " << first_zero_field << '\n';
		outstream << ']';

		if (reference_necessary.find(target) != reference_necessary.end()) outstream << target;

		if (output_braces)
			outstream << "}";
	}
};