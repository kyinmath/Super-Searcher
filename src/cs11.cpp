/* see README.md for how to run this program.
Functions which create ASTs:
main() handles the commandline arguments. if console input is requested, it will handle input too. 
however, it won't parse the input.
fuzztester() generates random, possibly malformed ASTs.
for console input, read_single_AST() parses a single AST and its field's ASTs, then creates a tree 
of ASTs out of it.
  then, create_single_basic_block() parses a list of ASTs, then chains them in a linked list using 
  AST.preceding_BB_element

Things which compile and run ASTs:
compiler_object holds state for a compilation. you make a new compiler_state for each compilation, 
and call compile_AST() on the last AST in your function. compile_AST automatically wraps the AST in 
a function and runs it; the last AST is assumed to hold the return object.
compile_AST() is mainly a wrapper. it generates the llvm::Functions and llvm::Modules that contain 
the main code. it calls generate_IR() to do the main work.
generate_IR() is the main AST conversion tool. it turns ASTs into llvm::Values, recursively. these 
llvm::Values are automatically inserted into a llvm::Module.
  to do this, generate_IR first runs itself on a target's dependencies. then, it looks at the 
  target's tag, and then uses a switch-case to determine how it should use those dependencies.

Debug:
output_AST_and_previous() shows an AST and everything it depends on.
output_Type_and_previous() is similar.
Module->print(*llvm_outstream, nullptr) prints the generated llvm code.
*/
#include <cstdint>
#include <iostream>
//#include <mutex>
#include <array>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Support/raw_ostream.h> 
#include "types.h"
#include "debugoutput.h"

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = false;
bool CONSOLE = false;

basic_onullstream<char> null_stream;
std::ostream& outstream = std::cerr;
llvm::raw_null_ostream llvm_null_stream;
llvm::raw_ostream* llvm_outstream = &llvm::outs();

//todo: threading. create non-global contexts
#ifndef _MSC_VER
thread_local
#endif
llvm::LLVMContext& thread_context = llvm::getGlobalContext();


#include <chrono>
#include <random>
unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
std::mt19937_64 mersenne(seed);
uint64_t generate_random() { return mersenne(); }

//generates an approximately exponential distribution using bit twiddling
uint64_t generate_exponential_dist()
{
  uint64_t cutoff = generate_random();
  cutoff ^= cutoff - 1; //1s starting from the lowest digit of cutoff
  return generate_random() & cutoff;
}

enum IRgen_status {
  no_error,
  infinite_loop, //ASTs have pointers in a loop
  active_object_duplication, //an AST has two return values active simultaneously, making the 
  //AST-to-object map ambiguous
  type_mismatch, //in an if statement, the two branches had different types.
  null_AST, //tried to generate_IR() a nullptr but was caught, which is normal.
  pointer_without_target, //tried to get a pointer to an AST, but it was not found at all
  pointer_to_temporary, //tried to get a pointer to an AST, but it was not placed on the stack.
};

//every time IR is generated, this holds the relevant return info.
struct Return_Info
{
  IRgen_status error_code;

  llvm::Value* IR;
    
  Type* type;
  bool on_stack; //if the return value refers to an alloca.
  //in that case, the llvm type is actually a pointer. but the internal type doesn't change.

  //this lifetime information is only used when a cheap pointer is in the type. see pointers.txt
  //to write a pointer into a pointed-to memory location, we must have upper of pointer < lower of 
  //memory location
  //there's only one pair of target lifetime values per object, which can be a problem for objects 
  //which concatenate many cheap pointers.
  //however, we need concatenation only for heap objects, parameters, function return; in these 
  //cases, the memory order doesn't matter
  uint64_t self_lifetime; //for stack objects, determines when you will fall off the stack. it's a 
  //deletion time, not a creation time.
  //it's important that it is a deletion time, because deletion and creation are not in perfect 
  //stack configuration.
  //because temporaries are created before the base object, and die just after.

  uint64_t target_upper_lifetime; //higher is stricter. the target must last longer than this.
  //measures the pointer's target's lifetime, for when the pointer wants to be written into a 
  //memory location

  uint64_t target_lower_lifetime; //lower is stricter. the target must die faster than this.
  //measures the pointer's target's lifetime, for when the pointed-to memory location wants 
  //something to be written into it.
  
  Return_Info(IRgen_status err, llvm::Value* b, Type* t, bool o, uint64_t s, uint64_t u, uint64_t 
  l)
    : error_code(err), IR(b), type(t), on_stack(o), self_lifetime(s), target_upper_lifetime(u), 
    target_lower_lifetime(l) {}

  //default constructor for a null object
  //make sure it does NOT go in map<>objects, because the lifetime is not meaningful. no references 
  //allowed.
  Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), type(T_null), on_stack(false), 
  self_lifetime(0), target_upper_lifetime(0), target_lower_lifetime(-1ull) {}
};


