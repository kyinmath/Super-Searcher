#include <stack>
#include "types.h"
#include "debugoutput.h"
#include "cs11.h"

namespace T
{
	constexpr Type internal::int_;
	constexpr Type internal::nonexistent;
	constexpr Type internal::missing_field;
	constexpr Type internal::special_return;
	constexpr Type internal::parameter_no_type_check;
	constexpr Type internal::cheap_dynamic_pointer;
	constexpr Type internal::full_dynamic_pointer;
	constexpr Type internal::AST_pointer;
};

/* this checks if a new reference can be bound to an old reference.


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
*/

//this ensures that the static variable address is the same across translation units.
//call it with T::nonexistent from a different translation unit
void debugtypecheck(Type* test)
{
	if (test != T::nonexistent)
	{
		console << "constexpr address error.\n";
		output_type(T::nonexistent);
		output_type(test);
		abort();
	}
}

type_check_result type_check_once(type_status version, Type* existing_reference, Type* new_reference);
type_check_result type_check(type_status version, Type* existing_reference, Type* new_reference)
{
	std::array<Type*, 2> iter{{existing_reference, new_reference}};
	if (VERBOSE_DEBUG)
	{
		console << "type_checking these types: ";
		output_type(existing_reference);
		output_type(new_reference);
	}
	if (iter[0] == nullptr || iter[1] == nullptr) //at least one is nullptr
	{
		if (iter[0] == nullptr && iter[1] == nullptr)
			return type_check_result::perfect_fit; //success!
		else if (iter[1] == nullptr)
			return type_check_result::existing_reference_too_large;
		else return type_check_result::new_reference_too_large;
	}
	if (iter[0] == iter[1]) return type_check_result::perfect_fit; //easy way out, if lucky. we can't do this later, because our stack might have extra things to look at.
	if (existing_reference == T::nonexistent) return type_check_result::perfect_fit; //nonexistent means that the code path is never seen.
	if (existing_reference->tag != Typen("con_vec")) //not a concatenation
	{
		if (new_reference->tag != Typen("con_vec"))
			return type_check_once(version, existing_reference, new_reference);
		else return type_check_result::different;
	}
	else if (new_reference->tag != Typen("con_vec")) //then the other one better be a concatenation too
		return type_check_result::different;
	else
	{
		uint64_t number_of_fields;
		type_check_result best_case_scenario;
		if (existing_reference->fields[0].num > new_reference->fields[0].num)
		{
			best_case_scenario = type_check_result::existing_reference_too_large;
			number_of_fields = new_reference->fields[0].num;
		}
		else if (existing_reference->fields[0].num < new_reference->fields[0].num)
		{
			best_case_scenario = type_check_result::new_reference_too_large;
			number_of_fields = existing_reference->fields[0].num;
		}
		else
		{
			best_case_scenario = type_check_result::perfect_fit;
			number_of_fields = existing_reference->fields[0].num;
		}
		for (uint64_t x = 2; x < number_of_fields + 2; ++x)
		{
			uint64_t* e = (uint64_t*)existing_reference;
			uint64_t* n = (uint64_t*)new_reference;
			if (type_check_once(version, (Type*)e[x], (Type*)n[x]) != type_check_result::perfect_fit)
				return type_check_result::different;
		}
		return best_case_scenario;
	}
}

