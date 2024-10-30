Title: Lessons learned from a successful Rust rewrite
Tags: Rust, C++, Rewrite
---

I have written about my on-going rewrite-it-to-Rust effort at work: [1](/blog/you_inherited_a_legacy_cpp_codebase_now_what.md), [2](/blog/how_to_rewrite_a_cpp_codebase_successfully.md). And now it's finished. Let's have a look back at what worked, what didn't, and what can be done about it.

## What worked well

The rewrite was done incrementally, in a stop-and-go fashion. At some point, as I expected, we had to add brand new features while the rewrite was on-going and that was very smooth with this approach. Contrast this with the (wrong) approach of starting a new codebase from scratch in parallel, and then the feature has to be implemented twice.

The new code is much, much simpler and easier to reason about. It is roughly the same number of lines of code as the old C++ codebase, or slightly more. Some people think that equivalent Rust code will be much shorter (I have heard ratios of 1/2 or 2/3), but in my experience, it's not really the case. C++ can be incredibly verbose in some instances, but Rust also. And the C++ code will often ignore some errors that the Rust compiler forces the developer to handle, which is a good thing, but also makes the codebase slightly bigger.

Undergoing a rewrite, even a bug-for-bug one, opens many new doors in terms of performance. For example, some fields in C++ were assumed to be of a dynamic size, but we realized that they were always 16 bytes, so we stored them in a static array, thus simplifying lots of code and reducing heap allocations. That's not strictly due to Rust, it's just that having this holistic view of the codebase yields many benefits.


Related to this: we delete lots and lots of dead code. I estimate that we removed perhaps a third or half of the whole C++ codebase because it was simply never used. Some of it were half-assed features some long-gone customer asked for, and some were simply never run or even worse, never even built (they were C++ files not even present in the CMake build system). I feelthat modern programming languages such as Rust or Go are much more aggressive at flagging dead code and pestering the developer about it, which again, is a good thing.

We don't have to worry about out-of-bounds accesses and overflow/underflows with arithmetic. These were the main issues in the C++ code. Even if C++ containers have this `.at()` method to do bounds check, in my experience, most people do not use them. It's nice that this happens by default. And overflows/underflows checks are typically never addressed in C and C++ codebases.

Cross-compilation is pretty smooth, although not always, see next section.

The builtin test framework in Rust is very serviceable. All the ones I used in C++ were terrible and took so much time to even compile.

Rust is much more concerned with correctness than C++, so it sparked a lot of useful discussions. For example: oh, the Rust compiler is forcing me to check if this byte array is valid UTF8 when I try to convert it to a string. The old C++ code did no such check. Let's add this check.

## What did not work so well

This section is surpringly long and is the most interesting in my opinion. Did Rust hold its promises?


## I am still chasing undefined behavior

Doing an incremental rewrite from C/C++ to Rust, we had to use a lot of raw pointers and `unsafe{}` blocks. And even when segregating these to the entry point of the library, they proved to be a big pain in the neck.

All the stringent rules of Rust still apply inside these blocks but the compiler just stops checking them for you, so you are on your own. As such, it's so easy to introduce undefined behavior. The main rule is: `multiple read-only pointers XOR one mutable pointer`. That's what the borrow checker is always pestering you about.

But when using raw pointers, it's so easy to silently break, especially when porting C or C++ code as-is, which is mutation heavy:

```rust
fn main() {
    let mut x = 1;
    unsafe {
        let a: *mut usize = &mut x;
        let b: *mut usize = &mut x;

        *a = 2;
        *b = 3;
    }
}
```

You might think that this code is dumb and obviously wrong, but in a big real codebase, this is not so easy to spot, especially when these operations are hidden inside helper functions.

`cargo run` is content with that. But the Rust compiler can and will assume that there is only one mutable pointer to `x`, and make optimizations, and generate machine code, based on that.

The only savior here is Miri:

```sh
$ cargo +nightly-2024-09-01 miri r
error: Undefined Behavior: attempting a write access using <2883> at alloc1335[0x0], but that tag does not exist in the borrow stack for this location
 --> src/main.rs:7:9
  |
7 |         *a = 2;
  |         ^^^^^^
  |         |
  |         attempting a write access using <2883> at alloc1335[0x0], but that tag does not exist in the borrow stack for this location
  |         this error occurs as part of an access at alloc1335[0x0..0x8]
  |
  [...]
 --> src/main.rs:4:29
  |
4 |         let a: *mut usize = &mut x;
  |                             ^^^^^^
help: <2883> was later invalidated at offsets [0x0..0x8] by a Unique retag
 --> src/main.rs:5:29
  |
5 |         let b: *mut usize = &mut x;
  |                             ^^^^^^
  [...]
```

