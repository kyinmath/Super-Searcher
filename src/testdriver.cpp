#include "globalinfo.h"
#include "cs11.h"
#include "runtime.h"
#include "debugoutput.h"
#include <llvm/Support/raw_ostream.h> 


bool LIMITED_FUZZ_CHOICES = false;
bool GC_TIGHT = false;
bool INTERACTIVE = false;
bool CONSOLE = false;
bool OLD_AST_OUTPUT = false;
bool OUTPUT_MODULE = true;
bool FUZZTESTER_NO_COMPILE = false;
uint64_t runs = ~0ull;
llvm::raw_null_ostream llvm_null_stream;

std::array<uint64_t, ASTn("never reached")> hitcount;
std::vector<uint64_t> allowed_tags;
extern std::deque< uAST*> fuzztester_roots; //for use in garbage collecting. each valid AST that the fuzztester has a reference to must be kept in here
/**
The fuzztester generates random ASTs and attempts to compile them. not all randomly-generated ASTs will be well-formed.

fuzztester has a vector of pointers to working, compilable ASTs (initially just a nullptr).
it chooses a random AST tag.
then, it chooses pointers randomly from the vector of working ASTs, which go in the first few fields.
each remaining field is chosen randomly according to an exponential distribution
finally, if the created AST successfully compiles, it is added to the vector of working ASTs.

todo: this scheme can't produce forward references, which are necessary for goto. that is, a goto points to an AST that's created after it.
and, it can't produce [concatenate [int]a [load a]]. that requires speculative creation of multiple ASTs simultaneously.
*/
void fuzztester(uint64_t iterations)
{
	uint64_t max_fuzztester_size = 1;
	while (iterations)
	{
		--iterations; //this is here, instead of having "iterations--", so that integer-sanitizer doesn't complain
		fuzztester_roots.push_back(nullptr); //we this is so that we always have something to find, when we're looking for previous_ASTs
		//create a random AST
		uint64_t tag = mersenne() % ASTn("never reached");
		if (LIMITED_FUZZ_CHOICES)
			tag = allowed_tags[mersenne() % allowed_tags.size()];
		uint64_t pointer_fields = AST_descriptor[tag].pointer_fields; //how many fields will be AST pointers. they will come at the beginning
		uint64_t prev_AST = generate_exponential_dist() % fuzztester_roots.size(); //perhaps: prove that exponential_dist is desired.
		//birthday collisions is the problem. a concatenate with two branches will almost never appear, because it'll result in an active object duplication.
		//but does exponential falloff solve this problem in the way we want?

		std::vector<uAST*> fields;
		uint64_t incrementor = 0;
		for (; incrementor < pointer_fields; ++incrementor)
			fields.push_back(fuzztester_roots.at(mersenne() % fuzztester_roots.size())); //get pointers to previous ASTs
		for (; incrementor < pointer_fields + AST_descriptor[tag].additional_special_fields; ++incrementor)
		{
			if (AST_descriptor[tag].parameter_types[incrementor].type == T::dynamic_pointer)
			{
				fields.push_back((uAST*)(u::integer.ver())); //make a random integer
				fields.push_back((uAST*)new_object(generate_exponential_dist()));
			}
			else error("fuzztester doesn't know how to make this special type, so I'm going to panic");
		}
		uAST* test_AST = new_AST(tag, fuzztester_roots.at(prev_AST), fields);
		output_AST_console_version(test_AST);
		fuzztester_roots.pop_back(); //delete the null we put on the back
		finiteness = FINITENESS_LIMIT;
		if (!FUZZTESTER_NO_COMPILE)
		{
			uint64_t result[3];
			compile_returning_legitimate_object(result, (uint64_t)test_AST);
			console << "results of user compile are " << result[0] << ' ' << result[1] << ' ' << result[2] << '\n';
			auto func = (function*)result[0];
			if (result[1] == 0)
			{
				fuzztester_roots.push_back((uAST*)func->the_AST);
				if (fuzztester_roots.size() > max_fuzztester_size)
					fuzztester_roots.pop_front();


				if (DONT_ADD_MODULE_TO_ORC || DELETE_MODULE_IMMEDIATELY)
					continue;

				std::array<uint64_t, 2> dynamic_result = run_null_parameter_function(result[0]);
				uint64_t size_of_return = get_size((Tptr)dynamic_result[0]);
				output_array((uint64_t*)dynamic_result[1], size_of_return);
				//theoretically, this action is disallowed. these ASTs are pointing to already-immuted ASTs, which can't happen. however, it's safe as long as we isolate these ASTs from the user
				++hitcount[tag];
			}
		}
		else
		{
			//don't bother compiling. just shove everything in there.
			fuzztester_roots.push_back(test_AST);
			if (fuzztester_roots.size() > max_fuzztester_size)
				fuzztester_roots.pop_front();
			console << fuzztester_roots.size() - 1 << "\n";
			++hitcount[tag];
		}
		//else delete test_AST;
		if (INTERACTIVE)
		{
			console << "Press enter to continue\n";
			std::cin.get();
		}
		console << "\n";
		if ((generate_random() % (GC_TIGHT ? 1 : 30)) == 0)
			start_GC();
	}
	fuzztester_roots.clear();
}


