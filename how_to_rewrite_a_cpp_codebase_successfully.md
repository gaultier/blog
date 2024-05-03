# How to rewrite a C++ codebase successfully

I recently wrote about [inheriting a legacy C++ codebase](/blog/you_inherited_a_legacy_cpp_codebase_now_what.html). At some point, although I cannot pinpoint exactly when, a few things became clear to me:

- No one in the team but me is able - or feels confident enough - to make a change in this codebase
- This is a crucial project for the company and will live for years if not decades
- The code is pretty bad on all the criteria we care about: correctness, maintainability, security, you name it. I don't blame the original developers, they were understaffed and it was written as a prototype (the famous case of the prototype which becomes the production code).
- No hiring of C++ developers is planned

So it was apparent that sticking with C++ was a dead end. The only solution would be to train everyone in the team on C++ and dedicate a significant amount of time rewriting the most problematic parts of the codebase to perhaps reach a good enough state. It's a judgement call in the end, but that seemed to be more effort than 'simply' introducing a new language and doing a rewrite.

I don't actually like the term 'rewrite'. Folks on the internet will eagerly repeat that rewrites are a bad idea, will indubtedly fail, and are a sign of hubris and naivity. I have experienced such rewrites, from scratch, and yes that does not end well.

However, I claim, because I've done it, and many others before me, that an **incremental** rewrite can be successful, and is absolutely worth it. It's all about how it is being done, so here's how I proceeded and I hope it can be applied in other cases, and people find it useful.

I think it's a good case study because whilst not a big codebase, it is a complex codebase, and it's used in production on 10+ different operating systems and architectures, including by external customers. This is not a toy. 

So join me on this journey, here's the guide to rewrite a C++ codebase successfully. And also what not do!


## The project

