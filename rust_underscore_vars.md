Title: In Rust, 'let _ = ...' and 'let _unused = ...' is not the same
Tags: Rust
----


Simple TIL for me. In Rust and some otherl languages, the compiler or linter warns about unused variables. To silence these warnings we can name the unused variable either `_` or prefix it with `_`:

```rust
let _ = foo();
let _bar = foo();
```

