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
	oversized_offset, //when offsetting a pointer, you gave an excessively large offset
	requires_constant //when offsetting a pointer, you gave something that wasn't a constant
};

//solely for convenience
inline llvm::IntegerType* llvm_i64() { return llvm::Type::getInt64Ty(*context); }
inline llvm::Type* llvm_void() { return llvm::Type::getVoidTy(*context); }

inline llvm::ConstantInt* llvm_integer(uint64_t value)
{
	return llvm::ConstantInt::get(llvm_i64(), value);
}

inline llvm::ArrayType* llvm_array(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm array");
	return llvm::ArrayType::get(llvm_i64(), size);
}

inline llvm::Type* llvm_type(uint64_t size)
{
	check(size != 0, "tried to get 0 size llvm type");
	if (size > 1) return llvm_array(size);
	else return llvm_i64();
}

inline llvm::Type* llvm_type_including_void(uint64_t size)
{
	if (size == 0) return llvm_void();
	if (size > 1) return llvm_array(size);
	else return llvm_i64();
}


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
	return TmpB.CreateAlloca(llvm_i64(), llvm_integer(0));
}

inline llvm::AllocaInst* create_actual_alloca(uint64_t size) {
	llvm::BasicBlock& first_block = builder->GetInsertBlock()->getParent()->getEntryBlock();
	llvm::IRBuilder<> TmpB(&first_block, first_block.begin());
	return TmpB.CreateAlloca(llvm_i64(), llvm_integer(size));
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
inline llvm::Value* llvm_create_phi(llvm::ArrayRef<llvm::Value*> values, llvm::ArrayRef<Type*> types, llvm::ArrayRef<llvm::BasicBlock*> basic_blocks)
{
	check(values.size() == types.size(), "wrong number of arguments");
	check(values.size() == basic_blocks.size(), "wrong number of arguments");
	check(values.size() >= 2, "why even bother making a phi");
	uint64_t choices = values.size();
	if (types[0] == nullptr) return nullptr;
	uint64_t eventual_size;
	std::set<uint64_t> legitimate_values; //ones that aren't T::does_not_return
	for (uint64_t idx = 0; idx < choices; ++idx)
	{
		if (types[idx]->ver() != Typen("does not return"))
		{
			legitimate_values.insert(idx);
			eventual_size = get_size(types[idx]);
		}
	}
	if (legitimate_values.size() == 0) return nullptr;
	if (legitimate_values.size() == 1) return values[*legitimate_values.begin()];

	llvm::PHINode* PN = builder->CreatePHI(eventual_size == 1 ? (llvm::Type*)llvm_i64() : llvm_array(eventual_size), legitimate_values.size()); //phi with two incoming variables
	for (uint64_t idx : legitimate_values)
		PN->addIncoming(values[idx], basic_blocks[idx]);
	return PN;
}


struct value_collection
{
	//each llvm::Value* must be either an aggregate {i64, i64}, or an array [4 x i64]. but either is fine, because extractvalue works on both.
	//however, each term inside must have size 1.
	std::vector<std::pair<llvm::Value*, uint64_t>> objects; //value, then size.
	value_collection(llvm::Value* integer, uint64_t size) : objects{std::make_pair(integer, size)} {}
	value_collection(std::vector<std::pair<llvm::Value*, uint64_t>> a) : objects{a} {}
};

//the ssa bool is in case the target is an array/integer. in that case, "target" is overwritten.
inline void write_into_place(value_collection data, llvm::Value*& target, bool ssa = false)
{
	uint64_t offset = 0;
	for (auto& single_object : data.objects)
	{
		for (uint64_t subplace = 0; subplace < single_object.second; ++subplace)
		{
			check(single_object.second != 0, "tried to store 0 size object");

			llvm::Value* integer_transfer = (single_object.second > 1) ? builder->CreateExtractValue(single_object.first, subplace) : single_object.first;

			//transfer the values
			if (ssa)
			{
				if (target->getType()->isIntegerTy())
					target = integer_transfer;
				else
				{
					check(offset < ~0u, "large types not allowed for CreateInsertValue");
					target = builder->CreateInsertValue(target, integer_transfer, {(unsigned)offset});
				}
			}
			else
			{
				llvm::Value* location = offset ? builder->CreateConstInBoundsGEP1_64(target, offset, s("write")) : target;
				builder->CreateStore(integer_transfer, location);
			}
			++offset;
		}
	}
}


llvm::AllocaInst* create_empty_alloca();

//how to use a memory allocation:
//change its size continually as you add elements to it.
//to use this, put one in the deque, so that it can be pointed to for the entire duration of the function. the reason we do this is because the memory_allocation represents an object, which may be lifted to become a heap object. later, if we want to do optimization, we should have a representation for the object.
//create an allocation_handler, whose destructor will call cast_base() if appropriate, and call eraseFromParent() if appropriate.
//use turn_full if we ever get a reference to it.
struct memory_allocation
{
	llvm::Value* allocation; //AllocaInst* a = alloca i64, size, or an allocate() address
	uint64_t size;

	memory_allocation(uint64_t s) : allocation(create_actual_alloca(s)), size(s) {}
private:
	bool self_is_full = false; //use turn_full(). don't manipulate this directly.
public:
	
	void turn_full()
	{
		if (self_is_full == false)
		{
			self_is_full = true;

			llvm::Instruction* new_alloca;
			llvm::Value* allocator = llvm_function(allocate, llvm_i64()->getPointerTo(), llvm_i64());
			new_alloca = llvm::CallInst::Create(allocator, {llvm_integer(size)});

			if (auto allocainstruct = llvm::dyn_cast<llvm::Instruction>(allocation))
			{
				llvm::BasicBlock::iterator ii(allocainstruct);
				ReplaceInstWithInst(allocainstruct->getParent()->getInstList(), ii, new_alloca);
				allocation = new_alloca;
			}
			else error("allocation isn't an instruction");
		}
	}
};


inline llvm::Value* load_from_memory(llvm::Value* location, uint64_t size)
{
	check(size < ~0u, "load object not equipped to deal with large objects, because CreateInsertValue has a small index");
	if (size == 1) return builder->CreateLoad(location);
	llvm::Value* undef_value = llvm::UndefValue::get(llvm_array(size));
	for (uint64_t a = 0; a < size; ++a)
	{
		llvm::Value* integer_transfer;
		llvm::Value* new_location = a ? builder->CreateConstInBoundsGEP1_64(location, a, s("load")) : location;
		integer_transfer = builder->CreateLoad(new_location);
		undef_value = builder->CreateInsertValue(undef_value, integer_transfer, {(unsigned)a});
	};
	return undef_value;
}

//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
	IRgen_status error_code;

	//either IR or place is active, not both.
	//if memory_location_active == false, then IR is active. otherwise, place is active.
	//place is active when memory_location_active = true.
	//either way, the internal type doesn't change.
	llvm::Value* IR;
	memory_allocation* place;

	Type* type;
	bool memory_location_active;
	//either b or m must be null. both of them can't be active at the same time.
	Return_Info(IRgen_status err, llvm::Value* b, memory_allocation* m, Type* t)
		: error_code(err), IR(b), place(m), type(t), memory_location_active((b == 0) ? true : false)
	{
		check((b == 0) || (m == 0), "at least one must be 0"); //both can be 0, if it's an error code.
	}

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), memory_location_active(false) {}
};


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



//makes a deep copy of ASTs.
struct deep_AST_copier
{
	uAST* result;
	deep_AST_copier(uAST* starter) { result = internal_copy(starter); }
private:
	//maps user ASTs to copied ASTs. it handles nullptr, just to make the code a little shorter
	std::unordered_map<uAST*, uAST*> copy_mapper{{nullptr, nullptr}};
	uAST* internal_copy(uAST* user_target)
	{
		auto search_for_copy = copy_mapper.find(user_target);
		uAST* target;
		if (search_for_copy == copy_mapper.end())
		{
			target = copy_AST(user_target); //make a bit copy. the fields will still point to the old ASTs; that will be corrected after inserting into the map
			//this relies on loads in x64 being atomic, which may not be wholly true.
			copy_mapper.insert({user_target, target});

			target->preceding_BB_element = internal_copy(target->preceding_BB_element);
			for (uint64_t x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x)
				target->fields[x].ptr = internal_copy(target->fields[x].ptr);
		}
		else target = search_for_copy->second;
		return target;
	}
};