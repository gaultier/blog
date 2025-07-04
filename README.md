https://gaultier.github.io/blog/


## Quickstart

*Ensure git submodules are present e.g. `git submodule update --init`.*

Requirements: 
- A C23 compiler
- `git`

Build this blog (i.e. convert markdown files to HTML):

```sh
# Build once.
$ make gen
# Watch & rebuild on change (requires `entr`).
$ make dev
```

Serve the files locally:

```sh
$ python3 -m http.server -d ..
```

Optimize a PNG (requires `pngquant`):

```sh
$ pngquant foo.png -o foo.tmp && mv foo.tmp foo.png
```

Spell (in Neovim):

```
:setlocal spell spelllang=en_us
```