class compiler_object
{
  llvm::Module *TheModule;
  llvm::IRBuilder<> Builder;
  llvm::ExecutionEngine* engine;

  //lists the ASTs we're currently looking at. goal is to prevent infinite loops.
  std::unordered_set<AST*> loop_catcher;

  //a stack for bookkeeping lifetimes; keeps track of when objects are alive.
  std::stack<AST*> object_stack;

  //maps ASTs to their generated IR and return type.
  std::unordered_map<AST*, Return_Info> objects;

  //these are the labels which are later in the basic block. you can jump to them without checking 
  //finiteness, but you must check the type stack
  //std::stack<AST*> future_labels;

  std::deque<Type> type_scratch_space;
  //a memory pool for storing temporary types. will be cleared at the end of compilation.
  //it must be a deque so that its memory locations stay valid after push_back().
  //even though the constant impact of deque (memory) is bad for small compilations.

  //increases by 1 every time an object is created. imposes an ordering on stack object lifetimes.
  uint64_t incrementor_for_lifetime = 0;

  //some objects have expired - this clears them
  void clear_stack(uint64_t desired_stack_size);

  llvm::AllocaInst* create_alloca_in_entry_block(uint64_t size);

  Return_Info generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* storage_location = 
  nullptr);

public:
  compiler_object();
  unsigned compile_AST(AST* target); //we can't combine this with the ctor, because it needs to 
  //return an int

  //exists when IRgen_status has an error.
  AST* error_location;
  unsigned error_field; //which field in error_location has the error

};

compiler_object::compiler_object() : error_location(nullptr), Builder(thread_context)
{
  if (VERBOSE_DEBUG)
    outstream << "creating compiler object\n";
  TheModule = new llvm::Module("temp module", thread_context);

  std::string ErrStr;
  
  //todo: memleak with our memory manager
  engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(TheModule))
    .setErrorStr(&ErrStr)
    .setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(new 
    llvm::SectionMemoryManager))
    .create();
  if (!engine)
  {
    outstream << "Could not create ExecutionEngine: " << ErrStr.c_str() << '\n';
    exit(1);
  }
}

unsigned compiler_object::compile_AST(AST* target)
{

  if (VERBOSE_DEBUG)
    outstream << "starting compilation\n";

  using namespace llvm;
  FunctionType *FT;

  auto size_of_return = get_size(target);
  if (size_of_return == 0) FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
  else if (size_of_return == 1) FT = FunctionType::get(llvm::Type::getInt64Ty(thread_context), 
  false);
  else FT = FunctionType::get(llvm::ArrayType::get(llvm::Type::getInt64Ty(thread_context), 
  size_of_return), false);

  Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

  BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
  Builder.SetInsertPoint(BB);

  auto return_object = generate_IR(target, false);
  if (return_object.error_code)
    return return_object.error_code; //error

  if (size_of_return)
    Builder.CreateRet(return_object.IR);
  else Builder.CreateRetVoid();

  TheModule->print(*llvm_outstream, nullptr);
  // Validate the generated code, checking for consistency.
  if (llvm::verifyFunction(*F, llvm_outstream))
  {
    llvm_unreachable("verification failed");
  }

  if (OPTIMIZE)
  {
    outstream << "optimized code: \n";
    llvm::legacy::FunctionPassManager FPM(TheModule);
    TheModule->setDataLayout(engine->getDataLayout());
    // Provide basic AliasAnalysis support for GVN.
    FPM.add(createBasicAliasAnalysisPass());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    FPM.add(createInstructionCombiningPass());
    // Reassociate expressions.
    FPM.add(createReassociatePass());
    // Eliminate Common SubExpressions.
    FPM.add(createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    FPM.add(createCFGSimplificationPass());

    // Promote allocas to registers.
    FPM.add(createPromoteMemoryToRegisterPass());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    FPM.add(createInstructionCombiningPass());
    // Reassociate expressions.
    FPM.add(createReassociatePass());

    FPM.doInitialization();
    FPM.run(*TheModule->begin());

    TheModule->print(*llvm_outstream, nullptr);
  }

  engine->finalizeObject();
  void* fptr = engine->getPointerToFunction(F);
  if (size_of_return)
  {
    uint64_t(*FP)() = (uint64_t(*)())(intptr_t)fptr;
    outstream <<  "Evaluated to " << FP() << '\n';
  }
  else
  {
    void(*FP)() = (void(*)())(intptr_t)fptr;
    FP();
  }
  return 0;
}


/** mem-to-reg only works on entry block variables.
thus, this function builds llvm::Allocas in the entry block.
maybe scalarrepl is more useful for us.
clang likes to allocate everything in the beginning, so we follow their lead
*/
llvm::AllocaInst* compiler_object::create_alloca_in_entry_block(uint64_t size) {
  if (size == 0)
    llvm_unreachable("tried to create a 0-size alloca");
  llvm::BasicBlock& first_block = Builder.GetInsertBlock()->getParent()->getEntryBlock();
  llvm::IRBuilder<> TmpB(&first_block, first_block.begin());

  //we explicitly create the array type instead of allocating multiples, because that's what clang 
  //does for C++ arrays.
  llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);
  if (size > 1) return TmpB.CreateAlloca(llvm::ArrayType::get(int64_type, size));
  else return TmpB.CreateAlloca(int64_type);
}

