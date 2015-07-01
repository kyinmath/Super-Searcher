#pragma once
#include <string>
#include <iostream>
extern std::ostream& console;
#include <llvm/Support/raw_ostream.h> 
extern llvm::raw_ostream* llvm_console;
using std::string;

extern bool VERBOSE_DEBUG;
[[noreturn]] inline void error(const string& Str) { std::cout << "Error: " << Str << '\n'; abort(); } //std::cout, because we want error messages even when default console output is turned off. which sets failbit on cerr.

//the condition is true when the program behaves normally.
inline void check(bool condition, const string& Str) { if (!condition) error(Str); }