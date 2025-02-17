Title: Making my debug build viable with a 30 times speed-up
Tags: LLVM, Zig, Alpine
---


I am writing a torrent application. A download is made of chunks, typically 1 MiB or 2 MiB.  At start-up, it reads the downloaded file chunk by chunk, computes its SHA1 hash, and marks this chunk as downloaded if the hash is the expected hash. Indeed, the `.torrent` file contains for each chunk the expected hash. 

When we have not downloaded anything, the file is completely empty and nearly every chunk has the wrong hash (some chunks will still have the right hash, since they are all zeroes in the file we are downloading - good news then, with this approach we do not even have to download them at all!). If we continue an interrupted download (for example we computer restarted), some chunks will have the right hash, and some not. When the download is complete, all chunks will have the correct hash.

Some torrent clients prefer to skip this verification at startup because they persist their state in a separate file (perhaps a sqlite database), while downloading chunks. However I favor doing a from scratch verification at startup for a few reasons, over the 'state file' approach:

- We might have crashed in the middle of a previous download, before updating the state file, and the persisted state is out-of-sync with the download
- There may have been data corruption at the disk level (not everybody runs ZFS and can detect that!)
- We can continue a partial downloaded started with a different torrent client
- Some other program might have corrupted/modified the download, unbeknownst to us and our state file

For this reason I do not have a state file at all. It's simpler and a whole class of out-of-sync issues disappear.

So I have this big [NetBSD image](https://netbsd.org/mirrors/torrents/) torrent that I primarly test with. It's not that big:

```sh
$ du -h ./NetBSD-9.4-amd64.iso 
485M	./NetBSD-9.4-amd64.iso
```

But when I build my code in debug mode (no optimizations) with Adress Sanitizer, to detect various issues early, startup takes **20 to 30 seconds!** That's unbearable, especially when working in the debugger and inspecting some code that runs after the startup. For a while I simply renounced using a debug build, instead I use minimal optimizations (`-O1`) with Adress Sanitizer. It was much faster, but lots of functions and variables got optimized away, and the debugging experience was thus subpar. I needed to make my debug + Asan build viable.  The debug build without Asan is much faster: 'only' around 2 seconds. But Asan is very valuable, I want to be able to use it! And 2 seconds is still too long.

What's vexing is that from first principles, we know it should/could be much, much faster:

```sh
```

Why is it so slow then? I can see on CPU profiles that the SHA1 function takes all of the startup time. The SHA1 code is simplistic, it does not use any SIMD or intrisics directly. And that's fine, because when it's compiled with optimizations on, the compiler does a pretty good job at auto-vectorizing most of the code, and it's reallt fast. But the issue is that this code is working one byte at a time. And Adress Sanitizer, with its nice runtime and bounds checks, makes each memory access **very** expensive.

So what can we do then?

- We can build the SHA1 code separately with optimizations on, always (and potentially without Address Sanitizer). That's a bit annoying, because I currently do a Unity build meaning there is only one compilation unit. So having suddenly multiple compilation units with different build flags makes the build system more complex. And clang has annotations to *lower* the optimization level for one function but not to *raise* it.
- We can compute the hash of each chunk in parallel for example in a thread pool, since each chunk is independent. That works, but that assumes that the target computer is multicore, and it forces some complexity:
  + We need to implement a thread pool (spawning a new thread for each chunk will not perform well) and pick a reasonable amount of cores
  + We need a M:N scheduling logic to compute the hash of M chunks on N threads. It could be a work-stealing queue backed by a thread-pool, or read the whole file in memory and split the data in equal parts for each thread to plow through (but beware that the data for each thread is aligned with the chunk size!). This seems complex.
- We can implement SHA1 with SIMD. That way, it's much faster regardless of the build level. Essentially, we do not rely on the compiler auto-vectorization that only occurs at higher optimization levels, we do it directly. It has the nice advantage that we have guaranteed performance even when using a different compiler, or an older compiler that cannot do auto-vectorization properly, or if a new compiler version comes along and auto-vectorization broke for this code. Since it uses lots of heuristics, this may happen.


To isolate the issue, I have created a simple benchmark program. It reads the `.torrent` file, and the dowload file, in my case the `.iso` NetBSD image. Every chunk

## SHA1 with SSE

[This](http://arctic.org/~dean/crypto/sha1.html) is an implementation from the early 2000s in the public domain. 

Intel references it on their [website](https://www.intel.com/content/www/us/en/developer/articles/technical/improving-the-performance-of-the-secure-hash-algorithm-1.html). According to Intel, it was fundamental work at the time and influenced them. It's also not the fastest SSE implementation, the very article from Intel is about some performance enhancements they found for this code, but it has the advantage that if you have a processor from 2004 or after, it works, and it's simple.

It works 4 bytes at a time instead of one byte at a time with the pure standard C approach. 

-------------------------

## Debug + ASAN, SW

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     26.312 s ±  0.734 s    [User: 26.164 s, System: 0.066 s]
  Range (min … max):   25.366 s … 27.780 s    10 runs
```
 
## Debug, SW

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
 ⠸ Current estimate: 2.338 s      ██████████████████████████████████████████████████████████████  Time (mean ± σ):      2.344 s ±  0.054 s    [User: 2.292 s, System: 0.046 s]
  Range (min … max):    2.268 s …  2.403 s    10 runs
```

## Release, SW


```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     617.8 ms ±  20.9 ms    [User: 573.6 ms, System: 42.2 ms]
  Range (min … max):   598.7 ms … 669.1 ms    10 runs
 
```

## Debug + ASAN, HW

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     858.7 ms ±  40.4 ms    [User: 802.5 ms, System: 53.9 ms]
  Range (min … max):   821.3 ms … 944.1 ms    10 runs
```

## Debug, HW

```sh
 $ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     799.5 ms ±  46.8 ms    [User: 751.0 ms, System: 43.4 ms]
  Range (min … max):   762.3 ms … 870.8 ms    10 runs
 
```

## Release, HW

```
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     281.2 ms ±   5.4 ms    [User: 240.6 ms, System: 39.6 ms]
  Range (min … max):   276.1 ms … 294.3 ms    10 runs
 
```

## Release, libcrypto

```sh
 $ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     281.5 ms ±   3.9 ms    [User: 245.7 ms, System: 35.1 ms]
  Range (min … max):   276.3 ms … 288.9 ms    10 runs
 
```

## Debug + Asan, SSE (no SHA extension)

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):      7.748 s ±  0.119 s    [User: 7.665 s, System: 0.057 s]
  Range (min … max):    7.635 s …  8.062 s    10 runs
```

~ x4 speed-up since we process 4 bytes at a time instead of 1.
