#include <string>
#include <iostream>
extern std::ostream& outstream;
#include <llvm/Support/raw_ostream.h> 
extern llvm::raw_ostream* llvm_outstream;
using std::string;
[[noreturn]] inline void error(const string& Str) { outstream << "Error: " << Str << '\n'; abort(); }

//the condition is true when the program behaves normally.
inline void check(bool condition, const string& Str) { if (!condition) error(Str); }