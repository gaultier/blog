Title: Tip of the day #6: Use Bpftrace to estimate how much memory an in-memory cache will use
Tags: Go, Tip of the day, Bpftrace
---

## Context

I have a Go service that has an in-memory LRU (Least Recently Used) cache to speed up some things. 
Here I am, writing documentation for this service, and it happens that you can specify in its configuration the maximum number of cache entries.
That's useful to limit the overall memory usage. Obviously this value is directly related to the Kubernetes memory limit for this deployment.

But then I am wondering: what value should the docs recommend for this configuration field? A 1000 entries, 10 000? One factor is how many distinct entries do we expect, but another is: *How big is a cache entry*? 

An entry in the cache in this case is a slice of bytes (a blob) so it's not statically possible to determine, just looking at the code, how much memory it will consume.

This distribution of entry sizes is however easy to uncover: all entries in the cache are inserted by one callback. It happens to be a Go function that is passed to a C library (via CGO) but this trick works with any language. This function takes as argument a slice of bytes to be inserted in the cache. So, add a log in this callback, print the slice length, process all the relevant logs, compute some statistics, and done? Or, add a custom Prometheus metric, deploy, done?

Well... why modify the source code when we don't have too? Let's use [bpftrace](https://github.com/bpftrace/bpftrace) to determine the distribution of entry sizes *at runtime* on the unmodified program! In the past I have used `dtrace` on macOS which is similar and the direct inspiration for `bpftrace`. I find `dtrace` more powerful in some regards - although `bpftrace` has support for loops whereas `dtrace` does not. Point being, the `bpftrace` incantation can be adapted for `dtrace` pretty easily. Both of these tools are essential workhorses of exploratory programming and troubleshooting.

## Bpftrace

So, the plan is: I run the tests under `bpftrace`, collect a histogram of the slice of bytes to be inserted in the cache, and voila! 

We can also run the real service with a load test to generate traffic, or simply wait for real traffic to come - all of that works, and `dtrace`/`bpftrace` are designed to inspect production programs without the risk of crashing them, or adversely impacting the system. The `bpftrace` incantation will be the same in all of these cases, only the binary (or process id) will change.

Here, my function to insert a slice of bytes in the cache is called `cache_insert`, the executable is called `itest.test`, and the length of the slice of bytes happens to be passed as the third function argument. Arguments are zero-indexed so that means `arg2`:

```sh
$ sudo bpftrace -e 'uprobe:./itest.test:cache_insert {@bytes=lhist(arg2, 0 , 16384, 128)}' -c './itest.test -test.count=1'
```

`lhist` creates a linear histogram with the minimum value here being `0`, the maximum value `16384` and the bucket size `128`. I used the `hist` function initially which uses a power-of-two bucket size but my values were all in one big bucket so that was a bit imprecise. Still a good first approximation. But we can get a better estimate by using a small bucket size with `lhist`.

`bpftrace` prints the histogram by default at the end:

```
@bytes: 
[512, 640)            96 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

So all slices of bytes have their length between `512` and `640` in this case, all in one bucket.

---

Alternatively, we can point `bpftrace` at the Go function instead of the C function:

```
func (c Cache) Insert(ctx context.Context, key [32]byte, value []byte, expiryDate time.Time) error { [...] }
```

We are interested in `len(value)` which happens to be accessible in `arg5`:

```sh
$ sudo bpftrace -e 'uprobe:./itest.test:/path/to/my/pkg/Cache.Insert {@bytes=lhist(arg5, 0 , 16384, 128)}' -c './itest.test -test.count=1'
```

and we get the same output.

## Addendum: Function arguments in bpftrace

`bpftrace` reads neither debug information nor C headers by default so all function arguments are register sized, i.e. 64 bits on x86_64. `bpftrace` does not even know how many arguments the function accepts!

My function signature is (simplified):

```c
struct ByteSliceView {
    uint8_t* data;
    size_t len;
}

void cache_insert(const uint8_t *key, struct ByteSliceView value, [...]);
```

The value of interest is `value.len`. So initially I tried to access it in `bpftrace` using `arg1.len`, however it did not work. Here is an excerpt from the documentation:

> Function arguments are available through the argN for register args. Arguments passed on stack are available using the stack pointer, e.g. $stack_arg0 = (int64)reg("sp") + 16. Whether arguments passed on stack or in a register depends on the architecture and the number or arguments used, e.g. on x86_64 the first 6 non-floating point arguments are passed in registers and all following arguments are passed on the stack. Note that floating point arguments are typically passed in special registers which don’t count as argN arguments which can cause confusion

So, it's a mess ...

I fired up `gdb` and printed registers directly when the `cache_insert` function is entered. I discovered by doing `info registers` that (on my machine, with this compiler and build flags, yada yada yada), the `rdx` register contains `value.len`. I.e. the compiler unpacks `value` which is a struct of two fields, into `arg1` (i.e. the `rsi` register) and `arg2` (i.e. the `rdx` register). 

Thus, this call: `cache_insert(foo, bar)` gets transformed by the compiler into `cache_insert(foo, bar.data, bar.len)`, and the third function argument (aka `arg2`) is our length.


