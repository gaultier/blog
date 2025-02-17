Title: Making my debug build viable with a 30 times speed-up
Tags: LLVM, Zig, Alpine
---


I am writing a torrent application, to download and serve torrent files, in C. A torrent download is made of chunks, typically 1 MiB or 2 MiB.  At start-up, the program reads the downloaded file chunk by chunk, computes its [SHA1](https://en.wikipedia.org/wiki/SHA-1) hash, and marks this chunk as downloaded if the hash is the expected hash. Indeed, the `.torrent` file contains for each chunk the expected hash.

When we have not downloaded anything yet, the file is completely empty (but still of the right size - we use `ftruncate(2)` to size it properly even if empty), and nearly every chunk has the wrong hash. Some chunks will still have the right hash, since they are all zeroes in the file we are downloading - good news then, with this approach we do not even have to download them at all!). If we continue an interrupted download (for example we computer restarted), some chunks will have the right hash, and some not. When the download is complete, all chunks will have the correct hash. That way, we know what what chunks we need to download, if any.

Some torrent clients prefer to skip this verification at startup because they persist their state in a separate file (perhaps a sqlite database), while downloading chunks. However I favor doing a from scratch verification at startup for a few reasons, over the 'state file' approach:

- We might have crashed in the middle of a previous download, before updating the state file, and the persisted state is out-of-sync with the download
- There may have been data corruption at the disk level (not everybody runs ZFS and can detect that!)
- We can continue a partial downloaded started with a different torrent client
- Some other program might have corrupted/modified the download, unbeknownst to us and our state file

For this reason I do not have a state file at all. It's simpler and a whole class of out-of-sync issues disappears.


