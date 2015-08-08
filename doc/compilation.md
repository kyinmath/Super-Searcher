#Bootstrap clang:
###To compile in Ubuntu 15.04, following http://llvm.org/apt/:

To get llvm, append this to /etc/apt/sources.list. Change /vivid/ to the proper version, if using a different Ubuntu version.
```
deb http://llvm.org/apt/vivid/ llvm-toolchain-vivid main
deb-src http://llvm.org/apt/vivid/ llvm-toolchain-vivid main
```

Run 
```
wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key|sudo apt-key add -
sudo apt-get update
sudo apt-get install clang-3.7 llvm-3.7 zlib1g-dev libedit-dev
```

The packages zlib1g-dev and libedit-dev are to suppress errors about lz and ledit. The 3.7 branch is necessary at the moment because the headers move around every llvm version.

Symlinks:
//sudo ln -s /usr/bin/clang-3.7 /usr/bin/clang
sudo ln -s /usr/bin/clang++-3.7 /usr/bin/clang++
sudo ln -s /usr/bin/llvm-config-3.7 /usr/bin/llvm-config


Otherwise, to live on the top of trunk, follow the clang "Getting Started" guide, and use at the end (perhaps without assertions if you want, they cause a 80% slowdown):
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=Off -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_INCLUDE_EXAMPLES=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_ENABLE_THREADS=Off -DLLVM_ENABLE_CXX1Y=On -DCMAKE_INSTALL_PREFIX=/usr/ ../llvm
sudo make install

Maybe for lld:
svn co http://llvm.org/svn/llvm-project/lld/trunk lld
in /tools/. see the lld getting started page. I think I'm doing this because things like address sanitizer don't work with gcc. and gcc-5 breaks the linker completely.

cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=On -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_INSTALL_PREFIX=/usr/ ../llvm
then make install lld?


sudo apt-get install libcrypto++-dev libcrypto++-doc libcrypto++-utils



For my project:

To compile, run "make". Or if your path is clang++ instead of clang++-3.7, you should change the makefile from clang++-3.7 to clang++, and llvm-config-3.7 to llvm_config.



I installed gcc-5 and my Orc project broke completely, when using either clang++-3.7 or g++-5 as the compiler (I suspect the linker was switched). then after I uninstalled gcc-5, my Orc project started working again