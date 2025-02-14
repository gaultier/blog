Title: Addressing CGO thousand cuts one at a time
Tags: Go, C, Make
---

Rust? Go? Cgo!

I maintain a Go codebase at work which does most of its work through a Rust library that exposes a C API. So they interact via Cgo, Go's FFI mechanism. And it works!

Also, Cgo has many weird limitations and surprises. Fortunately, over the two years or so I have been working in this project, I have (re-)discovered solutions for most of these issues. Let's go through them, and hopefully the next time you use Cgo, you'll have a smooth experience.

*From Go's perspective, Rust is invisible, the C library looks like a pure C library (and indeed it used to be 100% C++ before it got incrementally rewritten to Rust). So I will use C snippets in this article, because that's what the public C header of the library looks like, and not everybody knows Rust, but most people know a C-like language.*

Let's create a sample app:

```
.
├── app
│   └── app.go
├── c
│   ├── api.c
│   ├── api.h
│   ├── api.o
│   ├── libapi.a
│   └── Makefile
├── go.mod
└── main.go
```
The C code is in the `c` directory, we build a static library `libapi.a` from it. The public header file is `api.h`.

The Go code then links this library.

## CGO does not have unions

This is known to Go developers: Go does not have unions, also known as tagged unions, sum types, rich enums, etc. But Go needs to generate Go types for each C type, so that we can use them! So what does it do for C unions? Let's have a look.

So, here is a (very useful) C tagged union:

```c
// c/api.h

#pragma once
#include <stdint.h>

typedef struct {
  char *data;
  uint64_t len;
} String;

typedef enum {
  ANIMAL_KIND_DOG,
  ANIMAL_KIND_CAT,
} AnimalKind;

typedef struct {
  AnimalKind kind;
  union {
    String cat_name;   // Only for `ANIMAL_KIND_CAT`.
    uint16_t dog_tail; // Only for `ANIMAL_KIND_DOG`.
  };
} Animal;

Animal animal_make_dog();

Animal animal_make_cat();

void animal_print(Animal *animal);
```

The C implementation is straightforward:

```c
// c/api.c

#include "api.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

Animal animal_make_dog() {
  return (Animal){
      .kind = ANIMAL_KIND_DOG,
      .dog_tail = 42,
  };
}

Animal animal_make_cat() {
  return (Animal){
      .kind = ANIMAL_KIND_CAT,
      .cat_name =
          {
              .data = "kitty",
              .len = 5,
          },
  };
}

void animal_print(Animal *animal) {
  switch (animal->kind) {
  case ANIMAL_KIND_DOG:
    printf("Dog: %" PRIu16 "\n", animal->dog_tail);
    break;
  case ANIMAL_KIND_CAT:
    printf("Cat: %.*s\n", (int)animal->cat_name.len, animal->cat_name.data);
    break;
  default:
    assert(0 && "unreachable");
  }
}
```

And here's how we use it in Go:

```go
// app/app.go

package app

// #cgo CFLAGS: -g -O2 -I${SRCDIR}/../c/
// #cgo LDFLAGS: ${SRCDIR}/../c/libapi.a
// #include <api.h>
import "C"
import "fmt"

func DoStuff() {
	dog := C.animal_make_dog()
	C.animal_print(&dog)

	cat := C.animal_make_cat()
	C.animal_print(&cat)
}
```

So far, so good. Let's run it (our `main.go` simply calls `app.DoStuff()`):

```sh
$ go run .
Dog: 42
Cat: kitty
```

Great!

Now, let's say we want to access the fields of the C tagged union. We can to have some logic based on whether our cat's name is greater than a limit, say 255? What does the Go struct look like for `Animal`?

```go
type _Ctype_struct___0 struct {
	kind	_Ctype_AnimalKind
	_	[4]byte
	anon0	[16]byte
}
```

So it's a struct with a `kind` field, so far so good. Then comes 4 bytes of padding, as expected (the C struct also has them). But then, we only see 16 opaque bytes. The size is correct: the C union is the size of its largest member which is 16 bytes long (`String`). But then, how do we access `String.len`? 

Here's the very tedious way, by hand:

```go
// app/app.go
func DoStuff() {
    // [...]

	cat_ptr := unsafe.Pointer(&cat)
	cat_name_ptr := unsafe.Add(cat_ptr, 8)
	cat_name_len_ptr := unsafe.Add(cat_name_ptr, 8)
	fmt.Println(*(*C.uint64_t)(cat_name_len_ptr))
}
```

