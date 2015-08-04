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
bool READER_VERBOSE_DEBUG = false;
bool QUIET = false;
uint64_t runs = ~0ull;
llvm::raw_null_ostream llvm_null_stream;

std::array<uint64_t, ASTn("never reached")> hitcount;
std::vector<uint64_t> allowed_tags;
//each of these is a basicblock AST.
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
	uint64_t max_fuzztester_size = 3;
	//std::vector<uAST*> loose_ASTs;
	while (iterations)
	{
		--iterations; //this is here, instead of having "iterations--", so that integer-sanitizer doesn't complain
		event_roots.push_back(nullptr); //we this is so that we always have something to find, when we're looking for previous ASTs
		//create a random AST
		uint64_t tag = mersenne() % ASTn("never reached");
		if (LIMITED_FUZZ_CHOICES) tag = allowed_tags[mersenne() % allowed_tags.size()];
		uint64_t prev_number = generate_random() % event_roots.size(); //perhaps: prove that exponential_dist is desired.

		function* previous_func = event_roots.at(prev_number);
		uAST* previous = previous_func ? previous_func->the_AST : 0; //assume it's a basic block?
		uAST* test_AST;
		if (tag == ASTn("basicblock")) //simply concatenate two previous basic blocks.
		{
			function* second_prev_func = event_roots.at(mersenne() % event_roots.size());
			test_AST = new_AST(tag, {previous, second_prev_func ? second_prev_func->the_AST : 0});
		}
		else
		{
			uAST* new_random_AST;
			if (tag == ASTn("imv"))
			{
				//make a random integer
				new_random_AST = new_AST(tag, (uAST*)new_object_value(u::integer.ver(), generate_exponential_dist()));
			}
			else
			{
				std::vector<uAST*> fields;
				for (uint64_t incrementor = 0; incrementor < AST_descriptor[tag].pointer_fields; ++incrementor)
				{
					function* second_prev_func = event_roots.at(mersenne() % event_roots.size());
					fields.push_back(second_prev_func ? second_prev_func->the_AST : 0); //get pointers to previous ASTs
				}
				new_random_AST = new_AST(tag, fields);
			}
			if (previous)
			{
				test_AST = copy_AST(previous);
				auto k = (svector*)test_AST->fields[0];
				pushback_int(k, (uint64_t)new_random_AST);
			}
			else
			{
				test_AST = new_AST(ASTn("basicblock"), new_random_AST);
			}
		}

		output_AST_console_version(test_AST);
		event_roots.pop_back(); //delete the null we put on the back
		finiteness = FINITENESS_LIMIT;

		uint64_t result[3];
		compile_returning_legitimate_object(result, (uint64_t)test_AST);
		print("results of user compile are ", result[0], ' ', result[1], ' ', result[2], '\n');
		auto func = (function*)result[0];
		if (result[1] == 0)
		{
			if (event_roots.size() > max_fuzztester_size)
				event_roots[generate_random() % event_roots.size()] = func;
			else event_roots.push_back(func);


			if (DONT_ADD_MODULE_TO_ORC || DELETE_MODULE_IMMEDIATELY)
				continue;

			dynobj* dynamic_result = run_null_parameter_function((function*)result[0]);
			uint64_t size_of_return = dynamic_result ? get_size(dynamic_result->type) : 0;
			if (size_of_return) output_array(&(*dynamic_result)[0], size_of_return);
			//theoretically, this action is disallowed. these ASTs are pointing to already-immuted ASTs, which can't happen. however, it's safe as long as we isolate these ASTs from the user
			++hitcount[tag];
		}
		//else delete test_AST;
		if (INTERACTIVE)
		{
			print("Press enter to continue\n");
			std::cin.get();
		}
		print("\n");
		if ((generate_random() % (GC_TIGHT ? 1 : 30)) == 0)
			start_GC();
	}
	event_roots.clear();
}




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
			//print("word is ", word, "|||\n");
			return word;
		}
	}

	//continued_string is in case we already read the first token. then, that's the first thing to start with.
	uAST* read_single_AST(string continued_string = "")
	{
		string first = continued_string == "" ? get_token() : continued_string;

		string thisASTname = "";

		//it's an AST name. variable declarations start with a _. they come before the AST, because goto needs to see previously created labels, and BBs relocate.
		//we rely on short circuit evaluation to not access the 0th element of an empty string.
		if ((first.size() > 0) && (first.at(0) == '_'))
		{
			//print("first before ignore and get_token is ", first, "\n");
			thisASTname = first.substr(1, string::npos);
			//print("first is ", first, "\n");
			//print("thisASTname is ", thisASTname, "\n");
			if (!thisASTname.empty())
			{
				check(ASTmap.find(thisASTname) == ASTmap.end(), string("duplicated variable name: ") + thisASTname);
			}
			first = get_token(); //re-get the token. it should be [ or {, since we've processed the name.
		}
		if (first != string(1, '[') && first != string(1, '{')) //it's a name reference or a new variable.
		{
			//print("first was ", first, '\n');
			auto AST_search = ASTmap.find(first);
			check(AST_search != ASTmap.end(), string("variable name not found: ") + first);
			return AST_search->second;
		}
		if (first == string(1, '{'))
			return create_single_basic_block(true);

		string tag_str = get_token();
		uint64_t AST_type = ASTn(tag_str.c_str());

		std::vector<uAST*> dummy_uASTs(get_size(get_AST_fields_type(AST_type)), nullptr);
		uAST* new_type_location = new_AST(AST_type, dummy_uASTs); //we have to make it now, so that we know where the AST will be. this lets us specify where our reference must be resolved.

		if (thisASTname.compare("") != 0) ASTmap.insert(std::make_pair(thisASTname, new_type_location));

		if (READER_VERBOSE_DEBUG) print("AST tag was ", AST_type, "\n");


		//field_num is which field we're on
		uint64_t field_num = 0;
		string next_token = get_token();
		for (; next_token != "]"; next_token = get_token(), ++field_num)
		{
			check(field_num < max_fields_in_AST, "more fields than possible");
			if (field_num < AST_descriptor[AST_type].pointer_fields) //it's a pointer!
			{
				if (next_token == "{") new_type_location->fields[field_num] = create_single_basic_block(true);
				else new_type_location->fields[field_num] = read_single_AST(next_token);
			}
			else //it's a static object, "imv", or "basicblock". make an integer
			{
				if (AST_type == ASTn("imv"))
				{
					bool isNumber = true;
					for (auto& k : next_token)
						isNumber = isNumber && isdigit(k);
					check(isNumber, string("tried to input non-number ") + next_token);
					check(next_token.size(), "token is empty, probably a missing ]");
					new_type_location->fields[field_num] = (uAST*)new_object_value(u::integer, std::stoull(next_token));
				}
				else if (AST_type == ASTn("basicblock"))
				{
					svector* k = (svector*)new_type_location->fields[0];
					if (next_token == "{") pushback_int(k, (uint64_t)create_single_basic_block(true));
					else pushback_int(k, (uint64_t)read_single_AST(next_token));
				}
				else error("trying to write too many fields");
			}
		}
		check(next_token == "]", "failed to have ]");

		if (READER_VERBOSE_DEBUG)
			output_AST_console_version(new_type_location);


		if (READER_VERBOSE_DEBUG)
		{
			print("next char is (", (char)input.peek(), input.peek(), ")\n");
		}

		return new_type_location;
	}

	//if it's reading a {}, the { has already been consumed.
	uAST* create_single_basic_block(bool create_brace_at_end = false)
	{
		uAST* BB_AST = new_AST(ASTn("basicblock"), {});
		svector*& k = (svector*&)BB_AST->fields[0];
		string last_char;
		if (create_brace_at_end == false)
		{
			if (input.peek() == '\n') input.ignore(1);
			last_char = "";
		}
		else last_char = "}";
		for (string next_word = get_token(); next_word != last_char; next_word = get_token())
		{
			uAST* single_AST = read_single_AST(next_word);
			pushback_int(k, (uint64_t)single_AST);
		}
		return BB_AST; //which is the very last.
	}

