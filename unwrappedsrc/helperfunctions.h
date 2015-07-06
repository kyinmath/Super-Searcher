#pragma once
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include "globalinfo.h"
#include "types.h"

enum IRgen_status {
	no_error,
	infinite_loop, //ASTs have pointers in a loop
	active_object_duplication, //an AST has two return values active simultaneously, making the AST-to-object map ambiguous
	type_mismatch, //in an if statement, the two branches had different types.
	null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
	pointer_without_target, //tried to get a pointer to an AST, but it was not found at all
	pointer_to_temporary, //tried to get a pointer to an AST, but it was not placed on the stack.
	missing_label, //tried to goto a label, but the label was not found
	label_incorrect_stack, //tried to goto a label, but the stack elements didn't match
	label_duplication, //a label was pointed to in the tree twice
	store_pointer_lifetime_mismatch, //tried to store a short-lasting object in a long-lasting memory location
};

enum class stack_state
{
	temp,
	cheap_alloca,
	might_be_visible, //to other threads. better use atomic operations
	full_might_be_visible, //when you load, you get a full_pointer
	full_isolated, //it's a full pointer. but no need for atomic operations
};

inline bool requires_atomic(stack_state x)
{
	if (x == stack_state::full_might_be_visible || x == stack_state::might_be_visible)
		return true;
	else return false;
}

inline bool is_full(stack_state x)
{
	if (x == stack_state::full_might_be_visible || x == stack_state::full_isolated)
		return true;
	else return false;
}

constexpr uint64_t full_lifetime = -1ll;


//solely for convenience
inline llvm::IntegerType* int64_type() { return llvm::Type::getInt64Ty(*context); }
inline llvm::Type* void_type() { return llvm::Type::getVoidTy(*context); }

inline llvm::Constant* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(int64_type(), value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm array");
	return llvm::ArrayType::get(int64_type(), size);
}

inline llvm::Type*  llvm_type(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm type");
	if (size > 1) return llvm_array(size);
	else return int64_type();
}

inline llvm::Type* llvm_type_including_void(uint64_t size)
{
	if (size == 0) return void_type();
	if (size > 1) return llvm_array(size);
	else return int64_type();
}


//return type is not a llvm::Function*, because it's a pointer to a function.
template<typename... should_be_type_ptr, typename fptr> inline llvm::Value* llvm_function(fptr* function, llvm::Type* return_type, should_be_type_ptr... argument_types)
{
	std::vector<llvm::Type*> argument_type{argument_types...};
	llvm::FunctionType* function_type = llvm::FunctionType::get(return_type, argument_type, false);
	llvm::PointerType* function_pointer_type = function_type->getPointerTo();
	llvm::Constant *function_address = llvm_integer((uint64_t)function);
	return builder->CreateIntToPtr(function_address, function_pointer_type, s("convert address to function"));
}

//we already typechecked and received 3. then, they're the same size, unless one of them is T::does_not_return
//thus, we check for T::nonexistent
inline llvm::Value* llvm_create_phi(llvm::Value* first, llvm::Value* second, Type* first_t, Type* second_t, llvm::BasicBlock* firstBB, llvm::BasicBlock* secondBB)
{
	uint64_t size1 = get_size(first_t);
	uint64_t size2 = get_size(second_t);

	//these are in case one of them is T::does_not_return
	if (size2 == 0) return first;
	if (size1 == 0) return second;

	llvm::PHINode* PN = builder->CreatePHI(size1 == 1 ? (llvm::Type*)int64_type() : llvm_array(size1), 2); //phi with two incoming variables
	PN->addIncoming(first, firstBB);
	PN->addIncoming(second, secondBB);
	return PN;
}


struct value_collection
{
	//each llvm::Value* must be either an aggregate {i64, i64}, or an array [4 x i64]. but either is fine, because extractvalue works on both.
	//however, each term inside must have size 1.
	std::vector<std::pair<llvm::Value*, uint64_t>> objects; //value, then size.
	value_collection(llvm::Value* integer, uint64_t size) : objects{std::make_pair(integer, size)} {}
};



inline void write_into_place(value_collection data, llvm::Value* start)
{
	uint64_t offset = 0;
	for (auto& single_object : data.objects)
	{
		for (uint64_t subplace = 0; subplace < single_object.second; ++subplace)
		{
			check(single_object.second != 0, "tried to store 0 size object");

			llvm::Value* integer_transfer;
			if (single_object.second > 1)
				integer_transfer = builder->CreateExtractValue(single_object.first, subplace);
			else integer_transfer = single_object.first;
			llvm::Value* location = builder->CreateConstInBoundsGEP1_64(start, offset);
			builder->CreateStore(integer_transfer, location);
			++offset;
		}
	}
}


//val = i64 if size = 1, val = [i64 x size] if size > 1.
//AllocaInst* a = alloca i64, size for size >= 1.
struct memory_location //used for GEP. good for delaying emission of instructions unless they're necessary.
{
	llvm::AllocaInst* base;
	uint64_t offset;
	llvm::Value* spot;
	memory_location(llvm::AllocaInst* f1, uint64_t o) : base(f1), offset(o), spot(nullptr) {}
	memory_location() : base(nullptr), offset(0), spot(nullptr) {}


