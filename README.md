https://gaultier.github.io/blog/


## Quickstart

```shell
# Build once.
$ cargo run [--release]

# Watch.
$ carg run [--release] -- watch
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
