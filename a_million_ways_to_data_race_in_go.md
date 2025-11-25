Title: A million ways to die from a data race in Go
Tags: Go, Concurrency
---

*Comments: [r/golang](https://www.reddit.com/r/golang/comments/1p5avp3/a_million_ways_to_die_from_a_data_race_in_go/).*

I have been writing production applications in Go for a few years now. I like some aspects of Go. One aspect I do not like is how easy it is to create data races in Go. 

Go is often touted for its ease to write highly concurrent programs. However, it is also mind-boggling how many ways Go happily gives us developers to shoot ourselves in the foot.

Over the years I have encountered and fixed many interesting kinds of data races in Go. If that interests you, I have written about Go concurrency in the past and about some existing footguns, without them being necessarily 'Go data races':

- [What should your mutexes be named?](/blog/what_should_your_mutexes_be_named.html)
- [A subtle data race in Go](/blog/a_subtle_data_race_in_go.html)
- [A subtle bug with Go's errgroup](/blog/subtle_bug_with_go_errgroup.html)
- [How to reproduce and fix an I/O data race with Go and DTrace](/blog/how_to_reproduce_and_fix_an_io_data_race_with_dtrace.html)


So what is a 'Go data race'? Quite simply, it is Go code that does not conform to the [Go memory model](https://go.dev/ref/mem). Importantly, Go defines in its memory model what a Go compiler MUST do and MAY do when faced with a non-conforming program exhibiting data races. Not everything is allowed, quite the contrary in fact. Data races in Go are not benign either: their effects can range from 'no symptoms' to 'arbitrary memory corruption'. 

Quoting the Go memory model:

> This means that races on multiword data structures can lead to inconsistent values not corresponding to a single write. When the values depend on the consistency of internal (pointer, length) or (pointer, type) pairs, as can be the case for interface values, maps, slices, and strings in most Go implementations, such races can in turn lead to arbitrary memory corruption. 

With this out of the way, let's take a tour of real data races in Go code that I have encountered and fixed. At the end I will emit some recommendations to (try to) avoid them.

I also recommend reading the paper [A Study of Real-World Data Races in Golang](https://arxiv.org/pdf/2204.00764). This article humbly hopes to be a spiritual companion to it. Some items here are also present in this paper, and some are new.

In the code I will often use `errgroup.WaitGroup` or `sync.WaitGroup` because they act as a fork-join pattern, shortening the code. The exact same can be done with 'raw' Go channels and goroutines. This also serves to show that using higher-level concepts does not magically protect against all data races.

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

### The fix

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

### Learnings 

It is unfortunate that a one character difference is all we need to fall into this trap. I feel for the original developer who wrote this code without realizing the implicit capture. As mentioned in a [previous article](/blog/a_subtle_data_race_in_go.html) where this silent behavior bit me, we can use the build flag `-gcflags='-d closure=1'` to make the Go compiler print which variables are being captured by the closure:

```shell
$ go build -gcflags='-d closure=1' 
./main.go:20:8: heap closure, captured vars = [err]
./main.go:28:8: heap closure, captured vars = [err]
```

But this is not realistic to do that in a big codebase and inspect each closure. It's useful if you know that a given closure might suffer from this problem.


## Concurrent use of http.Client

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

The program makes two concurrent HTTP requests to two different URLs. For the first one, the code restricts redirects (I invented the exact logic for that, no need to look too much into it, the real code has complex logic here). For the second one, no redirect checks are performed, by setting `CheckRedirect` to nil. This code is idiomatic and follows the recommendations from the documentation:

> CheckRedirect specifies the policy for handling redirects. If CheckRedirect is not nil, the client calls it before following an HTTP redirect.
> If CheckRedirect is nil, the Client uses its default policy [...].


The problem is: the `CheckRedirect` field is modified concurrently without any synchronization which is a data race. 

This code also suffers from an I/O race: depending on the network speed and response time for both URLs, the redirects might or might be checked, since the callback might get overwritten from the other goroutine, right when the HTTP client would call it. 

Alternatively, the `http.Client` could end up calling a `nil` callback if the callback was set when the `http.Client` checked whether it was nil or not, but before `http.Client` had the chance to call it, the other goroutine set it to `nil`. Boom, `nil` dereference.

### The fix

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

### Learnings

This may affect performance negatively since some resources will not be shared anymore.

Additionally, in some situations, this is not so easy because `http.Client` does not offer a `Clone()` method (a recurring issue in Go as we'll see). For example, a Go test may start a `httptest.Server` and then call `.Client()` on this server to obtain a preconfigured HTTP client for this server. Then, there is no easy way to duplicate this client to use it from two different tests running in parallel.

Again here, I would not blame the original developer. In my view, the docs for `http.Client` are misleading and should mention that not every single operation is concurrency safe. Perhaps with the wording: 'once a http.Client is constructed, performing a HTTP request is concurrency safe, provided that the http.Client fields are not modified concurrently'. Which is less catchy than 'Clients are safe for concurrent use', period.


## Improper lifetime of a mutex

The next data race is one that baffled me for a bit, because the code was using a mutex properly and I could not fathom why a race would be possible. 

Here's a minimal reproducer:

```go
package main

import (
	"encoding/json"
	"net/http"
	"sync"
)

type Plans map[string]int

type PricingInfo struct {
	plans Plans
}

var pricingInfo = PricingInfo{plans: Plans{"cheap plan": 1, "expensive plan": 5}}

type PricingService struct {
	info    PricingInfo
	infoMtx sync.Mutex
}

func NewPricingService() *PricingService {
	return &PricingService{info: pricingInfo, infoMtx: sync.Mutex{}}
}

func AddPricing(w http.ResponseWriter, r *http.Request) {
	pricingService := NewPricingService()

	pricingService.infoMtx.Lock()
	defer pricingService.infoMtx.Unlock()

	pricingService.info.plans["middle plan"] = 3

	encoder := json.NewEncoder(w)
	encoder.Encode(pricingService.info)
}

func GetPricing(w http.ResponseWriter, r *http.Request) {
	pricingService := NewPricingService()

	pricingService.infoMtx.Lock()
	defer pricingService.infoMtx.Unlock()

	encoder := json.NewEncoder(w)
	encoder.Encode(pricingService.info)
}

func main() {
	http.HandleFunc("POST /add-pricing", AddPricing)
	http.HandleFunc("GET /pricing", GetPricing)
	http.ListenAndServe(":12345", nil)
}
```

A global mutable map of pricing information is guarded by a mutex. One HTTP endpoint reads the map, another adds an item to it. Pretty simple I would say. The locking is done correctly. 

Yet the map suffers from a data race. Here is a reproducer:

```go
package main

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestMain(t *testing.T) {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /add-pricing", AddPricing)
	mux.HandleFunc("GET /pricing", GetPricing)

	server := httptest.NewServer(mux)
	t.Cleanup(server.Close)

	t.Run("get pricing", func(t *testing.T) {
		t.Parallel()

		_, err := server.Client().Get(server.URL + "/pricing")
		if err != nil {
			panic(err)
		}
	})

	for range 5 {
		t.Run("add pricing", func(t *testing.T) {
			t.Parallel()

			_, err := server.Client().Post(server.URL+"/add-pricing", "application/json", nil)
			if err != nil {
				panic(err)
			}
		})
	}
}
```


The reason why is because the data, and the mutex guarding it, do not have the same 'lifetime'. The `pricingInfo` map is global and exists from the start of the program to the end. But the mutex `infoMtx` exists only for the duration of the HTTP handler (and thus HTTP request). We effectively have 1 map and N mutexes, none of them shared between HTTP handlers. So HTTP handlers cannot synchronize access to the map.

The intent of the code was (I think) to do a deep clone of the pricing information at the beginning of the HTTP handler in `NewPricingService`. Unfortunately, Go does a shallow copy of structures and thus each `PricingService` instance ends up sharing the same underlying `plans` map, which is this global map. It could be that for a long time, it worked because the `PricingInfo` struct did not yet contain the map (in the real code it contains a lot of `int`s and `string`s which are value types and will be copied correctly by a shallow copy), and the map was only added later. 


This data race is akin to copying a mutex by value when passing it to a function, which then locks it. This does no synchronization at all since a copy of the mutex is being locked - no mutex is shared between concurrent units.

### The fix

In any event, the fix is to align the lifetime of the data and the mutex:

- We can keep the map global and make the mutex also global, so that it is shared by every HTTP handler, thus we have 1 map and 1 mutex; or
- We make the map scoped to the HTTP handler by implementing a deep clone function, thus we have N maps and N mutexes

I went with the second approach in the real code because it seemed to be the original intent:

```diff
diff --git a/cmd-sg/main.go b/cmd-sg/main.go
index fb59f5c..c7a7a94 100644
--- a/cmd-sg/main.go
+++ b/cmd-sg/main.go
@@ -2,6 +2,7 @@ package main
 
 import (
 	"encoding/json"
+	"maps"
 	"net/http"
 	"sync"
 )
@@ -19,8 +20,15 @@ type PricingService struct {
 	infoMtx sync.Mutex
 }
 
+func ClonePricing(pricingInfo PricingInfo) PricingInfo {
+	cloned := PricingInfo{plans: make(Plans, len(pricingInfo.plans))}
+	maps.Copy(cloned.plans, pricingInfo.plans)
+
+	return cloned
+}
+
 func NewPricingService() *PricingService {
-	return &PricingService{info: pricingInfo, infoMtx: sync.Mutex{}}
+	return &PricingService{info: ClonePricing(pricingInfo), infoMtx: sync.Mutex{}}
 }
 
 func AddPricing(w http.ResponseWriter, r *http.Request) {
```

### Learnings

It's annoying to have to implement this manually and especially to have to check every single nested field to determine if it's a value type or a reference type (the former will behave correctly with a shallow copy, the latter needs a custom deep copy implementation). I miss the `derive(Clone)` annotation in Rust. This is something that the compiler can (and should) do better than me. 

Furthermore, as mentioned in the previous section, some types from the standard library or third-party libraries do not implement a deep `Clone()` function and contain private fields which prevent us from implementing that ourselves.


I think Rust's API for a mutex is better because a Rust mutex wraps the data it protects and thus it is harder to have uncorrelated lifetimes for the data and the mutex. 

Go's mutex API likely could not have been implemented this way since it would have required generics which did not exist at the time. But as of today: I think it could. 

Nonetheless, the Go compiler has no way to detect accidental shallow copying, whereas Rust's compiler has the concepts of `Copy` and `Clone` - so that issue remains in Go, and is not a simple API mistake in the standard library we can fix.


## Concurrent reads and writes to standard library containers

I encountered many cases of concurrently modifying a `map`, `slice`, etc without any synchronization. That's your run of the mill data race and they are typically fixed by 'slapping a mutex on it' or using a concurrency safe data structure such as `sync.Map`. 

I will thus share here a more interesting one where it is not as straightforward.

This time, the code is convoluted but what it does is relatively simple:

1. Spawn a docker container and capture its standard output in a byte buffer
2. Concurrently (in a different goroutine), read this output and find a given token. 
3. Once the token is found, the context is canceled and thus the container is automatically stopped. 
4. The token is returned

```go
package main

import (
	"bytes"
	"context"
	"io"
	"strings"
	"time"

	"github.com/ory/dockertest/v3"
	"github.com/ory/dockertest/v3/docker"
	"golang.org/x/sync/errgroup"
)
func GetSigningSecretFromStripeContainer() string {
	dp, err := dockertest.NewPool("")
	if err != nil {
		panic(err)
	}

	forwarder, err := dp.RunWithOptions(&dockertest.RunOptions{
		Repository: "stripe/stripe-cli",
		Tag:        "v1.19.1",
	})
	if err != nil {
		panic(err)
	}

	output := &bytes.Buffer{}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	var signingSecret string
	eg := errgroup.Group{}
	eg.Go(func() error {
		defer cancel()

		for {
			ln, err := output.ReadString('\n')
			if err == io.EOF {
				<-time.After(100 * time.Millisecond)
				continue
			}
			if err != nil {
				return err
			}
			if strings.Contains(ln, "Ready!") {
				ln = ln[strings.Index(ln, "whsec_"):]
				signingSecret = ln[:strings.Index(ln, " ")]
				return nil
			}
		}
	})

	dp.Client.Logs(docker.LogsOptions{
		Context:      ctx,
		Stderr:       true,
		Follow:       true,
		RawTerminal:  true,
		Container:    forwarder.Container.ID,
		OutputStream: output,
	})

	eg.Wait()

	return signingSecret
}

func main() {
	println(GetSigningSecretFromStripeContainer())
}
```

So, the issue may be clear from the description but here it is spelled out: one goroutine writes to a (growing) byte buffer, another one reads from it, and there is no synchronization: that's a clear data race.

What is interesting here is that we have to pass an `io.Writer` for the `OutputStream` to the library, and this library will write to the writer we passed. We cannot insert a mutex lock anywhere around the write site, since we do not control the library and there no hooks (e.g. pre/post write callbacks) to do so.


### The fix

We implement our own writer that does the synchronization with a mutex:

```go
type SyncWriter struct {
	Writer io.Writer
	Mtx    *sync.Mutex
}

func NewSyncWriter(w io.Writer, mtx *sync.Mutex) io.Writer {
	return &SyncWriter{Writer: w, Mtx: mtx}
}

func (w *SyncWriter) Write(p []byte) (n int, err error) {
	w.Mtx.Lock()
	defer w.Mtx.Unlock()

	written, err := w.Writer.Write(p)

	return written, err
}
```

We pass it as is to the third-party library, and when we want to read the byte buffer, we lock the mutex first:

```diff
diff --git a/cmd-sg/main.go b/cmd-sg/main.go
index 5529d90..42571b9 100644
--- a/cmd-sg/main.go
+++ b/cmd-sg/main.go
@@ -5,6 +5,7 @@ import (
 	"context"
 	"io"
 	"strings"
+	"sync"
 	"time"
 
 	"github.com/ory/dockertest/v3"
@@ -12,6 +13,24 @@ import (
 	"golang.org/x/sync/errgroup"
 )
 
+type SyncWriter struct {
+	Writer io.Writer
+	Mtx    *sync.Mutex
+}
+
+func NewSyncWriter(w io.Writer, mtx *sync.Mutex) io.Writer {
+	return &SyncWriter{Writer: w, Mtx: mtx}
+}
+
+func (w *SyncWriter) Write(p []byte) (n int, err error) {
+	w.Mtx.Lock()
+	defer w.Mtx.Unlock()
+
+	written, err := w.Writer.Write(p)
+
+	return written, err
+}
+
 func GetSigningSecretFromStripeContainer() string {
 	dp, err := dockertest.NewPool("")
 	if err != nil {
@@ -27,6 +46,8 @@ func GetSigningSecretFromStripeContainer() string {
 	}
 
 	output := &bytes.Buffer{}
+	outputMtx := sync.Mutex{}
+	writer := NewSyncWriter(output, &outputMtx)
 
 	ctx, cancel := context.WithCancel(context.Background())
 	defer cancel()
@@ -37,7 +58,9 @@ func GetSigningSecretFromStripeContainer() string {
 		defer cancel()
 
 		for {
+			outputMtx.Lock()
 			ln, err := output.ReadString('\n')
+			outputMtx.Unlock()
 			if err == io.EOF {
 				<-time.After(100 * time.Millisecond)
 				continue
@@ -59,7 +82,7 @@ func GetSigningSecretFromStripeContainer() string {
 		Follow:       true,
 		RawTerminal:  true,
 		Container:    forwarder.Container.ID,
-		OutputStream: output,
+		OutputStream: writer,
 	})
 
 	eg.Wait()
```


### Learnings

Most types in the Go standard library (or third-party libraries for that matter) are *not* concurrency safe and synchronization is typically on you. I still often see questions on the internet about that, so assume it is not until the documentation states otherwise.

It would also be nice if more types have a 'sync' version, e.g. `SyncWriter`, `SyncReader`, etc.

## Conclusion

The Go race detector is great but will not detect all data races. Data races will cause you pain and suffering, be it flaky tests, weird production errors, or in the worst case memory corruption. 

Due to how easy it is to spawn goroutines without a care in the world (and also to run tests in parallel), it will happen to you. It's not a question of if, just when, how bad, and how many days/weeks it will cost you to find them and fix them.

If you are not running your test suite with the race detector enabled, you have numerous data races in your code. That's just a fact.

Go the language and the Go linter ecosystem do not have nearly enough answers to this problem. Some language features make it way too easy to trigger data races, for example implicit capture of outer variables in closures.

The best option left to Go developers is to try to reach 100% test coverage of their code and run the tests with the race detector on.

We should be able to do better in 2025. Just like with memory safety, when even expert developers regularly produce data races, it's the fault of the language/tooling/APIs/etc. It is not enough to blame humans and demand they just 'do better'.

## Ideas to improve the status quo

Ideas for the Go language:

1. Add explicit capture lists for closures, just like in C++.
1. Add a lint to forbid using the implicit capture syntax in closures (a.k.a.: current Go closures). I am fine writing a stand-alone plain function instead, if that means keeping my sanity and removing an entire category of errors. I have also seen implicit captures cause logic bugs and high memory usage in the past.
1. Support `const` in more places. If something is constant, there cannot be data races with it.
1. Generate a `Clone()` function in the compiler for every type (like Rust's `derive(Clone)`). Maybe it's opt-in or opt-out, not sure. Or perhaps it's a built-in like `make`.
1. Add a `freeze()` functionality like JavaScript's `Object.freeze()` to prevent an object from being mutated.
1. Expand the standard library documentation to have more details concerning the concurrency safety of certain types and APIs.
1. Expand the Go memory model documentation and add examples. I have read it many times and I am still unsure if concurrent writes to separate fields of a struct is legal or not, for example.
1. Consider adding better, higher-level APIs for synchronization primitives e.g. `Mutex` by taking inspiration from other languages. This has been done successfully in the past with `WaitGroup` compared to using raw goroutines and channels.

Ideas for Go programs:

1. Consider never using Go closures, and instead using plain functions that cannot implicit capture outer variables.
1. Consider using goroutines as little as possible, whatever the API to manage them.
1. Consider spawning an OS process instead of a goroutine for isolation. No sharing of data means no data race possible.
1. Deep clone abundantly (just like in Rust). Memory (especially cache) is lightning fast. Your Go program will anyways not be bottlenecked by that, I guarantee it. Memory usage should be monitored, though, but will probably be fine.
1. Avoid global mutable variables.
1. Carefully audit resource sharing code: caches, connection pools, OS process pools, HTTP clients, etc. They are likely to contain data races.
1. Run the tests with the race detector on, all of them, always, from day one. Inspect the test coverage to know which areas may be uncharted areas in terms of concurrency safety.
1. Study places where a shallow copy may take place, e.g. function arguments passed by value and assignments. Does the type require a deep copy? Each non-trivial type should have documentation stating that.
1. If a type can be implemented in an immutable fashion, then it's great because there is no data race possible. For example, the `string` type in Go is immutable.