void compiler_object::clear_stack(uint64_t desired_stack_size)
{

  while (object_stack.size() != desired_stack_size)
  {
    //todo: run dtors if necessary, such as for lock elements
    object_stack.pop();
  }
}

/** generate_IR() is the main code that transforms AST into LLVM IR. it is called with the AST to 
be transformed, which must not be nullptr.

storage_location is for RVO. if stack_degree says that the return object should be stored 
somewhere, the location will be storage_location. storage is done by the returning function.
ASTs that are directly inside basic blocks should allocate their own stack_memory, so they are 
given stack_degree = 2.
  this tells them to create a memory location and to place the return value inside it. the memory 
  location is returned.
ASTs that should place their return object inside an already-created memory location are given 
stack_degree = 1, and storage_location.
  then, they store the return value into storage_location. storage_location is returned.
ASTs that return SSA objects are given stack_degree = 0.
  minor note: we use create_alloca_in_entry_block() so that the llvm optimization pass mem2reg can 
  recognize it.

Steps of generate_IR:
check if generate_IR will be in an infinite loop, by using loop_catcher to store the previously 
seen ASTs.
run generate_IR on the previous AST in the basic block, if such an AST exists.
  after this previous AST is generated, we can figure out where the current AST lives on the stack. 
  this uses incrementor_for_lifetime.
find out what the size of the return object is, using get_size()
generate_IR is run on fields of the AST. it knows how many fields are needed by using 
fields_to_compile from AST_descriptor[] 
then, the main IR generation is done using a switch-case 
on a tag.
the return value is created using the finish() macros. these automatically pull in any 
miscellaneous necessary information, such as stack lifetimes, and bundles them into a Return_Info. 
furthermore, if the return value needs to be stored in a stack location, finish() does this 
automatically using the move_to_stack lambda.

note that while we have a rich type system, the only types that llvm sees are int64s and arrays of 
int64s. pointers, locks, etc. have their information stripped, so llvm only sees the size of each 
object. we forcefully cast things to integers.
  for an object of size 1, its llvm-type is an integer
  for an object of size N>=2, the llvm-type is an array of N integers.
however, note that pointers are still pointers, even though they are casted to integers. so the 
llvm::Value* of an object that had stack_degree = 2 is a pointer to the memory location, casted to 
an integer.
*/
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* 
storage_location)
{
  //an error has occurred. mark the target, return the error code, and don't construct a return 
  //object.
#define return_code(X, Y) do { error_location = target; error_field = Y; return \
Return_Info(IRgen_status::X, nullptr, T_null, false, 0, 0, 0); } while (0)

  if (VERBOSE_DEBUG)
  {
    outstream << "generate_IR single AST start ^^^^^^^^^\n";
    output_AST(target);
    outstream << "stack degree " << stack_degree;
    outstream << ", storage location is ";
    if (storage_location)
      storage_location->print(*llvm_outstream);
    else outstream << "null";
    outstream << '\n';
    outstream << "dumping module before generation:\n";
    TheModule->print(*llvm_outstream, nullptr);
  }


  //target should never be nullptr.
  if (target == nullptr) llvm_unreachable("null AST compiler bug");

  //if we've seen this AST before, we're stuck in an infinite loop. return an error.
  if (this->loop_catcher.find(target) != this->loop_catcher.end()) return_code(infinite_loop, 10);
  loop_catcher.insert(target); //we've seen this AST now.

  //after we're done with this AST, we remove it from loop_catcher.
  struct loop_catcher_destructor_cleanup{
    //compiler_state can only be used if it is passed in.
    compiler_object* object; AST* targ;
    loop_catcher_destructor_cleanup(compiler_object* x, AST* t) : object(x), targ(t) {}
    ~loop_catcher_destructor_cleanup() { object->loop_catcher.erase(targ); }
  } temp_object(this, target);

  //compile the previous elements in the basic block.
  if (target->preceding_BB_element)
  {
    auto previous_elements = generate_IR(target->preceding_BB_element, 2);
    if (previous_elements.error_code) return previous_elements;
  }

  //after compiling the previous elements in the basic block, we find the lifetime of this element
  uint64_t lifetime_of_return_value = incrementor_for_lifetime++;

  uint64_t size_result = -1; //note: it's only active when stack_degree = 1. otherwise, you must 
  //have special cases.
  //-1 is a debug choice, since having it at 0 led to invisible errors.
  //whenever you use create_alloca(), make sure size_result is actually set.

  uint64_t final_stack_position = object_stack.size();


  //generated IR of the fields of the AST
  std::vector<Return_Info> field_results; //we don't actually need the return code, but we leave it 
  //anyway.

  for (unsigned x = 0; x < AST_descriptor[target->tag].fields_to_compile; ++x)
  {
    Return_Info result; //default constructed for null object
    if (target->fields[x].ptr)
    {
      result = generate_IR(target->fields[x].ptr, false);
      if (result.error_code) return result;
    }

    if (VERBOSE_DEBUG)
    {
      outstream << "type checking. result type is \n";
      output_type_and_previous(result.type);
      outstream << "desired type is \n";
      output_type_and_previous(AST_descriptor[target->tag].parameter_types[x]);
    }
    //check that the type matches.
    if (AST_descriptor[target->tag].parameter_types[x] != T_nonexistent)
      if (type_check(RVO, result.type, AST_descriptor[target->tag].parameter_types[x]) != 3) 
      return_code(type_mismatch, x);

    field_results.push_back(result);
  }

  llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);

  //internal: do not call this directly. use the finish macro instead
  //places a constructed object into storage_location.
  auto move_to_stack = [&](llvm::Value* return_value) -> llvm::Value*
  {
    if (size_result == -1) size_result = get_size(target); //note that it's -1, not -1ull.
    if (stack_degree == 2 && size_result >= 1 && storage_location == nullptr) storage_location = 
    create_alloca_in_entry_block(size_result);
    if (size_result >= 1)
    {
      Builder.CreateStore(return_value, storage_location);
      return storage_location;
    }
    return return_value;
  };

  //internal: do not call this directly. use the finish macro instead
  //clears dead objects off the stack, and makes your result visible to other ASTs
  auto finish_internal = [&](llvm::Value* return_value, Type* type, uint64_t upper_life, uint64_t 
  lower_life) -> Return_Info
  {
    if (VERBOSE_DEBUG && return_value != nullptr)
    {
      outstream << "finish() in generate_IR, called value is ";
      return_value->print(*llvm_outstream);
      outstream << "\nand the llvm type is ";
      return_value->getType()->print(*llvm_outstream);
      outstream << "\nand the internal type is ";
      output_type_and_previous(type);
      outstream << '\n';
    }
    else if (VERBOSE_DEBUG) outstream << "finish() in generate_IR, null value\n";
    if (VERBOSE_DEBUG)
      outstream << "generate_IR single AST " << target << " vvvvvvvvv\n";

    //we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for 
    //justification.
    //TODO: what about if statements? they should eliminate temporaries no matter what, since it's 
    //not guaranteed that the temporary will exist.
    if (stack_degree == 2) clear_stack(final_stack_position);

    if (stack_degree >= 1)
    {
      if (size_result >= 1)
      {
        object_stack.push(target);
        auto insert_result = objects.insert({ target, Return_Info(IRgen_status::no_error, 
        return_value, type, true, lifetime_of_return_value, upper_life, lower_life) });
        if (!insert_result.second) //collision: AST is already there
          return_code(active_object_duplication, 10);
      }
      //type_scratch_space.push_back(Type("cheap pointer", type)); //stack objects are always 
      //pointers, just like in llvm.
      //type = &type_scratch_space.back();
    }
    return Return_Info(IRgen_status::no_error, return_value, type, false, lifetime_of_return_value, 
    upper_life, lower_life);
  };

  //call the finish macros when you've constructed something. the _pointer suffix is when you are 
  //returning a pointer, and need lifetime information.
  //use finish_passthrough if your AST is not the one constructing the return object.
  //for example, concatenate() and if() use previously constructed return objects, and simply pass 
  //them through.

  //we can't use X directly because we will be duplicating the argument.
