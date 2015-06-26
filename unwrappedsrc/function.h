#pragma once
#include "types.h"
#include "ASTs.h"
#include "cs11.h"

//function in clouds has a pointer to this object.

struct function
{
	const uAST* the_AST; //it's immutable! this must always come first in the function type: our mark algorithm relies on it.
	Type* return_type;
	Type* parameter_type = 0;
	void* function; //the function pointer
	KaleidoscopeJIT* J;
	llvm::Module* M;
	//todo: finiteness
};