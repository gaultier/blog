Title: Build PIE executables in Go
Tags: Go, PIE
---

## Context

Lately I have been hardening the build at work of our services and one low-hanging fruit was [PIE](https://en.wikipedia.org/wiki/Position-independent_code). This is code built to be relocatable, meaning it can be loaded by the operating system at any memory address. That complicates the work of an attacker because they typically want to manipulate the return address of the current function (e.g. by overwriting the stack due to a buffer overflow) to jump to a specific function e.g. `system()` from libc to pop a shell.

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

Ok, it worked, but also: why did we go from a statically linked file to a dynamically linked file? PIE is orthogonal to that! Or is it?

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

However many people deploy their statically linked Go executable in a Alpine or a distroless Docker container where there is not even a libc available. So our freshly built executable will simply result in a cryptic error at runtime:

```
exec /home/nonroot/my-service: no such file or directory
```

## The solution

We want to have an executable built in PIE mode *and* statically linked. 
A [Stack Overflow issue](https://stackoverflow.com/questions/64019336/go-compile-to-static-binary-with-pie#64453488) mentions the magic flags:

```sh
$ CC=musl-gcc go build -ldflags '-linkmode external -s -w -extldflags "--static-pie"' -buildmode=pie main.go
$ file ./main
./main: ELF 64-bit LSB pie executable [..] static-pie linked
```

libc is being linked and thus we need a libc that supports static linking e.g. musl.
