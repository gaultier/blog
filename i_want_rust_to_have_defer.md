Title: I want Rust to have "defer"
Tags: Rust, C
---

In a previous article I [mentioned](/blog/lessons_learned_from_a_successful_rust_rewrite.html#i-am-still-chasing-memory-leaks) that we use the `defer` idiom in Rust through a crate, but that it actually rarely gets past the borrow checker. Some comments were <s>doubtful</s> surprised and I did not have an example at hand.

Well, today I hit this issue again so I thought I would document it. 

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

I can now use it from C so:

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



Now, we are principled developers who test their code (right?). So let's write a Rust test for it. We expect it to be exactly the same as the C code:

```rust
#[cfg(test)]
mod tests {
    #[test]
    fn test_get_foos() {
        let mut foos = std::ptr::null_mut();
        let mut foos_count = 0;
        assert_eq!(0, super::MYLIB_get_foos(&mut foos, &mut foos_count));
    }
}
```

And it passes:

```sh
$ cargo test
...
running 1 test
test tests::test_get_foos ... ok
...
```

Of course, we have not yet freed anything, so we expect Miri to complain, and it does:

```sh
$ cargo +nightly miri test
...
error: memory leaked: alloc59029 (Rust heap, size: 16, align: 8), allocated here:
...
```

Great, so let's free it at the end of the test, like C does, with `free` from libc, which we add as a dependency:

```rust

    #[test]
    fn test_get_foos() {
        ..

        if !foos.is_null() {
            unsafe { libc::free(foos as *mut std::ffi::c_void) };
        }
    }
```

The test passes, great. Let's try with Miri:

```sh
$ cargo +nightly miri test
...
 error: Undefined Behavior: deallocating alloc59029, which is Rust heap memory, using C heap deallocation operation
...
```

Hmm...ok...Well that's a bit weird because what Rust does when the `Vec` is allocated, is to call out to `malloc` from libc, as we can see with `strace`:

```sh
$ strace -k -v -e brk ./a.out
...
brk(0x213c0000)                         = 0x213c0000
 > /usr/lib64/libc.so.6(brk+0xb) [0x10fa9b]
 > /usr/lib64/libc.so.6(__sbrk+0x6b) [0x118cab]
 > /usr/lib64/libc.so.6(__default_morecore@GLIBC_2.2.5+0x15) [0xa5325]
 > /usr/lib64/libc.so.6(sysmalloc+0x57b) [0xa637b]
 > /usr/lib64/libc.so.6(_int_malloc+0xd39) [0xa7399]
 > /usr/lib64/libc.so.6(tcache_init.part.0+0x36) [0xa7676]
 > /usr/lib64/libc.so.6(__libc_malloc+0x125) [0xa7ef5]
 > /home/pg/scratch/rust-blog2/a.out(alloc::alloc::alloc+0x6a) [0x4a145a]
 > /home/pg/scratch/rust-blog2/a.out(alloc::alloc::Global::alloc_impl+0x140) [0x4a15a0]
 > /home/pg/scratch/rust-blog2/a.out(alloc::alloc::exchange_malloc+0x3a) [0x4a139a]
 > /home/pg/scratch/rust-blog2/a.out(MYLIB_get_foos+0x26) [0x407cc6]
 > /home/pg/scratch/rust-blog2/a.out(main+0x2b) [0x407bfb]
```

Note the irony that we do not need to have a third-party dependency on the `libc` crate to allocate with `malloc` being called under the hood, but we do need it to free the memory with `free`. Anyway. Where was I.

Right, Rust wants to free the memory it allocated. Ok. Let's do that I guess. 

The only problem is that to do so properly, we ought to use `Vec::from_raw_parts` and let the `Vec` free the memory when it gets dropped at the end of the scope. The only problem is: This function requires the pointer, the length, *and the capacity*. Wait, but we lost the capacity when we returned the pointer + length to the caller in `MYLIB_get_foos()`, and the caller *does not care one bit about the capacity*! It's irrelevant to them! At work, the mobile developers using our library rightfully asked: wait, what is this `cap` field? Why do I care? 

So, let's first try to dodge the problem the <s>hacky</s> easy way by pretending that the memory is allocated by a `Box`, which only needs the pointer, just like `free()`:

```rust
    #[test]
    fn test_get_foos() {
        ...

        if !foos.is_null() {
            unsafe {
                let _ = Box::from_raw(foos);
            }
        }
    }
```

It builds. The test passes. And Miri is unhappy. I guess you know the drill by now:

```sh
$ cargo +nightly miri test
...
 incorrect layout on deallocation: alloc59029 has size 16 and alignment 8, but gave size 8 and alignment 8
...
```

Let's take a second to marvel at the fact that Rust, probably the programming language the most strict at compile time, the if-it-builds-it-runs-dude-I-swear language, seems to work at compile time and at run time, but only fails when run under an experimental analyzer that only works in nightly and does not support lots of FFI patterns. Anyways, I guess we have to refactor our whole API! 

So, in our codebase at work, we have defined this type:

```rust
/// Owning Array i.e. `Vec<T>` in Rust or `std::vector<T>` in C++.
#[repr(C)]
pub struct OwningArrayC<T> {
    pub data: *mut T,
    pub len: usize,
    pub cap: usize,
}
```

It clearly signifies to the caller that they are in charge of freeing the memory, and also it carries the capacity of the `Vec` with it, so it's not lost.

In our project, this struct is used a lot.

So let's adapt the function, and also add a function in the API to free it for convenience:

```rust
#[no_mangle]
pub extern "C" fn MYLIB_get_foos(out_foos: &mut OwningArrayC<Foo>) -> i32 {
    let res = vec![Foo { value: 42 }, Foo { value: 99 }];
    let len = res.len();
    let cap = res.capacity();

    *out_foos = OwningArrayC {
        data: res.leak().as_mut_ptr(),
        len,
        cap,
    };
    0
}

#[no_mangle]
pub extern "C" fn MYLIB_free_foos(foos: &mut OwningArrayC<Foo>) {
    if !foos.data.is_null() {
        unsafe {
            let _ = Vec::from_raw_parts(foos.data, foos.len, foos.cap);
        }
    }
}
```

Let's also re-generate the C header, adapt the C code, rebuild it, etc...

Back to the Rust test:

```rust
#[cfg(test)]
mod tests {
    #[test]
    fn test_get_foos() {
        let mut foos = crate::OwningArrayC {
            data: std::ptr::null_mut(),
            len: 0,
            cap: 0,
        };
        assert_eq!(0, super::MYLIB_get_foos(&mut foos));
        println!("foos: {}", foos.len);
        super::MYLIB_free_foos(&mut foos);
    }
}
```

And now, Miri is happy. Urgh. So, back to what we set out to do originally, `defer`.

Let's use the `scopeguard` crate which provides a `defer!` macro, in the test, to automatically free the memory:

```rust
    #[test]
    fn test_get_foos() {
        let mut foos = crate::OwningArrayC {
            data: std::ptr::null_mut(),
            len: 0,
            cap: 0,
        };
        assert_eq!(0, super::MYLIB_get_foos(&mut foos));
        defer! {
            super::MYLIB_free_foos(&mut foos);
        }

        println!("foos: {}", foos.len);
    }
```

And we get a compile error:

```sh
$ cargo test
error[E0502]: cannot borrow `foos.len` as immutable because it is also borrowed as mutable
  --> src/lib.rs:54:30
   |
50 | /         defer! {
51 | |             super::MYLIB_free_foos(&mut foos);
   | |                                         ---- first borrow occurs due to use of `foos` in closure
52 | |         }
   | |_________- mutable borrow occurs here
53 |
54 |           println!("foos: {}", foos.len);
   |                                ^^^^^^^^ immutable borrow occurs here
55 |       }
   |       - mutable borrow might be used here, when `_guard` is dropped and runs the `Drop` code for type `ScopeGuard`
   |
```


Dum dum duuuum....Yes, we cannot use the `defer` idom here (or at least I did not find a way). In some cases it's possible, in lots of cases it's not. Despite the version without defer and with defer being equivalent and the borrow checker being fine with the former and not with the latter.

So that is why I argue that Rust should get a `defer` statement in the language and the borrow checker should be made aware of this construct to allow this approach to take place.

And that's irrespective of the annoying constraints around freeing memory that Rust has allocated. Or that the code builds and runs fine even though it is subtly flawed.
