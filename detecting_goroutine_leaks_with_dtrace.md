Title: Detecting goroutine leaks with DTrace
Tags: Go, DTrace
---

*For a gentle introduction to DTrace especially in conjunction with Go, see my past article: [An optimization and debugging story with Go and DTrace](/blog/an_optimization_and_debugging_story_go_dtrace.html).*

Recently I read a cool blog [article](https://antonz.org/detecting-goroutine-leaks/) about new changes in Go 1.25 and (as the time of writing, upcoming) 1.26 to more easily track goroutine leaks.

I thought: ok, that's nice, that's a real problem. But what if you cannot use this `goroutineleak` profile? Perhaps you are stuck on an old Go version, perhaps you cannot change the code easily to enable this profile. 

What to do then? Well, as always, DTrace comes to the rescue!

## What is a goroutine leak, and why is it a problem?

Simply, a goroutine 'leaks' if it is blocked waiting on an unreachable object: a mutex, channel, wait condition, etc. No place in the program still holds a reference to this object, which means this object can never be 'unlocked' and the goroutine can never be unblocked.

And it turns out that Go currently offers us various ways to accidentally enter this case: forgetting to read from a channel for example.

Why is this an issue? Well, goroutines are designed to not take much memory, at least initially (around 2 KiB), so that our program can spawn millions of them. Note that the memory used by a goroutine can and usually does grow. 

This memory is not reclaimed until the goroutine is destroyed. If the goroutine lives forever, we have a memory leak on our hands.

Additionally, due to the M:N model, the Go runtime juggles N goroutines on M physical cores. So having lots of 'zombie' goroutines mixed with valid goroutines means having a bigger memory and CPU usage than normal when 
doing garbage collection, scheduling, collecting statistics, etc, in the Go runtime.


## Watch goroutines be created and destroyed with DTrace

Alright, let's start slow first with the simplest Go program using goroutines we can think of:

```go
package main

import (
	"time"
)

func Foo() {
	go func() int { return 1 }()
}

func main() {
	Foo()
}
```

Let's trace all goroutine creations and deletions, as well as the functions entry and return in this program.

Peeking at the Go runtime [code](https://github.com/golang/go/blob/master/src/runtime/proc.go), we identify two functions of interest:

- `runtime.newproc1` creates a new goroutine object and returns it. When we use the `go` keyword in our Go code, the compiler generates a call to `runtime.newproc` which in turn calls `runtime.newproc1`.
- `runtime.gdestroy` takes a goroutine object as an argument and destroys it. It is called from inside the Go runtime.


```dtrace
// Creates a new goroutine.
pid$target::runtime.newproc1:

// Destroys a goroutine.
pid$target::runtime.gdestroy:

pid$target::main.*: 
```

*Functions starting with `runtime.` are from the Go runtime.*

We can use the `-F` DTrace command line option to see the function call tree and see:

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

- The Go runtime spawns itself some goroutines at the beginning (before `main` runs), which we should ignore
- The `go` keyword only enqueues a task in a thread-pool to be run later, essentially (it does some more stuff such as allocating a stack for our goroutine on the heap, etc). The task (here, the closure `main.Foo.func1`) does not run yet. Experienced Go developers know this of course, but I find the Go syntax confusing: `go bar()` or `go func()  {} ()` looks like the function in the goroutine starts executing right away, but no: when it will get to run is unknown.
- Thus, and as we see in the output, the closure (`main.Foo.func1`) in the goroutine spawned inside the `Foo` function, starts running in this case *after* `Foo` returned, and as a result, the goroutine is destroyed after `Foo` has returned. However, it is obviously not a leak: the goroutine does nothing and returns immediately.
- We create the goroutine ourselves, but the Go runtime destroys it for us, automatically, when the function running in the goroutine returns

In a big, real program, goroutines are being destroyed left and right, even while our function is executing, or after it is done executing.

So we need to track the set of our own goroutines. The function of interest in the Go runtime is `runtime.newproc1`: it returns a pointer to a newly allocated goroutine object, so we can use this as a 'goroutine id'. This value is accessible in `arg1` in DTrace (on my system; this depends on the system we are running our D script on. The [Go ABI](https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md) is documented but differs between systems and so it requires a bit of trial and error in DTrace to know which `argN` contains the information we need).

Then, in `runtime.gdestroy`, we can react only to our own goroutines being destroyed. There, `arg0` contains the goroutine id/pointer to be destroyed.

## A naive approach: count goroutines creations and deletions

```dtrace
int goroutines_count;

pid$target::main.main:entry 
{
    t=1;  // Only track goroutines spawned from inside `main` (and its callees).
}

pid$target::runtime.newproc1:return 
/t!=0/ 
{
    this->g = arg1;
    goroutines[this->g] = 1; // Add the new goroutine to the tracking set of active goroutines we have spawned.
    goroutines_count += 1; // Increment the counter of active goroutines.
    printf("goroutine %p created: count=%d\n", this->g, goroutines_count);
} 

pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/ 
{
    this->g = arg0;
    goroutines_count -= 1; // Decrement the counter of active goroutines.
    printf("goroutine %p destroyed: count=%d\n", this->g, goroutines_count);
    goroutines[this->g] = 0; // Remove the goroutine from the tracking set of active goroutines we have spawned.
}
```

A few notes:

- Here we start tracking when entering `main`, but you could choose to do it after the program has initialized some things, or when entering a given component, etc
- We could even use `ustack()` in the `runtime.newproc1:return` probe to see what Go function in our code is creating the goroutine. This is not the case for `runtime.gdestroy:entry`: the goroutine could be destroyed at any point in the program as explained before, even after the function that created it is long gone (along with its callers, etc), so a call stack here does not make sense in the general case and only shows internals from the Go runtime. This is the same as a memory allocation: our code creates a new object with `new()`, and there a call stack is meaningful, and later, the Go runtime will garbage collect it, at which point a call stack does not carry meaning.

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


## A better approach: Track blocked goroutines

Our current D script is flawed: consider a goroutine that does `time.Sleep(10*time.Second)`. It will appear as 'leaking' for 10 seconds, after which it will be destroyed and not appear as 'leaking' anymore. So what's the cutoff? Should we wait one minute, one hour? 

Well, remember the initial definition of a goroutine leak:

> a goroutine 'leaks' if it is blocked waiting on an unreachable object: a mutex, channel, wait condition, etc

Each goroutine in the Go runtime has a 'status' field which is 'idle', 'running', 'waiting' (meaning blocked), 'dead', etc. We need to track that, in order to know how many goroutines are really blocked and leaking!

The Go runtime has a key function to watch, that does this state transition: `runtime.gopark`.

A goroutine is typically 'parked', meaning taken off CPU, when it is waiting on something such as a synchronization object, the network, a system call, etc, to make room for other goroutines to run. Doing so changes the goroutine status from 'running' to something else.

The fourth argument of `runtime.gopark` is a 'block reason' which explains why (if at all) the goroutine is blocked. This way, the Go scheduler knows not to try to run the blocked goroutines since they have no chance to do anything, until the object they are blocked on is unblocked (for example a mutex). This field is [defined](https://github.com/golang/go/blob/master/src/runtime/traceruntime.go#L91) like this in the Go runtime:


```go
// traceBlockReason is an enumeration of reasons a goroutine might block.
[...]
type traceBlockReason uint8

const (
	traceBlockGeneric traceBlockReason = iota
	traceBlockForever
	traceBlockNet
	traceBlockSelect
	traceBlockCondWait
	traceBlockSync
	traceBlockChanSend
	traceBlockChanRecv
	traceBlockGCMarkAssist
	traceBlockGCSweep
	traceBlockSystemGoroutine
	traceBlockPreempted
	traceBlockDebugCall
	traceBlockUntilGCEnds
	traceBlockSleep
	traceBlockGCWeakToStrongWait
	traceBlockSynctest
)
```

Let's track that then. We maintain a set of blocked goroutines. If a goroutine goes from unblocked to blocked, it gets added to this set. If it goes from blocked to unblocked, it gets removed from the set.

*Note: according to the [Go ABI](https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md), a register is reserved to store the current goroutine. On my system (ARM64), it is the `R28` register, [accessible](/blog/an_optimization_and_debugging_story_go_dtrace.html#addendum-a-goroutine-aware-d-script) in DTrace with `uregs[R_X28]`. On x86_64, it is the `r14` register. This is handy when a Go runtime function does not take the goroutine to act on, as an argument.*

```dtrace
pid$target::runtime.gopark:entry 
// arg3 = traceBlockReason.
/t!=0 && goroutines[uregs[R_X28]] != 0/
{
  this->g = uregs[R_X28]; 
  this->waitreason = arg3;
  
  this->blocked = 
    this->waitreason == 1 || // traceBlockForever
    this->waitreason == 3 || // traceBlockSelect
    this->waitreason == 4 || // traceBlockCondWait
    this->waitreason == 5 || // traceBlockSync
    this->waitreason == 6 || // traceBlockChanSend
    this->waitreason == 7;   // traceBlockChanRecv
  if (goroutines_blocked[this->g] == 0 && this->blocked) {
    goroutines_blocked_count += 1;
  } else if (goroutines_blocked[this->g] == 1 && this->blocked == 0) {
    goroutines_blocked_count -= 1;
  }
  goroutines_blocked[this->g] = this->blocked;

  printf("gopark: goroutine=%p blocked=%d reason=%d blocked_count=%d\n", this->g, this->blocked, this->waitreason, goroutines_blocked_count);
}
```

The counterpart of `runtime.gopark` is `runtime.goready` (typically inlined and calls `runtime.ready` which we can watch), that marks a goroutine as runnable again (unblocked). So, we remove the goroutine from the 'blocked' set:

```dtrace
pid$target::runtime.ready:entry 
/goroutines[arg0] != 0/ 
{
    goroutines_blocked[this->g] = 0;
    goroutines_blocked_count -= 1;
}
```

And of course, if the goroutine gets destroyed, we also remove it from the 'blocked' set (if it was inside it):

```diff
pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/
{
  this->g = arg0; // goroutine id.

  goroutines[this->g] = 0; 
  goroutines_count -= 1;

+ if (goroutines_blocked[this->g] != 0) {
+   goroutines_blocked[this->g] = 0;
+   goroutines_blocked_count -= 1;
+ }

  printf("godestroy: goroutine=%p count=%d blocked_count=%d\n", this->g, goroutines_count, goroutines_blocked_count);
}
```


Here is the whole script (click to expand):

<details>
  <summary>The full script</summary>

```dtrace
int goroutines_count;
int goroutines_blocked_count;
int goroutines[int]; 
int goroutines_blocked[int]; 

pid$target::main.main:entry { 
  t=1;
}

pid$target::runtime.newproc1:entry {
  self->gparent = arg1;
} 

pid$target::runtime.newproc1:return 
/t!=0/
{
  this->g = arg1; // goroutine id.

  goroutines[this->g] = 1;
  goroutines_count += 1;

  printf("newproc1: goroutine=%p parent=%d count=%d blocked_count=%d\n", this->g, self->gparent, goroutines_count, goroutines_blocked_count);

  self->gparent = 0;
}

pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/
{
  this->g = arg0; // goroutine id.

  goroutines[this->g] = 0; 
  goroutines_count -= 1;

  if (goroutines_blocked[this->g] != 0) {
    goroutines_blocked[this->g] = 0;
    goroutines_blocked_count -= 1;
  }

  printf("godestroy: goroutine=%p count=%d blocked_count=%d\n", this->g, goroutines_count, goroutines_blocked_count);
}

pid$target::runtime.ready:entry 
/goroutines[arg0] != 0/ 
{
    goroutines_blocked[this->g] = 0;
    goroutines_blocked_count -= 1;
}

pid$target::runtime.gopark:entry 
// arg3 = traceBlockReason.
/t!=0 && goroutines[uregs[R_X28]] != 0/
{
  this->g = uregs[R_X28]; 
  this->waitreason = arg3;
  
  this->blocked = 
    this->waitreason == 1 || // traceBlockForever
    this->waitreason == 3 || // traceBlockSelect
    this->waitreason == 4 || // traceBlockCondWait
    this->waitreason == 5 || // traceBlockSync
    this->waitreason == 6 || // traceBlockChanSend
    this->waitreason == 7;   // traceBlockChanRecv
  if (goroutines_blocked[this->g] == 0 && this->blocked) {
    goroutines_blocked_count += 1;
  } else if (goroutines_blocked[this->g] == 1 && this->blocked == 0) {
    goroutines_blocked_count -= 1;
  }
  goroutines_blocked[this->g] = this->blocked;

  printf("gopark: goroutine=%p blocked=%d reason=%d blocked_count=%d\n", this->g, this->blocked, this->waitreason, goroutines_blocked_count);
}

profile-1s, END {
  printf("%s: count=%d blocked_count=%d\n", probename, goroutines_count, goroutines_blocked_count);
}
```

</details>


Let's try it on the leaky program:

```shell
$ sudo dtrace -s /Users/philippe.gaultier/my-code/dtrace-tools/goroutines.d -c ./leaky.exe -q
nGoro = 3
newproc1: goroutine=140000036c0 parent=1374389543360 count=1 blocked_count=0
newproc1: goroutine=14000003880 parent=1374389543360 count=2 blocked_count=0
newproc1: goroutine=14000003a40 parent=1374389543360 count=3 blocked_count=0
gopark: goroutine=140000036c0 blocked=1 reason=6 blocked_count=1
gopark: goroutine=14000003a40 blocked=1 reason=6 blocked_count=2
gopark: goroutine=14000003880 blocked=1 reason=6 blocked_count=3
END: count=3 blocked_count=3
```

And on the non-leaky program:

```shell
$ sudo dtrace -s /Users/philippe.gaultier/my-code/dtrace-tools/goroutines.d -c ./not_leaky.exe -q
nGoro = 0
newproc1: goroutine=140000036c0 parent=1374389543360 count=1 blocked_count=0
newproc1: goroutine=14000003880 parent=1374389543360 count=2 blocked_count=0
newproc1: goroutine=14000003a40 parent=1374389543360 count=3 blocked_count=0
godestroy: goroutine=140000036c0 count=2 blocked_count=1
gopark: goroutine=14000003a40 blocked=1 reason=6 blocked_count=1
godestroy: goroutine=14000003a40 count=1 blocked_count=0
godestroy: goroutine=14000003880 count=0 blocked_count=0
END: count=0 blocked_count=0
```


## Discussion

This D script is much better, however it does not implement the full algorithm from Go's new goroutine leak detector: We know what goroutines are blocked on an object, but in order to mark them officially as leaking, the object should be unreachable. 

Thus, we would need to also 1) track which object is being blocked on, and 2) track the Garbage Collector operations, to know when said object gets garbage collected, which means it becomes unreachable.

For 1), the goroutine structure in the Go runtime has the field `parkingOnChan` to know on which channel to goroutine is waiting on. That's a good start, but I do not know if there is an equivalent for mutexes and other synchronization objects. 

For 2), we have the DTrace probes `runtime.gc*:` at our disposal to watch the Garbage Collector. I believe this is possible, just some more work.

