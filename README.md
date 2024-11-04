https://gaultier.github.io/blog/


## Quickstart

Requirements: 
- [Odin](https://github.com/odin-lang/Odin.git)
- `cmark`
- `git`

E.g.: `apt install cmark git`

Build this blog (i.e. convert markdown files to HTML):

```sh
$ odin run src
# Or directly (this binary is available after running the above command once, or from the Github releases):
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
