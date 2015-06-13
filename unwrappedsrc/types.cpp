#include <stack>
#include "types.h"
#include "debugoutput.h"
#include "cs11.h"

/*
the docs in this file are incomplete.
this checks if types are equivalent, but with many special cases depensing on the nature of the two types.


we can only convert things that are FULLY IMMUT. just immut is not enough, because it might change anyway (perhaps it's inside a lock, or TU).
that is, the fields[0] pointer must never be able to change those things.
the first pointer's validity must assuredly last longer than the fields[0] pointer.
so if the first pointer says the lock is immut, then we can treat it as fully immut. because it is immut for the duration of the fields[0] pointer's life.
fully immut still requires the actual lock to also be immut, before you can start converting.

we have two problems: 1. we might be interested in offsets. solution: you use another function to acquire the appropriate subtype.

full pointers have to match the entire type.
thus, we return: 0 if inequal for sure, 1 if fields[0] points to a valid subtype, and 2 if fields[0] points to a valid conversion that fills the entire type
if version = reference, it checks if the first type can be referenced by the fields[0].
*/
/*if version == RVO: checks if an object of the first type can be placed in a memory slot of the fields[0] type. that means that the fields[0] type is what everyone will refer to it as.
don't worry about lock states of integers or pointers; the target will absorb the lock state anyway.
example: an immut TU of pointer, 0, that's been fixed to a pointer, can be converted into just a pointer type.

with RVO, first must be smaller than fields[0]. with reference, it's the opposite.
so: 0 = different. 1 = new reference is too large. 2 = existing reference is too large. 3 = perfect match.
*/

