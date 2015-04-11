#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include <vector>
#include <string>
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

int main()
{
	llvm::LLVMContext & context = llvm::getGlobalContext();
	llvm::Module *module = new llvm::Module("asdf", context);
	llvm::IRBuilder<> builder(context);

	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	std::string ErrStr;
	llvm::ExecutionEngine* engine;
	engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(module))
		.setErrorStr(&ErrStr)
		.setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(
		new llvm::SectionMemoryManager))
		.create();
	if (!engine) {
		fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
		exit(1);
	}

	auto int64_type = llvm::Type::getInt64Ty(llvm::getGlobalContext());	
	llvm::FunctionType *funcType = llvm::FunctionType::get(int64_type, false);
	llvm::Function *mainFunc =
		llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main", module);
	llvm::BasicBlock *entry = llvm::BasicBlock::Create(context, "entrypoint", mainFunc);
	builder.SetInsertPoint(entry);

	llvm::Value *helloWorld = builder.CreateGlobalStringPtr("hello world!\n");

	//create the function type
	std::vector<llvm::Type *> putsArgs;
	putsArgs.push_back(builder.getInt8Ty()->getPointerTo());
	llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);
	llvm::FunctionType *putsType =
		llvm::FunctionType::get(builder.getInt32Ty(), argsRef, false);

	llvm::Constant *putsFunc = module->getOrInsertFunction("puts", putsType);

	builder.CreateCall(putsFunc, helloWorld);
	builder.CreateRet(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 40)));
	module->dump();

	engine->finalizeObject();
	void* fptr = engine->getPointerToFunction(mainFunc);
	uint64_t(*FP)() = (uint64_t(*)())(intptr_t)fptr;
	fprintf(stderr, "Evaluated to %lu\n", FP());
}