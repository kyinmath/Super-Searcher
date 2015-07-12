CPP_FILES := $(wildcard ../unwrappedsrc/*.cpp)
ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG := -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions
LLVM_CONFIG_CXX_SUBSTITUTE := -I/usr/include  -fPIC -fvisibility-inlines-hidden -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-uninitialized -Wno-comment -std=c++11 -ffunction-sections -fdata-sections -O3   -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS

all: ../unwrappedsrc/*
	clang++ -g -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

fast: ../unwrappedsrc/*
	clang++ -Wall -fno-rtti $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -g -O3
	-rm -If core

check: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=address -fsanitize=integer -fsanitize=undefined -fsanitize=dataflow -fsanitize=safe-stack -fno-rtti -fno-exceptions $(LLVM_CONFIG_CXX_SUBSTITUTE) $(CPP_FILES) `llvm-config  --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core


checkmem: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=memory -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core


checkcfi: ../unwrappedsrc/*
	clang++ -g -Wall -fsanitize=cfi -flto -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

gccfast: ../unwrappedsrc/*
	clang++ -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++14 -g -O3
	-rm -If core