Finally, it's important to note that when a goroutine is destroyed, we need to remove it from the various maps we maintain. This is not only to reduce the DTrace memory usage, but also because the Go runtime puts the freshly destroyed goroutine on a free list to be possibly reused, 
so this could get confusing.


## Limitations

The `runtime/HACKING.md` document mentions: 

> `getg()` and `getg().m.curg`
> 
> To get the current user `g`, use `getg().m.curg`.
> 
> `getg()` alone returns the current `g`, but when executing on the
> system or signal stacks, this will return the current M's "g0" or
> "gsignal", respectively. This is usually not what you want.
> 
> To determine if you're running on the user stack or the system stack,
> use `getg() == getg().m.curg`.


`g` is the name of the goroutine struct in the Go runtime. `getg()` gets the current goroutine, which is a pointer to a `g` struct, and this call gets transformed by the compiler into a register lookup. In DTrace, we do it with `uregs[R_X28]`. So far so good. 

However, we do not do `getg().m.curg` currently. In my testing I have not seen a difference, it was always the case of `getg() == getg().m.curg`.

But we should be rigorous. 

This is easy to do in DTrace:

- We define minimally the struct for `g` (goroutine) and `m` (an OS thread) with the exact same layout as in the Go runtime
- We use `copyin()` to copy the data in these structs
- We use `*` and `->` to dereference and follow the pointers like `getg().m.curg`

