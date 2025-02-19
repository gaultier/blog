 $ hyperfine --warmup 2 './src-main.bin' './src.bin'
Benchmark 1: ./src-main.bin
  Time (mean ± σ):      2.300 s ±  0.029 s    [User: 1.649 s, System: 0.583 s]
  Range (min … max):    2.265 s …  2.354 s    10 runs
 
Benchmark 2: ./src.bin
  Time (mean ± σ):     197.0 ms ±   6.4 ms    [User: 128.4 ms, System: 62.4 ms]
  Range (min … max):   188.7 ms … 213.7 ms    15 runs
 
Summary
  ./src.bin ran
   11.67 ± 0.41 times faster than ./src-main.bin
