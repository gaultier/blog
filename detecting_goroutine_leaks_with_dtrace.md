Title: Detecting goroutine leaks with DTrace
Tags: Go, DTrace
---

Recently I read a cool blog [article](https://antonz.org/detecting-goroutine-leaks/) about new changes in Go 1.25 and (as the time of writing, upcoming) 1.26 to more easily track goroutine leaks.

I thought: ok, that's nice, that's a real problem. But what if you cannot use this `goroutineleak` profile? Perhaps you are stuck on an old Go version, perhaps you cannot change the code easily. 

What to do then? Well, as always, DTrace comes to the rescue. Let's track the creation and deletion of goroutines in our function. If there are more created gourtines than deleted, we have a leak.

We can take the exact same leaky Go program as in the original article:

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

This prints `3` since our code spawns a goroutine for each argument, and there are 3 arguments. Since the channel is unbuffered and never read from, the goroutines run forever and leak.

Let's track the creation and deletion of goroutines with DTrace. We only track goroutines created inside our function, to avoid observing goroutines from other parts of our code, or from the Go runtime:

```dtrace
pid$target::main.main:entry 
{
    t=1;
}

pid$target::runtime.newproc1:return 
/t!=0/ 
{
    ustack(); 
} 

pid$target::runtime.gdestroy:entry 
/t!=0/ 
{}
```

And we see as expected, 3 goroutines created and none deleted:

```text
CPU     ID                    FUNCTION:NAME
 12   8323          runtime.newproc1:return 
              leaky.exe`runtime.newproc.func1+0x2c
              leaky.exe`runtime.systemstack.abi0+0x68
              leaky.exe`runtime.newproc+0x38
              leaky.exe`main.Gather+0x44
              leaky.exe`main.main+0x48
              leaky.exe`runtime.main+0x278
              leaky.exe`runtime.goexit.abi0+0x4

 12   8323          runtime.newproc1:return 
              leaky.exe`runtime.newproc.func1+0x2c
              leaky.exe`runtime.systemstack.abi0+0x68
              leaky.exe`runtime.newproc+0x38
              leaky.exe`main.Gather+0x44
              leaky.exe`main.main+0x48
              leaky.exe`runtime.main+0x278
              leaky.exe`runtime.goexit.abi0+0x4

 12   8323          runtime.newproc1:return 
              leaky.exe`runtime.newproc.func1+0x2c
              leaky.exe`runtime.systemstack.abi0+0x68
              leaky.exe`runtime.newproc+0x38
              leaky.exe`main.Gather+0x44
              leaky.exe`main.main+0x48
              leaky.exe`runtime.main+0x278
              leaky.exe`runtime.goexit.abi0+0x4
```

Let's compare this with the fixed version of the program:

```text
CPU     ID                    FUNCTION:NAME
  9  11006          runtime.newproc1:return 
              not_leaky.exe`runtime.newproc.func1+0x2c
              not_leaky.exe`runtime.systemstack.abi0+0x68
              not_leaky.exe`runtime.newproc+0x38
              not_leaky.exe`main.Gather+0x44
              not_leaky.exe`main.main+0x4c
              not_leaky.exe`runtime.main+0x278
              not_leaky.exe`runtime.goexit.abi0+0x4

  9  11006          runtime.newproc1:return 
              not_leaky.exe`runtime.newproc.func1+0x2c
              not_leaky.exe`runtime.systemstack.abi0+0x68
              not_leaky.exe`runtime.newproc+0x38
              not_leaky.exe`main.Gather+0x44
              not_leaky.exe`main.main+0x4c
              not_leaky.exe`runtime.main+0x278
              not_leaky.exe`runtime.goexit.abi0+0x4

  9  11006          runtime.newproc1:return 
              not_leaky.exe`runtime.newproc.func1+0x2c
              not_leaky.exe`runtime.systemstack.abi0+0x68
              not_leaky.exe`runtime.newproc+0x38
              not_leaky.exe`main.Gather+0x44
              not_leaky.exe`main.main+0x4c
              not_leaky.exe`runtime.main+0x278
              not_leaky.exe`runtime.goexit.abi0+0x4

  9  11007           runtime.gdestroy:entry 
  9  11007           runtime.gdestroy:entry 
  9  11007           runtime.gdestroy:entry 
```

There, we see the 3 `runtime.gdestroy` calls as expected.


The reason why we track `runtime.newproc1:return` and `runtime.gdestroy:entry` in particular, will become clear in a second.

In a big, real program, goroutines are being destroyed left and right, even while our function is executing, and even goroutines not spawned by it. This is noisy and might hide leaks.

So do it correctly, we need to maintain a set of our own goroutines. Since `runtime.newproc1` returns a pointer to a newly allocated goroutine object, we can use this as a 'goroutine id'. This is accessible in `arg1` in DTrace (this depends on the system we are running our D script on. The Go ABI is documented but differs between systems and so it requires a bit of trial and error in DTrace to know which `argN` contains the information we need).

Then, in `runtime.gdestroy`, we only react to our own goroutines being destroyed. There, `arg0` contains the goroutine id/pointer being destroyed: 

```dtrace
pid$target::main.main:entry 
{
    t=1;
}

pid$target::runtime.newproc1:return 
/t!=0/ 
{
    goroutines[arg1] = 1;
    printf("goroutine %p created\n", arg1);
} 

pid$target::runtime.gdestroy:entry 
/goroutines[arg0] != 0/ 
{
    
    printf("goroutine %p destroyed\n", arg0);
    goroutines[arg0] = 0;

}
```


Our leaky program  shows again no goroutine deletion:

```text
goroutine 140000aa1c0 created
goroutine 140000aa380 created
goroutine 140000aa540 created
```

And the fixed program shows all of our goroutines being deleted:

```
goroutine 140000036c0 created
goroutine 14000003880 created
goroutine 14000003a40 created
goroutine 14000003a40 destroyed
goroutine 14000003880 destroyed
goroutine 140000036c0 destroyed
```



We can even use an aggregation to print which goroutines are still alive (aggregations are printed by default at the end of the DTrace program):

<!-- ```dtrace -->
<!-- pid$target::main.main:entry  -->
<!-- { -->
<!--     t=1; -->
<!-- } -->
<!---->
<!-- pid$target::runtime.newproc1:return  -->
<!-- /t!=0/  -->
<!-- { -->
<!--     @goroutines[arg1] = min(1); -->
<!--     goroutines[arg1] = 1; -->
<!-- }  -->
<!---->
<!-- pid$target::runtime.gdestroy:entry  -->
<!-- /goroutines[arg0] != 0/  -->
<!-- { -->
<!---->
<!--     @goroutines[arg0] = min(0); -->
<!--     goroutines[arg0] = 0; -->
<!-- } -->
<!-- ``` -->
<!---->
<!-- And we see: -->
