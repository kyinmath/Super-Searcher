/*
For any Type, get_unique_type() generates a unique version of it. These unique versions are stored 
in type_hash_table.
First, it generates unique versions of any Types in its fields.
Then, it checks type_hash_table if there is already a unique type that is the same, byte-for-byte.
  It can do a byte comparison because the fields are already unique. If we didn't make the fields 
  unique, two copies of [pointer [integer]] would register as different, since they would refer to 
  different copies of [integer]. But since [integer] is first made unique, the bytes of the 
  [pointer [integer]] type are exactly the same.
*/

#include "types.h"
#include "debugoutput.h"
#include "cs11.h"
#include <unordered_set>

//this wrapper type is used for the hash table. we want equality and hashing to occur on a Type, 
//not a Type*, since hashing on pointers is dumb. but we want the hash table to store pointers to 
//types, so that references to them stay valid forever.
struct Type_wrapper_pointer
{
  Type* ptr;
  Type& operator*() { return *ptr; }
  //Type* operator->() { return ptr; }
  //I have no idea how the ->() operator is supposed to be overloaded. see 
  //https://stackoverflow.com/questions/21569483/c-overloading-dereference-operators
  Type_wrapper_pointer(Type* x) : ptr(x) {}
};

namespace std {
  template <> struct hash < Type_wrapper_pointer >
  {
    size_t operator () (const Type_wrapper_pointer& f) const
    {
      uint64_t hash = f.ptr->tag;
      for (int x = 0; x < fields_in_Type; ++x)
        hash ^= f.ptr->fields[x].num;
      return hash;
    }
  };

  //does a bit comparison
  template <> struct equal_to < Type_wrapper_pointer >
  {
    size_t operator () (const Type_wrapper_pointer& l, const Type_wrapper_pointer& r) const
    {
      if (VERBOSE_DEBUG)
      {
        outstream << "testing equal.";
        output_type(l.ptr);
        output_type(r.ptr);
      }
      if (l.ptr->tag != r.ptr->tag)
        return false;
      for (int x = 0; x < fields_in_Type; ++x)
        if (l.ptr->fields[x].num != r.ptr->fields[x].num)
          return false;

      return true;
    }
  };
}

std::unordered_set<Type_wrapper_pointer> type_hash_table; //a hash table of all the unique types.


//an internal function with a bool for speedup.
//the bool is true if you created a type instead of finding a type.
//this means that any types pointing to this type must necessarily not already exist in the type 
//hash table.
std::pair<Type*, bool> get_unique_type_internal(Type* model)
{
  Type temporary_model = *model;

  bool create_new_for_sure; //if this is true, then we don't have to check if this object is in the 
  //hash table. we know it is new.
  //but if it's false, it still might not be new.

  //uniqueify any pointer fields.
  for (int x; x < Type_descriptor[model->tag].pointer_fields; ++x)
  {
    auto result = get_unique_type_internal(model->fields[x].ptr);
    create_new_for_sure |= result.second;
    temporary_model.fields[x] = result.first;
  }
  if (VERBOSE_DEBUG)
  {
    outstream << "type of temporary model ";
    output_type(&temporary_model);
  }

  if (create_new_for_sure)
  {
    Type* handle = new Type(temporary_model);
    type_hash_table.insert(handle);
    return std::make_pair(handle, true);
  }
  auto existing_type = type_hash_table.find(&temporary_model);
  if (existing_type == type_hash_table.end())
  {
    Type* handle = new Type(temporary_model);
    type_hash_table.insert(handle);
    return std::make_pair(handle, true);
  }
  else return std::make_pair(existing_type->ptr, false);

}

Type* get_unique_type(Type* model) { return get_unique_type_internal(model).first; }


void test_unique_types()
{
  Type zero("integer");
  Type one("integer", 1);

  Type* unique_zero = get_unique_type(&zero);
  Type* second_zero = get_unique_type(&zero);
  check(unique_zero == second_zero, "duplicated type doesn't even unique");

  Type* unique_one = get_unique_type(&one);
  check(unique_zero != unique_one, "zero and one uniqued to same element");

  Type pointer_zero("cheap pointer", &zero);
  Type pointer_zero_second("cheap pointer", &zero);
  Type* unique_pointer_zero = get_unique_type(&pointer_zero);
  Type* unique_pointer_zero_second = get_unique_type(&pointer_zero);
  check(unique_pointer_zero == unique_pointer_zero_second, "pointers don't unique");

  Type pointer_one("cheap pointer", &one);
  Type* unique_pointer_one = get_unique_type(&pointer_one);
  check(unique_pointer_zero != unique_pointer_one, "different pointers unique");
}
