CPP_FILES := $(wildcard ../unwrappedsrc/*.cpp)

all: ../unwrappedsrc/*
	clang++-3.7 -g -O0 -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config-3.7 --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2
	rm -If core

fast: ../unwrappedsrc/*
	clang++-3.7 -O3 -Wall -fno-rtti -fno-exceptions -g $(CPP_FILES) `llvm-config-3.7 --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2
	-rm -If core
	-rm -If callgrind.out.*