bool READER_VERBOSE_DEBUG = false;

/*
this will never, ever do anything useful. NDEBUG is manipulated like a bitch, I can't even set it by passing in -DNDEBUG to the compiler. don't bother using NDEBUG or assert() for anything.
#ifdef NDEBUG
panic time! refuse to compile.
except, I don't know why this won't ever trigger
#endif
*/

#ifndef NO_CONSOLE
#include <iostream>
#include <sstream>
#include <istream>
//parses console input and generates ASTs.
using std::string; //this can't go inside source_reader for stupid C++ reasons
class source_reader
{
	std::unordered_map<string, uAST*> ASTmap = {{"0", nullptr}}; //from name to location. starts with 0 = nullptr.
	std::unordered_map<uAST**, string> goto_delay; //deferred binding of addresses, for goto. we can only see the labels at the end.
	uAST* delayed_binding_special_value = (uAST*)1; //because of alignment, pointers to uAST must be multiples of 8
	string delayed_binding_name; //a backdoor return value.
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
			while (next_char != '[' && next_char != ']' && next_char != '{' && next_char != '}' && next_char != '\n' && next_char != ' ')
			{
				word.push_back(next_char);
				input.ignore(1);
				next_char = input.peek();
			}
			//console << "word is " << word << "|||\n";
			return word;
		}
	}

	//continued_string is in case we already read the first token. then, that's the first thing to start with.
	uAST* read_single_AST(uAST* previous_AST, string continued_string = "")
	{
		string first = continued_string == "" ? get_token() : continued_string;
		if (first != string(1, '[')) //it's a name reference.
		{
			//console << "first was " << first << '\n';
			auto AST_search = ASTmap.find(first);
			//check(AST_search != ASTmap.end(), string("variable name not found: ") + first);
			if (AST_search == ASTmap.end())
			{
				delayed_binding_name = first;
				return delayed_binding_special_value;
			}
			return AST_search->second;
		}

		string tag_str = get_token();
		uint64_t AST_type = ASTn(tag_str.c_str());

		std::vector<uAST*> dummy_uASTs(get_size(get_AST_fields_type(AST_type)), nullptr);
		uAST* new_type_location = new_AST(AST_type, previous_AST, dummy_uASTs); //we have to make it now, so that we know where the AST will be. this lets us specify where our reference must be resolved.


		if (READER_VERBOSE_DEBUG) console << "AST tag was " << AST_type << "\n";
		uint64_t pointer_fields = AST_descriptor[AST_type].pointer_fields;


		//field_num is which field we're on
		uint64_t field_num = 0;
		for (string next_token = get_token(); next_token != "]"; next_token = get_token(), ++field_num)
		{
			check(field_num < max_fields_in_AST, "more fields than possible");
			if (field_num < pointer_fields) //it's a pointer!
			{
				if (next_token == "{") new_type_location->fields[field_num] = create_single_basic_block(true);
				else new_type_location->fields[field_num] = read_single_AST(nullptr, next_token);
				if (new_type_location->fields[field_num].ptr == delayed_binding_special_value)
				{
					goto_delay.insert(make_pair(&((uAST*)(new_type_location))->fields[field_num].ptr, delayed_binding_name));
				}
			}
			else //it's a static object, "imv". make an integer
			{
				if (AST_type == 0 && field_num == 0)
				{
					bool isNumber = true;
					for (auto& k : next_token)
						isNumber = isNumber && isdigit(k);
					check(isNumber, string("tried to input non-number ") + next_token);
					check(next_token.size(), "token is empty, probably a missing ]");
					new_type_location->fields[field_num] = (uint64_t)u::integer;
					new_type_location->fields[field_num + 1] = (uint64_t)new_object(std::stoull(next_token));
				}
				else error("trying to write too many fields");
			}
		}
		//check(next_token == "]", "failed to have ]");

		if (READER_VERBOSE_DEBUG)
			output_AST_console_version(new_type_location);

		//get an AST name if any.
		if (input.peek() != ' ' && input.peek() != '\n' && input.peek() != ']' && input.peek() != '[' && input.peek() != '{' && input.peek() != '}')
		{
			string thisASTname = get_token();
			//seems that once we consume the last '\n', the cin stream starts blocking again.
			if (!thisASTname.empty())
			{
				check(ASTmap.find(thisASTname) == ASTmap.end(), string("duplicated variable name: ") + thisASTname);
				ASTmap.insert(std::make_pair(thisASTname, new_type_location));
			}
		}

		if (READER_VERBOSE_DEBUG)
		{
			console << "next char is" << (char)input.peek() << input.peek();
			console << '\n';
		}
		return new_type_location;
	}

	uAST* create_single_basic_block(bool create_brace_at_end = false)
	{
		uAST* current_previous = nullptr;
		if (create_brace_at_end == false)
		{
			if (input.peek() == '\n') input.ignore(1);
			for (string next_word = get_token(); next_word != ""; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference: " + next_word); //note that forward references in basic blocks are not allowed
			}
			return current_previous; //which is the very last.
		}
		else
		{
			for (string next_word = get_token(); next_word != "}"; next_word = get_token())
			{
				current_previous = read_single_AST(current_previous, next_word);
				check(current_previous != delayed_binding_special_value, "failed to resolve reference: " + next_word);
			}
			return current_previous; //which is the very last
		}
	}

