CPP_FILES := $(wildcard ../src/*.cpp)
#ADMIN_FILES := $(wildcard ../user_admin/*.cpp)
ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG := -fomit-frame-pointer -fvisibility-inlines-hidden -fno-exceptions
LLVM_CONFIG_CXX_SUBSTITUTE := -I/usr/include  -fPIC -fvisibility-inlines-hidden -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-uninitialized -Wno-comment -std=c++11 -ffunction-sections -fdata-sections -O3   -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS

#Unity build far faster than normal build (11s vs 23s)

#boost interprocess requires rtti, but llvm requires fno-rtti, or it'll give link errors looking for typeinfo...
nosanitize: ../src/*
	clang++ -g -Wall -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O0

#can't have fno-rtti with this
admin: ../user_admin/*
	#clang++ -g -Wall -fno-rtti -fno-exceptions $(ADMIN_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o admin -std=c++1z -O0
	clang++ -g -Wall -fno-rtti -fno-exceptions ../user_admin/mmap.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o admin -std=c++1z -O0
	clang++ -g -Wall -fno-rtti -fno-exceptions ../user_admin/slave.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o slave -std=c++1z -O0

sanitize: ../src/*
	clang++ -g -Wall -fno-rtti -fno-exceptions -fsanitize=undefined -fsanitize=address -fno-sanitize-recover=undefined $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O0

unity: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -g -Wall -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -O0

fast: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -g -Wall -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O3

#absolutely no debug symbols or anything like that allowed.
secure: ../src/*
	rm -rf build
	mkdir build
	cp ../src/* build
	awk 'FNR==1{print ""}1' ../src/*.cpp > build/unity.cpp
	clang++ -Wall -Wl,-s -fno-rtti -fno-exceptions build/unity.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O3 -DNDEBUG -DNO_CONSOLE -DNOCHECK
	strip -R .comment -R .note toy

check: ../src/*
	clang++ -g -Wall -fsanitize=integer -fsanitize=dataflow -fsanitize=safe-stack -fno-rtti -fno-exceptions $(LLVM_CONFIG_CXX_SUBSTITUTE) $(CPP_FILES) `llvm-config  --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O0
	

#If you want MemorySanitizer to work properly and not produce any false positives, you must ensure that all the code in your program and in libraries it uses is instrumented (i.e. built with -fsanitize=memory). In particular, you would need to link against MSan-instrumented C++ standard library. We recommend to use libc++ for that purpose. That is, don't worry about errors here for now.
checkmem: ../src/*
	clang++ -g -Wall -fsanitize=memory -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O0
	


checkcfi: ../src/*
	clang++ -g -Wall -fsanitize=cfi -flto -fno-rtti -fno-exceptions $(CPP_FILES) `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++1z -ferror-limit=4 -O0
	

gccfast: ../src/*
	clang++ -Wall -fno-rtti $(CPP_FILES) $(ADDITIONAL_CXX_FLAGS_MISSING_FROM_CPP_FLAGS_FOR_LLVM_CONFIG) `llvm-config --cppflags --ldflags --system-libs --libs core orcjit native` -o toy -std=c++14 -g -O3
	-