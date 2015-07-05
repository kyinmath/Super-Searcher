CPP_FILES := $(wildcard ../unwrappedsrc/*.cpp)
ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG := -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions

all: ../unwrappedsrc/*
	clang++ -g -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

fast: ../unwrappedsrc/*
	clang++ -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -g -O3
	-rm -If core

check: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=address -fsanitize=integer -fsanitize=undefined -fsanitize=dataflow -fsanitize=safe-stack -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core


checkmem: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=integer -fsanitize=undefined -fsanitize=memory -fsanitize=dataflow -fsanitize=safe-stack -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core


checkcfi: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=address -fsanitize=integer -fsanitize=undefined -fsanitize=dataflow -fsanitize=cfi -flto -fsanitize=safe-stack -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

gccfast: ../unwrappedsrc/*
	clang++ -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++14 -g -O3
	-rm -If core