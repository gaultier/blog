Title: How to reproduce and fix an I/O data race with Go and DTrace
Tags: DTrace, Go, Bug
---

Today I was confronted at work with a bizarre test failure happening only in CI, in a project I do not know. This would only happen in CI, sometimes, probably due to slow I/O and a low number of threads in this environment.

 An esteemed [colleague](https://github.com/zepatrik) of mine hypothesized this was a data race on a file. A component is writing to the file, another component is concurrently reading from this file, and due to improper synchronization, the latter sometimes reads empty or partial data. 

My job was to prove it, without knowing anything about the codebase, armed only with the knowledge of the line and test file that fails.


For the impatient, here is the [fix](https://github.com/ory/keto/commit/bfa248ed009c17ceb2b2b534e321e01a64d58e73#diff-a0a8ff9cd805ffadfa2802d755c475cc08e2b9ee3825c23323858cd8df48727f).

## A minimal reproducer

The real codebase is big but here is a minimal reproducer for this article. It's conceptually quite simple: a goroutine writes to the file, and another goroutine reads from this file as soon as it is present, and parses the data (which is simply a host and port e.g. `localhost:4567`). In the real code, the two goroutines are in different components (they might even run in different OS processes now that I think of it) and thus an in-memory synchronization mechanism (mutex, etc) is not feasible:

```go
package main

import (
	"fmt"
	"math/rand/v2"
	"net"
	"os"
	"time"
)

const fileName = "test.addr"

func retryForever(fn func() error) {
	for {
		if err := fn(); err != nil {
			time.Sleep(10 * time.Millisecond)
		} else {
			return
		}
	}
}

func writeHostAndPortToFile() {
	addr := fmt.Sprintf("localhost:%d", rand.Uint32N(1<<16))
	_ = os.WriteFile(fileName, []byte(addr), 0600)
}

func readHostAndPortFromFile() (string, string, error) {
	retryForever(func() error {
		_, err := os.Stat(fileName)
		return err
	})

	content, err := os.ReadFile(fileName)
	if err != nil {
		return "", "", err
	}

	host, addr, err := net.SplitHostPort(string(content))
	return host, addr, err
}

func main() {
	os.Remove(fileName)

	go writeHostAndPortToFile()

	host, addr, err := readHostAndPortFromFile()
	if err != nil {
		fmt.Fprintf(os.Stderr, "error reading: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("host=%s addr=%s\n", host, addr)
}
```

The code tries to (spoiler alert: not quite correctly) spin in a loop to wait for the file to appear, and then reads from it.

When we run this, sometimes it works fine, and sometimes it fails. A clear data race on a shared resource!

```shell
$ go run io_race.go
host=localhost addr=28773

$ go run io_race.go
error reading: missing port in address
exit status 1
```

The astute  reader may have already detected a classic [TOCTOU](https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use) issue. 

## Observe reads and writes

We have two main possible approaches: observe system calls (with the `syscall::read:entry` syntax), or observe Go function calls (with the `pid$target::os.ReadFile:entry` syntax). Let's do both because it will turn out useful in a moment:


```dtrace
pid$target::os.ReadFile:entry  {
  this->name = stringof(copyin(arg0, arg1)); 
  ustack();
  printf("name=%s\n", this->name); 
}

pid$target::os.ReadFile:return  {}

pid$target::os.WriteFile:entry  {
  this->name = stringof(copyin(arg0, arg1)); 
  ustack();
  printf("name=%s\n", this->name); 
}

pid$target::os.WriteFile:return  {}

pid$target::os.Stat:entry  {}

pid$target::os.Stat:return  {
  printf("err=%d\n", arg2); 
}

syscall::write:entry 
/pid==$target && arg0 > 2/
{
  self->trace = 1;
}

syscall::write:return 
/pid==$target && self->trace != 0/
{
  printf("write return: res=%d\n", arg0);
  self->trace = 0;
}

syscall::read:entry 
/pid==$target && arg0 > 2/
{
  printf("fd=%d\n", arg0);
  self->trace = 1;
}


syscall::read:return 
/pid==$target && self->trace != 0/
{
  printf("read return: res=%d\n", arg0);
  self->trace = 0;
}
```

Let's observe the happy case:


```text
dtrace: script '/Users/philippe.gaultier/scratch/data_race_rw.d' matched 10 probes
host=localhost addr=45564
CPU     ID                    FUNCTION:NAME
  5  65087                    os.Stat:entry 
  5  65162                   os.Stat:return err=0

  7  65081               os.WriteFile:entry 
              io_race`os.WriteFile
              io_race`main.writeHostAndPortToFile+0x88
              io_race`runtime.goexit.abi0+0x4
name=test.addr

  7    177                     write:return write return: res=15

  7  65130              os.WriteFile:return 
  7  65087                    os.Stat:entry 
  7  65162                   os.Stat:return err=0

  7  62760                os.ReadFile:entry 
              io_race`os.ReadFile
              io_race`main.readHostAndPortFromFile+0x34
              io_race`main.main+0x3c
              io_race`runtime.main+0x278
              io_race`runtime.goexit.abi0+0x4
name=test.addr

  7    174                       read:entry fd=4

  7    175                      read:return read return: res=15

  7    174                       read:entry fd=4

  7    175                      read:return read return: res=0

  7  65044               os.ReadFile:return 
dtrace: pid 99037 has exited
```

*The last read returning 0 is normal: this is how `os.ReadFile` detects the end of the file.*

We see that this case worked out well because the write fully finished before the read started.


And now the error case (note how the syscalls are interleaved differently from the previous case):

```shell
dtrace: script '/Users/philippe.gaultier/scratch/data_race_rw.d' matched 10 probes
error reading: missing port in address
CPU     ID                    FUNCTION:NAME
  7 532133                    os.Stat:entry 
  7 532134                   os.Stat:return err=0

  6 532131               os.WriteFile:entry 
              io_race`os.WriteFile
              io_race`main.writeHostAndPortToFile+0x88
              io_race`runtime.goexit.abi0+0x4
name=test.addr

  8 532133                    os.Stat:entry 
  8 532134                   os.Stat:return err=0

  8 532129                os.ReadFile:entry 
              io_race`os.ReadFile
              io_race`main.readHostAndPortFromFile+0x34
              io_race`main.main+0x3c
              io_race`runtime.main+0x278
              io_race`runtime.goexit.abi0+0x4
name=test.addr

  8    174                       read:entry fd=5

  8    175                      read:return read return: res=0

 10 532130               os.ReadFile:return 
  6    177                     write:return write return: res=15

  6 532132              os.WriteFile:return 
dtrace: pid 432 has exited
```

Now, the bug triggered because the read operation finished before the write operation could. Thus, the read sees no data (`res=0`).


Note the order of operations for this bug to occur:

1. Goroutine #1 creates the file as part of `os.WriteFile`. At this point this file is empty.
2. Goroutine #2 is watching for this file to appear with `os.Stat`, and notices the file has been created.
3. Goroutine #2 reads from the file and sees empty data. Parsing fails.
4. Goroutine #1 finishes writing the data to the file.

## Add disk latency

Alright, so how do we systematically reproduce the issue, to convince ourselves that this is indeed the root cause? We would like to ideally add write latency, to simulate a slow disk. This is ultimately the main factor: if the write is too slow to complete, the read finishes too early before data (fully) appears.

My first attempt was to use in DTrace the `system` action: 

> void system(string program, ...) 
>
> The system action causes the program specified by program to be executed as if it were given to the shell as input. 

For example: `system("sleep 10")`.

However this did nothing, because this action happens asynchronously, as noted by the [docs](https://illumos.org/books/dtrace/chp-actsub.html#chp-actsub-4):

> The execution of the specified command does not occur in the context of the firing probe – it occurs when the buffer containing the details of the system action are processed at user-level. How and when this processing occurs depends on the buffering policy, described in Buffers and Buffering. 

Alright, let's try something else, maybe `chill`?

> void chill(int nanoseconds)
>
> The chill action causes DTrace to spin for the specified number of nanoseconds. chill is primarily useful for exploring problems that might be timing related. For example, you can use this action to open race condition windows, or to bring periodic events into or out of phase with one another. Because interrupts are disabled while in DTrace probe context, any use of chill will induce interrupt latency, scheduling latency, and dispatch latency.

Perfect! Let's try it (and remember to use the `-w` option on the DTrace command line to allow destructive actions):

```diff
syscall::write:entry 
/pid==$target/
{
+ chill(500000000);
  self->trace = 1;
}
```

To limit adverse effects on the system, DTrace limits the `chill` value to 500 ms:

> DTrace will refuse to execute the chill action for more than 500 milliseconds out of each one-second interval on any given CPU.

This means that we increased significantly our odds of this bug occurring, but not to 100%. Still, this is enough.

Note that since goroutines are involved, the number of threads on the system and the behavior of the Go scheduler are also factors.

## The fix

My biggest, niche pet peeve is seeing the use of `stat(2)` before doing an operation on the file. Not only is this unnecessary in nearly every case (the I/O operation such as `read(2)` or `write(2)` will report `EEXIST` if the file does not exist), not only is this a waste of time and battery, not only will many standard libraries do a `stat(2)` call anyway as part of reading/writing a file, but it also opens the door to various, hard-to-diagnose, TOCTOU issues, such as here. 

I guess a fine use of `stat(2)` before reading a file is to discover the file size, in order to reserve capacity in a growable array before reading the file data into it. It is just a hint anyway to avoid lots of reallocations. This is in fact exactly what `os.ReadFile` does under the covers. Which makes our use of `os.Stat` even more redundant.

Now, let's fix the bug by doing less (as it is often the case, I have found). We can simply try to read the file and parse its content. If we fail, we retry.

```diff
--- io_race.go	2025-10-01 18:01:47
+++ io_race_fixed.go	2025-10-01 18:14:51
@@ -25,18 +25,18 @@
 	_ = os.WriteFile(fileName, []byte(addr), 0600)
 }
 
-func readHostAndPortFromFile() (string, string, error) {
+func readHostAndPortFromFile() (host string, addr string, err error) {
 	retryForever(func() error {
-		_, err := os.Stat(fileName)
+		var content []byte
+		content, err = os.ReadFile(fileName)
+		if err != nil {
+			return err
+		}
+
+		host, addr, err = net.SplitHostPort(string(content))
 		return err
 	})
 
-	content, err := os.ReadFile(fileName)
-	if err != nil {
-		return "", "", err
-	}
-
-	host, addr, err := net.SplitHostPort(string(content))
 	return host, addr, err
 }
```

With the patch applied, we see in the DTrace output, even with the write delay, that the bug is fixed. We sometimes do multiple reads until one succeeds with the right data.

There are other possible ways to fix this problem such as using a lockfile, etc, but this is the simplest.

## Conclusion

Every time you use a `stat(2)` syscall, ask yourself if this is necessary and if a TOCTOU bug is possible. 

Also, I'm happy to have discovered the `chill` DTrace action to simulate disk (or network, or anything really) latency. I can see myself running the test suite with this on, to detect other cases of TOCTOU. Another tool in the toolbox!

