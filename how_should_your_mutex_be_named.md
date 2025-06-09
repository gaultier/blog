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

But I was confused for a second: What should the mutex be named? I usually use the `xxxMtx` convention. To avoid a sterile 'you vs me' debate, I thought: What do other people do? What naming convention does the Go project use, if any? 

And more generally, what is the best way to find out what naming conventions are used in a project? 