public:
	source_reader(std::istream& stream, char end) : input(stream) {} //char end must be '\n', because the char needs to go in a switch case
	uAST* read()
	{
		uAST* end = create_single_basic_block();
		if (READER_VERBOSE_DEBUG)
		{
			print("final read AST is ");
			output_AST_console_version(end);
			pfAST(end);
			print('\n');
		}
		return end;
	}
};

//no StringRef because stringstreams can't take it
dynobj* compile_string(std::string input_string)
{
	std::stringstream div_test_stream;
	div_test_stream << input_string << '\n';
	if (VERBOSE_DEBUG) print(input_string, '\n');
	source_reader k(div_test_stream, '\n');
	uAST* end = k.read();
	check(end != nullptr, "failed to make AST");
	finiteness = FINITENESS_LIMIT;
	uint64_t compile_result[3];
	compile_returning_legitimate_object(compile_result, (uint64_t)end);
	check(compile_result[1] == 0, string("failed to compile, error code ") + std::to_string(compile_result[1]));
	return run_null_parameter_function((function*)compile_result[0]); //even if it's 0, it's fine.
}

void compile_verify_string(std::string input_string, Tptr type, uint64_t value)
{
	dynobj* k = compile_string(input_string);
	check(k->type == type, "test failed type, got " + std::to_string(k->type) + " wanted " + std::to_string(type));
	check((*k)[0] == value, "test failed value, got " + std::to_string((*k)[0]) + " wanted " + std::to_string(value));
}
void compile_verify_string_nonzero_value(std::string input_string, Tptr type)
{
	auto k = compile_string(input_string);
	check(k->type == type, "test failed type, got " + std::to_string(k->type) + " wanted " + std::to_string(type));
	check((*k)[0] != 0, "test failed value, got " + std::to_string((*k)[0]) + " wanted nonzero");
}

