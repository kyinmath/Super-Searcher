#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
extern llvm::IntegerType* int64_type;
inline llvm::Constant* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(int64_type, value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	return llvm::ArrayType::get(int64_type, size);
}

