#include "helperfunctions.h"
#include "cs11.h"

llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
llvm::Type* void_type = llvm::Type::getVoidTy(thread_context);