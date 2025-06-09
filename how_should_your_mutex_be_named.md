Title: How should your mutexes be named?
Tags: Go, Concurrency
---

The other day a Pull Request popped up at work, it looked like this in Go (simplified):

```go
type Foo struct {
    bar int
    barMu sync.Mutex
}
```

It's a typical approach with concurrent code: a mutex protects the variable it is named after. So using the variable `bar` looks like this:

```go
barMu.Lock()
bar += 1
barMu.Unlock()
```

*Yes, in this particular case an atomic would be used instead of a mutex but that's just to illustrate.*

But I paused for a second: What should the mutex be named? I usually use the `xxxMtx` convention. To avoid a sterile 'you vs me' debate, I thought: What do other people do? What naming convention does the Go project use, if any? 

And more generally, what is the best way to find out what naming conventions are used in a project? Since I just started a new job, it's a prevalent question which will come again and again. Thus, I need a good tool to find the answers quickly.

I use `ripgrep` and `ag` all the time (probably at least once a minute), and they can do that, kind of, since they operate on raw text. What I often actually need to do is *search the code structurally*, meaning search the Abstract Syntax Tree (AST). And the good news, there are tools nowadays than can do that!

Enter [ast-grep](https://github.com/ast-grep/ast-grep). Suprisingly, the main way to search with it is to write a rule file in YAML. A command line search also exists but seems much more limited. Let's search for structure fields whose type is a mutex:

```yaml
id: find-mtx-fields
message: Mutex fields found
severity: info
language: go
rule:
  kind: field_declaration
  all:
    - has: 
        field: name
        # Ignore nameless fields.
        regex: ".+"
    - has:
        field: type
        regex: "Mutex"
```

A potential match must pass all conditions under the `all` section to be a result.

When we run the tool on the Go project source code, we get something like this (excerpt):

```sh
$ ast-grep scan --rule ~/scratch/mtx.yaml
[...]

note[find-mtx-fields]: Mutex fields found
   ┌─ net/textproto/pipeline.go:29:2
   │
29 │     mu       sync.Mutex
   │     ^^^^^^^^^^^^^^^^^^^


note[find-mtx-fields]: Mutex fields found
    ┌─ net/rpc/server.go:191:2
    │
191 │     reqLock    sync.Mutex // protects freeReq
    │     ^^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields]: Mutex fields found
   ┌─ net/rpc/jsonrpc/server.go:31:2
   │
31 │     mutex   sync.Mutex // protects seq, pending
   │     ^^^^^^^^^^^^^^^^^^

[...]
```

Very useful. The tool can do much more, but that's enough for us to discover that there isn't *one* naming convention in this case:

## Low-tech alternatives

A quick and dirty way to achieve the same with a regexp is:

```
rg -t go '^\s*\w+\s+sync\.Mutex$'
```

This works since Go is a language with only one way to define a struct field, but some languages would be more difficult.

Another way to only find field declarations would be to use awk:

```awk
/\s+struct\s+/ { in_struct = 1 }

in_struct && /\s+\w+\s+sync\.Mutex/ { print }

in_struct && /^}$/ { in_struct = 0 }
```

But this might not work with complex constructs such as defining a struct within a struct e.g.:

```go

type Foo struct {
	bar struct {
		x int
		y int
	}
	barMtx sync.Mutex
}
```
