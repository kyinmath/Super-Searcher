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
*/
#include <cstdint>
#include <iostream>
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
#include "unique_type_creator.h"

bool OPTIMIZE = false;
bool VERBOSE_DEBUG = false;
bool INTERACTIVE = false;
bool CONSOLE = false;
bool TIMER = false;

basic_onullstream<char> null_stream;
std::ostream& outstream = std::cerr;
llvm::raw_null_ostream llvm_null_stream;
llvm::raw_ostream* llvm_outstream = &llvm::outs();

//s("test") returns "test" if debug_names is true, and an empty string if debug_names is false.
#define debug_names
#ifdef debug_names
std::string s(std::string k) {return k;}
#else
std::string s(std::string k) { return ""; }
#endif

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
  Return_Info() : error_code(IRgen_status::no_error), IR(nullptr), type(T::null), on_stack(false), 
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

  //a memory pool for storing temporary types. will be cleared at the end of compilation.
  //it must be a deque so that its memory locations stay valid after push_back().
  //even though the constant impact of deque (memory) is bad for small compilations.
  std::deque<Type> type_scratch_space;

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
  //todo: right now, compile_AST runs the function that it creates. that's not what we want.

  //exists when IRgen_status has an error.
  AST* error_location;
  unsigned error_field; //which field in error_location has the error

  //these exist on successful compilation. guaranteed to be uniqued and in the heap.
  //currently, parameters are not implemented
  Type* return_type;
  Type* parameter_type = nullptr;
};


uint64_t ASTmaker()
{
  unsigned iterations = 4;
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
    AST* test_AST = new AST(tag, AST_list.at(prev_AST), fields[0], fields[1], fields[2], 
    fields[3]);
    output_AST_and_previous(test_AST);
    output_AST_console_version a(test_AST);

    compiler_object compiler;
    unsigned error_code = compiler.compile_AST(test_AST);
    if (error_code) delete test_AST;
    else AST_list.push_back(test_AST);
  }
  output_AST_and_previous(AST_list.back());
  return (uint64_t)AST_list.back();
}

/* this is a function that the compile() AST will call.
returns a pointer to the return object, which is either:
1. a working pointer to AST
2. a failure object to be read
the bool is TRUE if the compilation is successful, and FALSE if not.
this determines what the type of the pointer is.

the type of target is actually of type AST*. it's stated as uint64_t for current convenience
Note: memory allocations that interact with the user should use new, not malloc.
we might need realloc. BUT, the advantage of new is that it throws an exception on failure.

NOTE: std::array<2> becomes {uint64_t, uint64_t}. but array<4> becomes [i64 x 4].
*/
std::array<uint64_t, 2> compile_user_facing(uint64_t target)
{
  compiler_object a;
  unsigned error = a.compile_AST((AST*)target);

  void* return_object_pointer;
  Type* return_type_pointer;
  if (error)
  {
    uint64_t* error_return = new uint64_t[3];
    error_return[0] = error;
    error_return[1] = (uint64_t)a.error_location;
    error_return[2] = a.error_field;

    return_object_pointer = error_return;
    return_type_pointer = T::error_object;
  }
  else
  {
    return_object_pointer = nullptr;
    //todo: make this actually work.

    Type* function_return_type = a.return_type;
    return_type_pointer = new Type("function in clouds", a.return_type, a.parameter_type);
  }
  return std::array < uint64_t, 2 > {(uint64_t)return_object_pointer, 
  (uint64_t)return_type_pointer};
}



compiler_object::compiler_object() : error_location(nullptr), Builder(thread_context),
TheModule(new llvm::Module("temp module", thread_context))
{
  if (VERBOSE_DEBUG) outstream << "creating compiler object\n";

  std::string ErrStr;
  
  //todo: memleak with our memory manager
  engine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(TheModule))
    .setErrorStr(&ErrStr)
    .setMCJITMemoryManager(std::unique_ptr<llvm::SectionMemoryManager>(new 
    llvm::SectionMemoryManager))
    .create();
  check(engine, "Could not create ExecutionEngine: " + ErrStr + "\n"); //check engine! :D
}

