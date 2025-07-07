Title: A subtle data race in Go
Tags: Go
---

*Discussions: [/r/golang](https://www.reddit.com/r/golang/comments/1l92qe9/a_subtle_data_race_in_go).*

At work, a good colleague of mine opened a PR titled: 'fix data race'. Ok, I thought, let's see. They probably forgot a mutex or an atomic. Or perhaps they returned a pointer to an object when they intended to return a copy of the object. Easy to miss.

Then I was completely puzzled.

The diff for the fix was very small, just a few lines, and contained neither mutexes, nor pointers, nor goroutines, nor any concurrency construct for that matter. How could it be? 

## The original code

I have managed to reproduce the race in a self-contained Go program resembling the real production code, and the diff for the fix is basically the same as the real one. It's a web server with a middleware to do rate limiting. The actual rate limiting code is omitted because it does not matter:

```go
package main

import (
	"fmt"
	"net/http"
	"strings"
)

func NewMiddleware(handler http.Handler, rateLimitEnabled bool) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/admin") {
			rateLimitEnabled = false
		}

		if rateLimitEnabled {
			fmt.Printf("path=%s rate_limit_enabled=yes\n", r.URL.Path)
			// Rate limiting logic here ...
		} else {
			fmt.Printf("path=%s rate_limit_enabled=no\n", r.URL.Path)
		}

		handler.ServeHTTP(w, r)
	})
}

func handle(w http.ResponseWriter, r *http.Request) {
	w.Write([]byte("hello!\n"))
}

func main() {
	handler := http.HandlerFunc(handle)
	middleware := NewMiddleware(handler, true)
	http.Handle("/", middleware)

	http.ListenAndServe(":3001", nil)
}
```

It's very typical Go code I would say. The only interesting thing going on here, is that we never do rate-limiting for the admin section of the site. That's handy if a user is abusing the site, and the admin has to go disable their account as fast as possible in the admin section.

The intent behind the `rateLimitEnabled` parameter was likely to have it 'off' in development mode, and 'on' in production, based on some environment variable read in `main` (also omitted here).

Can you spot the data race? Feel free to pause for a second. I glanced at code very similar to this for like, 10 minutes, to even begin to form hypotheses about a data race, while knowing from the get go there is a data race, and having the fix in front of me.

## Symptoms of the bug

Let's observe the behavior with a few HTTP requests:

```sh
$ curl http://localhost:3001/
$ curl http://localhost:3001/admin
$ curl http://localhost:3001/
$ curl http://localhost:3001/
```

We see these server logs:

```text
path=/ rate_limit_enabled=yes
path=/admin rate_limit_enabled=no
path=/ rate_limit_enabled=no
path=/ rate_limit_enabled=no
```

*The actual output could vary from machine to machine due to the data race. This is what I have observed on my machine. Reading the Go memory model, another legal behavior in this case could be to immediately abort the program. No symptoms at all is not possible. The only question is when the race will manifest. When receiving lots of HTTP requests, it might not happen right after the first request. See the 'Conclusion and recommendations' section for more information.*

The third and fourth log are definitely wrong. We would have expected:

```text
path=/ rate_limit_enabled=yes
path=/admin rate_limit_enabled=no
path=/ rate_limit_enabled=yes
path=/ rate_limit_enabled=yes
```

The non-admin section of the site should be rate limited, always. But it's apparently not, starting from the second request. Trying to access the admin section disables rate limiting for everyone, until the next server restart! So this data race just became a security vulnerability as well!

In the real code at work it was actually not a security issue, since the parameter in question governed something related to metrics about rate limiting, not rate limiting itself, so the consequence was only that some metrics were wrong. Which is how the bug was initially spotted.

## The fix

The diff for the fix looks like this:

```text
diff --git a/http-race.go b/http-race.go
index deff273..6c73b7e 100644
--- a/http-race.go
+++ b/http-race.go
@@ -6,8 +6,10 @@ import (
 	"strings"
 )
 
-func NewMiddleware(handler http.Handler, rateLimitEnabled bool) http.Handler {
+func NewMiddleware(handler http.Handler) http.Handler {
 	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
+		rateLimitEnabled := true
+
 		if strings.HasPrefix(r.URL.Path, "/admin") {
 			rateLimitEnabled = false
 		}
@@ -29,7 +31,7 @@ func handle(w http.ResponseWriter, r *http.Request) {
 
 func main() {
 	handler := http.HandlerFunc(handle)
-	middleware := NewMiddleware(handler, true)
+	middleware := NewMiddleware(handler)
 	http.Handle("/", middleware)
 
 	http.ListenAndServe(":3001", nil)

```

Just transforming a function argument to a local variable. No other change. How can it be?

We can confirm that the behavior is now correct:

```sh
$ curl http://localhost:3001/
$ curl http://localhost:3001/admin
$ curl http://localhost:3001/
$ curl http://localhost:3001/
```

Server logs:

```text
path=/ rate_limit_enabled=yes
path=/admin rate_limit_enabled=no
path=/ rate_limit_enabled=yes
path=/ rate_limit_enabled=yes
```

As expected.

## Explanation

Go, like many languages, has closures: functions that implicitly capture their environment as needed so that surrounding variables can be accessed within the function scope.

The `NewMiddleware` function returns a closure (the middleware) that implicitly captures the `rateLimitEnabled` variable. The intent in the original code was to treat the function argument, which is passed by value (as most programming languages do), as essentially an automatic local variable. When mutated, nothing is visible outside of the scope of the function, as if we just used a local variable.

But...it's neither a plain local variable nor a plain function argument: it's a variable existing outside of the closure, captured by it. So when the closure mutates it, this mutation *is* visible to the outside. We can confirm our hypothesis by toggling a knob to ask the compiler to log this:

```sh
$ go build -gcflags='-d closure=1' http-race.go
./http-race.go:9:26: heap closure, captured vars = [rateLimitEnabled handler]
[...]
```

We can plainly see that `rateLimitEnabled` is being captured.


The Go compiler conceptually transforms the closure into code like this:

```go
type ClosureEnv struct {
	rateLimitEnabled *bool
	handler          *http.Handler
}

func rateLimitMiddleware(w http.ResponseWriter, r *http.Request, env *ClosureEnv) {
	if strings.HasPrefix(r.URL.Path, "/admin") {
		env.rateLimitEnabled = false
	}

	if env.rateLimitEnabled {
		fmt.Printf("path=%s rate_limit_enabled=yes\n", r.URL.Path)
		// Rate limiting logic here ...
	} else {
		fmt.Printf("path=%s rate_limit_enabled=no\n", r.URL.Path)
	}

	env.handler.ServeHTTP(w, r)
}
```

Note that `ClosureEnv` holds a pointer to `rateLimitEnabled`. If it was not a pointer, the closure could not modify the outer values. That's why closures capturing their environment lead in the general case to heap allocations so that environment variables live long enough.

---

Ok, it's a logic bug. Not yet a data race, right? (We could debate whether all data races are also logic bugs. But let's move on).

