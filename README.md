https://gaultier.github.io/blog/


## Quickstart

```sh
apt install cmark zig

$ zig build

# (Re)generate all articles
./zig-out/bin/blog

# Output the table of contents
$ ./zig-out/bin/blog toc <file.md>
```

Serving the files locally:

```sh
$ python3 -m http.server -d ..
```
