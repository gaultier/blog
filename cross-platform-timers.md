Title: The missing API for cross-platform timers
Tags: Unix, Signals, C, Linux, FreeBSD, illumos, MacOS, Windows
---

Most serious programs will need to trigger some action at a delayed point in time, often repeateadly: set timeouts, clean up temporary files or entries in the database, send keep-alives, etc. All while doing some work in the meantime. A blocking `sleep` won't cut it! For example, Javascript has `setTimeout`. But how does it work under the hood? How does each OS handle that?

Lately I found myself in need of doing just that, repeatedly sending a keep-alive to a remote peer in some network protocol, in C. And I wanted to do it in a cross-platform way. And to my surprise, I could not find a (sane) libc function or a syscall to do so, that is the same on all Unices! 

Each had their own weird way to do it, as I discovered! I am used to Windows being the odd kid in its corner doing its thing, but usually, Unices agree on a simple API to do something. There's the elephant in the room, of course, with epoll/kqueue/event ports...Which is such a pain that numerous libraries have sprung up to paper over the differences and offer The Once API To Rule Them All: libuv, libev, libevent, etc.

Well, let's take a tour of all the OS APIs to handle timers.

## Windows

This will be brief because I do not develop on Windows. The official documentation mentions the `SetTimer` function from Win32 and you pass it a timeout and a callback. Super simple.

## POSIX

POSIX has one API for timers, and it sucks. A timer is created with `timer_create`, which does initially nothing, and the timer is started with a timeout using `timer_settime`. When the timeout is reached, a signal is sent to the program. And that's the problematic part. Signals are *very* problematic, as seen in my [previous article](/blog/way_too_many_ways_to_wait_for_a_child_process_with_a_timeout.html):

- They do not compose with any other OS primitive
- They behave confusingly with child processes
- It's hard to write complex programs with signals in mind due to their global nature. Code of our own or in a library we use could block some signals for some period of time, unbeknownst to us.
- Most functions are not async-signal-safe and should not be used from within a signal handler but no compiler warns about that and most example code is wrong. This is exarcerbated by the fact that a given function may be async-signal safe on some OS but not on another. Or for some version of this OS but not for another version. This has caused really security vulnerabilities in the past.

I'll just quote here the Linux man page for [timer_create](https://www.man7.org/linux/man-pages/man2/timer_create.2.html):

```
 /* Note: calling printf() from a signal handler is not safe
  (and should not be done in production programs), since
  printf() is not async-signal-safe; see signal-safety(7).
  Nevertheless, we use printf() here [...]. */
```

Enough said.


And this is really tricky to get right. For example, `malloc` is not async-signal-safe. Well, you think, let's just remember to not use it in signal handlers! Done! Feeling confident, we happen to call `qsort` in our signal handler. Should be fine, right? We just sort some data... Well, we just introduced a security vulnerability!

That's because in glibc, the innocent looking `qsort` calls `malloc` under the hood! (And that was, in the past, the cause of `qsort` segfaulting, which I find hilarious). To quote https://www.qualys.com/2024/01/30/qsort.txt :

> to our great surprise, we discovered
that the glibc's qsort() is not, in fact, a quick sort by default, but a
merge sort (in stdlib/msort.c).
[...]
But merge sort suffers from one
major drawback: it does not sort in-place -- it malloc()ates a copy of
the array of elements to be sorted

So...let's accept that writing signal handlers is not for doable for us mere humans. Many people have concluded the same in the past and have created better APIs that do not involve timers at all. Let's look into that.