So, what could have been a compile time error, is now a runtime error. Great. I hope you have 100% test coverage! Thank god there's Miri. 

If you are writing `unsafe{}` code without Miri checking it, or if you do so without absolutely having to, I think this is foolish. It will blow up in your face.

Miri is awesome. But...

## Miri does not always work

I am not talking about some parts of Miri that are experimental. Or the fact that running code under Miri is excruciantingly slow. Or the fact that Miri only works in `nightly`.

No, I am talking about code that Miri cannot run, period:

```
    |
471 |     let pkey_ctx = LcPtr::new(unsafe { EVP_PKEY_CTX_new_id(EVP_PKEY_EC, null_mut()) })?;
    |                                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ can't call foreign function `␁aws_lc_0_16_0_EVP_PKEY_CTX_new_id` on OS `linux`
    |
    = help: if this is a basic API commonly used on this target, please report an issue with Miri
    = help: however, note that Miri does not aim to support every FFI function out there; for instance, we will not support APIs for things such as GUIs, scripting languages, or databases
```

If you are using a library that has parts written in C or assembly, which is usual for cryptography libraries, or video compression, etc, you are out of luck.

So we resorted to add a feature flag to split the codebase between parts that use this library and parts that don't. And miri only runs tests with the feature disabled. 

That means that there is a lot of `unsafe` code that is simply not being checked right now. Bummer.

Perhaps there could be a fallback implementation for these libraries that's entirely implemented in software (and in pure Rust). But that's not really feasible for most libraries to maintain two implementations just for Rust developers.

I resorted to run the problematic tests in `valgrind`, like I used to do with pure C/C++ code. It does not detect many things that Miri would, for example having more than one mutable pointer to the same value, which is perfectly fine in C/C++/Assembly, but not Rust.


## I am still chasing memory leaks


Our library offers a C API, something like this:

```
int handle = 0;
MYLIB_init(&handle);

// Do some stuff with the handle...

MYLIB_release(handle);
```

Under the hood, `MYLIB_init` allocates some memory and `MYLIB_release()` frees it. This is a very usual pattern in C libraries, e.g. `curl_easy_init()/curl_easy_cleanup()`.

So immediately, you are thinking: well, it's easy to forget to call `MYLIB_release` in some codepaths, and thus leak memory. And you'd be right. So let's implement something like the curl API to illustrate. We are good principled developers so we write a Rust test for the new implementations:

```rust
#[no_mangle]
pub extern "C" fn MYLIB_init() -> *mut std::ffi::c_void {
    let alloc = Box::leak(Box::new(1usize));

    alloc as *mut usize as *mut std::ffi::c_void
}

#[no_mangle]
pub extern "C" fn MYLIB_do_stuff(_handle: *mut std::ffi::c_void) {
    // Do some stuff.
}

#[no_mangle]
pub extern "C" fn MYLIB_release(handle: *mut std::ffi::c_void) {
    let _ = unsafe { Box::from_raw(handle as *mut usize) };
}

fn main() {}

#[cfg(test)]
mod test {
    #[test]
    fn test_init_release() {
        let x = super::MYLIB_init();

        super::MYLIB_do_stuff(x);

        super::MYLIB_release(x);
    }
}
```

That looks good, right? Straight from a textbook. Unfortunately, the reality is a bit more complex. In a hybrid C++/Rust codebase, some entities having their allocation function in Rust and their deallocation function in C++, not migrated yet. So you can't easily write a `Drop` function in Rust to automatically cleanup the resources by calling the C++ function. That's because the C++ library links against the Rust library, and to make the `Drop` work, it would require linking the Rust tests against the C++ library, which we really did not want to get into. Additionally, we wanted to write our tests using the public C API of the library like a normal C application would, and it would not have access to this Rust feature.

And often, there is complicated logic with lots of code paths, and we need to ensure that the cleanup is always called. In C, this is typically done with `goto` to an `end:` label that always cleans up the resources. But Rust does not support this form of `goto`.

So we solved it with the [defer](https://docs.rs/scopeguard/latest/scopeguard/) crate in Rust and implementing a [defer](https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/) statement in C++.

However, the Rust borrow checker really does not like the `defer` pattern. Typically, a cleanup function will take as its argument as `&mut` reference and that precludes the rest of the code to also store and use a second `&mut` reference to the same value. So we could not always use `defer` on the Rust side.
