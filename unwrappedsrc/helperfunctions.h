#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include "debugoutput.h"
extern llvm::IntegerType* int64_type;
inline llvm::Constant* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(int64_type, value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	return llvm::ArrayType::get(int64_type, size);
}

template<typename... should_be_type_ptr, typename fptr> inline llvm::Function* llvm_function(llvm::IRBuilder<>& Builder, fptr* function, llvm::Type* return_type, should_be_type_ptr... argument_types)
{
	std::vector<llvm::Type*> argument_type{argument_types...};
	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, argument_type, false);
	llvm::PointerType* function_pointer_type = function_type->getPointerTo();
	llvm::Constant *function_address = llvm_integer((uint64_t)function);
	return (llvm::Function*)Builder.CreateIntToPtr(function_address, function_pointer_type, s("convert address to function"));
}