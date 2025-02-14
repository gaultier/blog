Title: Addressing CGO thousand cuts one at a time
Tags: Go, C, Make
---

Rust? Go? Cgo!

I maintain a Go codebase at work which does most of its work through a Rust library that exposes a C API. So they interact via Cgo, Go's FFI mechanism. And it works!

Also it has many weird limitations and surprises. Fortunately, over the two years or so I have been working in this project, I have (re-)discovered solutions for most of these issues. Let's go through them.

*From Go's perspective, Rust is invisible, the C library looks like a pure C library (and indeed it used to be 100% C++ before it got incrementally rewritten to Rust). So I will use C snippets in this article, because that's what the public C header of the library looks like.*

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

This is known to Go developers: Go does not have unions, also known as tagged unions, sum types, rich enums, etc. But Go needs to generate Go types for each C type, so that we can use them!

So, here is a C tagged union:

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

	fmt.Println(dog.anon0)
}
```

So far, so good. Let's run it (our `main.go` simply calls `app.DoStuff()`):

```sh
$ go run -a .
Dog: 42
Cat: kitty
```

Great!

Now, let's say we want to access the fields of the C tagged union. We can to have some logic based on whether our dog''s tail has a value greater than 100. How do we do that? What does the C tagged union get transformed to in Go?

```go
type _Ctype_struct___0 struct {
	kind	_Ctype_AnimalKind
	_	[4]byte
	anon0	[8]byte
}
```

So it's a struct with a `kind` field, so far so good. Then comes 4 bytes of padding, as expected (the C struct also has them). But then, we only see 8 opaque bytes. The size is correct: the C union is the size of its largest member which is 8 bytes long.

But how do we access it?



## The Go compiler does not detect changes

## Test a C function in Go tests

## Cross-compile

## False positive warnings

## Runtime checks

## Convert slices between Go and C


