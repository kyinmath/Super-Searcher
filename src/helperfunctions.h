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
	missing_reference, //tried to store, but the target didn't have a reference available.
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
inline llvm::StructType* double_int() { return llvm::StructType::get(*context, {llvm_i64(), llvm_i64()}); }

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
inline llvm::Value* llvm_create_phi(llvm::ArrayRef<llvm::Value*> values, llvm::ArrayRef<Tptr> types, llvm::ArrayRef<llvm::BasicBlock*> basic_blocks)
{
	check(values.size() == types.size(), "wrong number of arguments");
	check(values.size() == basic_blocks.size(), "wrong number of arguments");
	check(values.size() >= 2, "why even bother making a phi");
	uint64_t choices = values.size();
	if (types[0] == 0) return nullptr;
	uint64_t eventual_size;
	std::set<uint64_t> legitimate_values; //ones that aren't T::does_not_return
	for (uint64_t idx = 0; idx < choices; ++idx)
	{
		if (types[idx].ver() != Typen("does not return"))
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
	std::vector<llvm::Value*> objects; //value, then size.
	value_collection(llvm::Value* integer) : objects{integer} {}
	value_collection(std::vector<llvm::Value*> a) : objects{a} {}
};

//the ssa bool is in case the target is an array/integer. in that case, "target" is overwritten.
inline void write_into_place(value_collection data, llvm::Value*& target, bool ssa = false)
{
	uint64_t offset = 0;
	for (auto& single_object : data.objects)
	{
		uint64_t size;
		if (single_object == nullptr) continue;
		llvm::Type* type_of_single = single_object->getType();
		if (llvm::isa<llvm::IntegerType>(type_of_single))
			size = 1;
		else if (auto array = llvm::dyn_cast<llvm::ArrayType>(type_of_single))
			size = array->getNumElements();
		else if (auto aggregate = llvm::dyn_cast<llvm::StructType>(type_of_single))
			size = aggregate->getNumElements();
		else error("what fucking size is the Value anyway");
		for (uint64_t subplace = 0; subplace < size; ++subplace)
		{
			llvm::Value* integer_transfer = (size > 1) ? builder->CreateExtractValue(single_object, subplace) : single_object;

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

	memory_allocation(uint64_t s) : allocation(create_actual_alloca(s)) {}
	memory_allocation(llvm::Value* existing_location) : allocation(existing_location) {} //used for hidden references.
	memory_allocation() : allocation(0) {}
private:
	bool self_is_full = false; //use turn_full(). don't manipulate this directly.
public:
	
	void turn_full()
	{
		if (self_is_full == false)
		{
			self_is_full = true;

			uint64_t size;
			if (auto k = llvm::dyn_cast<llvm::AllocaInst>(allocation))
			{
				if (auto const_size = llvm::dyn_cast<llvm::ConstantInt>(k->getArraySize()))
				{
					size = const_size->getZExtValue();
					goto successful_size_get;
				}
			}
			error("couldn't get size properly");
			successful_size_get: //the if conditions are inverted because we need to see the if condition.

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
//IRi is active <=> memory_location = 0.
//if memory_location is active, hidden_reference may still be true or false. this affects whether you can get a pointer to it.
//hidden_reference => no pointers allowed. hidden_reference => memory_location is active. 
struct Return_Info
{
	IRgen_status error_code;

	//either IR or place is active, not both.
	//if memory_location_primary == false, then IR is active. otherwise, place is active.
	//place is active when memory_location_primary = true.
	//either way, the internal type doesn't change.
	llvm::Value* IR;
	memory_allocation* place; //public, because [pointer] wants to turn it full.

	Tptr type;
	bool memory_location_primary; //if this is false, the IR is the primary. if this is true, the memory_location is the primary.
	//if this is false, the allocation isn't something we own. that is, geting a pointer to it is disallowed.
	bool hidden_reference; //this is used for store(). set by load() load_subobj(), dyn_subobj(), stack_degree == 2. it's true if you can get a reference.

	Return_Info(IRgen_status err, llvm::Value* b, Tptr t, bool h = false)
		: error_code(err), IR(b), place(0), type(t), memory_location_primary(place), hidden_reference(h) {}
	Return_Info(IRgen_status err, memory_allocation* m, Tptr t, bool h = false)
		: error_code(err), IR(0), place(m), type(t), memory_location_primary(place), hidden_reference(h)
	{ IR = load_from_memory(place->allocation, get_size(type)); }

	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), memory_location_primary(false), hidden_reference(false) {}
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


inline uint64_t* copy_object(Tptr type, uint64_t* pointer)
{
	uint64_t total_size = get_size(type);
	uint64_t* new_memory = allocate(total_size);
	for (uint64_t x = 0; x < total_size; ++x)
		new_memory[x] = pointer[x];
	return new_memory;
}


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
			if (target->tag == ASTn("imv"))
				copy_object(target->fields[0].num, (uint64_t*)target->fields[1].ptr);
			else for (uint64_t x = 0; x < AST_descriptor[target->tag].pointer_fields; ++x)
				target->fields[x].ptr = internal_copy(target->fields[x].ptr);
		}
		else target = search_for_copy->second;
		return target;
	}
};