Title: The missing API for cross-platform timers
Tags: Unix, Signals, C, Linux, FreeBSD, Illumos, MacOS, Windows, OpenBSD, NetBSD, Timers
---

Most serious programs will need to trigger some action at a delayed point in time, often repeatedly: set timeouts, clean up temporary files or entries in the database, send keep-alives, garbage-collect unused entities, etc. All while doing some work in the meantime. A blocking `sleep` won't cut it! For example, JavaScript has `setTimeout`. But how does it work under the hood? How does each OS handle that?

Lately I found myself in need of doing just that, repeatedly sending a keep-alive to a remote peer in some network protocol, in C. And I wanted to do it in a cross-platform way. And to my surprise, I could not find a (sane) libc function or a syscall to do so, that is the same on all Unices! 

Each had their own weird way to do it, as I discovered. I am used to Windows being the odd kid in its corner doing its thing, but usually, Unices (thanks to POSIX) agree on a simple API to do something. There's the elephant in the room, of course, with epoll/kqueue/event ports...Which is such a pain that numerous libraries have sprung up to paper over the differences and offer The Once API To Rule Them All: libuv, libev, libevent, etc. So, are timers the same painful ordeal?

Well, let's take a tour of all the OS APIs to handle them.

## Windows: SetTimer

This will be brief because I do not develop on Windows. The official documentation mentions the `SetTimer` function from Win32 and you pass it a timeout and a callback. Alternatively, since Windows applications come with a built-in event queue, an event of type `WM_TIMER` gets emitted and can be consumed from the queue. Simple, and it composes with other OS events, I like it.

## POSIX: timer_create, timer_settime

POSIX has one API for timers, and it sucks. A timer is created with `timer_create`, which does initially nothing, and the timer is started with a timeout using `timer_settime`. When the timeout is reached, a signal is sent to the program. And that's the issue. Signals are *very* problematic, as seen in my [previous article](/blog/way_too_many_ways_to_wait_for_a_child_process_with_a_timeout.html):

