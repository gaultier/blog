Title: Detecting goroutine leaks with DTrace
Tags: Go, DTrace
---

Recently I read a cool blog [article](https://antonz.org/detecting-goroutine-leaks/) about new changes in Go 1.25 and (as the time of writing, upcoming) 1.26 to more easily track goroutine leaks.

I thought: ok, that's nice, that's a real problem. But what if you cannot use this `goroutineleak` profile? Perhaps you are stuck on an old Go version, perhaps you cannot change the code easily to enable this profile. 

What to do then? Well, as always, DTrace comes to the rescue. Let's track the creation and deletion of goroutines in our function. If there are more created goroutines than deleted ones, we have a leak.


## Watch goroutine be created and destroyed

Let's start with the simplest Go program using goroutines we can think of:

```go
package main

import (
	"time"
)

func Foo() {
	go func() int { return 1 }() // Obviously does not leak!
}

func main() {
	Foo()
}
```

Let's trace all goroutine creations and deletions, as well as the functions entry and return in this program, with the `-F` DTrace command line option:

```dtrace
// Creates a new goroutine.
pid$target::runtime.newproc1:

// Destroys a goroutine.
pid$target::runtime.gdestroy:

pid$target::main.*: 
```

*Functions starting with `runtime.` are from the Go runtime.*

We see:

```text
CPU FUNCTION                                 

[...] // Some Go runtime goroutines elided, spawned before `main`.

 12  -> main.main                             
 12    -> main.main                           
 12      -> main.Foo                          
 12        -> runtime.newproc1                
 12        <- runtime.newproc1                
 12      <- main.Foo                          
 12      -> main.Foo.gowrap1                  
 12        -> main.Foo.func1                  
 12        <- main.Foo.func1                  
 12      <- main.Foo.gowrap1                  
 12      -> runtime.gdestroy                  
 12      <- runtime.gdestroy  
dtrace: pid 28627 has exited
  0 | main.main:return                        
  0  <- main.main 
```

We notice a few interesting things:

- The Go runtime spawns itself some goroutines, which we should ignore
- The `go` keyword only enqueues a task in a thread-pool to be run later, essentially (it does some more stuff such as allocating a stack for our goroutine on the heap, etc). The task (here, the closure `main.Foo.func1`) does not run yet. Experienced Go developers know this of course, but I find the Go syntax confusing: `go bar()` or `go func()  {} ()` looks like the function in the goroutine starts executing right away, but no: when it will get to run is unknown.
- Thus, and as we see in the output, the closure (`main.Foo.func1`) in the goroutine spawned inside the `Foo` function, starts running in this case *after* `Foo` returned, and as a result, the goroutine is destroyed after `Foo` has returned. However, it is obviously not a leak: the goroutine does basically nothing.

At this point we should define what a 'goroutine leak' is: [TODO]



In a big, real program, goroutines are being destroyed left and right, even while our function is executing, or after it is done executing.

So to do it correctly, we need to track the set of our own goroutines. The function of interest in the Go runtime is `runtime.newproc1`: it returns a pointer to a newly allocated goroutine object, so we can use this as a 'goroutine id'. This value is accessible in `arg1` in DTrace (on my system; this depends on the system we are running our D script on. The Go ABI is documented but differs between systems and so it requires a bit of trial and error in DTrace to know which `argN` contains the information we need).

Then, in `runtime.gdestroy`, we can react only to our own goroutines being destroyed. There, `arg0` contains the goroutine id/pointer to be destroyed.

## The D script

```dtrace
int goroutines_count;

pid$target::main.main:entry 
{
    t=1;  // Only track goroutines spawned from inside `main` (and its callees).
}

pid$target::runtime.newproc1:return 
/t!=0/ 
{
    goroutines[arg1] = 1; // Add the new goroutine to the tracking set of active goroutines we have spawned.
    goroutines_count += 1; // Increment the counter of active goroutines.
    printf("goroutine %p created: count=%d\n", arg1, goroutines_count);
} 

pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/ 
{
    
    goroutines_count -= 1; // Decrement the counter of active goroutines.
    printf("goroutine %p destroyed: count=%d\n", arg0, goroutines_count);
    goroutines[arg0] = 0; // Remove the goroutine from the tracking set of active goroutines we have spawned.

}
```


We could even use `ustack()` in the `runtime.newproc1:return` probe to see what Go function in our code is creating the goroutine. This is not the case for `runtime.gdestroy:entry`: the goroutine could be destroyed at any point in the program as explained before, even after the function that created it is long gone (along with its callers, etc), so a call stack here does not make sense in the general case.

## A leaky program

Let's take the same leaky Go program as the original [article](https://antonz.org/detecting-goroutine-leaks/):

```go
package main

import (
	"fmt"
	"runtime"
	"time"
)

// Gather runs the given functions concurrently
// and collects the results.
func Gather(funcs ...func() int) <-chan int {
	out := make(chan int)
	for _, f := range funcs {
		go func() {
			out <- f()
		}()
	}
	return out
}

func main() {
	Gather(
		func() int { return 11 },
		func() int { return 22 },
		func() int { return 33 },
	)

	time.Sleep(50 * time.Millisecond)
	nGoro := runtime.NumGoroutine() - 1 // minus the main goroutine
	fmt.Println("nGoro =", nGoro)
}
```

This prints `3` since our code spawns a goroutine for each argument, and there are 3 arguments. Because the channel is unbuffered and never read from, the 3 goroutines run forever and leak. Oops.


Note that our D script is already more powerful than the naive Go way of using `runtime.NumGoroutine()` which returns *all* goroutines active in the program, even goroutines that our function did not spawn itself (hence the `-1` in the Go code in this simplistic example).

Let's use our brand new D script on this program:

```text
goroutine 14000102fc0 created: count=1
goroutine 14000103180 created: count=2
goroutine 14000103340 created: count=3
```

We see indeed that there are 3 leaky goroutines (created and never destroyed) by the end of the program.

Let's now fix the Go program by reading from the returned channel:


```diff
diff --git a/main.go b/main.go
index 01659c2..bfd7ee4 100644
--- a/main.go
+++ b/main.go
@@ -19,12 +19,17 @@ func Gather(funcs ...func() int) <-chan int {
 }
 
 func main() {
-	Gather(
+	out := Gather(
 		func() int { return 11 },
 		func() int { return 22 },
 		func() int { return 33 },
 	)
 
+	total := 0
+	for range 3 {
+		total += <-out
+	}
+
 	time.Sleep(50 * time.Millisecond)
 	nGoro := runtime.NumGoroutine() - 1 // minus the main goroutine
 	fmt.Println("nGoro =", nGoro)
```

And the fixed program now shows all of our goroutines being deleted, no more leaks:

```text
goroutine 140000036c0 created: count=1
goroutine 14000003880 created: count=2
goroutine 14000003a40 created: count=3
goroutine 14000003a40 destroyed: count=2
goroutine 14000003880 destroyed: count=1
goroutine 140000036c0 destroyed: count=0
```

If we were willing to do some post-processing of the DTrace output, we could even print the call stack with `ustack()` when a goroutine is created along with the goroutine id/pointer, to be able to track down where the leaky goroutines came from. Since DTrace maps cannot be iterated on and cannot be printed as a whole, we cannot simply store that call stack information in the `goroutines` map and print the whole map at the end, in pure DTrace.


## Conclusion

With two simple DTrace probes, we can observe the Go runtime creating and destroying goroutines, along with a way to uniquely identify this goroutine. From these two primitives, we can track goroutine leaks, but that's not all! We could do a lot more, all in DTrace (or possibly with a bit of post-processing):

- How long does a goroutine live, with a histogram? (Hint: `pid$target::runtime.gdestroy:return { @[""] = quantize(timestamp - spawned[arg0]) }`)
- What is the peak (maximum) number of active goroutines? (Hint: `pid$target::runtime.newproc1:return { @[ustack()] = max() }`)
- What places in the code spawn the most goroutines? (Hint: `pid$target::runtime.newproc1:return { @[ustack()] = count() }`
- How much time passes between asking the Go runtime to create the goroutine, and the code in the goroutine actually running?
- Etc

Which is pretty cool if you ask me, given that:

- All of that works for all versions of Go, even the very first one, and no need to upgrade to a new version
- No need to ask the Go maintainers to add one more metric or profile we need
- No need to change and recompile the application
- No overhead when not running, and safe to use in production. 
- Same behavior when on or off. The Go runtime has a lot of different code paths depending if tracing is enabled, if the race detector is enabled, if valgrind is enabled... that makes the code quite complex, and potentially behave differently depending on what is on/off. With DTrace, we know that peeking inside the inner workings of the Go runtime does not change its behavior.

Oh and by the way, try these probes:

- `runtime.*chan*`: see channel operations such as sends and receives
- `runtime.gc*`: see GC operations such as mark, sweep, marking roots
- `runtime.*alloc*`: see memory allocator operations

When used in conjunction with tracking functions from our code, like we did in the first DTrace snippet in this article, Go feels a lot less magic! I wish I did that as a beginner.
