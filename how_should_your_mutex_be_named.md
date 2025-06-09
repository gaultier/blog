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

When we run the tool on the Go project source code, we get something like this (excerpt):

```sh

```
