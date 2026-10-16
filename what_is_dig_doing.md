Title: What is `dig` doing?
Tags: DTrace
---

When I'm bored or blocked on something at work, I run programs I frequently use under DTrace to learn what they are exactly doing. Am I the only one to do this?

So yesterday I did that with `dig` which is an open source DNS resolver and troubleshooting CLI. And oh boy I wasn't disappointed.

So I ran: `sudo dtruss -d -e dig google.com` to display all system calls done by the program.
The `-d` options prints the relative timestamps, and the `-e` option prints the elapsed time of each system call, both in microseconds.

## Open some non-existing translations files

The first thing `dig` does is try to open translation files:

```
5228       3 stat64("/usr/share/nls/en_US.UTF-8/libdns.cat.cat\0", 0x16F2D2A20, 0x0)		 = -1 Err#2
5229       0 stat64("/usr/share/nls/libdns.cat/en_US.UTF-8\0", 0x16F2D2A20, 0x0)		 = -1 Err#2
5231       2 stat64("/usr/local/share/nls/en_US.UTF-8/libdns.cat.cat\0", 0x16F2D2A20, 0x0)		 = -1 Err#2
5232       0 stat64("/usr/local/share/nls/libdns.cat/en_US.UTF-8\0", 0x16F2D2A20, 0x0)		 = -1 Err#2
5239       0 stat64("/usr/share/nls/en_US.UTF-8/libisc.cat.cat\0", 0x16F2D28F0, 0x0)		 = -1 Err#2
5239       0 stat64("/usr/share/nls/libisc.cat/en_US.UTF-8\0", 0x16F2D28F0, 0x0)		 = -1 Err#2
5240       0 stat64("/usr/local/share/nls/en_US.UTF-8/libisc.cat.cat\0", 0x16F2D28F0, 0x0)		 = -1 Err#2
5240       0 stat64("/usr/local/share/nls/libisc.cat/en_US.UTF-8\0", 0x16F2D28F0, 0x0)		 = -1 Err#2
```

Which all subsequently fail. I did not ask for any transaction, actually, but it went ahead and above, by trying to translate from English to English, and failing to find its own translation files. Well ok then.

## Open some sockets and throw them away

Next, it opens some sockets, and immediately closes them. I added human readable names for the socket features it uses:

```
# AF_INET, SOCK_STREAM: Check if IPv4 works.
5256      13 socket(0x2, 0x1, 0x0)		 = 3 0
5263       6 close(0x3)		 = 0 0

# AF_INET6, SOCK_STREAM: Check if IPv6 works.
5265       2 socket(0x1E, 0x1, 0x0)		 = 3 0
5267       1 getsockname(0x3, 0x16F2D2E98, 0x16F2D2E94)		 = 0 0
5268       1 close(0x3)		 = 0 0

# AF_UNIX, SOCK_STREAM: Check if Unix domain sockets work.
5270       1 socket(0x1, 0x1, 0x0)		 = 3 0
5272       2 close(0x3)		 = 0 0
```

So it is probing what the system supports.

For one, you could optimistically open a socket with all the desired options to do the real work, and it it fails, inspect the error to know what is not supported.

Second, which has existed since the eighties, so what advanced socket features do you really need?

And finally, what are the chances that I am running `dig` on a computer that does not support IPv4 or v6 in 2026?

## Create some threads

Next, `dig` creates 3 threads, along with a non-blocking pipe to communicate between them, and `kqueue` to be awoken by the OS whenever a message arrives on this pipe:

```
5337       8 bsdthread_create(0x980F800100C55720, 0xA22405000, 0x16F3DB000)		 = 1866313728 0
   2       0 thread_selfid(0x0, 0x0, 0x0)		 = 5219193 0
5348       4 bsdthread_create(0xFF6F800100C58CB8, 0xA22407000, 0x16F4E7000)		 = 1867411456 0
   2       1 thread_selfid(0x0, 0x0, 0x0)		 = 5219194 0
5366       3 pipe(0x0, 0x0, 0x0)		 = 3 0
5367       0 fcntl(0x3, 0x3, 0x0)		 = 0 0
5368       1 fcntl(0x3, 0x4, 0x4)		 = 0 0
5371       0 kqueue(0x0, 0x0, 0x0)		 = 5 0
5373       1 kevent(0x5, 0x16F2D2E78, 0x1)		 = 0 0
5380       4 bsdthread_create(0x3942000100C61820, 0xA22409000, 0x16F5F3000)		 = 1868509184 0
   2       0 thread_selfid(0x0, 0x0, 0x0)		 = 5219195 0
```

