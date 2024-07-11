# How to compile LLVM, Clang, LLD, and Ziglang from source on Alpine Linux

*<svg aria-hidden="true" focusable="false" role="img" class="octicon octicon-tag" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" style="display:inline-block;user-select:none;overflow:visible"><path d="M1 7.775V2.75C1 1.784 1.784 1 2.75 1h5.025c.464 0 .91.184 1.238.513l6.25 6.25a1.75 1.75 0 0 1 0 2.474l-5.026 5.026a1.75 1.75 0 0 1-2.474 0l-6.25-6.25A1.752 1.752 0 0 1 1 7.775Zm1.5 0c0 .066.026.13.073.177l6.25 6.25a.25.25 0 0 0 .354 0l5.025-5.025a.25.25 0 0 0 0-.354l-6.25-6.25a.25.25 0 0 0-.177-.073H2.75a.25.25 0 0 0-.25.25ZM6 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2Z"></path></svg> [LLVM](/blog/articles-per-tag.html#LLVM), [Zig](/blog/articles-per-tag.html#Zig), [Alpine](/blog/articles-per-tag.html#Alpine)*

*This article is now outdated but remains for historical reasons.*

[Ziglang](https://ziglang.org), or `Zig` for short, is an ambitious programming language addressing important flaws of mainstream languages such as failing to handle memory allocation failures or forgetting to handle an error condition in general.

It is also fast moving so for most, the latest (HEAD) version will be needed, and most package managers will not have it, so we will compile it from source.

Since the official Zig compiler is (currently) written in C++ and using the LLVM libraries at a specific version, we will need them as well, and once again, some package managers will not have the exact version you want (10.0.0). 

I find it more reliable to compile LLVM, Clang, LLD, and Zig from source and that is what we will do here. I have found that the official LLVM and Zig instructions differed somewhat, were presenting too many options, and I wanted to have one place to centralize them for my future self.

Incidentally, if you are a lost C++ developer trying to compile LLVM from source, without having ever heard of Zig, well you have stumbled on the right page, you can simply skip the final block about Zig.

Note that those instructions should work just the same on any Unix system. Feel free to pick the directories you want when cloning the git repositories.

```sh
# The only Alpine specific bit. build-base mainly installs make and a C++ compiler. Python 3 is required by LLVM for some reason.
$ apk add build-base cmake git python3

$ git clone https://github.com/llvm/llvm-project.git --branch llvmorg-10.0.0  --depth 1
$ cd llvm-project/
$ mkdir build
$ cd build/
# The flag LLVM_ENABLE_PROJECTS is crucial, otherwise only llvm will be built, without clang or lld,
# and we need all three with the exact same version since C++ does not have a stable ABI.
$ cmake -DCMAKE_BUILD_TYPE=Release -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD="AVR" -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_PROJECTS="clang;lld" ../llvm

# nproc is Linux only but you can set the number of threads manually
$ make -j$(nproc)
$ sudo make install

$ cd ~
$ git clone https://github.com/ziglang/zig.git --depth 1
$ cd zig
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_BUILD_TYPE=Release -DZIG_STATIC=ON
# nproc is Linux only but you can set the number of threads manually
$ make -j$(nproc)
$ sudo make install
```

You will now have a `zig` executable in the PATH as well as the zig standard library. You can verify you have now the latest version by doing:

```
$ zig version
0.6.0+749417a
```