void compile_verify_string(std::string input_string, Tptr type)
{
	auto k = compile_string(input_string);
	if (type == 0) check(k == 0, "test failed type, got nonzero");
	check(k->type == type, "test failed type, got " + std::to_string(k->type) + " wanted " + std::to_string(type));
}

//functions passed into here are required to fail compilation.
void cannot_compile_string(std::string input_string)
{
	std::stringstream div_test_stream;
	div_test_stream << input_string << '\n';
	if (VERBOSE_DEBUG) print(input_string, '\n');
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
	Tptr unique_zero = new_unique_type(Typen("integer"), {});
	check(unique_zero == new_unique_type(Typen("integer"), {}), "duplicated type doesn't even unique");
	Tptr pointer_zero = new_unique_type(Typen("pointer"), unique_zero);
	check(pointer_zero == new_unique_type(Typen("pointer"), unique_zero), "pointers don't unique");
	check(pointer_zero != unique_zero, "pointers uniqueing to integers");
	Tptr unique_pointer_dynamic = new_unique_type(Typen("dynamic object"), {});
	check(pointer_zero != unique_pointer_dynamic, "different pointers unique");

	check(u::integer == uniquefy_premade_type(u::integer, false), "u::types aren't unique");
	check(u::integer == uniquefy_premade_type(T::integer, false), "u::types don't come from T::types");
	
	//basics. console reading, function return types, basic blocks, referencing copies, etc.
	compile_verify_string("[zero]", u::integer, 0);
	start_GC();
	compile_verify_string("[imv 1]", u::integer, 1);
	compile_string("[concatenate [imv 100] [imv 200]]");
	start_GC();
	compile_verify_string("_a[imv 100] [imv 200]", u::integer, 200);
	compile_verify_string("_a[imv 100] [imv 200] a", u::integer, 100);
	start_GC();

	//random value. then check that (x / k) * k + x % k == x
	compile_verify_string("_a[system1 [imv 2]] [subtract [add [multiply [udiv a [imv 4]] [imv 4]] [urem a [imv 4]]] a]", u::integer, 0);
	//looping until finiteness ends, increasing a value. tests storing values
	compile_verify_string("_b[imv 0] _a[label] [store b [increment b]] [goto a] [concatenate b]", u::integer, FINITENESS_LIMIT);

	//vector creation and pushback
	compile_string("_vec[nvec [typeof [zero]]] [vector_push vec [imv 40]] [concatenate vec]");

	//loading from dynamic objects. a single-object dynamic object, pointing to an int.
	compile_verify_string("_empty[dynamify] _ret[imv 0] _a[imv 40] _subobj[dyn_subobj _dyn[dynamify [imv 40]] [imv 0] [label] [store ret subobj] [store empty subobj]] [concatenate ret]", u::integer, 40);
	//try to load the next object. it should go through the failure branch.
	compile_verify_string("_empty[dynamify] _ret[imv 0] _a[imv 40] _subobj[dyn_subobj _dyn[dynamify [imv 40]] [imv 1] [store ret [imv 4]] [store ret subobj] [store empty subobj]] [concatenate ret]", u::integer, 4);

	//future: implement the "pointer to something", then test it here.

	//again, try to load from dynamic objects. this time, a concatenation of two objects. load the int from the concatenation, then write it.
	compile_verify_string("_empty[dynamify] _ret[imv 0] _subobj[dyn_subobj _dyn[dynamify [concatenate _in[imv 40] [pointer ret]]] [imv 0] [label] [store ret subobj]] [concatenate ret]", u::integer, 40);
	//load the pointer from the concatenation
	//compile_verify_string_nonzero_value("[dynamify]empty [imv 0]ret [dyn_subobj [dynamify [concatenate [imv 40]in [imv 40]]]dyn [imv 1] [label] [store ret subobj]]subobj [concatenate empty]", u::dynamic_object);
	//load the dynamic object from the concatenation
	compile_verify_string_nonzero_value("_empty[dynamify] _ret[imv 0] _subobj[dyn_subobj [dynamify [concatenate _in[imv 40] _dyn[dynamify [zero]]]] [imv 1] [label] [store ret subobj] [store empty subobj]] [concatenate empty]", u::dynamic_object);
	/*
	//again, try to load from dynamic objects. this time, a concatenation of two objects. load the int from the concatenation, then write it.
	compile_verify_string("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [pointer ret]]a [dyn_subobj [dynamify [imv 40]]dyn [imv 0] [store ret subobj] [label] [label] [label] [label] [store empty subobj]]subobj [concatenate ret]", u::integer, 40);
	//load the pointer from the concatenation
	compile_verify_string_nonzero_value("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [imv 40]]a [dyn_subobj [dynamify [imv 40]]dyn [imv 1] [store ret subobj] [label] [label] [label] [label] [store empty subobj]]subobj [concatenate empty]", u::dynamic_object);
	//load the dynamic object from the concatenation
	compile_verify_string_nonzero_value("[dynamify]empty [imv 0]ret [concatenate [imv 40]in [dynamify [imv 40]]]a [dyn_subobj [dynamify [imv 40]]dyn [imv 1] [store ret subobj] [store empty subobj]]subobj [concatenate empty]", u::dynamic_object);*/

	//loading a subobject from a concatenation, as well as copying across fields of a concatenation
	compile_verify_string("_co[concatenate _s[imv 20] [increment s]] [load_subobj [pointer co] [imv 1]]", u::integer, 20 + 1);

	//goto forward. should skip the second store, and produce 20.
	compile_verify_string("_b[imv 0] _a[label {[store b [imv 20]] [goto a] [store b [imv 40]]}] [concatenate b]", u::integer, 20);

	//basic testing of if statement
	compile_verify_string("_k[zero] _b[imv 1] [if k k b]", u::integer, 1);

	//clear old unused stack allocas
	compile_string("[label {_a[imv 40] [label]}] [label {a [label]}]");
	//[imv 40]a is preserved across both fields of the concatenate, but it is copied
	compile_string("[concatenate {_a[imv 40] [label]} {a [label]}]");
	//can't get a pointer to ant, which has stack_degree == 2
	cannot_compile_string("[concatenate _ant[imv 40] [pointer ant]]");
	//can't get a reference to ant either.
	cannot_compile_string("[concatenate _ant[imv 40] [store ant [imv 40]]]");


	//compile_string("[run_function [compile [convert_to_AST [system1 [imv 2]] [label] {[imv 0]v [dynamify [pointer v]]}]]]");
	//compile_string("[run_function [compile [convert_to_AST [imv 1] [label] [dynamify]]]]"); //produces 0 inside the dynamic object, using the zero AST.
	//future: implement vectors, then test them here

	//debugtypecheck(T::does_not_return); stopped working after type changes to bake in tags into the pointer. this is useless anyway, in a unity build.
}
#endif