This project is a library that exposes a C API but the implementation is C++. The final artifacts are a `libfoo.a` static library and a `libfoo.h` C header. It is used to talk to applets on a [smart card](https://en.wikipedia.org/wiki/Smart_card) like your credit card, ID, passport or driving license (yes, smart cards are nowadays everywhere - you probably carry several on you right now), since they use a ~~bizzarre~~ interesting communication protocol. The library also implements a home-grown protocol on top of the well-specified smart card protocol, encryption, and business logic.

This library is used in:

- Android applications, through JNI
- Go back-end services running in the Kubernetes, through CGO
- iOS applications, through Swift FFI
- C and C++ applications running on very small 32 bits ARM boards similar to the first Raspberry Pi

Additionally, developers are using macOS (x64 and arm64) and Linux so the library needs to build and run on these platforms.

Since external customers also integrate their applications with our library and we do not control these environments, we also need to work with either the glibc and the musl C libraries, as well a clang and gcc, and expose a C89-ish API, to maximize compatibility.

Alright, now that the stage is set, let's go through the steps of rewriting this project.

## Improve the existing codebase

That's basically all the steps in [Inheriting a legacy C++ codebase](/blog/you_inherited_a_legacy_cpp_codebase_now_what.html). We need to start the rewrite with a codebase that builds and runs on every platform we support, with tests passing, and a clear README explaining how to setup the project locally. This is a small investment (1 or 2 weeks) that will pay massive dividends in the future. 

But I think the most important point is to trim all the unused code which is typically the majority of the codebase! No one wants to spend time and effort on rewriting completely unused code.

Additionally, if you fail to convince your team and the stakeholders to do the rewrite, you at least have improved the codebase you are now stuck with.

## Get buy-in

Same as in my previous article: Buy-in from teammates and stakeholders is probably the most important thing to get, and maintain. 

It's a big investment in time and thus money we are talking about, it can only work with everyone on board.

Here I think the way to go is showing the naked truth and staying very factual, in terms managers and non-technical people can understand. This is roughly what I presented:

- The bus factor for this project is 1
- Tool X shows that there are memory leaks at the rate of Y MiB/hour which means the application using our library will be OOM killed after around Z hours.
- Quick and dirty fuzzing manages to make the library crash 133 times in 10 seconds
- Linter X detects hundreds of real issues we need to fix
- All of these points make it really likely a hacker can exploit our library to gain Remote Code Execution (RCE) or steal secrets

Essentially, it's a matter of genuinely presenting the alternative of rewriting being cheaper in terms of time and effort compared to improving the project with pure C++. If your teammates and boss are rationale, it should be a straightforward decision.


After the problematic situation has been presented, I think at least 3 different solutions should be presented and compared (including sticking with pure C++), and seriously consider each option.

Ideally, if time permits, a small prototype for the preferred solution should be done, to confirm or infirm early that it can work, and to eliminate doubts. It's a much more compelling argument to say: "Of course it will work, here is prototype I made!" compared to "I hope it will work, but who knows, oh well I guess we'll see...".


After much debate, we settled on Rust as the new programming language being introduced into the codebase. It's important to note that I am not a Rust diehard fan. I appreciate the language but it's not a perfect language, it has issues, it's just that it solves all the issues we have in this project, especially considering the big focus on security (since we deal with payments),  the relative similarity with the company tech stack (Go), and the willingness of the team to learn it and review code in it.

After all, the goal is also to gain additional developers, and stop being the only person who can even touch this code.

I also seriously considered Go, but after doing a prototype, I was doubtful the many limitations of CGO would allow us to achieve the rewrite.


## Preparing to introduce the new language

Once I reached this point, I created a Git tag `last-before-rust` (spoiler alert!). The commit right after introduced the first lines of code in the new language.

This proved invaluable, because when rewriting the legacy code, I found tens of bugs lying around, and I think that's very typical. Also, this rewriting effort requires time, during which other team members or external customers may report bugs they just found.

Every time such a bug appeared, I switched to this Git tag, and tried to reproduce the bug. Almost every time, the bug was already present before the rewrite. That's a very important information (for me, it was a relief!) for solving the bug, and also for stakeholders. That's the difference in their eye between: We are improving the product by fixing long existing bugs; or: we are introducing new bugs with our risky changes and we should maybe stop the effort completely because it's harming the product.

Stakeholder support is a really big deal. Be prepared to repeat many many times the decision process that led to the rewrite to your boss, your boss's boss, the odd product manager who's not technical, the salesperson supporting the external customers, etc. It's important to nail the elevator's pitch.

## Incremental rewrite

Along with stakeholder buy-in, the most important point in the article is that only an **incremental** rewrite can succeed, in my opinion. Rewriting from scratch is bound to fail, I think. At least I have never seen it succeed, and have seen it fail many times.

What does it mean, very pragmatically? Well it's just a few rules of thumb:

- A small component is picked to be rewritting, the smallest, the better. Ideally it is as small as one function, or one class.
- The new implementation is written in the same Git (or whatever CVS you use) repository as the existing code, alongside it. It's a 'bug for bug' implementation which means it does the exact same thing as the old implementation, even if the old seems sometimes non-sensical.
- Tests for the new implementation are written and pass (so that we know the new implementation is likely correct)
- Each call site calling the function/class is switched to using the new implementation. After each replacement, the test suite is run and passes (so that we know that nothing broke at the scale of the project; a kind of regression testing). The change is committed. That way is something breaks, we know exactly which change is the culprit.
- A small PR is opened, reviewed and merged. Since our changes are anyways incremental, it's up to us to decide that the current diff is of the right size for a PR. We can make the PR as big or small as we want. We can even make a PR with only the new implementation that's not yet used at all.
- Once the old function/class is not used anymore by any code, it can be 'garbage-collected' i.e. safely removed. This can even be its own PR depending on the size.
- Rinse and repeat until all of the old code has been replaced

There are of course thornier cases which we'll explore in more detail later, but that's the gist of it. What's crucial is that each commit on the main branch builds and runs fine. At not point the codebase is every broken, does not build, or is in an unknown state.


It's actually not much different from the way I do a refactor in a codebase with just one programming language.

What's very important to avoid are big PRs that are thousands lines long and nobody wants to review them, or long running branches that effectively create a multiverse inside the codebase. It's the same as regular software development, really.

Here are a few additional tips I recommend doing:

- Port the code comments from the old code to the new code if they make sense and add value
- If you can use automated tools (search and replace, or tools operating at the AST level) to change every call site to use the new implementation, it'll make your reviewers very happy, and save you hours and hours of debugging because of a copy-paste mistake
- Since Rust and C++ basically only can communicate through a C API (I am aware of experimental projects to make them talk directly but we did not use those), it means that each Rust function must be accompanied by a corresponding C function signature, so that C++ can call it as a C function. I recommend automating this process with [cbindgen](https://github.com/mozilla/cbindgen). I have encountered some limitations with it but it's very useful, especially to keep the implementation (in Rust) and the API (in C) in sync, or if your teammates are not comfortable with C. I added the call to `cbindgen` to CMake so that rebuilding the C++ project would automatically run `cbindgen`.
- When rewriting a function/class, port the tests for this function/class to the new implementation to avoid reducing the code coverage each time
- Make the old and the new test suites fast so that the iteration time is shorty
- When a divergence is detected (a difference in output or side effects between the old and the new implementation), observe with tests or within the debugger the output of the old implementation (that's where the Git tag comes handy) in detail so that you can correct the new implementation. Some people even develop big test suites verifying that the output of the old and the new implementation are exactly the same.
- Since it's a bug-for-bug rewrite, *what* the new implementation does may seem weird or unnecessarily convulated. However, *how* it does it should be up to the best software engineering standards, that means tests, fuzzing, documentation, etc.

Finally, there is one hidden advantage of doing an incremental rewrite. A from-scratch rewrite is all or nothing, if it does not fully complete and replace the old implementation, it's useless and waste. However, an incremental rewrite is immediately useful, may be pause and continued a number of times, and even if the funding gets cut short and it never fully completes, it's still a clear improvement over the starting point.


## Fuzzing

I am a fan a fuzzing, it's great. Almost every time I fuzz some code, I find an corner case I did not think about, especially when doing parsing.

I added fuzzing to the project so that every new Rust function is fuzzed. I initially used [AFL](https://rust-fuzz.github.io/book/afl.html) but then turned to [cargo-fuzz](https://rust-fuzz.github.io/book/cargo-fuzz.html), and I'll explain why.

Fuzzing is only useful if code coverage is [high](https://blog.trailofbits.com/2024/03/01/toward-more-effective-curl-fuzzing/). The worst that can happen is to dedicate serious time to setup fuzzing, to only discover at the end that the same few branches are always taken during fuzzing.

Coverage can only be improved if developers can easily see exactly which branches are being executed during fuzzing. And I could not find an easy way with AFL to get a hold on that data.

Using `cargo-fuzz` and various LLVM tools, I wrote a small shell script to visualize exactly which branches are taken during fuzzing as well as the code coverage in percents for each file and for the project as a whole (right now it's at around 90%).

To get to a high coverage, the quality of the corpus data is paramount, since fuzzing works by doing small mutations of this corpus and observing which branches are taken as a result.

I realized that the existing tests in C++ had lots of useful data in them, e.g.:

```c++
const std::vector<char> input = {0x32, 0x01, 0x49, ...}; // <= This is the interesting data.
assert(foo(input) == ...);
```

So I had the idea of extracting all the `input = ...` data from the tests to build a good fuzzing corpus. My first go at it was a poor man's quick and dirty C++ lexer. It worked but it was clunky. Right after I finished it, I thought: why don't I use `tree-sitter` to properly parse C++? 

And so I did, and it turned out great, just 300 lines of Rust walking through each `TestXXX.cpp` file in the repository and using tree-sitter to extract each pattern. I used the query language of tree-sitter to do so: 

```rust
let query = tree_sitter::Query::new(
    tree_sitter_cpp::language(),
    "(initializer_list (number_literal)+) @capture",
)
```

The tree-sitter website thankfully has a playground where I could experiment and tweak the query and see the results live.


As time went on and more and more C++ tests were migrated to Rust tests, it was very easy to extend this small Rust program that builds the corpus data, to also scan the Rust tests!

A typical Rust test would look like this:


```rust
const INPUT: [u8; 4] = [0x01, 0x02, 0x03, 0x04]; // <= This is the interesting data.
assert_eq!(foo(&INPUT), ...);
```

And the query to extract the interesting data would be:

```rust
let query = tree_sitter::Query::new(
    tree_sitter_rust::language(),
    // TODO: Maybe make this query more specific with:
    // `(let_declaration value: (array_expression (integer_literal)+)) @capture`.
    // But in a few cases, the byte array is defined with `const`, not `let`.
    "(array_expression (integer_literal)+) @capture",
)
```

However I discovered that not all data was successfully extracted. What about this code:

```rust
const BAR : u8 = 0x42;
const INPUT: [u8; 4] = [BAR, 0x02, 0x03, 0x04]; // <= This is the interesting data.
assert_eq!(foo(&INPUT), ...);
```

We have a constant `BAR` which trips up tree-sitter, because it only see a literal and does not know its value.

The way I solved this issue was to do two passes: once to collect all constants along with their values in a map, and then a second pass to find all arrays in tests:

```rust
let query = tree_sitter::Query::new(
    tree_sitter_rust::language(),
    "(const_item value: (integer_literal)) @capture ",
)
```

So that we can then resolve the literals to their numeric value.

I am pretty happy with how this turned out, scanning all C++ and Rust files to find interesting test data in them to build the corpus. I think this was key to move from the initial 20% code coverage with fuzzing (using a few hard-coded corpus files) to 90%. It's fast too.

Also, it means the corpus gets better each time we had a test (be it in C++ or Rust), for free.

Does it mean that the corpus will grow to an extreme size? Well, worry not, because LLVM comes with a fuzzing corpus minimizer:

```
# Minimize the fuzzing corpus (in place).
cargo +nightly fuzz cmin [...]
```

For each file in the corpus, it feeds it as input to our code, observes which branches are taken, and if a new set of branches is taken, this file remains (or perhaps gets minimized even more, not sure how smart this tool is). Otherwise it is deemed a duplicate and is trimmed.

So:

1. We generate the corpus with our program
2. Minimize it
3. Run the fuzzing for however long we wish. It runs in CI for every commit and developers can also run it locally.
4. When fuzzing is complete, we print the code coverage statistics


##  Memory management and unsafety

Writing Rust has been a joy, even for more junior developers in the team. Pure Rust code was pretty much 100% correct on the first try.

However we had to use `unsafe {}` blocks in the FFI layer. We segregated all the FFI code to one file, and converted the C FFI structs to Rust idiomatic structs as soon as possible, so that the bulk of the Rust code can be idiomatic and safe.

But that means this FFI code is the most likely part of the Rust code to have bugs. To get some confidence in its correctness, we write Rust tests using the C FFI functions (as if we were a C consumer of the library) running under [Miri](https://github.com/rust-lang/miri) which acts as valgrind essentially, simulating a CPU and checking that our code is memory safe. Tests run perhaps 5 to 10 times as slow as without Miri but this has proven invaluable since it detected many bugs ranging from alignment issues to memory leaks and use-after-free issues.

We run tests under Miri in CI to make sure each commit is reasonably safe.

So beware: introducing Rust to a C or C++ codebase may actually introduce new memory safety issues in the FFI code.

Thankfully it's much shorter and simpler than the rest of the codebase.

## C FFI in Rust is cumbersome

The root cause for all these issues is that the C API that C++ and Rust use to call each other is very limited in its expresiveness w.r.t ownership, as well as many Rust types not being marked `#[repr(C)]`, even types you would expect to, such as `Option`, `Vec` or `&[u8]`. That means that you have to define your own equivalent types:

```rust
#[repr(C)]
// An option type that can be used from C
pub struct OptionC<T> {
    pub has_value: bool,
    pub value: T,
}


#[repr(C)]
// Akin to `&[u8]`, for C.
pub struct ByteSliceView {
    pub ptr: *const u8,
    pub len: usize,
}

/// Owning Array i.e. `Vec<T>` in Rust or `std::vector<T>` in C++.
#[repr(C)]
pub struct OwningArrayC<T> {
    pub data: *mut T,
    pub len: usize,
    pub cap: usize,
}

/// # Safety
/// Only call from C.
#[no_mangle]
pub extern "C" fn FMW_RUST_make_owning_array_u8(len: usize) -> OwningArrayC<u8> {
    vec![0; len].into()
}

```

Apparently, Rust developers do not want to commit to a particular ABI for these types, to avoid missing out on some future optimizations. So it means that every Rust struct now needs the equivalent "FFI friendly" struct along with conversion functions (usually implemented as `Into` for convenience):

```
struct Foo<'a> {
    x: Option<usize>,
    y: &'a [u8],
    z: Vec<u8>,
}


#[repr(C)]
struct FooC {
    x: OptionC<usize>,
    y: ByteSliceView,
    z: OwningArrayC<u8>,
}
```

Which is cumbersome but still fine, especially since Rust has powerful macros. However, since Rust also does not have great idiomatic support for custom allocators, we stuck with the standard memory allocator, which meant that each struct with heap-allocated fields had to have a deallocation function:

```
#[no_mangle]
pub extern "C" fn foo_free(foo: &FooC) {
    ...
}
```

And the C or C++ calling code would have to do:

```c++
FooC foo{};
if (foo_parse(&foo, bytes) == SUCCESS) {
    // do something with foo...
    ...

    foo_free(foo);
}
```

To simplify this, I introduced a `defer` [construct](https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/) (thanks Gingerbill!):

```c++
FooC foo{};
defer({foo_free(foo);});

if (foo_parse(&foo, bytes) == SUCCESS) {
    // do something with foo...
    ...
}
```

which feels right at home for Go developers. Still, it's more work than you'd have to do in pure idiomatic Rust or C++ code (or even C code with arenas for that matter).

In Zig or Odin, I would probably have used arenas to avoid that.