And we get:

```
$ go run .
Dog: 42
Cat: kitty
5
```

Ok, we are a C compiler now. Back to computing fields offsets by hand! I sure hope you do not forget about alignement! And keep the offsets in sync with the C struct when its layout changes!

Well we all agree this sucks, but that's all what the `unsafe` package offers. Cherry on the cake, every pointer in this code has the same type: `unsafe.Pointer`, even though the first one really is a `Animal*`, the second one is a `String*`, and the third one is a `uint64_t*`. Not great. 


So the solution is: treat C unions as opaque values in Go, and only access them with C functions (essentially, getters and setters):

```c
// c/api.h

uint16_t animal_dog_get_tail(Animal *animal);

String animal_cat_get_name(Animal *animal);
```


```c
// c/api.c

uint16_t animal_dog_get_tail(Animal *animal) {
  assert(ANIMAL_KIND_DOG == animal->kind);
  return animal->dog_tail;
}

String animal_cat_get_name(Animal *animal) {
  assert(ANIMAL_KIND_CAT == animal->kind);
  return animal->cat_name;
}
```

And now we have a sane Go code:

```go
// app/app.go
func DoStuff() {
    // [...]

	cat_name := C.animal_cat_get_name(&cat)
	fmt.Println(cat_name.len)
}
```

