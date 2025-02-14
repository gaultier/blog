Title: Addressing CGO thousand cuts one at a time
Tags: Go, C, Make
---

Rust? Go? Cgo!

I maintain a Go codebase at work which does most of its work through a Rust library that exposes a C API. So they interact via Cgo, Go's FFI mechanism. And it works!

Also it has many weird limitations and surprises. Fortunately, over the two years or so I have been working in this project, I have (re-)discovered solutions for most of these issues. Let's go through them.

*From Go's perspective, Rust is invisible, the C library looks like a pure C library (and indeed it used to be 100% C++ before it got incrementally rewritten to Rust). So I will use C snippets in this article, because that's what the public C header of the library looks like.*

## CGO does not have unions

This is known to Go developers: Go does not have unions, also known as tagged unions, sum types, rich enums, etc. But Go needs to generate Go types for each C type, so that we can use them!

So how does this C tagged union get transformed?

```c

```


## The Go compiler does not detect changes

## Test a C function in Go tests

## Cross-compile

## False positive warnings

## Runtime checks

## Convert slices between Go and C


