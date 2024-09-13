https://gaultier.github.io/blog/


## Quickstart

*Requirement: [Odin](https://github.com/odin-lang/Odin.git)*

```sh
apt install cmark git

# Build this blog.
$ odin run src
```

Serving the files locally:

```sh
$ python3 -m http.server -d ..
```

Optimize a PNG:

```
pngquant foo.png -o foo.tmp && mv foo.tmp foo.png
```
