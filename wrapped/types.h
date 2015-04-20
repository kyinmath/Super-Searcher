#pragma once
#include <cstdint>
#include <array>
#include <llvm/Support/ErrorHandling.h>

template<typename target_type>
union int_or_ptr
{
  uint64_t num;
  target_type* ptr;
  int_or_ptr(uint64_t x) : num(x) {}
  int_or_ptr(target_type* x) : ptr(x) {}
};

#ifdef _MSC_VER
#define constexpr
#endif

/**
this is a cool C++ hack to attach information to enums.
first, we construct a vector with strings and integers (information).
then, get_enum_from_name() associates numbers to the strings.
this lets us switch-case on the strings.
since the information is in the vector, we are guaranteed not to forget it.
this reduces the potential for careless errors.
*/
struct enum_info
{
  const char* name;
  const unsigned pointer_fields; //how many field elements must be pointers

  //in ASTs, this is the size of the return object. -1 if it must be handled in a special way.
  //in the case of types, the size of the actual object.
  const int size_of_return;

  constexpr enum_info(const char a[], unsigned n, int s)
    : name(a), pointer_fields(n), size_of_return(s) {}
};

//this is an enum which has extra information for each element. it is constexpr so that it can be 
//used in a switch-case statement.
constexpr enum_info AST_descriptor[] =
{
  { "integer", 0, 1 },
  { "Hello World!", 0, 0 },
  { "if", 3, -1 }, //test, first branch, fields[0] branch. passes through the return object of each 
  branch; the return objects must be the same.
  { "scope", 1, 0 }, //fulfills the purpose of {} from C++
  { "add", 2, 1 }, //adds two integers
  { "subtract", 2, 1 },
  { "random", 0, 1 }, //returns a random integer
  { "pointer", 1, 1 }, //acquires a pointer to an alloca'd element. it takes one field, but does 
  NOT compile it - instead, it searches for it in <>objects.
  { "copy", 1, 1 }, //creates a copy of an element. takes one field, but does NOT compile it.
  { "never reached", 0, 0 }, //marks the end of the currently-implemented ASTs. beyond this is 
  rubbish.
  //{ "dereference pointer", 0, 1 },
  { "concatenate", 2, -1 },
  /*	{ "goto", 2 }, //label and failure branch
  { "label" },
  { "no op", 0, 0 },
  { "sub", 2 },
  { "mul", 2 },
  { "divuu", 3 }, //2x int, subAST* on failure
  { "divus", 3 },
  { "divsu", 3 },
  { "divss", 3 },
  { "incr", 1 }, //int
  { "decr", 1 },
  { "less", 2 },
  { "lteq", 2 },
  { "less signed", 2 },
  { "lteq signed", 2 },
  { "equal", 2 },
  { "logical not", 1 },
  { "logical and", 2 },
  { "logical or", 2 },
  { "logical xor", 2 },
  { "bitwise not", 1 },
  { "bitwise and", 2 },
  { "bitwise or", 2 },
  { "bitwise xor", 2 },*/
};


constexpr enum_info type_descriptor[] =
{
  { "concatenate", 2, -1 }, //concatenate two types
  { "integer", 0, 1 }, //64-bit integer
  { "fixed integer", 1, 1 }, //64-bit integer whose value is fixed by the type.
  { "cheap pointer", 1, 1 }, //pointer to anything
  //TODO: ASTs, locks, types
  { "never reached", 0, 0 }
};


//for whatever reason, strcmp is not constexpr. so we do this
constexpr bool static_strequal(const char str1[], const char str2[])
{ return *str1 == *str2 ? (*str1 == '\0' ? true : static_strequal(str1 + 1, str2 + 1)) : false; }


template<constexpr enum_info vector_name[]> constexpr uint64_t get_enum_from_name(const char 
name[])
{
  unsigned k = 0;
  while (1)
  {
    if (static_strequal(vector_name[k].name, name)) return k;
    if (static_strequal("never reached", name))
      llvm_unreachable("tried to get a nonexistent name");
    ++k;
  }
}
constexpr uint64_t ASTn(const char name[])
{ return get_enum_from_name<AST_descriptor>(name); }
constexpr uint64_t Typen(const char name[])
{ return get_enum_from_name<type_descriptor>(name); }

struct Type
{
  uint64_t tag;
  std::array<int_or_ptr<Type>, 2> fields;
  Type(const char name[], const int_or_ptr<Type> a = nullptr, const int_or_ptr<Type> b = nullptr) : 
  tag(Typen(name)), fields{ a, b } {}
  Type(const uint64_t t, const int_or_ptr<Type> a = nullptr, const int_or_ptr<Type> b = nullptr) : 
  tag(t), fields{ a, b } {}
};

#define max_fields_in_AST 4u
//should accomodate the largest possible AST

struct AST
{
  //std::mutex lock;
  uint64_t tag;
  AST* preceding_BB_element; //this object survives on the stack and can be referenced. it's the 
  previous basic block element.
  std::array<int_or_ptr<AST>, max_fields_in_AST> fields;
  AST(const char name[], AST* preceding = nullptr, int_or_ptr<AST> f1 = nullptr, int_or_ptr<AST> f2 
  = nullptr, int_or_ptr<AST> f3 = nullptr, int_or_ptr<AST> f4 = nullptr)
    : tag(ASTn(name)), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {} //VS complains 
    about aggregate initialization, but it is wrong.
  AST(unsigned direct_tag, AST* preceding = nullptr, int_or_ptr<AST> f1 = nullptr, int_or_ptr<AST> 
  f2 = nullptr, int_or_ptr<AST> f3 = nullptr, int_or_ptr<AST> f4 = nullptr)
    : tag(direct_tag), preceding_BB_element(preceding), fields{ f1, f2, f3, f4 } {}
  //watch out and make sure we remember _preceding_! maybe we'll use named constructors later
};



enum type_status { RVO, reference }; //distinguishes between RVOing an object, or just creating a 
reference

unsigned type_check(type_status version, Type* existing_reference, Type* new_reference);