//can't take a concatenation type.
type_check_result type_check_once(type_status version, Type* existing_reference, Type* new_reference)
{
	std::array<Type*, 2> iter{{existing_reference, new_reference}};
	//T::nonexistent is a special value. we can't be handling it here.
	check(iter[0] != T::nonexistent, "can't handle nonexistent types in type_check()");
	check(iter[1] != T::nonexistent, "can't handle nonexistent types in type_check()");

	
	/*if fully immut + immut, the new reference can't change the object
	if RVO, there's no old reference to interfere with changes
	thus, cheap pointers can convert to integers
	*/
	if (version == RVO)
	{
		switch (iter[1]->tag)
		{
		case Typen("pointer"):
			if (iter[0]->tag == iter[1]->tag)
			{
				if (iter[0]->fields[1].num >= iter[1]->fields[1].num) //full pointers can RVO into cheap pointers.
				{
					auto result = type_check(reference, iter[0]->fields[0].ptr, iter[1]->fields[0].ptr);
					if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
						return type_check_result::perfect_fit;
				}
			}
			else return type_check_result::different;
		case Typen("integer"):
				if (iter[0]->tag == iter[1]->tag || iter[0]->tag == Typen("pointer"))
					return type_check_result::perfect_fit;
			//maybe we should also allow two uints in a row to take a dynamic pointer?
				//we'd have to think about that. the current system allows for large types in the new reference to accept pieces, but I don't know if that's the best.
				return type_check_result::different;

		case Typen("dynamic pointer"):

			if (iter[0]->fields[0].num >= iter[1]->fields[0].num) //full pointers can RVO into cheap pointers.
			{
				if (iter[0]->tag == iter[1]->tag)
					return type_check_result::perfect_fit;
			}
			return type_check_result::different;

		case Typen("AST pointer"):
			if (iter[0]->tag == iter[1]->tag)
				return type_check_result::perfect_fit;
			return type_check_result::different;
		default:
			error("default switch in fully immut/RVO branch " + std::string(Type_descriptor[iter[1]->tag].name));
		}
	}

	//else, it's not fully immut. remember: some things are naturally immut, like fixed uint64. in that case, check fully_immut, but the lock state doesn't matter


	else
	{
		//for pointers, we cannot pass in fully_immut. because the old reference must be valid too, and we can't point to anything more general than the old reference.

		//first type field must be the same
		if (iter[1]->tag != iter[0]->tag) return type_check_result::different;

		switch (iter[1]->tag)
		{
		case Typen("pointer"):
			{
				if (iter[0]->fields[1].num >= iter[1]->fields[1].num) //full pointers can be thought of as cheap pointers.
				{
					auto result = type_check(reference, iter[0]->fields[0].ptr, iter[1]->fields[0].ptr);
					if (result == type_check_result::existing_reference_too_large || result == type_check_result::perfect_fit)
						return type_check_result::perfect_fit;
				}
				return type_check_result::different;
			}
		case Typen("integer"):
		case Typen("AST pointer"):
			return type_check_result::perfect_fit;

		case Typen("dynamic pointer"):
			if (iter[0]->fields[0].num >= iter[1]->fields[0].num) //full pointers can be thought of as cheap pointers
				return type_check_result::perfect_fit;
			else return type_check_result::different;
	
		default:
			error("default case in other branch");
		}
	}
	error("end of type_check");
}

#include <functional>
//this takes a vector of types, and then puts them in concatenation. none of them is allowed to be a concatenation.
Type* concatenate_types(llvm::ArrayRef<Type*> components)
{
	std::vector<Type*> true_components; //every Type here is a single element, not a concatenation.
	for (auto& x : components)
	{
		if (x == nullptr) continue;
		if (x->tag == Typen("con_vec"))
		{
			uint64_t last_offset_field = x->fields[0].num + 2;
			for (uint64_t k = 2; k < last_offset_field; ++k)
			{
				Type** conc_type = (Type**)x;
				true_components.push_back(conc_type[k]);
			}
		}
		else true_components.push_back(x);
	}

	if (true_components.size() == 0) return nullptr;
	if (true_components.size() == 1) return components[0];
	else
	{
		uint64_t* new_type = new uint64_t[true_components.size() + 2];
		new_type[0] = Typen("con_vec");
		new_type[1] = true_components.size();
		for (int idx = 0; idx < true_components.size(); ++idx) new_type[idx + 2] = (uint64_t)true_components[idx];
		return (Type*)new_type;
	}
}

bool is_full(Type* t)
{
	if (t == nullptr) return true;
	else switch (t->tag)
	{
	case Typen("con_vec"):
		{
			uint64_t fields = t->fields[0].num;
			for (uint64_t idx = 0; idx < fields; ++idx)
				if (!is_full(t->fields[idx + 1].ptr)) return false; //I hope this works
			return true;
		}
	case Typen("pointer"):
	case Typen("dynamic pointer"):
		if (t->fields[1].num != 0 && t->fields[1].num != 1)
			error("cases we haven't considered");
		return t->fields[1].num == 1;
	default: return true;
	}
}