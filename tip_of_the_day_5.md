Title: Tip of the day #5: Install Go tools with a specific version
Tags: Go
---

 I had an issue with Go tools around versioning, and here's how I solved it. It could be useful to others. This was the error:

```sh
$ staticcheck  ./...
-: module requires at least go1.23.6, but Staticcheck was built with go1.23.1 (compile)
$ go version
go version go1.23.6 linux/amd64
```

Indeed the project was specifying `go 1.23.6` in `go.mod`.

Even after removing the staticcheck binary and re-installing it I still had the same issue:

```sh
$ which staticcheck
/home/pg/go/bin/staticcheck
$ rm /home/pg/go/bin/staticcheck
$ which staticcheck
which: no staticcheck 
$ go install honnef.co/go/tools/cmd/staticcheck@v0.5.1
$ staticcheck  ./...
-: module requires at least go1.23.6, but Staticcheck was built with go1.23.1 (compile)
```

I even tried the `-a` flag for `go install` to force a clean build (since `go install` fetches the sources and builds them) to no avail.

**Solution:** following https://go.dev/doc/manage-install, I installed the specific version of Go I needed and used that to install the tool:


```sh
$ go install golang.org/dl/go1.23.6@latest
$ go1.23.6 download
$ go1.23.6 install honnef.co/go/tools/cmd/staticcheck@v0.5.1
$ staticcheck  -tags=integration_tests ./... # Works!
```

That was a TIL for me.

Note that Go 1.24 supports the project listing tools directly in go.mod which would probably solve this issue directly.