#define finish_pointer(X, type, u, l) do {llvm::Value* first_value = X; llvm::Value* end_value = \
stack_degree >= 1 ? move_to_stack(first_value) : first_value; return finish_internal(end_value, \
type, u, l); } while (0)
#define finish(X, type) do { finish_pointer(X, type, 0, -1ull); } while (0)
#define finish_passthrough(X, type) do { return finish_internal(X, type, 0, -1ull); } while (0)
#define finish_passthrough_pointer(X, type, u, l) do { return finish_internal(X, type, u, l); } \
while (0)

  //we generate code for the AST, depending on its tag.
  switch (target->tag)
  {
  case ASTn("integer"):
    {
      finish(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].num)), 
      T_int);
    }
  case ASTn("add"): //add two integers.
    finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR), T_int);
  case ASTn("subtract"):
    finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR), T_int);
  case ASTn("hello"):
    {
      llvm::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!\n");

      //create the function type
      std::vector<llvm::Type *> putsArgs;
      putsArgs.push_back(Builder.getInt8Ty()->getPointerTo());
      llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);
      llvm::FunctionType *putsType = llvm::FunctionType::get(Builder.getInt32Ty(), argsRef, false);

      //get the actual function
      llvm::Constant *putsFunc = TheModule->getOrInsertFunction("puts", putsType);

      finish(Builder.CreateCall(putsFunc, helloWorld), T_null);
    }
  case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
    {
      llvm::FunctionType *twister_type = llvm::FunctionType::get(int64_type, false);
      llvm::PointerType *twister_ptr_type = llvm::PointerType::getUnqual(twister_type);
      llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 
      (uint64_t)&generate_random));
      llvm::Value *twister_function = Builder.CreateIntToPtr(twister_address, twister_ptr_type);
      finish(Builder.CreateCall(twister_function), T_int);
    }
  case ASTn("if"): //todo: you can see the condition's return object in the branches.
    //we could have another version where the condition's return object is invisible.
    //this lets us goto the inside of the if statement.
    {


      //the condition statement
      if (target->fields[0].ptr == nullptr)
        return_code(null_AST, 0);
      auto condition = generate_IR(target->fields[0].ptr, 0);
      if (condition.error_code) return condition;

      if (type_check(RVO, condition.type, T_int) != 3)
        return_code(type_mismatch, 0);

      if (stack_degree == 2)
      {
        size_result = get_size(target);
        if (size_result >= 1) storage_location = create_alloca_in_entry_block(size_result);
      }

      //since the fields are conditionally executed, the temporaries generated in each branch are 
      //not necessarily referenceable.
      //therefore, we must clear the stack between each branch.
      uint64_t if_stack_position = object_stack.size();

      //see http://llvm.org/docs/tutorial/LangImpl5.html#code-generation-for-if-then-else
      llvm::Value* comparison = Builder.CreateICmpNE(condition.IR, 
      llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)));
      llvm::Function *TheFunction = Builder.GetInsertBlock()->getParent();

      // Create blocks for the then and else cases.  Insert the 'then' block at the end of the 
      //function.
      llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(thread_context, "then", TheFunction);
      llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(thread_context, "else");
      llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(thread_context, "ifcont");

      Builder.CreateCondBr(comparison, ThenBB, ElseBB);

      //start inserting code in the "then" block
      //WATCH OUT: this actually sets the insert point to be the end of the "Then" basic block. 
      //we're relying on the block being empty.
      //if there are already labels inside the "Then" basic block, we could be in big trouble.
      Builder.SetInsertPoint(ThenBB);

      //calls with stack_degree as 1 in the called function, if it is 2 outside.
      Return_Info then_IR; //default constructor for null object
      if (target->fields[1].ptr != nullptr)
        then_IR = generate_IR(target->fields[1].ptr, stack_degree != 0, storage_location);
      if (then_IR.error_code) return then_IR;

      //get rid of any temporaries
      clear_stack(if_stack_position);

      Builder.CreateBr(MergeBB);
      ThenBB = Builder.GetInsertBlock();

      TheFunction->getBasicBlockList().push_back(ElseBB);
      Builder.SetInsertPoint(ElseBB); //WATCH OUT: same here.

      Return_Info else_IR; //default constructor for null object
      if (target->fields[2].ptr != nullptr)
        else_IR = generate_IR(target->fields[2].ptr, stack_degree != 0, storage_location);
      if (else_IR.error_code) return else_IR;

      //RVO, because that's what defines the slot.
      if (type_check(RVO, then_IR.type, else_IR.type) != 3)
        return_code(type_mismatch, 2);

      //for the second branch
      Builder.CreateBr(MergeBB);
      ElseBB = Builder.GetInsertBlock();

      // Emit merge block.
      TheFunction->getBasicBlockList().push_back(MergeBB);
      Builder.SetInsertPoint(MergeBB);

      uint64_t result_target_upper_lifetime = std::max(then_IR.target_upper_lifetime, 
      else_IR.target_upper_lifetime);
      uint64_t result_target_lower_lifetime = std::min(then_IR.target_lower_lifetime, 
      else_IR.target_lower_lifetime);
      if (stack_degree == 0)
      {
        size_result = get_size(target->fields[1].ptr);
        if (size_result == 0)
          return_code(no_error, 0);

        llvm::PHINode *PN;
        if (size_result == 1)
          PN = Builder.CreatePHI(int64_type, 2);
        else
        {
          llvm::Type* array_type = llvm::ArrayType::get(int64_type, size_result);
          PN = Builder.CreatePHI(array_type, 2);
        }
        PN->addIncoming(then_IR.IR, ThenBB);
        PN->addIncoming(else_IR.IR, ElseBB);
        finish_pointer(PN, then_IR.type, result_target_upper_lifetime, 
        result_target_lower_lifetime);
      }
      else finish_passthrough_pointer(storage_location, then_IR.type, result_target_upper_lifetime, 
      result_target_lower_lifetime);
      //even though finish_pointer returns, the else makes it clear from first glance that it's not 
      //a continued statement.
    }
  case ASTn("scope"):
    finish(nullptr, T_null);
  case ASTn("pointer"):
    {
      auto found_AST = objects.find(target->fields[0].ptr);
      if (found_AST == objects.end()) return_code(pointer_without_target, 0);
      if (found_AST->second.on_stack == false) return_code(pointer_to_temporary, 0);
      //our new pointer type
      type_scratch_space.push_back(Type("cheap pointer", found_AST->second.type));

      ///we force cast all llvm pointer types to integers. this makes it easy to represent types 
      //inside llvm, since they're described by a single number - their size.
      llvm::Value* final_result = Builder.CreatePtrToInt(found_AST->second.IR, int64_type);

      finish_pointer(final_result, &type_scratch_space.back(), found_AST->second.self_lifetime, 
      found_AST->second.self_lifetime);
    }
  case ASTn("load"):
    {
      auto found_AST = objects.find(target->fields[0].ptr);
      if (found_AST == objects.end())
        return_code(pointer_without_target, 0);
      if (found_AST->second.on_stack == false) //it's not an AllocaInst
        finish_pointer(found_AST->second.IR, found_AST->second.type, 
        found_AST->second.target_upper_lifetime, found_AST->second.target_lower_lifetime);
      else finish(Builder.CreateLoad(found_AST->second.IR), found_AST->second.type);
    }
    /*case ASTn("get 0"):
      finish(llvm::ConstantInt::get(thread_context, llvm::APInt(64, 0)), T_int);*/
  case ASTn("concatenate"):
    {
      //TODO: if stack_degree == 1, then you've already gotten the size. look it up in a special 
      //location.

      int64_t first_size = get_size(target->fields[0].ptr);
      uint64_t second_size = get_size(target->fields[1].ptr);
      size_result = first_size + second_size;
      if (first_size > 0 && second_size > 0)
      {
        //we want the return objects to RVO. thus, we create a memory slot.
        if (stack_degree == 0 || stack_degree == 2)
            storage_location = create_alloca_in_entry_block(size_result);

        llvm::AllocaInst* first_location = storage_location;
        if (first_size == 1) first_location = 
        (llvm::AllocaInst*)Builder.CreateConstGEP2_64(storage_location, 0, 0);

        if (target->fields[0].ptr == nullptr)
          return_code(null_AST, 0);
        Return_Info first_half = generate_IR(target->fields[0].ptr, 1, first_location);
        if (first_half.error_code) return first_half;


        llvm::AllocaInst* second_location;
        if (second_size == 1) second_location = 
        (llvm::AllocaInst*)Builder.CreateConstGEP2_64(storage_location, 0, first_size);
        else
        {
          llvm::Type* array_type = llvm::ArrayType::get(int64_type, second_size);
          llvm::Type* pointer_to_array = llvm::PointerType::getUnqual(array_type);
          llvm::Value* second_pointer = Builder.CreateConstGEP2_64(storage_location, 0, 
          first_size); //pointer to int.
          second_location = (llvm::AllocaInst*)Builder.CreatePointerCast(second_pointer, 
          pointer_to_array); //ptr to array.
        }
        if (target->fields[1].ptr == nullptr) return_code(null_AST, 1);
        Return_Info second_half = generate_IR(target->fields[1].ptr, 1, second_location);
        if (second_half.error_code) return second_half;
        type_scratch_space.push_back(Type("concatenate", first_half.type, second_half.type));

        llvm::Value* final_value;
        if (stack_degree == 0) final_value = Builder.CreateLoad(storage_location);
        else final_value = storage_location;

        uint64_t result_target_upper_lifetime = std::max(first_half.target_upper_lifetime, 
        second_half.target_upper_lifetime);
        uint64_t result_target_lower_lifetime = std::min(first_half.target_lower_lifetime, 
        second_half.target_lower_lifetime);

        finish_passthrough_pointer(final_value, &type_scratch_space.back(), 
        result_target_upper_lifetime, result_target_lower_lifetime);
      }
      else //it's convenient having a special case, because no need to worry about integer llvm 
      //values for size-1 objects.
      {

        if (target->fields[0].ptr == nullptr) return_code(null_AST, 0);
        //we only have to RVO if stack_degree is 1, because otherwise we can return the result 
        //directly.
        //that means we don't have to create a storage_location here.
        Return_Info first_half = generate_IR(target->fields[0].ptr, (stack_degree == 1) && 
        (first_size > 0), storage_location);
        if (first_half.error_code) return first_half;

        if (target->fields[1].ptr == nullptr) return_code(null_AST, 1);
        Return_Info second_half = generate_IR(target->fields[1].ptr, (stack_degree == 1) && 
        (second_size > 0), storage_location);
        if (second_half.error_code) return second_half;

        if (first_size > 0)
          finish_passthrough_pointer(first_half.IR, first_half.type, 
          first_half.target_upper_lifetime, first_half.target_lower_lifetime);
        else
          finish_passthrough_pointer(second_half.IR, second_half.type, 
          second_half.target_upper_lifetime, second_half.target_lower_lifetime);
        //even if second_half is 0, we return it anyway.
      }
    }
  }

  llvm_unreachable("fell through switches");
}

