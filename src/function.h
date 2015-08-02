#pragma once
#include "types.h"
#include "ASTs.h"
#include "orc.h"
#include <iomanip>

//function in clouds has a pointer to this object.
constexpr bool OUTPUT_ASSEMBLY = false; //problem: if this is turned on, ubsan complains about the print function I think. maybe the flags are bad?
struct function
{
	uAST* the_AST;
	Tptr return_type;
	Tptr parameter_type = 0; //if nonzero, change the mark algorithm
	void* fptr; //the function pointer
	KaleidoscopeJIT::ModuleHandleT result_module;
	std::unique_ptr<llvm::LLVMContext> context;
	//todo: finiteness
	//function() { the_AST = (uAST*)(this - 1); return_type = (Tptr)(this + 1); } //initializing the doubly-linked list.
	function(uAST* a, Tptr r, Tptr p, void* f, KaleidoscopeJIT::ModuleHandleT m, std::unique_ptr<llvm::LLVMContext> c)
		: the_AST(a), return_type(r), parameter_type(p), fptr(f), result_module(m), context(std::move(c))
	{
		if (OUTPUT_ASSEMBLY)
		{
			print("fptr at ", fptr, ": ");
			for (int x = 0; x < 100; ++x)
			{
				//unsigned is necessary
				print(std::hex, std::setfill('0'), std::setw(2), (unsigned)((unsigned char*)f)[x]);
			}
		}
	}
	~function()
	{
		if (!DONT_ADD_MODULE_TO_ORC && !DELETE_MODULE_IMMEDIATELY)
		{
			if (VERBOSE_GC) print("removing module, where this is ", this, "\n");
			c->removeModule(result_module);
		}
	}
};

inline std::ostream& operator<< (std::ostream& o, const function& fred)
{
	return o << "function at " << &fred << " with AST " << fred.the_AST << " return " << fred.return_type << " fptr " << fred.fptr << '\n';
}

function* allocate_function();