Title: I want Rust to have "defer"
Tags: Rust, C
---

In a previous article I [mentioned](/blog/lessons_learned_from_a_successful_rust_rewrite.html#i-am-still-chasing-memory-leaks) that we use the `defer` idiom in Rust through a crate, but that it actually rarely gets past the borrow checker. Some comments were <s>doubtful</s> surprised and I did not have an example at hand.

Well, today I hit this issue again so I thought I would document it. 

## The situation

I have a Rust API like this:

```rust
#[repr(C)]
pub struct Foo {
    value: usize,
}

#[no_mangle]
pub extern "C" fn MYLIB_get_foos(out_foos: *mut *mut Foo, out_foos_count: &mut usize) -> i32 {
    let res = vec![Foo { value: 42 }, Foo { value: 99 }];
    *out_foos_count = res.len();
    unsafe { *out_foos = res.leak().as_mut_ptr() };
    0
}
```

It allocates and returns an dynamically allocated array as a pointer and a length. Of course in reality, `Foo` has many fields and the values are not known in advance but decoded from the network.

I tell Cargo this is a static library:

```toml
# Cargo.toml

[lib]
crate-type = ["staticlib"]
```

It's a straightforward API, so I generate the corresponding C header with cbindgen: 

```sh
$ cbindgen -v src/lib.rs --lang=c -o mylib.h
```

And I get:

```c
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct Foo {
  uintptr_t value;
} Foo;

int32_t MYLIB_get_foos(struct Foo **out_foos, uintptr_t *out_foos_count);

```

use it from C so:

```c
#include "mylib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  Foo *foos = NULL;
  size_t foos_count = 0;
  assert(0 == MYLIB_get_foos(&foos, &foos_count));

  for (size_t i = 0; i < foos_count; i++) {
    printf("%lu\n", foos[i].value);
  }

  if (NULL != foos) {
    free(foos);
  }
}
```

I build it with all the warnings enabled, run it with sanitizers on, and/or in valgrind, all good.


> If I feel fancy (and non-portable), I can even automate the freeing of the memory in C with `__atribute(cleanup)`, like `defer` (ominous sounds). But let's not, today. Let's focus on the Rust side.



Now, we are principled developers who test their code, right? So let's write a Rust test for it:

```rust

```
