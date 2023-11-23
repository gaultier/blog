<link rel="stylesheet" type="text/css" href="main.css">
<link rel="stylesheet" href="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/styles/default.min.css">
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/highlight.min.js"></script>
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/languages/x86asm.min.js"></script>
<script>
window.addEventListener("load", (event) => {
  hljs.highlightAll();
});
</script>

<a href="/blog">All articles</a>

# Roll your own memory profiling

*Or: An exploration of the `pprof` format.*

Say that you are using a programming language where memory is manually managed, and you have decided to use a custom allocator for one reason or another, for example an arena allocator, and are wondering:

- How do I track every allocation, recording how many bytes were allocated and what was the call stack at that time
- How much memory is my program using, and what is the peak use?
- How much memory does my program free? Is it all of it (are there leaks)?
- Which line of code in my function is allocating, and how much?
- I want a flamegraph showing allocations by function

What to do?

Well, it turns out that this can all be achieved very simply without adding dependencies to your application, in ~100 lines of code (with lots of comments). I'll show one way and then explore other possibilities. And here are the results we are working towards:

![1](mem_prof1.png)
![2](mem_prof2.png)
![3](mem_prof3.png)

Another good reason to do this, is when the standard `malloc` comes with some form of memory profiling which is not suitable for your needs and you want something different/better/the same on every platform.