public:
	source_reader(std::istream& stream, char end) : input(stream) {} //char end must be '\n', because the char needs to go in a switch case
	uAST* read()
	{
		uAST* end = create_single_basic_block();

		//resolve all the forward references from goto()
		for (auto& x : goto_delay)
		{
			auto AST_search = ASTmap.find(x.second);
			check(AST_search != ASTmap.end(), string("variable name not found: ") + x.second);
			*(x.first) = AST_search->second;
		}
		return end;
	}
};

//no StringRef because stringstreams can't take it
std::array<uint64_t, 2> compile_string(std::string input_string)
{
	std::stringstream div_test_stream;
	div_test_stream << input_string << '\n';
	if (VERBOSE_DEBUG) console << input_string << '\n';
	source_reader k(div_test_stream, '\n');
	uAST* end = k.read();
	check(end != nullptr, "failed to make AST");
	finiteness = FINITENESS_LIMIT;
	uint64_t compile_result[3];
	compile_returning_legitimate_object(compile_result, (uint64_t)end);
	check(compile_result[1] == 0, string("failed to compile, error code ") + std::to_string(compile_result[1]));
	return run_null_parameter_function(compile_result[0]); //even if it's 0, it's fine.
}

void compile_verify_string(std::string input_string, Tptr type, uint64_t value)
{
	auto k = compile_string(input_string);
	check((Tptr)k[0] == type, "test failed type");
	check(*(uint64_t*)k[1] == value, "test failed value, got " + std::to_string(*(uint64_t*)k[1]));
}
void compile_verify_string_nonzero_value(std::string input_string, Tptr type)
{
	auto k = compile_string(input_string);
	check((Tptr)k[0] == type, "test failed type");
	check(*(uint64_t*)k[1] != 0, "test failed value, got " + std::to_string(*(uint64_t*)k[1]));
}

void compile_verify_string(std::string input_string, Tptr type)
{
	auto k = compile_string(input_string);
	check((Tptr)k[0] == type, "test failed type");
}

//functions passed into here are required to fail compilation.
void cannot_compile_string(std::string input_string)
{
	std::stringstream div_test_stream;
	div_test_stream << input_string << '\n';
	if (VERBOSE_DEBUG) console << input_string << '\n';
	source_reader k(div_test_stream, '\n');
	uAST* end = k.read();
	check(end != nullptr, "failed to make AST");
	finiteness = FINITENESS_LIMIT;
	uint64_t compile_result[3];
	compile_returning_legitimate_object(compile_result, (uint64_t)end);
	check(compile_result[1] != 0, "compile succeeded when it shouldn't have");
}

