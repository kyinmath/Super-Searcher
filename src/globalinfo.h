#pragma once
#include <string>
#include <iostream>
#include <llvm/Support/raw_ostream.h> 
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>

extern std::ostream& console;
extern llvm::raw_ostream* llvm_console;
using std::string;


#ifndef NO_CONSOLE
inline void print(){}

template<typename First, typename ...Rest>
inline void print(First && first, Rest && ...rest)
{
	console << std::forward<First>(first);
	print(std::forward<Rest>(rest)...);
}
#else

template<typename ...Rubbish> inline void print(Rubbish && ...rest){}
#endif

extern bool VERBOSE_DEBUG;
//llvm::StringRef disallowed because cout can't take it
[[noreturn]] inline constexpr void error(const string& Str) { std::cout << "Error: " << Str << '\n'; abort(); } //std::cout, because we want error messages even when default console output is turned off. which sets failbit on cerr.


//s("test") returns "test" if enabled, and nothing otherwise
//check() calls abort if the condition is false. beware: in release mode, the expression inside will not be executed.
//the condition is true when the program behaves normally.
#ifdef NOCHECK
#define check(x, y)
inline std::string s(std::string k) { return ""; }
#else
inline std::string s(std::string k) { return k; }
inline constexpr void check(bool condition, const string& Str = "") { if (!condition) error(Str); }
#endif

extern bool OPTIMIZE;
extern bool INTERACTIVE;
extern bool CONSOLE;
extern bool TIMER;
extern bool DONT_ADD_MODULE_TO_ORC;
extern bool DELETE_MODULE_IMMEDIATELY;
extern bool OUTPUT_MODULE;
constexpr bool HEURISTIC = true; //heuristically gives errors. for example, large objects are assumed to be bad.

//todo: expanding memory. (or, acquire maximum available memory? nah, we'll let the OOM killer work)
//the constexpr objects are outside of our memory pool. they're quite exceptional, since they can't be GC'd. 
//thus, we wrap them in a unique() function, so that the user only ever sees GC-handled objects.
#ifdef NOCHECK
constexpr bool DEBUG_GC = false;
#else
constexpr bool DEBUG_GC = true; //do some checking to make sure the GC is tracking free memory accurately. slow. mainly: whenever GCing or creating, it sets memory locations to special values.
#endif
constexpr bool VERBOSE_GC = false;
constexpr bool SUPER_VERBOSE_GC = false; //some additional, extremely noisy output. print every single living value at GC time.
constexpr bool VECTOR_DEBUG = false;

//ok, so these two are special: they work like a stack. when you want to work with a context/builder, you push it here. and when you're done, you pop it from here.
//otherwise, everything using them would have to carry around references to them. and then when you wanted to use llvm_array, you'd need to bind references to it.
extern llvm::LLVMContext* context;
extern llvm::IRBuilder<>* builder;
extern llvm::TargetMachine* TM; //can't be initialized statically