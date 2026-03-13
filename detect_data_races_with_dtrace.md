Title: Detect data races with DTrace in any language
Tags: DTrace, Concurrency
---

A data race is concurrent access to shared data in a way that does not respect the rules of the programming language. Some languages are stricter or looser when they establish how that can happen, but they all forbid *some* kinds of concurrent (unsynchronized) accesses, typically write-write or read-write.

## The theory

For example [Go's memory model](https://go.dev/ref/mem#model) defines a data race in terms of 'happens-before' and 'synchronized before':


>  A read-write data race on memory location x consists of a read-like memory operation r on x and a write-like memory operation w on x, at least one of which is non-synchronizing, which are unordered by happens before (that is, neither r happens before w nor w happens before r).
>
> A write-write data race on memory location x consists of two write-like memory operations w and w' on x, at least one of which is non-synchronizing, which are unordered by happens before.
>
> Note that if there are no read-write or write-write data races on memory location x, then any read r on x has only one possible W(r): the single w that immediately precedes it in the happens before order. 
>
>  More generally, it can be shown that any Go program that is data-race-free, meaning it has no program executions with read-write or write-write data races, can only have outcomes explained by some sequentially consistent interleaving of the goroutine executions. (The proof is the same as Section 7 of Boehm and Adve's paper cited above.) This property is called DRF-SC.
> 
> The intent of the formal definition is to match the DRF-SC guarantee provided to race-free programs by other languages, including C, C++, Java, JavaScript, Rust, and Swift. 



## In practice

This is great to have formally defined, but it's not very actionable. Many programming languages do not have enough compile time guarantees to avoid data races at compile time, and thus provide a runtime race detector: Go, C and C++ with Thread Sanitizer, etc.

Typically these can detect some data races but not all, and incur a big performance penalty (I have experienced x5 to x20 slow-downs in real production code).

Also, these cannot be used in some contexts. What can we do then?

What if we could somehow use DTrace to observe our program and detect, like a runtime race detector, invalid accesses? It does not have to catch *all* races, *some* would be already nice. Even a crude approach could be helpful.

So let's simplify things first: we'll exclude atomics for now. Since synchronized operations are safe, and mutexes do synchronize:

> For any sync.Mutex or sync.RWMutex variable l and n < m, call n of l.Unlock() is synchronized before call m of l.Lock() returns. 

These lock and unlock operations create a 'critical', or 'exclusive', section. If two or more threads access concurrently this section without synchronization (i.e. without mutex), and this section does a write, then we have a data race.

So here is the idea:

- We suspect a data structure to be racy
- We observe with DTrace functions that mutate ('write') this data structure. DTrace can also observe arbitrary locations in the program with [function offset probes (33.6.2. Tracing Arbitrary Instructions)](https://illumos.org/books/dtrace/chp-user.html#chp-user) or statically defined probes, but simply tracing functions is enough to demonstrate the point here.
- When entering such a function, we record the thread id in a global map, where the key is the location, and the value is the thread id. 
- When exiting such a function, we clear the entry in the map. 
- If we enter such a function and we see from the global map that another thread is already running this function, this means that we have a concurrent access to the data structure in a write-write or read-write fashion. This is a data race and we report it.



## Example


I fixed recently a data race in Go at work. I have reproduced it in C for simplicity, because Go inlines quite heavily and some functions, e.g. `append()`, `len()`, are actually builtin, it's hard to trace them.

The program appends data to a growable byte array in a thread, and reads the length of this byte array in another thread, without synchronization. Text book data race, but this kind of thing happens in production code when the compiler does not protect us from ourselves:

```c
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DATA_LEN (1 << 20)

typedef struct {
  uint8_t *data;
  size_t len, cap;
} ByteArray;

size_t byte_array_get_len(ByteArray *b) { return b->len; }

size_t byte_array_get_cap(ByteArray *b) { return b->cap; }

void byte_array_set_len(ByteArray *b, size_t len) { b->len = len; }

void byte_array_set_cap(ByteArray *b, size_t cap) { b->cap = cap; }

void byte_array_ensure_cap_at_least(ByteArray *b, size_t cap) {
  size_t current_cap = byte_array_get_cap(b);

  if (current_cap >= cap) {
    return;
  }

  byte_array_set_cap(b, current_cap < 8 ? 8 : current_cap * 2);

  b->data = realloc(b->data, b->cap);
}

void byte_array_push(ByteArray *b, uint8_t elem) {
  const size_t len = byte_array_get_len(b);
  byte_array_ensure_cap_at_least(b, len + 1);
  b->data[len] = elem;

  byte_array_set_len(b, len + 1);
}

void *async_push(void *any) {
  ByteArray *b = any;

  for (size_t i = 0; i < DATA_LEN; i++) {
    byte_array_push(b, (uint8_t)i);
  }

  return NULL;
}

int main() {
  ByteArray byte_array = {0};

  pthread_t thread_push = {0};
  pthread_create(&thread_push, NULL, async_push, &byte_array);

  for (;;) {
    const size_t len = byte_array_get_len(&byte_array);
    if (len == DATA_LEN) {
      break;
    }
  }

  assert(byte_array_get_len(&byte_array) == DATA_LEN);
}
```

A few notes about this code:

- I use getters and setters for one reason only: to make it easy to observe read and write operations in DTrace. If you hate getters and setters, the same can be achieved with DTrace static probes.
- The main thread polls very aggressively watching the length of the growable array. It's effectively a spin-lock, that unlocks when the length has reached the target value. In real code there would be a `nanosleep()` call in each loop ieration, or a proper synchronization mechanism e.g. a condition variable or simply a call to `pthread_join`. But I have seen plenty code like this in the wild.
- The obvious data race is on the `len` field of the byte array. It's likely benign in unoptimized mode, because the consequence in the current code is that we'll either do a few extra loop iterations or the assert at the end will fail. However the compiler is free to re-order the code since there are no explicit data dependencies and that's where the fun begins. Don't be this guy that says 'well, it's a data race, but it isn't actually that bad, so let's not fix it...'. It's a matter of *when*, not *if*, it's going to explode in your hand.


---

Now we run it with Thread Sanitizer:

```shell
$ clang blog1.c -g -o blog1 -fsanitize=thread
$ ./blog1
```

And it appears to run just fine, no warning whatsoever.


Alright, now let's implement our idea in DTrace:

```dtrace
#pragma D option dynvarsize=16m
#pragma D option cleanrate=100hz

typedef enum {AccessKindRead=1, AccessKindWrite=2} AccessKind;

typedef struct {
  AccessKind kind;
  size_t tid;
  int ts;
} Access;

Access accesses[uintptr_t /* data ptr */];

int func_access[string];

BEGIN {
  func_access["byte_array_get_len"] = AccessKindRead;
  func_access["byte_array_set_len"] = AccessKindWrite;
}

pid$target::pthread_rwlock_*:entry {
  self->lock = arg0;
  printf("ts=%d tid=%d lock=%p\n", timestamp, tid, arg0);
}

pid$target::pthread_rwlock_*:return {
  printf("ts=%d tid=%d lock=%p\n", timestamp, tid, self->lock);
}


pid$target::byte_array_?et_len:entry {
  self->mem_ptr = arg0 + 8;
  this->theirs = accesses[self->mem_ptr];
  this->my_access_kind = func_access[probefunc];
  this->now = timestamp;


  if (this->theirs.tid !=0 && 
      this->theirs.tid != tid && 
      (this->my_access_kind == AccessKindWrite || this->theirs.kind == AccessKindWrite)) {
    printf("possible data race: my_access_kind:%d my_tid=%d my_ts=%d their_access_kind:%d their_tid=%d their_ts=%d mem_ptr=%p\n", this->my_access_kind, tid, this->now, this->theirs.kind, this->theirs.tid, this->theirs.ts, self->mem_ptr);
    ustack();
  }

  accesses[self->mem_ptr].kind = this->my_access_kind;
  accesses[self->mem_ptr].tid = tid;
  accesses[self->mem_ptr].ts = this->now;
}

pid$target::byte_array_?et_len:return /self->mem_ptr != 0/ {
  accesses[self->mem_ptr].kind = 0;
  accesses[self->mem_ptr].tid = 0;
  accesses[self->mem_ptr].ts = 0;
  self->mem_ptr = 0;
}
```

We observe here all 'methods' of `byte_array`. This is heavy-handed and might cause false positives, for example if two threads are concurrently calling `byte_array_get_len`, this is a read-read case and not a data race. But this is simple.

When we detect a data race, we print the id of the two colliding threads and the call stack of the current thread. Note that we do not print the call stack of the other thread, but chances are, if thread A has a race with thread B, we'll alternate detecting the race inside thread A, and inside thread B, so we'll have both call stacks.

Now let's run our D script. We see a ton of these messages:

```shell
$ sudo dtrace -s ./blog.d -c ./blog1

[...]

  4  16760         byte_array_get_len:entry WARN: data race:(246345030, 246344951)

              blog1`byte_array_get_len
              blog1`main+0x78
              dyld`start+0x1c10

  7  16760         byte_array_get_len:entry WARN: data race:(246344951, 246345030)

              blog1`byte_array_get_len
              blog1`byte_array_push+0x30
              blog1`async_push+0x40
              libsystem_pthread.dylib`_pthread_start+0x88
              libsystem_pthread.dylib`thread_start+0x8

[...]
```


Indeed, the main thread has a data race when calling `byte_array_get_len` with 
