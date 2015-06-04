#include <string>
#include <iostream>
extern std::ostream& outstream;
using std::string;
[[noreturn]] inline void error(const string& Str) { outstream << "Error: " << Str << '\n'; abort(); }

//the condition is true when the program behaves normally.
inline void check(bool condition, const string& Str) { if (!condition) error(Str); }

inline void debugcheck(bool condition, const string& Str) { if (!condition) error(Str); }