Well, when is this closure called? When handling **every single incoming HTTP request, concurrently.**

It's as if we spawned many goroutines which all called this function concurrently, and said function reads and writes an outer variable without any synchronization. So it is indeed a data race.


We can confirm with the compiler that no variable is captured by accident now that the patch is applied:

```sh
$ go build -gcflags='-d closure=1' http-race.go
./http-race.go:9:26: heap closure, captured vars = [handler]
```


## Conclusion and recommendations

This was quite a subtle data race which took me time to spot and understand. The go race detector did not notice it, even when throwing lots of concurrent requests at it. LLMs, when asked to analyze the code, did not spot it.

The good news is: The Go memory model gives us some guarantees for data races. Contrary to C or C++, where a data race means the wild west of undefined behavior, it only means in Go that we may read/write the wrong value, when reading/writing machine word sizes or smaller (which is the case of booleans). We even find quite a strong guarantee in the [Implementation Restrictions for Programs Containing Data Races](https://go.dev/ref/mem#restrictions) section: 

> each read must observe a value written by a preceding or concurrent write. 

However, the [official documentation](https://go.dev/ref/mem#restrictions) also warns us that reading more than one machine word at once may have dire consequences:

>  This means that races on multiword data structures can lead to inconsistent values not corresponding to a single write. When the values depend on the consistency of internal (pointer, length) or (pointer, type) pairs, as can be the case for interface values, maps, slices, and strings in most Go implementations, such races can in turn lead to arbitrary memory corruption. 


So how can we avoid this kind of data race from occurring? How can we adapt our code style, if tools do not spot it? Well, you may have been bitten in the past by logic bugs using Go closures, when the wrong variable is captured by accident. The recommendation in these cases is typically: do not capture, pass variables you need inside the closure as function arguments to the closure (i.e., pass explicitly by value instead of implicitly by reference). That's probably a good idea but also: it's so easy to forget to do it. 

The root cause is that the environment is captured implicitly. When I was writing C++ I actually liked the lambda [syntax](https://en.cppreference.com/w/cpp/language/lambda.html) because it started with a capture list. Every capture was explicit! It was slightly verbose but as we have seen: it serves to avoid real production bugs! For example:

```c++
int a = 1, b = 2, c = 3;
auto fn =  [a, b, c]() { return a + b + c; };
int res = fn();
``` 

After writing quite a lot of code in C, Zig and Odin, which all do *not* have support for closures, I actually do not miss them. I even think it might have been a mistake to have them in most languages. Every single time I have to deal with code with closures, it is always harder to understand and debug than code without them. It can even lead to performance issues due to hidden memory allocations, and makes the compiler quite a bit more complex. The code that gives me the most headaches is functions returning functions returning functions... And some of these functions in the chain capture their environment implicitly... Urgh.

---

So here's my personal, **very subjective** recommendation when writing code in any language including in Go:

1. Avoid closures if possible. Write standard functions instead. This will avoid accidental captures and make all arguments explicit.
1. Avoid writing callback-heavy code a la JavaScript. Closures usually show up most often in this type of code. It makes debugging and reading hard. Prefer using Go channels, events like `io_uring`, or polling functions like `poll`/`epoll`/`kqueue`. In other words, let the caller pull data, instead of pushing data to them (by calling their callback with the new data at some undetermined future point).
1. Prefer, when possible, using OS processes over threads/goroutines, to have memory isolation between tasks and to remove entire categories of bugs. You can always map memory pages that are accessible to two or more processes if you absolutely need shared mutable state. Although message passing (e.g. over pipes or sockets) would be less error-prone. Another advantage with OS processes is that you can set resource limits on them e.g. on memory usage. Yes, some cases will require using threads. I'm talking about *most* cases here. 
1. Related to the previous point: Reduce global mutable state to the absolute minimum, ideally zero.

---

And here's my personal, **very subjective** recommendation for future programming language designers:

1. Consider not having closures in your language, at all. Plain function (pointers) are still fine.
2. If you *really* must have closures:
  - Consider forcing the developer to explicitly write which variables are captured (like C++ does).
  - Have a knob in your compiler to easily see what variables are being captured in closures (like Go does).
  - Have good statical analysis to spot common problematic patterns (`golangci-lint` finds the bug neither in our reproducer nor in the real production service).
  - Consider showing in the editor captured variables in a different way, for example with a different color, from normal variables
3. Implement a race detector (even if that just means using Thread sanitizer). It's not a perfect solution because some races will not be caught, but it's better than nothing.
4. Document precisely what is the memory model you offer and what are legal behaviors in the presence of data races. Big props to Go for doing this very well. 
5. Consider making function arguments not re-assignable. Force the developer to define a local variable instead.


## Addendum: A reproducer program for the Go race detector

Here is a reproducer program with the same issue but this time the Go race detector finds the data race:

```go
package main

import (
	"fmt"
	"sync"
	"time"
)

func NewMiddleware(rateLimitEnabled bool) func() {
	return func() {
		if time.Now().Nanosecond()%2 == 0 {
			rateLimitEnabled = false
		}

		if rateLimitEnabled {
			fmt.Printf("rate_limit_enabled=yes\n")
			// Rate limiting logic here ...
		} else {
			fmt.Printf("rate_limit_enabled=no\n")
		}
	}
}

func main() {
	middleware := NewMiddleware(true)
	count := 100
	wg := sync.WaitGroup{}
	wg.Add(count)

	for range count {
		go func() {
			middleware()
			wg.Done()
		}()
	}

	wg.Wait()
}
```

Here's the output:

```sh
$ go run -race race_reproducer.go
rate_limit_enabled=no
==================
WARNING: DATA RACE
Read at 0x00c00001216f by goroutine 8:
  main.main.NewMiddleware.func2()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:15 +0x52
  main.main.func1()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:32 +0x33

Previous write at 0x00c00001216f by goroutine 7:
  main.main.NewMiddleware.func2()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:12 +0x45
  main.main.func1()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:32 +0x33

Goroutine 8 (running) created at:
  main.main()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:31 +0xe4

Goroutine 7 (running) created at:
  main.main()
      /home/pg/scratch/http-race/reproducer/race_reproducer.go:31 +0xe4
==================
```

It's not completely clear to me why the Go race detector finds the race in this program but not in the original program since each HTTP request is handled in its own goroutine, both programs should be analogous. Maybe not enough concurrent HTTP traffic?

## Addendum: The fixed code in full

```go
package main

import (
	"fmt"
	"net/http"
	"strings"
)

func NewMiddleware(handler http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		rateLimitEnabled := true

		if strings.HasPrefix(r.URL.Path, "/admin") {
			rateLimitEnabled = false
		}

		if rateLimitEnabled {
			fmt.Printf("path=%s rate_limit_enabled=yes\n", r.URL.Path)
			// Rate limiting logic here ...
		} else {
			fmt.Printf("path=%s rate_limit_enabled=no\n", r.URL.Path)
		}

		handler.ServeHTTP(w, r)
	})
}

func handle(w http.ResponseWriter, r *http.Request) {
	w.Write([]byte("hello!\n"))
}

func main() {
	handler := http.HandlerFunc(handle)
	middleware := NewMiddleware(handler)
	http.Handle("/", middleware)

	http.ListenAndServe(":3001", nil)
}
```
