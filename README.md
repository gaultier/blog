https://gaultier.github.io/blog/


## Quickstart

```shell
$ cargo run [--release]
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
$ git gc --aggressive --prune=now
```
