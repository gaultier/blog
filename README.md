https://gaultier.github.io/blog/


## Quickstart

```
apt install golang pandoc zig
make
```

Generating table of contents:

```
zig run ./zig-out/bin/blog toc <file.md>
```

Serving the files locally:

```sh
$ python3 -m http.server -d ..
```
