#pragma once
#include "types.h"
#include "ASTs.h"
#include "cs11.h"

//function in clouds has a pointer to this object.

struct function
{
	const uAST* the_AST; //it's immutable! note that our mark algorithm relies on the exact integer offset of this object being 0
	Type* return_type; //mark algorithm relies on this exact offset as well.
	Type* parameter_type = 0; //if nonzero, change the mark algorithm
	void* fptr; //the function pointer
	KaleidoscopeJIT* J;
	KaleidoscopeJIT::ModuleHandleT result_module;
	//todo: finiteness
	//function() { the_AST = (uAST*)(this - 1); return_type = (Type*)(this + 1); } //initializing the doubly-linked list.
	function() {}
	function(uAST* a, Type* r, Type* p, void* f, KaleidoscopeJIT* j, KaleidoscopeJIT::ModuleHandleT m) : the_AST(a), return_type(r), parameter_type(p), fptr(f), J(j), result_module(m) {}
	void erase() { J->removeModule(result_module); }
};

//%discarded: functions should have no idea what their type is. instead, the type is stated in pointers to the function.


function* allocate_function();