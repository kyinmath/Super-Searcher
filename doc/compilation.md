#Bootstrap
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

To compile, run "make". Or if your path is clang++ instead of clang++-3.7, you should change the makefile from clang++-3.7 to clang++, and llvm-config-3.7 to llvm_config.



I installed gcc-5 and my Orc project broke completely, when using either clang++-3.7 or g++-5 as the compiler (I suspect the linker was switched). then after I uninstalled gcc-5, my Orc project started working again