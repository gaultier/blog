Title: Build PIE executables in Go: I got nerd-sniped
Tags: Go, PIE, Linux, Musl, Security
---

## Context

Lately I have been hardening the build at work of our Go services and one (seemingly) low-hanging fruit was [PIE](https://en.wikipedia.org/wiki/Position-independent_code). This is code built to be relocatable, meaning it can be loaded by the operating system at any memory address. That complicates the work of an attacker because they typically want to manipulate the return address of the current function (e.g. by overwriting the stack due to a buffer overflow) to jump to a specific function e.g. `system()` from libc to pop a shell. That's easy if `system` is always at the same address. If the target function is loaded at a different address each time, it makes it more difficult for the attacker to find it. 

This approach was already used in the sixties (!) and has been the default for years now in most OSes and toolchains when building system executables. There is practically no downside. Some people report a single digit percent slowdown in rare cases although it's not the rule. Amusingly this randomness can be used in interesting ways for example seeding a random number generator with the address of a function.

Go supports PIE as well, however this is not the default so we have to opt in. 

PIE is especially desirable when using CGO to call C functions from Go which is my case at work. 
But also Go is [not entirely memory safe](https://blog.stalkr.net/2015/04/golang-data-races-to-break-memory-safety.html) so I'd argue having PIE enabled in all cases is preferable.

So let's look into enabling it. And this is also a good excuse to learn more about how surprisingly complex it is for an OS to just execute a program.

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
./main: ELF 64-bit LSB pie executable [..] dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2 [..]
```

Ok, it worked, but also: why did we go from a statically linked to a dynamically linked executable? PIE should be orthogonal to static/dynamic linking! Or is it? We'll come back to that in a second.

When we run our freshly built Go executable in a bare-bone Docker image (distroless), we get a nice cryptic error at runtime:

```
exec /home/nonroot/my-service: no such file or directory
```

Oh oh. Let's investigate.

### A helpful mental model

I'd argue that the wording of the tools and the online content is confusing because it conflates two different things.

For example, invoking `lld` with the above executable prints `statically linked`. But `file` prints `dynamically linked` for the exact same file! So which is it?

A [helpful mental model](https://www.quora.com/Systems-Programming/What-is-the-exact-difference-between-dynamic-loading-and-dynamic-linking/answer/Jeff-Darcy) is to split *linking* from *loading* and have thus two orthogonal dimensions:

- static linking, static loading
- static linking, dynamic loading
- dynamic linking, static loading
- dynamic linking, dynamic loading

The first dimension (static vs dynamic linking) is, from the point of view of the OS trying to launch our program, decided by the field 'Type' in the ELF header (bytes 16-20): if it's `EXEC`, it's a statically linked executable. If it's `DYN`, it's a shared object file or a statically linked PIE executable (note that the same file can be both a shared library and an executable. Pretty cool, no?).

The second dimension (static vs dynamic loading) is decided by the ELF program headers: if there is one program header of type `INTERP` (which specifies the loader to use), our executable is using dynamic loading meaning it requires a loader (a.k.a interpreter) at runtime. Otherwise it does not. This aspect can be observed with `readelf`:

```sh
$ readelf --program-headers ./main
[..]
Elf file type is DYN (Position-Independent Executable file)
[..]
Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  [..]
  INTERP         0x0000000000000fe4 0x0000000000400fe4 0x0000000000400fe4
                 0x000000000000001c 0x000000000000001c  R      0x1
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  [..]
```

Our newly built Go executable is in the second category: static linking, dynamic loading.


And that's an issue because we deploy it in a distroless Docker container where there is not even a libc available or the loader `ld.so`. 

It means we now need to change our Docker image to include a loader at runtime.

So not ideal. What can we do? We'd like to be in the first category: static linking, static loading.

I suppose that folks that ship executables to customer workstations would also have interest in doing that to remove one moving piece (the loader on each target machine). 

Also possibly people who want to obfuscate what their program does at startup and do not want anyone monkeying around with environment variables that impact the loader such as `LD_PRELOAD` (so, perhaps malware or anti-malware programs?).

*Addendum: I have found at least one [CVE](https://seclists.org/oss-sec/2023/q4/18) in the glibc loader, and it seems it's a frequent occurrence, so in my opinion that's reason enough to remove one moving piece from the equation and prefer static loading!*

## Troubleshooting the problem


It turns out that PIE was historically designed for executables using dynamic loading. 
The loader loads at startup the sections of the executable at different places in memory, fills (eagerly or lazily) in a global table (the [GOT](https://en.wikipedia.org/wiki/Global_Offset_Table)) the locations of symbols. And voila, functions are placed randomly in memory and function calls go through the GOT which is a level of indirection to know at runtime where the function they want to call is located. Blue team, rejoice! Red team, sad.

So how does it work with a statically linked executable where a loader is not even *present* on the system? Here's a bare-bone C program that uses PIE *and* is statically linked:

```c
#include <stdio.h>

int main() { printf("%p %p hello!\n", &main, &printf); }
```

We compile it, create an empty `chroot` with only our executable in it, and run it multiple times, to observe that the functions `main` and `printf` are indeed loaded in different places of memory each time:

```sh
$ musl-gcc pie.c -fPIE -static-pie
$ file ./a.out
./a.out: ELF 64-bit LSB pie executable [..] static-pie linked [..]
$ mkdir /tmp/scratch
$ sudo chroot /tmp/scratch ./a.out
0x7fcf33688419 0x7fcf336887e0 hello!
$ sudo chroot /tmp/scratch ./a.out
0x7f2b44f20419 0x7f2b44f207e0 hello!
$ sudo chroot /tmp/scratch ./a.out
0x7f891d95e419 0x7f891d95e7e0 hello!
```

So... how does it work when no loader is present in the environment? Well, what is the only thing that we link in our bare-bone program? Libc! And what does libc contain? You guessed it, a loader! 

For musl, it's the file `ldso/dlstart.c` and that's the code that runs before our `main`. Effectively libc doubles as a loader. And when statically linked, the loader gets embedded in our application and runs at startup before our code.

That means that we can have our cake and eat it too: static linking and PIE! No loader required in the environment.


So, how can we coerce Go to do the same?

## The solution

The only way I have found is to ask Go to link with an external linker and pass it the flag `-static-pie`. Due to the explanation above that means that CGO gets enabled automatically and we need to link a libc statically:

```sh
$ CGO_ENABLED=0 go build -buildmode=pie -ldflags '-linkmode external -extldflags "-static-pie"' main.go
-linkmode requires external (cgo) linking, but cgo is not enabled
```

We use `musl-gcc` again for simplicity but you can also use the Zig build system to automatically build musl from source, or provide your own build of musl, etc:

```sh
$ CC=musl-gcc go build -ldflags '-linkmode external -extldflags "--static-pie"' -buildmode=pie main.go
$ file ./main
./main: ELF 64-bit LSB pie executable [..] static-pie linked
```

Yeah!

We can check that it works in our empty `chroot` again. Here's is our Go program:

```go
package main

import (
	"fmt"
)

func main() {
	fmt.Println(main, fmt.Println, "hello")
}
```

And here's how we build and run it:

```sh
$ CC=musl-gcc go build -ldflags '-linkmode external -extldflags "--static-pie"' -buildmode=pie main.go
$ cp ./main /tmp/scratch/
$ sudo chroot /tmp/scratch ./main
0x7f0701b17220 0x7f0701b122a0 hello
$ sudo chroot /tmp/scratch ./main
0x7f0f27f3b220 0x7f0f27f362a0 hello
$ sudo chroot /tmp/scratch ./main
0x7f61f8fd7220 0x7f61f8fd22a0 hello
```

Compare that with the non-PIE default build where the function addresses are fixed:

```sh
$ go build main.go
$ cp ./main /tmp/scratch/
$ sudo chroot /tmp/scratch ./main
0x48f0e0 0x48a160 hello
$ sudo chroot /tmp/scratch ./main
0x48f0e0 0x48a160 hello
$ sudo chroot /tmp/scratch ./main
0x48f0e0 0x48a160 hello
```

## Conclusion

'Static PIE', or statically linked PIE executables, are a relatively new development: OpenBSD [added](https://www.openbsd.org/papers/asiabsdcon2015-pie-slides.pdf) that in 2015 and builds all system executables in that mode, Clang only [added](https://reviews.llvm.org/D58307) the flag in 2019, etc. Apparently the Go linker does not support this yet, I suppose because it does not ship with a loader and so has to rely on the libc loader (if someone knows for sure, I'd be curious to know!). After all, the preferred and default way for Go on Linux is 'static linking, static loading'. 

Still I think it's great to do since we get the best of both worlds, only requiring a little bit of finagling with linker flags.

Also it would be nice that the Go documentation talks at least a little about this topic. In the meantime, there is this article, which I hope does not contain inaccuracies and helps a bit.

A further hardening on top of PIE, that I have not yet explored yet, but is on my to do list, is [read-only relocations](https://www.redhat.com/en/blog/hardening-elf-binaries-using-relocation-read-only-relro) which makes the Global Offset Table read-only to prevent an attacker from overwriting the relocation entries there. On Fedora for example, all system executables are built with this mitigation on.

