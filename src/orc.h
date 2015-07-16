#pragma once
#include <memory>
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/NullResolver.h"
#include "llvm/IR/Mangler.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "globalinfo.h"

//taken directly from Lang Hames' Orc Kaleidoscope tutorial

#include <sstream>
inline std::string GenerateUniqueName(const std::string &Root)
{
	static uint64_t i = 0;
	std::ostringstream NameStream;
	NameStream << Root << ++i;
	//console << "name is " << NameStream.str() << '\n';
	return NameStream.str();
}




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


	KaleidoscopeJIT(llvm::TargetMachine* tm) : DL(*tm->getDataLayout()), CompileLayer(ObjectLayer, llvm::orc::SimpleCompiler(*tm)) {}

	std::string mangle(const std::string &Name) {
		std::string MangledName;
		{
			llvm::raw_string_ostream MangledNameStream(MangledName);
			llvm::Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
		}
		return MangledName;
	}

	ModuleHandleT addModule(std::unique_ptr<llvm::Module> M)
	{
		//later, if we want optimization, we'll need to change this back
		std::unique_ptr<llvm::orc::NullResolver> Resolver((new llvm::orc::NullResolver()));
		return CompileLayer.addModuleSet(singletonSet(std::move(M)), llvm::make_unique<llvm::SectionMemoryManager>(), std::move(Resolver));
	}

	void removeModule(ModuleHandleT H) { CompileLayer.removeModuleSet(H); }

	llvm::orc::JITSymbol findSymbol(const std::string &Name) { return CompileLayer.findSymbol(Name, true); }
	llvm::orc::JITSymbol findUnmangledSymbol(const std::string Name) { return findSymbol(mangle(Name)); }

private:

	const llvm::DataLayout &DL;
	ObjLayerT ObjectLayer;
	CompileLayerT CompileLayer;
};
