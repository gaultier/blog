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

That's how `timeout` from coreutils implements it. This is quite simple on paper: This is a system call that suspends the process until a selected set of signals triggers, and it is used in `timeout` so:

```c
      while ((wait_result = waitpid (monitored_pid, &status, WNOHANG)) == 0)
        sigsuspend (&orig_set);  /* Wait with cleanup signals unblocked.  */
```

This is a very compact way to do:

```c
    while(true) {
        sigsuspend (&orig_set);
        if (0 == waitpid (monitored_pid, &status, WNOHANG)) { break; }
    }
```


We wait with `waitpid` in a non-blocking way (thanks to `WNOHANG`) on the child process, and 
