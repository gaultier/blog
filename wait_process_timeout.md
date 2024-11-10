Title: What is the best way to wait on a process with timeout?
Tags: Unix, Signals, C, Linux, FreeBSD
---

I often need to launch a program in the terminal in a retry loop. Maybe because it's flaky, or because it tries to contact a remote service that is not available. A few scenarios:

- ssh to a (re)starting machine
- `psql` to a (re)starting database
- Ensuring that a network service started fine with netcat
- Filesystem commands over NFS

It's a common problem, so much so that there are two utilities that I usually reach for: 

- [timeout](https://www.gnu.org/software/coreutils/manual/html_node/timeout-invocation.html) from GNU coreutils, which launches a command with a timeout (useful if the command itself does not have a `--timeout` option)
- [eb](https://github.com/rye/eb) which runs a command with a certain number of times with an exponential backoff. That's useful to avoid hammering a server with connection attempts for example.

This will all sound familiar to people who develop distributed systems: they have long known that this is [best practice](https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/) to retry an operation:

- With a timeout (either constant or adaptative)
- A bounded number of times e.g. 10
- With a waiting time between each retry, either a constant one or a increasing one e.g. with exponential backoff
- With jitter, although this point also seemed the least important since most of us use non real-time operating systems which introduce some jitter anytime we sleep or wait on something with a timeout. The AWS article makes a point that in highly contended systems, the jitter parameter is very important, but for the scope of this article I'll leave it out.


This is best practice in distributed systems, and we often need to do the same on the command line. But the two aforementioned tools only parts of the above points:

- `timeout` does not retry
- `eb` does not have a timeout


So let's implement our own that does both! As we'll see, it's much less straightforward, and thus more interesting, than I thought. It's a whirlwind tour through Unix deeps.

## What are we building?

I call the tool we are building `ueb` for: micro exponential backoff. It does up to 10 retries, with a waiting period in between that starts at an arbitrary 128 ms and doubles every retry. The timeout for the subprocess is the same as the sleep time, so that it's adaptative and we give the subprocess a longer and longer time to finish successfully. These numbers would probably be exposed as command line options in a real polished program, but there's no time, what have to demo it:

```sh
# This returns immediately since it succeeds on the first try.
$ ueb true

# This retries 10 times since the command always fails, waiting more and more time between each try, and finally returns the last exit code of the command (1).
$ ueb false

# This retries a few times (~ 4 times), until the waiting time exceeds the duration of the sub-program. It exits with `0` since from the POV of our program, the sub-program finally finished in its alloted time.
$ ueb sleep 1

# Some more practical examples.
$ ueb ssh <some_ip>
$ ueb createdb my_great_database
```

If you want to monitor the retries and the sleeps, you can use `strace` or `dtrace`, but that is very variable on which system call is in use (which is implementation, OS, and architecture dependent). Example:

```sh
$ strace -e rt_sigtimedwait,clock_nanosleep ueb sleep 1
```


Note that the sub-command should be idempotent, otherwise we might create a given resource twice, or the command might have succeeded right after our timeout triggered but also right before we killed it, so our program thinks it timed out and thus need to be retried. There is this small data race window, which is completely fine if the command is idempotent but will erroneously retry the command to the bitter end otherwise. There is also the case where the sub-command does stuff over the network for example creating a resource, it succeeds, but the ACK is never received due to network issues. The sub-command will think it failed and retry. Again, fairly standard stuff in distributed systems but I thought it was worth mentioning.

So how do we implement it?

Immediately, we notice something: even though there are a bazillion ways to wait on a child process to finish (`wait`, `wait3`, `wait4`, `waitid`, `waitpid`), none of them take a timeout as an argument. This has sparked numerous questions online ([1](https://stackoverflow.com/questions/18542089/how-to-wait-on-child-process-to-finish-with-time-limit), [2](https://stackoverflow.com/questions/18476138/is-there-a-version-of-the-wait-system-call-that-sets-a-timeout)). So let's explore a few different ways to achieve this on Unix.

We'd like the pseudo-code to be something like:

```
wait_ms := 128

for retry in 0..<10:
    child_pid := run_command_in_subprocess(cmd)

    ret := wait_for_process_to_finish_with_timeout_ms(child_pid, wait_ms)
    if (did_process_finish_successfully(ret)):
        exit(0)
        
    // In case of a timeout, we need to kill the child process and retry.
    kill(child_pid, SIGKILL)

    // Reap zombie process to avoid a resource leak.
    waitpid(child_pid)

    sleep_ms(wait_ms);

    wait_ms *= 2;

// All retries exhausted, exit with an error code.
exit(1)
```

*There is a degenerate case where the give command to run is wrong (e.g. typo in the parameters) or the executable does not exist, and our program will happily retry it to the bitter end. But there is solace: this is bounded by the number of retries (10). That's why we do not retry forever.*

# First way: old-school sigsuspend

That's how `timeout` from coreutils implements it. This is quite simple on paper:

1. We opt-in to receive a `SIGCHLD` signal when the child processes finishes with: `signal(SIGCHLD, on_chld_signal)` where `on_chld_signal` is a function pointer we provide. Even if the signal handler does not do anything in this case.
2. We schedule a `SIGALARM` signal with `alarm` or more preferrably `setitimer` which can take a duration in microseconds whereas `alarm` can only handle seconds. There's also `timer_create/timer_settime` which handles nanoseconds. It depends what the OS and hardware support.
3. We wait for either signal with `sigsuspend` which suspends the program until a given set of signals arrive
4. We should not forget to `wait` on the child process to avoid leaving zombie processes behind

The reality is grimmer, looking through the `timeout` implementation:

- We could have inherited any signal mask from our parent so we need to explictly unblock the signals we are interested in
- Signals can be sent to a process group we need to handle that case
- We have to avoid entering a 'signal loop'
- Our process can be implicitly multithreaded due to some `timer_settime` implementations, therefore a `SIGALRM` signal sent to a process group, can be result in the signal being sent multiple times to a process (I am directly quoting the code comments from the `timeout` program here)
- When using `timer_create`, we need to take care of cleaning it up with `timer_delete`, lest we have a resource leak when retrying
- The signal handler may be called concurrently and we have to be aware of that
- Depending on the timer implementation we chose, we are susceptible to clock adjustments for example going back. E.g. `setitimer` only offers the `CLOCK_REALTIME` clock option for counting time, which is just the wall clock. We'd like something like `CLOCK_MONOTONIC` or `CLOCK_MONOTONIC_RAW` (the latter being Linux specific).


So... I don't *love* this approach:

- I find signals hard. It's basically a global `goto` to a completely different location
- A sigal handler is forced to use global mutable state, which is better avoided if possible, and it does not play nice with threads
- Lots of functions are not 'signal-safe', and that has led to security vulnerabilities in the past e.g. in [ssh](https://www.qualys.com/2024/07/01/cve-2024-6387/regresshion.txt). In short, non-atomic operations are not signal safe because they might be suspended in the middle, thus leaving an inconsistent state behind. Thus, we have to read documentation very carefully to ensure that we only call signal safe functions in our signal handler, and cherry on the cake, that varies from platform to platform.
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

  uint32_t wait_ms = 128;

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
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
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

    usleep(wait_ms * 1000);
    wait_ms *= 2;
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
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

static int pipe_fd[2] = {0};
void on_sigchld(int sig) {
  (void)sig;
  char dummy = 0;
  write(pipe_fd[1], &dummy, 1);
}

int main(int argc, char *argv[]) {
  (void)argc;

  if (-1 == pipe(pipe_fd)) {
    return errno;
  }

  signal(SIGCHLD, on_sigchld);

  uint32_t wait_ms = 128;

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

    struct pollfd poll_fd = {
        .fd = pipe_fd[0],
        .events = POLLIN,
    };
    // Wait for the child to finish with a timeout.
    int ret = poll(&poll_fd, 1, (int)wait_ms);
    if (-1 == ret && EINTR != errno) {
      return errno;
    }
    if (1 == ret) {
      char dummy = 0;
      read(pipe_fd[0], &dummy, 1);
      int status = 0;
      if (-1 == wait(&status)) {
        return errno;
      }
      if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
        return 0;
      }
    }

    if (-1 == kill(child_pid, SIGKILL)) {
      return errno;
    }

    if (-1 == wait(NULL)) {
      return errno;
    }

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
```

So we still have one signal handler but the rest of our program does not deal with programs in any way (well, except to kill the child when the timeout triggers, but that's invisible). 

There's one catch: contrary to `sigtimedwait`, `poll` does not give us the exit status of the child, we have to get it with `wait`. Which is fine, but we cannot call `kill` unconditionally and then `wait`, because the exit status would then show that the child process was killed, even though we sent a KILL signal to the child process that already finished by itself. That was surprising to me. So we only inspect the status returned by `wait` if the self-pipe is readable, meaning, if the child finished by itself.

So, this trick is clever, but wouldn't it be nice if we could avoid signals *entirely*?

### Fourth approach: Linux's signalfd

This is a short one: on Linux, there is a system call that does exactly the same as the self-pipe trick: from a signal, it gives us a file descriptor that we can `poll`. Cool, but also....Was it really necessary to introduce a system call for that? I guess the advantage is that we do not have to provide a signal handler at all and it is clearer than the self-pipe trick. 
Next!

### Fifth approach: process descriptors

*Recommended reading about this topic: [1](https://lwn.net/Articles/801319/) and [2](https://lwn.net/Articles/794707/).*

In the recent years, people realized that process identifiers (`pid`s) have a number of problems:

- PIDs are recycled and the space is small, so collisions will happen. Typically, a process spawns a child process, some work happens, and then the parent decides to send a signal to the pid of the child. But it turns out that the child already terminated (unbeknownst to the parent) and another process took its place with the same PID. So now the parent is sending signals, or communicating with, a process that it thinks is its original child but is in fact something completely different. Chaos and security issues ensue. Now, in our very simple case, that would not really happen, and additionally, a process can only kill its children. But imagine that you are implementing the init process with PID 1, e.g. systemd: every single process is your child! Or think of the case of reparenting a process. Or sending a certain PID to another process. It becomes hairy and it's a very real problem.
- Data races are hard to escape (see the previous point)
- It's easy to accidentally send a signal to all processes with `kill(0, SIGKILL)` or `kill(-1, SIGKILL)`

And the kernel developers have worked hard to introduce a better concept: process descriptors, which are (almost) bog-standard file descriptors, like files or sockets. After all, that's what sparked our whole investigation: we wanted to use `poll` and it did not work on a pid. Pids and signals do not compose well, but file descriptors do. Also, just like file descriptors, process descriptors are per-process. If I open a file with `open()` and get the file descriptor `3`, it is scoped to my process. Another process can `close(3)` and it will refer to their own file descriptotr, and not affect my file descriptor. That's great, we get isolation, so bugs in our code do not affect other processes.

So, Linux and FreeBSD have introduced the same concepts but with slightly different APIs (unfortunately):

- A child process can be created with `clone3(..., CLONE_PIDFD)` or `pdfork()` which returns a process descriptor which is almost like a normal file descriptor. On Linux, a process descriptor can also be obtained from a pid with `pidfd_open(pid)` e.g. if a normal `fork` was done.
- We wait on the process descriptor with `poll(..., timeout)` (or `select`, or `epoll`, etc)
- We kill the child process using the process descriptor with `pidfd_send_signal` (Linux) or `close` (FreeBSD) or `pdkill` (FreeBSD)
- We wait on the zombie child process again using the process descriptor to get its exit status

And voila, no signals! Isolation! Composability! (Almost) No PIDs in our program! Life can be nice like this sometimes. It's just unfortunate that there isn't a cross-platform API for that.

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

  uint32_t wait_ms = 128;

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
    if (-1 == poll(&poll_fd, 1, (int)wait_ms)) {
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

    wait_ms *= 2;
    usleep(wait_ms * 1000);

    close(child_fd);
  }
}
```

A small note: To `poll` a process descriptor, Linux wants us to use `POLLIN` whereas FreeBSD wants us to use `POLLHUP`. So we use `POLLHUP | POLLIN` since there are no side-effects to use both.


## Sixth approach: MacOS's and BSD's kqueue

It feels like cheating, but MacOS and the BSDs have had `kqueue` for decades which works out of the box with PIDs. It's similar to `poll` or `epoll` on Linux:

```c
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  (void)argc;

  uint32_t wait_ms = 128;
  int queue = kqueuex(KQUEUE_CLOEXEC);

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

    struct kevent change_list = {
        .ident = child_pid,
        .filter = EVFILT_PROC,
        .fflags = NOTE_EXIT,
        .flags = EV_ADD | EV_CLEAR,
    };

    struct kevent event_list = {0};

    struct timespec timeout = {
        .tv_sec = wait_ms / 1000,
        .tv_nsec = (wait_ms % 1000) * 1000 * 1000,
    };

    int ret = kevent(queue, &change_list, 1, &event_list, 1, &timeout);
    if (-1 == ret) { // Error
      return errno;
    }
    if (1 == ret) { // Child finished.
      int status = 0;
      if (-1 == wait(&status)) {
        return errno;
      }
      if (WIFEXITED(status) && 0 == WEXITSTATUS(status)) {
        return 0;
      }
    }

    kill(child_pid, SIGKILL);
    wait(NULL);

    change_list = (struct kevent){
        .ident = child_pid,
        .filter = EVFILT_PROC,
        .fflags = NOTE_EXIT,
        .flags = EV_DELETE,
    };
    kevent(queue, &change_list, 1, NULL, 0, NULL);

    usleep(wait_ms * 1000);
    wait_ms *= 2;
  }
  return 1;
}
```

The only surprising thing, perhaps, is that `kqueue` is stateful, so once the child process exited by itself or was killed, we have to remove the watcher on its PID, since the next time we spawn a child process, the PID will very likely be different. `kqueue` offers the flag `EV_ONESHOT`, which automatically deletes the event from the queue once it has been consumed. However, it would not help in all cases: if the timeout triggers, we have to kill the child process, which creates an event in the queue! So we have to always delete the event from the queue right before we retry.

I love that `kqueue` works with every kind of Unix entity: file descriptor, pipes, PIDs, Vnodes, sockets, etc. However, I am not sure that I love its statefulness. I find the `poll` approach simpler, since it's stateless. But perhaps this behavior is necessary for some corner cases?

On Linux, we can make this code work by using `libkqueue` which acts as a emulation layer, using `epoll` or such under the hood.

## Sixth approach: Linux's io_uring

TODO

## Conclusion

I find signals and spawning child process to be the hardest parts of Unix. Evidently this is not a rare opinion, looking at the development in these areas: process descriptors, the various expansions to the venerable `fork` with `vfork`, `clone`, `clone3`, `clone6`, etc. 

So what's the best approach then in a complex program? Let's recap:

- If you need maximum portability and are not afraid of signals and their pitfalls, use `sigsuspend`
- If you are not afraid of signals and want a simpler API, use `sigtimedwait`
- If you favor correctness and work with recent Linux and FreeBSD versions, use process descriptors with shims to get the same API on all OSes. That's probably my favorite option.
- If you only care about MacOS and BSDs (or accept to use `libkqueue` on Linux), use `kqueue` because it works out of the box with PIDs, you avoid signals completely, and it's used in all the big libraries out of there e.g. `libuv`
- If you only care about Linux and are already using `io_uring`, use `io_uring`
- If you only care about Linux and are afraid of using `io_uring`, use `signalfd` + `poll`

I often look at complex code and think: what are the chances that this is correct? What are the chances that I missed something? Is there a way to make it simplistic that it is obviously correct? And how can I limit the blast of a bug I wrote? 

Process descriptors or `kqueue` seem to me so straightforward, so obviously correct, that I would definitely favour them over signals. 


