#Bootstrap
###To compile in Ubuntu 14.10:

To get llvm, append this to /etc/apt/sources.list :
```
deb http://llvm.org/apt/trusty/ llvm-toolchain-trusty main
deb-src http://llvm.org/apt/trusty/ llvm-toolchain-trusty main
```

Change /trusty/ to the proper version. Then in console, run
```
sudo apt-get update
sudo apt-get install clang-3.7 llvm-3.7
```

If there are errors about lz and ledit, apt-get install the packages libedit-dev and zlib1g-dev. The 3.7 branch is necessary at the moment because the headers move around every llvm version.
There may be some errors about unauthenticated packages. The clang page says something about
```
wget -O - http://llvm.org/apt/llvm-snapshot.gpg.key|sudo apt-key add -
```
but I don't know what that actually means.

To compile, run "make". Or if your path is clang++ instead of clang++-3.7, you should change the makefile from clang++-3.6 to clang++, and maybe llvm-config-3.7 to llvm_config.