	llvm::Value* get_location()
	{
		determine_spot();
		return spot;
	}
	void cast_base(uint64_t size)
	{
		llvm::AllocaInst* new_alloca = new llvm::AllocaInst(int64_type(), llvm_integer(size));
		llvm::BasicBlock::iterator ii(base);
		ReplaceInstWithInst(base->getParent()->getInstList(), ii, new_alloca);

		base = new_alloca;
		spot = base;
		check(offset == 0, "this isn't expected, I'm relying on this to cache base in spot which I probably shouldn't be doing");
	}
	//assigns spot to be the correct pointer. i64*
	void determine_spot()
	{
		if (spot == nullptr)
		{
			spot = base;
			if (offset) spot = builder->CreateConstInBoundsGEP1_64(base, offset);
		}
	}
	void store(value_collection val)
	{
		determine_spot();
		write_into_place(val, spot);
	}
};

inline llvm::Value* load_from_stack(llvm::Value* location, uint64_t size)
{
	check(size < -1u, "load object not equipped to deal with large objects, because CreateInsertValue has a small index");
	if (size == 1) return builder->CreateLoad(location);
	llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(size));
	for (uint64_t a = 0; a < size; ++a)
	{
		llvm::Value* integer_transfer;
		llvm::Value* new_location = builder->CreateConstInBoundsGEP1_64(location, a);
		integer_transfer = builder->CreateLoad(new_location);
		undef_value = builder->CreateInsertValue(undef_value, integer_transfer, std::vector < unsigned > { (unsigned)a });
	};
	return undef_value;
}


//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
	IRgen_status error_code;

	//either IR or place is active, not both.
	//if on_stack == temp, then IR is active. otherwise, place is active.
	//place is only ever active if you passed in stack_degree >= 1, because that's when on_stack != temp.
	//either way, the internal type doesn't change.
	llvm::Value* IR;
	memory_location place;

	Type* type;
	stack_state on_stack;

	//to write an object into a pointed-to memory location, we must have guarantee of object <= hit contract of memory location
	//there's only one hit contract value per object, which can be a problem for concatenations of many cheap pointers.
	//however, we need concatenation only for heap objects, parameters, function return; in these cases, the memory order doesn't matter
	uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a deletion time, not a creation time.
	//it's important that it is a deletion time, because deletion and creation are not in perfect stack configuration.
	//because temporaries are created before the base object, and die just after.

	//low number = longer life.
	uint64_t self_validity_guarantee; //higher is less lenient (validity is shorter). the object's values must be valid for at least this time.
	//for when an object wants to be written into a memory location

	uint64_t target_hit_contract; //lower is less lenient. the reference will disappear after this time. used for pointers
	//measures the pointer's target's lifetime, for when the pointed-to memory location wants something to be written into it.

	Return_Info(IRgen_status err, llvm::Value* b, Type* t, stack_state o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(b), type(t), on_stack(o), self_lifetime(s), self_validity_guarantee(u), target_hit_contract(l) {}
	Return_Info(IRgen_status err, memory_location b, Type* t, stack_state o, uint64_t s, uint64_t u, uint64_t l)
		: error_code(err), IR(0), place(b), type(t), on_stack(o), self_lifetime(s), self_validity_guarantee(u), target_hit_contract(l) {}
	//IR should be initialized to 0 in this ctor to avoid uninitialized-value errors from valgrind. however, it is otherwise unnecessary as the uninitialized value is never used except when trying to output it for debug purposes.

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), on_stack(stack_state::temp), self_lifetime(0), self_validity_guarantee(0), target_hit_contract(full_lifetime) {}
};



/** mem-to-reg only works on entry block variables.
thus, this function builds llvm::Allocas in the entry block. it should be preferred over trying to create allocas directly.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
we call this "create_alloca" instead of "create_alloca_in_entry_block", because it's the general alloca mechanism. if we said, "in_entry_block", then the user would be confused as to when to use this. by not having that, it's clear that this should be the default.

we create an alloca. it's a placeholder for dependencies, in case we need a place to store things.
if we don't need it, we can use eraseFromParent()
if we do need it, then we use ReplaceInstWithInst.

we create a many-element allocation instead of an array allocation, because we need to use ReplaceInstWithInst, which must preserve type.
	otherwise, I see assert(): "replaceAllUses of value with new value of different type!"
*/
inline llvm::AllocaInst* create_empty_alloca() {
	llvm::BasicBlock& first_block = builder->GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());
	return TmpB.CreateAlloca(int64_type(), llvm_integer(0));
}

inline llvm::AllocaInst* create_actual_alloca(uint64_t size) {
	llvm::BasicBlock& first_block = builder->GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());
	return TmpB.CreateAlloca(int64_type(), llvm_integer(size));
}

struct builder_context_stack
{
	llvm::IRBuilder<>* old_builder = builder;
	llvm::LLVMContext* old_context = context;
	builder_context_stack(llvm::IRBuilder<>* b, llvm::LLVMContext* c) {
		builder = b;
		context = c;
	}
	~builder_context_stack() {
		builder = old_builder;
		context = old_context;
	}
};