/*
pfAST() shows an AST and everything it depends on.
output_AST_console_version() is a superior AST printer.
pftype() is similar.
Module->print(*llvm_console, nullptr) prints the generated llvm code.
*/
#pragma once
#include "types.h"
#include "ASTs.h"
#include <unordered_set>


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
	{
		this->init(&m_sbuf);
	}

private:
	basic_nullbuf<cT, traits> m_sbuf;
};




//only call on a non-nullptr target. outputs a single Type. doesn't have the right number of fields, but that's probably ok.
//for debug purposes, we don't want to limit it to the number of pointer fields.
inline void output_type(const Tptr target)
{
#ifndef NO_CONSOLE
	print(target, '\n');
#endif
}

//for debugging. outputs a Type and everything it points to, recursively.
inline void pftype(Tptr target)
{
	if (target == 0)
	{
		print("type is null\n");
		return;
	}
	output_type(target);
	for (auto& x : Type_pointer_range(target))
		pftype(x);
}


//only call on a non-nullptr target. outputs a single AST.
inline void output_AST(uAST* target)
{
#ifndef NO_CONSOLE
	print(ou(target), '\n');
#endif
}

#include <set>
//for debugging. outputs an AST and everything it can see, recursively.
inline void pfAST(uAST* target)
{
#ifndef NO_CONSOLE
	if (target == nullptr)
	{
		print("null AST\n");
		return;
	}

	//this makes you not output any AST that has been output before. it prevents infinite loops
	static std::set<uAST*> AST_list;
	if (AST_list.find(target) != AST_list.end())
	{
		print(target, '\n');
		return;
	}
	AST_list.insert(target);

	output_AST(target);
	if (target->tag == ASTn("basicblock"))
	{
		for (auto& x : Vector_range((svector*)target->fields[0]))
			pfAST((uAST*)x);
	}
	else
	{
		uint64_t number_of_further_ASTs = AST_descriptor[target->tag].pointer_fields;
		for (uint64_t x = 0; x < number_of_further_ASTs; ++x)
			if (target->fields[x] != nullptr)
				pfAST(target->fields[x]);
	}
#endif
}

//outputs the AST in a form that can be input into the console
//any time it may start a new basic block, it outputs braces if necessary
//might_be_end_in_BB is to check if you may be starting a new basic block, which really means that your AST is a field in another AST.
//be careful: an AST and its previous basic block elements may need braces when referenced one way, but may not need them when referenced another way (for example, if it's referenced as dependencies of two different ASTs, but is not the primary element in the first). this isn't an issue when reading, but is handled appropriately when outputting.

//bascially, we don't know which ASTs will be referenced until we reach the end. we don't want to output names for everything when it's not necessary.
//thus, we go through the tree once using determine references, then later to do the outputting.
struct output_AST_struct
{
	std::unordered_set<uAST*> AST_list = { nullptr }; //nullptr = 0 is a special value. lists all the ASTs we've come across.
	std::unordered_set<uAST*> reference_necessary; //list of ASTs that have references to them later. these ASTs need to print a name.
	std::ostream& output;
	output_AST_struct(std::ostream& o, const uAST* const_target) : output(o)
	{
		uAST* target = const_cast<uAST*>(const_target);
		determine_references(target);
		AST_list = { nullptr }; //determine_references changes this, so we must reset it
		output_output(target, false);
	}

	void determine_references(uAST* target)
	{
		//print("determining references for ", target, '\n');
		//pfAST(target);
		if (AST_list.find(target) != AST_list.end()) //already seen
		{
			if (reference_necessary.find(target) == reference_necessary.end()) //reference not yet added
				reference_necessary.insert(target);
			return;
		}
		else AST_list.insert(target);

		for (auto& x : AST_range(target))
			determine_references(x);
	}

	//the purpose of "braces_allowed" is to decide whether to output {} or not. if it's false, you're at the top level, and no braces are permitted 
	void output_output(uAST* target, bool braces_allowed)
	{
#ifndef NO_CONSOLE
		
		//we need to know if we printed a _name, in case we are emitting a basic block, because it might try to output its target directly, or maybe just a 0.
		//because if we did print a _name, we better emit the BB errata as well.
		bool printed_reference = false;
		if (AST_list.find(target) != AST_list.end())
		{
			print(target);
			return;
		}
		else
		{
			AST_list.insert(target);
			if (reference_necessary.find(target) != reference_necessary.end())
			{
				print('_', target);
				printed_reference = true;
			}
		}

		if (target->tag == ASTn("basicblock"))
		{
			auto& xvec = target->BBvec();
			if ((printed_reference || xvec->size > 1) && braces_allowed) //we print it no matter what, in case 
				print("{");
			if (xvec->size == 0)
			{
				print("0");
				return;
			}
			else
			{
				bool first = true;
				for (auto& x : Vector_range(xvec))
				{
					if (first == false) print(" ");
					first = false;
					output_output((uAST*)x, true);
				}
			}
			if ((printed_reference || xvec->size > 1) && braces_allowed)
				print("}");
		}
		else
		{
			print("[", AST_descriptor[target->tag].name);
			uint64_t x = 0;
			for (; x < AST_descriptor[target->tag].pointer_fields; ++x)
			{
				print(' ');
				output_output(target->fields[x], true);
			}

			//any additional fields that aren't pointers
			for (; x < get_field_size_of_AST(target->tag); ++x)
				print(' ', target->fields[x]);
			print(']');
		}

#endif
	}
};

inline std::ostream& operator<< (std::ostream& o, const uAST& fred)
{
	output_AST_struct(o, &fred);
	return o;
}

inline void output_AST_console_version(uAST* target) { print(*target, '\n'); }
