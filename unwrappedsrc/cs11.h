extern bool OPTIMIZE;
extern bool VERBOSE_DEBUG;
extern bool INTERACTIVE;
extern bool CONSOLE;
extern bool TIMER;
#include <iostream>
extern std::ostream& outstream;


#include <llvm/IR/LLVMContext.h>
#ifndef _MSC_VER
thread_local
#endif
extern llvm::LLVMContext& thread_context;