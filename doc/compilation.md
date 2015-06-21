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
sudo apt-get install clang-3.7 llvm-3.7
```

If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev. The 3.7 branch is necessary at the moment because the headers move around every llvm version.

To compile, run "make". Or if your path is clang++ instead of clang++-3.7, you should change the makefile from clang++-3.6 to clang++, and maybe llvm-config-3.7 to llvm_config.
