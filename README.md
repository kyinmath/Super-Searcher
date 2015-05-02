###Running things
Just run the program (./toy) and it will automatically generate ASTs and try to run them. Malformed ASTs are fine (they're randomly created by a fuzzer). If you downloaded toy directly instead of compiling, then mark "toy" as executable before running ./toy. There are also arguments you can pass in: "noninteractive", "optimize", and "verbose". For example, you can try "./toy noninteractive optimize".

If you don't want to use the fuzzer, you can use the console (./toy console). Then type things like "[integer 5]int_load [if [integer 1] [copy int_load] [integer 6]]", without the quotation marks. Each [] refers to a single AST. The first word in each AST is its tag, and a list of tags can be found in AST_descriptor[] in "types.h". The remaining words inside the [], separated by spaces, are the fields of the AST. These fields might be more ASTs. A word immediately after the [] is the variable name, which is optional. Variable names are used when ASTs need to point to each other.

###Understanding the program
To get started on the design of the program, read "generate IR.txt" and "AST structure.md". The type information is in "type information.txt".