/**
The fuzztester generates random ASTs and attempts to compile them.
the output "Malformed AST" is fine. not all randomly-generated ASTs will be well-formed.

fuzztester starts with a vector of pointers to working, compilable ASTs (originally just a 
nullptr).
it chooses a random AST tag. this tag requires AST_descriptor[tag].pointer_fields pointers
then, it chooses pointers randomly from the vector of working ASTs.
these pointers go in the first few fields.
each remaining field has a 50-50 chance of either being a random number, or 0.
finally, if the created AST successfully compiles, it is added to the vector of working ASTs.
*/
void fuzztester(unsigned iterations)
{
  std::vector<AST*> AST_list{ nullptr }; //start with nullptr as the default referenceable AST
  while (iterations--)
  {
    //create a random AST
    unsigned tag = mersenne() % ASTn("never reached");
    unsigned pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST 
    //pointers. they will come at the beginning
    unsigned prev_AST = generate_exponential_dist() % AST_list.size(); //todo: prove that 
    //exponential_dist is desired.
    //birthday collisions is the problem. a concatenate with two branches will almost never appear, 
    //because it'll result in an active object duplication.
    //but does exponential falloff solve this problem in the way we want?

    int_or_ptr<AST> fields[4]{nullptr, nullptr, nullptr, nullptr};
    int incrementor = 0;
    for (; incrementor < pointer_fields; ++incrementor)
      fields[incrementor] = AST_list.at(mersenne() % AST_list.size()); //get pointers to previous 
      //ASTs
    for (; incrementor < max_fields_in_AST; ++incrementor)
      fields[incrementor] = generate_exponential_dist(); //get random integers and fill in the 
      //remaining fields
    AST* test_AST= new AST(tag, AST_list.at(prev_AST), fields[0], fields[1], fields[2], fields[3]);
    output_AST_and_previous(test_AST);
    output_AST_console_version a(test_AST);

    compiler_object compiler;
    unsigned error_code = compiler.compile_AST(test_AST);
    if (error_code)
    {
        outstream << "Malformed AST: code " << error_code << " at AST " << compiler.error_location 
        << "\n\n";
        delete test_AST;
    }
    else
    {
      AST_list.push_back(test_AST);
      outstream << "Successful compile " << AST_list.size() - 1 << "\n";
    }
    if (INTERACTIVE)
    {
      outstream << "Press enter to continue\n";
      std::cin.get();
    }
  }
}

