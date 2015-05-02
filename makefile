all: src/*
	clang++-3.6 -g unwrappedsrc/cs11.cpp unwrappedsrc/types.cpp `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z -ferror-limit=2

van: src/*
	clang++ -g unwrappedsrc/cs11.cpp unwrappedsrc/types.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z
