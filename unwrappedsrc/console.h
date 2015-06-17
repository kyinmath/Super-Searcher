#pragma once
#include <string>
#include <iostream>
extern std::ostream& console;
#include <llvm/Support/raw_ostream.h> 
extern llvm::raw_ostream* llvm_console;
using std::string;
[[noreturn]] inline void error(const string& Str) { console << "Error: " << Str << '\n'; abort(); }

//the condition is true when the program behaves normally.
inline void check(bool condition, const string& Str) { if (!condition) error(Str); }