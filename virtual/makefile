CPP_FILES := $(wildcard ../src/*.cpp)
ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG := -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions
LLVM_CONFIG_CXX_SUBSTITUTE := -I/usr/include  -fPIC -fvisibility-inlines-hidden -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-uninitialized -Wno-comment -std=c++11 -ffunction-sections -fdata-sections -O3   -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS

#Unity build far faster than normal build (11s vs 23s)

all: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -g -Wall -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

fast: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -g -Wall -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O3
	rm -If core

#absolutely no debug symbols or anything like that allowed.
secure: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -g -Wall -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O3
	strip -R .comment -R .note toy
	rm -If core

check: ../src/*
	clang++ -g -Wall -fsanitize=address -fsanitize=integer -fsanitize=undefined -fsanitize=dataflow -fsanitize=safe-stack -fno-rtti -fno-exceptions $(LLVM_CONFIG_CXX_SUBSTITUTE) $(CPP_FILES) `llvm-config  --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

debug: ../src/*
	clang++ -g -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

checkmem: ../src/*
	clang++ -g -Wall -fsanitize=memory -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core


checkcfi: ../src/*
	clang++ -g -Wall -fsanitize=cfi -flto -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=2 -O0
	rm -If core

gccfast: ../src/*
	clang++ -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++14 -g -O3
	-rm -If core