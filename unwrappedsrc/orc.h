#pragma once
#include <memory>
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/IR/Mangler.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "console.h"

//taken directly from Lang Hames' Orc Kaleidoscope tutorial

class SessionContext
{
public:
	SessionContext(llvm::LLVMContext &C) : Context(C), TM(llvm::EngineBuilder().selectTarget()) {}
	llvm::LLVMContext& getLLVMContext() const { return Context; }
	llvm::TargetMachine& getTarget() { return *TM; }
private:
	llvm::LLVMContext &Context;
	std::unique_ptr<llvm::TargetMachine> TM;
};

#include <sstream>
inline std::string GenerateUniqueName(const std::string &Root)
{
	thread_local static int i = 0;
	std::ostringstream NameStream;
	NameStream << Root << ++i;
	return NameStream.str();
}


class IRGenContext
{
public:
	IRGenContext(SessionContext &S) : Session(S), M(new llvm::Module(GenerateUniqueName("jit_module_"), Session.getLLVMContext())), Builder(Session.getLLVMContext())
	{ M->setDataLayout(*Session.getTarget().getDataLayout()); }

	SessionContext& getSession() { return Session; }
	llvm::Module& getM() const { return *M; }
	std::unique_ptr<llvm::Module> takeM() { return std::move(M); }
	llvm::IRBuilder<>& getBuilder() { return Builder; }
	llvm::LLVMContext& getLLVMContext() { return Session.getLLVMContext(); }
private:
	SessionContext &Session;
	std::unique_ptr<llvm::Module> M;
	llvm::IRBuilder<> Builder;
};


template <typename T>
inline static std::vector<T> singletonSet(T t)
{
	std::vector<T> Vec;
	Vec.push_back(std::move(t));
	return Vec;
}

class KaleidoscopeJIT
{
public:
	typedef llvm::orc::ObjectLinkingLayer<> ObjLayerT;
	typedef llvm::orc::IRCompileLayer<ObjLayerT> CompileLayerT;
	typedef CompileLayerT::ModuleSetHandleT ModuleHandleT;

	KaleidoscopeJIT(SessionContext &Session) : Mang(Session.getTarget().getDataLayout()), CompileLayer(ObjectLayer, llvm::orc::SimpleCompiler(Session.getTarget())) {}

	std::string mangle(const std::string &Name) {
		std::string MangledName;
		{
			llvm::raw_string_ostream MangledNameStream(MangledName);
			Mang.getNameWithPrefix(MangledNameStream, Name);
		}
		return MangledName;
	}

	ModuleHandleT addModule(std::unique_ptr<llvm::Module> M)
	{
		// We need a memory manager to allocate memory and resolve symbols for this
		// new module. Create one that resolves symbols by looking back into the JIT.
		auto Resolver = llvm::orc::createLambdaResolver([&](const std::string &Name)
		{
			if (auto Sym = findSymbol(Name))
				return llvm::RuntimeDyld::SymbolInfo(Sym.getAddress(), Sym.getFlags());
			error("return llvm::RuntimeDyld::SymbolInfo(nullptr)");
		},
			[](const std::string &S) { return nullptr; });
		return CompileLayer.addModuleSet(singletonSet(std::move(M)), llvm::make_unique<llvm::SectionMemoryManager>(), std::move(Resolver));
	}

	void removeModule(ModuleHandleT H) { CompileLayer.removeModuleSet(H); }

	llvm::orc::JITSymbol findSymbol(const std::string &Name) { return CompileLayer.findSymbol(Name, true); }
	llvm::orc::JITSymbol findUnmangledSymbol(const std::string Name) { return findSymbol(mangle(Name)); }

private:

	llvm::Mangler Mang;
	ObjLayerT ObjectLayer;
	CompileLayerT CompileLayer;
};