void initialize()
{
	type_roots.push_back(u::vector_of_ASTs);
}

int main(int argc, char* argv[])
{
	//tells LLVM the generated code should be for the native platform
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();
	initialize();

	std::unique_ptr<llvm::TargetMachine> TM_backer(llvm::EngineBuilder().selectTarget());
	TM = TM_backer.get();
	KaleidoscopeJIT c_holder(TM); //purpose is to make valgrind happy by deleting the compiler_host at the end of execution
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
		else if (strcmp(argv[x], "noaddmodule") == 0) DONT_ADD_MODULE_TO_ORC = true;
		else if (strcmp(argv[x], "deletemodule") == 0) DELETE_MODULE_IMMEDIATELY = true;
		else if (strcmp(argv[x], "truefuzz") == 0)
		{
			OUTPUT_MODULE = false;
		}
		else if (strcmp(argv[x], "benchmark") == 0)
		{
			runs = 10000;
			QUIET = true;
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
			QUIET = true;
			llvm_console = &llvm_null_stream;
		}
		else error(string("unrecognized flag ") + argv[x]);
	}

#ifndef NOCHECK
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
			print("Input AST:\n");
			uAST* end = k.read();
			if (READER_VERBOSE_DEBUG) print("Done reading.\n");
			if (end == nullptr)
			{
				print("it's nullptr\n");
				std::cin.get();
				continue;
			}
			pfAST(end);
			finiteness = FINITENESS_LIMIT;
			uint64_t compile_result[3];
			compile_returning_legitimate_object(compile_result, (uint64_t)end);
			dynobj* run_result = run_null_parameter_function((function*)compile_result[0]); //even if it's 0, it's fine.
			if (compile_result[1] != 0)
				std::cout << "wrong! error code " << compile_result[1] << "\n";
			else if (run_result) output_array(&(*run_result)[0], get_size(run_result->type));

			if ((generate_random() % (GC_TIGHT ? 1 : 30)) == 0)
				start_GC();
		}
	}
#endif

	std::clock_t start = std::clock();
	fuzztester(runs);
	std::cout << runs << " time: " << (std::clock() - start) / (double)CLOCKS_PER_SEC << '\n';
	return 0;
	//default mode
}