Actually, I am fine with this, but also, if we are already using asynchronous I/O, do we really need 3 threads on top? We could just do `read(2)` and `write(2)` calls on one socket and use `kqueue` for the event loop, single-threaded.

## Translations, again

Perhaps some more translation files have appeared in the meantime? There's only one way to know! Let's try to open them!

```
5388       2 stat64("/usr/share/nls/en_US.UTF-8/libdst.cat.cat\0", 0x16F2D29F0, 0x0)		 = -1 Err#2
5389       0 stat64("/usr/share/nls/libdst.cat/en_US.UTF-8\0", 0x16F2D29F0, 0x0)		 = -1 Err#2
5391       2 stat64("/usr/local/share/nls/en_US.UTF-8/libdst.cat.cat\0", 0x16F2D29F0, 0x0)		 = -1 Err#2
5392       0 stat64("/usr/local/share/nls/libdst.cat/en_US.UTF-8\0", 0x16F2D29F0, 0x0)		 = -1 Err#2
```

## How many files can I open?

```
# 0x1008: RLIMIT_NO_FILE
5398       0 getrlimit(0x1008, 0x16F2D2678, 0x0)		 = 0 0
# [...]
5410       0 getrlimit(0x1008, 0x16F2D2C58, 0x0)		 = 0 0
```

`RLIMIT_NOFILE` gets the maximum number of file descriptors our program can open. `dig` does that twice in a row.

So, ok, why not, but also, how many thousands of file descriptors do you intend to open exactly, to do a simple _DNS resolution_ ?

## Open a configuration file

Next it opens its configuration file, which on my system does not exist. No issue there.

```
5401       2 open_nocancel("/Users/philippe.gaultier/.digrc\0", 0x0, 0x0)		 = -1 Err#2
```

## Open `/etc/resolv.conf`

Then, `dig` opens and reads `/etc/resolv.conf`:

```
5437      27 open_nocancel("/etc/resolv.conf\0", 0x0, 0x0)		 = 6 0
5439       0 fstat64(0x6, 0x16F2D2B30, 0x0)		 = 0 0
5513    3710 read_nocancel(0x6, "#\n# macOS Notice\n#\n# This file is not consulted for DNS hostname resolution, address\n# resolution, or the DNS query routing mechanism used by most\n# processes on this system.\n#\n# To view the DNS configuration used by this system, use:\n#   scutil --dns\n#\n# ", 0x1000)		 = 473 0
5525       1 read_nocancel(0x6, "\0", 0x1000)		 = 0 0
5534       8 close_nocancel(0x6)		 = 0 0
```

On the face of it, it seems reasonable, until you notice the big warning in this file:

```
# macOS Notice
#
# This file is not consulted for DNS hostname resolution, address
# resolution, or the DNS query routing mechanism used by most
# processes on this system.
#
# To view the DNS configuration used by this system, use:
#   scutil --dns
```

Ok, so, surely, the next thing `dig` is going to do is follow these instructions, right? Right?

## We need some shared memory, immediately

```
184      11 shm_open(0x190A22F98, 0x0, 0x0)		 = 6 0
193       5 mmap(0x0, 0x4000, 0x1, 0x40001, 0x6, 0x0)		 = 0x100DD0000 0
194       1 close_nocancel(0x6)		 = 0 0
```

So, at that point we have 3 threads communicating over a pipe, but we decide to create shared memory (`shm_open`) to presumably also share/communicate between threads?

## Is `UDP` even supported?

More socket opening and throwing away to check if `UDP` is supported:

```
# 0x11: SOCK_DGRAM (UDP)
252      13 socket(0x2, 0x2, 0x11)		 = 6 0
255       2 setsockopt(0x6, 0x0, 0x3)		 = 0 0
255       0 setsockopt(0x6, 0x0, 0x1B)		 = 0 0
259       4 close(0x6)		 = 0 0
```

