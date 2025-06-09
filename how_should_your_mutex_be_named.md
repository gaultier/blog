Title: How should your mutexes be named?
Tags: Go, Concurrency
---

The other day a Pull Request popped up at work, it looked like this (simplified):

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

But I paused for a second: What should the mutex be named? I usually use the `xxxMtx` convention. To avoid a sterile 'you vs me' debate, I thought: What do other people do? What naming convention does the Go project use, if any? 

And more generally, what is the best way to find out what naming conventions are used in a project? Since I just started a new job, it's a prevalent question which will come again and again. Thus, I need a good tool to find the answers quickly.

I use `ripgrep` and `ag` all the time (probably at least once a minute), and they can do that, kind of, since they operate on raw text. What I often actually need to do is *search the code structurally*, meaning search the Abstract Syntax Tree (AST). And the good news, there are tools nowadays than can do that!

Enter `ast-grep`. Suprisingly, the main way to search with it is to write a rule file in YAML. A command line search also exists but seems much more limited.
