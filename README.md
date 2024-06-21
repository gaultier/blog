https://gaultier.github.io/blog/


## Quickstart

```sh
apt install golang cmark zig

$ zig build

# (Re)generate all articles
./zig-out/bin/blog gen_all

# (Re)generate an article
./zig-out/bin/blog gen <file.md>

# Output the table of contents
$ ./zig-out/bin/blog toc <file.md>
```

Serving the files locally:

```sh
$ python3 -m http.server -d ..
```
