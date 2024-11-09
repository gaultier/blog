Title: What is the best way to wait on a process with timeout?
Tags: Unix, Signals, C, Linux, FreeBSD
---

I often need to launch a program in the terminal in a retry loop. Maybe because it's flaky, or because it tries to contact a remote service that is not available. A few scenarios:

- ssh to a starting machine
- `psql` to a (re)starting database
- Ensuring that a network service started fine with netcat

It's a common problem, so much so that there are two utilities that I usually reach for: 

- [timeout](https://www.gnu.org/software/coreutils/manual/html_node/timeout-invocation.html) from GNU coreutils, which launches a command with a timeout (useful if the command itself does not have a `--timeout` option)
- [eb](https://github.com/rye/eb) which runs a command with a certain number of times with an exponential backoff. That's useful to avoid hammering a server with connection attempts for example.

This will all sound familiar to people who develop distributed systems: they have long known that this is [best practice](https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/) to retry an operation:

- With a timeout
- A bounded number of times e.g. 10
- With a waiting time between each retry, either a constant one or a increasing one e.g. with exponential backoff
- With jitter (although this point also seemed the least important since most of us use non real-time operating systems which introduce some jitter anytime we sleep or wait on something with a timeout)

This is best practice in distributed systems, and we often need to do the same on the command line. But the two aforementioned tools only do one or two of the above points. 


So let's implement our own! As we'll see, it's much less straightforward, and thus more interesting, than I thought. It's a whirlwind tour through Unix deeps.

## What are we working toward?

```sh
$ TODO
```

So how do we implement it?

Immediately, we notice something: even though there are a bazillion ways to wait on a child process to finish (`wait`, `wait3`, `wait4`, `waitid`, `waitpid`), none of them take a timeout as an argument. This has sparked numerous questions online ([1](https://stackoverflow.com/questions/18542089/how-to-wait-on-child-process-to-finish-with-time-limit), [2](https://stackoverflow.com/questions/18476138/is-there-a-version-of-the-wait-system-call-that-sets-a-timeout)). So let's explore a few different ways to achieve this on Unix.

# The old-school way: sigsuspend

That's how `timeout` from coreutils implements it. This is quite simple on paper:

1. We opt-in to receive a `SIGCHLD` signal when the child processes finishes with: `signal(SIGCHLD, on_chld_signal)` where `on_chld_signal` is a function pointer we provide. Even if does not do anything in this case.
2. We schedule a `SIGALARM` signal with `alarm` or more preferrably `setitimer` which can take a duration in microseconds whereas `alarm` can only handle seconds
3. We wait for either signal with `sigsuspend` which suspends the program until a given set of signals arrive
4. We should not forget to `wait` on the child process to avoid leaving zombie processes behind

A barebone implementation:

```c
TODO
```


Now, I don't *love* this approach:

- I find signals hard. It's basically a global `goto` to a completely different location
- A sigal handler is forced to use global mutable state, which is better avoided if possible
- Lots of functions are not 'signal-safe', and that has led to security vulnerabilities in the past e.g. in [ssh](TODO). In short, non-atomic operations are not signal safe because they might be suspended in the middle, thus leaving an inconsistent state behind. Thus, we have to read documentation very carefully to ensure that we only call signal safe functions in our signal handler, and cherry on the cake, that varies from platform to platform.
- Signals do not compose well with other Unix entities such as file descriptors and sockets. For example, we cannot `poll` them. This led to Linux introducting `signalfd` to get a file descriptor out of a set of signals so that we can now use all the usual functions. However that is Linux specific, for example FreeBSD does not implement it.
- Different signals have different default behaviors, and this gets inherited in child processes, so you cannot assume anything in your program and have to be very defensive. Who knows what the parent process, e.g. the shell, set as the signal mask? If you read through the whole implementation of the `timeout` program, a lot of the code is dedicated to setting signal masks in the parent, forking, immediately changing the signal mask in the child and the parent, etc. Now, I believe modern Unices offer more control than `fork()` about what signal mask the child should be created with, so maybe it got better. Still, it's a lot of stuff to know.
- They are many libc functions and system calls relating to signals and that's a lot to learn. A non-exhaustive list e.g. on Linux: `kill(1), alarm(2), kill(2), pause(2), sigaction(2), signalfd(2),  sigpending(2),  sigprocmask(2),   sigsuspend(2),  bsd_signal(3),  killpg(3),  raise(3),  siginterrupt(3), sigqueue(3), sigsetops(3), sigvec(3), sysv_signal(3), signal(7)`. Oh wait, I forgot `sigemptyset(3)` and  `sigaddset(3)`. And I'm sure I forgot about a few!

So, let's stick with signals for a bit but simplify our current approach.

## Second way: sigtimedwait

Wouldn't it be great if we could wait on a signal, say, `SIGCHLD`, with a timeout? Oh look, a system call that does exactly that *and* is standardized by POSIX. Cool! I am not quite sure why the `timeout` program does not use it, but we sure as hell can. My only guess would be that it wants to support old Unices that did not have this system call. 

Anyways, here's a very straightforward implementation:

```c
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

void on_sigchld(int sig) { (void)sig; }

int main(int argc, char *argv[]) {
  (void)argc;
  signal(SIGCHLD, on_sigchld);

  uint32_t sleep_ms = 128;

  for (int retry = 0; retry < 10; retry += 1) {
    int child_pid = fork();
    if (-1 == child_pid) {
      return errno;
    }

    if (0 == child_pid) { // Child
      argv += 1;
      if (-1 == execvp(argv[0], argv)) {
        return errno;
      }
      __builtin_unreachable();
    }

    sigset_t sigset = {0};
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);

    siginfo_t siginfo = {0};

    struct timespec timeout = {
        .tv_sec = sleep_ms / 1000,
        .tv_nsec = (sleep_ms % 1000) * 1000 * 1000,
    };

    int sig = sigtimedwait(&sigset, &siginfo, &timeout);
    if (-1 == sig && EAGAIN != errno) { // Error
      return errno;
    }
    if (-1 != sig) { // Child finished.
      if (WIFEXITED(siginfo.si_status) && 0 == WEXITSTATUS(siginfo.si_status)) {
        return 0;
      }
    }

    if (-1 == kill(child_pid, SIGKILL)) {
      return errno;
    }

    if (-1 == wait(NULL)) {
      return errno;
    }

    sleep_ms *= 2;
    usleep(sleep_ms * 1000);
  }
  return 1;
}
```

I like this implementation. It's pretty easy to convince ourselves looking at the code that it is obviously correct, and that's a very important factor for me.

We still have to deal with signals though. Could we reduce their imprint on our code?

## Third approach: Self pipe trick

This is a really nifty, quite well known trick at this point, where we bridge the world of signals with the world of file descriptors with the `pipe(2)` system call. 

Usually, pipes are a form of inter-process communication, and here we do not want to communicate with the child process (since it could be any program, and most programs do not get chatty with their parent process). What we do is: in the signal handler for `SIGCHLD`, we simply write (anything) to our own pipe. We know this is signal-safe so it's good. 

And you know what's cool with pipes? They are simply a file descriptor which we can `poll`. With a timeout. Nice! Here goes:

```c
TODO
```

So we still have one signal handler but the rest of our program does not deal with programs in any way (well, except to kill the child when the timeout triggers, but that's invisible). That's better. Now, wouldn't it be nice if we could avoid signals *entirely*?

### Fourth approach: process descriptors

*Recommended reading about this topic: [1](https://lwn.net/Articles/801319/) and [2](https://lwn.net/Articles/794707/).*

In the recent years, people realized that process identifiers (`pid`s) have a number of problems:

- PIDs are recycled and the space is small, so collisions will happen. Typically, a process spawns a child process, some work happens, and then the parent decides to send a signal to the pid of the child. But it turns out that the child already terminated (unbeknownst to the parent) and another process took its place with the same PID. So now the parent is sending signals, or communicating with, a process that it thinks is its original child but is in fact something completely different. Chaos and security issues ensue. Now, in our very simple case, that would not really happen, and additionally, a process can only kill its children. But imagine that you are implementing the init process with PID 1, e.g. systemd: every single process is your child! Or think of the case of reparenting a process. Or sending a certain PID to another process. It becomes hairy and it's a very real problem.
- Data races are hard to escape (see the previous point)

And they have worked hard to introduce a better concept: process descriptors, which are (almost) bog-standard file descriptors, like files or sockets. After all, that's what sparked our whole investigation: we wanted to use `poll` and it did not work on a pid. Pids and signals do not compose well, but file descriptors do. Also, just like file descriptors, process descriptors are per-process. If I open a file with `open()` and get the file descriptor `3`, it is scoped to my process. Another process can `close(3)` and it will refer to their own file descriptotr, and not affect my file descriptor. That's great, we get isolation, so bugs in our code do not affect other processes.

So, Linux and FreeBSD have introduced the same concepts but with slightly different APIs (unfortunately):

- A child process can be created with `clone3(..., CLONE_PIDFD)` or `pdfork()` which returns a process descriptor which is almost like a normal file descriptor. On Linux, a process descriptor can also be obtained from a pid with `pidfd_open(pid)` e.g. if a normal `fork` was done.
- We wait on the process descriptor with `poll(..., timeout)` (or `select`, or `epoll`, etc)
- We kill the child process using the process descriptor with `pidfd_send_signal` (Linux) or `close` (FreeBSD) or `pdkill` (FreeBSD)
- We wait on the zombie child process again using the process descriptor to get its exit status

And voila, no signals! Isolation! Composability! Life can be nice like this sometimes. It's just unfortunate that there isn't a cross-platform API for that.

Here's the Linux implementation:

```c
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t sleep_ms = 128;

  for (int retry = 0; retry < 10; retry += 1) {
    int child_pid = fork();
    if (-1 == child_pid) {
      return errno;
    }

    if (0 == child_pid) { // Child
      argv += 1;
      if (-1 == execvp(argv[0], argv)) {
        return errno;
      }
      __builtin_unreachable();
    }

    // Parent.

    int child_fd = (int)syscall(SYS_pidfd_open, child_pid, 0);
    if (-1 == child_fd) {
      return errno;
    }

    struct pollfd poll_fd = {
        .fd = child_fd,
        .events = POLLHUP | POLLIN,
    };
    // Wait for the child to finish with a timeout.
    if (-1 == poll(&poll_fd, 1, 2000)) {
      return errno;
    }

    // Maybe kill the child (the child might have terminated by itself even if
    // poll(2) timed-out).
    if (-1 == syscall(SYS_pidfd_send_signal, child_fd, SIGKILL, NULL, 0)) {
      return errno;
    }

    siginfo_t siginfo = {0};
    // Get exit status of child & reap zombie.
    if (-1 == waitid(P_PIDFD, (id_t)child_fd, &siginfo, WEXITED)) {
      return errno;
    }

    if (WIFEXITED(siginfo.si_status) && 0 == WEXITSTATUS(siginfo.si_status)) {
      return 0;
    }

    sleep_ms *= 2;
    usleep(sleep_ms * 1000);

    close(child_fd);
  }
}
```


## Fifth approach: BSD's kqueue

TODO

## Sixth approach: Linux's io_uring

TODO

## Conclusion

I find signals and spawning child process to be the hardest parts of Unix. Evidently this is not a rare opinion, looking at the development in these areas: process descriptors, the various expansions to the venerable `fork` with `vfork`, `clone`, `clone3`, `clone6`, etc. 

So what's the best approach then in a complex program? Let's recap:

- If you need maximum portability and are not afraid of signals and their pitfalls, use `sigsuspend`
- If you favor correctness and work with recent Linux and FreeBSD versions, use process descriptors with shims to get the same API on all OSes. That's probably my favorite option.
- If you only care about BSDs (or accept to use `libkqueue` on Linux), use `kqueue` because it works out of the box with PIDs, you avoid signals completely, and it's used in all the big libraries out of there e.g. `libuv`
- If you only care about Linux and are already using `io_uring`, use `io_uring`

I often look at complex code and think: what are the chances that this is correct? What are the chances that I missed something? Is there a way to make it simplistic that it is obviously correct? And how can I limit the blast of a bug I wrote? 

Process descriptors or `kqueue` seem to me so straightforward, so obviously correct, that I would definitely favour them over signals. 


