Title: Tip of the day #6: Use bpftrace to estimate how much memory an in-memory cache will use
Tags: Go, Tip of the day, Bpftrace
---

I have a Go service has an in-memory LRU (Least Recently Used) cache to speed up some things. 
Here I am writing documentation for this service, and you can specify in the configuration of this service, a maximum number of cache entries.
That's useful to limit the memory usage of this service. Obviously this value is directly related to the Kubernetes memory limit for this service.

But then I am wondering: what should this configuration value be? A 1000 entries, 10 000? *How big is a cache entry*? An entry in the cache here is a slice of bytes (a blob) so it's not statically possible to determine, just looking at the code.

This is easy to estimate: all entries in the cache are inserted by one callback. It happens to be a Go function that is passed to C library (using CGO) but this trick works with any language. The second argument is a slice of bytes to be inserted in the cache. So, add a log in this callback, print the slice length, compute some statistics, and done?

Well it's even easier: let's use `bpftrace` to determine the average size of an entry *at runtime*! In the past I have used `dtrace` on macOS which is similar and the direct inspiration for `bpftrace`. I find `dtrace` more powerful in some regards - although `bpftrace` has loops whereas `dtrace` does not. Point being, the `bpftrace` incantation can be adapted for `dtrace` pretty easily.

So, I run the integration tests, collect a histogram of the slice of bytes to be inserted in the cache, and voila! 

We can also run the service with say a load test to generate traffic, or simply wait for real traffic to come - all of that works, and `dtrace`/`bpftrace` are designed to inspect production programs without the risk of crashing them, or adversely impacting the system.

Here, my function to insert a slice of bytes in the cache is called `cache_insert`, the executable is called `itest.test`, and the length of the slice of bytes happens to be passed as the third function argument. Arguments are zero-indexed so that means `arg2`. See the addendum at the end to understand why (try to guess before reading the explanation!):

```sh
$ sudo bpftrace -e 'uprobe:./itest.test:cache_insert {@bytes=lhist(arg2, 0 , 16384, 128)}' -c './itest.test -test.count=1'
```

`lhist` creates a linear histogram with the minimum value here being 0, the maximum value `16384` and the bucket size `128`. I used `hist` initially which uses a power-of-two bucket size but my values were all in one bucket so that was a bit imprecise. Still a good first approximation.

And we get:

```
@bytes: 
[512, 640)            96 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

So all slices of bytes have their length between `512` and `640` in this case, all in one bucket.

## Addendum: Function arguments in bpftrace

`bpftrace` does neither read debug information nor C headers by default so all function arguments are register sized, i.e. 64 bits on x86_64. `bpftrace` does not even know how many arguments does the function have!

My function signature is (simplified):

```c
struct ByteSliceView {
    uint8_t* data;
    size_t len;
}

void cache_insert(const uint8_t *hash, struct ByteSliceView value, [...]);
```

The value of interest is `value.len`. So initially I tried to access it in `bpftrace` using `arg1.len`, however it did not work. Here is an excerpt from the documentation:

```
Function arguments are available through the argN for register args. Arguments passed on stack are available using the stack pointer, e.g. $stack_arg0 = (int64)reg("sp") + 16. Whether arguments passed on stack or in a register depends on the architecture and the number or arguments used, e.g. on x86_64 the first 6 non-floating point arguments are passed in registers and all following arguments are passed on the stack. Note that floating point arguments are typically passed in special registers which don’t count as argN arguments which can cause confusion
```

So, it's a mess ...

I fired up `gdb` and printed registers directly when the `cache_insert` function is entered. I discovered by doing `print $rdx` that (on my machine, whith this compiler and buildflags, yadi yada), the `rdx` register contains `value.len`. I.e. the compiler unpacks `value` which is a struct of two fields, into `arg1` (i.e. the `rsi` register) and `arg2` (i.e. the `rdx` register).


