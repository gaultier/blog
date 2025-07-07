Title: A small trick for simple Rust/C++ interop
Tags: Rust, C++
---

*Discussions: [/r/rust](https://www.reddit.com/r/rust/comments/1fkpbfk/a_small_trick_for_simple_rustc_interop/), [HN](https://news.ycombinator.com/item?id=41593661).*

I am [rewriting](/blog/how_to_rewrite_a_cpp_codebase_successfully.html) a gnarly [C++ codebase](/blog/you_inherited_a_legacy_cpp_codebase_now_what.html) in Rust at work.

Due to the heavy use of callbacks (sigh), Rust sometimes calls C++ and C++ sometimes calls Rust. This done by having both sides expose a C API for the functions they want the other side to be able to call.

This is for functions; but what about C++ methods? Here is a trick to rewrite one C++ method at a time, without headaches. And by the way, this works whatever the language you are rewriting the project in, it does not have to be Rust!

## The trick

1. Make the C++ class a [standard layout class](https://en.cppreference.com/w/cpp/language/classes#Standard-layout_class). This is defined by the C++ standard. In layman terms, this makes the C++ class be similar to a plain C struct. With a few allowances, for example the C++ class can still use inheritance and a few other things. Most notably, virtual methods are forbidden. I don't care about this limitation because I never use virtual methods myself and this is my least favorite feature in any programming language.
2. Create a Rust struct with the *exact* same layout as the C++ class.
3. Create a Rust function with a C calling convention, whose first argument is this Rust class. You can now access every C++ member of the class!

Note: Depending on the C++ codebase you find yourself in, the first point could be either trivial or not feasible at all. It depends on the amount of virtual methods used, etc.

In my case, there were a handful of virtual methods, which could all be advantageously made non virtual, so I first did this.

This is all very abstract? Let's proceed with an example!


## Example

Here is our fancy C++ class, `User`. It stores a name, a uuid, and a comment count. A user can write comments, which is just a string, that we print.

```cpp
// Path: user.cpp

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class User {
  std::string name;
  uint64_t comments_count;
  uint8_t uuid[16];

public:
  User(std::string name_) : name{name_}, comments_count{0} {
    arc4random_buf(uuid, sizeof(uuid));
  }

  void write_comment(const char *comment, size_t comment_len) {
    printf("%s (", name.c_str());
    for (size_t i = 0; i < sizeof(uuid); i += 1) {
      printf("%x", uuid[i]);
    }
    printf(") says: %.*s\n", (int)comment_len, comment);
    comments_count += 1;
  }

  uint64_t get_comment_count() { return comments_count; }
};

int main() {
  User alice{"alice"};
  const char msg[] = "hello, world!";
  alice.write_comment(msg, sizeof(msg) - 1);

  printf("Comment count: %lu\n", alice.get_comment_count());

  // This prints:
  // alice (fe61252cf5b88432a7e8c8674d58d615) says: hello, world!
  // Comment count: 1
}
```

So let's first ensure it is a standard layout class. We add this compile-time assertion in the constructor (could be placed anywhere, but the constructor is as good a place as any):

```c++
// Path: user.cpp

    static_assert(std::is_standard_layout_v<User>);
```

And... it builds! 

Now onto the second step: let's define the equivalent class on the Rust side. 

We create a new Rust library project:

```sh
$ cargo new --lib user-rs-lib
```

And place our Rust struct in `src/lib.rs`.

We just need to be careful about alignment (padding between fields) and the order the fields, so we mark the struct `repr(C)` to make the Rust compiler use the same layout as C does:

```rust
// Path: ./user-rs/src/lib.rs

#[repr(C)]
pub struct UserC {
    pub name: [u8; 32],
    pub comments_count: u64,
    pub uuid: [u8; 16],
}
```

Note that the fields can be named differently from the C++ fields if you so choose.

Also note that `std::string` is represented here by an opaque array of 32 bytes. That's because on my machine, with the standard library I have, `sizeof(std::string)` is 32. That is *not* guaranteed by the standard, so this makes it very much not portable. We'll go over some options to work-around this at the end. I wanted to include a standard library type to show that it does not prevent the class from being a 'standard layout class', but that is also creates challenges.

For now, let's forget about this hurdle.

We can also write a stub for the Rust function equivalent to the C++ method:

```rust
// Path: ./user-rs-lib/src/lib.rs

#[no_mangle]
pub extern "C" fn RUST_write_comment(user: &mut UserC, comment: *const u8, comment_len: usize) {
    todo!()
}
```

Now, let's use the tool [cbindgen](https://github.com/mozilla/cbindgen) to generate the C header corresponding to this Rust code:

```sh
$ cargo install cbindgen
$ cbindgen -v src/lib.rs --lang=c++ -o ../user-rs-lib.h
```

And we get this C header:

```c
// Path: user-rs-lib.h

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>

struct UserC {
  uint8_t name[32];
  uint64_t comments_count;
  uint8_t uuid[16];
};

extern "C" {

void RUST_write_comment(UserC *user, const uint8_t *comment, uintptr_t comment_len);

} // extern "C"
```

Now, let's go back to C++, include this C header, and add lots of compile-time assertions to ensure that the layouts are indeed the same. Again, I place these asserts in the constructor:

```c++
#include "user-rs-lib.h"

class User {
 // [..]

  User(std::string name_) : name{name_}, comments_count{0} {
    arc4random_buf(uuid, sizeof(uuid));

    static_assert(std::is_standard_layout_v<User>);
    static_assert(sizeof(std::string) == 32);
    static_assert(sizeof(User) == sizeof(UserC));
    static_assert(offsetof(User, name) == offsetof(UserC, name));
    static_assert(offsetof(User, comments_count) ==
                  offsetof(UserC, comments_count));
    static_assert(offsetof(User, uuid) == offsetof(UserC, uuid));
  }

  // [..]
}
```

With that, we are certain that the layout in memory of the C++ class and the Rust struct are the same. We could probably generate all of these asserts, with a macro or with a code generator, but for this article, it's fine to do manually.


So let's rewrite the C++ method in Rust. We will for now leave out the `name` field since it is a bit problematic. Later we will see how we can still use it from Rust:

```rust
// Path: ./user-rs-lib/src/lib.rs

#[no_mangle]
pub extern "C" fn RUST_write_comment(user: &mut UserC, comment: *const u8, comment_len: usize) {
    let comment = unsafe { std::slice::from_raw_parts(comment, comment_len) };
    let comment_str = unsafe { std::str::from_utf8_unchecked(comment) };
    println!("({:x?}) says: {}", user.uuid.as_slice(), comment_str);

    user.comments_count += 1;
}
```

We want to build a static library so we instruct `cargo` to do so by sticking these lines in `Cargo.toml`:

```toml
[lib]
crate-type = ["staticlib"]
```

We now build:

```sh
$ cargo build
# This is our artifact:
$ ls target/debug/libuser_rs_lib.a
```

We can use our Rust function from C++ in `main`, with some cumbersome casts:

```c++
// Path: user.cpp

int main() {
  User alice{"alice"};
  const char msg[] = "hello, world!";
  alice.write_comment(msg, sizeof(msg) - 1);

  printf("Comment count: %lu\n", alice.get_comment_count());

  RUST_write_comment(reinterpret_cast<UserC *>(&alice),
                     reinterpret_cast<const uint8_t *>(msg), sizeof(msg) - 1);
  printf("Comment count: %lu\n", alice.get_comment_count());
}
```

And link (manually) our brand new Rust library to our C++ program:

```sh
$ clang++ user.cpp ./user-rs-lib/target/debug/libuser_rs_lib.a
$ ./a.out
alice (336ff4cec0a2ccbfc0c4e4cb9ba7c152) says: hello, world!
Comment count: 1
([33, 6f, f4, ce, c0, a2, cc, bf, c0, c4, e4, cb, 9b, a7, c1, 52]) says: hello, world!
Comment count: 2
```

The output is slightly different for the uuid, because we use in the Rust implementation the default `Debug` trait to print the slice, but the content is the same. 

A couple of thoughts:
- The calls `alice.write_comment(..)` and `RUST_write_comment(alice, ..)` are strictly equivalent and in fact, a C++ compiler will transform the former into the latter in a pure C++ codebase, if you look at the assembly generated. So our Rust function is just mimicking what the C++ compiler would do anyway. However, we are free to have the `User` argument be in any position in the function. An other way to say it: We rely on the API, not the ABI, compatibility.
- The Rust implementation can freely read and modify private members of the C++ class, for example the `comment_count` field is only accessible in C++ through the getter, but Rust can just access it as if it was public. That's because `public/private` are just rules enforced by the C++ compiler. However your CPU does not know nor care. The bytes are the bytes. If you can access the bytes at runtime, it does not matter that they were marked 'private' in the source code.
- We have to use tedious casts which is normal. We are indeed reinterpreting memory from one type (`User`) to another (`UserC`). This is allowed by the standard because the C++ class is a 'standard layout class'. If it was not the case, this would be undefined behavior and likely work on some platforms but break on others.


## Accessing std::string from Rust

`std::string` should be an opaque type from the perspective of Rust, because it is not the same across platforms or even compiler versions, so we cannot exactly describe its layout.

But we only want to access the underlying bytes of the string. We thus need a helper on the C++ side, that will extract these bytes for us.

First, the Rust side. We define a helper type `ByteSliceView` which is a pointer and a length (the equivalent of a `std::string_view` in C++ latest versions and `&[u8]` in Rust), and our Rust function now takes an additional parameter, the `name`:

```rust
#[repr(C)]
// Akin to `&[u8]`, for C.
pub struct ByteSliceView {
    pub ptr: *const u8,
    pub len: usize,
}


#[no_mangle]
pub extern "C" fn RUST_write_comment(
    user: &mut UserC,
    comment: *const u8,
    comment_len: usize,
    name: ByteSliceView, // <-- Additional parameter
) {
    let comment = unsafe { std::slice::from_raw_parts(comment, comment_len) };
    let comment_str = unsafe { std::str::from_utf8_unchecked(comment) };

    let name_slice = unsafe { std::slice::from_raw_parts(name.ptr, name.len) };
    let name_str = unsafe { std::str::from_utf8_unchecked(name_slice) };

    println!(
        "{} ({:x?}) says: {}",
        name_str,
        user.uuid.as_slice(),
        comment_str
    );

    user.comments_count += 1;
}
```

We re-run cbindgen, and now C++ has access to the `ByteSliceView` type. We thus write a helper to convert a `std::string` to this type, and pass the additional parameter to the Rust function (we also define a trivial `get_name()` getter for `User` since `name` is still private):

```c++
// Path: user.cpp

ByteSliceView get_std_string_pointer_and_length(const std::string &str) {
  return {
      .ptr = reinterpret_cast<const uint8_t *>(str.data()),
      .len = str.size(),
  };
}

// In main:
int main() {
    // [..]
  RUST_write_comment(reinterpret_cast<UserC *>(&alice),
                     reinterpret_cast<const uint8_t *>(msg), sizeof(msg) - 1,
                     get_std_string_pointer_and_length(alice.get_name()));
}
```

We re-build, re-run, and lo and behold, the Rust implementation now prints the name:

```text
alice (69b7c41491ccfbd28c269ea4091652d) says: hello, world!
Comment count: 1
alice ([69, b7, c4, 14, 9, 1c, cf, bd, 28, c2, 69, ea, 40, 91, 65, 2d]) says: hello, world!
Comment count: 2
```

Alternatively, if we cannot or do not want to change the Rust signature, we can make the C++ helper `get_std_string_pointer_and_length` have a C convention and take a void pointer, so that Rust will call the helper itself, at the cost of numerous casts in and out of `void*`.


## Improving the std::string situation

- Instead of modeling `std::string` as an array of bytes whose size is platform-dependent, we could move this field to the end of the C++ class and remove it entirely from Rust (since it is unused there). This would break `sizeof(User) == sizeof(UserC)`, it would now be `sizeof(User) - sizeof(std::string) == sizeof(UserC)`. Thus, the layout would be exactly the same (until the last field which is fine) between C++ and Rust. However, it will be an ABI breakage, if external users depend on the exact layout of the C++ class, and C++ constructors will have to be adapted since they rely on the order of fields. This approach is basically the same as the [flexible array member](https://en.wikipedia.org/wiki/Flexible_array_member) feature in C.
- If allocations are cheap, we could store the name as a pointer: `std::string * name;` on the C++ side, and on the Rust side, as a void pointer: `name: *const std::ffi::c_void`, since pointers have a guaranteed size on all platforms. That has the advantage that Rust can access the data in `std::string`, by calling a C++ helper with a C calling convention. But some will dislike that a naked pointer is being used in C++.



## Conclusion

We now have successfully re-written a C++ class method. This technique is great because the C++ class could have hundreds of methods, in a real codebase, and we can still rewrite them one at a time, without breaking or touching the others.

The big caveat is that: the more C++ specific features and standard types the class is using, the more difficult this technique is to apply, necessitating helpers to make conversions from one type to another, and/or numerous tedious casts. If the C++ class is basically a C struct only using C types, it will be very easy.

Still, I have employed this technique at work a lot and I really enjoy its relative simplicity and incremental nature. 

It can also be in theory automated, say with tree-sitter or libclang to operate on the C++ AST:

1. Add a compile-time assert in the C++ class constructor to ensure it is a 'standard layout class' e.g. `static_assert(std::is_standard_layout_v<User>);`. If this fails, skip this class, it requires manual intervention.
1. Generate the equivalent Rust struct e.g. the struct `UserC.`
1. For each field of the C++ class/Rust struct, add an compile-time assert to make sure the layout is the same e.g. `static_assert(sizeof(User) == sizeof(UserC)); static_assert(offsetof(User, name) == offsetof(UserC, name));`. If this fails, bail.
1. For each C++ method, generate an (empty) equivalent Rust function. E.g. `RUST_write_comment`.
1. A developer implements the Rust function. Or AI. Or something.
1. For each call site in C++, replace the C++ method call by a call to the Rust function. E.g. `alice.write_comment(..);` becomes `RUST_write_comment(alice, ..);`.
1. Delete the C++ methods that have been rewritten.

And boom, project rewritten.



## Addendum: the full code

<details>
  <summary>The full code</summary>

```cpp
// Path: user.cpp

#include "user-rs-lib.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" ByteSliceView
get_std_string_pointer_and_length(const std::string &str) {
  return {
      .ptr = reinterpret_cast<const uint8_t *>(str.data()),
      .len = str.size(),
  };
}

class User {
  std::string name;
  uint64_t comments_count;
  uint8_t uuid[16];

public:
  User(std::string name_) : name{name_}, comments_count{0} {
    arc4random_buf(uuid, sizeof(uuid));

    static_assert(std::is_standard_layout_v<User>);
    static_assert(sizeof(std::string) == 32);
    static_assert(sizeof(User) == sizeof(UserC));
    static_assert(offsetof(User, name) == offsetof(UserC, name));
    static_assert(offsetof(User, comments_count) ==
                  offsetof(UserC, comments_count));
    static_assert(offsetof(User, uuid) == offsetof(UserC, uuid));
  }

  void write_comment(const char *comment, size_t comment_len) {
    printf("%s (", name.c_str());
    for (size_t i = 0; i < sizeof(uuid); i += 1) {
      printf("%x", uuid[i]);
    }
    printf(") says: %.*s\n", (int)comment_len, comment);
    comments_count += 1;
  }

  uint64_t get_comment_count() { return comments_count; }

  const std::string &get_name() { return name; }
};

int main() {
  User alice{"alice"};
  const char msg[] = "hello, world!";
  alice.write_comment(msg, sizeof(msg) - 1);

  printf("Comment count: %lu\n", alice.get_comment_count());

  RUST_write_comment(reinterpret_cast<UserC *>(&alice),
                     reinterpret_cast<const uint8_t *>(msg), sizeof(msg) - 1,
                     get_std_string_pointer_and_length(alice.get_name()));
  printf("Comment count: %lu\n", alice.get_comment_count());
}
```

```c
// Path: user-rs-lib.h

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>

struct UserC {
  uint8_t name[32];
  uint64_t comments_count;
  uint8_t uuid[16];
};

struct ByteSliceView {
  const uint8_t *ptr;
  uintptr_t len;
};

extern "C" {

void RUST_write_comment(UserC *user,
                        const uint8_t *comment,
                        uintptr_t comment_len,
                        ByteSliceView name);

} // extern "C"
```

```rust
// Path: user-rs-lib/src/lib.rs

#[repr(C)]
pub struct UserC {
    pub name: [u8; 32],
    pub comments_count: u64,
    pub uuid: [u8; 16],
}

#[repr(C)]
// Akin to `&[u8]`, for C.
pub struct ByteSliceView {
    pub ptr: *const u8,
    pub len: usize,
}

#[no_mangle]
pub extern "C" fn RUST_write_comment(
    user: &mut UserC,
    comment: *const u8,
    comment_len: usize,
    name: ByteSliceView,
) {
    let comment = unsafe { std::slice::from_raw_parts(comment, comment_len) };
    let comment_str = unsafe { std::str::from_utf8_unchecked(comment) };

    let name_slice = unsafe { std::slice::from_raw_parts(name.ptr, name.len) };
    let name_str = unsafe { std::str::from_utf8_unchecked(name_slice) };

    println!(
        "{} ({:x?}) says: {}",
        name_str,
        user.uuid.as_slice(),
        comment_str
    );

    user.comments_count += 1;
}
```

</details>