void test_suite()
{
	//try moving the type check to the back as well.
	Tptr unique_zero = new_type(Typen("integer"), {});
	check(unique_zero == new_type(Typen("integer"), {}), "duplicated type doesn't even unique");
	Tptr pointer_zero = new_type(Typen("pointer"), unique_zero);
	check(pointer_zero == new_type(Typen("pointer"), unique_zero), "pointers don't unique");
	check(pointer_zero != unique_zero, "pointers uniqueing to integers");
	Tptr unique_pointer_dynamic = new_type(Typen("dynamic pointer"), {});
	check(pointer_zero != unique_pointer_dynamic, "different pointers unique");

	check(u::integer == get_unique_type(u::integer, false), "u::types aren't unique");
	check(u::integer == get_unique_type(T::integer, false), "u::types don't come from T::types");

	//random value. then check that (x / k) * k + x % k == x
	compile_verify_string("[system1 [imv 2]]a [subtract [add [multiply [udiv a [imv 4]] [imv 4]] [urem a [imv 4]]] a]", u::integer, 0);
	//looping until finiteness ends, increasing a value. tests storing values
	compile_verify_string("[imv 0]b [label]a [store [pointer b] [increment b]] [goto a] [concatenate b]", u::integer, FINITENESS_LIMIT);


	//loading from dynamic objects. a single-object dynamic pointer, pointing to an int.
	compile_verify_string("[dynamify]empty [imv 0]ret [imv 40]a [dyn_subobj [dynamify [pointer a]]dyn [imv 0] [store [pointer ret] subobj] [store [pointer empty] subobj]]subobj [concatenate ret]", u::integer, 40);
	//try to load the next object. it should return nothing.
	compile_verify_string("[dynamify]empty [imv 0]ret [imv 40]a [dyn_subobj [dynamify [pointer a]]dyn [imv 0] [store [pointer ret] subobj] [store [pointer empty] subobj]]subobj [concatenate empty]", u::dynamic_pointer, 0);

	//again, try to load from dynamic objects. this time, a concatenation of two objects. load the int from the concatenation, then write it.
	compile_verify_string("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [pointer ret]]a [dyn_subobj [dynamify [pointer a]]dyn [imv 0] [store [pointer ret] subobj] [label] [label] [label] [label] [store [pointer empty] subobj]]subobj [concatenate ret]", u::integer, 40);
	//load the pointer from the concatenation
	compile_verify_string_nonzero_value("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [pointer ret]]a [dyn_subobj [dynamify [pointer a]]dyn [imv 1] [store [pointer ret] subobj] [label] [label] [label] [label] [store [pointer empty] subobj]]subobj [concatenate empty]", u::dynamic_pointer);
	//load the dynamic pointer from the concatenation
	compile_verify_string_nonzero_value("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [dynamify [pointer ret]]]a [dyn_subobj [dynamify [pointer a]]dyn [imv 1] [store [pointer ret] subobj] [store [pointer empty] subobj]]subobj [concatenate empty]", u::dynamic_pointer);

	//loading a subobject from a concatenation, as well as copying across fields of a concatenation
	compile_verify_string("[concatenate [imv 20]a [increment a]]co [load_subobj [pointer co] [imv 1]]", u::integer, 20 + 1);

	//goto forward. should skip the second store, and produce 20.
	compile_verify_string("[imv 0]b [label {[store [pointer b] [imv 20]] [goto a] [store [pointer b] [imv 40]]}]a [concatenate b]", u::integer, 20);

	//clear old unused stack allocas
	compile_string("[label {[imv 40]a [label]}] [label {a [label]}]");
	//[imv 40]a is preserved across both fields of the concatenate, but it is copied
	compile_string("[concatenate {[imv 40]a [label]} {a [label]}]");
	//can't get a pointer to ant, which has stack_degree == 2
	cannot_compile_string("[concatenate [imv 40]ant [pointer ant]]");


	compile_string("[run_function [compile [convert_to_AST [system1 [imv 2]] [label] {[imv 0]v [dynamify [pointer v]]}]]]");
	compile_string("[run_function [compile [convert_to_AST [imv 1] [label] [dynamify]]]]"); //produces 0 inside the dynamic pointer, using the zero AST.

	//debugtypecheck(T::does_not_return); stopped working after type changes to bake in tags into the pointer. this is useless anyway, in a unity build.
}
#endif

