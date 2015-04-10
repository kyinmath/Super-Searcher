#CS11 backend

This program (backend) takes in ASTs, converts them into LLVM IR, and then runs them. The AST language will be mostly C-like, supporting things like stacks, functions, structures, destructors, and control flow. The design of the ASTs will emphasize readability and potential for self-modification. The backend will enforce type and memory safety. LLVM's JIT will be doing all the heavy lifting to assemble its IR into executable code.


###To compile in Ubuntu:

Append this to /etc/apt/sources.list :
```
deb http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
deb-src http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
```

then in console, run
```
sudo apt-get install clang-3.6 llvm-3.6
```

If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev.

To compile, run
```
clang++-3.6 -g cs11.cpp `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z
```
or if your path is clang++ instead of clang++-3.6, run
```
clang++ -g cs11.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z
```
On other distros, you'll need clang 3.6 and llvm 3.6. Then, change the commands "clang++-3.6" and "llvm-config-3.6" as suitable (for example, to "clang++" if that's what it's called).

The 3.6 branch is necessary at the moment because the headers move around every llvm version.

###AST structure
Each AST has 3 main components:

1. "tag", signifying what type of AST it is.
2. "preceding_BB_element", which is a pointer that points to the previous element in its basic block.
3. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all. For example, the "add" AST takes two integers in its first two fields, and the remaining fields are ignored.

Note that our basic block structure is reversed: each element points to the _previous_ element in the basic block, instead of pointers pointing to the _next_ element. The purpose of this reversal is to make construction easier.

###Running things
To run ASTs, you have to first construct them in main(), which contains sample code. The AST constructor takes in the tag, then the preceding basic block element, and then any field elements.

For example,
```
AST getif("if", &addthem, &get1, &get2, &get3);
```

"if" is the tag.

&addthem is the previous basic block element, so that the addthem command will be run before the if command. If we had nullptr instead, then there would be no previous basic block element.

get1 is tested, and since it's non-zero, the if statement will evaluate to true. Thus, the if statement will run the get2 AST, instead of the get3 AST.

Finally, we call compile_AST on the last AST in the block. This outputs the IR and then runs the code.
