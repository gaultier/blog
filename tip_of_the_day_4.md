Title: Tip of the day #4: Type annotations on Rust match patterns
Tags: Rust
---

Today at work I was adding error logs to our Rust codebase and I hit an interesting case. I had a match pattern, and the compiler asked me to add type annotations to a branch of the pattern, because it could not infer by itself the right type.

```rust
fn decode_foo(input: &[u8]) -> Result<(&[u8], [u8; 33]), Error> {
    if input.len() < 33 {
        return Err(Error::InvalidData);
    }

    let (left, right) = input.split_at(33);
    let value = match left.try_into() {
        Ok(v) => v,
        Err(err) => {
            let err_s = err.to_string(); // <= Here is the compilation error.
            eprintln!("failed to decode data, wrong length: {}", err_s);
            return Err(Error::InvalidData);
        }
    };

    Ok((right, value))
}
```

```
error[E0282]: type annotations needed
  --> src/main.rs:14:25
   |
14 |             let err_s = err.to_string();
   |                         ^^^ cannot infer type
```

This function parses a slice of bytes, and on error, logs the error. The real code is of course more complex but I could reduce the error to this minimal code.

---

So I tried to add type annotations the usual Rust way:

```rust
    let value = match left.try_into() {
        Ok(v) => v,
        Err(err: TryFromSliceError) => {
            // [...]
        }
```

Which leads to this nice error:

```
error: expected one of `)`, `,`, `@`, or `|`, found `:`
  --> src/main.rs:15:16
   |
15 |         Err(err: TryFromSliceError) => {
   |                ^ expected one of `)`, `,`, `@`, or `|`
```

If you're feeling smart, thinking, 'Well that's because you did not use `inspect_err` or `map_err`!'. Well they suffer from the exact same problem: a type annotation is needed. However, since they use a lambda, the intuitive type annotation, like the one I tried, works. But not for `match`.

Alright, so after some [searching around](https://users.rust-lang.org/t/type-annotation-on-match-pattern/49180/10), I came up with this mouthful of a syntax:

```rust
    let value = match left.try_into() {
        Ok(v) => v,
        Err::<_, TryFromSliceError>(err) => {
            // [...]
        }
```

Which works! And the same syntax can be done to the `Ok` branch (per the link above) if needed. Note that this is a partial type annotation: we only care about the `Err` part of the `Result` type.

That was a TIL for me. It's a bit of a weird syntax here. It's usually the syntax for type annotations on methods (more on that in a second).

Anyways, there's a much better way to solve this issue. We can simply  annotate the resulting variable outside of the whole match pattern, so that `rustc` knows which `try_into` method we are using:

```rust
    let value: [u8; 33] = match left.try_into() {
        Ok(v) => v,
        Err(err) => {
          // [...]
        }
```

Another approach that works is to annotate the `try_into()` function with the type, but it's even noisier than annotating the `Err` branch:

```rust
    let value = match TryInto::<[u8; 33]>::try_into(left) {
        Ok(v) => v,
        Err(err) => {
          // [...]
        }
```

Astute readers will think at this point that all of this is unnecessary: let's just have the *magic traits(tm)* do their wizardry. We do not convert the error to a string, we simply let `eprintln!` call `err.fmt()` under the hood, since `TryFromSliceError` implements the `Display` trait (which is why we could convert it to a `String` with `.to_string()`):

```rust
    let value = match left.try_into() {
        Ok(v) => v,
        Err(err) => {
            eprintln!("failed to decode data, wrong length: {}", err);
            return Err(Error::InvalidData);
        }
    };
```

That works but in my case I really needed to convert the error to a `String`, to be able to pass it to C, which does not know anything about fancy traits.


I find this issue interesting because it encapsulates well the joy and pain of writing Rust: match patterns are really handy, but they sometimes lead to weird syntax not found elsewhere in the Rust language (maybe due to the OCaml heritage?). Type inference is nice but sometimes the compiler/language server fails at inferring things you'd think they should really be able to infer. Traits and `into/try_into` are found everywhere in Rust code, but it's hard to know what type is being converted to what, especially when these are chained several times without any type annotation whatsoever.

By the way, here's a tip I heard some time ago: if you want to know the real type of a variable that's obscured by type inference, just add a type annotation that's obviously wrong, and the compiler will show the correct type. That's how I pinpointed the `TryFromSliceError` type. Let's add a bogus `bool` type annotation:

```rust
    let value = match left.try_into() {
        Ok(v) => v,
        Err::<_, bool>(err) => {
          // [...]
        }
```

And the compiler helpfully gives us the type:

```
error[E0271]: type mismatch resolving `<[u8; 33] as TryFrom<&[u8]>>::Error == bool`
  --> src/main.rs:11:28
   |
11 |     let value = match left.try_into() {
   |                            ^^^^^^^^ expected `bool`, found `TryFromSliceError`
```

So...it *does* actually know the type of `err`... You naughty compiler, playing games with me! It reminds me of this picture:

<img style="height:50rem" src="coffee_or_tea.png">Coffee or tea?</img>
