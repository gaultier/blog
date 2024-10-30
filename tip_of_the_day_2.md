Title: Tip of the day #2: A safer arena allocator
Tags: C, Allocator
---

The most transformative action you can do to dramatically improve your code in a programming language where you are in control of the memory is: to use arenas.

Much has been written about arenas (TODO links). In short, it means grouping multiple allocations with the same lifetime in one batch that gets allocated and deallocated only once.

Another way to look at it, is that the allocations are append only. They never get freed during their 'life'. The program is split into 'phases'. Typically, each phase has its own arena, and when it reaches its end, the whole arena gets nuked from space and all entities attached to it alongside it. It's a great way to simplify the code, make it faster, and escape from the 'web of pointers' hell.



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

Very simple, just ask the OS to give us a region of virtual memory and off we go (on Windows, the system call is different, but bottom line, there is an equivalent one).

Now, since we use a system call directly, sanitizers and runtime checks from the libc allocator do not apply, since we bypass them completely.

So it turns out that I had a bug in my code: I allocated an array from the arena, and then accidentally wrote paste the bounds of my array (so far, this sounds like a typical story from the C trenches). 

Normally, this would likely (depending on a few factors) write past the memory page that the OS gave us, thus triggering a `SIGSEGV`.

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


So how do we fix it? What I did was defence in depth:

- Add asserts everywhere I could to check pre and post conditions. I believe that's how I discovered the bug in the first place, when one assert failed, even though it seemed impossible.
- Replace all direct array and pointer accesses with macros that check bounds (like most modern programming languages)
- Tweak how the arena is created to make it safer. That's our tip of the day, so let's see it.


The idea is not new, most allocators do so in 'hardening' mode. When the arena is created, we place a 'guard page' right before and after the real allocation. 

We mark these guard pages as neither readable nor writable, so any access will trigger a `SIGSEGV`, even though that's memory owned by our program.

That way, going slightly past the bounds of the real allocation in either direction, will result in a crash that's easy to diagnose.

Note that this is a tradeoff:

- It will not catch all out-of-bounds accesses. We could get unlucky and accidentally hit the memory of another arena still. This is a protection that typically helps with off-by-one errors.
- It's very lightweight: the OS only has to maintain an entry in a table, recording that the program owns the two additional pages (per arena). No actualy physicaly memory will be dedicated for them. But, if there are millions of arenas, it could make a difference.
- It's theoritically tunable: nothing prevents us from having larger guard 'regions'. If we are paranoid, we could make the guard region 64 Gib before and after the real allocation of 4096 bytes, if we wish. That's the power of virtual memory.
- The granularity is still the page (typically 4096 bytes, something larger). We cannot easily prevent out-of-bounds accesses within a page.
- The original implementation at the beginning of the article did not have to bother with the size of a page. But this implementation has to, which slightly complicates the logic (but not by much).