>  If you spot an error, please open a [Github issue](https://github.com/gaultier/blog)!


## Pprof

Here is the plan: 

1. Each time there is an allocation in our program, we record information about it in an array
2. At the end of the program (or upon receiving a signal, a special tcp packet, whatever), we dump the information in the (original) [pprof](https://github.com/gperftools/gperftools) format, which is basically just a text file with one line per allocation (more details on that in a bit)
3. We can then use the (original) pprof which is just a [giant Perl script](https://github.com/gperftools/gperftools/blob/master/src/pprof) which will extract interesting information and most importantly symbolize (meaning: transform memory addresses into line/column/function/file information)

I will showcase this approach with C code using an arena allocator. The full code can be found in my project [micro-kotlin](https://github.com/gaultier/micro-kotlin/blob/pprof-original/str.h#L320). But this can be done in any language since the pprof text format is so simple!

> The original pprof written in Perl is not to be confused with the rewritten [pprof](https://github.com/google/pprof) in Go which offers a superset of the features of the original but based on a completely different and incompatible file format (protobuf)!

## The text format

Here is the text format we want to generate:

```
heap profile:    <in use objects sum>:  <in use bytes sum> [   <space objects sum>:  <space bytes sum>] @ heapprofile
<in use objects>: <in use bytes> [<space objects>: <space bytes>] @ <rip1> <rip2> <rip3> [...]
<in use objects>: <in use bytes> [<space objects>: <space bytes>] @ <rip1> <rip2> <rip3> [...]
<in use objects>: <in use bytes> [<space objects>: <space bytes>] @ <rip1> <rip2> <rip3> [...]
                                                                             
MAPPED_LIBRARIES:
[...]

```

The first line is a header identifying that this is a heap profile (in opposition to a CPU profile which pprof can also analyze) and gives for each of the four fields we will record, their sum. 
Then comes one line per entry. Each entry has these four fields that the header gave a sum of:
- `in use objects`: How many objects are 'live' i.e. in use on the heap at the time of exporting this heap profile. Allocating increases its value, freeing decreases it.
- `in use bytes`: How many bytes are 'live' i.e. in use on the heap at the time of exporting this heap profile. Allocating increases its value, freeing decreases it.
- `space objects`: How many objects have been allocated since the start of the program. It is not affected by freeing memory, it only increases.
- `space bytes`: How many bytes have been allocated since the start of the program. It is not affected by freeing memory, it only increases.

So when we allocate an object e.g. `new(Foo)` in C++:
- `in use objects` and `space objects` increment by 1 
- `in use bytes` and `space bytes` increment by `sizeof(Foo)`

When we allocate an array of N elements of type `Foo`:
- `in use objects` and `space objects` increment by N
- `in use bytes` and `space bytes` increment by `N * sizeof(Foo)`

When we free an object:
- `in use objects` decrements by 1 
- `in use bytes` decrements by `sizeof(Foo)`

When we free an array of N elements of type `Foo`:
- `in use objects` decrements by N 
- `in use bytes` decrements by `N * sizeof(Foo)`

These 4 dimensions are really useful to spot memory leaks (`in use objects` and `in use bytes` increase over time), peak memory usage (`space bytes`), whether we are doing many small allocations versus a few big allocations, etc.

Each entry (i.e. line) ends with the call stack which is a space-separated list of addresses. We'll see that it is easy to get that information without resorting to external libraries such as `libunwind` by simply walking the stack, a topic I touched on in a previous [article](/blog/x11_x64.html#a-stack-primer).

Very importantly, multiple allocation records with the same stack must be merged together into one, summing their values. In that sense, each line conceptually an entry in a hashmap where the key is the call stack (the part of the right of the `@` character) and the value is a 4-tuple: `(u64, u64, u64, u64)` (the part on the left of the `@` character).

The text file ends with a trailer which is crucial for symbolication (to transform memory addresses into source code locations), which (at least on Linux, other systems must be able to do the same but I have not investigated them) is trivial to get: This is just a copy of the file `/proc/self/maps`. It lists of the loaded libraries and at which address they are.


Here is a small example:

```c
#include <stdlib.h>

void b(int n) { malloc(n); }

void a(int n) {
  malloc(n);
  b(n);
}

int main() {
  for (int i = 0; i < 2; i++)
    a(2);

  b(3);
}
```

Leveraging `tcmalloc`, this program will generate a heap profile:

```sh
$ cc /tmp/test_alloc.c -ltcmalloc  -g3
$ HEAPPROFILE=/tmp/heapprof ./a.out
Starting tracking the heap
Dumping heap profile to /tmp/heapprof.0001.heap (Exiting, 11 bytes in use)
```

*This is just an example to showcase the format, we will from this point on use our own code to generate this text format.*

```
heap profile:      5:       11 [     5:       11] @ heapprofile
     2:        4 [     2:        4] @ 0x558e804cc165 0x558e804cc18e 0x558e804cc1b0 0x7f452a4daa90 0x7f452a4dab49 0x558e804cc085
     2:        4 [     2:        4] @ 0x558e804cc184 0x558e804cc1b0 0x7f452a4daa90 0x7f452a4dab49 0x558e804cc085
     1:        3 [     1:        3] @ 0x558e804cc165 0x558e804cc1c4 0x7f452a4daa90 0x7f452a4dab49 0x558e804cc085

MAPPED_LIBRARIES:
558e804cb000-558e804cc000 r--p 00000000 00:00 183128      /tmp/a.out
558e804cc000-558e804cd000 r-xp 00001000 00:00 183128      /tmp/a.out
558e804cd000-558e804ce000 r--p 00002000 00:00 183128      /tmp/a.out
558e804ce000-558e804cf000 r--p 00002000 00:00 183128      /tmp/a.out
558e804cf000-558e804d0000 rw-p 00003000 00:00 183128      /tmp/a.out
558e814b7000-558e81db8000 rw-p 00000000 00:00 0           [heap]
7f4529e7e000-7f452a112000 rw-p 00000000 00:00 0           
7f452a112000-7f452a115000 r--p 00000000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f452a115000-7f452a136000 r-xp 00003000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f452a136000-7f452a142000 r--p 00024000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f452a142000-7f452a143000 r--p 00030000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f452a143000-7f452a144000 rw-p 00031000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f452a144000-7f452a152000 r--p 00000000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f452a152000-7f452a1d0000 r-xp 0000e000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f452a1d0000-7f452a22b000 r--p 0008c000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f452a22b000-7f452a22c000 r--p 000e6000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f452a22c000-7f452a22d000 rw-p 000e7000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f452a22d000-7f452a22f000 rw-p 00000000 00:00 0           
7f452a22f000-7f452a2cb000 r--p 00000000 00:00 678806      /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32
7f452a2cb000-7f452a3fc000 r-xp 0009c000 00:00 678806      /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32
7f452a3fc000-7f452a489000 r--p 001cd000 00:00 678806      /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32
7f452a489000-7f452a494000 r--p 0025a000 00:00 678806      /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32
7f452a494000-7f452a497000 rw-p 00265000 00:00 678806      /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32
7f452a497000-7f452a49b000 rw-p 00000000 00:00 0           
7f452a49b000-7f452a49e000 r--p 00000000 00:00 702044      /usr/lib/x86_64-linux-gnu/libunwind.so.8.0.1
7f452a49e000-7f452a4a8000 r-xp 00003000 00:00 702044      /usr/lib/x86_64-linux-gnu/libunwind.so.8.0.1
7f452a4a8000-7f452a4ab000 r--p 0000d000 00:00 702044      /usr/lib/x86_64-linux-gnu/libunwind.so.8.0.1
7f452a4ab000-7f452a4ac000 r--p 0000f000 00:00 702044      /usr/lib/x86_64-linux-gnu/libunwind.so.8.0.1
7f452a4ac000-7f452a4ad000 rw-p 00010000 00:00 702044      /usr/lib/x86_64-linux-gnu/libunwind.so.8.0.1
7f452a4ad000-7f452a4b7000 rw-p 00000000 00:00 0           
7f452a4b7000-7f452a4d9000 r--p 00000000 00:00 668342      /usr/lib/x86_64-linux-gnu/libc.so.6
7f452a4d9000-7f452a651000 r-xp 00022000 00:00 668342      /usr/lib/x86_64-linux-gnu/libc.so.6
7f452a651000-7f452a6a9000 r--p 0019a000 00:00 668342      /usr/lib/x86_64-linux-gnu/libc.so.6
7f452a6a9000-7f452a6ad000 r--p 001f1000 00:00 668342      /usr/lib/x86_64-linux-gnu/libc.so.6
7f452a6ad000-7f452a6af000 rw-p 001f5000 00:00 668342      /usr/lib/x86_64-linux-gnu/libc.so.6
7f452a6af000-7f452a6bc000 rw-p 00000000 00:00 0           
7f452a6bc000-7f452a6bf000 r--p 00000000 00:00 677590      /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
7f452a6bf000-7f452a6da000 r-xp 00003000 00:00 677590      /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
7f452a6da000-7f452a6de000 r--p 0001e000 00:00 677590      /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
7f452a6de000-7f452a6df000 r--p 00021000 00:00 677590      /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
7f452a6df000-7f452a6e0000 rw-p 00022000 00:00 677590      /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
7f452a6e0000-7f452a6f3000 r--p 00000000 00:00 182678      /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4.5.9
7f452a6f3000-7f452a719000 r-xp 00013000 00:00 182678      /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4.5.9
7f452a719000-7f452a729000 r--p 00039000 00:00 182678      /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4.5.9
7f452a729000-7f452a72a000 r--p 00048000 00:00 182678      /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4.5.9
7f452a72a000-7f452a72b000 rw-p 00049000 00:00 182678      /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4.5.9
7f452a72b000-7f452a8e1000 rw-p 00000000 00:00 0           
7f452a8e4000-7f452a8f8000 rw-p 00000000 00:00 0           
7f452a8f8000-7f452a8f9000 r--p 00000000 00:00 668336      /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7f452a8f9000-7f452a921000 r-xp 00001000 00:00 668336      /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7f452a921000-7f452a92b000 r--p 00029000 00:00 668336      /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7f452a92b000-7f452a92d000 r--p 00033000 00:00 668336      /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7f452a92d000-7f452a92f000 rw-p 00035000 00:00 668336      /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
7fff91a4d000-7fff91a6e000 rw-p 00000000 00:00 0           [stack]
7fff91b3f000-7fff91b43000 r--p 00000000 00:00 0           [vvar]
7fff91b43000-7fff91b45000 r-xp 00000000 00:00 0           [vdso]
ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0           [vsyscall]
```

We see that at the end of the program, we have (looking at the first line):
- 5 objects in use
- 11 bytes in use
- 5 objects allocated in total
- 11 bytes allocated in total

Since we never freed any memory, the `in use` counters are the same as the `space` counters.

We have 3 unique call stacks that allocate, in the same order as they appear in the text file (although order does not matter for `pprof`):
- `b` <- `a` <- `main`
- `a` <- `main`
- `b` <- `main`

Since our program is a Position Independant Executable (PIE), the loader picks a random address for where to load our program in virtual memory. Consequently, addresses collected from within our program have this offset added to them. Thankfully, the `MAPPED_LIBRARIES` section lists address ranges (the first column of each line in that section) for each library that gets loaded. 

As such, `pprof` only needs to find for each address the relevant range, subtract the start of the range from this address, and it has the real address in our executable. It then runs `addr2line` or similar to get the code location. 

