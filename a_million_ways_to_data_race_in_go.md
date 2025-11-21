Title: A million ways to data-race in Go
Tags: Go, Concurrency
---

I have been writing production applications in Go for a few years now. I like some aspects of Go. One aspect I do not like is how easy it is to create data races in Go. 

Go is often touted for its ease to write highly concurrent programs. However, it is also mind-boggling how many ways Go happily gives us developers to shoot ourselves in the foot.

Over the years I have encountered and fixed many interesting kinds of data races in Go. If that interests you, I have written about Go concurrency in the past and about some existing footguns, whithout them being necessarily 'Go data races':

- [What should your mutexes be named?](/blog/what_should_your_mutexes_be_named.html)
- [A subtle data race in Go](/blog/a_subtle_data_race_in_go.html)
- [A subtle bug with Go's errgroup](/blog/subtle_bug_with_go_errgroup.html)
- [How to reproduce and fix an I/O data race with Go and DTrace](/blog/how_to_reproduce_and_fix_an_io_data_race_with_dtrace.html)


So what is a 'Go data race'? Quite simply, it is Go code that does not conform to the [Go memory model](https://go.dev/ref/mem). Importantly, Go defines in its memory model what a Go compiler MUST do and MAY do when faced with a non-conforming program exhibiting data races. Not everything is allowed, quite the contrary in fact. Data races in Go are not benign either: their effects can range from 'no symptoms' to 'arbitrary memory corruption'. 

Quoting the Go memory model:

> This means that races on multiword data structures can lead to inconsistent values not corresponding to a single write. When the values depend on the consistency of internal (pointer, length) or (pointer, type) pairs, as can be the case for interface values, maps, slices, and strings in most Go implementations, such races can in turn lead to arbitrary memory corruption. 

With this out of the way, let's take a tour of real data races in Go code that I have encountered and fixed. At the end I will emit some recommendations to (try to) avoid them.

I also recommend reading the paper [A Study of Real-World Data Races in Golang](https://arxiv.org/pdf/2204.00764). This article aims to be a spiritual successor to it. Some items here are also present in this paper, and some are new.

## Accidental capture in a closure of an outer variable

This one is very common in Go and also very easy to fall into. Here is a simplified reproducer:


```go
err := Foo()
if err != nil {
  return err
}

go func() error {
    err = Bar()
    if err != nil {
      return err
    }

    return nil
}()
```


