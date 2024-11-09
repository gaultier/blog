Title: What is the best way to wait on a process with timeout?
Tags: Unix, Signals, C, Linux, FreeBSD
---

I often need to launch a program in the terminal in a retry loop. Maybe because it's flaky, or because it tries to contact a remote service that is not available. Think `ssh` on a machine that's booting up, or running end-to-end tests where the server is in a container that's starting up, for example in CI.

It's a common problem, so much so that there are two utilities that I usually reach for: 

- [timeout](https://www.gnu.org/software/coreutils/manual/html_node/timeout-invocation.html) from GNU coreutils, which launches a command with a timeout (useful if the command itself does not have a `--timeout` option)
- [eb](https://github.com/rye/eb) which runs a command with a certain number of times with an exponential backoff. That's useful to avoid hammering a server with connection attempts for example.

This will all sound familiar to people who develop distributed systems: they have long known that this is [best practice](https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/) to retry an operation:

- With a timeout
- A bounded number of times e.g. 10
- With a waiting time between each retry, either a constant one or a increasing one e.g. with exponential backoff
- With jitter (although this point also seemed the least important since most of us use non real-time operating systems which introduce some jitter anytime we sleep or wait on something with a timeout)

This is best practice in distributed systems, and we often need to do the same on the command line. But the two aforementioned tools only do one or two of the above points. 


So let's implement our own! As we'll see, it's much less straightforward, and thus more interesting, than I thought.

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
