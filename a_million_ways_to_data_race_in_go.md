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

In the code I will often use `errgroup.WaitGroup` or `sync.WaitGroup` because they act as a fork-join pattern, shortening the code. The exact same can be done with 'raw' Go channels and goroutines. This also serves to show that using higher-level concepts does not magically protect against data races.

## Accidental capture in a closure of an outer variable

This one is very common in Go and also very easy to fall into. Here is a simplified reproducer:


```go
package main

import (
	"context"

	"golang.org/x/sync/errgroup"
)

func Foo() error { return nil }
func Bar() error { return nil }
func Baz() error { return nil }

func Run(ctx context.Context) error {
	err := Foo()
	if err != nil {
		return err
	}

	wg, ctx := errgroup.WithContext(ctx)
	wg.Go(func() error {
		err = Baz()
		if err != nil {
			return err
		}

		return nil
	})
	wg.Go(func() error {
		err = Bar()
		if err != nil {
			return err
		}

		return nil
	})

	return wg.Wait()
}

func main() {
	println(Run(context.Background()))
}
```

The issue might not be immediately visible.

The problem is that the `err` outer variable is implicitly captured by the closures running each in a separate goroutine. They then mutate `err` concurrently. What they meant to do is instead use a variable local to the closure and return that instead. There is conceptually no need to share any data; this is purely accidental. 

The fix is simple, I'll show two variants in the same diff: define a local variable, or use a named return value.

```diff
diff --git a/cmd-sg/main.go b/cmd-sg/main.go
index 7eabdbc..4349157 100644
--- a/cmd-sg/main.go
+++ b/cmd-sg/main.go
@@ -18,14 +18,14 @@ func Run(ctx context.Context) error {
 
 	wg, ctx := errgroup.WithContext(ctx)
 	wg.Go(func() error {
-		err = Baz()
+		err := Baz()
 		if err != nil {
 			return err
 		}
 
 		return nil
 	})
-	wg.Go(func() error {
+	wg.Go(func() (err error) {
 		err = Bar()
 		if err != nil {
 			return err

```

It is unfortunate that a one character difference is all we need to fall into this trap. I feel for the original developer who wrote this code without realizing the implicit capture. As mentioned in a [previous article](/blog/a_subtle_data_race_in_go.html) where this silent behavior bit me, we can use the build flag `-gcflags='-d closure=1'` to make the Go compiler print which variables are being captured by the closure:

```shell
$ go build -gcflags='-d closure=1' 
./main.go:20:8: heap closure, captured vars = [err]
./main.go:28:8: heap closure, captured vars = [err]
```

But this is not realistic to do that in a big codebase and inspect each closure. It's useful if you know that a given closure might suffer from this problem.


## Concurrent use of `http.Client`

The Go docs state about `http.Client`: 

> [...] Clients should be reused instead of created as needed. Clients are safe for concurrent use by multiple goroutines. 

So imagine my surprise when the Go race detector flagged a race tied to `http.Client`. The code looked like this:


```go
package main

import (
	"context"
	"net/http"

	"golang.org/x/sync/errgroup"
)

func Run(ctx context.Context) error {
	client := http.Client{}

	wg, ctx := errgroup.WithContext(ctx)
	wg.Go(func() error {
		client.CheckRedirect = func(req *http.Request, via []*http.Request) error {
			if req.Host == "google.com" {
				return nil
			} else {
				return http.ErrUseLastResponse
			}
		}
		_, err := client.Get("http://google.com")
		return err
	})
	wg.Go(func() error {
		client.CheckRedirect = nil
		_, err := client.Get("http://amazon.com")
		return err
	})

	return wg.Wait()
}

func main() {
	println(Run(context.Background()))
}
```

The program makes two concurrent HTTP requests to two different URLs. For the first one, the code restricts redirects (I invented the exact logic for that, no need to look into it). For the second one, no redirect checks are performed, by setting `CheckRedirect` to nil. This code is idiomatic and follows the recommendations from the documentation:

> CheckRedirect specifies the policy for handling redirects. If CheckRedirect is not nil, the client calls it before following an HTTP redirect.
> If CheckRedirect is nil, the Client uses its default policy [...].


The problem is: the `CheckRedirect` field is modified concurrently without any synchronization which is a data race. 

This code also suffers from a I/O race: depending on the network speed and response time for both URLs, the redirects might or might be checked, since the callback might get overwritten from the other goroutine, right when the HTTP client would call it.


Here, the simplest fix is to use two different HTTP clients:

```diff
diff --git a/cmd-sg/main.go b/cmd-sg/main.go
index 351ecc0..8abee1c 100644
--- a/cmd-sg/main.go
+++ b/cmd-sg/main.go
@@ -8,10 +8,10 @@ import (
 )
 
 func Run(ctx context.Context) error {
-	client := http.Client{}
 
 	wg, ctx := errgroup.WithContext(ctx)
 	wg.Go(func() error {
+		client := http.Client{}
 		client.CheckRedirect = func(req *http.Request, via []*http.Request) error {
 			if req.Host == "google.com" {
 				return nil
@@ -23,6 +23,7 @@ func Run(ctx context.Context) error {
 		return err
 	})
 	wg.Go(func() error {
+		client := http.Client{}
 		client.CheckRedirect = nil
 		_, err := client.Get("http://amazon.com")
 		return err

```

This may affect performance negatively since some resources will not be shared anymore.

I would not blame the original developer 