int main(int argc, char* argv[])
{
	//tells LLVM the generated code should be for the native platform
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	std::unique_ptr<llvm::TargetMachine> TM_backer(llvm::EngineBuilder().selectTarget());
	TM = TM_backer.get();
	KaleidoscopeJIT c_holder(TM); //purpose is to make valgrind happy by deleting the compiler_host at the end of execution. however, later we'll need to move this into each thread.
	c = &c_holder;

	bool BENCHMARK = false;
	for (int x = 1; x < argc; ++x)
	{
		if (strcmp(argv[x], "interactive") == 0) INTERACTIVE = true;
		else if (strcmp(argv[x], "verbose") == 0) VERBOSE_DEBUG = true;
		else if (strcmp(argv[x], "optimize") == 0) OPTIMIZE = true;
		else if (strcmp(argv[x], "console") == 0) CONSOLE = true;
		else if (strcmp(argv[x], "timer") == 0)
		{
			runs = 10000;
		}
		else if (strcmp(argv[x], "longrun") == 0)
		{
			bool isNumber = true;
			string next_token = argv[++x];
			for (auto& k : next_token)
				isNumber = isNumber && isdigit(k);
			check(isNumber, string("tried to input non-number ") + next_token);
			check(next_token.size(), "no digits in the number");
			runs = std::stoull(next_token);
		}
		else if (strcmp(argv[x], "oldoutput") == 0) OLD_AST_OUTPUT = true;
		else if (strcmp(argv[x], "fuzznocompile") == 0) FUZZTESTER_NO_COMPILE = true;
		else if (strcmp(argv[x], "noaddmodule") == 0) DONT_ADD_MODULE_TO_ORC = true;
		else if (strcmp(argv[x], "deletemodule") == 0) DELETE_MODULE_IMMEDIATELY = true;
		else if (strcmp(argv[x], "truefuzz") == 0)
		{
			DEBUG_GC = true;
			OUTPUT_MODULE = false;
		}
		else if (strcmp(argv[x], "benchmark") == 0)
		{
			runs = 10000;
			DEBUG_GC = false;
			console.setstate(std::ios_base::failbit);
			console.rdbuf(nullptr);
			llvm_console = &llvm_null_stream;
			BENCHMARK = true;
		}
		else if (strcmp(argv[x], "limited") == 0) //write "limited label", where "label" is the AST tag you want. you can have multiple tags like "limited label limited random", putting "limited" before each one.
		{
			LIMITED_FUZZ_CHOICES = true;
			runs = 500;
			allowed_tags.push_back(ASTn(argv[++x]));
		}
		else if (strcmp(argv[x], "gctight") == 0) GC_TIGHT = true;
		else if (strcmp(argv[x], "quiet") == 0)
		{
			console.setstate(std::ios_base::failbit);
			console.rdbuf(nullptr);
			llvm_console = &llvm_null_stream;
		}
		else error(string("unrecognized flag ") + argv[x]);
	}

#ifndef NO_CONSOLE
	if (!BENCHMARK)
	{
		test_suite();
	}
#endif

	struct cleanup_at_end
	{
		~cleanup_at_end() {
			start_GC(); //this cleanup is to let Valgrind know that we've legitimately taken care of all memory.

			uint64_t total_successful_compiles = 0;
			for (uint64_t x = 0; x < ASTn("never reached"); ++x)
			{
				total_successful_compiles += hitcount[x];
				std::cout << "tag " << x << " " << AST_descriptor[x].name << ' ' << hitcount[x] << '\n';
			}
			std::cout << "success rate " << (float)total_successful_compiles/runs << '\n';
		}
	} a;

#ifndef NO_CONSOLE
	if (CONSOLE)
	{
		while (1)
		{
			source_reader k(std::cin, '\n'); //have to reinitialize to clear the ASTmap
			console << "Input AST:\n";
			uAST* end = k.read();
			if (READER_VERBOSE_DEBUG) console << "Done reading.\n";
			if (end == nullptr)
			{
				console << "it's nullptr\n";
				std::cin.get();
				continue;
			}
			output_AST_and_previous(end);
			finiteness = FINITENESS_LIMIT;
			uint64_t compile_result[3];
			compile_returning_legitimate_object(compile_result, (uint64_t)end);
			auto run_result = run_null_parameter_function(compile_result[0]); //even if it's 0, it's fine.
			if (compile_result[1] != 0)
				std::cout << "wrong! error code " << compile_result[1] << "\n";
			else
				output_array((uint64_t*)run_result[1], get_size((Tptr)run_result[0]));
		}
	}
#endif

	std::clock_t start = std::clock();
	fuzztester(runs);
	std::cout << runs << " time: " << (std::clock() - start) / (double)CLOCKS_PER_SEC << '\n';
	return 0;
	//default mode
}