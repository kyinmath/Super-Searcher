#Bootstrap
###To compile in Ubuntu:

To get llvm, append this to /etc/apt/sources.list :
```
deb http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
deb-src http://llvm.org/apt/trusty/ llvm-toolchain-trusty-3.6 main
```

then in console, run
```
sudo apt-get update
sudo apt-get install clang-3.6 llvm-3.6
```

If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev. The 3.6 branch is necessary at the moment because the headers move around every llvm version.
There may be some errors about unauthenticated packages. The clang page says something about
```
wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key|sudo apt-key add -
```
but I don't know what that actually means.

To compile, run "make". Or if your path is clang++ instead of clang++-3.6, you should change the makefile from clang++-3.6 to clang++, and maybe llvm-config-3.6 to llvm_config.
