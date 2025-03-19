Title: Build PIE executables in Go: more involved than expected
Tags: Go, PIE
---

## Context

Lately I have been hardening the build at work of our Go services and one (seemingly) low-hanging fruit was [PIE](https://en.wikipedia.org/wiki/Position-independent_code). This is code built to be relocatable, meaning it can be loaded by the operating system at any memory address. That complicates the work of an attacker because they typically want to manipulate the return address of the current function (e.g. by overwriting the stack due to a buffer overflow) to jump to a specific function e.g. `system()` from libc to pop a shell. That's easy if `system` is always at the same address. 

If the target function is loaded at a different address each time, it makes it a bit more difficult for the attacker to find it. This approach has been supported for more than two decades by most OSes and toolchains and there is practially no downside. Some people report a single digit percent slowdown in rare cases although it's not the rule. Go supports PIE as well, however this is not the default so we have to opt in. 

PIE is especially desirable when using CGO to call C functions from Go which is my case at work. 
But also Go is [not entirely memory safe](https://blog.stalkr.net/2015/04/golang-data-races-to-break-memory-safety.html) so I'd argue having PIE enabled in all cases is preferrable.

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

Ok, it worked, but also: why did we go from a statically linked file to a dynamically linked executable? PIE should be orthogonal to static/dynamic linking! Or is it? We'll come back to that.

Now, that's an isse because we deploy our freshly built PIE executable in a distroless Docker container where there is not even a libc available or the loader `ld.so`. 
And we get a nice cryptic error at runtime:

```
exec /home/nonroot/my-service: no such file or directory
```

That's because our executable is dynamically linked and now requires the loader `ld.so` to be present. Which means we now need to change our Docker image to include a loader.

If we are shipping Go binaries to customer machines, we'd also like to keep them statically linked to be sure they will work regardless of the glibc version, etc.

So not ideal. What can we do?


## Troubleshooting the problem


### The how

Statically or dynamically linking an executable is decided by the linker flags. Let's have a look at them by passing the `-x` option:

```sh
$ go build -x main.go
[..] /home/pg/Downloads/go/pkg/tool/linux_amd64/link [..] -buildmode=exe 
```

The linker shipping with Go, `link`, is used. This is in Go parlance: 'internal mode'. 'external mode' is where linking is delegated to an external linker e.g. `ld`.

Now let's see what happens in PIE mode:

```sh
$ go build -buildmode-pie -x main.go
[..]
/home/pg/Downloads/go/pkg/tool/linux_amd64/link [..] -installsuffix shared -buildmode=pie 
```

The `shared` flag is sneakily passed to the Go linker. That explains how it happened, but not yet why.

### The why

Back to our central question: 

> PIE is orthogonal to static/dynamic linking! Or is it?


Well...no. It turns out that PIE was initially tailored to dynamically linked executables:
The loader loads at startup the sections of the executable at different places in memory, fills (eagerly or lazily) in a global table (the [GOT](https://en.wikipedia.org/wiki/Global_Offset_Table)) the locations of symbols. And voila.

So how does it work with a statically linked executable where a loader is not even *present* on the system? Here's a bare-bone C program that uses PIE *and* is statically linked:

```c
#include <stdio.h>

int main() { printf("%p %p hello!\n", &main, &printf); }
```

We compile it, create an empty chroot with only our executable in it, and run it multiple times, to observe that the functions `main` and `printf` are indeed loaded in different places of memory each time:

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

So... how does it work when no loader is present in the environment? Well, what is the only thing that we link in our bare-bone program? Libc! And what libc contain? You guess it, a loader! 

For musl, it's the file `ldso/dlstart.c` and that's the code that runs before our `main`. Effectively libc doubles as a loader. That means that we can have our cake and eat it too: static linking and PIE.


So, how can we coerce Go to do the same?

## The solution

The only way I have found is to ask Go to link with an external linker and pass it the flag `-static-pie`. Due to the explanation above that means that CGO gets enabled automatically and we need to link a libc statically:

```sh
$ CGO_ENABLED=0 go build -buildmode=pie -ldflags '-linkmode external -extldflags "-static-pie"' main.go
-linkmode requires external (cgo) linking, but cgo is not enabled
```

We use `musl-gcc` again for simplicity but you can also use Zig, or provide your own build of musl, etc:

```sh
$ CC=musl-gcc go build -ldflags '-linkmode external -extldflags "--static-pie"' -buildmode=pie main.go
$ file ./main
./main: ELF 64-bit LSB pie executable [..] static-pie linked
```

Yeah!

We can check it works in our empty chroot again. Here's is our Go program:

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




