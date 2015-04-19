###AST format
Each AST has 3 main components:

1. "tag", signifying what type of AST it is.
2. "preceding_BB_element", which is a pointer that points to the previous element in its basic block.
3. "fields", which contains the information necessary for the AST to operate. Depending on the tag, this may be pointers to other ASTs, or integers, or nothing at all. For example, the "add" AST takes two integers in its first two fields, and the remaining fields are ignored.

Note that our basic block structure is reversed: each element points to the _previous_ element in the basic block, instead of pointers pointing to the _next_ element. The purpose of this reversal is to make construction easier.

Descriptions of the ASTs are in the AST_descriptor[] in "AST commands.h". The first field in each descriptor element is the tag name, and the second field is the number of fields required by the AST. To construct ASTs, use the AST constructor, which takes in the tag, then the preceding basic block element, and then any field elements. For example, using the sample code in main(),
```
AST getif("if", &addthem, &get1, &get2, &get3);
```

"if" is the tag.

&addthem is the previous basic block element, so that the addthem command will be run before the if command. If we had nullptr instead, then there would be no previous basic block element.

get1 is tested, and since it's non-zero, the if statement will evaluate to true. Thus, the if statement will run the get2 AST, instead of the get3 AST.