#Bootstrap
###To compile in Ubuntu:

To get llvm, append this to /etc/apt/sources.list :
```
deb http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
deb-src http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
```

then in console, run
```
sudo apt-get update
sudo apt-get install clang-3.6 llvm-3.6
```

If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev. The 3.6 branch is necessary at the moment because the headers move around every llvm version.
There may be some errors about unauthenticated packages. The clang page says something about
```
wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key|sudo apt-key add -
```
but I don't know what that actually means.

To compile, run "make". Or if your path is clang++ instead of clang++-3.6, run "make van".


###Running things
Just run the program (./toy) and it will automatically generate ASTs and try to run them. Malformed ASTs are fine (they're randomly created by a fuzzer). If you downloaded toy directly instead of compiling, then mark "toy" as executable before running ./toy. There are also arguments you can pass in: "noninteractive", "optimize", and "verbose". For example, you can try "./toy noninteractive optimize".

If you don't want to use the fuzzer, you can construct ASTs in main(). There is sample code there, demonstrating how to use the AST constructor, construct a compiler_object, and compile_AST() the last AST in the block. This outputs the IR and then runs the code.

###Understanding the program
To get started on the design of the program, read "generate IR.txt" and "AST structure.md". The type information is in "type information.txt".