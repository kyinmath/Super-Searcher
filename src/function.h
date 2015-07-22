#pragma once
#include "types.h"
#include "ASTs.h"
#include "orc.h"

//function in clouds has a pointer to this object.

extern bool VERBOSE_GC;
struct function
{
	uAST* the_AST; //note that our mark algorithm relies on the exact integer offset of this object being 0
	Tptr return_type; //mark algorithm relies on this exact offset as well.
	Tptr parameter_type = 0; //if nonzero, change the mark algorithm
	void* fptr; //the function pointer
	KaleidoscopeJIT* thread_jit;
	KaleidoscopeJIT::ModuleHandleT result_module;
	std::unique_ptr<llvm::LLVMContext> context;
	//todo: finiteness
	//function() { the_AST = (uAST*)(this - 1); return_type = (Tptr)(this + 1); } //initializing the doubly-linked list.
	function(uAST* a,Tptr r,Tptr p, void* f, KaleidoscopeJIT* j, KaleidoscopeJIT::ModuleHandleT m, std::unique_ptr<llvm::LLVMContext> c)
		: the_AST(a), return_type(r), parameter_type(p), fptr(f), thread_jit(j), result_module(m), context(std::move(c)) {}
	~function()
	{
		if (!DONT_ADD_MODULE_TO_ORC && !DELETE_MODULE_IMMEDIATELY)
		{
			if (VERBOSE_GC) print("removing module, where this is ", this, "\n");
			thread_jit->removeModule(result_module);
		}
	}
};

inline std::ostream& operator<< (std::ostream& o, const function& fred)
{
	return o << "function at " << &fred << " with AST " << fred.the_AST << " return " << fred.return_type << " fptr " << fred.fptr << " jit " << fred.thread_jit << '\n';
}

function* allocate_function();