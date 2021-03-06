#pragma once
#include <string>
#include <iostream>
#include <llvm/Support/raw_ostream.h> 
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>

extern llvm::raw_ostream* llvm_console;
using std::string;
extern bool QUIET;
extern bool OPTIMIZE;
extern bool INTERACTIVE;
extern bool CONSOLE;
extern bool TIMER;
extern bool DONT_ADD_MODULE_TO_ORC;
extern bool DELETE_MODULE_IMMEDIATELY;
extern bool OUTPUT_MODULE;
extern bool SERIALIZE_ON_EXIT;
constexpr bool HEURISTIC = false; //heuristically gives errors. for example, large objects are assumed to be bad.


#ifndef NO_CONSOLE
inline void print(){}

template<typename First, typename ...Rest>
inline void print(First && first, Rest && ...rest)
{
	if (!QUIET)
	{
		std::cerr << std::forward<First>(first);
		print(std::forward<Rest>(rest)...);
	}
}
#else
#define QUIET true
template<typename ...Rubbish> inline void print(Rubbish && ...rest){}
#endif

extern bool VERBOSE_DEBUG;
//llvm::StringRef disallowed because cout can't take it
[[noreturn]] inline constexpr void error(const string& Str) { std::cerr << "Error: " << Str << '\n'; abort(); } //std::cerr, because we want error messages even when default console output (print) is turned off.
//later, we'll want it to work in a big way by logging and everything. and then we'll have "small errors", for OOM and such.


//s("test") returns "test" if enabled, and nothing otherwise
//check() calls abort if the condition is false. beware: in release mode, the expression inside will not be executed.
//the condition is true when the program behaves normally.
#ifdef NOCHECK
#define check(x, y)
inline std::string s(std::string k) { return ""; }
#else
inline std::string s(std::string k) { return k; }
inline constexpr void check(bool condition, const string& Str) { if (!condition) error(Str); }
#endif
//future: expanding memory. (or, acquire maximum available memory? nah, we'll let the OOM killer work)
//the constexpr objects are outside of our memory pool. they're quite exceptional, since they can't be GC'd. 
//thus, we wrap them in a unique() function, so that the user only ever sees GC-handled objects.
#ifdef NOCHECK
constexpr bool DEBUG_GC = false;
#else
constexpr bool DEBUG_GC = true; //do some checking to make sure the GC is tracking free memory accurately. slow. mainly: whenever GCing or creating, it sets memory locations to special values.
#endif
constexpr bool VERBOSE_GC = false;
constexpr bool SUPER_VERBOSE_GC = false; //some additional, extremely noisy output. print every single living value at GC time.
constexpr bool VERBOSE_VECTOR = false;
extern bool UNSERIALIZATION_MODE; //if this is true, then the GC should act in unserialization mode instead of GC sweeping mode.

//for the memory allocator
constexpr const uint64_t pool_size = 100000ull;
constexpr const uint64_t function_pool_size = 2000ull * 64;
constexpr const uint64_t initial_special_value = 21212121ull;
constexpr const uint64_t collected_special_value = 1234567ull;

extern uint64_t pointer_offset; //when unserializing, the difference between the new and old pointers
extern uint64_t function_pointer_offset;

//ok, so these two are special: they work like a stack. when you want to work with a context/builder, you push it here. and when you're done, you pop it from here.
//otherwise, everything using them would have to carry around references to them. and then when you wanted to use llvm_array, you'd need to bind references to it.
extern llvm::LLVMContext* context;
extern llvm::IRBuilder<>* IRB;
extern llvm::TargetMachine* TM; //can't be initialized statically