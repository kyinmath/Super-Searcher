#CS11 backend

This program (backend) takes in ASTs, JITs them, and then runs them. The AST language will be mostly C-like, supporting things like stacks, functions, structures, destructors, and control flow. The design of the ASTs will emphasize readability and potential for self-modification. The backend will enforce type and memory safety.


###To compile in Ubuntu:

these go in /etc/apt/sources.list :
```
deb http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
deb-src http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
```
then in console,
```
sudo apt-get install clang-3.6 llvm-3.6
clang++-3.6 -g cs11.cpp `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z
```
If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev.

###AST structure
Each AST has 3 components:
1. "tag", signifying what type of AST it is.
2. "preceding_BB_element", which is a pointer that points to the previous element in the basic block.
3. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all.
Note that our basic block structure is reversed: each element points to the _previous_ element, instead of pointers pointing to the _next_ element. The purpose of this reversal is to make construction significantly easier.

To run ASTs, you have to construct them first in main(). A sample is given. The AST constructor takes in the tag, then the preceding basic block element, and then any field elements.