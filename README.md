https://gaultier.github.io/blog/


## Quickstart

```
apt install parallel graphviz golang pandoc
make
```

Generating table of contents (not sure if there is a better way):

```
pandoc -s --toc input.md -o /tmp/output.md
```

Serving the files locally:

```sh
$ python3 -m http.server -d ..
```
