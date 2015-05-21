#include <string>
#include "cs11.h"
using std::string;
inline void error(string Str) { outstream << "Error: " << Str << '\n'; abort(); }

//the condition is true when the program behaves normally.
inline void check(bool condition, string Str) { if (!condition) error(Str); }

inline void debugcheck(bool condition, string Str) { if (!condition) error(Str); }
inline void debugerror(bool condition, string Str) { if (!condition) error(Str); }