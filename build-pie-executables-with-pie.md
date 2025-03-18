Title: Build PIE executables in Go: it's a rabbithole
Tags: Go, PIE
---

## Context

Lately I have been hardening the build at work of our Go services and one low-hanging fruit was [PIE](https://en.wikipedia.org/wiki/Position-independent_code). This is code built to be relocatable, meaning it can be loaded by the operating system at any memory address. That complicates the work of an attacker because they typically want to manipulate the return address of the current function (e.g. by overwriting the stack due to a buffer overflow) to jump to a specific function e.g. `system()` from libc to pop a shell.

If the target function is loaded at a different address each time, it makes it a bit more difficult for the attacker to find it. This approach has been supported for more than two decades by most OSes and toolchains and there is practially no downside. Some people report a single digit percent slowdown in rare cases although it's not the rule. Go supports PIE as well, however this is not the default. 

PIE is especially desirable when using CGO to call C functions from Go which is my case at work. 
And Go is [not entirely memory safe](https://blog.stalkr.net/2015/04/golang-data-races-to-break-memory-safety.html) so there are a few situations there where PIE is a mitigation we'd like to have.

So let's look into enabling it.

## How hard can it be?

`go help buildmode` states:

>  -buildmode=pie
>    Build the listed main packages and everything they import into
>    position independent executables (PIE). Packages not named
>    main are ignored.

Easy enough, right?

Let's build a hello world program (*not* using CGO) with default options:

```sh
$ go build main.go
$ file ./main
./main: ELF 64-bit LSB executable [..] statically linked [..]
```

Now, let's add the `-buildmode=pie` option:

```sh
$ go build -buildmode=pie ./main.go
$ file ./main
./main: ELF 64-bit LSB pie executable [..] dynamically linked [..] 
```

Ok, it worked, but also: why did we go from a statically linked file to a dynamically linked file? PIE is orthogonal to static/dynamic linking! Or is it? We'll come back to that.

Now, we deploy our freshly built PIE executable in a distroless Docker container where there is not even a libc available or the loader `ld.so`. 
And we get a nice cryptic error at runtime:

```
exec /home/nonroot/my-service: no such file or directory
```

That's because our executable is dynamically linked and now required the loader `ld.so` to be present. Which means we need to change our Docker image, etc. Not ideal. What can we do?

## Troubleshooting the problem

Statically or dynamically linking an executable is decided by the linker flags. Let's have a look at them by passing the `-x` option:

```sh
$ go build -x main.go
[..] /home/pg/Downloads/go/pkg/tool/linux_amd64/link [..] -buildmode=exe 
```

The linker shipping with Go, `link`, is used.

Now let's see what happens in PIE mode:

```sh
$ go build -buildmode-pie -x main.go
[..]
/home/pg/Downloads/go/pkg/tool/linux_amd64/link [..] -installsuffix shared -buildmode=pie 
```

The `shared` flag is sneakily passed to the Go linker. That explains it.

---

Back to our central question: 

> PIE is orthogonal to static/dynamic linking! Or is it?


Well...no. It turns out that PIE was initially tailored to dynamically linked executables:
The loader loads at startup the sections of the executable at different places in memory and voila!

So how does it work with a statically linked executable that does not even use a loader? Well, have you heard the saying: 

> In Computer Science, any problem can be solved by introducing an additional level of indirection.

The whole purpose of PIE is to have the address of each function be unpredicatable. A call to a function at the assembly level is basically just pushing the return address on the stack
and jumping to an address. If we jump to a statically known address, we do not have PIE. If we store the runtime address of each function in a global table, load the address of the target function from this table, and jump to it, we have successfully reached our goal, by adding this additional level of indirection. And that's the [Global Offset Table](https://en.wikipedia.org/wiki/Global_Offset_Table).

And up until a few years ago, statically built PIE executables were less secure than their dynamic counterpart. [This article](https://www.leviathansecurity.com/blog/aslr-protection-for-statically-linked-executables) does a deep dive on this topic (it's from 2015 so some things have improved since).

**But**: The OS and the system toolchain support an executable built in PIE mode *and* statically linked! Let's take a simple C program:

```c
#include <stdio.h>

int main() { printf("Hello!"); }
```

We compile it som using the musl libc since glibc does not support static linking:

```sh
$ musl-gcc -static-pie pie.c -fPIE
$ file ./a.out
./a.out: ELF 64-bit LSB pie executable [...] static-pie linked [...]
```

So, how can we coerce Go to do the same?

## The solution

The only way I have found is to ask Go to link with an external linker and pass it the flag `-static-pie`. Unfortunately that means that Go will automatically enable
CGO:

```sh
$ CGO_ENABLED=0 go build -buildmode=pie -ldflags '-linkmode external -s -w -extldflags "-static-pie"' main.go
-linkmode requires external (cgo) linking, but cgo is not enabled
```

So, that now means we need to statically link libc. Let's do the same as with the C program and use `musl-gcc` (you can also use Zig, or provide your own build of musl, etc):

```sh
$ CC=musl-gcc go build -ldflags '-linkmode external -s -w -extldflags "--static-pie"' -buildmode=pie main.go
$ file ./main
./main: ELF 64-bit LSB pie executable [..] static-pie linked
```

Yeah!


