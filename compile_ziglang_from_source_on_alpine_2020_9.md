<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<link rel="shortcut icon" type="image/ico" href="/blog/favicon.ico"/>
<link rel="stylesheet" type="text/css" href="main.css">
<link rel="stylesheet" href="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/styles/default.min.css">
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/highlight.min.js"></script>
<script>
window.addEventListener("load", (event) => {
  hljs.highlightAll();
});
</script>
</head>
<body>

<div id="banner">
    <a id="name" href="/blog"><img id="me" src="me.jpeg"/> Philippe Gaultier </a>
    <ul>
      <li>
      <a href="/blog/feed.xml">Feed</a>
      </li>
      <li>
      <a href="https://www.linkedin.com/in/philippegaultier/">LinkedIn</a>
      </li>
      <li>
        <a href="https://github.com/gaultier">Github</a>
      </li>
    </ul>
</div>
<div class="body">


# How to compile LLVM, Clang, LLD, and Ziglang from source on Alpine Linux

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

> If you liked this article and you want to support me, and can afford it: [Donate](https://paypal.me/philigaultier?country.x=DE&locale.x=en_US)

</div>
</body>
</html>
