#pragma once
#include <string>
#include <iostream>
#include <llvm/Support/raw_ostream.h> 
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#define RELEASE 0

extern std::ostream& console;
extern llvm::raw_ostream* llvm_console;
using std::string;

extern bool VERBOSE_DEBUG;
[[noreturn]] inline constexpr void error(const string& Str) { std::cout << "Error: " << Str << '\n'; abort(); } //std::cout, because we want error messages even when default console output is turned off. which sets failbit on cerr.


//s("test") returns "test" if debug_names is true, and an empty string if debug_names is false.
#define debug_names
#ifdef debug_names
inline std::string s(std::string k) { return k; }
#else
inline std::string s(std::string k) { return ""; }
#endif

//the condition is true when the program behaves normally.
//#if RELEASE
//inline void check(bool condition, const string& Str) {}
//#else
inline constexpr void check(bool condition, const string& Str = "") { if (!condition) error(Str); }
//#endif

extern bool OPTIMIZE;
extern bool INTERACTIVE;
extern bool CONSOLE;
extern bool TIMER;
extern bool DONT_ADD_MODULE_TO_ORC;
extern bool DELETE_MODULE_IMMEDIATELY;
extern bool DEBUG_GC;
extern bool OUTPUT_MODULE;


//ok, so these two are special: they work like a stack. when you want to work with a context/builder, you push it here. and when you're done, you pop it from here.
//otherwise, everything using them would have to carry around references to them. and then when you wanted to use llvm_array, you'd need to bind references to it.
extern  llvm::LLVMContext* context;
extern  llvm::IRBuilder<>* builder;
extern llvm::TargetMachine* TM; //can't be initialized statically