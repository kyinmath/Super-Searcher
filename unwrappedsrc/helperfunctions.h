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

inline void write_into_place(value_collection data, llvm::Value* target)
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
			//llvm::Value* location = builder->CreateConstInBoundsGEP1_64(this->get_location(), offset);
			builder->CreateStore(integer_transfer, target);
			++offset;
		}
	}
}


llvm::AllocaInst* create_empty_alloca();

//how to use a memory allocation:
//change its size continually as you add elements to it.
//at the end, use cast_base() to make it the correct size.
struct memory_allocation
{
	llvm::Instruction* allocation = create_empty_alloca(); //AllocaInst* a = alloca i64, size, or an allocate() address
	uint64_t size = 0;

	bool self_is_full = 0; //use turn_full(). don't manipulate this directly.
	uint64_t references_to_me = 0; //how many pointers exist to this memory location. if the memory location tries to fall off the stack while references still exist, it is made into a full pointer.
	uint64_t version = 1; //used to version the allocation. when the allocation changes, the cached offsets of memory_location need to change as well.

	std::vector<memory_allocation*> references; //outward references. this memory allocation contains pointers that points to these things.
	bool full_reference_possible;

	void cast_base() //if this should be a heap alloca, mark self_is_full before calling this.
	{
		llvm::Instruction* new_alloca;
		if (!self_is_full)
			new_alloca = new llvm::AllocaInst(int64_type(), llvm_integer(size));
		else
		{
			llvm::Value* allocator = llvm_function(allocate, int64_type()->getPointerTo(), int64_type());
			new_alloca = llvm::CallInst::Create(allocator, std::vector<llvm::Value*>{llvm_integer(size)});
		}

		llvm::BasicBlock::iterator ii(allocation);
		ReplaceInstWithInst(allocation->getParent()->getInstList(), ii, new_alloca);

		++version;
		allocation = new_alloca;
	}
};


//val = i64 if size = 1, val = [i64 x size] if size > 1.
struct memory_location //used for GEP. good for delaying emission of instructions unless they're necessary.
{
	memory_allocation* base;
	uint64_t offset;
	llvm::Value* spot = nullptr;
	uint64_t version = 0; //this version number starts below memory_allocation's version. whenever this version is less, spot must be re-calculated, because the cached instruction is invalidated. it's bad that we're re-emitting the GEP, but I don't know how to find the new version of the ReplaceInstwithInst'd GEPs.

	memory_location(memory_allocation* f1, uint64_t o) : base(f1), offset(o) {}
	memory_location() : base(nullptr), offset(0) {}


	llvm::Value* get_location()
	{
		determine_spot();
		return spot;
	}
	//assigns spot to be the correct pointer. i64*
	void determine_spot()
	{
		if (version < base->version)
		{
			spot = base->allocation;
			if (offset) spot = builder->CreateConstInBoundsGEP1_64(base->allocation, offset);
		}
	}
	void store(value_collection val)
	{
		determine_spot();
		write_into_place(val, get_location());
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
		llvm::Value* new_location = builder->CreateConstInBoundsGEP1_64(location, a);
		integer_transfer = builder->CreateLoad(new_location);
		undef_value = builder->CreateInsertValue(undef_value, integer_transfer, std::vector < unsigned > { (unsigned)a });
	};
	return undef_value;
}


inline void add_reference(std::vector<memory_allocation*>& references, bool& full_reference_possible, memory_allocation* base)
{
	if (base->self_is_full)
	{
		full_reference_possible = true;
		return;
	}
	++(base->references_to_me);
	references.push_back(base);
}

//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
	IRgen_status error_code;

	//either IR or place is active, not both.
	//if memory_location_active == false, then IR is active. otherwise, place is active.
	//place is active when (stack_degree >= 1) <=> (memory_location_active = true).
	//either way, the internal type doesn't change.
	llvm::Value* IR;
	memory_location place;

	Type* type;
	bool memory_location_active;

	//this should eventually be changed to an unordered_set, to prevent exponential doubling of the vector size.
	std::vector<memory_allocation*> Value_references; //if this points to any memory locations that might be on the stack, then these are the uASTs.
	//those memory locations may nevertheless be in the heap anyway. however, we guarantee that any memory location that is on the stack must be here.
	//if escape analysis tells you the memory location needs to become full, then go over there and change things.
	//this only exists if memory_location_active = 0. because references are bound to the actual object.
	//so these references are by the IR if it's a temp object, and they're by the memory_allocation if it's not.

	bool Value_full_reference_possible; //you might be pointing to a full object. used for store(), where we must update references. if this is true, then instead of updating references, we force the stored pointer's references to become full immediately.
	//prefer: if a reference can be found, place it in the vector, which accepts all references. the bool is a last-ditch effort to catch unknowable things, and results in slowdowns. it's for when there isn't a meaningful reference to be found.
	//this also only exists if memory_location_active = 0

	//either b or m must be null. both of them can't be active at the same time.
	Return_Info(IRgen_status err, llvm::Value* b, memory_location m, Type* t, bool o, std::vector<memory_allocation*> bases, bool full)
		: error_code(err), IR(b), place(m), type(t), memory_location_active(o), Value_full_reference_possible(full)
	{
		if (!memory_location_active)
		{
			for (auto*& memory : bases)
				add_reference(Value_references, Value_full_reference_possible, memory);
		}
		else for (auto*& memory : bases)
			add_reference(place.base->references, place.base->full_reference_possible, memory);
	}


	//default constructor for a null object
	//make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references allowed.
	Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), place(), type(T::null), memory_location_active(false) {}


	~Return_Info()
	{
		if (!memory_location_active)
		{
			for (auto*& memory : Value_references)
				--(memory->references_to_me);
		}
		else for (auto*& memory : place.base->references)
			--(memory->references_to_me);
	}
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