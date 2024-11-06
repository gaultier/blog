Title: Perhaps Rust needs "defer"
Tags: Rust, C
---

In a previous article I [mentioned](/blog/lessons_learned_from_a_successful_rust_rewrite.html#i-am-still-chasing-memory-leaks) that we use the `defer` idiom in Rust through a crate, but that it actually rarely gets past the borrow checker. Some comments were <s>claiming this issue does not exist</s> surprised and I did not have an example at hand.

Well, today at work I hit this issue again so I thought I would document it. And the whole experience showcases well how working in Rust with lots of FFI interop feels like.

## Setting the stage

So, I have a Rust API like this:

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

It allocates and returns an dynamically allocated array as a pointer and a length. Of course in reality, `Foo` has many fields and the values are not known in advance but what happens is that we send messages to a Smartcard to ask it to send us a piece of data residing on it, and it replies with some encoded messages that our library decodes and returns to the user.

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


> If we feel fancy (and non-portable), we can even automate the freeing of the memory in C with `__attribute(cleanup)`, like `defer` (ominous sounds). But let's not, today. Let's focus on the Rust side.


*This code has a subtle mistake (can you spot it?), so keep on reading.*

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

## First attempt to free the memory properly

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

Hmm...ok...Well that's a bit weird, because what Rust does, when the `Vec` is allocated, is to call out to `malloc` from libc, as we can see with `strace`:

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

*Depending on your system, the call stack and specific system call may vary. It depends on the libc implementation, but point being, `malloc` from libc gets called by Rust.*

Note the irony that we do not need to have a third-party dependency on the `libc` crate to allocate with `malloc` (being called under the hood), but we do need it, in order to deallocate the memory with `free`. Perhaps it's by design. Anyway. Where was I.


The docs for `Vec` indeed state:

> In general, Vec’s allocation details are very subtle — if you intend to allocate memory using a Vec and use it for something else (either to pass to unsafe code, or to build your own memory-backed collection), be sure to deallocate this memory by using from_raw_parts to recover the Vec and then dropping it.

But a few sentences later it also says:

> That is, the reported capacity is completely accurate, and can be relied on. It can even be used to manually free the memory allocated by a Vec if desired.

So now I am confused, am I allowed to `free()` the `Vec`'s pointer directly or not?

By the way, we also spot in the same docs that there was no way to correctly free the `Vec` by calling `free()` on the pointer without knowing the capacity because:

> The pointer will never be null, so this type is null-pointer-optimized. However, the pointer might not actually point to allocated memory. 

Hmm, ok... So I guess the only way to not trigger Undefined Behavior on the C side when freeing, would be to keep the `capacity` of the `Vec` around and do:

```c
  if (capacity > 0) {
    free(foos);
  }
```

Let's ignore for now that this will surprise every C developer out there that has been doing `if (NULL != ptr) free(ptr)` for decades.


Let's stay on the safe side and assume that we ought to use `Vec::from_raw_parts` and let the `Vec` free the memory when it gets dropped at the end of the scope. The only problem is: This function requires the pointer, the length, *and the capacity*. Wait, but we lost the capacity when we returned the pointer + length to the caller in `MYLIB_get_foos()`, and the caller *does not care one bit about the capacity*! It's irrelevant to them! At work, the mobile developers using our library rightfully asked: wait, what is this `cap` field? Why do I care? What do I do with it? If you are used to manually managing your own memory, this is a very old concept, but if you are used to a Garbage Collector, it's very much new.

## Second attempt to free the memory properly

So, let's first try to dodge the problem the <s>hacky</s> simple way by pretending that the memory is allocated by a `Box`, which only needs the pointer, just like `free()`:

```rust
    #[test]
    fn test_get_foos() {
        ...

        unsafe {
            let _ = Box::from_raw(foos);
        }
    }
```

That's I think the first instinct for a C developer. Whatever way the memory was heap allocated, be it with `malloc`, `calloc`, `realloc`, be it for one struct or for a whole array, we want to free it with one call, passing it the base pointer. Let's ignore for a moment the docs that state that sometimes the pointer is heap-allocated and sometimes not.

So this Rust code builds. The test passes. And Miri is unhappy. I guess you know the drill by now:

```sh
$ cargo +nightly miri test
...
 incorrect layout on deallocation: alloc59029 has size 16 and alignment 8, but gave size 8 and alignment 8
...
```

Let's take a second to marvel at the fact that Rust, probably the programming language the most strict at compile time, the if-it-builds-it-runs-dude-I-swear language, seems to work at compile time and at run time, but only fails when run under an experimental analyzer that only works in nightly and does not support lots of FFI patterns.

That's the power of Undefined Behavior and `unsafe{}`. Again: audit all of your `unsafe` blocks, and be very suspicious of any third-party code that uses `unsafe`. I think Rust developers on average do not realize the harm that it is very easy to inflict to your program by using `unsafe` unwisely even if everything seems fine.

Anyways, I guess we have to refactor our whole C API to do it the Rust Way(tm)! 


## Third attempt to free the memory properly

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

Let's also re-generate the C header, adapt the C code, rebuild it, communicate with the various projects that use our C API to make them adapt, etc...

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

## Defer

The test is trivial right now but in real code, there are many code paths that sometimes allocate, sometimes not, with validation interleaved, and early returns, so we'd really like if we could statically demonstrate that the memory is always correctly freed. To ourselves, to auditors, etc.

One example at work of such hairy code is: building a linked list (in Rust), fetching more from the network based on the content of the last node in the list, and appending the additional data to the linked list, until some flag is detected in the encoded data. Oh, and there is also validation of the incoming data, so you might have to return early with a partially constructed list which should be properly cleaned up.

And there are many such examples like this, where the memory is often allocated/deallocated with a C API and it's not always possible to use RAII. So `defer` comes in handy.

---

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


Dum dum duuuum....Yes, we cannot use the `defer` idom here (or at least I did not find a way). In some cases it's possible, in lots of cases it's not. The borrow checker considers that the `defer` block holds an exclusive mutable reference and the rest of the code cannot use that reference in any way.

Despite the fact, that the version without defer, and with defer, are semantically equivalent and the borrow checker is fine with the former and not with the latter.

## Possible solutions

So that is why I argue that Rust should get a `defer` statement in the language and the borrow checker should be made aware of this construct to allow this approach to take place.

But what can we do otherwise? Are there any alternatives?

- We can be very careful and make sure we deallocate everything by hand in every code paths. Obivously that doesn't scale to team size, code complexity, etc. And it's unfortunate since using a defer-like approach in C with `__attribute(cleanup)` and in C++ by implementing our [own](https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/) `defer` is trivial.
- We can use a goto-like approach, as a reader [suggested](https://lobste.rs/s/n6gciw/lessons_learned_from_successful_rust#c_8pzmqg) in a previous article, even though Rust does not have `goto` per se:
    ```rust
    fn foo_init() -> *mut () { &mut () }
    fn foo_bar(_: *mut ()) -> bool { false }
    fn foo_baz(_: *mut ()) -> bool { true }
    fn foo_free(_: *mut ()) {}

    fn main() {
      let f = foo_init();
      
      'free: {
        if foo_bar(f) {
            break 'free;
        }
        
        if foo_baz(f) {
            break 'free;
        }
        
        // ...
      };
      
      foo_free(f);
    }
    ```
    It's very nifty, but I am not sure I would enjoy reading and writing this kind of code everywhere, especially with multiple levels of nesting. Again, it does not scale very well. But it's something.
- We can work-around the borrow-checker to still use `defer` by refactoring our code to make it happy. Again, tedious and not always possible. One thing that possibly works is using handles (numerical ids) instead of pointers, so that they are `Copy` and the borrow checker does not see an issue with sharing/copying them. Like file descriptors work in Unix. The potential downside here is that it creates global state since some component has to bookkeep these handles and their mapping to the real pointer. But it's a [common](https://floooh.github.io/2018/06/17/handles-vs-pointers.html) pattern in gamedev.
- Perhaps the borrow checker can be improved upon without adding `defer` to the language, 'just'(tm) by making it smarter?
- We can use arenas everywhere and sail away in the sunset, leaving all these nasty problems behind us
- Rust can stabilize various nightly APIs and tools, like custom allocators and sanitizers, to make development simpler


## Conclusion

Rust + FFI is nasty and has a lot of friction. I went at work through all these steps I went through in this article, and this happens a lot. 

The crux of the issue is that there is a lot of knowledge to keep in your head, lots of easy ways to shoot yourself in the foot, and I have to reconcile what various tools tell you: even if the compiler is happy, the tests might not be. Even the tests are happy, Miri might not be. Even if I think I have done the right thing, I discover buried deep in the docs that in fact I didn't. 

This should not be so hard!