type_check_result type_check(type_status version, Type* existing_reference, Type* new_reference)
{
	std::array<Type*, 2> iter = { existing_reference, new_reference };
	if (iter[0] == iter[1])// && (lock[0] == lock[1] || version == RVO))
		return type_check_result::perfect_fit; //easy way out, if lucky. we can't do this later, because our stack might have extra things to look at.

	if (VERBOSE_DEBUG)
	{
		outstream << "type_checking these types: ";
		output_type(existing_reference);
		output_type(new_reference);
	}
	if (existing_reference != nullptr)
		if (existing_reference->tag == Typen("nonexistent"))
			return type_check_result::perfect_fit; //nonexistent means that the code path is never seen.
	//todo: if we actually just check against the const T::nonexistent, it seems to test false. why isn't it uniqued properly?

	//this stack enables two-part recursion using both type objects, which isn't possible otherwise.
	//fully_immut must always be exactly the same for both types, so we technically don't need to store it twice. but we do anyway, for now.
	struct stack_element{
		Type* iter;
		stack_element(Type* a) : iter(a) {}
	};
	auto consume_until_physical = [](Type*& iterator, std::stack<stack_element>& history)
	{
	beginning:
		if (iterator == nullptr)
			return;

		switch (iterator->tag)
		{
		case Typen("concatenate"):
			history.push(stack_element(iterator->fields[1].ptr));
			iterator = iterator->fields[0].ptr;
			goto beginning;
		default:
			break;
		}
	};

	std::array<std::stack<stack_element>, 2> stack;

check_next_token:
	//acquire items until we reach a physical token. this makes sure that we're left at a valid, space-consuming thing to easily compare.
	//however, it does NOT acquire or process the object that we're left at. so if there's a TU, then the locks have not been erased
	consume_until_physical(iter[0], stack[0]);
	consume_until_physical(iter[1], stack[1]);

	if (iter[0] == nullptr || iter[1] == nullptr) //at least one is nullptr
	{
		bool found_no_further_things_to_do = true; //we check our stack for more elements. if we find one, then this becomes 0.
		for (unsigned x : {0, 1})
		{
			if (!stack[x].empty() && iter[x] == nullptr)
			{
				iter[x] = stack[x].top().iter;
				stack[x].pop();
				found_no_further_things_to_do = false;
			}
		}
		if (found_no_further_things_to_do)
		{
			if (iter[0] == nullptr && iter[1] == nullptr)
				return type_check_result::perfect_fit; //success!
			else if (iter[1] == nullptr)
				return type_check_result::existing_reference_too_large;
			else return type_check_result::new_reference_too_large;
		}
		else goto check_next_token;
	}
	//T::nonexistent is a special value. we can't be handling it here.
	check(iter[0] != T::nonexistent, "can't handle nonexistent types in type_check()");
	check(iter[1] != T::nonexistent, "can't handle nonexistent types in type_check()");


	/*if fully immut + immut, the new reference can't change the object
	if RVO, there's no old reference to interfere with changes

	immut TUs can implicitly convert into more diverse immut TUs
	fixed integers can convert to integers
	cheap pointers can convert to integers
	*/
	if (version == RVO)
	{
		switch (iter[1]->tag)
		{
		case Typen("cheap pointer"):
			if (iter[0]->tag == iter[1]->tag)
			{
				auto result = type_check(reference, iter[0]->fields[0].ptr, iter[1]->fields[0].ptr);
				if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
					goto finished_checking;
			}
			return type_check_result::different; //else
			/*
		case Typen("fixed integer"): //no need for lock check.
			if (iter[0]->tag == iter[1]->tag)
				if (iter[1]->fields[0].num == iter[0]->fields[0].num)
					goto finished_checking;
			return 0;*/
		case Typen("integer"):
				if (iter[0]->tag == iter[1]->tag /*|| iter[0]->tag == Typen("fixed integer") */|| iter[0]->tag == Typen("cheap pointer")) //also full pointer
					goto finished_checking;
			//maybe we should also allow two uints in a row to take a dynamic pointer?
				//we'd have to think about that. the current system allows for large types in the new reference to accept pieces, but I don't know if that's the best.
				return type_check_result::different;

		case Typen("dynamic pointer"):
		case Typen("AST pointer"):
			if (iter[0]->tag == iter[1]->tag)
				goto finished_checking;
			return type_check_result::different;
		default:
			error("default switch in fully immut/RVO branch");
		}
	}

	//TODO: else, it's not fully immut. remember: some things are naturally immut, like fixed uint64. in that case, check fully_immut, but the lock state doesn't matter


	else
	{
		//for pointers, we cannot pass in fully_immut. because the old reference must be valid too, and we can't point to anything more general than the old reference.

		//first type field must be the same
		if (iter[1]->tag != iter[0]->tag) return type_check_result::different;

		switch (iter[1]->tag)
		{
		case Typen("cheap pointer"):
			{
				auto result = type_check(reference, iter[0]->fields[0].ptr, iter[1]->fields[0].ptr);
				if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
					goto finished_checking;
				return type_check_result::different;
			}
		/*
		case Typen("fixed integer"): //no need for lock check.
			if (iter[1]->fields[0].num == iter[0]->fields[0].num)
				goto finished_checking;
			return 0; */
		case Typen("integer"):
		case Typen("AST pointer"):
			goto finished_checking;

		case Typen("dynamic pointer"):
			goto finished_checking;
	
		default:
			error("default case in other branch");
		}
	}
	error("end of type_check");
finished_checking:
	iter[0] = iter[1] = nullptr; //rely on the beginning to set them properly.
	goto check_next_token;
}

#include <functional>
//this takes a vector of types, and then puts them in concatenation
Type* concatenate_types(std::vector<Type*>& components)
{
	std::function<Type*(Type**, int)> concatenate_types = [&concatenate_types](Type** array_of_type_pointers, int number_of_terms)
	{
		if (number_of_terms == 1) return *array_of_type_pointers;
		else return new Type("concatenate", *array_of_type_pointers, concatenate_types(array_of_type_pointers + 1, number_of_terms - 1));
	};

	if (components.size() == 0) return nullptr;
	else return concatenate_types(components.data(), components.size());
}
