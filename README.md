https://gaultier.github.io/blog/


## Quickstart

```shell
# Build once.
$ cargo run [--release]

# Watch.
$ carg run [--release] -- watch
```

Check the unsafe code (requires nightly). Miri cannot call into libcmark-gfm, so
it only runs the tests that do not parse markdown; AddressSanitizer covers both
the Rust and the C:

```shell
$ cargo +nightly miri test -- --skip <test that parses markdown>...

$ RUSTFLAGS="-Zsanitizer=address" \
  CFLAGS="-fsanitize=address -mllvm -asan-guard-against-version-mismatch=0 -fsanitize=undefined -fsanitize-trap=undefined -fno-omit-frame-pointer" \
  cargo +nightly test --target aarch64-apple-darwin
```

Optimize a PNG (requires `pngquant`):

```shell
$ pngquant foo.png -o foo.tmp && mv foo.tmp foo.png
```

Spell (in Neovim):

```plaintext
:setlocal spell spelllang=en_us
```

Optimize git stats:

```shell
$ git maintenance run
```
