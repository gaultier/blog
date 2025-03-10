https://gaultier.github.io/blog/


## Quickstart

Requirements: 
- A C compiler
- [cmark-gfm](https://github.com/github/cmark-gfm) 
- `git`

Build this blog (i.e. convert markdown files to HTML):

```sh
$ ./build.sh release
$ ./main.bin
```

Serve the files locally:

```sh
$ python3 -m http.server -d ..
```

Optimize a PNG (requires `pngquant`):

```sh
$ pngquant foo.png -o foo.tmp && mv foo.tmp foo.png
```
