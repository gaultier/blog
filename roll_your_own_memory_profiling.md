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
heap profile:      7:  6815744 [     7:  6815744] @ heapprofile
     3:  3145728 [     3:  3145728] @ 0x55bdebf31165 0x55bdebf3118e 0x55bdebf311b0 0x7f0296b69a90 0x7f0296b69b49 0x55bdebf31085
     3:  3145728 [     3:  3145728] @ 0x55bdebf31184 0x55bdebf311b0 0x7f0296b69a90 0x7f0296b69b49 0x55bdebf31085
     1:   524288 [     1:   524288] @ 0x55bdebf31165 0x55bdebf311c4 0x7f0296b69a90 0x7f0296b69b49 0x55bdebf31085

MAPPED_LIBRARIES:
55bdebf30000-55bdebf31000 r--p 00000000 00:00 182326      /tmp/a.out
55bdebf31000-55bdebf32000 r-xp 00001000 00:00 182326      /tmp/a.out
55bdebf32000-55bdebf33000 r--p 00002000 00:00 182326      /tmp/a.out
55bdebf33000-55bdebf34000 r--p 00002000 00:00 182326      /tmp/a.out
55bdebf34000-55bdebf35000 rw-p 00003000 00:00 182326      /tmp/a.out
55bdeceb8000-55bdeddb8000 rw-p 00000000 00:00 0           [heap]
7f029644d000-7f02967a1000 rw-p 00000000 00:00 0           
7f02967a1000-7f02967a4000 r--p 00000000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f02967a4000-7f02967c5000 r-xp 00003000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f02967c5000-7f02967d1000 r--p 00024000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f02967d1000-7f02967d2000 r--p 00030000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f02967d2000-7f02967d3000 rw-p 00031000 00:00 678524      /usr/lib/x86_64-linux-gnu/liblzma.so.5.4.1
7f02967d3000-7f02967e1000 r--p 00000000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f02967e1000-7f029685f000 r-xp 0000e000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f029685f000-7f02968ba000 r--p 0008c000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f02968ba000-7f02968bb000 r--p 000e6000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7f02968bb000-7f02968bc000 rw-p 000e7000 00:00 668348      /usr/lib/x86_64-linux-gnu/libm.so.6
7ffc8f542000-7ffc8f563000 rw-p 00000000 00:00 0           [stack]
7ffc8f599000-7ffc8f59d000 r--p 00000000 00:00 0           [vvar]
7ffc8f59d000-7ffc8f59f000 r-xp 00000000 00:00 0           [vdso]
ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0           [vsyscall]
```

We have 3 unique call stacks that allocate, in the same order as they appear in the text file (although order does not matter for `pprof`):
- `b` <- `a` <- `main`
- `a` <- `main`
- `b` <- `main`
