https://gaultier.github.io/blog/


## Quickstart

Requirements: 
- [Odin](https://github.com/odin-lang/Odin.git)
- [cmark-gfm](https://github.com/github/cmark-gfm) 
- `git`

E.g.: `apt install cmark git`

Build this blog (i.e. convert markdown files to HTML):

```sh
$ odin build src/ -debug -vet -strict-style -o:speed
$ ./src.bin
```

Serve the files locally:

```sh
$ python3 -m http.server -d ..
```

Optimize a PNG:

```sh
$ pngquant foo.png -o foo.tmp && mv foo.tmp foo.png
```