And as a bonus, whenever the layout of `Animal` changes, for example the order of fields gets changed, or a new field gets added which changes the alignment and thus the padding (here it's not the case because the alignment is already 8 which is the maximum, but in other cases it could happen), the C code gets recompiled, it does the right thing automatically, and everything works as expected. 

**My recommendation:** never role-play as a compiler, just use getters and setters for unions and let the C compiler do the dirty work.


**My ask to the Go team:** mention the approach with getters and setters in the docs. The only thing the [docs](https://pkg.go.dev/cmd/cgo) have to say about unions right now is: `As Go doesn't have support for C's union type in the general case, C's union types are represented as a Go byte array with the same length`. And I don't expect Go to have (tagged) unions anytime soon, so that's the best we can do.


## Slices vs Strings

Quick Go trivia question: what's the difference between `[]byte` (a slice of bytes) and `string` (which is a slice of bytes underneath)?

...


The former is mutable while the latter is immutable.
Yes, I might have learned that while writing this article.

Anyways, converting C slices (pointer + length) to Go is straightforward using the `unsafe` package in modern Go (it used to be much hairier in older Go versions):

```go
// app/app.go
func DoStuff() {
    // [...]

	cat_name := C.animal_cat_get_name(&cat)
	slice := unsafe.Slice(cat_name.data, cat_name.len)
	str := unsafe.String((*byte)(unsafe.Pointer(cat_name.data)), cat_name.len)

	fmt.Println(slice)
	fmt.Println(str)
}
```

And it does what we expect:

```sh
$ go run .
Dog: 42
Cat: kitty
[107 105 116 116 121]
kitty
```

Ok, but just reading them is boring, let's try to mutate them. First, we need to allocate a fresh string in C, otherwise the string constant will be located in the read-only part of the executable, mapped to read-only page, and we will segfault when trying to mutate it. So we modify `animal_make_cat`:

```c
// c/api.c

Animal animal_make_cat() {
  return (Animal){
      .kind = ANIMAL_KIND_CAT,
      .cat_name =
          {
              .data = strdup("kitty"), // <= Heap allocation here.
              .len = 5,
          },
  };
}
```

Let's mutate all the things!

```go
// app/app.go
func DoStuff() {
    // [...]

	slice[0] -= 32 // Poor man's uppercase.
	fmt.Println(slice)
	fmt.Println(str)
}
```

And we get the additional output:

```
[75 105 116 116 121]
Kitty
```

But wait, this is undefined behavior! The string *did* get mutated! The Go compiler generates code based on the assumption that strings are immutable, so our program *may* break in very unexpected ways.

The docs for `unsafe.String` state:

> Since Go strings are immutable, the bytes passed to String
> must not be modified as long as the returned string value exists.

Maybe the runtime Cgo checks will detect it?

```
$ GODEBUG=cgocheck=1 go run .
[...]
[75 105 116 116 121]
Kitty
# Works fine!

$ GOEXPERIMENT=cgocheck2 go run . 
[...]
[75 105 116 116 121]
Kitty
# Works fine!
```

Nope...so what can we do about it? In my real-life program I have almost no strings to deal with, but some programs will.

**My recommendation:**

- In Go, do not use `unsafe.String`, just use `unsafe.Slice` and accept that it's mutable data everywhere in the program
- If you really want to use `unsafe.String`, make sure that the string data returned by the C code is immutable **at the OS level**, so either:
  + It's a constant string placed in the read-only segment
  + The string data is allocated in its own virtual memory page and the page permissions are changed to read-only before returning the pointer to Go
- In C, do not expose string data directly to Go, only expose opaque values (`void*`), and mutations are only done by calling a C function. That way, the Go caller simply cannot use `unsafe.String` (I guess they could with lots of casts, but that's not in the realm of *honest mistake* anymore).


**My ask to the Go team:** attempt to develop more advanced checks to detect this issue at runtime.


## Test a C function in Go tests

We are principled programmers who write tests. Let's write a test to ensure that `animal_make_dog()` does indeed create a dog, i.e. the kind is `ANIMAL_KIND_DOG`:

```go
// app/app_test.go

package app

import "testing"
import "C"

func TestAnimalMakeDog(t *testing.T) {
	dog := C.animal_make_dog()
	_ = dog
}
```

Let's run it:

```sh
$ go test ./app/
use of cgo in test app_test.go not supported
```

Ah...yeah this is a known limitation. 

Solution: wrap the C function in a Go one.

```
// app/app.go

func AnimalDogKind() int {
	return C.ANIMAL_KIND_DOG
}

func AnimalMakeDog() C.Animal {
	return C.animal_make_dog()
}
```

And we now have a passing Go test:

```go
// app/app_test.go

package app

import "testing"

func TestAnimalMakeDog(t *testing.T) {
	dog := AnimalMakeDog()
	if int(dog.kind) != AnimalDogKind() {
		panic("wrong kind")
	}
}
```

```sh
$ go test ./app/ -count=1 -v
=== RUN   TestAnimalMakeDog
--- PASS: TestAnimalMakeDog (0.00s)
PASS
ok  	cgo/app	0.003s
```

So...that works, and also: that's annoying boilerplate that no one wants to have to write. And if you're feeling smug, thinking your favorite LLM will do the right thing for you, I can tell you I tried and the LLM generated the wrong thing, with the test trying to use Cgo directly.

**My recommendation:** 

- Test the C code in C directly (or Rust, or whatever language it is)
- There is some glue code sometimes that is only useful to the Go codebase, and that's written in C. In that case wrap each C utility function in a Go one and write a Go test, hopefully it's not that much.

**My ask to the Go team:** let's allow the use of Cgo in tests.

## The Go compiler does not detect changes

People are used to say: Go builds so fast! And yes, it's not a slow compiler, but if you have ever built the Go compiler from scratch, you will have noticed it takes a significant amount of time still. What Go is really good at, is caching: it's really smart at detecting what changed, and only rebuilding that. And that's great! Until it isn't. 

Sometimes, changes to the Cgo build flags, or to the `.a` library, were not detected by Go. I could not really reproduce these issues reliably, but they happen often.

Solution: force a clean build with `go build -a`. 

## False positive warnings

So, let's run some C code once at startup, when the package gets initialized:

```Go
// app/app.go

package app

// #cgo CFLAGS: -g -O2 -I${SRCDIR}/../c/
// #cgo LDFLAGS: ${SRCDIR}/../c/libapi.a
// #include <api.h>
// void initial_setup();
import "C"

func init() {
	C.initial_setup()
}


[...]
```

And the C function `initial_setup` is defined in a second file in the same Go package (this is not strictly nessecary but it will turn out to be useful to showcase something later):

```go
// app/cfuncs.go

package app

/*
void initial_setup(){
    // Do some useful stuff.
}
*/
import "C"
```

Yes, we can write C code directly in Go files. Inside comments. Not, it's not weird at all.

We build, everything is fine:

```sh
$ go build .
```

Since we are serious programmers, we want to enable C warnings, right? Let's add `-Wall` to the `CFLAGS`:

```go
// app/app.go

[...]
// #cgo CFLAGS: -Wall -g -O2 -I${SRCDIR}/../c/    <= We add -Wall
[...]
```

We re-build, and get this nice error:

```go
$ go build .
# cgo/app
cgo-gcc-prolog: In function ‘_cgo_13d20cc583d0_Cfunc_initial_setup’:
cgo-gcc-prolog:78:49: warning: unused variable ‘_cgo_a’ [-Wunused-variable]
```

Wait, we do not have *any* variable in `initial_setup`, how come a variable is unused?

Some searching around turns up this [Github issue](https://github.com/golang/go/issues/6883#issuecomment-383800123,) where the official recommendation is: do not use `-Wall`, it creates false positives. Ok.

**My recommendation:** Write C code in C files and enable all the warnings you want.

**My ask to the Go team:** Let's please fix the false positives and allow people to enable some basic warnings. `-Wall` is the bare minimum!

## Whitespace is significant

Let's go back to the `app/cfuncs.go` we just created that builds fine:

```go
package app

/*
void initial_setup(){}
*/
import "C"
```

Let's add one empty line near the end:

```go

package app

/*
void initial_setup(){}
*/

import "C"
```

Let's build:

```sh
$ go build .
# cgo
/home/pg/Downloads/go/pkg/tool/linux_amd64/link: running gcc failed: exit status 1
/usr/bin/gcc -m64 -o $WORK/b001/exe/a.out -Wl,--export-dynamic-symbol=_cgo_panic -Wl,--export-dynamic-symbol=_cgo_topofstack -Wl,--export-dynamic-symbol=crosscall2 -Wl,--compress-debug-sections=zlib /tmp/go-link-2748897775/go.o /tmp/go-link-2748897775/000000.o /tmp/go-link-2748897775/000001.o /tmp/go-link-2748897775/000002.o /tmp/go-link-2748897775/000003.o /tmp/go-link-2748897775/000004.o /tmp/go-link-2748897775/000005.o /tmp/go-link-2748897775/000006.o /tmp/go-link-2748897775/000007.o /tmp/go-link-2748897775/000008.o /tmp/go-link-2748897775/000009.o /tmp/go-link-2748897775/000010.o /tmp/go-link-2748897775/000011.o /tmp/go-link-2748897775/000012.o /tmp/go-link-2748897775/000013.o /tmp/go-link-2748897775/000014.o /tmp/go-link-2748897775/000015.o /tmp/go-link-2748897775/000016.o -O2 -g /home/pg/scratch/cgo/app/../c/libapi.a -O2 -g -lpthread -no-pie
/usr/bin/ld: /tmp/go-link-2748897775/000001.o: in function `_cgo_f1a74d84225f_Cfunc_initial_setup':
/tmp/go-build/cgo-gcc-prolog:80:(.text+0x53): undefined reference to `initial_setup'
collect2: error: ld returned 1 exit status
```

Ok...not much to say here.


Here's another example. We add a comment about not using `-Wall`:

```go
// app/app.go

package app

// NOTE: Do not use -Wall.
// #cgo CFLAGS: -g -O2 -I${SRCDIR}/../c/
// #cgo LDFLAGS: ${SRCDIR}/../c/libapi.a
// #include <api.h>
// void initial_setup();
import "C"

[...]
```

We rebuild, and boom:

```sh
$ go build .
# cgo/app
app/app.go:3:6: error: expected '=', ',', ';', 'asm' or '__attribute__' before ':' token
    3 | // NOTE: Do not use -Wall.
      |      ^
```

That's because when seeing a `#cgo` directive in the comments, the Go compiler parses what it recognizes, passes the rest to the C compiler, which chokes on it.

Solution: insert a blank line between the comment and the `#cgo` directive to avoid that:

```go
// app/app.go

package app

// NOTE: Do not use -Wall.

// #cgo CFLAGS: -g -O2 -I${SRCDIR}/../c/
// #cgo LDFLAGS: ${SRCDIR}/../c/libapi.a
// #include <api.h>
// void initial_setup();
import "C"

[...]
```

**My recommendation:** If you get a hairy and weird error, compare the whitespace with official code examples.

**My ask to the Go team:** Can we please fix this? Or at least document it? There is zero mention of this pitfall anywhere, as far as I can see.

## Cross-compile

So picture me, building my Go program (a web service) using Cgo. Locally, it builds very quickly, due to Go caching (when it works). 

Now, time to build in Docker to be able to deploy it:

```sh
$ time docker build [...]
[...]
Total execution time 101.664413146
```

Every. Single. Time. Urgh.

That's why I am convinced that docker is not meant to build stuff. Ideally, a single static executable is built locally, relying on caching of previous builds. Then, it is copied inside the image which, again ideally, for security purposes, is very barebone. The dockerfile can look like this:

```dockerfile
FROM gcr.io/distroless/static:nonroot
USER nonroot
WORKDIR /home/nonroot

COPY --chown=nonroot:nonroot app.exe .

CMD ["/home/nonroot/app.exe"]
```

It's fast, simple, secure. But, to make it work, regardless of the host, we need to cross compile.

Go is praised for its uncomplicated cross compiling support. But this goes out of the window when Cgo is enabled. Let's try:

```sh 
$ GOOS=linux GOARCH=arm go build  .
cgo/app: build constraints exclude all Go files in /home/pg/scratch/cgo/app
```

It fails. But fortunately, Go still supports cross-compiling with Cgo as long as we provide it with a cross-compiler.

After some experimentation, my favorite way is to use [Zig](https://dev.to/kristoff/zig-makes-go-cross-compilation-just-work-29ho) for that. That way it works the same way for people using macOS, Linux, be it on ARM, on x86_64, etc. And it makes it trivial to build native docker images for ARM without changing the whole build system or installing additional tools.

The work on Zig is fantastic, please consider supporting them. 

So, how does it look like? Let's assume we want to target `x86_64-linux-musl`, built statically, since we use a distroless image that does not come with a libc. The benefit is that our service looks like any other Go service without Cgo.

We could also target a specific glibc version and deploy on a LTS version of ubuntu, debian, etc. Zig supports that.

First, we compile our C code:

``sh
$ CC="zig cc --target=x86_64-linux-musl" make -C ./c
```

If we have Rust code, we do instead:

```sh
$ rustup target add x86_64-unknown-linux-musl
$ cargo build --release --all-features --target=x86_64-unknown-linux-musl
```

Then we build our Go code using the Zig C compiler. I put the non cross-compiling build commands just before for comparison:

```sh
$ go build .
$ file cgo
cgo: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=9d5da9b6a211c5635a83e4a8a346ff605f7b6e3b, for GNU/Linux 3.2.0, with debug_info, not stripped

$ CGO_ENABLED=1 CC='zig cc --target=x86_64-linux-musl -static' GOOS=linux GOARCH=amd64 go build .
$ file cgo
cgo: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, Go BuildID=gTeiH1YL9FSvJJ2euuGd/P0s07MkwoQlcaBA6EXDD/NOYPaeKuxUZ0cnLxfpC9/YeAu_C7s53nGjPEKZDYI, with debug_info, not stripped
```

Tadam!

Time to build a native ARM image? No problem:

```sh
$ CC="zig cc --target=aarch64-linux-musl" make -C ./c
$ CGO_ENABLED=1 CC='zig cc --target=aarch64-linux-musl -static' GOOS=linux GOARCH=arm64 go build .
$ file ./cgo
./cgo: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, Go BuildID=QRDa72MrAj44K3mt54PK/_aJwgCwTO37mKpnfElWN/0TEmGFNLMEZCx3Zv_PKs/lgDlIHFQ6-LxhCOsdhQI, with debug_info, not stripped
```

If you've done *any* work with cross-compilation, you know that this is magic.

Oh, and what about the speed now? Here is a full docker build with my real-life program (Rust + Go):

```sh
$ make docker-build
Executed in    1.47 secs
```

That time includes `cargo build --release`, `go build`, and `docker build`. Most of the time is spent copying the giant executable (around 72 MiB!) into the docker image since neither Rust nor Go are particularly good at producing small executables.

So, we want from ~100s to ~1s, roughly a 100x improvement. Pretty pretty good if you ask me.


**My recommendation:**: Never build in docker if you can help it. Build locally and copy the one static executable into the docker image.

**My ask for the Go team**: None actually, they have done an amazing job on the build system to support this use-case, and on the documentation.


## Conclusion

Cgo is rocky, but there are no blocking issues, just lots of small pains. Half of the cure if being aware of the ailment. So armed with this knowledge, I wish you godspeed with your Cgo projects!
