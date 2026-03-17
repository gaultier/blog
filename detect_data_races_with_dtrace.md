Title: Detect data races with DTrace in any language
Tags: DTrace, Concurrency
---

*For a gentle introduction to DTrace especially in conjunction with Go, see my past article: [An optimization and debugging story with Go and DTrace](/blog/an_optimization_and_debugging_story_go_dtrace.html), or my other [DTrace articles](/blog/articles-by-tag.html#dtrace).*

A data race is a concurrent access to shared data in a way that does not respect the rules of the programming language. Some languages are stricter or looser when they establish how that can happen, but they all forbid *some* kinds of concurrent (unsynchronized) accesses, typically write-write or read-write. 

The symptoms are bizarre and very painful to diagnose: stale reads, inconsistent data, 'impossible' code path taken, crashes, etc. 

Since some compilers are very aggressive at re-ordering and optimizing code in the absence of explicit data dependencies, the compiled program can be very different from what the code looks like.

This is exacerbated by another actor: the CPU. On a typical machine, there are multiple CPUs, some on the same die and some on different dies, and they all have to coordinate to access the same memory. They also share a cache which has to be kept in sync with the real data. A data race will typically result in an out-of-date cache.

## The theory

[Go's memory model](https://go.dev/ref/mem#model) defines a data race in terms of 'happens-before' and 'synchronized before':


>  A read-write data race on memory location x consists of a read-like memory operation r on x and a write-like memory operation w on x, at least one of which is non-synchronizing, which are unordered by happens before (that is, neither r happens before w nor w happens before r).
>
> A write-write data race on memory location x consists of two write-like memory operations w and w' on x, at least one of which is non-synchronizing, which are unordered by happens before.
>
> Note that if there are no read-write or write-write data races on memory location x, then any read r on x has only one possible W(r): the single w that immediately precedes it in the happens before order. 
>
>  More generally, it can be shown that any Go program that is data-race-free, meaning it has no program executions with read-write or write-write data races, can only have outcomes explained by some sequentially consistent interleaving of the goroutine executions. (The proof is the same as Section 7 of Boehm and Adve's paper cited above.) This property is called DRF-SC.
> 
> The intent of the formal definition is to match the DRF-SC guarantee provided to race-free programs by other languages, including C, C++, Java, JavaScript, Rust, and Swift. 



[C's standard](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf) uses similar language, section '5.1.2.5 Multi-threaded executions and data races':

> [...] Two expression evaluations conflict if one of them modifies a memory location and the other one reads or modifies the same memory location.
> 
> [...] The library defines atomic operations (7.17) and operations on mutexes (7.28.4) that are specially
> identified as synchronization operations. These operations play a special role in making assignments
> in one thread visible to another. A synchronization operation on one or more memory locations is
> one of an acquire operation, a release operation, both an acquire and release operation, or a consume
> operation. A synchronization operation without an associated memory location is a fence and can
> be either an acquire fence, a release fence, or both an acquire and release fence. In addition, there
> are relaxed atomic operations, which are not synchronization operations, and atomic read-modify-write
> operations, which have special characteristics.

## In practice

This is great to have formally defined, but it's not very actionable. Many programming languages do not have enough compile time guarantees to avoid data races at compile time, and thus provide a runtime race detector: Go, C and C++ with Thread Sanitizer (a.k.a. TSan), etc.

Typically these can detect some data races but not all, and incur a big performance penalty (I have experienced x5 to x20 slow-downs in real production code). Also, they typically require that *all* the code in the program is compiled with this detector enabled, which is sometimes very time-consuming, or not possible at all (some projects use closed-source libraries). Additionally there is the case of new emerging programming languages which do not have yet a race detector implemented.

What can we do then?

What if we could somehow use DTrace to observe our program and detect, like a runtime race detector, invalid accesses? It does not have to catch *all* races, *some* would be already nice. Even a crude approach could be helpful.


So here is the idea:

- We suspect a data structure or specific code location to be racy
- Using DTrace, we observe functions that read and write this data structure. DTrace can also observe arbitrary locations in the program with [function offset probes (33.6.2. Tracing Arbitrary Instructions)](https://illumos.org/books/dtrace/chp-user.html#chp-user) or statically defined probes, but simply tracing functions is enough to demonstrate the point here.
- When entering such a function, we record in a global map the thread id and the kind of access (read/write). The key in this map is the memory address being accessed.
- When exiting such a function, we clear the entry in the map, because the access has ended. 
- When start to access a piece of memory, we check the global map of accesses. If we see that another thread is already accessing the same memory address, this means that we have a concurrent access to the data structure. If at least one of them is a write (read-read is fine), this is a potential data race.

This can be refined further as we'll see but it's good enough for now, and that's the basis of what Thread Sanitizer does.



## Example


I recently [fixed](https://github.com/ory/kratos/commit/66739820c9d45ad4bc465b2ce3e10311967e29e4) a data race in Go at work. Since it is an open-source project I can share my work which is great! I have reproduced this race in C for simplicity, because Go inlines function calls quite heavily and some functions, e.g. `append()`, `len()`, are not real functions but in fact builtin, it's hard to trace them.

In theory DTrace can trace arbitrary instructions and static probes, but in Go static probes are annoying to declare since that needs CGO, and on ARM64 macOS (my current laptop) tracing arbitrary instructions does not work.

The program appends data to a growable byte array in a thread, and reads the length of this byte array in another thread, without synchronization, until an expected value is reached. Textbook read-write data race, but this kind of thing happens in production code when the compiler does not protect us from ourselves:

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
- The main thread polls very aggressively watching the length of the growable array. It's effectively a spin-lock, that unlocks when the length has reached the target value. In real code there would be a `nanosleep()` call in each loop iteration (that's what the Go production code did), or a proper synchronization mechanism e.g. a condition variable or simply a call to `pthread_join` (but Go does not have a way to wait on a goroutine 'id').
- The obvious data race is on the `len` field of the byte array. It's likely benign in unoptimized mode, because the consequence in the current code is that we'll either do a few extra loop iterations or the assert at the end will fail. 
- However the compiler is free to re-order the code since there are no explicit data dependencies and that's where the fun begins. Don't be this guy that says 'well, it's a data race, but it isn't actually that bad, so let's not fix it...'. It's a matter of *when*, not *if*, it's going to explode in your hand.
- In fact when compiling this program in release mode it never terminates. That's because data races are undefined behavior and the compiler is free to do whatever it wants. One possible explanation is that the compiler assumes that no other thread modifies `len` and thus `len` is a constant, and the check can be hoisted out of the loop. When we look at the generated assembly, we see that the compiler indeed turned our for-loop into an infinite loop e.g.:
  ```c
  for (;;) {
    const size_t len = byte_array_get_len(&byte_array);
    if (len == DATA_LEN) {
      break;
    }
  }
  ```
  became: 
  ```c
  for(;;) {
  }
  ```
- The moment someone modifies the program, for example to print the last element of the array inside the loop on the main thread, we'll likely get a segfault, because `byte_array_push` modifies the fields of the `ByteArray` structure, which have to always be consistent with each other: `data`, `len`, and `cap`.  Since there is no synchronization primitive to ensure that, the other threads can see the structure in an inconsistent, half-updated state. That's the reason why we cannot simply make `len` an atomic. This issue of inconsistent state is even clearer if one thread does a lot of `byte_array_push()` and another `byte_array_pop()`: the program will very quickly explode because it tried to access a value out of bounds in the array, for example.
- There is another big issue with this code: since the thread pushing values to the growable array uses `realloc()`, the `data` field, which holds all the array elements, may change each time a new element is added: the allocator may decide to allocate a bigger piece of memory, return the address, and free the old piece of memory. Since the main thread has no synchronization with the other thread, if it decides to, for example, print the first element of the array, even if `len` has the latest value and is greater than zero, it may well be that `data` has a stale value and points to the old memory. That's a use-after-free bug, and a potential security vulnerability.



## Thread Sanitizer

Now we run it with Thread Sanitizer:

```shell
$ clang blog1.c -g -o blog1 -fsanitize=thread
$ ./blog1
```

And it appears to run just fine, no warning whatsoever (actually a warning complains that we never call `pthread_join` but that's ok).

If you get lucky, you'll see one diagnostic:

```plaintext
==================
WARNING: ThreadSanitizer: data race (pid=2145)
  Read of size 8 at 0x00016d946f08 by main thread:
    #0 byte_array_get_len blog1.c:14 (blog1:arm64+0x100000610)
    #1 main blog1.c:59 (blog1:arm64+0x1000009fc)

  Previous write of size 8 at 0x00016d946f08 by thread T1:
    #0 byte_array_set_len blog1.c:18 (blog1:arm64+0x1000006dc)
    #1 byte_array_push blog1.c:39 (blog1:arm64+0x100000904)
    #2 async_push blog1.c:46 (blog1:arm64+0x100000970)

  Location is stack of main thread.

  Thread T1 (tid=272869675, running) created by main thread at:
    #0 pthread_create <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64+0x31ac0)
    #1 main blog1.c:56 (blog1:arm64+0x1000009f0)

SUMMARY: ThreadSanitizer: data race blog1.c:14 in byte_array_get_len
==================
```

But this was very rare in my testing.


## The DTrace race detector


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
  // Record which kind of access each function does.
  func_access["byte_array_get_len"] = AccessKindRead;
  func_access["byte_array_set_len"] = AccessKindWrite;
}

// Watch `byte_array_get_len` and `byte_array_set_len`.
pid$target::byte_array_?et_len:entry {
  // `arg0` is the pointer to the `ByteArray` structure,
  // and `len` is at offset `8` in the `ByteArray` structure (on 64 bits systems).
  // So this gets the value of `byte_array->len`.
  self->mem_ptr = arg0 + 8;
  this->theirs = accesses[self->mem_ptr];
  // Get the access kind based on the function: `byte_array_get_len` is a read and `byte_array_set_len` is a write.
  this->my_access_kind = func_access[probefunc];
  this->now = timestamp;

  if (this->theirs.tid !=0 &&  // 'if a thread is concurrently accessing the same memory...'
      this->theirs.tid != tid &&  // 'and this is another thread as the current one...'
      (this->my_access_kind == AccessKindWrite || this->theirs.kind == AccessKindWrite)) { // 'and at least one access is a write...'
    printf("possible data race: my_access_kind:%d my_tid=%d my_ts=%d their_access_kind:%d their_tid=%d their_ts=%d mem_ptr=%p\n", this->my_access_kind, tid, this->now, this->theirs.kind, this->theirs.tid, this->theirs.ts, self->mem_ptr);
    ustack();
  }

  // Update the map with the current access.
  accesses[self->mem_ptr].kind = this->my_access_kind;
  accesses[self->mem_ptr].tid = tid;
  accesses[self->mem_ptr].ts = this->now;
}

pid$target::byte_array_?et_len:return /self->mem_ptr != 0/ {
  // Clear the map since the access is done.
  accesses[self->mem_ptr].kind = 0;
  accesses[self->mem_ptr].tid = 0;
  accesses[self->mem_ptr].ts = 0;
  self->mem_ptr = 0;
}
```

A few notes about this D script:

- We only observe here the getter and setter for `len` to demonstrate, but of course this can be done for all methods.
- The check `this->theirs.tid != tid` is important, because without this check, if a single thread calls a recursive function, we would think this is a data race, but it's not.
- When we detect a data race, we print the id of the two colliding threads and the call stack of the current thread. Note that we do not print the call stack of the other thread, but chances are, if thread A has a race with thread B, we'll alternate detecting the race inside thread A, and inside thread B, so we'll have both call stacks.

---

Now let's run our D script. We see a ton of these messages:

```shell
$ sudo dtrace -s ./blog.d -c ./blog1

[...]

 11 242002         byte_array_get_len:entry possible data race: my_access_kind:1 my_tid=273369021 my_ts=1447848961324000 their_access_kind:2 their_tid=273369096 their_ts=-1694029743 mem_ptr=16bd5f118

              blog1`byte_array_get_len
              blog1`main+0x44
              dyld`start+0x1c10

 12 242003         byte_array_set_len:entry possible data race: my_access_kind:2 my_tid=273369096 my_ts=1447848963598000 their_access_kind:1 their_tid=273369021 their_ts=-1691755409 mem_ptr=16bd5f118

              blog1`byte_array_set_len
              blog1`byte_array_push+0x58
              blog1`async_push+0x40
              libsystem_pthread.dylib`_pthread_start+0x88
              libsystem_pthread.dylib`thread_start+0x8
[...]
```


Indeed, the main thread has a data race when calling `byte_array_get_len` with the other thread which calls `byte_array_set_len`, as expected. We even see that it is a read-write data races, from the printed accesses.

Now let's fix the program crudely with a mutex:

```diff
--- blog1.c	2026-03-13 16:39:18
+++ blog_mtx.c	2026-03-13 16:38:48
@@ -9,6 +9,7 @@
 typedef struct {
   uint8_t *data;
   size_t len, cap;
+  pthread_mutex_t mtx;
 } ByteArray;
 
 size_t byte_array_get_len(ByteArray *b) { return b->len; }
@@ -32,11 +33,14 @@
 }
 
 void byte_array_push(ByteArray *b, uint8_t elem) {
+  pthread_mutex_lock(&b->mtx);
+
   const size_t len = byte_array_get_len(b);
   byte_array_ensure_cap_at_least(b, len + 1);
   b->data[len] = elem;
 
   byte_array_set_len(b, len + 1);
+  pthread_mutex_unlock(&b->mtx);
 }
 
 void *async_push(void *any) {
@@ -50,13 +54,15 @@
 }
 
 int main() {
-  ByteArray byte_array = {0};
+  ByteArray byte_array = {.mtx = PTHREAD_MUTEX_INITIALIZER};
 
   pthread_t thread_push = {0};
   pthread_create(&thread_push, NULL, async_push, &byte_array);
 
   for (;;) {
+    pthread_mutex_lock(&byte_array.mtx);
     const size_t len = byte_array_get_len(&byte_array);
+    pthread_mutex_unlock(&byte_array.mtx);
     if (len == DATA_LEN) {
       break;
     }
```


And now our D script shows no warning of potential data races. Yay!

There are many ways to fix this race, this one might not be the best since it will cause a lot of lock contention, so let's try another approach.

## Improvements


Now you might be thinking: wait a minute, how do we know that the D script is actually working? It seems to track concurrent accesses, but the fix with the mutex actually creates a critical section, or 'exclusive' section, where only one thread has access at any point to the data structure. As such, there are no concurrent accesses.

Let's check the correctness by applying a smarter, possibly more performant fix of the data race using a RW lock (called in Go `sync.RWMutex`):

```diff
--- blog1.c	2026-03-13 16:39:18
+++ blog_rwlock.c	2026-03-13 16:38:58
@@ -9,6 +9,7 @@
 typedef struct {
   uint8_t *data;
   size_t len, cap;
+  pthread_rwlock_t rw_lock;
 } ByteArray;
 
 size_t byte_array_get_len(ByteArray *b) { return b->len; }
@@ -32,11 +33,15 @@
 }
 
 void byte_array_push(ByteArray *b, uint8_t elem) {
+  pthread_rwlock_wrlock(&b->rw_lock);
+
   const size_t len = byte_array_get_len(b);
   byte_array_ensure_cap_at_least(b, len + 1);
   b->data[len] = elem;
 
   byte_array_set_len(b, len + 1);
+
+  pthread_rwlock_unlock(&b->rw_lock);
 }
 
 void *async_push(void *any) {
@@ -50,13 +55,15 @@
 }
 
 int main() {
-  ByteArray byte_array = {0};
+  ByteArray byte_array = {.rw_lock = PTHREAD_RWLOCK_INITIALIZER};
 
   pthread_t thread_push = {0};
   pthread_create(&thread_push, NULL, async_push, &byte_array);
 
   for (;;) {
+    pthread_rwlock_rdlock(&byte_array.rw_lock);
     const size_t len = byte_array_get_len(&byte_array);
+    pthread_rwlock_unlock(&byte_array.rw_lock);
     if (len == DATA_LEN) {
       break;
     }
```

The code is very similar in its structure to the mutex version, but this now allows for N concurrent reader or one concurrent writer. It means that we now can have multiple concurrent threads reading the same address in memory. 

To take full advantage of this, we spawn another thread that also reads the length in a loop:

```diff
diff --git a/blog_rwlock b/blog_rwlock
index 8e7a442..b5c8920 100755
Binary files a/blog_rwlock and b/blog_rwlock differ
diff --git a/blog_rwlock.c b/blog_rwlock.c
index a402298..271acf5 100644
--- a/blog_rwlock.c
+++ b/blog_rwlock.c
@@ -54,12 +54,29 @@ void *async_push(void *any) {
   return NULL;
 }
 
+void *async_read(void *any) {
+  ByteArray *b = any;
+
+  for (;;) {
+    pthread_rwlock_rdlock(&b->rw_lock);
+    const size_t len = byte_array_get_len(b);
+    pthread_rwlock_unlock(&b->rw_lock);
+    if (len == DATA_LEN) {
+      break;
+    }
+  }
+  return NULL;
+}
+
 int main() {
   ByteArray byte_array = {.rw_lock = PTHREAD_RWLOCK_INITIALIZER};
 
   pthread_t thread_push = {0};
   pthread_create(&thread_push, NULL, async_push, &byte_array);
 
+  pthread_t thread_read = {0};
+  pthread_create(&thread_read, NULL, async_read, &byte_array);
+
   for (;;) {
     pthread_rwlock_rdlock(&byte_array.rw_lock);
     const size_t len = byte_array_get_len(&byte_array);
```

Let's re-run our D script:

```shell
$ sudo dtrace -s ./blog.d -c ./blog_rwlock
```

And... no warnings, as expected. Terrific! 

This is the clause that does all the heavy lifting:

```dtrace
      (this->my_access_kind == AccessKindWrite || this->theirs.kind == AccessKindWrite)) { // 'and at least one access is a write...'
```

And N concurrent readers are fine. In fact, if we comment out this condition, we see indeed that we have two concurrent readers and lots of false positives get printed:

```plaintext

 13   6893         byte_array_get_len:entry possible data race: my_access_kind:1 my_tid=274158070 my_ts=1449489633975000 their_access_kind:1 their_tid=274158134 their_ts=0 mem_ptr=16eed3030

              blog_rwlock`byte_array_get_len
              blog_rwlock`main+0x9c
              dyld`start+0x1c10

 11   6893         byte_array_get_len:entry possible data race: my_access_kind:1 my_tid=274158134 my_ts=1449489634196625 their_access_kind:1 their_tid=274158070 their_ts=-1698664731 mem_ptr=16eed3030

              blog_rwlock`byte_array_get_len
              blog_rwlock`async_read+0x30
              libsystem_pthread.dylib`_pthread_start+0x88
              libsystem_pthread.dylib`thread_start+0x8
```

## Benchmarks

|                                     | Runtime | Runtime with Thread Sanitizer | Runtime with DTrace race detector |
|-------------------------------------|---------|-------------------------------|-----------------------------------|
| Racy program, debug mode            | 6.3 ms  | 275.8 ms                      | 7.05 s                            |
| Racy program, release mode          | DNF     | DNF                           | DNF                               |
| Program with mutex, release mode    | 44.5 ms | 714 ms                        | 156 ms                            |
| Program with RW lock, release mode  | 4.3 s   | 4.9   s                       | 4.2 s                             |

How the benchmarks were done:

- 'Release mode' means `clang -O2`.
- My machine is a macOS M4 Pro.
- The RW lock program has one writer thread and one reader thread to compare apples to apples.
- For the DTrace column, the time is measured using `timestamp` in the `BEGIN` and `END` probes.
- For the other columns, `hyperfine './cmd' --shell=none --warmup=3 -i` is used.
- We do not care about the exact values, only about the slowdown ratio between the first column (without race detector) and the other columns (with race detector).


Commentary:

- When there are races, DTrace performs really badly because it reports all races it sees, which is however great for the DevUX. If we make DTrace also report timestamps and the call stack, the runtime goes to 7.5s.
- DTrace has many tunables so it's possible that we can make it much faster this way.
- The racy program in release mode never terminates because the compiler does whatever it wants in the presence of undefined behavior.
- In the absence of data races, DTrace performs really well compared to TSan, we only see a ~3-4x slowdown, compared to a 16x slowdown with TSan.
- RW lock performs horribly compared to the mutex version. I just profiled it real quick and saw that the benchmark is dominated by `pthread_rwlock_lock_slow`. I think we are simply in the worst case scenario for a RW lock where there is 1 reader and 1 writer, and a RW lock optimizes for the cases of N readers most of the time, and 1 writer coming in from time to time. A typical implementation does a simple atomic increment where there are only readers, which is very fast, and acquires a mutex lock when there is one writer in the mix.

## Conclusion

Thread Sanitizer does a lot more than what we have covered, because it understands all the synchronization primitives: condition variables, atomics, etc. It tracks 'happens before' relationships between threads that call these primitives. We do not do that in our crude D script, even though we perhaps could, with a good amount of post-processing to eliminate false positives.

Another major difference is that Thread Sanitizer is general purpose and tries to track all memory accesses, whereas our DTrace approach selectively tracks a few memory accesses.

Even though, I think this is already a pretty good approach, in the spirit of '20% of the work gets you 80% of the way there'. The main appeal is that it avoids having to rebuild the whole program with different flags which can be very time consuming, or sometimes not feasible at all.

Finally, remember that neither our DTrace approach nor Thread Sanitizer guarantee that *all* data races will be caught, since these are runtime detectors that only see the code paths actually taken when observing this particular run of the program, and also because they operate with limited amounts of memory: they cannot remember *all* memory accesses in the program, they only do a best effort to remember most of them. They do not prove the absence of bugs, only their presence.

In fact, while writing this article and the accompanying test programs, Thread Sanitizer very rarely flagged the glaring data races. That was motivating, as well as terrifying. 


My recommendation would still be to use your programming language or platform recommended race detector if possible. DTrace is a good fallback if that's no feasible or if you want to avoid recompiling.

## Addendum: The full code



<details>
  <summary>The D script</summary>

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
  // Record which kind of access each function does.
  func_access["byte_array_get_len"] = AccessKindRead;
  func_access["byte_array_set_len"] = AccessKindWrite;
}

pid$target::byte_array_?et_len:entry {
  // `arg0` is the pointer to the `ByteArray` structure,
  // and `len` is at offset `8` in the `ByteArray` structure.
  // So this gets the value of `byte_array->len`.
  self->mem_ptr = arg0 + 8;
  this->theirs = accesses[self->mem_ptr];
  this->my_access_kind = func_access[probefunc];
  this->now = timestamp;

  if (this->theirs.tid !=0 &&  // 'if a thread is concurrently accessing the same memory...'
      this->theirs.tid != tid &&  // 'and this is another thread as the current one...'
      (this->my_access_kind == AccessKindWrite || this->theirs.kind == AccessKindWrite)) { // 'and at least one access is a write...'
    printf("possible data race: my_access_kind:%d my_tid=%d my_ts=%d their_access_kind:%d their_tid=%d their_ts=%d mem_ptr=%p\n", this->my_access_kind, tid, this->now, this->theirs.kind, this->theirs.tid, this->theirs.ts, self->mem_ptr);
    ustack();
  }

  // Update the map with the current access.
  accesses[self->mem_ptr].kind = this->my_access_kind;
  accesses[self->mem_ptr].tid = tid;
  accesses[self->mem_ptr].ts = this->now;
}

pid$target::byte_array_?et_len:return /self->mem_ptr != 0/ {
  // Clear the map since the access is done.
  accesses[self->mem_ptr].kind = 0;
  accesses[self->mem_ptr].tid = 0;
  accesses[self->mem_ptr].ts = 0;
  self->mem_ptr = 0;
}
```
</details>

<details>
  <summary>The racy C program</summary>

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

</details>


<details>
  <summary>The C program fixed with a mutex</summary>

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
  pthread_mutex_t mtx;
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
  pthread_mutex_lock(&b->mtx);

  const size_t len = byte_array_get_len(b);
  byte_array_ensure_cap_at_least(b, len + 1);
  b->data[len] = elem;

  byte_array_set_len(b, len + 1);
  pthread_mutex_unlock(&b->mtx);
}

void *async_push(void *any) {
  ByteArray *b = any;

  for (size_t i = 0; i < DATA_LEN; i++) {
    byte_array_push(b, (uint8_t)i);
  }

  return NULL;
}

int main() {
  ByteArray byte_array = {.mtx = PTHREAD_MUTEX_INITIALIZER};

  pthread_t thread_push = {0};
  pthread_create(&thread_push, NULL, async_push, &byte_array);

  for (;;) {
    pthread_mutex_lock(&byte_array.mtx);
    const size_t len = byte_array_get_len(&byte_array);
    pthread_mutex_unlock(&byte_array.mtx);
    if (len == DATA_LEN) {
      break;
    }
  }

  assert(byte_array_get_len(&byte_array) == DATA_LEN);
}
```

</details>


<details>
  <summary>The C program fixed with a RW lock</summary>

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
  pthread_rwlock_t rw_lock;
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
  pthread_rwlock_wrlock(&b->rw_lock);

  const size_t len = byte_array_get_len(b);
  byte_array_ensure_cap_at_least(b, len + 1);
  b->data[len] = elem;

  byte_array_set_len(b, len + 1);

  pthread_rwlock_unlock(&b->rw_lock);
}

void *async_push(void *any) {
  ByteArray *b = any;

  for (size_t i = 0; i < DATA_LEN; i++) {
    byte_array_push(b, (uint8_t)i);
  }

  return NULL;
}

void *async_read(void *any) {
  ByteArray *b = any;

  for (;;) {
    pthread_rwlock_rdlock(&b->rw_lock);
    const size_t len = byte_array_get_len(b);
    pthread_rwlock_unlock(&b->rw_lock);
    if (len == DATA_LEN) {
      break;
    }
  }
  return NULL;
}

int main() {
  ByteArray byte_array = {.rw_lock = PTHREAD_RWLOCK_INITIALIZER};

  pthread_t thread_push = {0};
  pthread_create(&thread_push, NULL, async_push, &byte_array);

  pthread_t thread_read = {0};
  pthread_create(&thread_read, NULL, async_read, &byte_array);

  for (;;) {
    pthread_rwlock_rdlock(&byte_array.rw_lock);
    const size_t len = byte_array_get_len(&byte_array);
    pthread_rwlock_unlock(&byte_array.rw_lock);
    if (len == DATA_LEN) {
      break;
    }
  }

  assert(byte_array_get_len(&byte_array) == DATA_LEN);
}
```

</details>