```dtrace
struct g {
  uint8_t pad[48];
  struct m* m;
};

struct m {
  uint8_t pad[184];
  struct g* curg;
};


pid$target::runtime.gopark:entry 
// arg3 = traceBlockReason.
/goroutines[uregs[R_X28]] != 0/
{
  this->g_addr = uregs[R_X28]; 
  this->go = (struct g*)copyin(this->g_addr, sizeof(struct g));
  this->m = (struct m*)copyin((user_addr_t)this->go->m, sizeof(struct m));
  this->curg_addr = (uintptr_t)this->m->curg;
  this->curg = (struct g*)copyin((user_addr_t)this->curg_addr, sizeof(struct g));
  print(*this->curg);

  [..]
}
```

And voila!

Of course, if we want to see all the data about the goroutine, we can define the `g` struct faithfully and get even more insights:

<details>
  <summary>The full <code>g</code> struct</summary>

```dtrace
struct g {
  uintptr_t stack[2];
  uintptr_t stackguard0;
  uintptr_t stackguard1;
  uintptr_t _panic;
  uintptr_t _defer;
  struct m* m;
  uintptr_t sched[6];
  uintptr_t syscallsp;
  uintptr_t syscallpc;
  uintptr_t syscallbp;
  uintptr_t stktopsp;
  uintptr_t param;

  uint32_t status;
  uint32_t stackLock;
    
  uint64_t goid;
  uintptr_t schedlink;
  int64_t waitsince;

  uint8_t waitreason;
  uint8_t preempt;
  uint8_t preemptStop;
  uint8_t preemptShrink;
  uint8_t asyncSafePoint;
  uint8_t paniconfault;
  uint8_t gcscandone;
  uint8_t throwsplit;

  uint8_t activeStackChans;
  uint8_t pad1[3];
  uint32_t parkingOnChan;

  uint8_t inMarkAssist;
  uint8_t coroexit;
  int8_t raceignore;
  uint8_t nocgocallback;
  uint8_t tracking;
  uint8_t trackingSeq;
  uint8_t pad2[2];

  int64_t trackingStamp;
  int64_t runnableTime;
  uintptr_t lockedm;

  uint8_t fipsIndicator;
  uint8_t syncSafePoint;
  uint8_t pad3[2];
  uint32_t runningCleanups;

  uint32_t sig;
  uint8_t pad4[4];

  uintptr_t writebuf_ptr;
  uint64_t writebuf_len;
  uint64_t writebuf_cap;
  uintptr_t sigcode0;
  uintptr_t sigcode1;
  uintptr_t sigpc;
  uint64_t parentGoid;
  uintptr_t gopc;
  uintptr_t ancestors;
  uintptr_t startpc;
  uintptr_t racectx;
  uintptr_t waiting;
  uintptr_t cgoCtxt_ptr;
  uint64_t cgoCtxt_len;
  uint64_t cgoCtxt_cap;
  uintptr_t labels;
  uintptr_t timer;
  int64_t sleepWhen;

  uint32_t selectDone;
  uint32_t goroutineProfiled;
  
  uintptr_t coro;
  uintptr_t bubble;

  uint64_t trace[4];
  int64_t gcAssistBytes;

  uintptr_t valgrindStackID;
}; 
```

