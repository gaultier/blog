Title: A subtle data race in Go
Tags: Go
---

At work, an good colleague of mine opened a PR titled: 'fix data race'. Ok, I thought, let's see. They probably forgot a mutex or an atomic. Or perhaps they returned a pointer to an object when they intended to return a copy of the object. Easy to miss.

Then I was completely puzzled.

The diff for the fix was very small, just a few lines, and contained neither mutexes nor pointers nor any concurrency construct for that matter. How could it be? 

## The original code

I have managed to reproduce the race in a self-contained Go program ressembling the real production code, and the diff for the fix is basically the same as the real one. It's a web server with a middleware to do rate limiting:

```go
package main

import (
"net/http"
"strings"
)

func NewMiddleware(handler http.Handler, rateLimitEnabled bool) http.Handler {
    return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
        if strings.HasPrefix(r.URL.Path, "/admin") {
            rateLimitEnabled = false
        }

        if rateLimitEnabled {
            println("rate limiting...")
            // Rate limiting logic here ...
        } else {
            println("not rate limiting")
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

It's very typical Go code I would say. The only interesting thing going on here is that we do not do rate-limiting for the admin section of the site. 

Can you spot the data race? Feel free to pause for a second. I glanced at code very similar to this for like, 10 minutes, to even begin to have hypotheses about a data race, while knowing from the get go there is a data race, and having the fix in front of me.

## Symptoms of the bug

Let's observe the behavior with a few HTTP requests:

```
$ curl http://localhost:3001/
$ curl http://localhost:3001/admin
$ curl http://localhost:3001/
```

We see these server logs:

```
rate limiting...
not rate limiting
not rate limiting
```

*The actual output could vary from machine to machine due to the data race. This is what I have observed on one machine.*

The third log is definitely wrong. We would have expected:

```
rate limiting...
not rate limiting
rate limiting...
```

The non-admin section of the site should be rate limited! But it's apparently not. And every single request after the second one is *not* rate limited, for everyone. So this data-race just became a security vulnerability as well!

## The fix

The diff for the fix looks like this:

```
diff --git a/http-race.go b/http-race.go
index 3d94a71..90b571e 100644
--- a/http-race.go
+++ b/http-race.go
@@ -5,8 +5,10 @@ import (
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
@@ -28,7 +30,7 @@ func handle(w http.ResponseWriter, r *http.Request) {
 
 func main() {
 	handler := http.HandlerFunc(handle)
-	middleware := NewMiddleware(handler, true)
+	middleware := NewMiddleware(handler)
 	http.Handle("/", middleware)
 
 	http.ListenAndServe(":3001", nil)
```

Just transforming a function argument to a local variable. No other change. What?

We can confirm that the behavior is now correct:

```
$ curl http://localhost:3001/
$ curl http://localhost:3001/admin
$ curl http://localhost:3001/
```

Server logs:

```
rate limiting...
not rate limiting
rate limiting...
```

As expected.

## Explanation

Go, like many languages, has closures: functions that implicitly capture their environment as needed so that surrounding variables can be accessed within the function scope.

The `NewMiddleware` returns a closure (the middleware) that captures `rateLimitEnabled`. The intent in the original code was to treat the function argument, which is passed by value as many programming languages do, as essentially an automatic local variable. When mutated, nothing is visible outside of the scope of the function, as if we just used a local variable.

But...it's neither a plain local variable nor a plain function argument: it's a variable existing outside of the closure, captured by it. So when the closure mutates it, it *is* visible to the outside. We can confirm this by toggling a knob to ask the compiler to log this:

```sh
$ go build -gcflags='-d closure=1' http-race.go
./http-race.go:9:26: heap closure, captured vars = [rateLimitEnabled handler]
[...]
```

We can plainly see that `rateLimitEnabled` is being captured.

---

Ok, it's a logic bug. Not yet a data race, right? 

Well, when is this closure called? When handling **every single incoming HTTP request, concurrently.**

It's as if we spawned many goroutines which all called this function concurrently. So it is indeed a data race.

Here is thus the fixed code:

```go
package main

import (
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
			println("rate limiting...")
			// Rate limiting logic here ...
		} else {
			println("not rate limiting")
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

We can confirm with the compiler that no variable is captured by accident now:

```sh
$ go build -gcflags='-d closure=1' http-race.go
./http-race.go:9:26: heap closure, captured vars = [handler]
```


## Conclusion and recommendations

This was quite a subtle data race which took me time to spot and understand. The go race detector did not notice it, even when throwing lots of concurrent requests at it. LLMs, when asked to analyze the code, did not spot it.

The good news is: The Go memory model gives us some guarantees for data races. Contrary to C or C++, where a data race means the wild west of undefined behavior, it only means in Go that we may read/write the wrong value, when reading/writing machine word sizes or smaller (which is the case of booleans). However, the official documentation warns us that reading more than one machine word at once (such as a big struct) may lead to undefined behavior due to breaking pre-conditions.


So how can we avoid this kind of data race from occurring? How can we adapt our code style, if tools do not spot it? Well, you may have been bitten in the past by logic bugs using Go closures, when the wrong variable is captured by accident. The recommendation in these cases is typically: do not capture, pass variables you need inside the closure as function arguments to the closure (i.e., by value instead of by reference). That's probably a good idea but also: it's so easy to forget to do it. 

The root cause is that the environment is captured implicitly. When I was writing C++ I actually liked the lambda syntax because it started with a capture list. Every capture was explicit! It was slightly verbose but as we have seen: it serves to avoid real production bugs!

After writing quite a lot of code in C, Zig and Odin, which all do not have support for closures, I do not miss them. I even think they might have been a mistake to have in most languages. Every singly time I have to deal with code with closures, it is always harder to understand and debug than code without them. It can even lead to performance issues due to hidden memory allocations, and makes the compiler quite a bit more complex. What's wrong with writing a plain function? 

Be explicit, avoid bugs. Probably stop using closures that implicitly capture stuff.
