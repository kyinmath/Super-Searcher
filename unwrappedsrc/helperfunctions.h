#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include "debugoutput.h"
#include "cs11.h"
extern llvm::IntegerType* int64_type;
inline llvm::Constant* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(int64_type, value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm array");
	return llvm::ArrayType::get(int64_type, size);
}

inline llvm::Type* llvm_type(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm type");
	if (size > 1) return llvm_array(size);
	else return int64_type;

}

//return type is not a llvm::Function*, because it's a pointer to a function.
template<typename... should_be_type_ptr, typename fptr> inline llvm::Value* llvm_function(llvm::IRBuilder<>& Builder, fptr* function, llvm::Type* return_type, should_be_type_ptr... argument_types)
{
	std::vector<llvm::Type*> argument_type{argument_types...};
	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, argument_type, false);
	llvm::PointerType* function_pointer_type = function_type->getPointerTo();
	llvm::Constant *function_address = llvm_integer((uint64_t)function);
	return Builder.CreateIntToPtr(function_address, function_pointer_type, s("convert address to function"));
}

//we already typechecked and received 3. then, they're the same size, unless one of them is T::nonexistent
//thus, we check for T::nonexistent
inline llvm::Value* llvm_create_phi(llvm::IRBuilder<>& Builder, llvm::Value* first, llvm::Value* second, Type* first_t, Type* second_t, llvm::BasicBlock* firstBB, llvm::BasicBlock* secondBB)
{
	uint64_t size1 = get_size(first_t);
	uint64_t size2 = get_size(second_t);

	//these are in case one of them is T::nonexistent
	if (size2 == 0) return first;
	if (size1 == 0) return second;

	llvm::PHINode* PN = Builder.CreatePHI(size1 == 1 ? (llvm::Type*)int64_type : llvm_array(size1), 2); //phi with two incoming variables
	PN->addIncoming(first, firstBB);
	PN->addIncoming(second, secondBB);
	return PN;
}

//return true on success
inline bool compile_and_run(AST* ast)
{
	finiteness = FINITENESS_LIMIT;
	compiler_object j;
	unsigned error_code = j.compile_AST(ast);
	if (error_code)
	{
		console << "Malformed AST: code " << error_code << " at AST " << j.error_location << " " << AST_descriptor[j.error_location->tag].name << " field " << j.error_field << "\n\n";
		return 0;
	}
	else console << "Successful compile\n\n";
	return true;
}