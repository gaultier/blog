
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
