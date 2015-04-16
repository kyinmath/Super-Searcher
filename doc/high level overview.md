This program (backend) takes in ASTs, converts them into LLVM IR, and then runs them. The AST language will be mostly C-like. For any C code that doesn't use "advanced" things like the C standard library or "restrict" pointers, it should be possible to transform it pretty cleanly into ASTs that this program likes. For example, suppose we have the following C-like code:
```
struct my_type
{
	mutex the_mutex;
	vector<int> numbers;
} global_object;
```
This shows globals, structs, vectors, and locks. The my_type structure would become the type object ["concatenate", ["mutex"], ["int"]]. And consider:
```
void bar(int k)
{
beginning:
	if (++k > 4)
		print(rand() * (int)&k);
	else goto beginning;
	return;
}
```
This shows functions, pointers, and control flow. print(rand() * k) would become ["print", ["multiply", ["rand"], ["pointer", k]]]. Each individual construct cleanly converts into an AST. In reality, there would be some extra nullptrs, but we omit them here for clarity. ASTs can already be created very nicely using the AST constructors, but I might end up implementing this anyway since it will allow for interactive compilation. Other planned features include safe concurrency, modification and re-compilation of functions in real time, 

The design of the ASTs will emphasize readability and potential for self-modification. The backend will enforce type and memory safety. LLVM's JIT will be doing all the heavy lifting to assemble its IR into executable code. The purpose of this backend is to make it easy for code to be self-modifying. By avoiding a textual representation, we ensure that self-modifying code won't have to lex or parse anything. In addition, memory safety means that the program won't cause too much damage even if it goes haywire, so the backend is robust against adversarial requests.


description of language overall, as desired
100 char line width
