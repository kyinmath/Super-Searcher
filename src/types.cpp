#include <stack>
#include "types.h"
#include "debugoutput.h"

/* this checks if a new reference can be bound to an old reference.

we can only convert things that are FULLY IMMUT. just immut is not enough, because it might change anyway (perhaps it's inside a lock, or TU).
that is, the fields[0] pointer must never be able to change those things.
the first pointer's validity must assuredly last longer than the fields[0] pointer.
so if the first pointer says the lock is immut, then we can treat it as fully immut. because it is immut for the duration of the fields[0] pointer's life.
fully immut still requires the actual lock to also be immut, before you can start converting.

full pointers have to match the entire type.
thus, we return: 0 if inequal for sure, 1 if fields[0] points to a valid subtype, and 2 if fields[0] points to a valid conversion that fills the entire type
if version = reference, it checks if the first type can be referenced by the fields[0].

if version == RVO: checks if an object of the first type can be placed in a memory slot of the fields[0] type. that means that the fields[0] type is what everyone will refer to it as.
don't worry about lock states of integers or pointers; the target will absorb the lock state anyway.
example: an immut TU of pointer, 0, that's been fixed to a pointer, can be converted into just a pointer type.

with RVO, first must be smaller than fields[0]. with reference, it's the opposite.
*/


type_check_result type_check_once(type_status version, Tptr existing_reference, Tptr new_reference);
type_check_result type_check(type_status version, Tptr existing_reference, Tptr new_reference)
{
	std::array<Tptr, 2> iter{{existing_reference, new_reference}};
	if (VERBOSE_DEBUG)
	{
		print("type_checking these types: ");
		output_type(existing_reference);
		output_type(new_reference);
	}
	if (iter[0] == 0 || iter[1] == 0) //at least one is nullptr
	{
		if (iter[0] == 0 && iter[1] == 0)
			return type_check_result::perfect_fit; //success!
		else if (iter[1] == 0)
			return type_check_result::existing_reference_too_large;
		else return type_check_result::new_reference_too_large;
	}
	if (iter[0] == iter[1]) return type_check_result::perfect_fit; //easy way out, if lucky. we can't do this later, because our stack might have extra things to look at.
	if (existing_reference == u::does_not_return) return type_check_result::perfect_fit; //nonexistent means that the code path is never seen.
	if (existing_reference.ver() != Typen("con_vec")) //not a concatenation
	{
		if (new_reference.ver() != Typen("con_vec"))
			return type_check_once(version, existing_reference, new_reference);
		else return type_check_result::different;
	}
	else if (new_reference.ver() != Typen("con_vec")) //then the other one better be a concatenation too
		return type_check_result::different;
	else
	{
		uint64_t number_of_fields;
		type_check_result best_case_scenario;
		if (existing_reference.field(0) > new_reference.field(0))
		{
			best_case_scenario = type_check_result::existing_reference_too_large;
			number_of_fields = new_reference.field(0);
		}
		else if (existing_reference.field(0) < new_reference.field(0))
		{
			best_case_scenario = type_check_result::new_reference_too_large;
			number_of_fields = existing_reference.field(0);
		}
		else
		{
			best_case_scenario = type_check_result::perfect_fit;
			number_of_fields = existing_reference.field(0);
		}
		for (uint64_t x = 1; x < number_of_fields + 1; ++x)
		{
			if (type_check_once(version, existing_reference.field(x), new_reference.field(x)) != type_check_result::perfect_fit)
				return type_check_result::different;
		}
		return best_case_scenario;
	}
}

//can't take a concatenation type.
type_check_result type_check_once(type_status version, Tptr existing_reference, Tptr new_reference)
{
	std::array<Tptr, 2> iter{{existing_reference, new_reference}};
	//u::does_not_return is a special value. we can't be handling it here.
	check(iter[0] != u::does_not_return, "can't handle nonexistent types in type_check_once()");
	check(iter[1] != u::does_not_return, "can't handle nonexistent types in type_check_once()");

	
	/*if fully immut + immut, the new reference can't change the object
	if RVO, there's no old reference to interfere with changes to the base object. but if it's a pointer, all bets are off.
	thus, cheap pointers can convert to integers
	*/
	if (version == RVO)
	{
		switch (iter[1].ver())
		{
		case Typen("vector"): //for now, this is ok being here because the interior type must have size 1.
		case Typen("pointer"):
			if (iter[0].ver() == iter[1].ver())
			{
				auto result = type_check(reference, iter[0].field(0), iter[1].field(0));
				if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
					return type_check_result::perfect_fit;
			}
			else return type_check_result::different;
		case Typen("integer"):
			if (iter[0].ver() == iter[1].ver() || iter[0].ver() == Typen("pointer") || iter[0].ver() == Typen("type pointer") || iter[0].ver() == Typen("function pointer") || iter[0].ver() == Typen("AST pointer"))
				return type_check_result::perfect_fit;
			return type_check_result::different;

		case Typen("dynamic object"):
		case Typen("AST pointer"):
		case Typen("function pointer"):
		case Typen("type pointer"):
			if (iter[0].ver() == iter[1].ver())
				return type_check_result::perfect_fit;
			//fall through to the ::different result.
		case Typen("vector of something"): //we'd need a way to help if() to stick two of these together. also, a pointer to something can't be stored in another pointer to something. for example, we expose dynamic subobjects as pointers and vectors to something.
		case Typen("pointer to something"): //same here.
			return type_check_result::different;

		default:
			error("default switch in fully immut/RVO branch " + std::string(Type_descriptor[iter[1].ver()].name));
		}
	}

	//else, it's not fully immut. remember: some things are naturally immut, like fixed uint64. in that case, check fully_immut, but the lock state doesn't matter


	else
	{
		//for pointers, we cannot pass in fully_immut. because the old reference must be valid too, and we can't point to anything more general than the old reference.

		//first type field must be the same
		if (iter[1].ver() != iter[0].ver()) return type_check_result::different;

		switch (iter[1].ver())
		{
		case Typen("vector"): //for now, this is ok being here because the interior type must have size 1.
		case Typen("pointer"):
			{
				auto result = type_check(reference, iter[0].field(0), iter[1].field(0));
				if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
					return type_check_result::perfect_fit;
				return type_check_result::different;
			}
		case Typen("integer"):
		case Typen("AST pointer"):
		case Typen("function pointer"):
		case Typen("type pointer"):
		case Typen("dynamic object"):
		case Typen("vector of something"):
		case Typen("pointer to something"):
				return type_check_result::perfect_fit;
	
		default:
			error("default case in other branch");
		}
	}
	error("end of type_check");
}

//this takes a vector of types, and then puts them in concatenation. to do this, it first expands out the types into their components, then shoves them back together
//automatically uses uniquefy_premade_type()
Tptr concatenate_types(llvm::ArrayRef<Tptr> components)
{
	std::vector<Tptr> true_components{}; //every Type here is a single element, not a concatenation.

	for (auto& x : components)
	{
		if (x == 0) continue;
		if (x.ver() == Typen("con_vec"))
		{
			for (Tptr& k : Type_pointer_range(x))
				true_components.push_back(k);
		}
		else true_components.push_back(x);
	}
	
	//print("size of true components is ", true_components.size();
	//output_type(components[0]);
	//output_type(true_components[1]);

	if (true_components.size() == 0) return 0; //only thing in the vector is the size
	if (true_components.size() == 1) return true_components[0]; //one element in the vector.
	else return new_unique_type(Typen("con_vec"), true_components);
}