#ifdef NDEBUG
panic time!
#endif
#include <iostream>
#include <sstream>
#include <istream>
//parses console input and generates ASTs.
using std::string; //why can't this go inside source_reader?
class source_reader
{

  std::unordered_map<string, AST*> ASTmap = { { "0", nullptr } }; //from name to location. starts 
  //with 0 = nullptr.
  std::istream& input;

  //grabs a token. characters like [, ], {, } count as tokens by themselves.
  //otherwise, get characters until you hit whitespace or a special character.
  //if the last char is \n, then it doesn't consume it.
  string get_token()
  {
    while (input.peek() == ' ')
      input.ignore(1);

    char next_char = input.peek();
    switch (next_char)
    {
    case '[':
    case ']':
    case '{':
    case '}':
      input.ignore(1);
      return string(1, next_char);
    case '\n':
      return string();
    default:
      string word;
      while (next_char != '[' && next_char != ']' && next_char != '{' && next_char != '}' && 
      next_char != '\n' && next_char != ' ')
      {
        word.push_back(next_char);
        input.ignore(1);
        next_char = input.peek();
      }
      //outstream << "word is " << word << "|||\n";
      return word;
    }
  }

  //continued_string is in case we already read the first token. then, that's the first thing to 
  //start with.
  AST* read_single_AST(AST* previous_AST, string continued_string = "")
  {
    string first = continued_string == "" ? get_token() : continued_string;

    if (first != string(1, '[')) //it's a name reference.
    {
      //outstream << "first was " << first << '\n';
      auto AST_search = ASTmap.find(first);
      check(AST_search != ASTmap.end(), string("variable name not found: ") + first);
      return AST_search->second;
    }

    string tag = get_token();

    uint64_t AST_type = ASTn(tag.c_str());
    if (VERBOSE_DEBUG) outstream << "AST tag was " << AST_type << "\n";
    uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;

    int_or_ptr<AST> fields[max_fields_in_AST] = { nullptr, nullptr, nullptr, nullptr };

    //field_num is which field we're on
    int field_num = 0;
    for (string next_token = get_token(); next_token != "]"; next_token = get_token(), ++field_num)
    {
      check(field_num < max_fields_in_AST, "more fields than possible");
      if (field_num < pointer_fields) //it's a pointer!
      {
        if (next_token == "{") fields[field_num] = create_single_basic_block(true);
        else fields[field_num] = read_single_AST(nullptr, next_token);
      }
      else //it's an integer
      {
        bool isNumber = true;
        for (auto& k : next_token)
          isNumber = isNumber && isdigit(k);
        check(isNumber, "tried to input non - number");
        fields[field_num] = std::stoull(next_token);
      }
    }
    //check(next_token == "]", "failed to have ]");
    AST* new_type = new AST(AST_type, previous_AST, fields[0], fields[1], fields[2], fields[3]);

    if (VERBOSE_DEBUG)
      output_AST_console_version a(new_type);

    //get an AST name if any.
    if (input.peek() != ' ' && input.peek() != '\n' && input.peek() != ']' && input.peek() != '[' 
    && input.peek() != '{' && input.peek() != '}')
    {
      string thisASTname = get_token();
      //seems that once we consume the last '\n', the cin stream starts blocking again.
      if (!thisASTname.empty())
      {
        check(ASTmap.find(thisASTname) == ASTmap.end(), string("duplicated variable name: ") + 
        thisASTname);
        ASTmap.insert(std::make_pair(thisASTname, new_type));
      }
    }

    if (VERBOSE_DEBUG)
    {
      outstream << "next char is" << (char)input.peek() << input.peek();
      outstream << '\n';
    }
    return new_type;


  }


public:
  source_reader(std::istream& stream, char end) : input(stream) {}
  AST* create_single_basic_block(bool create_brace_at_end = false)
  {
    AST* current_previous = nullptr;
    if (create_brace_at_end == false)
    {
      if (std::cin.peek() == '\n') std::cin.ignore(1);
      for (string next_word = get_token(); next_word != ""; next_word = get_token())
        current_previous = read_single_AST(current_previous, next_word);
      return current_previous; //which is the very last.
    }
    else
    {
      for (string next_word = get_token(); next_word != "}"; next_word = get_token())
        current_previous = read_single_AST(current_previous, next_word);
      return current_previous; //which is the very last
    }
  }
};