More feature tests, of course we immediately throw away the socket right after:

```
262       1 socket(0x1E, 0x2, 0x11)		 = 6 0
# IP_TOS
264       1 setsockopt(0x6, 0x29, 0x24)		 = 0 0
# IP_RECVTOS
265       0 setsockopt(0x6, 0x29, 0x23)		 = 0 0
278      13 bind(0x6, 0x101622FA0, 0x1C)		 = 0 0
280       1 getsockname(0x6, 0x16F3DABF0, 0x16F3DAAEC)		 = 0 0
340      66 sendmsg(0x6, 0x16F3DAAB8, 0x0)		 = 1 0
342       0 fcntl(0x6, 0x3, 0x0)		 = 2 0
343       1 fcntl(0x6, 0x4, 0x6)		 = 0 0
350       7 recvmsg(0x6, 0x16F3DAAB8, 0x0)		 = 1 0
357       6 close(0x6)		 = 0 0
```

## Preparations for the DNS query

We create a plain IPv4 UDP socket:

```
# 0x2: AF_INET
# 0x2: SOCK_DGRAM (UDP)
359       2 socket(0x2, 0x2, 0x11)		 = 6 0
# 0x0: F_DUPFD
# 0x14: 20
360       0 fcntl(0x6, 0x0, 0x14)		 = 20 0
360       0 close(0x6)		 = 0 0
```

Interestingly, it looks like we are opening and immediately closing the socket, but what we are doing is even more surprising:

1. Open a socket, we get the file descriptor `6`
2. Duplicate the file descriptor `6`, we ask for a file descriptor of at least `20`, we get the file descriptor `20`
3. Close the file descriptor `6`

I ... don't have the first idea why we do this dance. Maybe there's a reason, why do we care about getting a higher file descriptor number? On its face, it looks ridiculous.

---

Then we make the socket non-blocking (there is an option to create the socket as non-blocking in the first place, which we did not use).
This is the standard way of doing this: first we get the current socket options, then we set the new options to be the old options plus `O_NONBLOCK`:

```
# 0x3: F_GETFL
361       0 fcntl(0x14, 0x3, 0x0)		 = 2 0
# 0x4: F_SETFL
# 0x6:  2 | O_NONBLOCK
361       0 fcntl(0x14, 0x4, 0x6)		 = 0 0
```

Then we set some more socket options, presumably based on the feature tests from before:

```
setsockopt(0x14, 0xFFFF, 0x1022)   // SO_NOSIGPIPE
setsockopt(0x14, 0xFFFF, 0x400)    // SO_TIMESTAMP
setsockopt(0x14, 0x0, 0x1C)        // IP_DONTFRAG
getsockopt(0x14, 0xFFFF, 0x1002)   // SO_TYPE
setsockopt(0x14, 0x0, 0x1B)        // IP_RECVTOS
```

Next we `bind` the socket to a local address:

```
413      46 bind(0x14, 0x100D1DDB0, 0x10)		 = 0 0
```

We optimistically try to read from the socket, in case anyone is replying to our DNS query before we even sent it, which of course immediately fails:

```
415       1 recvmsg(0x14, 0x16F3DACC0, 0x0)		 = -1 Err#35
```

Then, will we finally do the real DNS query? No, it's time to delegate the work to another thread! We send the file descriptor of the socket to the worker thread over the pipe:

```
# \024: 20, the file descriptor
418       2 write(0x4, "\024\0", 0x8)		 = 8 0
```

And the worker thread wakes up and reads it:

```
6    4676 kevent(0x5, 0x0, 0x0)		 = 1 0
8       2 read(0x3, "\024\0", 0x8)		 = 8 0
```

And then the worker thread watches this socket for incoming data, and tries to optimistically read alread:

```
13       4 kevent(0x5, 0x16F5F2ED8, 0x1)		 = 0 0
13       0 read(0x3, "\0", 0x8)		 = -1 Err#35
```

Remember, at that point, the DNS query has still not been done. So of course the read fails.

## Finally, the DNS query

```
462      42 sendmsg(0x14, 0x16F3DAC90, 0x0)		 = 39 0
```