So I have this big [NetBSD image](https://netbsd.org/mirrors/torrents/) torrent that I primarly test with. It's not that big:

```sh
$ du -h ./NetBSD-9.4-amd64.iso 
485M	./NetBSD-9.4-amd64.iso
```

But when I build my code in debug mode (no optimizations) with Adress Sanitizer, to detect various issues early, startup takes **20 to 30 seconds!** That's unbearable, especially when working in the debugger and inspecting some code that runs after the startup.

Let's see how we can speed it up.

## Why is it a problem at all?

It's important to note that to reduce third-party dependencies, the SHA1 code is vendored in the source tree and comes from OpenSSL (there are similar variants, e.g. from OpenBSD, but not really with signficant differences). It is plain C code, not using SIMD or such.

For a while I simply renounced using a debug build, instead I use minimal optimizations (`-O1`) with Adress Sanitizer (a.k.a Asan). It was much faster, but lots of functions and variables got optimized away, and the debugging experience was thus subpar. I needed to make my debug + Asan build viable.  The debug build without Asan is much faster: the startup 'only' takes around 2 seconds. But Asan is very valuable, I want to be able to use it! And 2 seconds is still too long.

What's vexing is that from first principles, we know it should/could be much, much faster:

```sh
$ hyperfine --shell=none --warmup 3 'sha1sum ./NetBSD-9.4-amd64.iso'
Benchmark 1: sha1sum ./NetBSD-9.4-amd64.iso
  Time (mean ± σ):     297.7 ms ±   3.2 ms    [User: 235.8 ms, System: 60.9 ms]
  Range (min … max):   293.7 ms … 304.2 ms    10 runs
```

Granted, computing the hash for the whole file should be slightly faster than computing the hash for N chunks, because the final step for SHA1 is about padding the data to make it 64 bytes aligned and extracing the digest value from the state computed so far with some bit operations. But still, the order of magnitude could/should be ~300 milliseconds, not ~30 seconds!


Why is it so slow then? I can see on CPU profiles that the SHA1 function takes all of the startup time. The SHA1 code is simplistic, it does not use any SIMD or intrisics directly. And that's fine, because when it's compiled with optimizations on, the compiler does a pretty good job at optimizing and auto-vectorizing the code, and it's really fast, around ~300 ms. But the issue is that this code is working one byte at a time. And Adress Sanitizer, with its nice runtime and bounds checks, makes each memory access **very** expensive. So we accidentally wrote a stress-test for Asan.

Let's first review the simple version.

## Standard C

To isolate the issue, I have created a simple benchmark program. It reads the `.torrent` file, and the dowload file, in my case the `.iso` NetBSD image. Every chunk gets hashed and this gets compared with the expected value (a SHA1 hash, or digest, is 20 bytes long). To simplify, I skip the decoding of the `.torrent` file, and harcode where exactly in the file are the expected hashes. The only difficulty is that the last piece might be shorter than the others.

```c
#include <fcntl.h>
#include <inttypes.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static bool is_chunk_valid(uint8_t *chunk, uint64_t chunk_len,
                           uint8_t digest_expected[20]) {
  SHA_CTX ctx = {0};
  SHA1_Init(&ctx);

  SHA1_Update(&ctx, chunk, chunk_len);

  uint8_t digest_actual[20] = {0};
  SHA1_Final(digest_actual, &ctx);

  return !memcmp(digest_actual, digest_expected, 20);
}

int main(int argc, char *argv[]) {
  if (3 != argc) {
    return 1;
  }

  int file_download = open(argv[1], O_RDONLY, 0600);
  if (!file_download) {
    return 1;
  }

  struct stat st_download = {0};
  if (-1 == fstat(file_download, &st_download)) {
    return 1;
  }
  size_t file_download_size = (size_t)st_download.st_size;

  uint8_t *file_download_data = mmap(NULL, file_download_size, PROT_READ,
                                     MAP_FILE | MAP_PRIVATE, file_download, 0);
  if (!file_download_data) {
    return 1;
  }

  int file_torrent = open(argv[2], O_RDONLY, 0600);
  if (!file_torrent) {
    return 1;
  }

  struct stat st_torrent = {0};
  if (-1 == fstat(file_torrent, &st_torrent)) {
    return 1;
  }
  size_t file_torrent_size = (size_t)st_torrent.st_size;

  uint8_t *file_torrent_data = mmap(NULL, file_torrent_size, PROT_READ,
                                    MAP_FILE | MAP_PRIVATE, file_torrent, 0);
  if (!file_torrent_data) {
    return 1;
  }
  // HACK
  uint64_t file_torrent_data_offset = 237;
  file_torrent_data += file_torrent_data_offset;
  file_torrent_size -= file_torrent_data_offset - 1;

  uint64_t piece_length = 262144;
  uint64_t pieces_count = file_download_size / piece_length +
                          ((0 == file_download_size % piece_length) ? 0 : 1);
  for (uint64_t i = 0; i < pieces_count; i++) {
    uint8_t *data = file_download_data + i * piece_length;
    uint64_t piece_length_real = ((i + 1) == pieces_count)
                                     ? (file_download_size - i * piece_length)
                                     : piece_length;
    uint8_t *digest_expected = file_torrent_data + i * 20;

    if (!is_chunk_valid(data, piece_length_real, digest_expected)) {
      return 1;
    }
  }
}
```

The `SHA1_xxx` functions are lifted from OpenSSL (there are similar variants, e.g. from OpenBSD), and when compiled in non-optimized mode with Asan, we get this timing:

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     26.312 s ±  0.734 s    [User: 26.164 s, System: 0.066 s]
  Range (min … max):   25.366 s … 27.780 s    10 runs
```

This is consistent with our torrent program.

I experimented with doing a `read` syscall for each chunk (that's what `sha1sum` does) versus using `mmap`, and there was no difference; additionally the system time is nothing compared to user time, so I/O is not the limiting factor - SHA1 computation is, as confirmed by the CPU profile.

---

So what can we do about it?

- We can build the SHA1 code separately with optimizations on, always (and potentially without Address Sanitizer). That's a bit annoying, because I currently do a Unity build meaning there is only one compilation unit. So having suddenly multiple compilation units with different build flags makes the build system more complex. And clang has annotations to *lower* the optimization level for one function but not to *raise* it.
- We can compute the hash of each chunk in parallel for example in a thread pool, since each chunk is independent. That works, but that assumes that the target computer is multicore, and it forces some complexity:
  + We need to implement a thread pool (spawning a new thread for each chunk will not perform well) and pick a reasonable amount of cores
  + We need a M:N scheduling logic to compute the hash of M chunks on N threads. It could be a work-stealing queue backed by a thread-pool, or read the whole file in memory and split the data in equal parts for each thread to plow through (but beware that the data for each thread is aligned with the chunk size!). This seems complex.
- We can implement SHA1 with SIMD. That way, it's much faster regardless of the build level. Essentially, we do not rely on the compiler auto-vectorization that only occurs at higher optimization levels, we do it directly. It has the nice advantage that we have guaranteed performance even when using a different compiler, or an older compiler that cannot do auto-vectorization properly, or if a new compiler version comes along and auto-vectorization broke for this code. Since it uses lots of heuristics, this may happen.


So let's do SIMD! The nice thing about it is that we can always *also* compute hashes in parallel as well as use SIMD; the two approaches compose well together.

## SHA1 with SSE

[This](http://arctic.org/~dean/crypto/sha1.html) is an implementation from the early 2000s in the public domain. Yes, SSE, which is the forst widespread SIMD instruction set, is from the nineties to early 2000s. More than 25 years ago! There's basically no reason to write non-SIMD code for performance sensitive code for a SIMD-friendly problem - every CPU we care about has SIMD! Well, we have two write separate implementations for x64 and ARM, that's the downside. But still! 

Intel references this implementation on their [website](https://www.intel.com/content/www/us/en/developer/articles/technical/improving-the-performance-of-the-secure-hash-algorithm-1.html). According to Intel, it was fundamental work at the time and influenced them. It's also not the fastest SSE implementation, the very article from Intel is about some performance enhancements they found for this code, but it has the advantage that if you have a processor from 2004 or after, it works, and it's simple.

It works 4 bytes at a time instead of one byte at a time with the pure standard C approach. So predictably, we observe roughly a 4x speed-up (still in debug + Asan mode):

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):      7.748 s ±  0.119 s    [User: 7.665 s, System: 0.057 s]
  Range (min … max):    7.635 s …  8.062 s    10 runs
```

That's better but still not great. We could apply the tweaks suggested by Intel, but that probably would not give us the order of magnitude improvement we need.

So... did you know that in all likelihood, your CPU has dedicated silicon to accelerate SHA computations? Let's use that! We paid for it, we get to use it!

## SHA1 with the Intel SHA extension

Despite the name, Intel as well as AMD CPUs have been shipping with this extension, since around 2017. It adds a few SIMD instructions dedicated to compute SHA1 (and SHA256, and other variants). Note that ARM also has an equivalent (albeit incompatible, of course) extension so the same can be done there.

The advantage is that the structure of the code can remain the same: we still are using 128 bits SIMD registers, still computing 64 bytes at a time for SHA. It's just that a few operations get faster. How fast you ask?

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     858.7 ms ±  40.4 ms    [User: 802.5 ms, System: 53.9 ms]
  Range (min … max):   821.3 ms … 944.1 ms    10 runs
```

Now that's what I'm talking about. Around a 10x speed-up compared to the basic SSE implementation! And now we are running under a second.

What about a release build (without Asan), for comparison?


This is non SIMD version with `-O2 -march=native`, using auto-vectorization:

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     617.8 ms ±  20.9 ms    [User: 573.6 ms, System: 42.2 ms]
  Range (min … max):   598.7 ms … 669.1 ms    10 runs
```

And this is the code using the SHA extension, again with `-O2 -march=native`:

```sh
$ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     281.2 ms ±   5.4 ms    [User: 240.6 ms, System: 39.6 ms]
  Range (min … max):   276.1 ms … 294.3 ms    10 runs
```

Unsurprisingly, when inspecting the generated assembly code for the non SIMD version, the auto-vectorization is *very* limited and does not use the SHA extension (compilers are smart, but not *that* smart).

As such, it's still very impressive that it reaches such a high performance. My guess is that the compiler does a good job at analyzing data dependencies and reordering statements to maximize utilization.

The version using the SHA extension performs very well, be it in debug + Asan mode, or release mode.

## SHA using OpenSSL

The whole point of this article is to do SHA computations from scratch and avoid dependencies. Let's see how OpenSSL fares out of curiosity. It is the stock`libcrypto` (OpenSSL ships two libraries, `libcrypto` and `libssl`) found on my system, assumably compiled in release mode:

```sh
 $ hyperfine --warmup 3 './a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent'
Benchmark 1: ./a.out ./NetBSD-9.4-amd64.iso ~/Downloads/NetBSD-9.4-amd64.iso.torrent
  Time (mean ± σ):     281.5 ms ±   3.9 ms    [User: 245.7 ms, System: 35.1 ms]
  Range (min … max):   276.3 ms … 288.9 ms    10 runs
```

So, the performance is essentially identical to our version. Pretty good.



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