- They do not compose with other OS primitives. This forces numerous syscalls to have a normal version and a signal-aware version that can block some signals for its duration: `poll/ppoll`, `select/pselect`, etc.
- They are affected by the signal mask of the parent (e.g.: the shell, the service runner, etc)
- They behave confusingly with child processes. Normally, a signal mask is inherited by the child. But some signal-triggering APIs (e.g.: `timer_settime`)  explicitly prevent child processes from inheriting their signals. I guess we'll have to read the fine prints in the man page!
- It's hard to write complex programs with signals in mind due to their global nature. Code of our own, or in a library we use, could block some signals for some period of time, unbeknownst to us. Or simply modify the signal mask of the process, so we can never assume that the signal mask has a given value.
- Most functions are not async-signal-safe and should not be used from within a signal handler but no compiler warns about that and most example code is wrong. This is exacerbated by the fact that a given function may be async-signal safe on some OS but not on another. Or for some version of this OS but not for another version. This has caused real [security vulnerabilities](https://www.qualys.com/2024/07/01/cve-2024-6387/regresshion.txt) in the past.

I'll just quote here the Linux man page for [timer_create](https://www.man7.org/linux/man-pages/man2/timer_create.2.html):

```
 /* Note: calling printf() from a signal handler is not safe
  (and should not be done in production programs), since
  printf() is not async-signal-safe; see signal-safety(7).
  Nevertheless, we use printf() here [...]. */
```

Enough said.


And this is really tricky to get right. For example, `malloc` is not async-signal-safe. Well, you think, let's just remember to not use it in signal handlers! Done! Feeling confident, we happen to call `qsort` in our signal handler. Should be fine, right? We just sort some data... Well, we just introduced a security vulnerability!

That's because in glibc, the innocent looking `qsort` [calls](https://www.qualys.com/2024/01/30/qsort.txt) `malloc` under the hood! (And that was, in the past, the cause of `qsort` segfaulting, which I find hilarious): 

> to our great surprise, we discovered
> that the glibc's qsort() is not, in fact, a quick sort by default, but a
> merge sort (in stdlib/msort.c).
> [...]
> But merge sort suffers from one
> major drawback: it does not sort in-place -- it malloc()ates a copy of
> the array of elements to be sorted.

So...let's accept that writing signal handlers correctly is not feasible for us mere humans. Many people have concluded the same in the past and have created better OS APIs that do not involve timers at all. Let's look into that.

## Linux: timerfd_create, timerfd_settime

So, we all heard the saying: In Unix, everything is a file. So, what if a timer was also a file (descriptor)? And we could ask the OS to notify our program whenever there is data to read (i.e.: when our timer triggers), like with a file or a socket? 
That's the whole idea behind `timerfd_create` and `timerfd_settime`. We create a timer, we get a file descriptor back.

In the previous article, we saw that Linux added similar APIs for signals with `signalfd` and processes with `pidfd_open`, so there is a consistent effort to indeed make everything a file. 

That means that using the venerable `poll(2)`, we can wait on an array of very diverse things: sockets, files, signals, timers, processes, pipes, etc. This is great! That's simple (one API for all OS entities) and composable (handling an additional OS entity does not force our program to undergo big changes, and we can wait on diverse OS entities using the same API).

The only gotcha, which is mentioned by the man page, is that we need to remember to `read(2)` from the timer whenever it triggers. That only matters for repeating timers (also sometimes called interval timers).

However, it's unfortunate that this is a Linux-only API...or is it really?

- FreeBSD has it [too](https://man.freebsd.org/cgi/man.cgi?query=timerfd&sektion=2&format=html):
    > The timerfd facility was	originally ported to FreeBSD's Linux  compati-
    > bility  layer  [...] in FreeBSD 12.0.
    > It  was	revised	 and  adapted  to   be	 native	  [...] in FreeBSD 14.0.
- Illumos has it [too](https://smartos.org/man/3c/timerfd_create).
- NetBSD has it [too](https://man.netbsd.org/timerfd_create.2):
    > The timerfd interface first appeared in NetBSD 10.  It is compatible with
    > the timerfd interface that appeared in Linux 2.6.25.
- OpenBSD does not seem to have it.
- macOS does not seem to have it.

So, pretty good, but not ubiquitous. The search continues.

## BSD: kqueue/kevent

`kqueue` might be my favorite OS API: it can watch any OS entity for changes with just one call. Even timers! As it is often the case for BSD-borne APIs, they are well designed and well documented. The man page says:

> EVFILT_TIMER   Establishes an arbitrary timer identified by ident. 

That's great, we do not even have to use various APIs to  create the timer, set the time, read from the timer, etc.

What about the portability?

- FreeBSD has it
- NetBSD has it
- OpenBSD has it
- macOS does not have it

The last point is disappointing. macOS has kqueue due to its BSD heritage. But the [man page](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html) explicitly states that this particular feature is not supported:

> EVFILT_TIMER   This filter is currently unsupported.

Disappointing!

## Illumos: port_create

So, Illumos (in)famously has its own API for multiplexing events from disjoint sources, that is different from `kqueue`, and some Illumos developers have publicly stated they now wished they had adopted `kqueue` back in the day.

Anyways, similarly to kqueue, their API (`port_create`) also supports timers! From the [man page](https://illumos.org/man/3C/port_create):

> PORT_SOURCE_TIMER events represent one or more timer expirations for a
> given timer.  A timer is associated with a port by specifying SIGEV_PORT
> as its notification mechanism.

Interestingly, the timer is created using the POSIX API that normally triggers a signal upon timer expiration, but thanks to `port_create`, the signal is instead turned into an event ports notification, as if it was a file descriptor. I think it's pretty clever, because that means that historical code creating timers need not be modified. In other words, it makes the POSIX API sane by circumventing signals and integrating it into a more modern facility to make it composable with other OS entities.

## macOS: dispatch_source_create

Apple developers, in their infinite wisdom, decided to support `kqueue` but not kqueue timers, and invented their own thing instead.

It's called [dispatch_source_create](https://man.archlinux.org/man/dispatch_source_create.3.en) and it supports timers with `DISPATCH_SOURCE_TYPE_TIMER`. 

I do not currently have access to an Apple computer so I have not tried it. All I know is that [Grand Central Dispatch/libdispatch](https://en.wikipedia.org/wiki/Grand_Central_Dispatch) is an effort to have applications have an event queue and thread pool managed for them by the OS. It's more of a task system, actually. All of this seems to me somewhat redundant with `kqueue` (which, on Apple platforms, came first!), but I am not an Apple engineer. 

`libdispatch` has technically been ported to many platforms but I suppose this is just like `libkqueue` on Linux: it exposes the familiar API, but under the hood, it translates all calls to the OS-specific API, so for all intents and purposes, this syscall is macOS specific (well, and iOS, tvOS, IpadOS, etc, but let's group them all into a 'macOS' bucket).

## Linux: io_uring

`io_uring` is a fascinating Linux-only approach to essentially make every blocking system call... non-blocking. A syscall is enqueued into a ring buffer shared between userspace and the kernel, as a 'request', and at some point in time, a 'response' is enqueued by the kernel into a separate ring buffer that our program can read. It's simple, it's composable, it's great. 

At the beginning I said that a blocking 'sleep' was not enough, because our program cannot do any work while sleeping. `io_uring` renders this moot: we can enqueue a sleep, do some work, for example enqueue other syscalls, and whenever our sleep finishes, we can dequeue it from the second ring buffer, and voila: we just implemented a timer.

It's so simple it's brilliant! Sadly, it's Linux only, only for recent-ish kernels, and some cloud providers disable this facility.

## All OSes: timers fully implemented in userspace

Frustrated by my research, not having found one sane API that exists on all Unices, I wondered: How does `libuv`, the C library powering all of the asynchronous I/O for NodeJS, do it? I knew they support [timers](https://docs.libuv.org/en/v1.x/timer.html). And they support all OSes, even the most obscure ones like AIX. Surely, they have found the best OS API!

Let's make a super simple C program using libuv timers (loosely adapted from their test suite):

```c
#include <assert.h>
#include <uv.h>

static void once_cb(uv_timer_t *handle) {
  printf("timer %#x triggered\n", handle);
}

int main() {
  uv_timer_t once_timers[10] = {0};
  int r = 0;

  /* Start 10 timers. */
  for (int i = 0; i < 10; i++) {
    r = uv_timer_init(uv_default_loop(), &once_timers[i]);
    assert(0 == r);
    r = uv_timer_start(&once_timers[i], once_cb, i * i * 50, 0);
    assert(0 == r);
  }

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}
```

We create 10 timers with increasing durations, and run the event loop. When a timer triggers, our callback is called by `libuv`.

Of course, in a real program, we would also do real work while the timers run, e.g. network I/O.

Let's compile our program and look at what syscalls are being done (here I am on Linux but we'll soon seen it does not matter at all):

```sh
$ cc uv-timers.c -luv
$ strace ./a.out
[...]
epoll_pwait(3, [], 1024, 49, NULL, 8)   = 0
write(1, "timer 0x27432398 triggered\n", 27timer 0x27432398 triggered
) = 27
epoll_pwait(3, [], 1024, 149, NULL, 8)  = 0
write(1, "timer 0x27432430 triggered\n", 27timer 0x27432430 triggered
) = 27
epoll_pwait(3, [], 1024, 249, NULL, 8)  = 0
write(1, "timer 0x274324c8 triggered\n", 27timer 0x274324c8 triggered
) = 27
epoll_pwait(3, [], 1024, 349, NULL, 8)  = 0
write(1, "timer 0x27432560 triggered\n", 27timer 0x27432560 triggered
) = 27
[...]
```

Huh, no call to `timerfd_create` or something like this, just... `epoll_pwait` which is basically just `epoll_wait`, which is basically just a faster `poll`. And no events, just a timeout... So... are `libuv` timers fully implemented in userspace?

I was at this moment reminded of a [sentence](https://smartos.org/man/7/timerfd) I had read from a Illumos man page (there is a surprisingly big overlap of people developing Illumos and `libuv`):

> timerfd is a Linux-borne facility for creating POSIX timers and receiving
> their subsequent events via a file descriptor.  The facility itself is
> arguably unnecessary: portable code can [...] use the timeout value
> present in poll(2) [...].

So, what `libuv` does is quite simple in fact:

When a timer is created, add it to a data structure (it's a min-heap, i.e. a binary tree that is easy to implement and is designed to get the smallest element in a set quickly).

A typical event loop tick first gets the current time from the OS. Then, it computes the timeout to pass to poll/epoll/kqueue/etc.  If there are no active timers, it's easy, there is no timeout (that means that the program will block indefinitely until some I/O happens).

If there are active timers, get the 'smallest' one, meaning: the first that would trigger. The OS timeout is thus `now - timer.value`. 
Whenever a timer expires, it is removed from the min-heap. Simple, (relatively) efficient. The only caveat is that `epoll` only offers a millisecond precision for the timeout parameter so that's also the precision of `libuv` timers.

This approach is reminiscent of this part from the [man page](https://www.man7.org/linux/man-pages/man2/select.2.html) of `select` (which is basically `poll` with more limitations):

>    Emulating usleep(3)
>    Before the advent of usleep(3), some code employed a call to
>    select() with all three sets empty, nfds zero, and a non-NULL
>    timeout as a fairly portable way to sleep with subsecond
>    precision.

That way, we can 'sleep' while we also do meaningful work, for example network I/O. If some I/O completes before a timer triggers, we'll get notified by the OS and we can react to it. Then, during the next event loop tick, we'll compute a shorter timeout than the first one (since some time elapsed).

If no I/O happens at all, the OS will wake us up when our timeout is elapsed.

In short, we have multiplexed multiple timers using one system call (and a min-heap to remember what timers are on-going and when will the next one trigger).

## Conclusion

Writing cross-platform C code typically means writing two code paths: one for Windows and one for Unix. But for multiplexed I/O, and for timers, each Unix has its own idea of what's the Right Way(tm). 

To sum up:

| OS API                 | Windows | macOS | Linux | FreeBSD | NetBSD | OpenBSD | Illumos |
|------------------------|---------|-------|-------|---------|--------|---------|---------|
| SetTimer               | ✓       |       |       |         |        |         |         |
| POSIX timers [^1]      |         | ✓     | ✓     | ✓       | ✓      | ✓       | ✓       |
| timerfd                |         |       | ✓     | ✓       | ✓      |         | ✓       |
| kevent timer           |         |       |       | ✓       | ✓      | ✓       |         |
| port_create timer      |         |       |       |         |        |         | ✓       |
| dispatch_source_create |         | ✓     |       |         |        |         |         |
| io_uring sleep [^2]    |         |       | ✓     |         |        |         |         |
| Userspace timers       | ✓       | ✓     | ✓     | ✓       | ✓      | ✓       | ✓       |

[^1]: Do not recommend using in non-trivial programs.
[^2]: Not always enabled.


For performant multiplexed I/O, that means that we have to have a code path for each OS (using `epoll` on Linux, `kqueue` on macOS and BSDs, event ports on Illumos, I/O completion ports on Windows). 

For timers, it seems that the easiest approach is to implement them fully in userspace, as long as we have an efficient data structure to keep them around.


 


