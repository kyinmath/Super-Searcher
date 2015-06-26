CPP_FILES := $(wildcard ../unwrappedsrc/*.cpp)
ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG := -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions

all: ../unwrappedsrc/*
	clang++-3.7 -g -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config-3.7 --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

fast: ../unwrappedsrc/*
	clang++-3.7 -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config-3.7 --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O2
	-rm -If core
	-rm -If callgrind.out.*