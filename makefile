all: src/*
	clang++-3.6 -g src/cs11.cpp `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z

van: src/*
	clang++ -g src/cs11.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -rdynamic -std=c++1z