</details>

## Conclusion

With a few simple DTrace probes, we can observe the Go runtime creating, parking, unparking, and destroying goroutines, along with a way to uniquely identify the goroutine in question. From these two primitives, we can track goroutine leaks, but that's not all! We could do a lot more, all in DTrace (or possibly with a bit of post-processing):

- How long does a goroutine live, with a histogram?
- What is the peak (maximum) number of active goroutines?
- What places in the code spawn the most goroutines?
- How much time passes between asking the Go runtime to create the goroutine, and the code in the goroutine actually running?
- How long do goroutines sleep, with a histogram?
- Print a goroutine graph of what goroutine spawned which goroutine. `runtime.newproc1:entry` takes the parent goroutine as argument, so we know the parent-child relationship.
- How much do goroutines consume, with a histogram: the goroutine structure in the Go runtime has the `stack.lo` and `stack.hi` fields which describe the bounds of the goroutine memory.
- How long does a goroutine wait to run, when it is not waiting on any object, and it could run at any time? The goroutine struct in the Go runtime has this field: `runnableTime    int64 // the amount of time spent runnable, cleared when running, only used when tracking` but it only is updated in 'tracking' mode.
- How many goroutines are currently on the free list?
- Etc

Which is pretty cool if you ask me, given that:

- All of that works for all versions of Go, even the very first one, and no need to upgrade to a new version (although, the Go runtime was written in C up to version 1.4 so the probes stemming from the C functions might be named differently)
- Third-party tools to find goroutine leaks use `time.Sleep` extensively, which might change the behavior of our program, and take longer
- No need to ask the Go maintainers to add one more metric or profile we need
- No need to change and recompile the application
- No overhead when not running, and safe to use in production. 
- Same behavior when on or off. The Go runtime has a lot of different code paths depending if tracing is enabled, if the race detector is enabled, if Valgrind is enabled... that makes the code quite complex, and potentially behave differently depending on what is on/off. With DTrace, we know that peeking inside the inner workings of the Go runtime does not change its behavior.

