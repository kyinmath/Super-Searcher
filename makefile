CPP_FILES := $(wildcard unwrappedsrc/*.cpp)

all: unwrappedsrc/*
	clang++-3.6 -g $(CPP_FILES) `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

fast: unwrappedsrc/*
	clang++-3.6 -g $(CPP_FILES) `llvm-config-3.6 --cxxflags --ldflags --system-libs --libs core mcjit native` -o toy -std=c++1z -ferror-limit=2 -O3
	-rm -If core
	-rm -If callgrind.out.*