int main(int argc, char* argv[])
{
  //tells LLVM the generated code should be for the native platform
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  //these do nothing
  //assert(0);
  //assert(1);

  //used for testing ASTn's error reporting
  /*switch (2)
  {
  case ASTn("fiajewoar"):
    break;
  }*/
  for (int x = 1; x < argc; ++x)
  {
    if (strcmp(argv[x], "interactive") == 0) INTERACTIVE = true;
    if (strcmp(argv[x], "verbose") == 0) VERBOSE_DEBUG = true;
    if (strcmp(argv[x], "optimize") == 0) OPTIMIZE = true;
    if (strcmp(argv[x], "console") == 0) CONSOLE = true;
    if (strcmp(argv[x], "quiet") == 0)
    {
      outstream.setstate(std::ios_base::failbit);
      llvm_outstream = &llvm_null_stream;
    }
  }
  /*
  AST get1("integer", nullptr, 1); //get the integer 1
  AST get2("integer", nullptr, 2);
  AST get3("integer", nullptr, 3);
  AST addthem("add", nullptr, &get1, &get2); //add integers 1 and 2


  AST getif("if", &addthem, &get1, &get2, &get3); //first, execute addthem. then, if get1 is 
  //non-zero, return get2. otherwise, return get3.
  AST helloworld("hello", &getif); //first, execute getif. then output "Hello World!"
  compiler_object compiler;
  compiler.compile_AST(&helloworld);
  */

  if (CONSOLE)
  {
    while (1)
    {
      source_reader k(std::cin, '\n'); //have to reinitialize to clear the ASTmap
      outstream << "Input AST:\n";
      AST* end = k.create_single_basic_block();
      outstream << "Done reading.\n";
      if (end == nullptr)
      {
        outstream << "it's nullptr\n";
        std::cin.get();
        continue;
      }
      output_AST_and_previous(end);
      compiler_object j;
      unsigned error_code = j.compile_AST(end);
      if (error_code)
        outstream << "Malformed AST: code " << error_code << " at AST " << j.error_location << 
        "\n\n";
      else outstream << "Successful compile\n\n";
      }
  }

  fuzztester(-1);
}
