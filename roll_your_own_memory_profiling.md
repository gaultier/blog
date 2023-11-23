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
3. We can then use the (original) pprof which is just a [https://github.com/gperftools/gperftools/blob/master/src/pprof](giant Perl script) which will extract interesting information and most importantly symbolize (meaning: transform memory addresses into line/column/function/file information)

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

The text file ends with a trailer which is crucial for symbolication (to transform memory addresses into source code locations), which (at least on Linux) is trivial to get: This is just a copy of the file `/proc/self/maps`. It lists of the loaded libraries and at which address they are.