template<size_t array_num> void cout_array(std::array<uint64_t, array_num> object)
{
  outstream << "Evaluated to";
  for (int x = 0; x < array_num; ++x) outstream << ' ' << object[x];
  outstream << '\n';
}

//return value is the error code, which is 0 if successful
unsigned compiler_object::compile_AST(AST* target)
{

  if (VERBOSE_DEBUG) outstream << "starting compilation\n";
  outstream << "target is " << target << '\n'; //necessary in case it crashes here

  if (target == nullptr)
  {
    return IRgen_status::null_AST;
  }
  using namespace llvm;
  FunctionType *FT;

  auto size_of_return = get_size(target);
  if (size_of_return == 0) FT = FunctionType::get(llvm::Type::getVoidTy(thread_context), false);
  else if (size_of_return == 1) FT = FunctionType::get(llvm::Type::getInt64Ty(thread_context), 
  false);
  else FT = FunctionType::get(llvm::ArrayType::get(llvm::Type::getInt64Ty(thread_context), 
  size_of_return), false);
  if (VERBOSE_DEBUG) outstream << "Size of return is " << size_of_return << '\n';

  Function *F = Function::Create(FT, Function::ExternalLinkage, "temp function", TheModule);

  BasicBlock *BB = BasicBlock::Create(thread_context, "entry", F);
  Builder.SetInsertPoint(BB);

  auto return_object = generate_IR(target, false);
  if (return_object.error_code) return return_object.error_code; //error

  return_type = get_unique_type(return_object.type, false);
  if (size_of_return) Builder.CreateRet(return_object.IR);
  else Builder.CreateRetVoid();

  TheModule->print(*llvm_outstream, nullptr);
  debugcheck(!llvm::verifyFunction(*F, llvm_outstream), "verification failed");
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
  if (size_of_return == 1)
  {
    uint64_t(*FP)() = (uint64_t(*)())(uintptr_t)fptr;
    outstream <<  "Evaluated to " << FP() << '\n';
  }
  else if (size_of_return > 1)
  {
    using std::array;
    switch (size_of_return)
    {
    case 2: cout_array(((array<uint64_t, 2>(*)())(intptr_t)fptr)()); break;
    case 3: cout_array(((array<uint64_t, 3>(*)())(intptr_t)fptr)()); break;
    case 4: cout_array(((array<uint64_t, 4>(*)())(intptr_t)fptr)()); break;
    case 5: cout_array(((array<uint64_t, 5>(*)())(intptr_t)fptr)()); break;
    case 6: cout_array(((array<uint64_t, 6>(*)())(intptr_t)fptr)()); break;
    case 7: cout_array(((array<uint64_t, 7>(*)())(intptr_t)fptr)()); break;
    case 8: cout_array(((array<uint64_t, 8>(*)())(intptr_t)fptr)()); break;
    case 9: cout_array(((array<uint64_t, 9>(*)())(intptr_t)fptr)()); break;
    default: outstream << "function return size is " << size_of_return << " and C++ seems to only \
take static return types\n"; break;

    }
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
  debugcheck(size > 0, "tried to create a 0-size alloca");
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
'
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
  for an object of size N>=2, the llvm-type is an alloca'd array of N integers, which is [12 x 
  i64]*. note the * - it's a pointer to an array, not the array itself.
example: the llvm::Value* of an object that had stack_degree = 2 is a pointer to the memory 
location, where the pointer is casted to an integer.
currently, the finish() macro takes in the array itself, not the pointer-to-array.
*/
Return_Info compiler_object::generate_IR(AST* target, unsigned stack_degree, llvm::AllocaInst* 
storage_location)
{
  //an error has occurred. mark the target, return the error code, and don't construct a return 
  //object.
#define return_code(X, Y) do { error_location = target; error_field = Y; return \
Return_Info(IRgen_status::X, nullptr, T::null, false, 0, 0, 0); } while (0)


  if (VERBOSE_DEBUG)
  {
    outstream << "generate_IR single AST start ^^^^^^^^^\n";
    output_AST(target);
    outstream << "stack degree " << stack_degree;
    outstream << ", storage location is ";
    if (storage_location) storage_location->print(*llvm_outstream);
    else outstream << "null\n";
  }

  if (stack_degree == 2) debugcheck(storage_location == nullptr, "if stack degree is 2, we should \
have a nullptr storage_location");
  if (stack_degree == 1) debugcheck((storage_location != nullptr) || (get_size(target) == 0), "if \
stack degree is 1, we should have a storage_location");
  debugcheck(target != nullptr, "generate_IR should never receive a nullptr target");

  //if we've seen this AST before, we're stuck in an infinite loop. return an error.
  if (this->loop_catcher.find(target) != this->loop_catcher.end()) return_code(infinite_loop, 10);
  loop_catcher.insert(target); //we've seen this AST now.

  //after we're done with this AST, we remove it from loop_catcher.
  struct loop_catcher_destructor_cleanup
  {
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
  //note that it's -1, not -1ull.
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
    if (AST_descriptor[target->tag].parameter_types[x] != T::nonexistent)
      if (type_check(RVO, result.type, AST_descriptor[target->tag].parameter_types[x]) != 3) 
      return_code(type_mismatch, x);

    field_results.push_back(result);
  }

  llvm::IntegerType* int64_type = llvm::Type::getInt64Ty(thread_context);

  //internal: do not call this directly. use the finish macro instead
  //clears dead objects off the stack, and makes your result visible to other ASTs
  //if it's necessary to create and write to storage_location, we do so.
  //if move_to_stack == true, it writes into a previously created storage_location
  auto finish_internal = [&](llvm::Value* return_value, Type* type, uint64_t upper_life, uint64_t 
  lower_life, bool move_to_stack) -> Return_Info
  {
    if (stack_degree >= 1 && size_result == -1) size_result = get_size(target); //note that it's 
    //-1, not -1ull.
    if (stack_degree == 2 && size_result != 0 && storage_location == nullptr)
    {
      if (storage_location == nullptr)
      {
        storage_location = create_alloca_in_entry_block(size_result);
        Builder.CreateStore(return_value, storage_location);
        return_value = storage_location;
      }
      else debugcheck(move_to_stack == false, "if move_to_stack is true, then we shouldn't have \
already created the storage_location");
    }
    if (stack_degree == 1 && move_to_stack)
    {
      if (size_result >= 1) //we don't do it for stack_degree = 2, because if storage_location =/= 
      //nullptr, we shouldn't write
      {
        Builder.CreateStore(return_value, storage_location);
        return_value = storage_location;
      }
    }

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
    {
      TheModule->print(*llvm_outstream, nullptr);
      outstream << "generate_IR single AST " << target << " vvvvvvvvv\n";
    }


    //we only eliminate temporaries if stack_degree = 2. see "doc/lifetime across fields" for 
    //justification.
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

  //call the finish macros when you've constructed something.
  //_pointer suffix is when target lifetime information is relevant (for pointers).
  //_stack_handled if your AST has already created a stack alloca whenever necessary. also, 
  //size_result must be already known.
  //for example, concatenate() and if() use previously constructed return objects, and simply pass 
  //them through.

  //we can't use X directly because that will duplicate any expressions in the argument.
#define finish_pointer(X, type, u, l) do {return finish_internal(X, type, u, l, true); } while (0)
#define finish(X, type) do { finish_pointer(X, type, 0, -1ull); } while (0)
#define finish_stack_handled(X, type) do { return finish_internal(X, type, 0, -1ull, false); } \
while (0)
#define finish_stack_handled_pointer(X, type, u, l) do { return finish_internal(X, type, u, l, \
false); } while (0)

  //we generate code for the AST, depending on its tag.
  switch (target->tag)
  {
  case ASTn("integer"):
    {
      finish(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, target->fields[0].num)), 
      T::integer);
    }
  case ASTn("add"): //add two integers.
    finish(Builder.CreateAdd(field_results[0].IR, field_results[1].IR), T::integer);
  case ASTn("subtract"):
    finish(Builder.CreateSub(field_results[0].IR, field_results[1].IR), T::integer);
  case ASTn("hello"):
    {
      llvm::Value *helloWorld = Builder.CreateGlobalStringPtr("hello world!\n");

      //create the function type
      std::vector<llvm::Type*> putsArgs;
      putsArgs.push_back(Builder.getInt8Ty()->getPointerTo());
      llvm::ArrayRef<llvm::Type*> argsRef(putsArgs);
      llvm::FunctionType *putsType = llvm::FunctionType::get(Builder.getInt32Ty(), argsRef, false);

      //get the actual function
      llvm::Constant *putsFunc = TheModule->getOrInsertFunction("puts", putsType);

      finish(Builder.CreateCall(putsFunc, helloWorld), T::null);
    }
  case ASTn("random"): //for now, we use the Mersenne twister to return a single uint64.
    {
      //llvm::FunctionType *twister_type = llvm::FunctionType::get(int64_type, false);
      llvm::PointerType *twister_ptr_type = llvm::FunctionType::get(int64_type, 
      false)->getPointerTo();
      llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 
      (uint64_t)&generate_random));
      llvm::Value *twister_function = Builder.CreateIntToPtr(twister_address, twister_ptr_type);
      finish(Builder.CreateCall(twister_function), T::integer);
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

      if (type_check(RVO, condition.type, T::integer) != 3) return_code(type_mismatch, 0);

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
      if (target->fields[1].ptr != nullptr) then_IR = generate_IR(target->fields[1].ptr, 
      stack_degree != 0, storage_location);
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
      else finish_stack_handled_pointer(storage_location, then_IR.type, 
      result_target_upper_lifetime, result_target_lower_lifetime);
      //even though finish_pointer returns, the else makes it clear from first glance that it's not 
      //a continued statement.
    }
  case ASTn("scope"):
    finish(nullptr, T::null);
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
      if (found_AST == objects.end()) return_code(pointer_without_target, 0);
      //if on_stack is false, it's not an alloca so we should copy instead of loading.
      Return_Info AST_to_load = found_AST->second;
      if (AST_to_load.on_stack == false) finish_pointer(AST_to_load.IR, AST_to_load.type, 
      AST_to_load.target_upper_lifetime, AST_to_load.target_lower_lifetime);
      else finish(Builder.CreateLoad(AST_to_load.IR), AST_to_load.type);
    }
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
        if (first_size == 1)
        {
          first_location = (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(storage_location, 
          0, 0);
        }
        else
        {
          llvm::Type* pointer_to_array = llvm::ArrayType::get(int64_type, 
          first_size)->getPointerTo();
          llvm::Value* first_pointer = Builder.CreateConstInBoundsGEP2_64(storage_location, 0, 0);
          first_location = (llvm::AllocaInst*)Builder.CreatePointerCast(first_pointer, 
          pointer_to_array);
        }

        if (target->fields[0].ptr == nullptr)
          return_code(null_AST, 0);
        Return_Info first_half = generate_IR(target->fields[0].ptr, 1, first_location);
        if (first_half.error_code) return first_half;


        llvm::AllocaInst* second_location;
        if (second_size == 1) second_location = 
        (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(storage_location, 0, first_size);
        else
        {
          llvm::Type* pointer_to_array = llvm::ArrayType::get(int64_type, 
          second_size)->getPointerTo();
          llvm::Value* second_pointer = Builder.CreateConstInBoundsGEP2_64(storage_location, 0, 
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

        finish_stack_handled_pointer(final_value, &type_scratch_space.back(), 
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
          finish_stack_handled_pointer(first_half.IR, first_half.type, 
          first_half.target_upper_lifetime, first_half.target_lower_lifetime);
        else
          finish_stack_handled_pointer(second_half.IR, second_half.type, 
          second_half.target_upper_lifetime, second_half.target_lower_lifetime);
        //even if second_half is 0, we return it anyway.
      }
    }

  case ASTn("dynamic"): //todo: you can see the condition's return object in the branches.
    {
      uint64_t size_of_object = get_size(target->fields[0].ptr);
      if (size_of_object >= 1)
      {
        uint64_t* pointer_to_memory = new uint64_t[size_of_object];

        //can't use ?: because the return objects have different types
        //this represents either an integer or an array of integers.
        llvm::Type* target_type = int64_type;
        if (size_of_object > 1) target_type = llvm::ArrayType::get(int64_type, size_of_object);
        llvm::Type* target_pointer_type = target_type->getPointerTo();

        //store the returned value into the acquired address
        llvm::Constant* dynamic_object_address = llvm::Constant::getIntegerValue(int64_type, 
        llvm::APInt(64, (uint64_t)pointer_to_memory));
        llvm::Value* dynamic_object = Builder.CreateIntToPtr(dynamic_object_address, 
        target_pointer_type);
        Builder.CreateStore(field_results[0].IR, dynamic_object);

        //create a pointer to the type of the dynamic pointer. but we serialize the pointer to be 
        //an integer.
        Type* type_of_dynamic_object = get_unique_type(field_results[0].type, false);
        llvm::Constant* integer_type_pointer = llvm::Constant::getIntegerValue(int64_type, 
        llvm::APInt(64, (uint64_t)type_of_dynamic_object));


        //we now serialize both objects to become integers.
        llvm::AllocaInst* final_dynamic_pointer = create_alloca_in_entry_block(2);
        llvm::Value* part1 = 
        (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 0);
        Builder.CreateStore(dynamic_object_address, part1);
        llvm::Value* part2 = 
        (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 1);
        Builder.CreateStore(integer_type_pointer, part2);

        finish(Builder.CreateLoad(final_dynamic_pointer), T::dynamic_pointer);
      }
      else
      {
        //zero array, for null dynamic object.
        llvm::AllocaInst* final_dynamic_pointer = create_alloca_in_entry_block(2);
        llvm::Value* part1 = 
        (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 0);
        Builder.CreateStore(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 0)), 
        part1);
        llvm::Value* part2 = 
        (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 1);
        Builder.CreateStore(llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 0)), 
        part2);
        //abort();
        finish(Builder.CreateLoad(final_dynamic_pointer), T::dynamic_pointer);
        //todo: change this to finish(final_dynamic_pointer, T::dynamic_pointer);. llvm will start 
        //emitting some errors about function return type. how do I get that to abort the program?
      }
    }
  case ASTn("compile"):
    {
      std::vector<llvm::Type*> argument_types{ int64_type };
      llvm::ArrayRef<llvm::Type*> argument_type_refs(argument_types);
      //llvm::Type* return_type = llvm::ArrayType::get(int64_type, 2);
      

      //this is a very fragile transformation, which is necessary because array<uint64_t, 2> 
      //becomes {i64, i64}
      //if ever the optimization changes, we'll be in trouble.
      std::vector<llvm::Type*> return_type_members{ int64_type, int64_type };
      llvm::ArrayRef<llvm::Type*> return_type_refs(return_type_members);
      llvm::Type* return_type = llvm::StructType::get(thread_context, return_type_refs);
      
      
      llvm::FunctionType* compile_type = llvm::FunctionType::get(return_type, argument_type_refs, 
      false);


      
      llvm::PointerType* compile_type_pointer = compile_type->getPointerTo();
      llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 
      (uint64_t)&compile_user_facing));
      llvm::Value* compile_function = Builder.CreateIntToPtr(twister_address, compile_type_pointer, \
s("convert integer address to actual function"));

      //llvm::Constant *compile_function = TheModule->getOrInsertFunction("compile_user_facing", 
      //compile_type);

      std::vector<llvm::Value*> arguments{ field_results[0].IR };
      llvm::ArrayRef<llvm::Value*> argument_refs(arguments);
      llvm::Value* result_of_compile = Builder.CreateCall(compile_function, argument_refs, 
      s("compile"));



      //this is a very fragile transformation, which is necessary because array<uint64_t, 2> 
      //becomes {i64, i64}
      //if ever the optimization changes, we'll be in trouble.
      llvm::AllocaInst* final_dynamic_pointer = create_alloca_in_entry_block(2);

      llvm::Value* part1 = 
      (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 0);
      std::vector<unsigned> zero_index{ 0 };
      llvm::ArrayRef<unsigned> zero_index_ref(zero_index);
      llvm::Value* first_return = Builder.CreateExtractValue(result_of_compile, zero_index_ref);
      Builder.CreateStore(first_return, part1);

      llvm::Value* part2 = 
      (llvm::AllocaInst*)Builder.CreateConstInBoundsGEP2_64(final_dynamic_pointer, 0, 1);
      std::vector<unsigned> one_index{ 1 };
      llvm::ArrayRef<unsigned> one_index_ref(one_index);
      llvm::Value* second_return = Builder.CreateExtractValue(result_of_compile, one_index_ref);
      Builder.CreateStore(second_return, part2);

      finish(Builder.CreateLoad(final_dynamic_pointer), T::dynamic_pointer);
    }
  case ASTn("temp_generate_AST"):
    {
      llvm::FunctionType* AST_maker_type = llvm::FunctionType::get(int64_type, false);

      llvm::PointerType* AST_maker_type_pointer = AST_maker_type->getPointerTo();
      llvm::Constant *twister_address = llvm::Constant::getIntegerValue(int64_type, llvm::APInt(64, 
      (uint64_t)&ASTmaker));
      llvm::Value* twister_function = Builder.CreateIntToPtr(twister_address, 
      AST_maker_type_pointer, s("convert integer address to actual function"));
      //llvm::Constant *twister_function = TheModule->getOrInsertFunction("ASTmaker", 
      //AST_maker_type);

      llvm::Value* result_AST = Builder.CreateCall(twister_function, s("ASTmaker"));

      finish(result_AST, T::pointer_to_AST);
    }
  }
  error("fell through switches");
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
      outstream << "Malformed AST: code " << error_code << " at AST " << compiler.error_location << \
"field " << compiler.error_field << "\n\n";
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

  test_unique_types();

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
    else if (strcmp(argv[x], "verbose") == 0) VERBOSE_DEBUG = true;
    else if (strcmp(argv[x], "optimize") == 0) OPTIMIZE = true;
    else if (strcmp(argv[x], "console") == 0) CONSOLE = true;
    else if (strcmp(argv[x], "timer") == 0) TIMER = true;
    else if (strcmp(argv[x], "quiet") == 0)
    {
      outstream.setstate(std::ios_base::failbit);
      llvm_outstream = &llvm_null_stream;
    }
    else error(string("unrecognized flag ") + argv[x]);
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
  if (TIMER)
  {
    std::clock_t start = std::clock();
    for (int x = 0; x < 40; ++x)
    {
      std::clock_t ministart = std::clock();
      fuzztester(100);
      std::cout << "Minitime: " << (std::clock() - ministart) / (double)CLOCKS_PER_SEC << '\n';
    }
    std::cout << "Overall time: " << (std::clock() - start) / (double)CLOCKS_PER_SEC << '\n';
    return 0;
  }
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
