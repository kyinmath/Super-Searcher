#include <iostream>
#include <thread>
#include <vector>
#include "constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#ifdef _MSC_VER
__declspec( thread )
#else
thread_local
#endif
llvm::LLVMContext thread_context;

void thread_start(void(*AST_starting_pointer)())
{

}

//This function will be called from a thread
void call_from_thread() {
	std::cout << "I am thread " << std::this_thread::get_id() << std::endl;
}

int main() {
	//bitpattern of nullptr must be 0
	//static_assert(reinterpret_cast<uintptr_t>(nullptr) == 0, "We rely on nullptr being 0");
	assert(reinterpret_cast<uintptr_t>(nullptr) == 0, "We rely on nullptr being 0");
	static_assert(sizeof(void*) == 8, "Pointer must be 64 bits");

	llvm::Module* empty_module = new llvm::Module("", thread_context);
	the_engine = llvm::EngineBuilder(empty_module)
		.setUseMCJIT(true)
		.create();

	//ExecutionEngine has a lock. "It must be held while changing the internal state of any of those classes."
	//use MCJIT instead of ExecutionEngine. the lock still exists, though.MCJIT is just a derived class.

	int number_of_threads = std::thread::hardware_concurrency();
	assert(number_of_threads > 0, "hardware_concurrency() returned 0");
	
	//std::clog << number_of_threads << " threads\n";
	//this should be run once per system, and then verified. error when it's incorrect.
	system_threads.resize(number_of_threads);
	for (unsigned i = 0; i < number_of_threads; ++i)
	{
		system_threads[i].thread_is_running = 1;
		system_threads[i].handle = std::thread(call_from_thread);
		system_threads[i].handle.detach();
	}

	//main thread is the admin thread. the admin thread takes care of kings and countries, and forces death.


	return 0;
}