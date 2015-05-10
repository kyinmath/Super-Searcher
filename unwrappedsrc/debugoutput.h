#pragma once
#include <iostream>
#include "types.h"

using std::string;
void error(string Str) { std::cerr << "Error: " << Str << '\n'; abort(); }

//the condition is true when the program behaves normally.
void check(bool condition, string Str) { if (!condition) { std::cerr << "Error: " << Str << '\n'; abort(); } }


//only call on a non-nullptr target. outputs a single Type.
void output_type(Type* target)
{
	if (target == T_special)
	std::cerr << "type " << Type_descriptor[target->tag].name << "(" << target->tag << "), Addr " << target << ", ";
	std::cerr << "fields: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << '\n';
}

//for debugging. outputs a Type and everything it points to, recursively.
void output_type_and_previous(Type* target)
{
	if (target == nullptr)
	{
		std::cerr << "type is null\n";
		return;
	}
	output_type(target);
	unsigned further_outputs = Type_descriptor[target->tag].pointer_fields;
	for (int x = 0; x < further_outputs; ++x)
		if (target->fields[x].ptr != nullptr)
			output_type_and_previous(target->fields[x].ptr);
}


//only call on a non-nullptr target. outputs a single AST.
void output_AST(AST* target)
{
	std::cerr << "AST " << AST_descriptor[target->tag].name << "(" << target->tag << "), Addr " << target <<
		", prev " << target->preceding_BB_element << ", ";
	std::cerr << "fields: " << target->fields[0].ptr << ' ' << target->fields[1].ptr << ' ' << target->fields[2].ptr << ' ' << target->fields[3].ptr << '\n';
}

//for debugging. outputs an AST and everything it can see, recursively.
void output_AST_and_previous(AST* target)
{
	if (target == nullptr)
	{
		std::cerr << "AST is null\n";
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
		output_console(target, false);
		//we call it with "false", because the overall function is implicitly wrapped in braces.

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
	void output_console(AST* target, bool might_be_end_in_BB){
		if (AST_list.find(target) != AST_list.end())
		{
			std::cerr << target;
			return;
		}
		else AST_list.insert(target);

		bool output_braces = false;
		if (target->preceding_BB_element != nullptr)
		{
			if (might_be_end_in_BB)
			{
				output_braces = true;
				std::cerr << "{";
			}
			output_console(target->preceding_BB_element, false);
			std::cerr << ' ';
		}
		std::cerr << "[" << AST_descriptor[target->tag].name;
		int x = 0;
		for (; x < AST_descriptor[target->tag].pointer_fields; ++x)
		{
			std::cerr << ' ';
			output_console(target->fields[x].ptr, true);
		}
		unsigned final_nonzero_field = x;
		for (unsigned check_further_nonzero_fields = x + 1; check_further_nonzero_fields < max_fields_in_AST; ++check_further_nonzero_fields)
			if (target->fields[check_further_nonzero_fields].num) final_nonzero_field = check_further_nonzero_fields;
		for (; x <= final_nonzero_field; ++x)
		{
			std::cerr << ' ';
			std::cerr << target->fields[x].num;
		}
		std::cerr << ']';

		if (reference_necessary.find(target) != reference_necessary.end()) std::cerr << target;

		if (output_braces)
			std::cerr << "}";
	}
};