Oh and by the way, try these probes:

- `runtime.*chan*`: see channel operations such as sends and receives
- `runtime.gc*`: see Garbage Collector operations such as mark, sweep, marking roots
- `runtime.*alloc*`: see memory allocator operations

When used in conjunction with tracking functions from our code, like we did in the first DTrace snippet in this article, Go feels a lot less magic! I wish I did that as a beginner.


## Addendum: The full script


<details>
  <summary>The full script</summary>

```dtrace
int goroutines_count;
int goroutines_blocked_count;
int goroutines[int]; 
int goroutines_blocked[int]; 

pid$target::main.main:entry { 
  t=1;
}

pid$target::runtime.newproc1:entry {
  self->gparent = arg1;
} 

pid$target::runtime.newproc1:return 
/t!=0/
{
  this->g = arg1; // goroutine id.

  goroutines[this->g] = 1;
  goroutines_count += 1;

  printf("newproc1: goroutine=%p parent=%d count=%d blocked_count=%d\n", this->g, self->gparent, goroutines_count, goroutines_blocked_count);

  self->gparent = 0;
}

pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/
{
  this->g = arg0; // goroutine id.

  goroutines[this->g] = 0; 
  goroutines_count -= 1;

  if (goroutines_blocked[this->g] != 0) {
    goroutines_blocked[this->g] = 0;
    goroutines_blocked_count -= 1;
  }

  printf("godestroy: goroutine=%p count=%d blocked_count=%d\n", this->g, goroutines_count, goroutines_blocked_count);
}

pid$target::runtime.ready:entry 
/goroutines[arg0] != 0/ 
{
    goroutines_blocked[this->g] = 0;
    goroutines_blocked_count -= 1;
}

pid$target::runtime.gopark:entry 
// arg3 = traceBlockReason.
/t!=0 && goroutines[uregs[R_X28]] != 0/
{
  this->g = uregs[R_X28]; 
  this->waitreason = arg3;
  
  this->blocked = 
    this->waitreason == 1 || // traceBlockForever
    this->waitreason == 3 || // traceBlockSelect
    this->waitreason == 4 || // traceBlockCondWait
    this->waitreason == 5 || // traceBlockSync
    this->waitreason == 6 || // traceBlockChanSend
    this->waitreason == 7;   // traceBlockChanRecv
  if (goroutines_blocked[this->g] == 0 && this->blocked) {
    goroutines_blocked_count += 1;
  } else if (goroutines_blocked[this->g] == 1 && this->blocked == 0) {
    goroutines_blocked_count -= 1;
  }
  goroutines_blocked[this->g] = this->blocked;

  printf("gopark: goroutine=%p blocked=%d reason=%d blocked_count=%d\n", this->g, this->blocked, this->waitreason, goroutines_blocked_count);
}

profile-1s, END {
  printf("%s: count=%d blocked_count=%d\n", probename, goroutines_count, goroutines_blocked_count);
}

```

</details>
