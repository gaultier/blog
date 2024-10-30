Title: Tip of the day #2: A safer arena allocator
Tags: C, Allocator, Safety
---

The most transformative action you can do to dramatically improve your code in a programming language where you are in control of the memory is: to use arenas.

Much has been written about arenas ([1](https://nullprogram.com/blog/2023/09/27/), [2](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)). In short, it means grouping multiple allocations with the same lifetime in one batch that gets allocated and deallocated only once.

Another way to look at it, is that the allocations are append only. They never get freed during their 'life'. The program is split into 'phases'. Typically, each phase has its own arena, and when it reaches its end, the whole arena gets nuked from space along with all entities allocated from it. It's a great way to simplify the code, make it faster, and escape from the 'web of pointers' hell.


## The standard arena

A typical arena looks like that:

```c
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
  uint8_t *start;
  uint8_t *end;
} Arena;

static Arena arena_make_from_virtual_mem(uint64_t size) {
  uint8_t *alloc = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
  return (Arena){.start = alloc, .end = alloc + size};
}

static void *
arena_alloc(Arena *a, uint64_t size, uint64_t align, uint64_t count) {
  const uint64_t padding = (-(uint64_t)a->start & (align - 1));
  const int64_t available = (int64_t)a->end - (int64_t)a->start - (int64_t)padding;

  void *res = a->start + padding;

  a->start += padding + count * size;

  return memset(res, 0, count * size);
}

int main() {
  Arena a = arena_make_from_virtual_mem(4096);
}
```

Very simple, just ask the OS to give us a region of virtual memory and off we go (on Windows, the system call is named differently but is equivalent).


## The bug

Now, since we use a system call directly, sanitizers and runtime checks from the libc allocator do not apply, since we bypass them completely. In a way, it is also a feature: it means that our program will behave exactly the same on all OSes, have the exact same memory layout, and use the exact same amount of memory. It does not depend on the libc or allocator.

So it turns out that I had a bug in my code: I allocated an array from the arena, and then accidentally wrote past the bounds of my array (so far, this sounds like a typical story from the C trenches). 

Normally, this would likely (depending on a few factors, like where in the arena was this allocation located, how big was it, and by how many bytes did the write go past the bounds, etc) write past the memory page that the OS gave us, thus triggering a `SIGSEGV`.

However, in that instance, I got unlucky, because my code actually did something like that:

```c
int main() {
  Arena a = arena_make_from_virtual_mem(4096);
  Arena b = arena_make_from_virtual_mem(4096);

  // Simulate writing past the arena:
  a.start + 5000 = 42;
}
```

And...the program did not crash. The symptoms were very weird: data was subtly wrong in another place of the program, thus making it very difficult to troubleshoot. That's basically the nightmare scenario for any engineer. A crash would be so much easier.

But why?

Well, we basically asked the OS to give us one page of virtual memory when creating the first arena. Right after, we asked for a second page. And most often than not, the OS gives us then a page right after the first page. So from the OS perspective, we allocated `2 * 4096 = 8192` bytes, and wrote in the middle, so all is good. We wanted to write into the first arena but instead wrote into the second one accidentally. 

This behavior is however not consistent, running the programs many times will sometimes crash and sometimes not. It all depends if the memory pages for the different arenas are contiguous or not.

## The solution

So how do we fix it? What I did was defense in depth:

- Add asserts everywhere I could to check pre- and post-conditions. I believe that's how I discovered the bug in the first place, when one assert failed, even though it seemed impossible.
- Replace all direct array and pointer accesses with macros that check bounds (like most modern programming languages)
- Tweak how the arena is created to make it safer. That's our tip of the day, so let's see it.


The idea is not new, most allocators do so in 'hardening' mode: when the arena is created, we place a 'guard page' right before and after the real allocation. 

We mark these guard pages as neither readable nor writable, so any access will trigger a `SIGSEGV`, even though that's memory owned by our program.

That way, going slightly past the bounds of the real allocation in either direction, will result in a crash that's easy to diagnose.

Note that this is a trade-off:

- It will not catch all out-of-bounds accesses. We could get unlucky and accidentally hit the memory of another arena still. This is a protection that typically helps with off-by-one errors.
- It's very lightweight: the OS only has to maintain an entry in a table, recording that the program owns the two additional pages (per arena). No actually physical memory will be dedicated for them. But, if there are millions of arenas, it could make a difference.
- It's theoretically tunable: nothing prevents us from having larger guard 'regions'. If we are paranoid, we could make the guard region 64 Gib before and after the real allocation of 4096 bytes, if we wish. That's the power of virtual memory.
- The granularity is still the page (typically 4096 bytes, something larger). We cannot easily prevent out-of-bounds accesses within a page.
- The original implementation at the beginning of the article did not have to bother with the size of a page. But this implementation has to, which slightly complicates the logic (but not by much).


So here it is:

```c
static Arena arena_make_from_virtual_mem(uint64_t size) {
  uint64_t page_size = (uint64_t)sysconf(_SC_PAGE_SIZE);
  uint64_t alloc_real_size = round_up_multiple_of(size, page_size);

  // Page guard before + after.
  uint64_t mmap_size = alloc_real_size + 2 * page_size;

  uint8_t *alloc = mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);

  uint64_t page_guard_before = (uint64_t)alloc;

  alloc += page_size;
  uint64_t page_guard_after = (uint64_t)alloc + alloc_real_size;

  mprotect((void *)page_guard_before, page_size, PROT_NONE);
  mprotect((void *)page_guard_after, page_size, PROT_NONE);

  return (Arena){.start = alloc, .end = alloc + size};
}
```

We get the page size with `sysconf`. Again, that's required because we will use the system call `mprotect` to change the permissions on parts of the memory, and `mprotect` expects a page-aligned memory range.

Since an allocation is at least one page, even if the user asked for an arena of size `1`, we first round the user allocation size up, to the next page size. E.g. for a page size of `4096`: `1 -> 4096`, `4095 -> 4096`, `4096 -> 4096`, `4097` -> `8192`.

Then, in one `mmap` call, we allocate all the memory we need including the two guard pages. For a brief moment, all the memory is readable and writable. The very next thing we do is mark the first page and last page as neither readable nor writable. We then return the arena, and the user is none the wiser.

Wouldn't it be simpler to issue `3` `mmap` calls with the right permissions from the get go? Well, yes, but there is no guarantee that the OS would give us a contiguous region of memory across these 3 calls. On Linux, we can give hints, but still there is no guarantee. Remember, our program is one of many running concurrently, and could get interrupted for some time between these `mmap` calls, the whole OS could go to sleep, etc. What we want is an atomic operation, thus, one `mmap` call.

So that's it, a poor man Adress Sanitizer in a few lines of code.


## Variations

## The paranoid approach

If we are really paranoid, we could change how the arena works, to make every allocation get a new, separate page from the OS. That means that creating the arena would do nothing, and allocating from the arena would do the real allocation. This approach is, to me, indistinguishable from a general purpose allocator a la `malloc` from libc, just one that's very naive, and probably much slower.

But, if there is a pesky out-of-bound bug pestering you, that could be worth trying.

## The bucket per type approach

On [Apple platforms](https://www.youtube.com/watch?v=t7EJTO0-reg), the libc allocator has a hardening mode that can be enabled at compile time. It stems from the realization that many security vulnerabilities rely on type confusion: The program thinks it is handling an entity of type `X`, but due to a logic bug, or the attacker meddling, or the allocator reusing freshly freed memory from another place in the program, it is actually of another type `Y`. This results in an entity being in an 'impossible' state which is great for an attacker. Also, reusing a previously allocated-then-freed object with a different type, without zero-initializing it, can leak secrets or information about the state of the program, to an attacker.

So, the mitigation is to place all allocations of the same type in one bucket (supposedly, it's a separate memory region with guard pages before and after). When an object of type `X` is allocated, then freed, and then the program allocates an object of type `Y`, of roughly the same size, a typical allocator will reuse the memory of `X`. This Apple allocator would give memory from a separate bucket.

What I don't know, is whether or not there are runtime checks as well, for example when casting one object from one type to another e.g. with `reinterpret_cast` in C++. It seems that this allocator would have the information needed at runtime to do so, which could be an interesting feature.

Now, having one bucket per type turns out to be too slow in reality, and consumes too much memory, according to Apple developers, so as a trade-off, this allocator groups a handful a different types in one bucket. This is a typical trade-off between performance and security.

Still, this is an interesting approach, and could be implemented in our context by having one arena store all entities of one type, i.e. one arena is one bucket.

## See also

I wrote in the past about adding memory profiling an arena allocator: [Roll your own memory profiling: it's actually not hard](https://gaultier.github.io/blog/roll_your_own_memory_profiling.html).

