<link rel="stylesheet" type="text/css" href="main.css">
<link rel="stylesheet" href="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/styles/default.min.css">
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/highlight.min.js"></script>
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/languages/x86asm.min.js"></script>
<script>
window.addEventListener("load", (event) => {
  hljs.highlightAll();
});
</script>
<a href="/blog">All articles</a>

# Optimizing an Advent of Code solution in assembly

**Table of Contents**
-   [Optimizing a solution for Advent of Code 2018 challenge in
    assembly](#optimizing-a-solution-for-advent-of-code-2018-challenge-in-assembly)
    -   [The new solution](#the-new-solution)
    -   [The x86_64 implementation](#the-x86_64-implementation)
    -   [Benchmarking](#benchmarking)
    -   [Learnings](#learnings)
    -   [Appendix: The full code](#appendix-the-full-code)
        -   [The old C implementation](#the-old-c-implementation)
        -   [The x64 implementation](#the-x64-implementation)

*If you spot an error, please open a [Github issue](https://github.com/gaultier/blog)!*

---

A few days ago I was tweaking the appearance of this blog and I stumbled upon my [first article](/blog/advent_of_code_2018_5) which is about solving a simple problem from Advent of Code. Here it is again:

> We have a string looking like this: `AabcdZZqQ` which represents a chain of
> chemical units. Adjacent units of the same type (i.e letter) and opposite
> polarity (i.e casing) react together and disappear.
> It means we want to remove adjacent characters which are the same letter and have opposite casing, e.g
> `Aa` and `qQ` disappear while `bc` and `ZZ` remain. Once we are finished, we have: `bcdZZ`.
> 
> The final output is the number of characters in the final string, i.e, `5`.


Immediately, I thought I could do better than my past self: 
- In the Lisp solution, there are lots of allocations and the code is not straightforward.
- In the Lisp solution, we use multiple external dependencies, which usually turn out to be problematic in the long run.
- In the C solution, there is no allocation apart from the input but we do a lot of unnecessary work.
- In the C solution, we use `abs(x) == 32` which we could avoid by doing `x*x == 32*32`.



This coincided with me listening to an interview from the VLC developers saying there wrote hundred of thousand of lines of (multi platform!) Assembly code by hand in their new AV1 decoder. I thought that was intriguing, who still writes assembly by hand in 2023? Well these guys are no idiots so I should try it as well.

## The new solution

I came up with a new algorithm, which on paper does less work. It's one linear pass on the input, and does not allocate.

Since the result we care about is the number of remaining characters, we simply keep track of the count as we sift through the input.

We maintain two pointers, `current` and `next`, which we compare to decide whether we should merge the characters they point to. 'Merging' means setting the two characters to `0` (it's basically a tombstone) and lower the count.

`next` is always incremented by one in each loop iteration, that's the easy one.

`current` is always pointing to a character before `current`, but not always directly adjacent, because there may be tombstones, i.e. zeroes, in-between.


In pseudo-code:

```
remaining_count = len(input)
end = input + len(input)
current = &input[0]
next = &input[1]

while next != end:
    diff = *next - *current

    if diff*diff == 32*32:
      *current = 0
      *next = 0
      remaining_count -= 2

      current -= 1
      while current == 0:
        current -= 1
      endwhile
    else:
      current = next
    endif

 next += 1
    
endwhile

print(remaining_count)

```

The easy case is when there is no need to merge: `current` simply becomes `next` (and `next` is incremented at the end of the loop iteration).

The 'hard' case is merging: we set the two tombstones, lower the count, and now we are in a pickle: `current` needs to go backwards, but we do not know to where. There might be an arbitrary number of zeroes preceding the character `current` points to: the data on the left of `next` is sparse, the data on the right of `next` is not.

```
[...] 0 0 A 0 0 0 0 B 0 0 0 0 C D E F [...]
          ^         ^         ^
          |         |         |
          target    |         |
                    current   |
                              next
```

So we have to do a backwards search to find the first non zero character.
We could memoize this location, but that would basically come down to the Scheme solution, having an output array of the same size as the input.

Astute readers might have noticed a potential issue with the backwards search: We may underflow the `input` and go out of bounds! To avoid that, we could clamp `current`, but the branch misprediction is costly (an earlier implementation of mine did this and that was almost twice as slow!), and we can simplify the code as well as improve the performance by simply prefixing the `input` with a non-zero value that has no chance of being merged with the rest of the input, say, `1`. 

Let's implement it in x86_64 assembly!

## The x86_64 implementation

*For a gentle introduction to x64 assembly, go read an [earlier article](/blog/x11_x64.html) of mine.*


```x86asm
BITS 64
CPU X64

%define SYSCALL_EXIT 60
%define SYSCALL_WRITE 1

section .data


prefix: db 1
input: db "xPGgpXlvVLLP..." ; Truncated for readability
static input:data

%define input_len 50000

section .text

exit:
static exit:function
  mov rax, SYSCALL_EXIT
  mov rdi, 0
  syscall

solve:
static solve:function
  push rbp
  mov rbp, rsp

  ; TODO

  pop rbp
  ret

global _start
_start:
  call solve
  call exit
```


We circumvent reading the input from a file and embed it directly in our code, something many people having their hand at Advent of Code challenges do. It is in the `data` section and not in the `.rodata` section because we are going to mutate it in place with the tombstones.

We also have to exit the program by ourselves since there is no libc, and we create a `solve` function which will have our logic.

We compile and run it so (on Linux, other OSes will be similar but slightly different):

```shell
$ nasm -f elf64 -g aoc2018_5.asm && ld.lld aoc2018_5.o -static -g -o aoc2018_5
$ ./aoc2020_5
```

Which outputs nothing for now, of course.

---

We will need to print the `remaining_count` to `stdout` at the end so we add a function to do so:

```x86asm
write_int_to_stdout:
static write_int:function
  push rbp
  mov rbp, rsp

  sub rsp, 32

  %define ARG0 rdi
  %define N rax
  %define BUF rsi
  %define BUF_LEN r10
  %define BUF_END r9

  lea BUF, [rsp+32]
  mov BUF_LEN, 0
  lea BUF_END, [rsp]
  mov N, ARG0

  .loop:
    mov rcx, 10 ; Divisor.
    mov rdx, 0 ; Reset rem.
    div rcx ; rax /= rcx

    add rdx, '0' ; Convert to ascii.

    ; *(end--) = rem
    dec BUF_END
    mov [BUF_END], dl
    
    inc BUF_LEN

    cmp N, 0
    jnz .loop

  mov rax, SYSCALL_WRITE
  mov rdi, 1
  mov rsi, BUF_END
  mov rdx, BUF_LEN
  syscall


  %undef ARG0
  %undef N
  %undef BUF
  %undef BUF_LEN
  %undef BUF_END

  add rsp, 32
  pop rbp
  ret
```

I am trying a new style of writing assembly which I saw notably the Go developers use: Since the biggest problem is that we have no named variables, we leverage the macro system from `nasm` to name the registers we work with in a human readable fashion.


Our `solve` function can now return a dummy number and we can print it out by passing the return value (in `rax`) of `solve` as the first argument (in `rdi`) of `write_int_to_stdout`:

```x86asm
solve:
static solve:function
  push rbp
  mov rbp, rsp

  mov rax, 123

  pop rbp
  ret

global _start
_start:
  call solve

  mov rdi, rax
  call write_int_to_stdout

  call exit
```

---


We now can focus on implementing `solve`. It's a one to one translation of the pseudo-code. We just have to judiciously choose which registers to use based on the x64 System V ABI to avoid bookkeeping work of saving and restoring registers. For example, we use `rax` to store `remaining_count` since this will be the return value, so that we do not have to do anything special at the end of the function.

Another pitfall to be aware of is that since we are dealing with ASCII characters, we could use the 8 bit form of the registers. However, some opcode such as `imul` are not usable with these. We have to use the 16, 32, or 64 bit form. This does not compile:

```x86asm
  mov dl, 2
  imul dl, dl
```

But this does:

```x86asm
  mov dx, 2
  imul dx, dx
```

And so we need to zero extend the 16 bit registers in some locations with `movzx` to fill the remainder of the register with zeroes. Forgetting to do so will lead to very nasty, obscure bugs.

Finally, we always write loops in the form of `do { ... } while(condition)`. This is easier in our case; we assume (and know) the input is not empty, for example.

Here we go. Note that this function does not need any stack space, since we modify the input in place, and the standard registers are enough to store the few values we keep track of:

```x86asm
solve:
static solve:function
  push rbp
  mov rbp, rsp

  %define INPUT_LEN r10
  %define CURRENT r9
  %define NEXT r11
  %define REMAINING_COUNT rax
  %define END r8

  lea CURRENT, [input] 
  lea NEXT, [input + 1] 
  mov INPUT_LEN, input_len
  mov REMAINING_COUNT, INPUT_LEN
  lea END, [input]
  add END, INPUT_LEN
  

.loop:
  movzx dx, BYTE [CURRENT]
  movzx cx, BYTE [NEXT]
  sub dx, cx
  imul dx, dx

  mov rcx, 32*32

  cmp rdx, rcx
  jnz .else
  .then:
    mov BYTE [CURRENT], 0
    mov BYTE [NEXT], 0

    sub REMAINING_COUNT, 2

    .reverse_search:
    dec CURRENT
    mov dl, [CURRENT]
    cmp dl, 0
    jz .reverse_search


    jmp .endif
  .else:
    mov CURRENT, NEXT
  .endif:

  inc NEXT
  cmp NEXT, END
  jl .loop

  %undef INPUT_LEN
  %undef CURRENT
  %undef NEXT
  %undef REMAINING_COUNT
  %undef END


  pop rbp
  ret
```

## Benchmarking

So, did it work? Is it fast? Let's compare the old C solution (also embedding the input data for a fair comparison) with our new Assembly one:

```sh
$ clang -Ofast -g3 -march=native aoc2018_5.c -o aoc2018_5-c
$ hyperfine --warmup 3 --shell=none ./aoc2018_5 ./aoc2018_5-c

Benchmark 1: ./aoc2018_5
  Time (mean ± σ):       2.7 ms ±   1.0 ms    [User: 2.4 ms, System: 0.1 ms]
  Range (min … max):     1.2 ms …   4.6 ms    1174 runs
 
Benchmark 2: ./aoc2018_5-c
  Time (mean ± σ):       5.3 ms ±   1.4 ms    [User: 5.0 ms, System: 0.2 ms]
  Range (min … max):     4.1 ms …  10.4 ms    442 runs
 
Summary
  './aoc2018_5' ran
    1.95 ± 0.86 times faster than './aoc2018_5-c'
```

Yes, indeed, almost twice as fast!


## Learnings

- Assembly can absolutely be written by hand, although with (much) more effort and a harder time troubleshooting what goes wrong.
- Some tools (profilers, reverse-engineering tools) expect that the assembly they consume came from a C source file and will be confused when it's not the case.
- There is no need to reach for esoteric, vendor specific instructions (such as SIMD or Intel string opcodes) to go fast and even beat a C implementation that does a bit more work than necessary.
- Doing less work is the most important thing when optimizing.
- At the same time, doing a bit of work instead of memoizing can be fast(er), even though it seems counter-intuitive, due to memory latency and cache misses.
- Having a clear idea in pseudo-code of the solution will simplify the implementation in any language.
- Naive assembly is very fast (your CPU is crazy fast when you do not abuse it with badly written interpreted languages, seriously!), but a good compiler will emit better assembly than you if you write C or similar. Unless you are an expert like the VLC guys.


> If you liked this article and you want to support me, and can afford it: [Donate](https://paypal.me/philigaultier?country.x=DE&locale.x=en_US)

## Appendix: The full code

### The old C implementation

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char input[] =
    "xPGgpXlvVLLPplNSiIsWwaAEeMmJjyYfFWfFwpPqcCYvVySsAUuaCcDdHlLSshxKkMmXQnNKkr"
    "RptBbTqQEevKkkKVsSmMmMvqFfGFSsfZzgQTtFLlfsSFBTtbfbBiIAHhzZaVNbBOonsfFSBYGg"
    "RryvaAVTtbFfqaAEetqQUubBTyYAWwzZeENRrgGaAfFnNnpPJjulLEeUaQqnNJjQtTPTaqQAoO"
    "EerRtnVaALlhDdPpHvvVrRsFVvfsMcCvkPZzpKbBOofZzyYFzZsAjJavGgkKRrVwWSYygFfGLr"
    "RLlgGlVaAXZzHuULDpPdoOlhgGVoOKkVvaAhOoHIiBNnxsSXbBbvVFfvtTUvVlaALPptTGguPp"
    "jJFmRrMqjJOOqQZzooQRVvrKkpVvPOopPKKbBkYykYQqPpPpoKkOihHIECcJjeyvsSVpbBPGKk"
    "gfVvHhiIvGgViXxlLdDIgGKMCncCNcmfFDDdSsXxqQtTaAdenNdDVvPuUpVvtTZzEQqyoOvVTx"
    "XtbBnDdcCZWwrRzNaeEAlDZzeEdJlelLEzZhHhOoHYyKkLjWwNnLAjJaiHhIvhHHhEhHeSsSKk"
    "sXxVvzZEevVzpliIRPprLPcOHhsSoRSsTtrlLCiISsJjMmZvnNoOVclLCVzZMmKhHkqWufFmMS"
    "mMkKsiFfoRrOIUozZyYNnSNnqQsTrRtSHhNniIbGgCcnNBxXSFYyWwfgFRrfaROorAsxXSGsSK"
    "BbkspPIeoOHhEiXxVvpPUfFuyYypPynNYpPSskdDKjJCeErRtxXTHIzjJZihoOZvoOGfFWwgVz"
    "CUuaXxAeEnBVvbNiIAaRdDxXrmMcgCyYceyYyYEEMmgWwGOoKkqQkeEVvGgrRJjrYyEeRKYyOP"
    "ZzJjpooOcCrgGtTOoRNneHiIXxnNIQqzZiHhZgGZzyYeEuUOmMbZzUjaAJuPpBozZzlfFLiIph"
    "HsvVSvVUTtuDdPhPVDdPpvYyvDgGCcdTSstaHhcCHhAJgGLOOoaARVCclgGLvrsSolAPpUuaXx"
    "aAkoOPpeEzGgZcCjJHhKkKlYUuyMQIiqSiqQBbIsGLLGgvWwTtVlpyTtYgAaGPSeEfFsDdgGkV"
    "bBNnvQrLlRqYWwyEqQLlsSSVvstTghHGeKkKQqhHHhmMoYyhsSAaHdDuUKkOMmDdMmgGgQqRnN"
    "TtVvryYlDdrnNLlRUcCurRgGZuUWwMmbBTtjJzjJbBdDaAwWLjJKDpPpcCDdHhPRrhHRzjeEJZ"
    "mMcNfVvlLvtTVSsxXFnVvTxtTKkjkAaKhzZxXfFHuBbkKTeEyrRcCYTtGgtuZDdjJzUUcSsCqQ"
    "gGuUqUulLRrwWQeHOohSXxNnisSIWHhwiuUIfFVMxUaAuGdDgXmdHhDUuTFWwftbBvVKAakvrq"
    "QAayYWwRCcIvVijJhHrSNnsDaAdBbHLlhRPpAqZzQSsaQmOpPozQLlqhHZIDdisdDSyFpPfEJj"
    "eYowWhHrnNRRUuhHLeEdDlkKyYkKqQCcEexXifFIYfFqQyFzDdZcCorRgGzqQZDdzIiMmwWrRI"
    "IjJihlLHoOyYDzrRuUKbBvMmeEKkFffFsSVcYJuUjQqMHEeeEhbBYyucCfFGDdqQRmMHhhHqWw"
    "oOQTtrBhHxXCcbggGGpgGPgEecCYyEeEesSisSeOovVcwWCEIPJjnlLTtyYNLtfFHzZQCcqLly"
    "xXYSqvGgVQzZsBvVDoOdlLbfFGxphHqQPHhmMdDGgHszjJZShXxXYysbBifrRFiIxcChIiHgGQ"
    "qqvVbBEewWQxjJZmPpUuMzXlYyHhLXDvVBbdEsSexsSvtTrReEVnNmMTtXuKkjxXJMyYmZFfzU"
    "MmyVvTtFfYdDExEeXvVooOOjJTtCcUrRSwWsbBbhHtTBGVvgNDdnQqukKGgJOYHFfhKfFkSnNk"
    "KOoOohHsSnNsmmMMvVFGglHxXhLfFPsKkSpfyBbqQqaxXfpPZzbBCggGdDPlBbLaAZzKeEkYJi"
    "IemMtYyTlLjJEjXxsSyLlXxCckKeEuUZzrqQlLxXafFeEVvAiRrIRpmiRrFfbBzZvHhVICcoOM"
    "XxiWwNIinkkKBbgQqGPpLlKWhHwszZSQuUjKkfFJcZzCVvWeEYywtXxTjJzZkSsKfFeEcCrDhX"
    "xMmrRjUuOoYyJQDdqHwWMmLlOodqQMmqGCcgwWpJjqMmQKkPMmpPBbxnNzZyYySscTcCFftwWO"
    "eEoOobSsBSsOoaAGoOgRWwcQUWOokKwuTdUuDtYyYiJjYyIyquqaFfwWAQUZtKkTBbzndDXxjJ"
    "LlJjyxWjJDdLlsSMmGgtiGgIqQpgGTtDdPClLAasyYFlLKoOkfMmFUuBEerRYyGflLFDdnNFfg"
    "FLlXJjxWHhwDOoOodOokKPpMmfoOzZupPUvVjJSBRrbYLlyuUNTXxnNtUuvVVvpPQqQqUuaqQA"
    "lLMBbHhxOfFozZJjXzZhHgqQEPpeVvSsEgGAaeUucCItTiLuUlGSsyYEeRrBbZiIAPpvRrhHzq"
    "EeQqvVBbGgIhHRXxrishHSQNnyYZzxXZpdDPVWXxwMvVmmMByYcCaAEebxXVvKfFlLcCcCkZzz"
    "QqGgFIifPpZNnjJMwsSWwRrWmvHosSOhVIibMmyYQqTtBMoOYOoSsZFKuUkKZzZzxXPpbqAanN"
    "NnQyYsZzShHUuGgSiIsYgGyeeECcsAadGgDOojqQGgSstTPpJSQRrGgUunNqpkKdDZzPhHkSsr"
    "RQwtTXyYxsSZzAaRcUgGvVufNSfFsnFCrXeogGWwQqlLOKbBuUkvWwzZUuEoOKkNneseECcwWw"
    "zbBWiIwDQqdAawWiIwWNnLiQDdqIAatTEOdDoeVkOopPKvQmbBKkvVbyhHYBWwbzZrQqRZDdhp"
    "CcOoXxnayYizZIFfANPKkeELKDdHhvyYVCckkvVKTAadDsSHhAjBbeQqEJpPaJjXxaAAFfXxRr"
    "VpuFWwWwfFoOZRrzPpxHJjhIqQiXfrRkKkKkNnLlKqhhHLNWWKkwwZznvOIioMwWXxeEmYyzZX"
    "cCxVZzmMUuPKYykKyYkpiIlSAaYcCyBCwDwWdWPpcbsghHnNbBsSMlLbBTpPtEeVuUZzfciICE"
    "mMMfFmlLeuwWoOnNUgGJjiybBCJZziZzAaIjQqhHCcaAcnGgjUuPpQqpPhXxHUjJuuxNnXUYyJ"
    "CcLOoImMilBbLlkKGkKPwWEeuUzZHoOqQjJFfGgCckBbKzlwKpQqsSsSViIIirRvbBiZzIZipl"
    "xXLPTjnNJVpPyYvtDyYwqQWdYyarRxXDdALlIDdTtTcClZyYzvVRvVrLtmMkKvTDdxXtVqQlVv"
    "LPNnpoaAdDeELUwWuwxXWlOxXGgiaAITtgGyYuUzZdDynNYGwzZPkqQOoKklLxBbdQqDGyYgXK"
    "zHhRDhHdWzZwbBhvYyVHLHXxhlcaACIidDVbjJxeMmEXJBbtTjBVDdpPsNnSvrRbbBMmyoBbON"
    "nwWJTtOoQqIiVhsSHLlQqHhXxAoOHhTjCcJwWLltFfetTEFWwtTIyYiYyzZvVzZTOobchHCBQS"
    "iIsBbRDAamMJZzjoOJjyYIidHhQqOuUWwOovIiuUCcVoWPhHpwdDJjIvTufFUtWQqvVuUXxwtT"
    "iIVisqyYQiISHLjJGkKcCBAabhHiIKkWCcwpPcChfFHCnNlLcZSBbfFbBgwWzWwZaAZIizGcCm"
    "SsGxpNnPXQhHgGgGFfuJRraAjOouaUuTtAbRBbrjJlUtuUEeTCcwxXrROioZreOlnncKXxkCNK"
    "ZzaAkpKkRcCrRVvrPNuULfCcxdZzDZvVCfFsUuSreFdDafFAfqQWPpTeINniEBArbBRaeJjEbZ"
    "nNsSFflLzZztsPpEeBbwQqwWGgQqoOFfZzWnNSyYsXKkxWwKkkcCONnoNVvGgXxFuQqUkKfjJY"
    "yFeGgEcCnNFfLldyikbiIBhHKjOoJaAIYpPIMmHLlhibBIaJjyrRYbBjJAaASsVvFuUfTMNTtn"
    "nNmFfqQBbJOoCcXIhHgGCciTtSBMiImBYybxLgGlwWXNljuUJjJbBdDyYhHLQqxXmtTMwWqQnx"
    "XWwzSsmMkBbJjeRrsSEDdTtGAagXxKeEJjwuUDdpPdDWTCcKktjmMJjlLXybBYnNMmwoOfTtix"
    "XjJDduMmdzZJjclLqiIQciqQfjJvVFOJHhnNjUuoOoFfDdpkKPHbBhwWLYGvVgSXHsSFfxNnXh"
    "pvVAaSsFZtTFfzXoOxfUJjuYyRrYyqQvVSslLPMbYyBmpIiCdkTtKDcDmiIoOQxXBfSsFHhVtT"
    "vKcwWvVdDlOoHhuUQaTtAqLtTCkQXxEFfeoUunNHhtkKOobBEisSIetTWwTzrlLGgdDRiIelLm"
    "cOoSsYyzOoZPpdgGZzDenNEnyYXbBoUuOwHOYyplLPSsoUuSECdvPpcCVmvVLdDlDdoOUuRpPU"
    "YyurxXleKkeEEAaWwHhOoDrReXxEuULEelCNVvnjJIivVPpmwWeEAabBCcwZSwWsXxyxUuXYuU"
    "dXxoOhHhHggGIPpkKiNQqnSsrReRrEGXxrRfFgQqYyAZLllLzaAxbBNnXRrtTNcyPUumMIipSs"
    "TtYzOnNMmxXwVQsSvVqvJjVBbTtvoOQcCcCVvqWbmMBSkKsoOvVUCTtcuDxiTtIXxCcdlLDxXB"
    "FbBfbpPNfFnfFHhdDBkKowWObUUuuxOrRoDUuQqQHhHhBbqrRdiNnUuFfoOINEenNOAzZakoOh"
    "HJMhHmOoCcaAQIMmiqhHmDYwWdDjJXTtnNTnNpPtCcQqiInNnNXiYtWOYyNoOmRtVvlIiLIpiI"
    "PigetuqQUPqaAUrRvVugGuUsCcYySQzToOwgDdGaiIAWdeAaFfuUtuUequUQZzBbEpbBhHvVFf"
    "WpPXryoOYUcCASsauofnNFiHhInNKfFgGWwWVBSsbBbvMmWNnwQqDdnNDywWWNnwrveEVRgvVF"
    "fGgGfJKkjRgEeYyXxWRrwfFWhHwPIKCcsytTYyYAazXxTtEHheCjJQqoODWwgGdVahHAvcZgdD"
    "EhHuUSsYxXKkyXuGgUsGuUgLjJYRrsqJmMHhjQSfwWFrRaJKkRxXGglLrRDdHhIizZBYyJjbFf"
    "eEmvRPprXxYDdFfyrRMdDQqaAnNKsLpPjJleEctfTtFonNOVENnpnNZAaPjJhrREeVvbBCctEe"
    "IioOuKdDkXxYyJHfPpvVHlLAayaAYhVvZzFfFlWwBLlwWQqyYFPnNpPOopPoyYiIOpJpNiIJjk"
    "KnTcCEuVvtTtSsStjVjpPTCcfFteEtTxJjnxlLgGlLdDwWtLlrRTyIiYMmMmXmdDMDDddHhNuU"
    "vUuJjjJlLWAauUwmUppqQPAaHhtTUuwWJjYydXeELVNneuiqQkkaAaOoKtTkaQEyYeqAdDaTIi"
    "KqQkwWcCGaAQqUuIigGiIRrTLDVvCcdbBJIijhnNHsyYcChHmMSqQcCiIiDkgcCGOomMDkGIiw"
    "WSoOwdjJDLlsZYyzlLIkKivXxrRnNKkMmXmMPpkBrRoOIiHaAxbBXZrRoOzQqwWhXxbQHhqAoP"
    "BbZzQqCcpzFfJcCaAjuUzZZUuzqiIQwGgWjJWwFdDfQaAtTHlLFgGfBbBMmycqQHhSyYdMmyYm"
    "MgUuzZujJhvcnNCPpVPpHgiIGFHhOMmNnfFoGmMvIiHXzZDdnNZzxoiIBbIihUuDmMBbdXxXcC"
    "KkatTAzZFfmCUucTtUuhHCNnjeEoOVIbBjJbBXxYDdXxJjyzZNnVvApPpxXPfFtxXYBbKRrkyT"
    "jiQzOoEoOeSsLtTlZqyhxgGOqQTtOSaAskiWwqGlonNTtfVvvVFOLEegaXxAQRrJztTrROoQqZ"
    "jZzZhHBHhbeXxYQqyxEEexXYYZzyFfDcKkWaAkKwwWOozZIiBuUTtxXoOAHhsSWLCcLlrSssPp"
    "SIbBlkYyXOFCcfxXlfFLOoMmuUohHOmMoGCcgSsJWwjDdLlcpCcPidDwcOoCVvLbpPBEebmMSD"
    "yYeEdnlcCHhPzZtOozZdDhkKHTWwZzwhHBbtDdinyYNjpPJccZzVvYyChHRrDGkKUuxXUuaDdw"
    "QqKuUkLleSsERrRxXnNTxXggGGnNmMSsmpPBGgyYFyiYymMnNeTkKcIiCkKELleLmMgKkcClnF"
    "fEeSsdKLmMlkHDdjJuUiPdoOLlBbXsSxbBojcCJmMOCcUxXnaikwMmMhcZYysiIHhRrCcZzxXn"
    "NyYbQqAajJUurRQuUutTkKUnaAJsSjXRrpPeHJjoOhYyEoOeEUeOFCLAaRrloOFyoOYfWwwSrR"
    "sWwWMiwWQqWwIlLmAaHhRriIOVvQvqQIiotTcCVChKcCsSbBkKkeEFfEePdUwWuDRSiIsCcJAb"
    "HhBeFLlfjkKJqVZtiNnPpiNJjnvVxWwSsXpPINnKEyYpPpPjbnNTtaAWfFwBtTvVJrReOoEcOm"
    "rNnRuUoUuLlnNcYdDMmcCDdLlAiBOoNnbcCZzbENneIiwWsSDgmMGkKwGgEeHxXhyMzZrMmRmn"
    "epoOdDPENIMmiZzYfFVvAnLloONhHaFPKkpHhdjzZJNKkcmMCsSKkLWwKkRrdDlQqHobddDlUu"
    "oajWwmhHMXxLLlnTpSsTtPtTtNeSNnsalLAEjBbGgatTAtTJjJDAnNkinmMNrRSsIkRrtZkKzB"
    "bMmVQDdqvmMuBWkEeEexXKwwPpzZYydxXyYxmoONcCKkRWNnwZzrljJLCVPpvaAtlLwkKWTcbF"
    "fBdrRguUVXdVMLlmvwWVMyeEYmHRuUOcAaCoGoOgyxiBbIXFfuUroAOaAcClLoaeEODdRVvrHC"
    "ZUuzcDWdoOaAUuDOQquLOolUojJSsxXoGgOxMmiIDpPdGguUIidDIixXlLLlmVvpPHhQPaYyAk"
    "KptTXxXjJwLlrRPpCcWwZzqodDPtToOXxPporRMmHdDsSmMsHjJlCxXdyYDzvbBPkKpVvLEekK"
    "eEkSPpZzLlOVvHhCcosbBSJmsOoSNnNnPHhKkaVvZzoOoOaAeoOoAIiaUutTgVvGtTOhHGcCxO"
    "oexXKjJLlkiJqQjDdtTeqdDzQqAaGgZoOMmDyYdIisnyYnuUNWrTvVgLSsllLGgkCcTtoUugGO"
    "KGtCcZzKlLbAXUuWwrRxaexXEUuVqQvBYyYyrRzxXZvNNyYrsSRPpHhChpdDJjPIiKkHhrRHiI"
    "SswCcWwXgKkcCGxUukaAKkKszfBbDdiICSsbBZzwicCjJIfqQbBFZzBbRrJVDoOdEevjuUyYTt"
    "wGBoqQSjJsgGObtDdEeVrfkgGKFUuMmNJjWwHnNtFfTsDdtSsTZxXzWwSYmCcMIiyhnkPpsSOo"
    "KNVwWGgvFUuOofSAHhlLaIiuUAVsSvcBbCVQficoOjJVvCBbUKkdDugGjJPkacCAgkMdDmEeLl"
    "SsKBbKkXevVEZFfbHheTvpPRjJdGasrRSJjAgTJjoObBUurRcWwNnCMmFQquUvbqQBymMYVsOE"
    "eozIiLlZkDSsZzEYyhDuCcxXUOUDduKjJgGzZhHkoddDsSjGzZggJcCSsbrqQReEBPKrRHhkeE"
    "pjIioOowWOAaoOkKhHqGiIkKjJdMkjhalLAHaAlLxvaluLljJLlVvrQqrPwWwWzQqXxkwWXxKZ"
    "tSsTfFuxQqdDfHhFXpPFEembBbEqQeERCcDdxXaAPpsStuBAabGHhgwzZPpxipvVkKeEfoOjtC"
    "cFgGFfdnNoOKVvefFEBbHhkNTtKkrAaZzdDrbBRwwWyYMmdBbjbBfMInvVNEeGnNgiIdDDdRrU"
    "ugxXTtScAaCsuJRrjHhpPUGKZPpzXjXxuWwUcdDCyfFCrRcoORrDQqDdBfSsscCaLliCgGPrai"
    "vxXFfVIAJjRuUEDdepgGZyYzQNVzZvUkKZPxpPXGgRrpzwKsSlLkMmaalLmMDEelLcCTAuUfcC"
    "FoNvVnCcNcbrRSsRTtIiqeEeENnTRrdDkLlJMmjoqhHqQqmMQWBbwEqFaJjIiWwAbzZzCtTgGc"
    "qFtTfGgSswWAnhHNhaAHMKbByYKaAJEzmMdEbBeKkcjoOUTgGtuMmJNZznCvvTtQcCqVitFffF"
    "yhTtTtpZAdDaXqQxWwCvJzaAQqAauUZwGgWjjJnNDMmDdgEUaAsSaeEMmAubBFRrgGHqlGgLyY"
    "NnEiIrBbHhiIKktRrTRNgsIMmMZzmTtnWkKQOohHGgDdbBLlxXqwemjFvgGJjhDiINnHyeKeEm"
    "MaAxXhqQQAaqrRHtTMkKLbBlFfFMmfXhLQqzZMmtKSskDiIbBcClLdpPBbWECUuqQceLRrgqVv"
    "gGQezDdinJDdNPpRrAVvOoaZefsSsdDMmHGFCcfPsaoOAbBVePIgGYVaAOoAdDDTtdIiavAeeG"
    "gGpSscwWCYlLeEyexXIiEPKkeEdDAxRrXaGgsJvxhHHxXllLLhdDPiIUQlLSsqVvflLFVEXsSo"
    "JKkUWwukKjwWsNnrRsQqSStjJdDdDFfdcCAsvSsiMmIlLDfFuUIQIicCqtTRCnNcriVHKVvkhZ"
    "edDERryYyYnNzSqQsTYiHhIXxiqiIgYyoIinQxOoXxiIXVvdDIwWmUuyyYAaJjYGgdDrRSsAZT"
    "tzDdaNYyDXxdxgGgcqyYmMlVsSwnNWQTtTtqvnNqQbBqJZziHyYNnCyYIJjBbDdBDdTzZtuldD"
    "LjbfrRQjJqFBUNnujmHhcIocVnNdDaAtTvCRrgaAGHhOBbsZoOPiIprRksSKLWwXxIioOdHiIG"
    "gLExyYXmOoInNiZzMeGgfFyYIiEgGaAPpVvIYJjbncCNBTtyErRAkKasSeoZzOMUuAmNWwWFAa"
    "YyiIfRxBboOQqeqQluUwzZTMmDHhNbBnzZcCHMmhPIsSiAapDNnqQcfXxxNnrRZzXTtCGmRrtK"
    "kTMZOKknWyeNdJtlLyYgGEDdsSsYyCciTIgbBgGGstefzZZzZzhIijcrRCTtboDdOQqWwWmMSs"
    "uuNkCcDdXxiIHoRMmrENneDdIiVvxUuohHUuOaOolGgYUuyJqQVKkVUGBbgMlLmTtwWWwYyTEd"
    "DeaANnNnRrBAavRrVbHhhbBiFZzfIHtMQWwVXqvTtRrVksSjJDRrPMmhnNaApPtAaXxnNqsSKM"
    "ZcCzYyEjJedDBdDIKtEepPKkQqXxNdDnocGSsgVsmMVveESfUUuuFSvVgGrVvxXvVaFfiaAIku"
    "TtUqQjMmkwRrQqkbBKxZztPbmJSsXpGgHhPnNcJjpdYyDyqTJjeeEdrRJjDXYdDxXAaFxglLfF"
    "mRreEvpPuUtBbUusSPfHhydDYfRrXxqVvQKkYsSyMSSssZbBMUuwBRrrRbiuURrRIxQqwpPKvV"
    "kWfFEFgGpPlGqyEebUuBqZzWEuUYEQdDqLUhKkHRrXCoOwypPJjOoFfzGgaAAaZuFoYygGGgWD"
    "IskKvDdwScGgXwPTwWthjhVAaAakKvnbrRXxysSiUXMmxiIxoOmMweoWyhrBbRVvHYvrcCiIkK"
    "ZiIwVkKvZDdDCiutTUIoOIicdzkKzZhrRMmAaeQqkKBbSsOFfoVvEoOHGDmMKkdiJjuUUeTtim"
    "wWTJoOfSsFjWTtxXwuUuqfFiIQUhmMGgtTHllaAZzpLVOzZHxvhHtTdNnNdPwWLMNnRruUwPpW"
    "OogGVvDeEdnNzZkKkMmDdKnqQhFNYwWgGNqcYyZzRLlrpPkKsSLlLaAAaQsSVuVHhvjJUdVvDq"
    "HiUpsPpSKkNnyZDdAJjaYyYnZzKYyCcvNneEVwWTtdDkNrRXxaInNuxBOUUuuZSPpsSVHhPpjN"
    "nKjqQJuUCtTCcKuFfUNaAxMmgPpDdSEeszZGaAMrqQRvmONdDnpWwNnjJEePvGEewFwZzWjGgc"
    "CIiDkKXxmMGgcCZzkKdCcdDGGSobBvNnVKvVVqQUznlLZzZqQCzZvVmMaLlGgCcJjFMmfvVFso"
    "OSuCcyIzvVqQrCcRNQGDRFfYGJkoDdZzjJToNQqSNnsnDpPdqQTNMmvVfFcwWqTtCDdqaAQGgW"
    "CctTxXCqeEFfVsSaAULrBblnNmMpxXZOozxXPRrSJfFqZzIiwWnYycClYfFrRSsjJyLRrnNFtC"
    "czakYyrkKgGCkKtTcTtQqhwjJjJftYyqQJvVjTTtFOVwWAWwaPbVvyYdDWwrRBoOJjgGRKkUvM"
    "mVurhHyYHuUBfySsYyYqrRwWQTtepmMQikZfFzrRIVwWjoOJhHPpuUawzZWbjqQJdiIDBfFAhS"
    "gfFYlLLhoHhOZzmMoOgGDyYdpPFKkpPgGuUfkKEJrgGUuCEecRJbCcBEuUevmMSsVJwWCchdpP"
    "DYyYtZPpNKIikSslLcCfGqQrRvVQYuUxYpPUUuzHhUuTfFxquRSsdEtlduUDtTMrjJRhHPvJjI"
    "aAQqilbwKrRAaFnuUVvNfYhjcCckKcCcbBCaACmEehHgGQqHBbDdhpPHhnNqQHhcCIizZEespP"
    "SyYtDIeBOorxBXxbJjXKkYyYegYyyCsHhEeSGkKgXxGdCOOOuUobBbnNBVvxXiIFjJxXjqnNkK"
    "OojRrSsJQXxDdrRBRWJjFTyYtfZzTMGqQeEFfNnRrRDPpRrITWwWFdgGDfnBbyYfFBbReEhHfD"
    "dUAaJjkKQvVzZcCquyYvzZRrMgsKWNnwkkKvVPpCcAaFcgRYyrGqfZzzZLlGqMPqQmbFxXaJkK"
    "jFgDpuUbBXxPdGTyYqWwQBAaUtTucFfCACcSsakLMDdykdDfFUuRkKtTvQsKPRuUcFfkOoPpLl"
    "WwmTtiIQqMbIiwWBPCmlLMqQVagGAZzaJsSUQqbXrRTtGOogxrRlJjnNxXBcCaAsSsvgxXGPpV"
    "YyqtTxdixzZXGpNmrRMvHhEeVFzgBbGZBbnOgkIQqiKFfgAadDGBcoOEeCbWLlKkwXoRrcFfHh"
    "YyfyYFGLIKkBeELNNnbnNBbBbztTZtTnNUuBSslcyYnTtEeNHhRrpmMPxWwXGwWokFYyomWwmx"
    "XzZOxdmiTpemCcZzHZzyYetTleELEhjJHsSLlvMneElLBZFaejJxXEKRKkMmAatTrciXMvVUum"
    "xByWwYVaArFiIsSSNSSIiPAgAaRrRXKZdApPaNnDkBbIiCZqBwcCWvVcbnrCcHIigMmgGAabRr"
    "BLlZdDZzhHzOPmMCccamLTtYyTJjWMGgCcCCDdZkKlLtTvVNNnnzBvYyGgPpPAasdHhcCfFsSX"
    "xqQYyOoxrIizZPxXtTdUuDUKkLuRrBbUluUISIisifhHqhHGgWwSDdsDVvwWTrRsStpEcCuxwR"
    "wSsYJjndDCuUcCoMmOIeEisSUuiuUfuBbYdDyHhLvJjxXvVVlUoOZOGBhBbHbLsSlBYyLaAlYy"
    "izjJRrsLxhHIiXcCxvdDjJVGgSLVCcvAalsxXPsSpBbXeiwxNnXsmMhVvHZpcjjJDPpXEcCexg"
    "ERkZzKCNFfSAatNnTqQuOoCcUEeEeCcYPwWVvtdrRHhrRDkKVvNnQqoOIaAUuNeDCKGgkcQqAa"
    "JjmMKuxXzLGgRrvVvVPHhvVwWMwQrnsFYfFaAbDlDvVtXbBpPFfXxxaATnNuUfFhzbSVFfDCgq"
    "fFQpjJLlPynNoMmNNmMwWNQqsSDdnBbnYGSsjJgKOoPQDdsGgSqmMpBbkdDsShTDdtrROJvWwW"
    "pSsuUPwZMvVmzGgLlqQduhUuHYWmrfFnNyqQiiIIYkKRaBXTkKNubUWnNycaACCNSjkflLAdnR"
    "HhaANnqAxUuXMmaSTtsCcIiKkeEAjJaFBLlQPpgOlLsSrzYyEeeEzfFRRXyVoJjOvYtTPpBYzB"
    "DYyhAtGBbMmtTcfFZGfKkWwFWwgzIhZzFBGgxXxUaAiqQRzNLPgGUupBbGglTtcxXxvPGsIaKk"
    "mAaiBEBbnNhxBbZLlwLyYllspwWTEmMmMZmjJMKmXDdxvVHhDddkirRfxFfXrDdkkAalLvVUmn"
    "NHhCcrMjGnEeNSsgYybfSHhpqQUunNMEiRrIeEeqaAeEpqQGMmgVtTvPzZXdDxpaAPVVZzcCRi"
    "IIiryYQqfRcUUIiuohBMmRrfTZgtFvsScqSsgGQQOvBWmzZffFFtiIlEeoOUpPlvVLZjJHhyYx"
    "ZtiIOotTHaAcPXxpCdcCDzZicCXVvGbhGgZOoHhWoOJjPfFBSoOsfIsSiEeFVWnNKTcCeExliI"
    "aAqNTtnPnOvZpAaHyYsSUuCcAVvYBbyaWsSSBbQqshBbgGTNntHiIHJoOjicfFYymMCIhkKuHG"
    "gQoOUzvVKbBKAacuUCAANjJvVnzZwMOkKhHnYiSsqsSuUUujHfFhsgGXxGWwOUapcNmMnTtYTt"
    "XYwglAagwYWwNBZzblEeLGTtgOrljJLOoxsNnQEslfFSsgRrExaAWwaAMxXPbBHdaPpmMjJeBb"
    "ZzExXEezHRrNqOKkExrpZzPNoOnSsfFwMUFfCgPpGtEbVIiIkKoOiaAiZzBHnEuiIUxXQdDCbB"
    "cezsSZQqfodLlQoOkKdDDdiIyqQIaAkxtiXxaaAATnfFUuNVCctQqzZTnkKyesSOlLFfBbkVVe"
    "EXqYyYpkiILEEyYecCAhrRHHhYuQqUyxXFfaNnxXaAHlLBhHbBEqQRrMmetTWwbWwCEtLloKkO"
    "txcCXTpPTecSuUppeEnOoNAaEUueCWDdwckOaysSMuUlskAGgaKXazZIBbHivVIHhtlMqQlGgL"
    "FqBbQVCcOhHMmOcLlnslLaASYyNCxtSsUuWCciCcSsmwWnhWkZzYcCWGgwQAaEePcnNaAtTpPN"
    "QqRrnUuIOuULlTtJaBJjNRsPBcCIGwNnWCsSGgxoOlDuBbqkKKkHdDgGeNnlLROLoOlXFSIisY"
    "ynNSsNOjzoSTzZbBQfVaAvFxXiMuUbBOBIiLyfFIqQiiWwAaEFfuUCVLDdlvOOEpPgxXZzcwWe"
    "EnNhHCUcCUuAatnKkWwRriINUwlRrLWuwzRrZToOtWZggIiGmJjVvMSaAXiKeEkIxivVQqSLls"
    "lLSsQtTqXxtjLNnkKxNnzqQvVOoNxIivlLpdeFfExXDGHdwwWJjWnXgrRGqQlLxTtIiLlhRrMm"
    "VNNEeAQqaXBMcCfrWtFfNDdNSscIwDmMsYtDdTACOfFolUDdszHSseEOJevJxXjbGSsgTtSWwH"
    "RGglLXFfTtcCaAZKKNnVvEeQsRuvPplLfFTtHhyJjHhEzuUiIZYhSGgYVTaATTuUttfFLluUbZ"
    "zBCngGUxwxTRrHmfFeEfWwVvrvDEMmuUuLHCwWYyNncJjHhOoRXxvsShHVrQqiICbBPpIpZjOI"
    "AaibFpPZtTKGMHhwAiIKsSsQzZliPuGgwjAWyYoOwFIAOoFgxbTHbBhtHhuUIiGTkcXNpPQqnL"
    "SsGDagGABbdSsKkNMmnzMNRrzFjJhHfIeEeEGgHhFfZzexLlXNZJrRkQWkiILlFfPCcJQqgFiD"
    "SWICaAObSMmsAatHhTaMjJkGbBvDdVgkeEhkLZfzxXnNaAlNnhMVvdFfbBHhgBwWeEbBbUucCW"
    "wbBeqQEyPpYFMmFFfhHLVvzZAGzbgGBSQqQvPqJjKAanZzwPIxXUHrRtJjaGgEesSgGIPsztpe"
    "sSMmgGwWezlLnnNSsogGOTBCqFkKaHhSgGqlLGgNrVxXpbBbbaALgQqOoVCSslLPprgaZzHhgw"
    "UEeuDUKeEEekcuUAacwKdDHqQhVvsHmMtThIUWFfwEMdDGXxhZGcFuyAaYdDXVfFnnNmMnNXxp"
    "PpPVvAgGfFaNSsWtAapPTAaNqQZznyDPcyYJtUZzhDJIiKkjIifyYFtveCXxuUcCiYyxUuXcBo"
    "ellNnWPpPpFfnLlNIUWXjikKRrCYycfFjqQkKWaZzQqbHhUalLAbCcBGhKwWkWlLcmMClUuLEe"
    "TtVVzBdKwsSWmjlLosSOKkBOogXbzRIYNnNNNHhnWwJHhfunVjJjnNDLlUeEilLSmJAZvVVvza"
    "YzZLlyDdzIFfyVvVCchHgGQqevVEpPnNhrgGWhHwlLQqsDdSYxVvvXxTEbiIBePptlVvajJFfG"
    "gskKmlLeDHcCfFhtuknEomHhMmZbdDBtTLsdGqQXWVvwrRxggxXxqQQqXGxXkKInNBazDvVbBt"
    "aRrUmNnLiIgGrdjECiyGWLsSmeOuUoluvcCVcthHDMmIgGeRrMmXjYyQMJYyYDwwXxWhjCdFJj"
    "rzZpPhHpVvSsfeHhWGgMZzmbBlLHhUeEuHiIRDdqpPQkKliIKkpPQjlLyCmpPcIiUuQqjKwKYy"
    "kDduUiEgGZpPtpeEPNnTlokRrKmMOyYQBYMZzyAaboOLZKfyYFXSaAmMYHhxXgZzGySgqbBgGo"
    "BJjbOkKYyaKfPpOoxXyUuVRrvYQqFmMeEYyWwmrRhivoCcwWOQaHhArRBEKkelLOodDRrgzZiI"
    "uBbUNnLfVooOVXxvOvAaRrBbFBbkXqQrKBEOoebkgyYKnNkuUWwGRoOsSTtKmZzAaoGguBNnBb"
    "mMVwvVPlgHFZIiSTtwWsxXXxUuWrRvLlUtTfhHUuSsIYReEUurCUPIgIzlLJjZiPwWeETthrUi"
    "cWwJjFvLGPpDdcCZtTzhHcrcCRCfHhFlLMbtaAgiBDdlGJoOOoEeeVUbBKkMHjYywgQqGWveEp"
    "PCGgHWNnATtitTPQkkKKaAmxXdDMgGqRrmMKkprhHXxRCWwBqQdjJMBbiIOoSsCcSgvGgXSstd"
    "DTZzqfNnFQeETiuPpPpkyYnElDaPpyYAdsKpfOPpoUuJLludDCyYlxuUXeIhHXTZViIEecCviV"
    "vIzZzfZzsUuSNnqLXjJZUuNmMbBiInzxhwWpmXYynSrRxXgyjJYOopPGsaAxMmGQGNOSgeEMOE"
    "eEfoTtOlEeWNWwLlaRfMdDwIlLTxyYXBbWcyWAaXuUxszZSaoOAZjJBSkKsgbUuDDNKumYxXmN"
    "nLiNnFfIhRvzQZzjBKkbJmOVBbDpPdtTWYKdeEjJuePmMpOoVvDdbJjLlzZFwzlWuUwLnhHpPO"
    "omMcAxGLlOLdDsSlkKiZzwiZzTBEeqQbrRPplnSsNLUuBZxOBiIDdoOjJLlugyQgfjiSoBbOmM"
    "YeJEejrREZnNCczMJxSsbeEvVaVahHynNuNEeqQWtsSTbIAaYyMXiJjIRryYOoWsOpPPIdRAus"
    "EbpEcCrRrjJpPBYPpLUeEtfBbFrRZHoxKsSfFvFBbfVaAmMZzaAbByMtTGgQqZzTiItqMmxnqN"
    "XxknjkcCFDdJjVEeAmMbBSsavhvVHUCOoOJZpHlLvVhHHhPpQlLeKQLlqkQOouUkvVEeeESsHQ"
    "VvZwWEezUVQAQqNlEHlLhewBYSDCUuNUuLlAQqKkBmQAVZWUdDyXxmTtYyRGgTGwWBKkGgHhhK"
    "kbBHGgCMkKmHMEemcxXoJxedDEXsSBqQRLlYyMGgmeERkXxzZUvDMnNJjmWwhlphsHAigNnSsR"
    "rNUQLcCbBkuHhgGtTUNpVWtKkTwivVIGdrFpWwPnLlMmFgGvVDMmqQdpleEaLlAfFKkwWLxXxX"
    "iIzsTYymCyernsBOAaobBRFfayYwwoOvSsSsVWNLYylnNHhFhvksHicpOcaegGHhevxyjjJaaA"
    "fPmOcivaqQfhAagGHEiKrRFuuedDfWwHhKVgdDvVUJjlLTtxgJhdDVhHhHyDdYVHhxAVvVDdva"
    "fFfFKUuTGRLqQRrMmrzQquUMmzZZkgGvNrRZWxHhXWmGoRraAxXOmMXqQKCfLVvzvVNnpPZlUu"
    "hkXukKUZzXbNnnAcdIGioOaAjHhpXxdDRruquUOlLoVTtvTRGRNgGLkKhHJzcUrqQRNcCnDrTs"
    "UzLloKmJjThnNPEeptTHUurVedcCMpPHhCcoOAaNMiILlyYOsSjJqQqQfbBMwtpPKjJkWwvVTC"
    "cDiIjEwfkWwLiIiNnBSkrkIiEejUqQiKkicTeEdDPPpBbhHpBbGZzgJaAMmUxCcPPlKkLKkjJK"
    "kzZUsSCXxhHcbkKmzYpPyCIiWwcZzSsUAkQYqQEfrWcVLlvyVutTUoJpPibSZzQqYdUukGgKKM"
    "QoOqaAwRrlLiIGHhkKxjJXXKQNnuUxhyYopUZzZzKIiYyFXxWwfKxXwWqsSuVvUseEMmGqFfGJ"
    "AaPgxyhHYhquUQWwAAaXfFxEecJyZKkKqQdwxXWPppYCcyPqzbiIunxXfRiJjIebiMmYyABFIi"
    "zBbEeDdZMoOmfgxTdoBdwcXxgNnxXOhHVynwWVjOBbVIiMmYggOiHhIbntCosSOsSGSsgGgUuI"
    "qoCaUuwaAPpWAchWaAgGwHOqsXKyPdHhkKcCDdCiIvxxXEgfmMFaDwWghHMmvrbtJjTINnlIAg"
    "GuRrLlUiIMmairxLaQQuUqQZvVzKhtTkVuUuUvKbBUulKXAKuUkaMGgmonZzOoUuXxyYGELlZm"
    "MlLWmPUuMmuUAaSiwGzgEwvcLlEeTtCYybZznSsNZiImMwXxWgxNnqQJDnUIbqsWDdqdJffEzZ"
    "eQqEfCcFpPtKkTkiIUkqrRXywWwxmyYqQCjJBlJtuUoZjuQdlLdzkKEGgqQsmgGgKkZzxXkxlJ"
    "jKkLXKdsSUuDEqDnUsSfEmtYyfQqmMhfFHCQpEvVeqQMDjhODtaBvVAJjadaQCcqVWwpPsNkKU"
    "qQJoOvzZSswWPpOozUijzZyzRQqsAxXQpPhDdDKkfiIFsoUSoCgWwwltreEOoRcCgGweEuUrVf"
    "soVvaoCcONaMmaAFMhepXVnQqiIlLNtlLCvSUuIiskKZUuLqQrgGzuUVvspSalLDPpfFKDdkdr"
    "erXWwTMaAmvWtTdDhHwnUuNXJFfjyYIRrDNniIdZzoOXcQqczZxfFXERrheJjAPGgQqmMXLOol"
    "UulZzdqQalfGtduUPAUuAWzwWwHULIiluAbBavqQuMmguuNnyYUysqfUGgCUvZRwMmpjvVvKki"
    "AaNmkOVPFQqiUhtTVVvvBbjJGbDwWdwkWIxhyaFIifAGTnwQxXWDfjFVvqFgXxdhHDGrRJjHvk"
    "nNKkLVvlCpPcBjJrjJRrbBRhHZCkQJjqKtoDdPnCSnNsCcEjzhnYyEeKkNdDIUyFLosSkKOlBs"
    "SSKFKkKkVgQqRrMcClCVvhHcozlLXxXOoWwqQuOWwWmtTIiyYyaAPpYuOiepPEIoquUgGuRvVG"
    "TFfvSsVxPIirVvVKFMmVCIyZGJlLjwHmMctTldDJDMmFfuMMIHdLemRhHrSsrfYNEjcjHwWWHS"
    "GghZznNSsHDvVdWwBIiTuwWDaLlkKyYbBTERgANnaEelLzGghCaZAabBhTtLUGguDFRrGTtgfG"
    "zYyZzxgGXZgFfKQnGOoRrqQxXiItMhSIzZNniObvVvVRroRrqaAQBUvVdCtTcjJkKFFpCcCcbB"
    "lLPsSHhLnNzNTStPXIWwitCWlLNnweEcrkHhYPqQtTpXcKwFWqQxcCVulOmvDawWAzNcDvVdEW"
    "wQPpqWwjnuUIdDiCupUBbRoOrpPPvVpFLRcnLJjlqQTVhHAHfAncCNhHIiaoMmpuUApPaPADrp"
    "gwIBbMGgvzZVstTSmAtTivnmuEeZzUbBnDQfFSsqoOHhHHLvVlrOoRbBLSsUuUtThnNLUulMmH"
    "kKuGMmPqSwWwqEKkegeEODqYkMmKbjvRrfFYyVfFeFqQfWoOpPjzjPihXiHhPpHhWqdLlDrRKN"
    "nXxlvvVCuUcZaADdzUUaAuKkGiwxXYyCcWIuvQiXiEELEekbBzZKaEPZfdrKiIWrxXovVZZAZz"
    "xQquULeMjPHRXdqplLPqQQdkgejJxeEyYXYyIJJladTDoOpapEeslTtkmMYatwQqeEbgGgGsSP"
    "pbuBgFLOolSwEpRrENEWwteZCczPNnHRrjEeAafFSkKZzESOokUDAfFVvfwAaWIiUPpzgGuAan"
    "tlHKiLDdnNXFXxfxbBoKdDkOSsHhSTMLURrTtucePRrmMPpoORrhHWwUTkzKRGYqhHQzcChOHk"
    "cyoOEjiQqnEegdDNnGOoRjdDpMTwaArSsWWwnzXZzAMmmMaDdxFqbBiIgGlEPpXZzpPDLkKVvl"
    "mPpobeaAfNkKDbnISDdYSsnNRrvOoToIZoVvGjxRrwVPpPpmPCAvjJZzAlCcMmYtXZWrVvWzMU"
    "IiVVfZzsSFQjJhHqQNRrkKKUOouIijMCcXxtCcrbTtCQqXeIiEDEedstDdHSsfvVTtZyYbHhHd"
    "DMTtovNWwPaXoGHyYFlLNjJJjnLlRrJssvVSniINScCEoeRrAWTtoOYKzVvdxRPZSDdsKqQacC"
    "cuHhCGgcHHsqiZzIKkJvnNjgUuGFUVvufoiCIBIkKRvVmtlBbeExEqoFUumMJHxcZhHJcdjbBJ"
    "DwWdDZqsSXLJjuLzMZwGglVqSIimMkOgmQOCRrcCcnLwELltWUuYRuDtLlTsSeakipPYsSvVyI"
    "PpKGdVEenNvxhHXXgeECpiIqdDFxLGRraZzEfFeEeTzxQkwaAWsgGhHeCHhpvwYWDdPpuUgGWS"
    "sbBwtTwuNnNZzuEJjJNdDVvyVvYneEcaNnAZOBbmMpzXxWkKwOdizMTACuOiIoUQqXoOwyyZzJ"
    "jUvkZhHpPkVtTnqmAaMyxSffJMmZbBrUuRuUCJjpPPcRXiISnEAaDdednqLlLOMmJnNWIuqqQY"
    "uUyZCclLzZfjJoXxORdpPaADQqrUnYxXjkKrAaRJNoOnJdDjyUuPAapXxNuBvzmfMpTfuUiNFN"
    "nmxoFBbfOcCDtmMTnmaQRqbrxOgGgGHiIUCcXzqJWvcCBhHfzZpPGqQbqQFLNHhIinNwWbtTxq"
    "QuUuUIdrpPRHHhRrRxlXtfFHhlzZxcgGxXGgxKkYqQdDyQxjJjuNnhIodwVfjJfFmYyBPpPoOw"
    "VIeEiezpbkBbaALBbWwllCvfSsdZOoJjWuDdUrRvVwlLzyYvYpPjJsSUiIfukuUfFmtTnfKFNZ"
    "gHnPpvEezcbSHXxhsBihZzByYeHhUeuUVvJTtmyrVkEkbgDdwWfPnNfFppvVluTVLlhDdiIbBD"
    "bBqHCcCcKAWwXnCMkKoyjTKsSAtFfZoOziITtNmJQqTnNigkWuUTtwKYeDdECcVXqQdBbhHQsB"
    "WXxmMaDdtTSnyYOAQuJzUCweRrmKFrjxrNNfFnndYeEKkrpPlfFfaAxmMXxPCthHwbcCBEKLev"
    "GLNTDihkKIvQdnXcaAxXCVtaFleMDoOTtQqxXRpbBPRcQdyYZxFSsnNckwWQTQWwdDAafHnFNy"
    "CVvdRrDPWwwZeujJVvwBbWHhkRBVpPVoeEOPpPkKgGrLnLltlLTcCeYyQdgGjqSfBGgVkKTWQE"
    "nNuBbHLVvCcVzZJjNZzAkKVvcJjCcvifwIDdivtTAtNndDFwlOolLjJzAbvjNPxXuFYDdyDdrh"
    "tojCfFcuUUuoOTIjBbxxXXSbBCciZuxkJkKxNDdDNnbBeEdnRHuUsSbBTthHcPWhHsAaSZzZxA"
    "aiqzZfFjJeFxXrnWTtaAIiiScqQuQGgqQDrWwRlyPpYLtTEoBbOpPlLNrDsSdzoWwfEeQDdqsC"
    "cSEkeEBjIhHFssapPtTcxXlMhHmRrMmgRrOohHYKFfvlLmEeBnzJVRxTGHkkKKeEqRrSZPzGgG"
    "aAgZplIyqAamMVvWdddDDKrpdGCeEeVZWwyYzKrLLrWMLNRzAoOagiIAaGNPdLlKLENsRDGwVV"
    "gxXGdAuLlOOnDdLlaUSSssItTmwJVRfcKkCIimMxNnoUhGgSpPjwuceEeaKkrRSscCuUUcZPpS"
    "jjQBvFfSLlHtrRrpffZzLoSNACcnNTtagsrRSJPJTVqQvYyCqjJQRAaBblTVvpEetbBwWLHGZz"
    "LPvVtThHgGCqLzuJjUOCBbDHhHCaXmDsSMJvVAbqQUHhuQqdDGClLwDEedWOoIYwQbQcuJkmxX"
    "MKPUuchHyYhTxTtSsldRrkKDXqQEuMBTpPtJedStTjtTAwWvVLOQZElsSoSbTToJjtTkFYyAaZ"
    "zOyVvVkdDKJKSCcIYtTzZhDPYTUuBboOcmvzzZmdhHGajteEQWwMmMmpPLJjudlLFfHnTxXzZS"
    "bSERrehbKkbdDgGElRKDcxQqXhnBJjQkbvVvqQuFDdfCctHwPSspsSMLxnQboInUAMNnLxPXxb"
    "BXTrgTqLluwZzqQWzbDdsSxXMguTjJstTioOdfFDIRAvhWwnGOzioOIJQqJGeEihTtVNnvfFtB"
    "bwWLxfFhjJHLFyuHKmjaHozLlZNnaFlLWwrtQybBkKerBbXiIgxXAQYxrRxkOqpPbBCcgedDxW"
    "vVhNpBbjJgGfpOoPjUqYjJxhHXtCdvBaAuTzZKpPJkKLFflIatJgGoOrmMRYgGPAJxRlLYrRUu"
    "iILpwQqcCgXxLqQvlYEQBTttRroOCOGPpJaAgDWxAmPhnQqNHfFRjEeGBbCQTtqlUuqQhHAaVq"
    "QnSsNvxXSsuJjURrMmxJjPWYywlLpFfuUwpLlHeJbBjVyYOokQBjzaABvVGIiSdDTtvVhCqSwr"
    "RRRrZzZzonuUNolZymhZqbTtTbBjJVvYyXxoGgzZDdXOhHfFoPwWponlLAUNlJbBjLYXAUuvsS"
    "OYypwsLFfNktkKwBbIioKtBtYyhiIkKHKkThLkjJFaAEzZjJlKMmkKXBJjmjJqGzfyWwnAkDaA"
    "seEUuTtwWsRcCFiINnjJIrsSLlrakfgGPpnTtMfcnaQygGRIiRWwWcKkflkKLBbYyFQUuqJjgG"
    "pPvLlBmFWwpPgGxRKQzuFBcwWRUfePbCcBAquUngGgBVcEiQUuPpDMmyxXYyqpwWiJOoNnwdNV"
    "vSLOaEiIqQezeEZHcQoPClLLuUlcuuqQOoVvsUVvRIVvWdDwwgGaAbXxIKyMmYeDOrRDrOyGgY"
    "aAuzLlNZzAMmpkasSQqNnAxCcXmycCgkZzKvVAYmwRUuKyqytAaDWCqoNiIKkSsnJoOTXIitrd"
    "DRhboDxaAaUuhzfJjQqDDdxhkrbhAaHJjuUGUlVzcMmKIJjiQRrqkdRYQTuZeBMqPvzZOrRMjd"
    "SCDEeVCcEEeULeEEwXxGgXhWeiMmIvVbBJjnsSEXpjJeEOocCXpMaAtTmPaQrOkKoRosXbBXxx"
    "CjJcmMNWwnlgeLQHcCCeETtcRrPzWwsXHjKkqRGouUOoFSTBbbTtBBjvOormMRDdOoVwSZasqs"
    "SJqQNVnMmepXFcCXMdDTHgfFrBJjEQNRrRrhHoOoQCDAaCcdygdRrsCUNwWnXwWxrRoBbiAaIk"
    "aYyTyAaZZhHDVmLuUtPpTmVxXLyYlLohmJQqXkziDpLSYAaynNuDdTtVvEeMmDcCNfFJLljZHh"
    "AaqQWwLlxLlWneErxgnNoNnsIzdtTjJUEOtFduUwWDyoSshLrPbVvoOEeHhvVyMmYSpzsSZPKi"
    "IbBkNnZzLItMzZhUuSsQqHfnLIilYLsmMmYyHhazcmUuswtsKkBbvuUJzKOofFaApEjJgGUBbX"
    "xeEuvSdDsxhHqQXVeeELubBjAFTtfaeEgGtEGzQQryWlLwQYoOcyvzZRXxratmdgGDdmMDMFfm"
    "AFiXxrRwTtzoshjJHSuuiodDOIyYWAYZudzrqujtTmFlNXxABbzZCcdNnQqgriIGkLlPpzdtjl"
    "LPWwkpJjBbawLlUYLzbRTrtTxiPoVGgWrRlndDNEMDRrEpSauLwYcCyWyYtjJTiKkIPpAOokSs"
    "KHMmwtkKoMvWUulLMmeEnNevMGCeYsSJjPqNDdnKkGwWakJPdhbzIZzFIiMXLlhhNmkCVvZMBE"
    "cCvaQXhHKBzpDJmcCMmMaAHhZLsShMmrkKRuHhRChNnHsxMJcwWDdFUuPpJWwHyYGqjzZnNcWP"
    "lgtXxjNSjfNcCjJnPklMXGgkEsSeKSdvVmMDrPiItTpDqQAAagPHxXqCcALlZTtkEGnOaqzHxX"
    "sEeSUMmRrzZgGMmQqdbrsIzcbBClaAHggpEsdDqQwWSHSRraADXHhhHsZpPndDdDllvHEhHehV"
    "MzcCkKZcMJiIAFPpxAaeUlBbbBzVcatHhpgihRwDhHrjJcmTvlMNCQqwmXxMTPpEdCqQcQrXxP"
    "QoOqpDlGgLkKIiXkvasSAVKhNxNnjzWwnNZVvLgGHhlzTULoQqJbedDZZyHhAPVvVnYxYSOeCP"
    "JdDdDpEWwenrRQGxXMOoZziCcIsaAIfBHhkrRSsYyhHCMzeAaeRvVqRGNnuJjUgrQFlLOWTHhj"
    "JuvVGSpDdYZIiXntTNcCAtTrRwikKiwWIzdDZqLZQqvVvIimRCQqnNqaZzgEzAhHcCkGIiaDdN"
    "NnmMSsmMrkpPcCKdfWZYZRrsFfwSFpAuvWRYNbCMgGmKIXFfkEXJTXFPUNzkaZzAKeLEdDPpVU"
    "KrKmMgpVuUnNpaeEgGzQYLloOpPpghFftaZFfVKapWPpwijYyJdZztXywWYxjSspPCAaJWSxcP"
    "pCXlLUhHuJVcsSCBXUuWUlLhQLlUTtOZODdLdwLhxnDCcdVXwlLoaoCtNHhKsSDdkKRykqvQqW"
    "UuUuyclfFLlFACzLtTlWKZpSHbaInbUurSsUuxCcXRJFJlRrLhigFmJYhHdvXlVliIvbHvaAde"
    "XFfGhKWNpPnpPNttTttITtwWijeYysxXLGPvImmtnNDdTLimyLuSptqfFkIYKPMEWUApPaLRoY"
    "THfFIMGgMyjPprJPpiyvVtFfTXxrRjhloOrkPpUdyqQsETLlqTrJjLQqvosSyYHhWYFCiaPfWw"
    "gGZzFpAIwPdOPhgUuXKkxbOpPuQAaqFQbBzZqUgMmBUrRxsSwlLUuTttHhTHOGgFGgfnRaAeEn"
    "qgGlsSPKCckYyVxXXcdDCxQqvpqQZftWhnSsWDtAlLaTgOoxnxVlurzZCcsSvhHVaApPmuzZuu"
    "UrHwXHPFlOzoJSufFPHAaiZZUWoOvMpcYwWOCUmMuqzbBZWJYyBbjaAzZpreEuUoeyKkcCSeEc"
    "mMFslLYykjJKWQyYBnpMOvlLFOrznEYPyrkwJuUjQhEhqSspPNnKfdksLaAVJfFWbUSDJJdNJo"
    "OTNxdTtEvVBXhTtwbeXkKPpuUYVNNnDYxUuoFyYMGVvQqOSOFWtORAaYqQfRXxrHhoOYYyOhrM"
    "KkSmMjJFdFfnBhynqmMiXYQqyxAouRnLxvVNnPSEOiZiIcBILKBbkLyYBdZzDDSkKHhHygvUlt"
    "uUxTtCCcKXnOdIwWONnJyMOKPNlxZzpwdDwfSAHuUhCXxtTJsZrkbBjJxlRrUjJPlIjBbJxXMm"
    "jJizZRrETtUaAfFJjAJLvVFfVAuUbNnSAOENGToFfuhHgDVYyYmMaKkeQbdzZGgPIrbsmeEpPM"
    "MmLoOomxuWJNnFfcoYsSgGyxHhCjJttTxwowRmMzZrTJbBDlNVsiZOoAXJjKOOonIyYiwWRyYI"
    "aupGOPTWfjqQBJbBjKiICFfdHGzFNnssSCdthKSoyIUSUurhebMDdTtINnAsSsmaAQaGQqgAiC"
    "qdXZGsSUcHhItTkKKqpLszXgGxYlcCGEesvUHVyRaArPCAwwWKfgMnsiIXxQqmMUubtSOPVSZG"
    "XxgwPpWMzhMmqQbZzuUbTtYmWwMydtzFvVxXflTJvttTTPpDNThHIhJjHTzpcfojHAvbCBqCFI"
    "jJipPiGKAKkaSskgYoOuYeQhHCcSWTmnIZyhHainyGgZqQqQeVXfAzChHcZyYwDdZiIWwzOkIH"
    "yYnkkYdmhGPxVQXxqJjMmJjlSvHhJSFtNnWKEejfyYFAjJaWJMdgGDHpUWOocBbhPRYyZDafMT"
    "tAHuBWyNMlLfFepPsSbQqdDLADQPBrJjsxcVvlLOeEoxkWeEVJjvVMxXEVOhqQDdFamhHFXbPm"
    "RrsSSVYZFEeONnoQZeZgSsOodzKTtTmMpwWApMIurMHheKOEoOefFokEHHhPpAeEVvOuPRoYLI"
    "kKiQNgFnNKwMDdRNfFRrcKZhUiIUTgJNLxpnjJrKkjJiGkJbBzZMmjkKocCOgGNNLzZlXYLPoO"
    "ANcGbWKhoRPprSZaAzLlzZuexmMVNnsLlSMmaBbFfAvXEvNhgbTkQPaAhnCcNnOoNcwnqyYkcA"
    "IGjcbRrMmdvaOFfogvuqrsdxywKLCCcVvFfMwMdLCcPaHboaAXIOWpPJjIDdQyxZvViMZzPpkR"
    "NnwbBRXLGgXzZHhqhUbJVvtTsSGIfFLdQuUqFfzTZJnNjzrRGgtHVRrFwJyFpPeWzZfcZMmEYz"
    "tWlLvwWrlafPRouUOKVGpZznNPeJwbATyAadLRRWmFfMWTFgGqQERYyitBbgJjSRICQJAawWjw"
    "WORYsgReQSsnNqxYxXLlSKksObBVaAdfFDOowoOxfHhXxFUsZgtzZTgIyPrRUuFpPAafpyMsSr"
    "JphVbBwPIYQqmDvaDZpMmMnNmzZLJaCDQbzZpPWXWGgwXVRVMAasOFynRreBbPQqfZvgjiIJOo"
    "aAhTtaAKAakyoNnOBbIiJjVvVuVElcSsCmdDYyhuaysKmbDqQmMVFfTtGgXxnNodHhiqkiMmYR"
    "mMkbIiqLaUhiyYmXBbgGPwtTqYyQWpwWYyLlYbfkeiHIsSsSyLlVvVGmMXeQkHMGgGaAkvVWwo"
    "rZpPzmnSCFbAExaAuNzetvVTDmEWJoqmMhHYcCrcymTNDJroZfArRzZecaRrcCVkKzaLwAqAaQ"
    "NlVvykDqQqlLlQxikzTwPNBbXxUuxXnprRWtnfXxDhaFaQqCHhDCctBeEknLnBhRrMzZiqQInL"
    "lHVmMoEeSsCzGLHOADuUkVvcCBbEhhQqHMvVTOvVRIEsjCcgGrEDvVdMmPpKJLuLBCJjcfTtsT"
    "UZUVdnUAgpcCPGcCaTYSsgGXInUueFzCVVtcCcZJyKaAdYYvAajmOEWTFEeJjfdhHMyZvVzWMv"
    "VmWTgbLlqQfcJhcJjFfCBbwWFDdvKBbKkkFFfDlLdfvVJdLkAtZzTbchHCcEWpibgWIYyjobBK"
    "tpUuxXZzPOadpPpPzZbSaMwEgaAWDUPpusVIQqiBaAlCUFRSCcUtVXxrRBbLlvICEnTUHhhsAa"
    "vVSHctzVvZDaVvYyiMfjSsJFtTasOoSKkbPpUrLuHCchFvKNnCNnFdDGBhvVZlzZLzHbpPgfcN"
    "nkAaPpVfUlRubBBPMmZzpAfFmIUuVvYyjJAdxuUXTCyGgYutNeceEiTusrfdDucLbvSFfdwGeW"
    "mAGSsgsBDAoTkOsSJVLlvZIiziJjyYwGBIPlLweCBaKlDRrjVDdMbBPpmfHuUjCFqQBGtzZwmM"
    "wYmDnNtweoMJVyyDmNnMlLkYjXkKxzCGgTvvcZsStTfEbBNixSswWFfytuNDvuvVmVvMzFfBbu"
    "GgtLlSJjFblcCUKenMmNjJTtEkgGljbpPBkDdeRJSeirotmasSAqgGQHeKdaTtohVvoOlgRrZM"
    "mYycOvhNBbmHbNcClNKMmkKJrRjbnNTdjJcAfiIAHdFNFDhHdfZKvuUrRVIXDdqLQdKYLnSsaT"
    "tWlATmMtZvACEaFzORjdntfFMYCRQqyQEeOmGgMjwePpMdtEeTEZnUXeaBfcsNMROKgmhKqMmE"
    "xGgQaAoOJjqgvYihIEhOoHKbBfFFByftTFWwxMIqQLlHuAlnNQBKryIKQIDOAavdBMkSIxXiYA"
    "UHMLhsMmSHUbfFBuevUvOoUVvuLloOYlLcCHlsRrLlSLGVzFpwWEvVNYcCfCcoSmvrvxFfWwgX"
    "xGxwCcBqdVvcAbBjloOPzdAVdcCMmyYMyiQqpWvHPjRlLmYmMsSyfPpFYYiGnDdNGzSuXWvoyX"
    "EraAGPpSyroqcQlLqirsGfFTRrTtIOorefEFfeMmtwwrrlDxXYtayAaYBWRrjZzEVZzvpPgvCc"
    "kiIrpFALRVwWwTZyezOowWQqCFwEfYjWfSsSsvhJjZQAdDaqDligsSgGBbXxjBuHQxlxrWrKmI"
    "chHMmCtTzXYqiuUpPPpwnNoZzixOBhApNnNpPnlDmWmBbBbclkWYXDSRQUuhHUVZzGAVDBCJgi"
    "aCKQNWCHpqKtHhBGHBbnYmrRMyVUBbOzZoJiywWYIEejspPYyOHZvVzkwBgCnaplyxnngGKgIC"
    "cRFWwfNYyQqPXlnjGtuuHlLaAzkPpCnrmWkfGnqlyOrRCcrHhpUYyoaQqpPhmRMmUimPIiaoOt"
    "TPtgGkSsZMxXmWwDGzEzqpPfzyvkpPuUKsnDdLlNMdDwWpBxfhHMAeEfHovebBPQqpmkKDdxXn"
    "NvwKXCXSTtRbpMmZEezqdaCclgGmMvVBEmnYtTwbUhaKkmFAdzrMmpHCXxwuiFfIPSRrFfsxXh"
    "mjwJPpYykLlyYwTucCUfsjVsLBbvkKXpgHMDLliIyKGgKNhiKoWDdVvxXdDyYaFxvwWEiLlIQq"
    "wWztTYNIAYzaAiNnNMMmYytwsZzoOqEyUyIfcQbzZcBVahJOFCsSPZtitndVjzZtLWwZTDrRBB"
    "UuYyXxZzHZaAmODdSsozEeWJjwsTtsSvppPosTBrRSEeNmGFkWacpYmMCcvhAaumMVdDSgLSsy"
    "XxQqQqZSlOoKkPewWEQkiCugzxDQcIagLlGAlLnNpPqMSaelLWgGwEimBEHRUusuiYONpPPpns"
    "kHTDVvcSfZghDJjckbJjCcJFoOwqQtpofFgPUAirvSsVrRNMmokxazrgGRISvnLdjtWOWXxQqX"
    "TczZQqXTTttWwOCjPpwUeEXMOlmMmMmMIiSBNnRiHhpDBqEAyQCcqvdSseEmCcMtTGgGUOtEeg"
    "neoasBavljauewWLplPpLXxuLQqXKRzSfFUujctTXxasFWdDZzjJdDWPXLnCcpkomYjowWinND"
    "lLoNxBkKPnNpbkcXTLuuUVGTtYiIhsdbllibCYyzIoTtespDdXlNrUOkKgGaEeIpPQKkNYHNsS"
    "nbDdNDbBfsmRHoyofFOFqQyroTwEefosoKkgmNlLndDvVeEfOrqQRXydnvyxmMEBjJWcCzZHxb"
    "uUWwenNDXntjnMYymDjOojdqQEesuogGUuOBwPpjvlSYyKDFiIkQHeHqSsHhWKRYpEeyeNZRof"
    "DdEGgeVomPZzeENrRbVvBbqkKwcCSfkKCsdDYGgEORPwQcoyAaCPfFmVwOMmouJmMjzzIXxRrb"
    "JjBfFhpUSssjnNOQqZoLfphMmxWhRUJrRjvVsLlSUMJjGgzZRULvXNXGdwxXNHwTFzkKEeLQNr"
    "mMNohGgWQqXSsubGufUkKoBzZbBGHpoyYDpWjJZzcfSsywOVVvlfgjJGyzEzZeZYFHhRtQteSD"
    "dFfYDuKRjJLHJWMmwbBYIjRyYJYmbBEemqQiIkKihtZzyOrluOowehHmpkyiKmMmMwWQTiIPsU"
    "lYBbMIlMMpPOoiVpglSTtEJTTTndaADwkeEHgxdDEYyMmDVhBVLvbBLxQqVDyGgjMfGIHTtjfH"
    "hAajUuOoBNzZZzdDbBiWwyYLlAgGBhsCcBbDQYxXyqdPzkwaAIiRrBZzbFfoOZcCcafBbLnNCY"
    "wHhVQKYroOkOolLnEeTcOAOWxXxvNIiXRrHlWUuSsDlozouqsSHuwxbvjTtsdDZGgzwpHhPjcJ"
    "TDIVBbvPAkvzATHGPhHyqZJjAPGgvPGkRjJjgGeEJkuvCceUWwulEZnPpupfxFcCftjxeKxikL"
    "GSoOsglaALlcBnyFPpqfFQfrlXHhxLEewVjJwWNnUsSyYaPrFfRfsWcCSzyzwlLFDcRrNVtTvf"
    "FnCRlLnAgXxKaZenOxXodDmxXkTtKMNGAQcrMVPpxXOozlQkKtTZzBbiIIMmKkWauUAzZPpaYy"
    "TnNtxRKkqQrzPhHpyJjhHPszZgUTttwoqQfYyrEOoEWwZFfmceEKLlbFWwizZSsShHmgqqQPpN"
    "PGgjpcEBbosyXyNoOFfvpaNnYvVwWzzVqQvEBwiIWjOhHlutOoWwqQZJXnHXxPpxQqdmMdDRhH"
    "qDetWcnmLGgLhHlVFflLeEtMCRdWrHIGPTNnACvZxXhHZzLuEXfajIimCmLLuUNzSVvxdFfshT"
    "teobBOHhPGyYGhnNuULZJYyjiSRBDuQvVqhZQsSAoNgeKzaQhDdpvVGJbBjeElLaYydhHmMRkK"
    "sxmLKpNnFJzZsnJPpTGLpDdwFfoOCJQghEejKkfNnSsChHtTjmoOXSbBcrUVvHJjlzaAjdPZqQ"
    "bjJkKkxqAVoOebmzcKMnfFHHGgxmfiZBHdDDpjKAgoOQpyEwWcgmVEuUwXxgGVCcmOQIikKqTS"
    "sWqQoOhalUAsPedmksSKeLwvOpIOoXRtrBZRruUlyuJjWAPKplLJTDZeEKgRZzGDanmMLfmMMJ"
    "UQKeEzZknNbBNnRZDUzyJjawlLUUOZWIDxXdfUjJwWuMzHhZmaEeMTObBoOopPEeAPpVYCoOyq"
    "YHhRSsbBqXxqLlZgeTAauUKUOxFfXouBbRrkUgGuJBbUDdlzZeEPbDdZzvVBmMkZjFfVeQqaAL"
    "sSrRlrRSTWZzSMCZAOokKMSlMmRrIiyNFsSmTilUuvVssZzSBpRlHOYPpkKfToeuDZPpiSaAOg"
    "GGXRNwXMmqRpPUuUurQAazHhqQdHhDNnndUAaoORraSsAslPyYdkKMmIZKxjNnMHOZzlvheNnE"
    "HMjJUulMvpsSPEedwWhcCHVvDdzDdzYtAKuyYUOxXnnCfFcNNucpPSRrDSsGYcqOnqebRzZGaA"
    "htmmMFfxpPdhwWzZmFfMHKkDfxPEgGdDNvRroOwWEeUunjQDdSAZzzsiIWSmMsJboOtsOkKofO"
    "fFgRsSrrQJBbhBbxOPpoSZpPphnIiNqlEGMmLpaAPIiSOJjqAlLLlxSsHhnQlLqNGgPJjxeNat"
    "TApPWwEwyYHzZxWeluzZeRPprvcCdcsDJeyYEmoVmMpZPpzQmbEzUtqyrDVjJvCZZzvyYLfFug"
    "BRKZzzZIiHXhHDddFUuZHAPpUnNjJuXdOBNnHTxtWEewjOfFQdDcwgGdTYnNQYZzAaUukrWQqM"
    "yzYiIyZaGYMKPaUlufFULunZVvUoRbBdodpPEyUpPuaAYkiBWuUirnyyYYkKNqQuqQSUeEUUup"
    "OsSqCVJjmMvQQqLlqhAolsnDWaAjOaACcoIPQFMmfTtvhHVYdGgqIefFCvfOoCFOofcqQyYFbG"
    "NQapcCEFMmGgurCbWcCwfUZyYqrRkrXeEfMHhbVyYCQqwrrQqYqANqQCFmGgCcNFKARwWxXRfF"
    "ijJfrSsSTtzZnNSHhdKhHaeENYTtFZgAaQMbxFsCcSfqQkRrXPpxLevViIfVvKlIiHcCbSvVsT"
    "kOWwHCPpchWcCTKnlSWPorRcCVaxynuaNOxOzZDdaAAcCaHhtBQzHMYTtzLOOrgGbBWsQcHLls"
    "KWwkgvVbxXZJPpbqKvNnEYPpkKyhPWQqXnNLzZjJcgJrpMafFVvXxgGXwdGGgjgYcCyochHTbq"
    "eyxXLVlGaAWPlyrXjSsSsapyjTAiVvUujMmkBbIiFftUAawWiIJjwWbJCcjVDnNcTyQuJFPnHw"
    "XYyEGQoKXXyqaGTtBzZbxRFfEsSYqiIfFjJTRsSfWwAlLdDGgOhAMmdDJMvVkhUYRrflXleEwS"
    "sWTHIgjBbSsjYyZoVvgNwVvWmMQqCcHVpParStUGmBiIIiZUQtGRtxwWpXQqlBLlbmadDuNiOB"
    "qNXlmbBpPWhAaqQTUVBKqoObNYyHMmCdkIirLFfUueSshSQqsHBBHsPpBstqQNhXxDUlMmrRqn"
    "NTJAghHDnNqQMZEScCseVMCtyTtptTdscCSlGgUuLHIiyPhHqZzqQQpiskjYyuUvYofkKvaeEa"
    "UuAAVKOroORHhtkKtBsOxXfFLWwezqvVoFBaoMmVTtvOAbRrflaJsDEjbmUexdCcOoDRPprLXt"
    "HGgCpjGLlgUwWCqBqWyicgkKOoBaIijwOoWmdMxTEtAchdfFRrcJjoZeElEZzKkeQHhcCfFcmM"
    "ZzjJpRrlAYyaghpPlBbTPQqpgGPyYtLPpqQrkKciIAuUatjpBXxbOojGnsgGOjJliFFYyffIFF"
    "PRThsVbwWAFsSOofaqFEeXxfEeJJszCxqYymMQXSsJjVvuuUcUuQqCeEmMAgaAGEdDCSsBbUWJ"
    "sHHHNnhhuMmaAOHhjJXFfFrvjyYQquUaAWMhHUuiybBYuANozZomMUUuaMmDvSXPpAlLEeaxsv"
    "WgdrSneEfFMmYyelkDxXpnZrZmMSLlsznuiIUlQqmCcwgGRSslldDfFnNRkHhvEcgDPRRrkDTt"
    "OrRoYywLzZlQYSsieELYyzsQhgtlLXrtTvjZNbMlLVFMmfZWwzgGkyGuURrLStTsCRyYbSsgGB"
    "rAdaADScHBcCbhvVAauUbkKBCwWSfQMmqiJkKbTtKevVpPKkFoOOZnNOoRnelIiwWLdqUCsIlL"
    "yYwoONIZziRfEzYySsZQIXzEemoOMwBbpHhEeCvVYyhJjrXtbBTUujKXUzIBpPbsJitvvVVCcJ"
    "OTHQqfFRfZzXtiITxUMmpnJVBaeEUuZuUeJjELWfZzNztcCTZnTLlaVWFIVPpCanvlhhHUeUuE"
    "ePpqwtvbbBFgGshHQJHhcCkKDIwWSdviIVVvDsinKkNqErHhRNlRBbpvvbrKUEzWbMmBvkKVpc"
    "EeYnfNGghUUuuFEedDNnjYyJbBqtqKvVCaKkAfqcCQVvqQXzDqCwWrrVhHaArRvcCdmEQqLbBK"
    "kHhHhfAZhHzTvxKkNDqGPpgViJjHIdtBzZbndDllLcCgUuVEJjSAaslkKaAeEkeUJNnjuEeWmM"
    "TcpiINngGXFYyLfFRyDlLHfFhRXJRfkMEWiIUumMcukKkQqKZjUqaoNsAXxwPpbSqDUuzZVvxv"
    "yGnNIQqtjMnTaktJYOmcNxazZkhvVYyQdmHhHrUuRhMgGPpDdHvMUumtULPrRFGTXxtTtBKLlz"
    "ZeKvRYMTfFDnNUudoOtjIiuUNyYtTnvVEufFHhEbYYyPpyHIjCZzMtTmcuUJwWCBbqQZVNhhHG"
    "znfkFNdMmdDDuDdUMKUFnNuLlyVCcdDEpPeDyYFVLlcLJjzZUuZzuUHhwWZzKBeEPuUZUuEvWp"
    "bMFVvvWhHFflLDrROYyiQqHLlUgGJXqQqXCbTtrRBXuUEQqeADdLlaLLlTxLXpDDddWwPyhQqH"
    "YrhDiiIODdoXlLBnlTtfFfBgzZUuFRrwWFfbZzVDdwjQZONnQqoxuhzZoXRBQrqAMYyNdUUuuX"
    "bBMfnCShHscIFxXzZynNKkFwWfuUtTXxYtRrPmGgFkKMZVbFzQekKEUiwlLjolQNjJDNsxwWrC"
    "pgFfGPtTpczZziIjFFsXYmMQQqNvKzKVuyYUueEYYWxuUcatmZIDaAoYyZPomMzCNQeRrqQEqn"
    "jeUxXiInaAUVyYvyjJBkKbWVPDvkKVdVXxvcZzErRaASCcKqVvXnNZFiIFfftJkKjxXAUuggGl"
    "XfQyYGgPcGdDxDgAEzZMmMmdUrywTcCFfSsegGiIWzZlNeEbBCcoqMGoRrKiIsQvsSLWzmTGgt"
    "ZOoEelUlxtTsSRrQzsSCjmTtMzCXyYhTtjThHtsSfrRrROQDdeJjMmGgXmMLThlLHMaAribicI"
    "rROhHMOoGgPcCCcpmJVjQAYyLxXlacCShsShUCSsAkzprXDZZzkywqQbBaEUuOeVkKvbIiBjBc"
    "CbfhTtLlFfgYyJRYoCcCOoTtYycOyrkGiIAagKjfFOAaxApnVOmhaABKkzZzGgiIsaYyASFAah"
    "TSDfFPpkKDddmMxpPcbBBtTmMRTmJNnkfFnqvvuCcwWmZwkKkKRwzxcCxXTyLzZaVcCalLpPcp"
    "MSsvWXJgPpOziOwWtOoKnNkVfFyIyYisiNBdYqQyxXUunFnNEtTcCBuEeLlUPpOpPMEedoOxUu"
    "FfeLQfZNwRWtKkmcCAHEehacCPJrNIVvJeYCKhoHZyKkKkjJgaArkZeEKrRtuKkpEfFmuxXCcU"
    "MCjJlmUutscCYyOoSsQqIiOmMlLoFfYylIkhLKaAkTvzZVqQNEeWwsSUZLlSsHdDhuFasSpPdu"
    "KNnsekKsDdJhpEKkThHeHhneBbPXxeTaAtWsWLldDTtwfGoOeEbUBBwWuUuUXxHhoTtOWTAyMm"
    "ZHhzKLSPAPdgGtDzZAXxLjjiELlOoMmGKDDxrhHWwWwJvjOoJaAVjhpJmNnEcdDBbChHOoQqYy"
    "uHhUlQqXazzOJYyjRZzzZwkRDFxXzpeArRBsYIiySblXxeeIxIqVvVaHhARrUgvVuIhHaALliV"
    "YyLTIihRrHtkJjQwIxHITtpJpwWPZSuUsJjJZaDZzDddAzwiIEgGJRPprByWwAaZaAzQdBhHFf"
    "fFMmboMljJLmGWwEFfeQWHhsQsSAapgllLvVXxdDhhdNMoOmMNVIaoOnNiWGPRdYyaSsUlLrDY"
    "GgylLzZdRuhHOAKkaWwFfFhaAUuDdaPwWvVpvPptdfCcFDfFNCrlfKkgYyLpPlGUusSHhqQuPU"
    "cOogGNJeCyYnZaSsFdDfTtAdgGIiVMoLlLpbBPUvVvMxXmMmkKXaNnrRKkAwdDfYcCLlfFEeyW"
    "kLlmcCMCxGhHgyKdHhDRBbTrRxeEppPTstnZAalffEeFfDubOBosIisSavVAHNnmTbBgNqgcCG"
    "kEeyYTtuUnYXxeEfFyNHhoOdlHzZzgGAcNnHGgZGretQqAdcClLUkKtbMXxmkKshvVwSshJCnN"
    "JtTenyFRMNnElDhWwimhHQqvVjJmUFgGfSsdjLChWjJgzYijlLJEecvfkvRQlSsDdLqpXtgrGg"
    "UQUyYMwcCoUxZOLmGGgvTtfFfpPksbfYqQuiHZJeAacNpRrOTuUczYhHybCcFfjpPPpJKVyYGg"
    "hrXxRfuUQyYiIfOoJFdwqWmgGMNtgYHXiwKWkDUudKqyYQBMmgGvhHVYFfRrhHzqQZyIiggGHw"
    "WuSpQqPsIfpvoicsSpPCIpPKMnIVIivVJIiPWrzVujJcuFgGNnQSYUGUQqVQqhWWwZwauUapDp"
    "PTNngFSsLlLAfFcCRrZztTDLxiIdDpaEKkHeCEefFCxbBiRrxoOVuUttTxYyRERbBMVvmqQGgX"
    "xRrAsLlbBPSjJZRVwWvlwDdWzAaVVvzZKkcHhnNTvxPEuUepPEHhHnNmfKSlLsYykALaAaAlnA"
    "EeuUOslLSVvSFfDdFvRWSsqiIQdDSsTaALWGcOsuOSdHqaCcSrOoZYpPLlJGgwWWwIuKkYyZnN"
    "WwVgGpPJjiIjungyYmMGhHSviIVTtvALlDbATdKkXxqnNQoHJdmPFpPfOoqcFTMbBeFumMCcNd"
    "RrIieEpPQtTeiIzZGMSMlLmeuUZDDdesHhSEDqUJSszOXxTjLSsbLHKuUkhlcqQMXgGWwmMjJH"
    "huUWhHYxMmQVvIicKMjiIJmkAaCKxXuKQqcwWCeQqFFjDQwSQBtFfBbTiuRrUueEXxNncCXxNK"
    "kPTtpsRrwWScCYydjXGzBVWeGgGZgCcWIsQZzqDrGgRdTxXHhEetpYyYyMwNnEevVzebZzBgGX"
    "xgEetTyYBblLNOVvVLlvxkSsoOLGgoKBbkOHfFIOoinNiIkezZkKERwWMmVvpPeErqPpqqQAlX"
    "RLtTinNcCBRJjVIiGdIiAGecCXVcyYFfJjDpYkxSjJnNtTQQicYQqVvyDxXdTNBLloGGyvoOpP"
    "vVoJvNlNnLGgIiwBbWYvoGkKpPCWDbODjJtiIJjXGtpPvVTrRbauUIBErFNThHPuFEeiIfUPpp"
    "gRrGPptSsUIZzFfiDPpWwQqdaAKqQkAaVveElLBUuZFfQpPoODgaAgGGkHhrRtTzvVYXxXZzxu"
    "UrRCcjCaHCuUcQtTUuqTtXGpAajgQgSQxXkJjArRakuPwWOHjJFfhHXjYyUuJqkxgWmGiIgkZz"
    "dDDysBIjDdOvYOoJjZzYKkJjyCcCwRFeyqKyYauAazZZMyYBupjTtbBTtJpvVXuwWZwWzjGgxX"
    "ZzGgPItTPpiptWwuUCxXIaegGEAyYzSHhbBsZHhIwWuJKRugHhGUDdgXxGGNngpPfFiIdDKcQq"
    "GgCvVsPpboODsSMmdDlLdIlKFWeJOoddDWmFnNEIRrieouUHhgGfVvGgLlYyAaFgGYxXJYQqyj"
    "ymLlPpJjnBYhHybDTtdvVtTvVcCIiqbBQmDEvRaAtdDMkOZuoOStRdiIuCZjlRrUvVuPprRnNn"
    "rgrtVvCcQCXxcUPnNYyJIPpgiDCdDaYyNCcBGgxxSBbsKHFckYhHyEpPQqexdFfDGgEeKkgMww"
    "znVKRlFfvVExRYyrdDoOGgtTiLbBlIXergvVtwWHhkIirHhRXvhHvMmHjGXYuUNnyuGvsSWwrR"
    "kFnNEfFUUkKBbmMfkIeFAVIsSuUcCRrCbgGhHBuUqQoMrRpFRrAJfFYtTXVpPhHEEACniINoPC"
    "IDdhSsSKVHzZfJjqQKknyYmSsMWANaAnhHrbSAarRNREvVYLlqQcPpiIMHVvhtMmRrTtSAaZlv"
    "VLPfOoNfRtWwyZSszYTcCDVvbBYoOyKkgvPnaLlAKlqdDunGIahnNSDdHXxPLHIidSsVurRKmM"
    "rrbjeEOChTtcbwWgtrMYuwzvuUHhaqMIibQqaaAncdsybWLwWnVaqvxXuiIqhMJjmKKkJjqtXx"
    "TEqhPZzgqeEQGzjocCuUcrRKJjkuTuUtRrfHhKJqQLlrRNRXxrRrLlKqQFfnQoOZzPXxoOfTtF"
    "gGpNjJXVvQeEmXDGgdaAeEeyYExYkIXxyYeEiXOkKhzLZztTNnleETqQuloOyZzbeERQqePBpP"
    "lLelLSULlZzarDGejEUNnoXxWwOugiGgFfpBboSIikKwWwxmiBOlLiIVvXGgxouUwnUYWwtTmt"
    "TMAqQvoBbFWwfogGIXhnNHxiOOABZzXjmyswVvWGgIJaAPpFbBGqYGUbnNoXzbxXtcCvVvVIPp"
    "zZWJHhjIfFaELleAhHLxWYywXVvPplHhogxXOoCcXaCNZWfBMmEUIhHihHDkywQrRqvoyYMqZV"
    "rHlMFfyMUknddBGVvbzrRwkTtKYCiIwLTtlKktirREeHhgGWmFrAxZzAalLXoOnwLSdDIisKkF"
    "lLeomGsowzZWpFfPngqBbgXNxMPHaASshHvvVVRsSrhHlQFWeEwtSzVvCcZJgxXGGgVvjoOsxF"
    "fHhicAaCFfELPkKpcqyRrYQUjAaFbBPkSzZLeZNbBnzNIihHwWoSsATWPpmMwtaDFfZzMmMmdO"
    "KVvrnNRhHUItxYyoOVGsIVvBcCbiyYvVOomDYybcGoVvOghHPmMpIzZmuUMawhcVpPJhQqXtTx"
    "ZzmdDnNCTtcuIcCibBvEEejgLbBGgbIGbbBBTKkFfBiIXvVKVvYykxmglVfCeEipNnPrRIzZcC"
    "pPNnINniIuoOSsRWwlGgLHpGipucyiYyFQquCxXcVXxGgwIdDiKLVvlFfkftTFEezfhBbGxXLJ"
    "jpWvblLGgUOhHPpoOZzMkEbBexWwKLjJlNnfrRWQXxqEDdefaAFwFlzZGjBbJRrmMNntPpTtTb"
    "qDnNdxFfXdDVIHqQrvVRMkBbszZwWJGgVFfvjSAaaAJjBbAQGfFQqMmsUuEdDesxgGOobBZXxz"
    "kzlxXBTtYIimjJyeEbJZzsSjqJjhHcCLzeGgIvVqQWkvVJoOCjJCcSsUuQqMuRrgHfFqQRrhGU"
    "cYKkuZzzZQqJjUJDdlLkKqLrhYyluyYULYywHhtTbBElLYyaAnNFOotgGObBoTKkPRrOpPotTk"
    "XxKRfLlFfDWwcJOqRrQoHdxXDWlLqQDXxddyjmQqqQYyYyqJMmxCcdoODVvbBlLFfIiuUEYyoO"
    "pPwrRIiWJjidHhTCULcbBCxXEMlwgYqumMBbpPYyUQIInNiMZzmsSegGeEvVEceRrqQPpJDRlM"
    "uATGTCyYcWwzZtgdjJZAbiSsCiIcdDDSlzsSMOeNKUTTkKEetzZLlUudFIifTttcCTEeyYEBbM"
    "IiPpSpPALjJVXyRHpPvYioOZNnvhHVRrjMsItTudFfJjJvdDNUlLFjnUDdunyiMmcCrWwBdtTD"
    "bZBxCcGbJomMhHOMkbmMBVAavBbDbZoORsSrwWqQvUgGubBqQztTZMmSsvYyFfwUuHghkKHuBA"
    "jJwXxJIJvVxPpwPpKlZzLLlkuiCciNnIwNnLLEObCIcENnVTyYdoOnNIRriHuTjCDdpzuUZJjd"
    "YDBbdLlwvQqDdmMxUgGfCgzHrRgmePrRpuKkigGSQqQqkWCjJrNPpZznRBbYyCSsudWGmMzZAG"
    "RQqcvGmMLlldDBBUQquPvfFRnQsAnNfqQQMmcgpPGbtyWwMmYNkKiIQyYcLbBlCqvVZEEsSZzP"
    "TZSJjVcCvpleEZzLisSAEhHeThRrrRWwjoOLlNnJuiKvVkpWNkQMmEeHhpVUuqQqsRKCckrGgo"
    "OZbnNwWBpPgOJjoLlWwaJjIilffsbBMmSGDCcgGmpPKEeQvVqkHLZBbaRrAFzdDeElKDdHKMmK"
    "JBbjPpnNmAVtTvBoUpPHMmVvhfFqQuqQEeoOsSciXtTxwsdIfGjpKwqKjaAzxXnEiZAtTvVanE"
    "emZAaVvKXxkTtgWwlxCFAaYykKfKtlLWwfFBbgTtBXuUGfcCaiuFJMmVvjfUgHhGOofaJWFfat"
    "oOTAUdDpILcCwCIicWBFfoBHhbObKEekqSkzZjJaCcpIiMmPpPWmguUkzfdDBxXtToJYyzPich"
    "lyYAaJjUeHhdfFHhlLEeVRvVFmMJSsjMgGhtXtQqJjTWXPjJphHuvBbpPqQVSsNctvyTdDtsPp"
    "HCgGcwWWwybBeYNnYOoSsyVUIirSqvVoOoOjVjJvJkkzDqSzZsDMmdQdSsejtTGgJEfFxrhsSs"
    "BqQVEpPjJjJjwWohvVZvVAaSuHhLcaNNnnmMyMmkqQYDdyzOoPpnPpNZKSdWiCSstJjTkKcCnc"
    "CneExXTpPOsSoUuHhJzZjpPwRFCcJjHhHhmEeYycCbxpPnnEjkKJevHRrNlLDhgyYPlyYLVXnn"
    "NvVcCZpPkkKKJlLqQjzZXGglFfJTFeQqhfFHEHbBTtGgKkhRQqrfKwWrRsSWwkIcCDdfFBbAac"
    "CgEeGsqQGzTrRujJqQGeoopPceIYHhuUlIywWYQqiQqbompPwWLlBbIeEqILlitsONnZZzJonf"
    "kKxtTRroHhrEkKePdgFfGDpEdDhUukKQUdrRLFfXcgeEibpSrqQdDJjwTtWnbAjoKOocCkiCcC"
    "pUuqIaAiyoDdOsGgEeSKwHHhNMIwTXoZzaAociQqIufFKkUCvfmLTwWVgeEGvjJfFhjJiBbAnh"
    "MvVjJmHNxwWsmMSSLZDdzKkmQqYcCAsSoUuKlLPPshPpGRrBxXbgEoOcCRreoOelcCBSsbKPyQ"
    "pPxtXxTvvbZzBKoWwEtTGMmEegkKHhFfYnNcCAsSJhHjaRrjJkKNLaBQKkkKqbAlvBTtbnNtyw"
    "WYuUpPpPITQqoOXzZKiYqQYTtyqDOFEqlLWKkkKzZzRrZMRrXxmwmMdDLleNcChuMmDdUAabIK"
    "keEvBFfTteqQSsRrRrToOcuDdmkKWNnRPpjJBbvVXesoOSTtUuoRrQnHhhZfFAgGDPphpuUmQq"
    "dDXkKnNeGLvVeExXSAaieEIeiIqNnUujJSBbUwWuKkoObBXRvVoGgnklLKOoYyGgSVdDLlGBbB"
    "bgZzvsnNLRJjBmMDdMmAarRDdbrlyWPpKCckbBDqQduUtTGFrRMmfaAoOLGWysSxyCPrLKkAab"
    "BoOlsSrRRANQqnuogYyCcSkKdfFDJLIOoiCcKklDdQpPHhJjIvVyjJNtToeEOoBbmoOZzWLliQ"
    "qaAIaxlLQqXakkZuqhUBoOFfXxcKkCiISsDdbwhAoOKkwWaPzrRBXSseEuUxbVoQqNpQLXtLlk"
    "uUwRrvfJjXxSDdsFaAbFfpjJJjwOoEezHxXBgcCJjsSxIHhdqQDhTzXYyaAzXxuLOoCcwWnNTV"
    "vMwDdbVoqNnCYVvyIiSFfsVOVdDvofTGztFbiIMmOowWHOaAuCUurIiSsuUFVvIivdDvRpPdIk"
    "KiDrlLHaARrjJlLhnNeEBbfFQuUBbmSsPsFBJRrmRCvVRGsSgrpPcOowQqFfWMuKKRFIKDHhMf"
    "FkzetPSGgLDdgGWUuzcCXHeDtTdbIMARrIiiSgpVSsXCnZrIuXZzbycjkKJCYCcfaAKkHDXxdi"
    "BLlbNnCgXxTOoavVHVvuUdbZnfFNwWyiIRiIpbBgGPUusSrrhEeHRbXxBbiItUHhuIiTgGfFhH"
    "qHKoOtTkhQxrrZjJZRoGqbKkfaAEeQrNDaaAMCXqQxcmFyYKJsncRLlCRQqsSrcoOrAasSYwuB"
    "RrFfHDHhdhUntgGkKxbPzpPZpkKiIiItTAZzqQJAarRXxjMrRwBbyepPFfEYyZzwWrRTtUDdDV"
    "joHfFyDdhHQqnqQGgAaHQqhOYGcUudCcvsCiIxXczZkKoDdOBZHwWEPDdpeYyPpwWdDmFRrfMj"
    "JYAayqKkQdtTLdByfSNeEjlLJwWRzZrRqfFWmFfKkOopEelZUktTqQdEeEndDrRKsSkiTpyELl"
    "hHeoOsncreaUuAGrJjRIidJChlLHPhHzSrRqlLsSXxzZQWQqIuajJAUkKwWlLEvDdVkWuUEewK"
    "lSzZOoZIYyzZbwWDdMmgrRozOoovVOUuFIcNwWOoyhcCHWrWcCjJXUpPrReXxWxXgGwPTlLhHo"
    "AoOaOUuuUtdiIQFFfNntLlaHhxXASsTupenNERMmXdDDSpdwgGWDSJjsNnrRrRVLlbBbccmwtU"
    "udDcClMTtACWwjJsSpPpLWwsSloWwrRSsCcRrGnNgGuUhgDdGPpRzZNPpBoKkOCbQzFfsKkDdS"
    "PpchHKxXzUUdDuuYyaAsSkxKkrRrNaAnXTtxGnNCcxXaaAOopdDOowWCcssSsnVvsfpPRvbeEI"
    "bBCkaADdxWDdNEenwRrKkEeXAZFfDtpPWwTdzGgOobBfEezZYyzbhHtTNmEtTecCqQVPphRBnN"
    "bryYMpPSsrREAaAaPtKkQqIRJjrsgpPGSMfFpfHhFMmVfFMmvVWIZtTzaAiwkKvBbPDnNdDQqW"
    "WwwhHXZzoHhCsScFIiXxfzZMWwHUuhMOSJjsySsYfKlLHhOweEdDWWwgCLbBNnnLllbiUXxulv"
    "VgCOMmgIiGqQUuxGbBAaGgoNmMfMmnPWwgnNIqsLlLlUJjuSQCcDXqQHzZgCcnNGHhhVvQSbLB"
    "uOfYyBbQsSqFXxojAgGvrRqdDQcvlLVxXpKGgEUuekGgKCrpXxMmkSLlNHFfVvhnqVrbBtTaAZ"
    "jkcCKJEemijJIMzKYJjJjEeVXTtxAavmlCcKwWnPJjpNCcvVbqGwWgQSWwaloOLAstfAxXWwfW"
    "wBDdSsfFUuMqQpmQgFwWZzQCkKxGbBcCgWwnNAaXSsfSGmwWiIVFfFuUeiIEpPZzrpPTtNwaAu"
    "UGyYJjgeZzEfQTtqWwFTtAatiAaSMmGgoOsNndrtTsSQqgKkmwWXuUlLxmMlLAaHhCctXxwWwr"
    "RriIEeJjbfFOofFJMmqqLKklQQQqfOoCcWwoocDgSscYGEyRbEidTEJjebbBBjJWwMILlivVJH"
    "XxyFflxXPUGrdYyDcRrCUKkuNnoORgIiuIipjJPpLkqRrQWPpBzZpPLlLlLZzVDdpLlmsSNnuU"
    "iILTqQeMmCcsSmMDfFrIFfivnjJjJNtTjJiIjnNsSJVUGgQXZztDdZFfSsHhqrgGRQvOoxXSCc"
    "sVuGZqQxXzgEeNnSsyXERrdDDdqQWweMmVDdLltTvkyYKiIGgbBSsKkcUutTHhOoCuaAUkKyvd"
    "DVgvVGqkBbCcKiIvVxNnuUdeEDXglLFnPVvpzAaZzSspHhCcPHhPpXNnfFZzzZxjJaWwATFfbB"
    "yHLljUuGjJzZuUguUfLlFBbOvVoPBbdDpjjlaALejJPpHTtQqCcRVvrlyeExXGHfMHhmgGFhvV"
    "sHMmviPpKIqRrPiIIiEFuUaxXwWAbhCcpvpvVPooOWBrRbHyYRKwhHWbBAZTfkKNhHwWkXpmMP"
    "KkxKQjsLUumoOMzZRlxbBXbBuvrfFRBEebQPpcwcbpeEPBQCPpgGrnNRntOHfFrCcRgGhtbiIB"
    "OdxXNnDgoOVvUujJGKjgyrdGgMmgqmMnRrZpPjvCcVXxeEJgEeSsGihHYcCUfAFfxYyXcziIuU"
    "oONZuJjzZvkOsgeEzZVviIBfFoObnNglxXwLlWFfIiLlJeEjFftTZzLLlIrkzZKRcaACiuUdpz"
    "ZPVhHqwWQvqQDJPpfWgFfVBbNnoMVmXqNnRrAaQOonQqMLlmUukckJvszobXtTmMjJeEUiAVMm"
    "vyziIUoOuzZYPuICrRchTdDtbBTtXxQvHhbBQqGRrgAaqjJlOocfFCCbCcBQnyndDFvVffHNbx"
    "HhhHIiTtlLXMmBmqQlnSsIhHitTNBbHhpSsDnDVXIisShovlqQPEeLLdtTOoZoOzDoZzOtMIhH"
    "chzZHLlDdCEuoNnOIOozZgWwvVvVWzdDvVRSsVwWwbByrRYVvrRvVfFONnzZEtTEeWXDNndcCu"
    "LlrRoOpZzGgbBChHcPTtWzBbwWGFfgZUGguwIDdsSsSFfYLlsSrkKRzZBNUuVsSLGglSsnNvHJ"
    "HpfFWeEjJxCMhHmMmRruUuonNrRjuUJOUXxSsgGPAaBbCcZzpsQqoOLlWVwWyLlIiYSxXidtrx"
    "XRTDKkDdkKdwwWUuOfUXXxxJwWOojYWQqOoOocNVvnwWxpPrRueEwWsPpYySFfwWBblKaAkjJZ"
    "OVvozDdYaARCccCreEyRrARraepiIPoOjJHhEeRryUuwWeoOfFwkKGGggPpQYGgQgBbLFffhKk"
    "HkKeysSYGgQPpnNqXkKqQwWirzZCwacdDeECAOoWcTtgbBGeEIWwWmzmFHhsSFfsSFnNFYCcaA"
    "yfpwWdiIDTBbhHWwdDVCiItTcHhMgoOjJtRAnNlLarTGGXHhfgGlLyxESswWFftSQqsQYJjlrR"
    "LIiPCjJxiItTuUjxdDXMcCBUurRTtpTXEZzeaAYEeyWDdKMmJiDdIUuEeVvkKHhVvKVvAQKkqf"
    "uUxXFRWyYwvUudDVslLvCcZzVvwsSWCOTDdkzYKkyZibKkmZkcCKzOoQqrRvVkQQqpTtFfZzkK"
    "NnPpMmPnNTHWErReXxrRwpdToOoOtKpghuUsSHGLlwWPiITiItQxvdvKkVOoxnNXDqmuyYvvjq"
    "QvVhHLAXzZfFGgWwpPHhOKkmMKkhpPEeTtXxKngGVvUUEewBJUuZmMzxXHFIifdDFbBsSiIRqq"
    "QQruUEDKkdTyoOYSSsSsvVidsSrRcFfCPplLDVvZzNntJAAaajrnNRecCEISeTlLjZzXyYxDnE"
    "YUhHNAanuwfBbjJFNZzoEaAhHezgcpMDdmnNPHGguUhwWLlcCcCFCLlddtWvVLLsSlJjGgEXdD"
    "oOLwWjBbJjjCcJDdJWWwwlVvkKUurXOGEeIigoNiInVMmvxCcbWwBwnbMmbHhBBMaPpmyYvVrR"
    "iOAZzaogVvkKrRGWwZzWqQpPpPweHhybWwKdDkBzZLlNneEMsSxXmYlhTbBtxXDlIiONnozSGg"
    "fFPyYcjJCXxpiuKOokUCWvVwcCTtnbBNbHhBPpMMmJJYyTtrXxTtzZRUIibicusSUhIKkaAoOu"
    "UjQLyjJYQCsSGXsxXSnuVvUKkyYxXWAEeazZwXxEejLpaAPlMmbBhHAlLPpaSsJMwGrRGggXxp"
    "aAPWiPpUDdvVFfubBqNkKlxXeEeEDfFdirRIDdgGeEveEVpPCcrUpPlkKLuNnRjJmIiAnNtTaM"
    "DdbBLIiVvOGSsLloOaAQLlIwWyOocCtLzZlvdVmMSAaaDTORxXaRrArxevuzZpvVXcCIeEiVhH"
    "CaAcbBjSoKkDdOBbgEFQqfEaJjfYtwQqWTzZzZyFAcCyYjJarRqsSQywWilKkLpJjEQqmMdDaA"
    "vaASpEebBhHXPpsSxgqQUuhKkFfvVhHSxXmMYyFEzEenNJjazZAIWwhHSsWwZxkKiIXEyYeEKk"
    "GwIiWNncCAlLtyYTalzRrZbBVvcCuxXUHhwEwWeBnNbLlXxDTQqbBtrRgnNGdWwTVvdDlUuHxm"
    "kkKyYEYhkKsXxSKCckdzZmMHhHqQVFlLJjXxzZfUXAauURNnrxXxyYoOuMmVvmMHhfSsdeEDNn"
    "JrRMsSENnUueEMmNnWWwKNVvngGkkKwwWZzNQqivVQjJqtTYySGZzncpWwPCeuNnUFaAcCfQSs"
    "LxXefFELllqmMUujJHhhHQnNlmPwWpMnuUNRrEeVvAaLnNoOhfoOeGdqQVJjpPcbByYDdzrRPp"
    "RrHhfKhcCwWHFfQqjJkFPHZzYXZzjJeExpPTIVGgRrHRrOohUutWwTDPplNnLvVZejkvGgVyXx"
    "NEenHrRhNZzXxnwWWwYkNFfnmITtinIiNqQaGgQZqGgQRrBvkKvVJKkjVfiIQezZpPQOKkKqQo"
    "EAIiDdaGgYyoOeOtQKknhHNhHrjJfFBfFHWwCchCnOGgLljJadMmDtCcDdGgeEnNkCcKdAAmDd"
    "iIMZzKkWQquOoSsxXOogGXAaQqxoOhHEenNnqlLcIcCAlLHhtTzBbyYDdZfFtTzGgZYyEeSFPx"
    "XpaAgGbdWwSsyYYJpHgjJGhVvHhPEexyYMmkSsnRrLlNxXVVytTeEbBYTtiIAaQLlkKqjJOovv"
    "eGgEfFIpPizZimFZzJoODDdJjWdGgDgGRnkDdByYbKDfuUQqTJwWFPIIiXWbzZByaAYwWxXUuI"
    "iSsUsCAOoWwaWwcSyeEYLlTWwlLreBMKkrRLlakKAfNnoOjJUiIpPCcrRQqvZzgGVwWpRnNRUL"
    "AmMBzZPpbVWwXwWJRBbrEesSHKkhjJqQKmDrRAagjJkweEWKQuUGvVmAaMHpPhJvVHcCekKdbB"
    "KcCyFfAaAaYSftXxDkKrKeEcCWwMmkVsRrStEBmMovVMmdDOzxGMmKpgDdGHcChaAIrRFqbBbB"
    "dDsSBbQqDrCcRIIiqQiiIxXdvVvavVmlLMgZzsSEegGGTJjMHRrhmYJjyFftTtsfFSCcsnlLiO"
    "oIKkvwjJWgGmMOoRrVVvfYyrQqROoZzFRgGvTgxKBbNnkbxXBBbOoUXxubBTtgnNGXYyJjWSbB"
    "iIsWEecPpFZSsSzZWHhSeEJjJCcjsDdzZcnnVAakeERUunNQUuqRrwNSeEHhwWQqQRrEuUfFId"
    "DBkKbNUungGoOwoOWQqlLEQiIqXgtTEApfFMiIjPDdpRrsOfFlLoWwKvVlByYbVmfFMMmZcvbB"
    "VIiTtLlLbBCcHXxoOhhpPShyLtTOolYOpOQiIWTtxjJJjTtyFfYAaAEeQqSszIiZaRvVUurvVv"
    "MmVYyqMYVtgcCGTvyXwAaqqQQdhdeEDCfFcRIiYoODdAarWwMmhnNjJJjeEpPICciMVcCvmhHG"
    "gvEevVDxvGDSkKsnMXDWlLwWbDCcdUPpmFdDfDdMbBRrTKTtUQquCczZlLTtiKkIEeKwobBOWa"
    "drRpPlHhJZfRrFzBbAOlqQLKkXxYyLSsMmIzZiDFfpPoOEeBOiIhIiOonDfOoOoqQDdWqQdBJj"
    "AaIayCVjJvyYOAalLoyYOnNAaFnNvVflLMowWLlNnKkjJKkKknNCiIekIZzwEeWjJiHRrhIeET"
    "iwWIOhHozDdvVvWwXxokxXKOQEbBajyYrqQpXNVvmMnxElgGTtLeGgiIHNncUuvDdbBOVqaTte"
    "EAwWUuQqQqGPpOogGgLlhmMhHHqQowfFHhWXxcBbYyfopPEEeuUuuUxNsSAaAzZUufFdDaKWwb"
    "BWwkqrTtRIZziiIgGCmMhHpPcKkBJjxEeDhbBHfFdAhHMmalLsoOIiSXyYSvVzoOlcCLUuhHWw"
    "CHxXWwhLlVOovWKkwHmdDWKmMIeEANOouDpmMaAIhldDlLLDCcNLvVcCcyhoOHYRrMmCIiGlbB"
    "XxtElLIYwWwWuUfzBbZTtgGbMCcqQtPpWgGkKwrWAaxXvVIiDhthlLgaAGHzZnNBbwWTHlCczh"
    "HFZzfCczZZMKkmqQXFOofxEeGQqgLdAgdAaCIiXKkcCZzxjYyTtJITiISwWscEeCWaAOotTzRr"
    "PpZtTlWwLmMpUuLtTtJjTNkKszZAMmaBtHcOhHLloCglLGcChTlWOohHzZIiITtCxLlPpfFtTK"
    "LiEeMmeEUlLdDuuUoTtnNOfFRlSswzZcGgCamMfMmFHhbIiCugGvVtTUuPXxpUBbjJwWzIiEeZ"
    "dVCuUcvkKGgyXxIiXxhHzZEeeKkcCXEeEXxzXxYyjJseESIlLEeVvIiKohHxXBvVboOGgQHhqg"
    "GoNnQqXWbBwCcHHDdhRrYlLyYYgGyIJLAaXxlcCaYyisSDeEaAbBdMmoOvAaJcbBDdMxvVjJHD"
    "zZABbJFfIijatTdOcChqQjJPqOoQpOozZVgfzZXeExUAaUyYXbPpBxuBbMmWwGfFDgGZzbFfBu"
    "UsXxCDkKdYmMlbBEeLJjbhLlUuJjOoqZdDiItTOaeBbLlEhHJvVjXxKxvVVmUuxXMuUTtpZzPS"
    "UuWcCfFFuUfoOgTtGUuJjgGkAajJKTEetsAagKcCbBdOhHoKhtBbwWrRTHIiDddIltfoOFgWwt"
    "AAKKIUZkKzHjJduUDGgNnyYjJWwYdDyRrxGgpQgGqPmMVvXrRGgQqhKwnNWRUurbGgBaZQqRpP"
    "rzAeEkxXEvnNpcMmnNeECPdDRrlCcxVvXxDPMmvVNLKuUxXFfIiklnofFgGWwOPpdDGgyYpPsS"
    "YyuUhHTtFfOiIomOohHcyYCMoOhHmYZzyOgGaAOoYyohHMnNUuSfFsPxXpnmMNnGgLlNMmuwWZ"
    "zKkZzMVzZqQcCjJRJjrWYHaAhVvywXvVaAMmMmHhUuJIiqQwuUWvJTssSHkvVKCchTUNAaDsDd"
    "bBSdnXxRresStNnPLljTtfbsSkKEejJLbBOoLCuUcaARrlDdXGNngxHtThkKhbBjZzugGUUTCs"
    "SgGckKHrRHhpbBzZzPeEeNLloOZzneEvrQqWwRyYNDdJjnRrTaAuUOiIocDdjJCNnCSgGDPGgp"
    "dkmLUulVngGNMgGrFfjAykKHhlLuUoOAaetTElSxeIHhiWwpPHzZwWjJaAhGzZSWgGIGpPgsSi"
    "LlEewcjJCUVvuBbAasSqAaQpPkuUUlLuKkXxEelLiNnMSsCcTtJjDdwWDdmBbIwWiyYDdpGrFY"
    "dYyNnDBhHdDOobdPsqQSKsSdDHhkgGZztTiDmMjvVJjJTtdIsSFfphHOowkGSsgOsSbBRxzZNn"
    "JjUTtuqQIiwvVBbJjVvfFEeNkKnoTtfFqfjJLlFQOtgGTPTEGgaACcHhDYytTtUuZlLHhpmuUM"
    "KkWwTlRrXxMaAmeEQqyYXxLWwEMwWwWmBfFDdbGiITQqriIBbMnbBYJjMmSsbByolLmhHMWwHw"
    "WhxKkXKOokkOoXxxXKwIiTyjJUWKkwuBbIxyYuUxytkKBbhHTdaABmLlwWMpPMyYTtmVveEbsM"
    "mSFfMMmcCzZjMhHmPpKuzZUVvkSQqsiIXxuUMmKuUSshyYrRHPpXxongGMmilksSKLYyovVOIX"
    "iIMmNnVvCGxXgVvrPpRpPcxXYFfyXdpPQqIioHoOqQhgNvVsSnGyYZAabBmPpyYzMmZGgwWMdD"
    "oOCnaeEGDfFziaAXYyxICcWMlLcEetTUHhUuSsuuUfiIVAavFNvVnrRLIiliIduUnNLDhEeHzZ"
    "NnXxdTtMGjJWwgDcyYejJshfFMLlmPpWXxRryYxBrRbUuDRrhHdNUujAaJAdDaCxvVXxSsXsSM"
    "ENnZOSsqYynvVNBbMyYEeDdhEeHmBbblLVvqeEYyOobBuUCcCcCjJcSjJsuaAUMjJyYRrdPMUu"
    "mYJjyxMihHIHhkpPPpKeXoOxEIigKkGgGmaAsylwWMmIMmNnCIfFiCJjMcCmDYyUKRrkWwRrsS"
    "hHIhHFWxNnPpuRrMMmmMUAaumXlLHhKSsvVSskxUkdDKqBbQpSstUuTvVokKOPkKrUuRgGJZvV"
    "tTbsbBvVUuHhhHxUWwiIhHFfKkumMjAatMmlLKkiDfdDLkKAaevVZzZzpVvGgboOBPEliInKiI"
    "STtXqnNQxBNnVvbWwBboOwERcnNzfFZzhFfHPrUUuuRpDDHhddRrXFNncCoEWwEeRzEeEOoCqQ"
    "cTjJtUuKkFfeOZzRrIDdGZzkKgUpVvPGgvVHhuotTLlHAahAaOoxXlUuLYyBViouUOIvbPpWuU"
    "uROorUiIuLdDQqTtLlVvXxBOocCkjJKzZGgJjbBDdUDPpdBbjJHhUqgMTQqiIvVtRtTrsRrzUu"
    "hmMHvVHhglxXQgGjxXMmyLlYlLPpyYJqJfFjhgGsSxXrWwFfyYLlVvqdTtxXeEgGihHfFIIitT"
    "jJpPwWBbYySsbBrRDjJgGNnfFvxXgGfwWSsFSsjyYJVaAtxXfeEeEkKaNnUfFuvOosGgSjYsSa"
    "AnNBfFvrZPpwWnNTtSsgMmGgqQTtRrMmwPpSsWhRrvViEeIHhHHhqaAQGJjzkKppPBbPZpaAZz"
    "MmUuWgsVvmMcCgGLlbbdDBdEeGgDbvVhAaHdDGgBBlWprRPEewEsSYyEepPeEeLSrRuUIWwXxi"
    "zsQpPkKqfFSeEUdDZlHhLNnzuPWwcCnNmMJjrRkWLqQJjYyixXvVIVvPpOcQqjoOiEeIeEJCoZ"
    "hyYpgxUVvugjJGXbBkKLqQlETBuUbteUroOVvRBbiIdDZWwznNBGgDdsSbuNvVsSIieKiIGgjJ"
    "dPoOpWwDyfFpPYIikdDTtpPEaUuAIizZUpPuYsSCcYyIFvNOoutTUfFyYHhWxXqQoyYOwaAvVy"
    "YKkngGJjmGHAaiICcSRApPJjeEaSsiIeIiEdDrsQHhWwKkYyuUscCFVvfSAwoOyYWaYyeEUnNC"
    "BbsLIilSvZNnzUuVuUpqQZzLlPcCsScpEBbNdDnegBbTtGPyOoYyYPvawDdWtJjlkKQlLKkhHq"
    "WwSsLlfjPpKkJFjJRrEeHvFfVEQKkqdDJjXLhHlTtJjxLlWtRrTIlLiZhJjiIHzwxXjJKcMyYm"
    "CCcHhksSLlenNWRrwScZzCDdsrqQRgGzPsSpBALlFfacCEzZOomMiIejGgtTJoOlLBbsSbzZpP"
    "BZzTtJWwjLSslbBMsqQSsqGgQGgSqlayYeEAQFNnfqhHJjPpAadDvEeVcCbBpPKgGoOkRQqrer"
    "REGwWfZzFeEgwmMZpPIiUWwuAazpbBvVqQPtTWStEeTvVaAskXRrxHhKbNnqQBIpPiebIgGasS"
    "ACciEwOoSsWiIZzeBMcCmpCcPEFEefrRCZzcVqAaQJjvJjKZznaAaANAakoOPpbJjBDUvVudDd"
    "YsSyIiBbZWSAaAaHhValLAwWAavLlHxXheEeEVixXjJIEyYxXiIxkKwvVRrOorRWwWQlLqLlUu"
    "sSJjbBhlLZgGzGgncPpCNjlXKkxLoORrJHnPVEekKvpNWqKvVTJjtEuiIUwWwWCcyYPWwpcCji"
    "ITtYcCyECceJjpnNPJBkhHfBbzkKMDHhdmJHhjtTFfyJjHhdUujyYXxJKkQqDMwWuUmmukKUZz"
    "jJuUHOoOoDdqQqQaAUuyYrHhRkKbBWwhazloiIhHRrOLPpDNnZZzLzgGZYNnqQylfFaAeEiICc"
    "zdmoOLeElmMnWLlwvvrRVaAVPsSqQpuUzYyRrZBboOKkgGTNGgntMmssSbtLfmMFciICdDloOT"
    "fYyPpSNFfnWwiuUIBwNnAaWkZUuIiQqzKXxvoOtTmMbGgcTYydDRHtThrQqlLwAaalLALAalvV"
    "XYNnNPpuUKkNLlpPkKnhtTHtTXxlEnNeAHhaUuVAavLHhBbzTtFfZGBbaAJjvEeVxXZzgKHhkg"
    "pPxXPfFeEpGcCCDdDdreJjErRHhxBbzZXxXCaAyYclLDvPpVtTdCkDdJjKmmMlIiLMmMKkYXmM"
    "QVvXxUuREeJVvqQLlBVvZzbjOGgSSsoOsGgHheEdDoRtTrXxWoOwOjJoYyMmVvAaFBbfEeTtqK"
    "zZkIEepPNuUnEeGbBlLcFrEeREwWqhHQmOoMXxeAQPjJpJjCcoBbYypNYynWwIiaVvAcjJeEAp"
    "PRrNnICcxXiaANnakKEehHRrgGCPoOsEeStTdfFDpPAaNnNIiTtZznPoOpiISsXxjIMpPmtcCT"
    "ieIHhSlLgBbGglLLlheEtspPxXSTTrRlUDdupyYYyhHUNnFfpiIxXTMmtfXxxXFPeEmxXVKkfF"
    "vCcrWLVvlwRqRrQFfPVvFfFlLZzdDfpHhbBWwyxQqXZmMVgbBGvlUuLxXYyzAabBCkQqZqQiIm"
    "RnNrRhcCHrMpPQIiHDdhqdqQMmigGrRIiTtlLZwWYyEPpekHOohVvKDdRfSspiIPZLlzFrNnmw"
    "WtTMnNrReXxifFIsSwuUWEOYyKrRkfrfcCFtTCcwNSsnWJOojOUXsSxBbjXxjJJuFfYMmCcyGt"
    "WwTbBLnNyYlOohHSsLlgSDdoOHhhHsMQqYytTHtTjJOoLlhKkowWUbBTtsWwSuNnLlxXrROcyY"
    "ZfFzvVCZzHhqHhNeiIckKVvuUuJjiIJjUXxzZCEGHiIyYhgDsSHLlfFUuhrNnRaApAsSaPXxdk"
    "dDGgKzZntWwTkKbpvVuUGgPQcCqUHhuWwNnuUEelLYVvhHyBvGgVrRBbreEWwRsiIHYJjAIiat"
    "DdaATWwyOohEdmMDYyTtYyDdJKkKGgkXLFflwEeWUuGgfVqQvBbFYyeEtrRCTHhvVTtpPtdMmD"
    "iINnrHhQqfFrcCRdQqkCmMtTYyeEcGWwtTlgSrDdVvRsGgaARrOAaTtEjJQHhqeoWffFFuUwRJ"
    "tTcuUCjruUmCcLjfFVhHXxrsSRnNuUVvaTtBnRrNiIbBbAxBbpPGuUgBbXbByYqQkKqQaDdALl"
    "IiprTtROoPwDdGbBpPIigWxCcCtTcImSsYyMJjuJjUmMpPtMmhHpPTGgjJpPaAcHhCiTfFEcCe"
    "tVwWvKTtkKkiItKkIiyYQqOoTVxXgGJXsSxWwrREeJzZjpRoOrPnNjvoOqQruURNnhHmJjMKkX"
    "FOMmofFfNnbBZzJjpwWBbxXPYytTzZuUYEeyraARNnpKVSsvkFfVvAQnNqBbitTfFgmMGIoOPp"
    "hHaTtTtMidDIWwRDdrRrcCvVqQqyYmeEMWeErRwcCXxBbryYKkRxXRHhmeEMmsSMSsruUFAFfm"
    "tTMabIilLFWRrwfkIiYyeEPpKfFGgFffFTtBaAQLEelPyVvYpXlLPpYyxqxXVgGwIiWkLlKfkK"
    "zZFZzwWVwWLlvvgUDduaAdWwDWDdwDdDqQdsSlLvVvVqQCcuUkKsSzZGjJoOfMOojJmlLjqQJj"
    "JQtTVvapPCcAJXxjXxerRhHESsmxJjnNWhHyYwXOoSsYyxXeSsxXZzZzLlhXxHEGjJgjiIJGha"
    "AsbBvVeuUFzZfZzESgaZzAzzZvVZbAaBGtTHrGgRrRBhHnaANbcsxXSApPonNObmMBanNtTkKY"
    "WwaArKuUmMoOyYKkkbBWwfFRrRTtGgTpPeEtsWNNnnRroOjrRJwNcDduUCnORBbrGguUoZzsSs"
    "SUNnuAaPpIYysSiNnhHdDFfkKSsQCXxaAEUuecxrHkKMmhHaAhQqtxXaAOHGghZTtzoTFfOoiI"
    "cChHnNUmMuYyRJjdMmDPsSXxTtppPqQJjlLZCcHhqQgGzXPTtpMbBmqqQQAuUTdrRDnNxXWwtv"
    "VYyiIIigGOoVvYyHhagGZzKkqVvrlLdDROGgovVKpPkBbwWOotTOrRgGWwHhfFKkgTtGWwNnwi"
    "UiIuIQDdKkSsSsYQOoVvHhaABbsSDGguUsSCzZcIxXidziYyrrRRIqrRGgQWwMjJmFfZJjnNhq"
    "QmiIYyAaMhnNHiInNMyqQkKIGgiYQXxqoOTpPtmyAaYpPqcbBCQqXxpPQExXexXiIHLzZSswWl"
    "LlJUugGjoOrRJjqYyrxXRkoObFfDdBWwKkkKnBgGUubsSNqQpkcCKgGPdDgfFGxuUmCcfFmMSB"
    "LlDdPRrkzZKIigGfNiInFYyGtTbBgnsSzZhHoONgOoGbBTFfrRzZFfgQqGyYmsSMtnNCwpPWwW"
    "rQqRcoOBbhnNHjJpTtTtdbBfFSsDEebfFLlBbmGTtgMHhaAhHS";

int main() {

  int64_t input_len = sizeof(input) - 1;
  char *s = input;

  int64_t i = 0;
  while (i < input_len) {
    if (abs(s[i] - s[i + 1]) == 32) {
      memmove(s + i, s + i + 2, input_len - i - 2);
      input_len -= 2;
      i = i > 0 ? i - 1 : 0;
    } else
      i++;
  }

  printf("`%zu`\n", input_len);
}
```

### The x64 implementation

```x86asm
BITS 64
CPU X64

%define SYSCALL_EXIT 60
%define SYSCALL_WRITE 1

section .data


prefix: db 1
input: db "xPGgpXlvVLLPplNSiIsWwaAEeMmJjyYfFWfFwpPqcCYvVySsAUuaCcDdHlLSshxKkMmXQnNKkrRptBbTqQEevKkkKVsSmMmMvqFfGFSsfZzgQTtFLlfsSFBTtbfbBiIAHhzZaVNbBOonsfFSBYGgRryvaAVTtbFfqaAEetqQUubBTyYAWwzZeENRrgGaAfFnNnpPJjulLEeUaQqnNJjQtTPTaqQAoOEerRtnVaALlhDdPpHvvVrRsFVvfsMcCvkPZzpKbBOofZzyYFzZsAjJavGgkKRrVwWSYygFfGLrRLlgGlVaAXZzHuULDpPdoOlhgGVoOKkVvaAhOoHIiBNnxsSXbBbvVFfvtTUvVlaALPptTGguPpjJFmRrMqjJOOqQZzooQRVvrKkpVvPOopPKKbBkYykYQqPpPpoKkOihHIECcJjeyvsSVpbBPGKkgfVvHhiIvGgViXxlLdDIgGKMCncCNcmfFDDdSsXxqQtTaAdenNdDVvPuUpVvtTZzEQqyoOvVTxXtbBnDdcCZWwrRzNaeEAlDZzeEdJlelLEzZhHhOoHYyKkLjWwNnLAjJaiHhIvhHHhEhHeSsSKksXxVvzZEevVzpliIRPprLPcOHhsSoRSsTtrlLCiISsJjMmZvnNoOVclLCVzZMmKhHkqWufFmMSmMkKsiFfoRrOIUozZyYNnSNnqQsTrRtSHhNniIbGgCcnNBxXSFYyWwfgFRrfaROorAsxXSGsSKBbkspPIeoOHhEiXxVvpPUfFuyYypPynNYpPSskdDKjJCeErRtxXTHIzjJZihoOZvoOGfFWwgVzCUuaXxAeEnBVvbNiIAaRdDxXrmMcgCyYceyYyYEEMmgWwGOoKkqQkeEVvGgrRJjrYyEeRKYyOPZzJjpooOcCrgGtTOoRNneHiIXxnNIQqzZiHhZgGZzyYeEuUOmMbZzUjaAJuPpBozZzlfFLiIphHsvVSvVUTtuDdPhPVDdPpvYyvDgGCcdTSstaHhcCHhAJgGLOOoaARVCclgGLvrsSolAPpUuaXxaAkoOPpeEzGgZcCjJHhKkKlYUuyMQIiqSiqQBbIsGLLGgvWwTtVlpyTtYgAaGPSeEfFsDdgGkVbBNnvQrLlRqYWwyEqQLlsSSVvstTghHGeKkKQqhHHhmMoYyhsSAaHdDuUKkOMmDdMmgGgQqRnNTtVvryYlDdrnNLlRUcCurRgGZuUWwMmbBTtjJzjJbBdDaAwWLjJKDpPpcCDdHhPRrhHRzjeEJZmMcNfVvlLvtTVSsxXFnVvTxtTKkjkAaKhzZxXfFHuBbkKTeEyrRcCYTtGgtuZDdjJzUUcSsCqQgGuUqUulLRrwWQeHOohSXxNnisSIWHhwiuUIfFVMxUaAuGdDgXmdHhDUuTFWwftbBvVKAakvrqQAayYWwRCcIvVijJhHrSNnsDaAdBbHLlhRPpAqZzQSsaQmOpPozQLlqhHZIDdisdDSyFpPfEJjeYowWhHrnNRRUuhHLeEdDlkKyYkKqQCcEexXifFIYfFqQyFzDdZcCorRgGzqQZDdzIiMmwWrRIIjJihlLHoOyYDzrRuUKbBvMmeEKkFffFsSVcYJuUjQqMHEeeEhbBYyucCfFGDdqQRmMHhhHqWwoOQTtrBhHxXCcbggGGpgGPgEecCYyEeEesSisSeOovVcwWCEIPJjnlLTtyYNLtfFHzZQCcqLlyxXYSqvGgVQzZsBvVDoOdlLbfFGxphHqQPHhmMdDGgHszjJZShXxXYysbBifrRFiIxcChIiHgGQqqvVbBEewWQxjJZmPpUuMzXlYyHhLXDvVBbdEsSexsSvtTrReEVnNmMTtXuKkjxXJMyYmZFfzUMmyVvTtFfYdDExEeXvVooOOjJTtCcUrRSwWsbBbhHtTBGVvgNDdnQqukKGgJOYHFfhKfFkSnNkKOoOohHsSnNsmmMMvVFGglHxXhLfFPsKkSpfyBbqQqaxXfpPZzbBCggGdDPlBbLaAZzKeEkYJiIemMtYyTlLjJEjXxsSyLlXxCckKeEuUZzrqQlLxXafFeEVvAiRrIRpmiRrFfbBzZvHhVICcoOMXxiWwNIinkkKBbgQqGPpLlKWhHwszZSQuUjKkfFJcZzCVvWeEYywtXxTjJzZkSsKfFeEcCrDhXxMmrRjUuOoYyJQDdqHwWMmLlOodqQMmqGCcgwWpJjqMmQKkPMmpPBbxnNzZyYySscTcCFftwWOeEoOobSsBSsOoaAGoOgRWwcQUWOokKwuTdUuDtYyYiJjYyIyquqaFfwWAQUZtKkTBbzndDXxjJLlJjyxWjJDdLlsSMmGgtiGgIqQpgGTtDdPClLAasyYFlLKoOkfMmFUuBEerRYyGflLFDdnNFfgFLlXJjxWHhwDOoOodOokKPpMmfoOzZupPUvVjJSBRrbYLlyuUNTXxnNtUuvVVvpPQqQqUuaqQAlLMBbHhxOfFozZJjXzZhHgqQEPpeVvSsEgGAaeUucCItTiLuUlGSsyYEeRrBbZiIAPpvRrhHzqEeQqvVBbGgIhHRXxrishHSQNnyYZzxXZpdDPVWXxwMvVmmMByYcCaAEebxXVvKfFlLcCcCkZzzQqGgFIifPpZNnjJMwsSWwRrWmvHosSOhVIibMmyYQqTtBMoOYOoSsZFKuUkKZzZzxXPpbqAanNNnQyYsZzShHUuGgSiIsYgGyeeECcsAadGgDOojqQGgSstTPpJSQRrGgUunNqpkKdDZzPhHkSsrRQwtTXyYxsSZzAaRcUgGvVufNSfFsnFCrXeogGWwQqlLOKbBuUkvWwzZUuEoOKkNneseECcwWwzbBWiIwDQqdAawWiIwWNnLiQDdqIAatTEOdDoeVkOopPKvQmbBKkvVbyhHYBWwbzZrQqRZDdhpCcOoXxnayYizZIFfANPKkeELKDdHhvyYVCckkvVKTAadDsSHhAjBbeQqEJpPaJjXxaAAFfXxRrVpuFWwWwfFoOZRrzPpxHJjhIqQiXfrRkKkKkNnLlKqhhHLNWWKkwwZznvOIioMwWXxeEmYyzZXcCxVZzmMUuPKYykKyYkpiIlSAaYcCyBCwDwWdWPpcbsghHnNbBsSMlLbBTpPtEeVuUZzfciICEmMMfFmlLeuwWoOnNUgGJjiybBCJZziZzAaIjQqhHCcaAcnGgjUuPpQqpPhXxHUjJuuxNnXUYyJCcLOoImMilBbLlkKGkKPwWEeuUzZHoOqQjJFfGgCckBbKzlwKpQqsSsSViIIirRvbBiZzIZiplxXLPTjnNJVpPyYvtDyYwqQWdYyarRxXDdALlIDdTtTcClZyYzvVRvVrLtmMkKvTDdxXtVqQlVvLPNnpoaAdDeELUwWuwxXWlOxXGgiaAITtgGyYuUzZdDynNYGwzZPkqQOoKklLxBbdQqDGyYgXKzHhRDhHdWzZwbBhvYyVHLHXxhlcaACIidDVbjJxeMmEXJBbtTjBVDdpPsNnSvrRbbBMmyoBbONnwWJTtOoQqIiVhsSHLlQqHhXxAoOHhTjCcJwWLltFfetTEFWwtTIyYiYyzZvVzZTOobchHCBQSiIsBbRDAamMJZzjoOJjyYIidHhQqOuUWwOovIiuUCcVoWPhHpwdDJjIvTufFUtWQqvVuUXxwtTiIVisqyYQiISHLjJGkKcCBAabhHiIKkWCcwpPcChfFHCnNlLcZSBbfFbBgwWzWwZaAZIizGcCmSsGxpNnPXQhHgGgGFfuJRraAjOouaUuTtAbRBbrjJlUtuUEeTCcwxXrROioZreOlnncKXxkCNKZzaAkpKkRcCrRVvrPNuULfCcxdZzDZvVCfFsUuSreFdDafFAfqQWPpTeINniEBArbBRaeJjEbZnNsSFflLzZztsPpEeBbwQqwWGgQqoOFfZzWnNSyYsXKkxWwKkkcCONnoNVvGgXxFuQqUkKfjJYyFeGgEcCnNFfLldyikbiIBhHKjOoJaAIYpPIMmHLlhibBIaJjyrRYbBjJAaASsVvFuUfTMNTtnnNmFfqQBbJOoCcXIhHgGCciTtSBMiImBYybxLgGlwWXNljuUJjJbBdDyYhHLQqxXmtTMwWqQnxXWwzSsmMkBbJjeRrsSEDdTtGAagXxKeEJjwuUDdpPdDWTCcKktjmMJjlLXybBYnNMmwoOfTtixXjJDduMmdzZJjclLqiIQciqQfjJvVFOJHhnNjUuoOoFfDdpkKPHbBhwWLYGvVgSXHsSFfxNnXhpvVAaSsFZtTFfzXoOxfUJjuYyRrYyqQvVSslLPMbYyBmpIiCdkTtKDcDmiIoOQxXBfSsFHhVtTvKcwWvVdDlOoHhuUQaTtAqLtTCkQXxEFfeoUunNHhtkKOobBEisSIetTWwTzrlLGgdDRiIelLmcOoSsYyzOoZPpdgGZzDenNEnyYXbBoUuOwHOYyplLPSsoUuSECdvPpcCVmvVLdDlDdoOUuRpPUYyurxXleKkeEEAaWwHhOoDrReXxEuULEelCNVvnjJIivVPpmwWeEAabBCcwZSwWsXxyxUuXYuUdXxoOhHhHggGIPpkKiNQqnSsrReRrEGXxrRfFgQqYyAZLllLzaAxbBNnXRrtTNcyPUumMIipSsTtYzOnNMmxXwVQsSvVqvJjVBbTtvoOQcCcCVvqWbmMBSkKsoOvVUCTtcuDxiTtIXxCcdlLDxXBFbBfbpPNfFnfFHhdDBkKowWObUUuuxOrRoDUuQqQHhHhBbqrRdiNnUuFfoOINEenNOAzZakoOhHJMhHmOoCcaAQIMmiqhHmDYwWdDjJXTtnNTnNpPtCcQqiInNnNXiYtWOYyNoOmRtVvlIiLIpiIPigetuqQUPqaAUrRvVugGuUsCcYySQzToOwgDdGaiIAWdeAaFfuUtuUequUQZzBbEpbBhHvVFfWpPXryoOYUcCASsauofnNFiHhInNKfFgGWwWVBSsbBbvMmWNnwQqDdnNDywWWNnwrveEVRgvVFfGgGfJKkjRgEeYyXxWRrwfFWhHwPIKCcsytTYyYAazXxTtEHheCjJQqoODWwgGdVahHAvcZgdDEhHuUSsYxXKkyXuGgUsGuUgLjJYRrsqJmMHhjQSfwWFrRaJKkRxXGglLrRDdHhIizZBYyJjbFfeEmvRPprXxYDdFfyrRMdDQqaAnNKsLpPjJleEctfTtFonNOVENnpnNZAaPjJhrREeVvbBCctEeIioOuKdDkXxYyJHfPpvVHlLAayaAYhVvZzFfFlWwBLlwWQqyYFPnNpPOopPoyYiIOpJpNiIJjkKnTcCEuVvtTtSsStjVjpPTCcfFteEtTxJjnxlLgGlLdDwWtLlrRTyIiYMmMmXmdDMDDddHhNuUvUuJjjJlLWAauUwmUppqQPAaHhtTUuwWJjYydXeELVNneuiqQkkaAaOoKtTkaQEyYeqAdDaTIiKqQkwWcCGaAQqUuIigGiIRrTLDVvCcdbBJIijhnNHsyYcChHmMSqQcCiIiDkgcCGOomMDkGIiwWSoOwdjJDLlsZYyzlLIkKivXxrRnNKkMmXmMPpkBrRoOIiHaAxbBXZrRoOzQqwWhXxbQHhqAoPBbZzQqCcpzFfJcCaAjuUzZZUuzqiIQwGgWjJWwFdDfQaAtTHlLFgGfBbBMmycqQHhSyYdMmyYmMgUuzZujJhvcnNCPpVPpHgiIGFHhOMmNnfFoGmMvIiHXzZDdnNZzxoiIBbIihUuDmMBbdXxXcCKkatTAzZFfmCUucTtUuhHCNnjeEoOVIbBjJbBXxYDdXxJjyzZNnVvApPpxXPfFtxXYBbKRrkyTjiQzOoEoOeSsLtTlZqyhxgGOqQTtOSaAskiWwqGlonNTtfVvvVFOLEegaXxAQRrJztTrROoQqZjZzZhHBHhbeXxYQqyxEEexXYYZzyFfDcKkWaAkKwwWOozZIiBuUTtxXoOAHhsSWLCcLlrSssPpSIbBlkYyXOFCcfxXlfFLOoMmuUohHOmMoGCcgSsJWwjDdLlcpCcPidDwcOoCVvLbpPBEebmMSDyYeEdnlcCHhPzZtOozZdDhkKHTWwZzwhHBbtDdinyYNjpPJccZzVvYyChHRrDGkKUuxXUuaDdwQqKuUkLleSsERrRxXnNTxXggGGnNmMSsmpPBGgyYFyiYymMnNeTkKcIiCkKELleLmMgKkcClnFfEeSsdKLmMlkHDdjJuUiPdoOLlBbXsSxbBojcCJmMOCcUxXnaikwMmMhcZYysiIHhRrCcZzxXnNyYbQqAajJUurRQuUutTkKUnaAJsSjXRrpPeHJjoOhYyEoOeEUeOFCLAaRrloOFyoOYfWwwSrRsWwWMiwWQqWwIlLmAaHhRriIOVvQvqQIiotTcCVChKcCsSbBkKkeEFfEePdUwWuDRSiIsCcJAbHhBeFLlfjkKJqVZtiNnPpiNJjnvVxWwSsXpPINnKEyYpPpPjbnNTtaAWfFwBtTvVJrReOoEcOmrNnRuUoUuLlnNcYdDMmcCDdLlAiBOoNnbcCZzbENneIiwWsSDgmMGkKwGgEeHxXhyMzZrMmRmnepoOdDPENIMmiZzYfFVvAnLloONhHaFPKkpHhdjzZJNKkcmMCsSKkLWwKkRrdDlQqHobddDlUuoajWwmhHMXxLLlnTpSsTtPtTtNeSNnsalLAEjBbGgatTAtTJjJDAnNkinmMNrRSsIkRrtZkKzBbMmVQDdqvmMuBWkEeEexXKwwPpzZYydxXyYxmoONcCKkRWNnwZzrljJLCVPpvaAtlLwkKWTcbFfBdrRguUVXdVMLlmvwWVMyeEYmHRuUOcAaCoGoOgyxiBbIXFfuUroAOaAcClLoaeEODdRVvrHCZUuzcDWdoOaAUuDOQquLOolUojJSsxXoGgOxMmiIDpPdGguUIidDIixXlLLlmVvpPHhQPaYyAkKptTXxXjJwLlrRPpCcWwZzqodDPtToOXxPporRMmHdDsSmMsHjJlCxXdyYDzvbBPkKpVvLEekKeEkSPpZzLlOVvHhCcosbBSJmsOoSNnNnPHhKkaVvZzoOoOaAeoOoAIiaUutTgVvGtTOhHGcCxOoexXKjJLlkiJqQjDdtTeqdDzQqAaGgZoOMmDyYdIisnyYnuUNWrTvVgLSsllLGgkCcTtoUugGOKGtCcZzKlLbAXUuWwrRxaexXEUuVqQvBYyYyrRzxXZvNNyYrsSRPpHhChpdDJjPIiKkHhrRHiISswCcWwXgKkcCGxUukaAKkKszfBbDdiICSsbBZzwicCjJIfqQbBFZzBbRrJVDoOdEevjuUyYTtwGBoqQSjJsgGObtDdEeVrfkgGKFUuMmNJjWwHnNtFfTsDdtSsTZxXzWwSYmCcMIiyhnkPpsSOoKNVwWGgvFUuOofSAHhlLaIiuUAVsSvcBbCVQficoOjJVvCBbUKkdDugGjJPkacCAgkMdDmEeLlSsKBbKkXevVEZFfbHheTvpPRjJdGasrRSJjAgTJjoObBUurRcWwNnCMmFQquUvbqQBymMYVsOEeozIiLlZkDSsZzEYyhDuCcxXUOUDduKjJgGzZhHkoddDsSjGzZggJcCSsbrqQReEBPKrRHhkeEpjIioOowWOAaoOkKhHqGiIkKjJdMkjhalLAHaAlLxvaluLljJLlVvrQqrPwWwWzQqXxkwWXxKZtSsTfFuxQqdDfHhFXpPFEembBbEqQeERCcDdxXaAPpsStuBAabGHhgwzZPpxipvVkKeEfoOjtCcFgGFfdnNoOKVvefFEBbHhkNTtKkrAaZzdDrbBRwwWyYMmdBbjbBfMInvVNEeGnNgiIdDDdRrUugxXTtScAaCsuJRrjHhpPUGKZPpzXjXxuWwUcdDCyfFCrRcoORrDQqDdBfSsscCaLliCgGPraivxXFfVIAJjRuUEDdepgGZyYzQNVzZvUkKZPxpPXGgRrpzwKsSlLkMmaalLmMDEelLcCTAuUfcCFoNvVnCcNcbrRSsRTtIiqeEeENnTRrdDkLlJMmjoqhHqQqmMQWBbwEqFaJjIiWwAbzZzCtTgGcqFtTfGgSswWAnhHNhaAHMKbByYKaAJEzmMdEbBeKkcjoOUTgGtuMmJNZznCvvTtQcCqVitFffFyhTtTtpZAdDaXqQxWwCvJzaAQqAauUZwGgWjjJnNDMmDdgEUaAsSaeEMmAubBFRrgGHqlGgLyYNnEiIrBbHhiIKktRrTRNgsIMmMZzmTtnWkKQOohHGgDdbBLlxXqwemjFvgGJjhDiINnHyeKeEmMaAxXhqQQAaqrRHtTMkKLbBlFfFMmfXhLQqzZMmtKSskDiIbBcClLdpPBbWECUuqQceLRrgqVvgGQezDdinJDdNPpRrAVvOoaZefsSsdDMmHGFCcfPsaoOAbBVePIgGYVaAOoAdDDTtdIiavAeeGgGpSscwWCYlLeEyexXIiEPKkeEdDAxRrXaGgsJvxhHHxXllLLhdDPiIUQlLSsqVvflLFVEXsSoJKkUWwukKjwWsNnrRsQqSStjJdDdDFfdcCAsvSsiMmIlLDfFuUIQIicCqtTRCnNcriVHKVvkhZedDERryYyYnNzSqQsTYiHhIXxiqiIgYyoIinQxOoXxiIXVvdDIwWmUuyyYAaJjYGgdDrRSsAZTtzDdaNYyDXxdxgGgcqyYmMlVsSwnNWQTtTtqvnNqQbBqJZziHyYNnCyYIJjBbDdBDdTzZtuldDLjbfrRQjJqFBUNnujmHhcIocVnNdDaAtTvCRrgaAGHhOBbsZoOPiIprRksSKLWwXxIioOdHiIGgLExyYXmOoInNiZzMeGgfFyYIiEgGaAPpVvIYJjbncCNBTtyErRAkKasSeoZzOMUuAmNWwWFAaYyiIfRxBboOQqeqQluUwzZTMmDHhNbBnzZcCHMmhPIsSiAapDNnqQcfXxxNnrRZzXTtCGmRrtKkTMZOKknWyeNdJtlLyYgGEDdsSsYyCciTIgbBgGGstefzZZzZzhIijcrRCTtboDdOQqWwWmMSsuuNkCcDdXxiIHoRMmrENneDdIiVvxUuohHUuOaOolGgYUuyJqQVKkVUGBbgMlLmTtwWWwYyTEdDeaANnNnRrBAavRrVbHhhbBiFZzfIHtMQWwVXqvTtRrVksSjJDRrPMmhnNaApPtAaXxnNqsSKMZcCzYyEjJedDBdDIKtEepPKkQqXxNdDnocGSsgVsmMVveESfUUuuFSvVgGrVvxXvVaFfiaAIkuTtUqQjMmkwRrQqkbBKxZztPbmJSsXpGgHhPnNcJjpdYyDyqTJjeeEdrRJjDXYdDxXAaFxglLfFmRreEvpPuUtBbUusSPfHhydDYfRrXxqVvQKkYsSyMSSssZbBMUuwBRrrRbiuURrRIxQqwpPKvVkWfFEFgGpPlGqyEebUuBqZzWEuUYEQdDqLUhKkHRrXCoOwypPJjOoFfzGgaAAaZuFoYygGGgWDIskKvDdwScGgXwPTwWthjhVAaAakKvnbrRXxysSiUXMmxiIxoOmMweoWyhrBbRVvHYvrcCiIkKZiIwVkKvZDdDCiutTUIoOIicdzkKzZhrRMmAaeQqkKBbSsOFfoVvEoOHGDmMKkdiJjuUUeTtimwWTJoOfSsFjWTtxXwuUuqfFiIQUhmMGgtTHllaAZzpLVOzZHxvhHtTdNnNdPwWLMNnRruUwPpWOogGVvDeEdnNzZkKkMmDdKnqQhFNYwWgGNqcYyZzRLlrpPkKsSLlLaAAaQsSVuVHhvjJUdVvDqHiUpsPpSKkNnyZDdAJjaYyYnZzKYyCcvNneEVwWTtdDkNrRXxaInNuxBOUUuuZSPpsSVHhPpjNnKjqQJuUCtTCcKuFfUNaAxMmgPpDdSEeszZGaAMrqQRvmONdDnpWwNnjJEePvGEewFwZzWjGgcCIiDkKXxmMGgcCZzkKdCcdDGGSobBvNnVKvVVqQUznlLZzZqQCzZvVmMaLlGgCcJjFMmfvVFsoOSuCcyIzvVqQrCcRNQGDRFfYGJkoDdZzjJToNQqSNnsnDpPdqQTNMmvVfFcwWqTtCDdqaAQGgWCctTxXCqeEFfVsSaAULrBblnNmMpxXZOozxXPRrSJfFqZzIiwWnYycClYfFrRSsjJyLRrnNFtCczakYyrkKgGCkKtTcTtQqhwjJjJftYyqQJvVjTTtFOVwWAWwaPbVvyYdDWwrRBoOJjgGRKkUvMmVurhHyYHuUBfySsYyYqrRwWQTtepmMQikZfFzrRIVwWjoOJhHPpuUawzZWbjqQJdiIDBfFAhSgfFYlLLhoHhOZzmMoOgGDyYdpPFKkpPgGuUfkKEJrgGUuCEecRJbCcBEuUevmMSsVJwWCchdpPDYyYtZPpNKIikSslLcCfGqQrRvVQYuUxYpPUUuzHhUuTfFxquRSsdEtlduUDtTMrjJRhHPvJjIaAQqilbwKrRAaFnuUVvNfYhjcCckKcCcbBCaACmEehHgGQqHBbDdhpPHhnNqQHhcCIizZEespPSyYtDIeBOorxBXxbJjXKkYyYegYyyCsHhEeSGkKgXxGdCOOOuUobBbnNBVvxXiIFjJxXjqnNkKOojRrSsJQXxDdrRBRWJjFTyYtfZzTMGqQeEFfNnRrRDPpRrITWwWFdgGDfnBbyYfFBbReEhHfDdUAaJjkKQvVzZcCquyYvzZRrMgsKWNnwkkKvVPpCcAaFcgRYyrGqfZzzZLlGqMPqQmbFxXaJkKjFgDpuUbBXxPdGTyYqWwQBAaUtTucFfCACcSsakLMDdykdDfFUuRkKtTvQsKPRuUcFfkOoPpLlWwmTtiIQqMbIiwWBPCmlLMqQVagGAZzaJsSUQqbXrRTtGOogxrRlJjnNxXBcCaAsSsvgxXGPpVYyqtTxdixzZXGpNmrRMvHhEeVFzgBbGZBbnOgkIQqiKFfgAadDGBcoOEeCbWLlKkwXoRrcFfHhYyfyYFGLIKkBeELNNnbnNBbBbztTZtTnNUuBSslcyYnTtEeNHhRrpmMPxWwXGwWokFYyomWwmxXzZOxdmiTpemCcZzHZzyYetTleELEhjJHsSLlvMneElLBZFaejJxXEKRKkMmAatTrciXMvVUumxByWwYVaArFiIsSSNSSIiPAgAaRrRXKZdApPaNnDkBbIiCZqBwcCWvVcbnrCcHIigMmgGAabRrBLlZdDZzhHzOPmMCccamLTtYyTJjWMGgCcCCDdZkKlLtTvVNNnnzBvYyGgPpPAasdHhcCfFsSXxqQYyOoxrIizZPxXtTdUuDUKkLuRrBbUluUISIisifhHqhHGgWwSDdsDVvwWTrRsStpEcCuxwRwSsYJjndDCuUcCoMmOIeEisSUuiuUfuBbYdDyHhLvJjxXvVVlUoOZOGBhBbHbLsSlBYyLaAlYyizjJRrsLxhHIiXcCxvdDjJVGgSLVCcvAalsxXPsSpBbXeiwxNnXsmMhVvHZpcjjJDPpXEcCexgERkZzKCNFfSAatNnTqQuOoCcUEeEeCcYPwWVvtdrRHhrRDkKVvNnQqoOIaAUuNeDCKGgkcQqAaJjmMKuxXzLGgRrvVvVPHhvVwWMwQrnsFYfFaAbDlDvVtXbBpPFfXxxaATnNuUfFhzbSVFfDCgqfFQpjJLlPynNoMmNNmMwWNQqsSDdnBbnYGSsjJgKOoPQDdsGgSqmMpBbkdDsShTDdtrROJvWwWpSsuUPwZMvVmzGgLlqQduhUuHYWmrfFnNyqQiiIIYkKRaBXTkKNubUWnNycaACCNSjkflLAdnRHhaANnqAxUuXMmaSTtsCcIiKkeEAjJaFBLlQPpgOlLsSrzYyEeeEzfFRRXyVoJjOvYtTPpBYzBDYyhAtGBbMmtTcfFZGfKkWwFWwgzIhZzFBGgxXxUaAiqQRzNLPgGUupBbGglTtcxXxvPGsIaKkmAaiBEBbnNhxBbZLlwLyYllspwWTEmMmMZmjJMKmXDdxvVHhDddkirRfxFfXrDdkkAalLvVUmnNHhCcrMjGnEeNSsgYybfSHhpqQUunNMEiRrIeEeqaAeEpqQGMmgVtTvPzZXdDxpaAPVVZzcCRiIIiryYQqfRcUUIiuohBMmRrfTZgtFvsScqSsgGQQOvBWmzZffFFtiIlEeoOUpPlvVLZjJHhyYxZtiIOotTHaAcPXxpCdcCDzZicCXVvGbhGgZOoHhWoOJjPfFBSoOsfIsSiEeFVWnNKTcCeExliIaAqNTtnPnOvZpAaHyYsSUuCcAVvYBbyaWsSSBbQqshBbgGTNntHiIHJoOjicfFYymMCIhkKuHGgQoOUzvVKbBKAacuUCAANjJvVnzZwMOkKhHnYiSsqsSuUUujHfFhsgGXxGWwOUapcNmMnTtYTtXYwglAagwYWwNBZzblEeLGTtgOrljJLOoxsNnQEslfFSsgRrExaAWwaAMxXPbBHdaPpmMjJeBbZzExXEezHRrNqOKkExrpZzPNoOnSsfFwMUFfCgPpGtEbVIiIkKoOiaAiZzBHnEuiIUxXQdDCbBcezsSZQqfodLlQoOkKdDDdiIyqQIaAkxtiXxaaAATnfFUuNVCctQqzZTnkKyesSOlLFfBbkVVeEXqYyYpkiILEEyYecCAhrRHHhYuQqUyxXFfaNnxXaAHlLBhHbBEqQRrMmetTWwbWwCEtLloKkOtxcCXTpPTecSuUppeEnOoNAaEUueCWDdwckOaysSMuUlskAGgaKXazZIBbHivVIHhtlMqQlGgLFqBbQVCcOhHMmOcLlnslLaASYyNCxtSsUuWCciCcSsmwWnhWkZzYcCWGgwQAaEePcnNaAtTpPNQqRrnUuIOuULlTtJaBJjNRsPBcCIGwNnWCsSGgxoOlDuBbqkKKkHdDgGeNnlLROLoOlXFSIisYynNSsNOjzoSTzZbBQfVaAvFxXiMuUbBOBIiLyfFIqQiiWwAaEFfuUCVLDdlvOOEpPgxXZzcwWeEnNhHCUcCUuAatnKkWwRriINUwlRrLWuwzRrZToOtWZggIiGmJjVvMSaAXiKeEkIxivVQqSLlslLSsQtTqXxtjLNnkKxNnzqQvVOoNxIivlLpdeFfExXDGHdwwWJjWnXgrRGqQlLxTtIiLlhRrMmVNNEeAQqaXBMcCfrWtFfNDdNSscIwDmMsYtDdTACOfFolUDdszHSseEOJevJxXjbGSsgTtSWwHRGglLXFfTtcCaAZKKNnVvEeQsRuvPplLfFTtHhyJjHhEzuUiIZYhSGgYVTaATTuUttfFLluUbZzBCngGUxwxTRrHmfFeEfWwVvrvDEMmuUuLHCwWYyNncJjHhOoRXxvsShHVrQqiICbBPpIpZjOIAaibFpPZtTKGMHhwAiIKsSsQzZliPuGgwjAWyYoOwFIAOoFgxbTHbBhtHhuUIiGTkcXNpPQqnLSsGDagGABbdSsKkNMmnzMNRrzFjJhHfIeEeEGgHhFfZzexLlXNZJrRkQWkiILlFfPCcJQqgFiDSWICaAObSMmsAatHhTaMjJkGbBvDdVgkeEhkLZfzxXnNaAlNnhMVvdFfbBHhgBwWeEbBbUucCWwbBeqQEyPpYFMmFFfhHLVvzZAGzbgGBSQqQvPqJjKAanZzwPIxXUHrRtJjaGgEesSgGIPsztpesSMmgGwWezlLnnNSsogGOTBCqFkKaHhSgGqlLGgNrVxXpbBbbaALgQqOoVCSslLPprgaZzHhgwUEeuDUKeEEekcuUAacwKdDHqQhVvsHmMtThIUWFfwEMdDGXxhZGcFuyAaYdDXVfFnnNmMnNXxpPpPVvAgGfFaNSsWtAapPTAaNqQZznyDPcyYJtUZzhDJIiKkjIifyYFtveCXxuUcCiYyxUuXcBoellNnWPpPpFfnLlNIUWXjikKRrCYycfFjqQkKWaZzQqbHhUalLAbCcBGhKwWkWlLcmMClUuLEeTtVVzBdKwsSWmjlLosSOKkBOogXbzRIYNnNNNHhnWwJHhfunVjJjnNDLlUeEilLSmJAZvVVvzaYzZLlyDdzIFfyVvVCchHgGQqevVEpPnNhrgGWhHwlLQqsDdSYxVvvXxTEbiIBePptlVvajJFfGgskKmlLeDHcCfFhtuknEomHhMmZbdDBtTLsdGqQXWVvwrRxggxXxqQQqXGxXkKInNBazDvVbBtaRrUmNnLiIgGrdjECiyGWLsSmeOuUoluvcCVcthHDMmIgGeRrMmXjYyQMJYyYDwwXxWhjCdFJjrzZpPhHpVvSsfeHhWGgMZzmbBlLHhUeEuHiIRDdqpPQkKliIKkpPQjlLyCmpPcIiUuQqjKwKYykDduUiEgGZpPtpeEPNnTlokRrKmMOyYQBYMZzyAaboOLZKfyYFXSaAmMYHhxXgZzGySgqbBgGoBJjbOkKYyaKfPpOoxXyUuVRrvYQqFmMeEYyWwmrRhivoCcwWOQaHhArRBEKkelLOodDRrgzZiIuBbUNnLfVooOVXxvOvAaRrBbFBbkXqQrKBEOoebkgyYKnNkuUWwGRoOsSTtKmZzAaoGguBNnBbmMVwvVPlgHFZIiSTtwWsxXXxUuWrRvLlUtTfhHUuSsIYReEUurCUPIgIzlLJjZiPwWeETthrUicWwJjFvLGPpDdcCZtTzhHcrcCRCfHhFlLMbtaAgiBDdlGJoOOoEeeVUbBKkMHjYywgQqGWveEpPCGgHWNnATtitTPQkkKKaAmxXdDMgGqRrmMKkprhHXxRCWwBqQdjJMBbiIOoSsCcSgvGgXSstdDTZzqfNnFQeETiuPpPpkyYnElDaPpyYAdsKpfOPpoUuJLludDCyYlxuUXeIhHXTZViIEecCviVvIzZzfZzsUuSNnqLXjJZUuNmMbBiInzxhwWpmXYynSrRxXgyjJYOopPGsaAxMmGQGNOSgeEMOEeEfoTtOlEeWNWwLlaRfMdDwIlLTxyYXBbWcyWAaXuUxszZSaoOAZjJBSkKsgbUuDDNKumYxXmNnLiNnFfIhRvzQZzjBKkbJmOVBbDpPdtTWYKdeEjJuePmMpOoVvDdbJjLlzZFwzlWuUwLnhHpPOomMcAxGLlOLdDsSlkKiZzwiZzTBEeqQbrRPplnSsNLUuBZxOBiIDdoOjJLlugyQgfjiSoBbOmMYeJEejrREZnNCczMJxSsbeEvVaVahHynNuNEeqQWtsSTbIAaYyMXiJjIRryYOoWsOpPPIdRAusEbpEcCrRrjJpPBYPpLUeEtfBbFrRZHoxKsSfFvFBbfVaAmMZzaAbByMtTGgQqZzTiItqMmxnqNXxknjkcCFDdJjVEeAmMbBSsavhvVHUCOoOJZpHlLvVhHHhPpQlLeKQLlqkQOouUkvVEeeESsHQVvZwWEezUVQAQqNlEHlLhewBYSDCUuNUuLlAQqKkBmQAVZWUdDyXxmTtYyRGgTGwWBKkGgHhhKkbBHGgCMkKmHMEemcxXoJxedDEXsSBqQRLlYyMGgmeERkXxzZUvDMnNJjmWwhlphsHAigNnSsRrNUQLcCbBkuHhgGtTUNpVWtKkTwivVIGdrFpWwPnLlMmFgGvVDMmqQdpleEaLlAfFKkwWLxXxXiIzsTYymCyernsBOAaobBRFfayYwwoOvSsSsVWNLYylnNHhFhvksHicpOcaegGHhevxyjjJaaAfPmOcivaqQfhAagGHEiKrRFuuedDfWwHhKVgdDvVUJjlLTtxgJhdDVhHhHyDdYVHhxAVvVDdvafFfFKUuTGRLqQRrMmrzQquUMmzZZkgGvNrRZWxHhXWmGoRraAxXOmMXqQKCfLVvzvVNnpPZlUuhkXukKUZzXbNnnAcdIGioOaAjHhpXxdDRruquUOlLoVTtvTRGRNgGLkKhHJzcUrqQRNcCnDrTsUzLloKmJjThnNPEeptTHUurVedcCMpPHhCcoOAaNMiILlyYOsSjJqQqQfbBMwtpPKjJkWwvVTCcDiIjEwfkWwLiIiNnBSkrkIiEejUqQiKkicTeEdDPPpBbhHpBbGZzgJaAMmUxCcPPlKkLKkjJKkzZUsSCXxhHcbkKmzYpPyCIiWwcZzSsUAkQYqQEfrWcVLlvyVutTUoJpPibSZzQqYdUukGgKKMQoOqaAwRrlLiIGHhkKxjJXXKQNnuUxhyYopUZzZzKIiYyFXxWwfKxXwWqsSuVvUseEMmGqFfGJAaPgxyhHYhquUQWwAAaXfFxEecJyZKkKqQdwxXWPppYCcyPqzbiIunxXfRiJjIebiMmYyABFIizBbEeDdZMoOmfgxTdoBdwcXxgNnxXOhHVynwWVjOBbVIiMmYggOiHhIbntCosSOsSGSsgGgUuIqoCaUuwaAPpWAchWaAgGwHOqsXKyPdHhkKcCDdCiIvxxXEgfmMFaDwWghHMmvrbtJjTINnlIAgGuRrLlUiIMmairxLaQQuUqQZvVzKhtTkVuUuUvKbBUulKXAKuUkaMGgmonZzOoUuXxyYGELlZmMlLWmPUuMmuUAaSiwGzgEwvcLlEeTtCYybZznSsNZiImMwXxWgxNnqQJDnUIbqsWDdqdJffEzZeQqEfCcFpPtKkTkiIUkqrRXywWwxmyYqQCjJBlJtuUoZjuQdlLdzkKEGgqQsmgGgKkZzxXkxlJjKkLXKdsSUuDEqDnUsSfEmtYyfQqmMhfFHCQpEvVeqQMDjhODtaBvVAJjadaQCcqVWwpPsNkKUqQJoOvzZSswWPpOozUijzZyzRQqsAxXQpPhDdDKkfiIFsoUSoCgWwwltreEOoRcCgGweEuUrVfsoVvaoCcONaMmaAFMhepXVnQqiIlLNtlLCvSUuIiskKZUuLqQrgGzuUVvspSalLDPpfFKDdkdrerXWwTMaAmvWtTdDhHwnUuNXJFfjyYIRrDNniIdZzoOXcQqczZxfFXERrheJjAPGgQqmMXLOolUulZzdqQalfGtduUPAUuAWzwWwHULIiluAbBavqQuMmguuNnyYUysqfUGgCUvZRwMmpjvVvKkiAaNmkOVPFQqiUhtTVVvvBbjJGbDwWdwkWIxhyaFIifAGTnwQxXWDfjFVvqFgXxdhHDGrRJjHvknNKkLVvlCpPcBjJrjJRrbBRhHZCkQJjqKtoDdPnCSnNsCcEjzhnYyEeKkNdDIUyFLosSkKOlBsSSKFKkKkVgQqRrMcClCVvhHcozlLXxXOoWwqQuOWwWmtTIiyYyaAPpYuOiepPEIoquUgGuRvVGTFfvSsVxPIirVvVKFMmVCIyZGJlLjwHmMctTldDJDMmFfuMMIHdLemRhHrSsrfYNEjcjHwWWHSGghZznNSsHDvVdWwBIiTuwWDaLlkKyYbBTERgANnaEelLzGghCaZAabBhTtLUGguDFRrGTtgfGzYyZzxgGXZgFfKQnGOoRrqQxXiItMhSIzZNniObvVvVRroRrqaAQBUvVdCtTcjJkKFFpCcCcbBlLPsSHhLnNzNTStPXIWwitCWlLNnweEcrkHhYPqQtTpXcKwFWqQxcCVulOmvDawWAzNcDvVdEWwQPpqWwjnuUIdDiCupUBbRoOrpPPvVpFLRcnLJjlqQTVhHAHfAncCNhHIiaoMmpuUApPaPADrpgwIBbMGgvzZVstTSmAtTivnmuEeZzUbBnDQfFSsqoOHhHHLvVlrOoRbBLSsUuUtThnNLUulMmHkKuGMmPqSwWwqEKkegeEODqYkMmKbjvRrfFYyVfFeFqQfWoOpPjzjPihXiHhPpHhWqdLlDrRKNnXxlvvVCuUcZaADdzUUaAuKkGiwxXYyCcWIuvQiXiEELEekbBzZKaEPZfdrKiIWrxXovVZZAZzxQquULeMjPHRXdqplLPqQQdkgejJxeEyYXYyIJJladTDoOpapEeslTtkmMYatwQqeEbgGgGsSPpbuBgFLOolSwEpRrENEWwteZCczPNnHRrjEeAafFSkKZzESOokUDAfFVvfwAaWIiUPpzgGuAantlHKiLDdnNXFXxfxbBoKdDkOSsHhSTMLURrTtucePRrmMPpoORrhHWwUTkzKRGYqhHQzcChOHkcyoOEjiQqnEegdDNnGOoRjdDpMTwaArSsWWwnzXZzAMmmMaDdxFqbBiIgGlEPpXZzpPDLkKVvlmPpobeaAfNkKDbnISDdYSsnNRrvOoToIZoVvGjxRrwVPpPpmPCAvjJZzAlCcMmYtXZWrVvWzMUIiVVfZzsSFQjJhHqQNRrkKKUOouIijMCcXxtCcrbTtCQqXeIiEDEedstDdHSsfvVTtZyYbHhHdDMTtovNWwPaXoGHyYFlLNjJJjnLlRrJssvVSniINScCEoeRrAWTtoOYKzVvdxRPZSDdsKqQacCcuHhCGgcHHsqiZzIKkJvnNjgUuGFUVvufoiCIBIkKRvVmtlBbeExEqoFUumMJHxcZhHJcdjbBJDwWdDZqsSXLJjuLzMZwGglVqSIimMkOgmQOCRrcCcnLwELltWUuYRuDtLlTsSeakipPYsSvVyIPpKGdVEenNvxhHXXgeECpiIqdDFxLGRraZzEfFeEeTzxQkwaAWsgGhHeCHhpvwYWDdPpuUgGWSsbBwtTwuNnNZzuEJjJNdDVvyVvYneEcaNnAZOBbmMpzXxWkKwOdizMTACuOiIoUQqXoOwyyZzJjUvkZhHpPkVtTnqmAaMyxSffJMmZbBrUuRuUCJjpPPcRXiISnEAaDdednqLlLOMmJnNWIuqqQYuUyZCclLzZfjJoXxORdpPaADQqrUnYxXjkKrAaRJNoOnJdDjyUuPAapXxNuBvzmfMpTfuUiNFNnmxoFBbfOcCDtmMTnmaQRqbrxOgGgGHiIUCcXzqJWvcCBhHfzZpPGqQbqQFLNHhIinNwWbtTxqQuUuUIdrpPRHHhRrRxlXtfFHhlzZxcgGxXGgxKkYqQdDyQxjJjuNnhIodwVfjJfFmYyBPpPoOwVIeEiezpbkBbaALBbWwllCvfSsdZOoJjWuDdUrRvVwlLzyYvYpPjJsSUiIfukuUfFmtTnfKFNZgHnPpvEezcbSHXxhsBihZzByYeHhUeuUVvJTtmyrVkEkbgDdwWfPnNfFppvVluTVLlhDdiIbBDbBqHCcCcKAWwXnCMkKoyjTKsSAtFfZoOziITtNmJQqTnNigkWuUTtwKYeDdECcVXqQdBbhHQsBWXxmMaDdtTSnyYOAQuJzUCweRrmKFrjxrNNfFnndYeEKkrpPlfFfaAxmMXxPCthHwbcCBEKLevGLNTDihkKIvQdnXcaAxXCVtaFleMDoOTtQqxXRpbBPRcQdyYZxFSsnNckwWQTQWwdDAafHnFNyCVvdRrDPWwwZeujJVvwBbWHhkRBVpPVoeEOPpPkKgGrLnLltlLTcCeYyQdgGjqSfBGgVkKTWQEnNuBbHLVvCcVzZJjNZzAkKVvcJjCcvifwIDdivtTAtNndDFwlOolLjJzAbvjNPxXuFYDdyDdrhtojCfFcuUUuoOTIjBbxxXXSbBCciZuxkJkKxNDdDNnbBeEdnRHuUsSbBTthHcPWhHsAaSZzZxAaiqzZfFjJeFxXrnWTtaAIiiScqQuQGgqQDrWwRlyPpYLtTEoBbOpPlLNrDsSdzoWwfEeQDdqsCcSEkeEBjIhHFssapPtTcxXlMhHmRrMmgRrOohHYKFfvlLmEeBnzJVRxTGHkkKKeEqRrSZPzGgGaAgZplIyqAamMVvWdddDDKrpdGCeEeVZWwyYzKrLLrWMLNRzAoOagiIAaGNPdLlKLENsRDGwVVgxXGdAuLlOOnDdLlaUSSssItTmwJVRfcKkCIimMxNnoUhGgSpPjwuceEeaKkrRSscCuUUcZPpSjjQBvFfSLlHtrRrpffZzLoSNACcnNTtagsrRSJPJTVqQvYyCqjJQRAaBblTVvpEetbBwWLHGZzLPvVtThHgGCqLzuJjUOCBbDHhHCaXmDsSMJvVAbqQUHhuQqdDGClLwDEedWOoIYwQbQcuJkmxXMKPUuchHyYhTxTtSsldRrkKDXqQEuMBTpPtJedStTjtTAwWvVLOQZElsSoSbTToJjtTkFYyAaZzOyVvVkdDKJKSCcIYtTzZhDPYTUuBboOcmvzzZmdhHGajteEQWwMmMmpPLJjudlLFfHnTxXzZSbSERrehbKkbdDgGElRKDcxQqXhnBJjQkbvVvqQuFDdfCctHwPSspsSMLxnQboInUAMNnLxPXxbBXTrgTqLluwZzqQWzbDdsSxXMguTjJstTioOdfFDIRAvhWwnGOzioOIJQqJGeEihTtVNnvfFtBbwWLxfFhjJHLFyuHKmjaHozLlZNnaFlLWwrtQybBkKerBbXiIgxXAQYxrRxkOqpPbBCcgedDxWvVhNpBbjJgGfpOoPjUqYjJxhHXtCdvBaAuTzZKpPJkKLFflIatJgGoOrmMRYgGPAJxRlLYrRUuiILpwQqcCgXxLqQvlYEQBTttRroOCOGPpJaAgDWxAmPhnQqNHfFRjEeGBbCQTtqlUuqQhHAaVqQnSsNvxXSsuJjURrMmxJjPWYywlLpFfuUwpLlHeJbBjVyYOokQBjzaABvVGIiSdDTtvVhCqSwrRRRrZzZzonuUNolZymhZqbTtTbBjJVvYyXxoGgzZDdXOhHfFoPwWponlLAUNlJbBjLYXAUuvsSOYypwsLFfNktkKwBbIioKtBtYyhiIkKHKkThLkjJFaAEzZjJlKMmkKXBJjmjJqGzfyWwnAkDaAseEUuTtwWsRcCFiINnjJIrsSLlrakfgGPpnTtMfcnaQygGRIiRWwWcKkflkKLBbYyFQUuqJjgGpPvLlBmFWwpPgGxRKQzuFBcwWRUfePbCcBAquUngGgBVcEiQUuPpDMmyxXYyqpwWiJOoNnwdNVvSLOaEiIqQezeEZHcQoPClLLuUlcuuqQOoVvsUVvRIVvWdDwwgGaAbXxIKyMmYeDOrRDrOyGgYaAuzLlNZzAMmpkasSQqNnAxCcXmycCgkZzKvVAYmwRUuKyqytAaDWCqoNiIKkSsnJoOTXIitrdDRhboDxaAaUuhzfJjQqDDdxhkrbhAaHJjuUGUlVzcMmKIJjiQRrqkdRYQTuZeBMqPvzZOrRMjdSCDEeVCcEEeULeEEwXxGgXhWeiMmIvVbBJjnsSEXpjJeEOocCXpMaAtTmPaQrOkKoRosXbBXxxCjJcmMNWwnlgeLQHcCCeETtcRrPzWwsXHjKkqRGouUOoFSTBbbTtBBjvOormMRDdOoVwSZasqsSJqQNVnMmepXFcCXMdDTHgfFrBJjEQNRrRrhHoOoQCDAaCcdygdRrsCUNwWnXwWxrRoBbiAaIkaYyTyAaZZhHDVmLuUtPpTmVxXLyYlLohmJQqXkziDpLSYAaynNuDdTtVvEeMmDcCNfFJLljZHhAaqQWwLlxLlWneErxgnNoNnsIzdtTjJUEOtFduUwWDyoSshLrPbVvoOEeHhvVyMmYSpzsSZPKiIbBkNnZzLItMzZhUuSsQqHfnLIilYLsmMmYyHhazcmUuswtsKkBbvuUJzKOofFaApEjJgGUBbXxeEuvSdDsxhHqQXVeeELubBjAFTtfaeEgGtEGzQQryWlLwQYoOcyvzZRXxratmdgGDdmMDMFfmAFiXxrRwTtzoshjJHSuuiodDOIyYWAYZudzrqujtTmFlNXxABbzZCcdNnQqgriIGkLlPpzdtjlLPWwkpJjBbawLlUYLzbRTrtTxiPoVGgWrRlndDNEMDRrEpSauLwYcCyWyYtjJTiKkIPpAOokSsKHMmwtkKoMvWUulLMmeEnNevMGCeYsSJjPqNDdnKkGwWakJPdhbzIZzFIiMXLlhhNmkCVvZMBEcCvaQXhHKBzpDJmcCMmMaAHhZLsShMmrkKRuHhRChNnHsxMJcwWDdFUuPpJWwHyYGqjzZnNcWPlgtXxjNSjfNcCjJnPklMXGgkEsSeKSdvVmMDrPiItTpDqQAAagPHxXqCcALlZTtkEGnOaqzHxXsEeSUMmRrzZgGMmQqdbrsIzcbBClaAHggpEsdDqQwWSHSRraADXHhhHsZpPndDdDllvHEhHehVMzcCkKZcMJiIAFPpxAaeUlBbbBzVcatHhpgihRwDhHrjJcmTvlMNCQqwmXxMTPpEdCqQcQrXxPQoOqpDlGgLkKIiXkvasSAVKhNxNnjzWwnNZVvLgGHhlzTULoQqJbedDZZyHhAPVvVnYxYSOeCPJdDdDpEWwenrRQGxXMOoZziCcIsaAIfBHhkrRSsYyhHCMzeAaeRvVqRGNnuJjUgrQFlLOWTHhjJuvVGSpDdYZIiXntTNcCAtTrRwikKiwWIzdDZqLZQqvVvIimRCQqnNqaZzgEzAhHcCkGIiaDdNNnmMSsmMrkpPcCKdfWZYZRrsFfwSFpAuvWRYNbCMgGmKIXFfkEXJTXFPUNzkaZzAKeLEdDPpVUKrKmMgpVuUnNpaeEgGzQYLloOpPpghFftaZFfVKapWPpwijYyJdZztXywWYxjSspPCAaJWSxcPpCXlLUhHuJVcsSCBXUuWUlLhQLlUTtOZODdLdwLhxnDCcdVXwlLoaoCtNHhKsSDdkKRykqvQqWUuUuyclfFLlFACzLtTlWKZpSHbaInbUurSsUuxCcXRJFJlRrLhigFmJYhHdvXlVliIvbHvaAdeXFfGhKWNpPnpPNttTttITtwWijeYysxXLGPvImmtnNDdTLimyLuSptqfFkIYKPMEWUApPaLRoYTHfFIMGgMyjPprJPpiyvVtFfTXxrRjhloOrkPpUdyqQsETLlqTrJjLQqvosSyYHhWYFCiaPfWwgGZzFpAIwPdOPhgUuXKkxbOpPuQAaqFQbBzZqUgMmBUrRxsSwlLUuTttHhTHOGgFGgfnRaAeEnqgGlsSPKCckYyVxXXcdDCxQqvpqQZftWhnSsWDtAlLaTgOoxnxVlurzZCcsSvhHVaApPmuzZuuUrHwXHPFlOzoJSufFPHAaiZZUWoOvMpcYwWOCUmMuqzbBZWJYyBbjaAzZpreEuUoeyKkcCSeEcmMFslLYykjJKWQyYBnpMOvlLFOrznEYPyrkwJuUjQhEhqSspPNnKfdksLaAVJfFWbUSDJJdNJoOTNxdTtEvVBXhTtwbeXkKPpuUYVNNnDYxUuoFyYMGVvQqOSOFWtORAaYqQfRXxrHhoOYYyOhrMKkSmMjJFdFfnBhynqmMiXYQqyxAouRnLxvVNnPSEOiZiIcBILKBbkLyYBdZzDDSkKHhHygvUltuUxTtCCcKXnOdIwWONnJyMOKPNlxZzpwdDwfSAHuUhCXxtTJsZrkbBjJxlRrUjJPlIjBbJxXMmjJizZRrETtUaAfFJjAJLvVFfVAuUbNnSAOENGToFfuhHgDVYyYmMaKkeQbdzZGgPIrbsmeEpPMMmLoOomxuWJNnFfcoYsSgGyxHhCjJttTxwowRmMzZrTJbBDlNVsiZOoAXJjKOOonIyYiwWRyYIaupGOPTWfjqQBJbBjKiICFfdHGzFNnssSCdthKSoyIUSUurhebMDdTtINnAsSsmaAQaGQqgAiCqdXZGsSUcHhItTkKKqpLszXgGxYlcCGEesvUHVyRaArPCAwwWKfgMnsiIXxQqmMUubtSOPVSZGXxgwPpWMzhMmqQbZzuUbTtYmWwMydtzFvVxXflTJvttTTPpDNThHIhJjHTzpcfojHAvbCBqCFIjJipPiGKAKkaSskgYoOuYeQhHCcSWTmnIZyhHainyGgZqQqQeVXfAzChHcZyYwDdZiIWwzOkIHyYnkkYdmhGPxVQXxqJjMmJjlSvHhJSFtNnWKEejfyYFAjJaWJMdgGDHpUWOocBbhPRYyZDafMTtAHuBWyNMlLfFepPsSbQqdDLADQPBrJjsxcVvlLOeEoxkWeEVJjvVMxXEVOhqQDdFamhHFXbPmRrsSSVYZFEeONnoQZeZgSsOodzKTtTmMpwWApMIurMHheKOEoOefFokEHHhPpAeEVvOuPRoYLIkKiQNgFnNKwMDdRNfFRrcKZhUiIUTgJNLxpnjJrKkjJiGkJbBzZMmjkKocCOgGNNLzZlXYLPoOANcGbWKhoRPprSZaAzLlzZuexmMVNnsLlSMmaBbFfAvXEvNhgbTkQPaAhnCcNnOoNcwnqyYkcAIGjcbRrMmdvaOFfogvuqrsdxywKLCCcVvFfMwMdLCcPaHboaAXIOWpPJjIDdQyxZvViMZzPpkRNnwbBRXLGgXzZHhqhUbJVvtTsSGIfFLdQuUqFfzTZJnNjzrRGgtHVRrFwJyFpPeWzZfcZMmEYztWlLvwWrlafPRouUOKVGpZznNPeJwbATyAadLRRWmFfMWTFgGqQERYyitBbgJjSRICQJAawWjwWORYsgReQSsnNqxYxXLlSKksObBVaAdfFDOowoOxfHhXxFUsZgtzZTgIyPrRUuFpPAafpyMsSrJphVbBwPIYQqmDvaDZpMmMnNmzZLJaCDQbzZpPWXWGgwXVRVMAasOFynRreBbPQqfZvgjiIJOoaAhTtaAKAakyoNnOBbIiJjVvVuVElcSsCmdDYyhuaysKmbDqQmMVFfTtGgXxnNodHhiqkiMmYRmMkbIiqLaUhiyYmXBbgGPwtTqYyQWpwWYyLlYbfkeiHIsSsSyLlVvVGmMXeQkHMGgGaAkvVWworZpPzmnSCFbAExaAuNzetvVTDmEWJoqmMhHYcCrcymTNDJroZfArRzZecaRrcCVkKzaLwAqAaQNlVvykDqQqlLlQxikzTwPNBbXxUuxXnprRWtnfXxDhaFaQqCHhDCctBeEknLnBhRrMzZiqQInLlHVmMoEeSsCzGLHOADuUkVvcCBbEhhQqHMvVTOvVRIEsjCcgGrEDvVdMmPpKJLuLBCJjcfTtsTUZUVdnUAgpcCPGcCaTYSsgGXInUueFzCVVtcCcZJyKaAdYYvAajmOEWTFEeJjfdhHMyZvVzWMvVmWTgbLlqQfcJhcJjFfCBbwWFDdvKBbKkkFFfDlLdfvVJdLkAtZzTbchHCcEWpibgWIYyjobBKtpUuxXZzPOadpPpPzZbSaMwEgaAWDUPpusVIQqiBaAlCUFRSCcUtVXxrRBbLlvICEnTUHhhsAavVSHctzVvZDaVvYyiMfjSsJFtTasOoSKkbPpUrLuHCchFvKNnCNnFdDGBhvVZlzZLzHbpPgfcNnkAaPpVfUlRubBBPMmZzpAfFmIUuVvYyjJAdxuUXTCyGgYutNeceEiTusrfdDucLbvSFfdwGeWmAGSsgsBDAoTkOsSJVLlvZIiziJjyYwGBIPlLweCBaKlDRrjVDdMbBPpmfHuUjCFqQBGtzZwmMwYmDnNtweoMJVyyDmNnMlLkYjXkKxzCGgTvvcZsStTfEbBNixSswWFfytuNDvuvVmVvMzFfBbuGgtLlSJjFblcCUKenMmNjJTtEkgGljbpPBkDdeRJSeirotmasSAqgGQHeKdaTtohVvoOlgRrZMmYycOvhNBbmHbNcClNKMmkKJrRjbnNTdjJcAfiIAHdFNFDhHdfZKvuUrRVIXDdqLQdKYLnSsaTtWlATmMtZvACEaFzORjdntfFMYCRQqyQEeOmGgMjwePpMdtEeTEZnUXeaBfcsNMROKgmhKqMmExGgQaAoOJjqgvYihIEhOoHKbBfFFByftTFWwxMIqQLlHuAlnNQBKryIKQIDOAavdBMkSIxXiYAUHMLhsMmSHUbfFBuevUvOoUVvuLloOYlLcCHlsRrLlSLGVzFpwWEvVNYcCfCcoSmvrvxFfWwgXxGxwCcBqdVvcAbBjloOPzdAVdcCMmyYMyiQqpWvHPjRlLmYmMsSyfPpFYYiGnDdNGzSuXWvoyXEraAGPpSyroqcQlLqirsGfFTRrTtIOorefEFfeMmtwwrrlDxXYtayAaYBWRrjZzEVZzvpPgvCckiIrpFALRVwWwTZyezOowWQqCFwEfYjWfSsSsvhJjZQAdDaqDligsSgGBbXxjBuHQxlxrWrKmIchHMmCtTzXYqiuUpPPpwnNoZzixOBhApNnNpPnlDmWmBbBbclkWYXDSRQUuhHUVZzGAVDBCJgiaCKQNWCHpqKtHhBGHBbnYmrRMyVUBbOzZoJiywWYIEejspPYyOHZvVzkwBgCnaplyxnngGKgICcRFWwfNYyQqPXlnjGtuuHlLaAzkPpCnrmWkfGnqlyOrRCcrHhpUYyoaQqpPhmRMmUimPIiaoOtTPtgGkSsZMxXmWwDGzEzqpPfzyvkpPuUKsnDdLlNMdDwWpBxfhHMAeEfHovebBPQqpmkKDdxXnNvwKXCXSTtRbpMmZEezqdaCclgGmMvVBEmnYtTwbUhaKkmFAdzrMmpHCXxwuiFfIPSRrFfsxXhmjwJPpYykLlyYwTucCUfsjVsLBbvkKXpgHMDLliIyKGgKNhiKoWDdVvxXdDyYaFxvwWEiLlIQqwWztTYNIAYzaAiNnNMMmYytwsZzoOqEyUyIfcQbzZcBVahJOFCsSPZtitndVjzZtLWwZTDrRBBUuYyXxZzHZaAmODdSsozEeWJjwsTtsSvppPosTBrRSEeNmGFkWacpYmMCcvhAaumMVdDSgLSsyXxQqQqZSlOoKkPewWEQkiCugzxDQcIagLlGAlLnNpPqMSaelLWgGwEimBEHRUusuiYONpPPpnskHTDVvcSfZghDJjckbJjCcJFoOwqQtpofFgPUAirvSsVrRNMmokxazrgGRISvnLdjtWOWXxQqXTczZQqXTTttWwOCjPpwUeEXMOlmMmMmMIiSBNnRiHhpDBqEAyQCcqvdSseEmCcMtTGgGUOtEegneoasBavljauewWLplPpLXxuLQqXKRzSfFUujctTXxasFWdDZzjJdDWPXLnCcpkomYjowWinNDlLoNxBkKPnNpbkcXTLuuUVGTtYiIhsdbllibCYyzIoTtespDdXlNrUOkKgGaEeIpPQKkNYHNsSnbDdNDbBfsmRHoyofFOFqQyroTwEefosoKkgmNlLndDvVeEfOrqQRXydnvyxmMEBjJWcCzZHxbuUWwenNDXntjnMYymDjOojdqQEesuogGUuOBwPpjvlSYyKDFiIkQHeHqSsHhWKRYpEeyeNZRofDdEGgeVomPZzeENrRbVvBbqkKwcCSfkKCsdDYGgEORPwQcoyAaCPfFmVwOMmouJmMjzzIXxRrbJjBfFhpUSssjnNOQqZoLfphMmxWhRUJrRjvVsLlSUMJjGgzZRULvXNXGdwxXNHwTFzkKEeLQNrmMNohGgWQqXSsubGufUkKoBzZbBGHpoyYDpWjJZzcfSsywOVVvlfgjJGyzEzZeZYFHhRtQteSDdFfYDuKRjJLHJWMmwbBYIjRyYJYmbBEemqQiIkKihtZzyOrluOowehHmpkyiKmMmMwWQTiIPsUlYBbMIlMMpPOoiVpglSTtEJTTTndaADwkeEHgxdDEYyMmDVhBVLvbBLxQqVDyGgjMfGIHTtjfHhAajUuOoBNzZZzdDbBiWwyYLlAgGBhsCcBbDQYxXyqdPzkwaAIiRrBZzbFfoOZcCcafBbLnNCYwHhVQKYroOkOolLnEeTcOAOWxXxvNIiXRrHlWUuSsDlozouqsSHuwxbvjTtsdDZGgzwpHhPjcJTDIVBbvPAkvzATHGPhHyqZJjAPGgvPGkRjJjgGeEJkuvCceUWwulEZnPpupfxFcCftjxeKxikLGSoOsglaALlcBnyFPpqfFQfrlXHhxLEewVjJwWNnUsSyYaPrFfRfsWcCSzyzwlLFDcRrNVtTvfFnCRlLnAgXxKaZenOxXodDmxXkTtKMNGAQcrMVPpxXOozlQkKtTZzBbiIIMmKkWauUAzZPpaYyTnNtxRKkqQrzPhHpyJjhHPszZgUTttwoqQfYyrEOoEWwZFfmceEKLlbFWwizZSsShHmgqqQPpNPGgjpcEBbosyXyNoOFfvpaNnYvVwWzzVqQvEBwiIWjOhHlutOoWwqQZJXnHXxPpxQqdmMdDRhHqDetWcnmLGgLhHlVFflLeEtMCRdWrHIGPTNnACvZxXhHZzLuEXfajIimCmLLuUNzSVvxdFfshTteobBOHhPGyYGhnNuULZJYyjiSRBDuQvVqhZQsSAoNgeKzaQhDdpvVGJbBjeElLaYydhHmMRkKsxmLKpNnFJzZsnJPpTGLpDdwFfoOCJQghEejKkfNnSsChHtTjmoOXSbBcrUVvHJjlzaAjdPZqQbjJkKkxqAVoOebmzcKMnfFHHGgxmfiZBHdDDpjKAgoOQpyEwWcgmVEuUwXxgGVCcmOQIikKqTSsWqQoOhalUAsPedmksSKeLwvOpIOoXRtrBZRruUlyuJjWAPKplLJTDZeEKgRZzGDanmMLfmMMJUQKeEzZknNbBNnRZDUzyJjawlLUUOZWIDxXdfUjJwWuMzHhZmaEeMTObBoOopPEeAPpVYCoOyqYHhRSsbBqXxqLlZgeTAauUKUOxFfXouBbRrkUgGuJBbUDdlzZeEPbDdZzvVBmMkZjFfVeQqaALsSrRlrRSTWZzSMCZAOokKMSlMmRrIiyNFsSmTilUuvVssZzSBpRlHOYPpkKfToeuDZPpiSaAOgGGXRNwXMmqRpPUuUurQAazHhqQdHhDNnndUAaoORraSsAslPyYdkKMmIZKxjNnMHOZzlvheNnEHMjJUulMvpsSPEedwWhcCHVvDdzDdzYtAKuyYUOxXnnCfFcNNucpPSRrDSsGYcqOnqebRzZGaAhtmmMFfxpPdhwWzZmFfMHKkDfxPEgGdDNvRroOwWEeUunjQDdSAZzzsiIWSmMsJboOtsOkKofOfFgRsSrrQJBbhBbxOPpoSZpPphnIiNqlEGMmLpaAPIiSOJjqAlLLlxSsHhnQlLqNGgPJjxeNatTApPWwEwyYHzZxWeluzZeRPprvcCdcsDJeyYEmoVmMpZPpzQmbEzUtqyrDVjJvCZZzvyYLfFugBRKZzzZIiHXhHDddFUuZHAPpUnNjJuXdOBNnHTxtWEewjOfFQdDcwgGdTYnNQYZzAaUukrWQqMyzYiIyZaGYMKPaUlufFULunZVvUoRbBdodpPEyUpPuaAYkiBWuUirnyyYYkKNqQuqQSUeEUUupOsSqCVJjmMvQQqLlqhAolsnDWaAjOaACcoIPQFMmfTtvhHVYdGgqIefFCvfOoCFOofcqQyYFbGNQapcCEFMmGgurCbWcCwfUZyYqrRkrXeEfMHhbVyYCQqwrrQqYqANqQCFmGgCcNFKARwWxXRfFijJfrSsSTtzZnNSHhdKhHaeENYTtFZgAaQMbxFsCcSfqQkRrXPpxLevViIfVvKlIiHcCbSvVsTkOWwHCPpchWcCTKnlSWPorRcCVaxynuaNOxOzZDdaAAcCaHhtBQzHMYTtzLOOrgGbBWsQcHLlsKWwkgvVbxXZJPpbqKvNnEYPpkKyhPWQqXnNLzZjJcgJrpMafFVvXxgGXwdGGgjgYcCyochHTbqeyxXLVlGaAWPlyrXjSsSsapyjTAiVvUujMmkBbIiFftUAawWiIJjwWbJCcjVDnNcTyQuJFPnHwXYyEGQoKXXyqaGTtBzZbxRFfEsSYqiIfFjJTRsSfWwAlLdDGgOhAMmdDJMvVkhUYRrflXleEwSsWTHIgjBbSsjYyZoVvgNwVvWmMQqCcHVpParStUGmBiIIiZUQtGRtxwWpXQqlBLlbmadDuNiOBqNXlmbBpPWhAaqQTUVBKqoObNYyHMmCdkIirLFfUueSshSQqsHBBHsPpBstqQNhXxDUlMmrRqnNTJAghHDnNqQMZEScCseVMCtyTtptTdscCSlGgUuLHIiyPhHqZzqQQpiskjYyuUvYofkKvaeEaUuAAVKOroORHhtkKtBsOxXfFLWwezqvVoFBaoMmVTtvOAbRrflaJsDEjbmUexdCcOoDRPprLXtHGgCpjGLlgUwWCqBqWyicgkKOoBaIijwOoWmdMxTEtAchdfFRrcJjoZeElEZzKkeQHhcCfFcmMZzjJpRrlAYyaghpPlBbTPQqpgGPyYtLPpqQrkKciIAuUatjpBXxbOojGnsgGOjJliFFYyffIFFPRThsVbwWAFsSOofaqFEeXxfEeJJszCxqYymMQXSsJjVvuuUcUuQqCeEmMAgaAGEdDCSsBbUWJsHHHNnhhuMmaAOHhjJXFfFrvjyYQquUaAWMhHUuiybBYuANozZomMUUuaMmDvSXPpAlLEeaxsvWgdrSneEfFMmYyelkDxXpnZrZmMSLlsznuiIUlQqmCcwgGRSslldDfFnNRkHhvEcgDPRRrkDTtOrRoYywLzZlQYSsieELYyzsQhgtlLXrtTvjZNbMlLVFMmfZWwzgGkyGuURrLStTsCRyYbSsgGBrAdaADScHBcCbhvVAauUbkKBCwWSfQMmqiJkKbTtKevVpPKkFoOOZnNOoRnelIiwWLdqUCsIlLyYwoONIZziRfEzYySsZQIXzEemoOMwBbpHhEeCvVYyhJjrXtbBTUujKXUzIBpPbsJitvvVVCcJOTHQqfFRfZzXtiITxUMmpnJVBaeEUuZuUeJjELWfZzNztcCTZnTLlaVWFIVPpCanvlhhHUeUuEePpqwtvbbBFgGshHQJHhcCkKDIwWSdviIVVvDsinKkNqErHhRNlRBbpvvbrKUEzWbMmBvkKVpcEeYnfNGghUUuuFEedDNnjYyJbBqtqKvVCaKkAfqcCQVvqQXzDqCwWrrVhHaArRvcCdmEQqLbBKkHhHhfAZhHzTvxKkNDqGPpgViJjHIdtBzZbndDllLcCgUuVEJjSAaslkKaAeEkeUJNnjuEeWmMTcpiINngGXFYyLfFRyDlLHfFhRXJRfkMEWiIUumMcukKkQqKZjUqaoNsAXxwPpbSqDUuzZVvxvyGnNIQqtjMnTaktJYOmcNxazZkhvVYyQdmHhHrUuRhMgGPpDdHvMUumtULPrRFGTXxtTtBKLlzZeKvRYMTfFDnNUudoOtjIiuUNyYtTnvVEufFHhEbYYyPpyHIjCZzMtTmcuUJwWCBbqQZVNhhHGznfkFNdMmdDDuDdUMKUFnNuLlyVCcdDEpPeDyYFVLlcLJjzZUuZzuUHhwWZzKBeEPuUZUuEvWpbMFVvvWhHFflLDrROYyiQqHLlUgGJXqQqXCbTtrRBXuUEQqeADdLlaLLlTxLXpDDddWwPyhQqHYrhDiiIODdoXlLBnlTtfFfBgzZUuFRrwWFfbZzVDdwjQZONnQqoxuhzZoXRBQrqAMYyNdUUuuXbBMfnCShHscIFxXzZynNKkFwWfuUtTXxYtRrPmGgFkKMZVbFzQekKEUiwlLjolQNjJDNsxwWrCpgFfGPtTpczZziIjFFsXYmMQQqNvKzKVuyYUueEYYWxuUcatmZIDaAoYyZPomMzCNQeRrqQEqnjeUxXiInaAUVyYvyjJBkKbWVPDvkKVdVXxvcZzErRaASCcKqVvXnNZFiIFfftJkKjxXAUuggGlXfQyYGgPcGdDxDgAEzZMmMmdUrywTcCFfSsegGiIWzZlNeEbBCcoqMGoRrKiIsQvsSLWzmTGgtZOoEelUlxtTsSRrQzsSCjmTtMzCXyYhTtjThHtsSfrRrROQDdeJjMmGgXmMLThlLHMaAribicIrROhHMOoGgPcCCcpmJVjQAYyLxXlacCShsShUCSsAkzprXDZZzkywqQbBaEUuOeVkKvbIiBjBcCbfhTtLlFfgYyJRYoCcCOoTtYycOyrkGiIAagKjfFOAaxApnVOmhaABKkzZzGgiIsaYyASFAahTSDfFPpkKDddmMxpPcbBBtTmMRTmJNnkfFnqvvuCcwWmZwkKkKRwzxcCxXTyLzZaVcCalLpPcpMSsvWXJgPpOziOwWtOoKnNkVfFyIyYisiNBdYqQyxXUunFnNEtTcCBuEeLlUPpOpPMEedoOxUuFfeLQfZNwRWtKkmcCAHEehacCPJrNIVvJeYCKhoHZyKkKkjJgaArkZeEKrRtuKkpEfFmuxXCcUMCjJlmUutscCYyOoSsQqIiOmMlLoFfYylIkhLKaAkTvzZVqQNEeWwsSUZLlSsHdDhuFasSpPduKNnsekKsDdJhpEKkThHeHhneBbPXxeTaAtWsWLldDTtwfGoOeEbUBBwWuUuUXxHhoTtOWTAyMmZHhzKLSPAPdgGtDzZAXxLjjiELlOoMmGKDDxrhHWwWwJvjOoJaAVjhpJmNnEcdDBbChHOoQqYyuHhUlQqXazzOJYyjRZzzZwkRDFxXzpeArRBsYIiySblXxeeIxIqVvVaHhARrUgvVuIhHaALliVYyLTIihRrHtkJjQwIxHITtpJpwWPZSuUsJjJZaDZzDddAzwiIEgGJRPprByWwAaZaAzQdBhHFffFMmboMljJLmGWwEFfeQWHhsQsSAapgllLvVXxdDhhdNMoOmMNVIaoOnNiWGPRdYyaSsUlLrDYGgylLzZdRuhHOAKkaWwFfFhaAUuDdaPwWvVpvPptdfCcFDfFNCrlfKkgYyLpPlGUusSHhqQuPUcOogGNJeCyYnZaSsFdDfTtAdgGIiVMoLlLpbBPUvVvMxXmMmkKXaNnrRKkAwdDfYcCLlfFEeyWkLlmcCMCxGhHgyKdHhDRBbTrRxeEppPTstnZAalffEeFfDubOBosIisSavVAHNnmTbBgNqgcCGkEeyYTtuUnYXxeEfFyNHhoOdlHzZzgGAcNnHGgZGretQqAdcClLUkKtbMXxmkKshvVwSshJCnNJtTenyFRMNnElDhWwimhHQqvVjJmUFgGfSsdjLChWjJgzYijlLJEecvfkvRQlSsDdLqpXtgrGgUQUyYMwcCoUxZOLmGGgvTtfFfpPksbfYqQuiHZJeAacNpRrOTuUczYhHybCcFfjpPPpJKVyYGghrXxRfuUQyYiIfOoJFdwqWmgGMNtgYHXiwKWkDUudKqyYQBMmgGvhHVYFfRrhHzqQZyIiggGHwWuSpQqPsIfpvoicsSpPCIpPKMnIVIivVJIiPWrzVujJcuFgGNnQSYUGUQqVQqhWWwZwauUapDpPTNngFSsLlLAfFcCRrZztTDLxiIdDpaEKkHeCEefFCxbBiRrxoOVuUttTxYyRERbBMVvmqQGgXxRrAsLlbBPSjJZRVwWvlwDdWzAaVVvzZKkcHhnNTvxPEuUepPEHhHnNmfKSlLsYykALaAaAlnAEeuUOslLSVvSFfDdFvRWSsqiIQdDSsTaALWGcOsuOSdHqaCcSrOoZYpPLlJGgwWWwIuKkYyZnNWwVgGpPJjiIjungyYmMGhHSviIVTtvALlDbATdKkXxqnNQoHJdmPFpPfOoqcFTMbBeFumMCcNdRrIieEpPQtTeiIzZGMSMlLmeuUZDDdesHhSEDqUJSszOXxTjLSsbLHKuUkhlcqQMXgGWwmMjJHhuUWhHYxMmQVvIicKMjiIJmkAaCKxXuKQqcwWCeQqFFjDQwSQBtFfBbTiuRrUueEXxNncCXxNKkPTtpsRrwWScCYydjXGzBVWeGgGZgCcWIsQZzqDrGgRdTxXHhEetpYyYyMwNnEevVzebZzBgGXxgEetTyYBblLNOVvVLlvxkSsoOLGgoKBbkOHfFIOoinNiIkezZkKERwWMmVvpPeErqPpqqQAlXRLtTinNcCBRJjVIiGdIiAGecCXVcyYFfJjDpYkxSjJnNtTQQicYQqVvyDxXdTNBLloGGyvoOpPvVoJvNlNnLGgIiwBbWYvoGkKpPCWDbODjJtiIJjXGtpPvVTrRbauUIBErFNThHPuFEeiIfUPppgRrGPptSsUIZzFfiDPpWwQqdaAKqQkAaVveElLBUuZFfQpPoODgaAgGGkHhrRtTzvVYXxXZzxuUrRCcjCaHCuUcQtTUuqTtXGpAajgQgSQxXkJjArRakuPwWOHjJFfhHXjYyUuJqkxgWmGiIgkZzdDDysBIjDdOvYOoJjZzYKkJjyCcCwRFeyqKyYauAazZZMyYBupjTtbBTtJpvVXuwWZwWzjGgxXZzGgPItTPpiptWwuUCxXIaegGEAyYzSHhbBsZHhIwWuJKRugHhGUDdgXxGGNngpPfFiIdDKcQqGgCvVsPpboODsSMmdDlLdIlKFWeJOoddDWmFnNEIRrieouUHhgGfVvGgLlYyAaFgGYxXJYQqyjymLlPpJjnBYhHybDTtdvVtTvVcCIiqbBQmDEvRaAtdDMkOZuoOStRdiIuCZjlRrUvVuPprRnNnrgrtVvCcQCXxcUPnNYyJIPpgiDCdDaYyNCcBGgxxSBbsKHFckYhHyEpPQqexdFfDGgEeKkgMwwznVKRlFfvVExRYyrdDoOGgtTiLbBlIXergvVtwWHhkIirHhRXvhHvMmHjGXYuUNnyuGvsSWwrRkFnNEfFUUkKBbmMfkIeFAVIsSuUcCRrCbgGhHBuUqQoMrRpFRrAJfFYtTXVpPhHEEACniINoPCIDdhSsSKVHzZfJjqQKknyYmSsMWANaAnhHrbSAarRNREvVYLlqQcPpiIMHVvhtMmRrTtSAaZlvVLPfOoNfRtWwyZSszYTcCDVvbBYoOyKkgvPnaLlAKlqdDunGIahnNSDdHXxPLHIidSsVurRKmMrrbjeEOChTtcbwWgtrMYuwzvuUHhaqMIibQqaaAncdsybWLwWnVaqvxXuiIqhMJjmKKkJjqtXxTEqhPZzgqeEQGzjocCuUcrRKJjkuTuUtRrfHhKJqQLlrRNRXxrRrLlKqQFfnQoOZzPXxoOfTtFgGpNjJXVvQeEmXDGgdaAeEeyYExYkIXxyYeEiXOkKhzLZztTNnleETqQuloOyZzbeERQqePBpPlLelLSULlZzarDGejEUNnoXxWwOugiGgFfpBboSIikKwWwxmiBOlLiIVvXGgxouUwnUYWwtTmtTMAqQvoBbFWwfogGIXhnNHxiOOABZzXjmyswVvWGgIJaAPpFbBGqYGUbnNoXzbxXtcCvVvVIPpzZWJHhjIfFaELleAhHLxWYywXVvPplHhogxXOoCcXaCNZWfBMmEUIhHihHDkywQrRqvoyYMqZVrHlMFfyMUknddBGVvbzrRwkTtKYCiIwLTtlKktirREeHhgGWmFrAxZzAalLXoOnwLSdDIisKkFlLeomGsowzZWpFfPngqBbgXNxMPHaASshHvvVVRsSrhHlQFWeEwtSzVvCcZJgxXGGgVvjoOsxFfHhicAaCFfELPkKpcqyRrYQUjAaFbBPkSzZLeZNbBnzNIihHwWoSsATWPpmMwtaDFfZzMmMmdOKVvrnNRhHUItxYyoOVGsIVvBcCbiyYvVOomDYybcGoVvOghHPmMpIzZmuUMawhcVpPJhQqXtTxZzmdDnNCTtcuIcCibBvEEejgLbBGgbIGbbBBTKkFfBiIXvVKVvYykxmglVfCeEipNnPrRIzZcCpPNnINniIuoOSsRWwlGgLHpGipucyiYyFQquCxXcVXxGgwIdDiKLVvlFfkftTFEezfhBbGxXLJjpWvblLGgUOhHPpoOZzMkEbBexWwKLjJlNnfrRWQXxqEDdefaAFwFlzZGjBbJRrmMNntPpTtTbqDnNdxFfXdDVIHqQrvVRMkBbszZwWJGgVFfvjSAaaAJjBbAQGfFQqMmsUuEdDesxgGOobBZXxzkzlxXBTtYIimjJyeEbJZzsSjqJjhHcCLzeGgIvVqQWkvVJoOCjJCcSsUuQqMuRrgHfFqQRrhGUcYKkuZzzZQqJjUJDdlLkKqLrhYyluyYULYywHhtTbBElLYyaAnNFOotgGObBoTKkPRrOpPotTkXxKRfLlFfDWwcJOqRrQoHdxXDWlLqQDXxddyjmQqqQYyYyqJMmxCcdoODVvbBlLFfIiuUEYyoOpPwrRIiWJjidHhTCULcbBCxXEMlwgYqumMBbpPYyUQIInNiMZzmsSegGeEvVEceRrqQPpJDRlMuATGTCyYcWwzZtgdjJZAbiSsCiIcdDDSlzsSMOeNKUTTkKEetzZLlUudFIifTttcCTEeyYEBbMIiPpSpPALjJVXyRHpPvYioOZNnvhHVRrjMsItTudFfJjJvdDNUlLFjnUDdunyiMmcCrWwBdtTDbZBxCcGbJomMhHOMkbmMBVAavBbDbZoORsSrwWqQvUgGubBqQztTZMmSsvYyFfwUuHghkKHuBAjJwXxJIJvVxPpwPpKlZzLLlkuiCciNnIwNnLLEObCIcENnVTyYdoOnNIRriHuTjCDdpzuUZJjdYDBbdLlwvQqDdmMxUgGfCgzHrRgmePrRpuKkigGSQqQqkWCjJrNPpZznRBbYyCSsudWGmMzZAGRQqcvGmMLlldDBBUQquPvfFRnQsAnNfqQQMmcgpPGbtyWwMmYNkKiIQyYcLbBlCqvVZEEsSZzPTZSJjVcCvpleEZzLisSAEhHeThRrrRWwjoOLlNnJuiKvVkpWNkQMmEeHhpVUuqQqsRKCckrGgoOZbnNwWBpPgOJjoLlWwaJjIilffsbBMmSGDCcgGmpPKEeQvVqkHLZBbaRrAFzdDeElKDdHKMmKJBbjPpnNmAVtTvBoUpPHMmVvhfFqQuqQEeoOsSciXtTxwsdIfGjpKwqKjaAzxXnEiZAtTvVanEemZAaVvKXxkTtgWwlxCFAaYykKfKtlLWwfFBbgTtBXuUGfcCaiuFJMmVvjfUgHhGOofaJWFfatoOTAUdDpILcCwCIicWBFfoBHhbObKEekqSkzZjJaCcpIiMmPpPWmguUkzfdDBxXtToJYyzPichlyYAaJjUeHhdfFHhlLEeVRvVFmMJSsjMgGhtXtQqJjTWXPjJphHuvBbpPqQVSsNctvyTdDtsPpHCgGcwWWwybBeYNnYOoSsyVUIirSqvVoOoOjVjJvJkkzDqSzZsDMmdQdSsejtTGgJEfFxrhsSsBqQVEpPjJjJjwWohvVZvVAaSuHhLcaNNnnmMyMmkqQYDdyzOoPpnPpNZKSdWiCSstJjTkKcCncCneExXTpPOsSoUuHhJzZjpPwRFCcJjHhHhmEeYycCbxpPnnEjkKJevHRrNlLDhgyYPlyYLVXnnNvVcCZpPkkKKJlLqQjzZXGglFfJTFeQqhfFHEHbBTtGgKkhRQqrfKwWrRsSWwkIcCDdfFBbAacCgEeGsqQGzTrRujJqQGeoopPceIYHhuUlIywWYQqiQqbompPwWLlBbIeEqILlitsONnZZzJonfkKxtTRroHhrEkKePdgFfGDpEdDhUukKQUdrRLFfXcgeEibpSrqQdDJjwTtWnbAjoKOocCkiCcCpUuqIaAiyoDdOsGgEeSKwHHhNMIwTXoZzaAociQqIufFKkUCvfmLTwWVgeEGvjJfFhjJiBbAnhMvVjJmHNxwWsmMSSLZDdzKkmQqYcCAsSoUuKlLPPshPpGRrBxXbgEoOcCRreoOelcCBSsbKPyQpPxtXxTvvbZzBKoWwEtTGMmEegkKHhFfYnNcCAsSJhHjaRrjJkKNLaBQKkkKqbAlvBTtbnNtywWYuUpPpPITQqoOXzZKiYqQYTtyqDOFEqlLWKkkKzZzRrZMRrXxmwmMdDLleNcChuMmDdUAabIKkeEvBFfTteqQSsRrRrToOcuDdmkKWNnRPpjJBbvVXesoOSTtUuoRrQnHhhZfFAgGDPphpuUmQqdDXkKnNeGLvVeExXSAaieEIeiIqNnUujJSBbUwWuKkoObBXRvVoGgnklLKOoYyGgSVdDLlGBbBbgZzvsnNLRJjBmMDdMmAarRDdbrlyWPpKCckbBDqQduUtTGFrRMmfaAoOLGWysSxyCPrLKkAabBoOlsSrRRANQqnuogYyCcSkKdfFDJLIOoiCcKklDdQpPHhJjIvVyjJNtToeEOoBbmoOZzWLliQqaAIaxlLQqXakkZuqhUBoOFfXxcKkCiISsDdbwhAoOKkwWaPzrRBXSseEuUxbVoQqNpQLXtLlkuUwRrvfJjXxSDdsFaAbFfpjJJjwOoEezHxXBgcCJjsSxIHhdqQDhTzXYyaAzXxuLOoCcwWnNTVvMwDdbVoqNnCYVvyIiSFfsVOVdDvofTGztFbiIMmOowWHOaAuCUurIiSsuUFVvIivdDvRpPdIkKiDrlLHaARrjJlLhnNeEBbfFQuUBbmSsPsFBJRrmRCvVRGsSgrpPcOowQqFfWMuKKRFIKDHhMfFkzetPSGgLDdgGWUuzcCXHeDtTdbIMARrIiiSgpVSsXCnZrIuXZzbycjkKJCYCcfaAKkHDXxdiBLlbNnCgXxTOoavVHVvuUdbZnfFNwWyiIRiIpbBgGPUusSrrhEeHRbXxBbiItUHhuIiTgGfFhHqHKoOtTkhQxrrZjJZRoGqbKkfaAEeQrNDaaAMCXqQxcmFyYKJsncRLlCRQqsSrcoOrAasSYwuBRrFfHDHhdhUntgGkKxbPzpPZpkKiIiItTAZzqQJAarRXxjMrRwBbyepPFfEYyZzwWrRTtUDdDVjoHfFyDdhHQqnqQGgAaHQqhOYGcUudCcvsCiIxXczZkKoDdOBZHwWEPDdpeYyPpwWdDmFRrfMjJYAayqKkQdtTLdByfSNeEjlLJwWRzZrRqfFWmFfKkOopEelZUktTqQdEeEndDrRKsSkiTpyELlhHeoOsncreaUuAGrJjRIidJChlLHPhHzSrRqlLsSXxzZQWQqIuajJAUkKwWlLEvDdVkWuUEewKlSzZOoZIYyzZbwWDdMmgrRozOoovVOUuFIcNwWOoyhcCHWrWcCjJXUpPrReXxWxXgGwPTlLhHoAoOaOUuuUtdiIQFFfNntLlaHhxXASsTupenNERMmXdDDSpdwgGWDSJjsNnrRrRVLlbBbccmwtUudDcClMTtACWwjJsSpPpLWwsSloWwrRSsCcRrGnNgGuUhgDdGPpRzZNPpBoKkOCbQzFfsKkDdSPpchHKxXzUUdDuuYyaAsSkxKkrRrNaAnXTtxGnNCcxXaaAOopdDOowWCcssSsnVvsfpPRvbeEIbBCkaADdxWDdNEenwRrKkEeXAZFfDtpPWwTdzGgOobBfEezZYyzbhHtTNmEtTecCqQVPphRBnNbryYMpPSsrREAaAaPtKkQqIRJjrsgpPGSMfFpfHhFMmVfFMmvVWIZtTzaAiwkKvBbPDnNdDQqWWwwhHXZzoHhCsScFIiXxfzZMWwHUuhMOSJjsySsYfKlLHhOweEdDWWwgCLbBNnnLllbiUXxulvVgCOMmgIiGqQUuxGbBAaGgoNmMfMmnPWwgnNIqsLlLlUJjuSQCcDXqQHzZgCcnNGHhhVvQSbLBuOfYyBbQsSqFXxojAgGvrRqdDQcvlLVxXpKGgEUuekGgKCrpXxMmkSLlNHFfVvhnqVrbBtTaAZjkcCKJEemijJIMzKYJjJjEeVXTtxAavmlCcKwWnPJjpNCcvVbqGwWgQSWwaloOLAstfAxXWwfWwBDdSsfFUuMqQpmQgFwWZzQCkKxGbBcCgWwnNAaXSsfSGmwWiIVFfFuUeiIEpPZzrpPTtNwaAuUGyYJjgeZzEfQTtqWwFTtAatiAaSMmGgoOsNndrtTsSQqgKkmwWXuUlLxmMlLAaHhCctXxwWwrRriIEeJjbfFOofFJMmqqLKklQQQqfOoCcWwoocDgSscYGEyRbEidTEJjebbBBjJWwMILlivVJHXxyFflxXPUGrdYyDcRrCUKkuNnoORgIiuIipjJPpLkqRrQWPpBzZpPLlLlLZzVDdpLlmsSNnuUiILTqQeMmCcsSmMDfFrIFfivnjJjJNtTjJiIjnNsSJVUGgQXZztDdZFfSsHhqrgGRQvOoxXSCcsVuGZqQxXzgEeNnSsyXERrdDDdqQWweMmVDdLltTvkyYKiIGgbBSsKkcUutTHhOoCuaAUkKyvdDVgvVGqkBbCcKiIvVxNnuUdeEDXglLFnPVvpzAaZzSspHhCcPHhPpXNnfFZzzZxjJaWwATFfbByHLljUuGjJzZuUguUfLlFBbOvVoPBbdDpjjlaALejJPpHTtQqCcRVvrlyeExXGHfMHhmgGFhvVsHMmviPpKIqRrPiIIiEFuUaxXwWAbhCcpvpvVPooOWBrRbHyYRKwhHWbBAZTfkKNhHwWkXpmMPKkxKQjsLUumoOMzZRlxbBXbBuvrfFRBEebQPpcwcbpeEPBQCPpgGrnNRntOHfFrCcRgGhtbiIBOdxXNnDgoOVvUujJGKjgyrdGgMmgqmMnRrZpPjvCcVXxeEJgEeSsGihHYcCUfAFfxYyXcziIuUoONZuJjzZvkOsgeEzZVviIBfFoObnNglxXwLlWFfIiLlJeEjFftTZzLLlIrkzZKRcaACiuUdpzZPVhHqwWQvqQDJPpfWgFfVBbNnoMVmXqNnRrAaQOonQqMLlmUukckJvszobXtTmMjJeEUiAVMmvyziIUoOuzZYPuICrRchTdDtbBTtXxQvHhbBQqGRrgAaqjJlOocfFCCbCcBQnyndDFvVffHNbxHhhHIiTtlLXMmBmqQlnSsIhHitTNBbHhpSsDnDVXIisShovlqQPEeLLdtTOoZoOzDoZzOtMIhHchzZHLlDdCEuoNnOIOozZgWwvVvVWzdDvVRSsVwWwbByrRYVvrRvVfFONnzZEtTEeWXDNndcCuLlrRoOpZzGgbBChHcPTtWzBbwWGFfgZUGguwIDdsSsSFfYLlsSrkKRzZBNUuVsSLGglSsnNvHJHpfFWeEjJxCMhHmMmRruUuonNrRjuUJOUXxSsgGPAaBbCcZzpsQqoOLlWVwWyLlIiYSxXidtrxXRTDKkDdkKdwwWUuOfUXXxxJwWOojYWQqOoOocNVvnwWxpPrRueEwWsPpYySFfwWBblKaAkjJZOVvozDdYaARCccCreEyRrARraepiIPoOjJHhEeRryUuwWeoOfFwkKGGggPpQYGgQgBbLFffhKkHkKeysSYGgQPpnNqXkKqQwWirzZCwacdDeECAOoWcTtgbBGeEIWwWmzmFHhsSFfsSFnNFYCcaAyfpwWdiIDTBbhHWwdDVCiItTcHhMgoOjJtRAnNlLarTGGXHhfgGlLyxESswWFftSQqsQYJjlrRLIiPCjJxiItTuUjxdDXMcCBUurRTtpTXEZzeaAYEeyWDdKMmJiDdIUuEeVvkKHhVvKVvAQKkqfuUxXFRWyYwvUudDVslLvCcZzVvwsSWCOTDdkzYKkyZibKkmZkcCKzOoQqrRvVkQQqpTtFfZzkKNnPpMmPnNTHWErReXxrRwpdToOoOtKpghuUsSHGLlwWPiITiItQxvdvKkVOoxnNXDqmuyYvvjqQvVhHLAXzZfFGgWwpPHhOKkmMKkhpPEeTtXxKngGVvUUEewBJUuZmMzxXHFIifdDFbBsSiIRqqQQruUEDKkdTyoOYSSsSsvVidsSrRcFfCPplLDVvZzNntJAAaajrnNRecCEISeTlLjZzXyYxDnEYUhHNAanuwfBbjJFNZzoEaAhHezgcpMDdmnNPHGguUhwWLlcCcCFCLlddtWvVLLsSlJjGgEXdDoOLwWjBbJjjCcJDdJWWwwlVvkKUurXOGEeIigoNiInVMmvxCcbWwBwnbMmbHhBBMaPpmyYvVrRiOAZzaogVvkKrRGWwZzWqQpPpPweHhybWwKdDkBzZLlNneEMsSxXmYlhTbBtxXDlIiONnozSGgfFPyYcjJCXxpiuKOokUCWvVwcCTtnbBNbHhBPpMMmJJYyTtrXxTtzZRUIibicusSUhIKkaAoOuUjQLyjJYQCsSGXsxXSnuVvUKkyYxXWAEeazZwXxEejLpaAPlMmbBhHAlLPpaSsJMwGrRGggXxpaAPWiPpUDdvVFfubBqNkKlxXeEeEDfFdirRIDdgGeEveEVpPCcrUpPlkKLuNnRjJmIiAnNtTaMDdbBLIiVvOGSsLloOaAQLlIwWyOocCtLzZlvdVmMSAaaDTORxXaRrArxevuzZpvVXcCIeEiVhHCaAcbBjSoKkDdOBbgEFQqfEaJjfYtwQqWTzZzZyFAcCyYjJarRqsSQywWilKkLpJjEQqmMdDaAvaASpEebBhHXPpsSxgqQUuhKkFfvVhHSxXmMYyFEzEenNJjazZAIWwhHSsWwZxkKiIXEyYeEKkGwIiWNncCAlLtyYTalzRrZbBVvcCuxXUHhwEwWeBnNbLlXxDTQqbBtrRgnNGdWwTVvdDlUuHxmkkKyYEYhkKsXxSKCckdzZmMHhHqQVFlLJjXxzZfUXAauURNnrxXxyYoOuMmVvmMHhfSsdeEDNnJrRMsSENnUueEMmNnWWwKNVvngGkkKwwWZzNQqivVQjJqtTYySGZzncpWwPCeuNnUFaAcCfQSsLxXefFELllqmMUujJHhhHQnNlmPwWpMnuUNRrEeVvAaLnNoOhfoOeGdqQVJjpPcbByYDdzrRPpRrHhfKhcCwWHFfQqjJkFPHZzYXZzjJeExpPTIVGgRrHRrOohUutWwTDPplNnLvVZejkvGgVyXxNEenHrRhNZzXxnwWWwYkNFfnmITtinIiNqQaGgQZqGgQRrBvkKvVJKkjVfiIQezZpPQOKkKqQoEAIiDdaGgYyoOeOtQKknhHNhHrjJfFBfFHWwCchCnOGgLljJadMmDtCcDdGgeEnNkCcKdAAmDdiIMZzKkWQquOoSsxXOogGXAaQqxoOhHEenNnqlLcIcCAlLHhtTzBbyYDdZfFtTzGgZYyEeSFPxXpaAgGbdWwSsyYYJpHgjJGhVvHhPEexyYMmkSsnRrLlNxXVVytTeEbBYTtiIAaQLlkKqjJOovveGgEfFIpPizZimFZzJoODDdJjWdGgDgGRnkDdByYbKDfuUQqTJwWFPIIiXWbzZByaAYwWxXUuIiSsUsCAOoWwaWwcSyeEYLlTWwlLreBMKkrRLlakKAfNnoOjJUiIpPCcrRQqvZzgGVwWpRnNRULAmMBzZPpbVWwXwWJRBbrEesSHKkhjJqQKmDrRAagjJkweEWKQuUGvVmAaMHpPhJvVHcCekKdbBKcCyFfAaAaYSftXxDkKrKeEcCWwMmkVsRrStEBmMovVMmdDOzxGMmKpgDdGHcChaAIrRFqbBbBdDsSBbQqDrCcRIIiqQiiIxXdvVvavVmlLMgZzsSEegGGTJjMHRrhmYJjyFftTtsfFSCcsnlLiOoIKkvwjJWgGmMOoRrVVvfYyrQqROoZzFRgGvTgxKBbNnkbxXBBbOoUXxubBTtgnNGXYyJjWSbBiIsWEecPpFZSsSzZWHhSeEJjJCcjsDdzZcnnVAakeERUunNQUuqRrwNSeEHhwWQqQRrEuUfFIdDBkKbNUungGoOwoOWQqlLEQiIqXgtTEApfFMiIjPDdpRrsOfFlLoWwKvVlByYbVmfFMMmZcvbBVIiTtLlLbBCcHXxoOhhpPShyLtTOolYOpOQiIWTtxjJJjTtyFfYAaAEeQqSszIiZaRvVUurvVvMmVYyqMYVtgcCGTvyXwAaqqQQdhdeEDCfFcRIiYoODdAarWwMmhnNjJJjeEpPICciMVcCvmhHGgvEevVDxvGDSkKsnMXDWlLwWbDCcdUPpmFdDfDdMbBRrTKTtUQquCczZlLTtiKkIEeKwobBOWadrRpPlHhJZfRrFzBbAOlqQLKkXxYyLSsMmIzZiDFfpPoOEeBOiIhIiOonDfOoOoqQDdWqQdBJjAaIayCVjJvyYOAalLoyYOnNAaFnNvVflLMowWLlNnKkjJKkKknNCiIekIZzwEeWjJiHRrhIeETiwWIOhHozDdvVvWwXxokxXKOQEbBajyYrqQpXNVvmMnxElgGTtLeGgiIHNncUuvDdbBOVqaTteEAwWUuQqQqGPpOogGgLlhmMhHHqQowfFHhWXxcBbYyfopPEEeuUuuUxNsSAaAzZUufFdDaKWwbBWwkqrTtRIZziiIgGCmMhHpPcKkBJjxEeDhbBHfFdAhHMmalLsoOIiSXyYSvVzoOlcCLUuhHWwCHxXWwhLlVOovWKkwHmdDWKmMIeEANOouDpmMaAIhldDlLLDCcNLvVcCcyhoOHYRrMmCIiGlbBXxtElLIYwWwWuUfzBbZTtgGbMCcqQtPpWgGkKwrWAaxXvVIiDhthlLgaAGHzZnNBbwWTHlCczhHFZzfCczZZMKkmqQXFOofxEeGQqgLdAgdAaCIiXKkcCZzxjYyTtJITiISwWscEeCWaAOotTzRrPpZtTlWwLmMpUuLtTtJjTNkKszZAMmaBtHcOhHLloCglLGcChTlWOohHzZIiITtCxLlPpfFtTKLiEeMmeEUlLdDuuUoTtnNOfFRlSswzZcGgCamMfMmFHhbIiCugGvVtTUuPXxpUBbjJwWzIiEeZdVCuUcvkKGgyXxIiXxhHzZEeeKkcCXEeEXxzXxYyjJseESIlLEeVvIiKohHxXBvVboOGgQHhqgGoNnQqXWbBwCcHHDdhRrYlLyYYgGyIJLAaXxlcCaYyisSDeEaAbBdMmoOvAaJcbBDdMxvVjJHDzZABbJFfIijatTdOcChqQjJPqOoQpOozZVgfzZXeExUAaUyYXbPpBxuBbMmWwGfFDgGZzbFfBuUsXxCDkKdYmMlbBEeLJjbhLlUuJjOoqZdDiItTOaeBbLlEhHJvVjXxKxvVVmUuxXMuUTtpZzPSUuWcCfFFuUfoOgTtGUuJjgGkAajJKTEetsAagKcCbBdOhHoKhtBbwWrRTHIiDddIltfoOFgWwtAAKKIUZkKzHjJduUDGgNnyYjJWwYdDyRrxGgpQgGqPmMVvXrRGgQqhKwnNWRUurbGgBaZQqRpPrzAeEkxXEvnNpcMmnNeECPdDRrlCcxVvXxDPMmvVNLKuUxXFfIiklnofFgGWwOPpdDGgyYpPsSYyuUhHTtFfOiIomOohHcyYCMoOhHmYZzyOgGaAOoYyohHMnNUuSfFsPxXpnmMNnGgLlNMmuwWZzKkZzMVzZqQcCjJRJjrWYHaAhVvywXvVaAMmMmHhUuJIiqQwuUWvJTssSHkvVKCchTUNAaDsDdbBSdnXxRresStNnPLljTtfbsSkKEejJLbBOoLCuUcaARrlDdXGNngxHtThkKhbBjZzugGUUTCsSgGckKHrRHhpbBzZzPeEeNLloOZzneEvrQqWwRyYNDdJjnRrTaAuUOiIocDdjJCNnCSgGDPGgpdkmLUulVngGNMgGrFfjAykKHhlLuUoOAaetTElSxeIHhiWwpPHzZwWjJaAhGzZSWgGIGpPgsSiLlEewcjJCUVvuBbAasSqAaQpPkuUUlLuKkXxEelLiNnMSsCcTtJjDdwWDdmBbIwWiyYDdpGrFYdYyNnDBhHdDOobdPsqQSKsSdDHhkgGZztTiDmMjvVJjJTtdIsSFfphHOowkGSsgOsSbBRxzZNnJjUTtuqQIiwvVBbJjVvfFEeNkKnoTtfFqfjJLlFQOtgGTPTEGgaACcHhDYytTtUuZlLHhpmuUMKkWwTlRrXxMaAmeEQqyYXxLWwEMwWwWmBfFDdbGiITQqriIBbMnbBYJjMmSsbByolLmhHMWwHwWhxKkXKOokkOoXxxXKwIiTyjJUWKkwuBbIxyYuUxytkKBbhHTdaABmLlwWMpPMyYTtmVveEbsMmSFfMMmcCzZjMhHmPpKuzZUVvkSQqsiIXxuUMmKuUSshyYrRHPpXxongGMmilksSKLYyovVOIXiIMmNnVvCGxXgVvrPpRpPcxXYFfyXdpPQqIioHoOqQhgNvVsSnGyYZAabBmPpyYzMmZGgwWMdDoOCnaeEGDfFziaAXYyxICcWMlLcEetTUHhUuSsuuUfiIVAavFNvVnrRLIiliIduUnNLDhEeHzZNnXxdTtMGjJWwgDcyYejJshfFMLlmPpWXxRryYxBrRbUuDRrhHdNUujAaJAdDaCxvVXxSsXsSMENnZOSsqYynvVNBbMyYEeDdhEeHmBbblLVvqeEYyOobBuUCcCcCjJcSjJsuaAUMjJyYRrdPMUumYJjyxMihHIHhkpPPpKeXoOxEIigKkGgGmaAsylwWMmIMmNnCIfFiCJjMcCmDYyUKRrkWwRrsShHIhHFWxNnPpuRrMMmmMUAaumXlLHhKSsvVSskxUkdDKqBbQpSstUuTvVokKOPkKrUuRgGJZvVtTbsbBvVUuHhhHxUWwiIhHFfKkumMjAatMmlLKkiDfdDLkKAaevVZzZzpVvGgboOBPEliInKiISTtXqnNQxBNnVvbWwBboOwERcnNzfFZzhFfHPrUUuuRpDDHhddRrXFNncCoEWwEeRzEeEOoCqQcTjJtUuKkFfeOZzRrIDdGZzkKgUpVvPGgvVHhuotTLlHAahAaOoxXlUuLYyBViouUOIvbPpWuUuROorUiIuLdDQqTtLlVvXxBOocCkjJKzZGgJjbBDdUDPpdBbjJHhUqgMTQqiIvVtRtTrsRrzUuhmMHvVHhglxXQgGjxXMmyLlYlLPpyYJqJfFjhgGsSxXrWwFfyYLlVvqdTtxXeEgGihHfFIIitTjJpPwWBbYySsbBrRDjJgGNnfFvxXgGfwWSsFSsjyYJVaAtxXfeEeEkKaNnUfFuvOosGgSjYsSaAnNBfFvrZPpwWnNTtSsgMmGgqQTtRrMmwPpSsWhRrvViEeIHhHHhqaAQGJjzkKppPBbPZpaAZzMmUuWgsVvmMcCgGLlbbdDBdEeGgDbvVhAaHdDGgBBlWprRPEewEsSYyEepPeEeLSrRuUIWwXxizsQpPkKqfFSeEUdDZlHhLNnzuPWwcCnNmMJjrRkWLqQJjYyixXvVIVvPpOcQqjoOiEeIeEJCoZhyYpgxUVvugjJGXbBkKLqQlETBuUbteUroOVvRBbiIdDZWwznNBGgDdsSbuNvVsSIieKiIGgjJdPoOpWwDyfFpPYIikdDTtpPEaUuAIizZUpPuYsSCcYyIFvNOoutTUfFyYHhWxXqQoyYOwaAvVyYKkngGJjmGHAaiICcSRApPJjeEaSsiIeIiEdDrsQHhWwKkYyuUscCFVvfSAwoOyYWaYyeEUnNCBbsLIilSvZNnzUuVuUpqQZzLlPcCsScpEBbNdDnegBbTtGPyOoYyYPvawDdWtJjlkKQlLKkhHqWwSsLlfjPpKkJFjJRrEeHvFfVEQKkqdDJjXLhHlTtJjxLlWtRrTIlLiZhJjiIHzwxXjJKcMyYmCCcHhksSLlenNWRrwScZzCDdsrqQRgGzPsSpBALlFfacCEzZOomMiIejGgtTJoOlLBbsSbzZpPBZzTtJWwjLSslbBMsqQSsqGgQGgSqlayYeEAQFNnfqhHJjPpAadDvEeVcCbBpPKgGoOkRQqrerREGwWfZzFeEgwmMZpPIiUWwuAazpbBvVqQPtTWStEeTvVaAskXRrxHhKbNnqQBIpPiebIgGasSACciEwOoSsWiIZzeBMcCmpCcPEFEefrRCZzcVqAaQJjvJjKZznaAaANAakoOPpbJjBDUvVudDdYsSyIiBbZWSAaAaHhValLAwWAavLlHxXheEeEVixXjJIEyYxXiIxkKwvVRrOorRWwWQlLqLlUusSJjbBhlLZgGzGgncPpCNjlXKkxLoORrJHnPVEekKvpNWqKvVTJjtEuiIUwWwWCcyYPWwpcCjiITtYcCyECceJjpnNPJBkhHfBbzkKMDHhdmJHhjtTFfyJjHhdUujyYXxJKkQqDMwWuUmmukKUZzjJuUHOoOoDdqQqQaAUuyYrHhRkKbBWwhazloiIhHRrOLPpDNnZZzLzgGZYNnqQylfFaAeEiICczdmoOLeElmMnWLlwvvrRVaAVPsSqQpuUzYyRrZBboOKkgGTNGgntMmssSbtLfmMFciICdDloOTfYyPpSNFfnWwiuUIBwNnAaWkZUuIiQqzKXxvoOtTmMbGgcTYydDRHtThrQqlLwAaalLALAalvVXYNnNPpuUKkNLlpPkKnhtTHtTXxlEnNeAHhaUuVAavLHhBbzTtFfZGBbaAJjvEeVxXZzgKHhkgpPxXPfFeEpGcCCDdDdreJjErRHhxBbzZXxXCaAyYclLDvPpVtTdCkDdJjKmmMlIiLMmMKkYXmMQVvXxUuREeJVvqQLlBVvZzbjOGgSSsoOsGgHheEdDoRtTrXxWoOwOjJoYyMmVvAaFBbfEeTtqKzZkIEepPNuUnEeGbBlLcFrEeREwWqhHQmOoMXxeAQPjJpJjCcoBbYypNYynWwIiaVvAcjJeEApPRrNnICcxXiaANnakKEehHRrgGCPoOsEeStTdfFDpPAaNnNIiTtZznPoOpiISsXxjIMpPmtcCTieIHhSlLgBbGglLLlheEtspPxXSTTrRlUDdupyYYyhHUNnFfpiIxXTMmtfXxxXFPeEmxXVKkfFvCcrWLVvlwRqRrQFfPVvFfFlLZzdDfpHhbBWwyxQqXZmMVgbBGvlUuLxXYyzAabBCkQqZqQiImRnNrRhcCHrMpPQIiHDdhqdqQMmigGrRIiTtlLZwWYyEPpekHOohVvKDdRfSspiIPZLlzFrNnmwWtTMnNrReXxifFIsSwuUWEOYyKrRkfrfcCFtTCcwNSsnWJOojOUXsSxBbjXxjJJuFfYMmCcyGtWwTbBLnNyYlOohHSsLlgSDdoOHhhHsMQqYytTHtTjJOoLlhKkowWUbBTtsWwSuNnLlxXrROcyYZfFzvVCZzHhqHhNeiIckKVvuUuJjiIJjUXxzZCEGHiIyYhgDsSHLlfFUuhrNnRaApAsSaPXxdkdDGgKzZntWwTkKbpvVuUGgPQcCqUHhuWwNnuUEelLYVvhHyBvGgVrRBbreEWwRsiIHYJjAIiatDdaATWwyOohEdmMDYyTtYyDdJKkKGgkXLFflwEeWUuGgfVqQvBbFYyeEtrRCTHhvVTtpPtdMmDiINnrHhQqfFrcCRdQqkCmMtTYyeEcGWwtTlgSrDdVvRsGgaARrOAaTtEjJQHhqeoWffFFuUwRJtTcuUCjruUmCcLjfFVhHXxrsSRnNuUVvaTtBnRrNiIbBbAxBbpPGuUgBbXbByYqQkKqQaDdALlIiprTtROoPwDdGbBpPIigWxCcCtTcImSsYyMJjuJjUmMpPtMmhHpPTGgjJpPaAcHhCiTfFEcCetVwWvKTtkKkiItKkIiyYQqOoTVxXgGJXsSxWwrREeJzZjpRoOrPnNjvoOqQruURNnhHmJjMKkXFOMmofFfNnbBZzJjpwWBbxXPYytTzZuUYEeyraARNnpKVSsvkFfVvAQnNqBbitTfFgmMGIoOPphHaTtTtMidDIWwRDdrRrcCvVqQqyYmeEMWeErRwcCXxBbryYKkRxXRHhmeEMmsSMSsruUFAFfmtTMabIilLFWRrwfkIiYyeEPpKfFGgFffFTtBaAQLEelPyVvYpXlLPpYyxqxXVgGwIiWkLlKfkKzZFZzwWVwWLlvvgUDduaAdWwDWDdwDdDqQdsSlLvVvVqQCcuUkKsSzZGjJoOfMOojJmlLjqQJjJQtTVvapPCcAJXxjXxerRhHESsmxJjnNWhHyYwXOoSsYyxXeSsxXZzZzLlhXxHEGjJgjiIJGhaAsbBvVeuUFzZfZzESgaZzAzzZvVZbAaBGtTHrGgRrRBhHnaANbcsxXSApPonNObmMBanNtTkKYWwaArKuUmMoOyYKkkbBWwfFRrRTtGgTpPeEtsWNNnnRroOjrRJwNcDduUCnORBbrGguUoZzsSsSUNnuAaPpIYysSiNnhHdDFfkKSsQCXxaAEUuecxrHkKMmhHaAhQqtxXaAOHGghZTtzoTFfOoiIcChHnNUmMuYyRJjdMmDPsSXxTtppPqQJjlLZCcHhqQgGzXPTtpMbBmqqQQAuUTdrRDnNxXWwtvVYyiIIigGOoVvYyHhagGZzKkqVvrlLdDROGgovVKpPkBbwWOotTOrRgGWwHhfFKkgTtGWwNnwiUiIuIQDdKkSsSsYQOoVvHhaABbsSDGguUsSCzZcIxXidziYyrrRRIqrRGgQWwMjJmFfZJjnNhqQmiIYyAaMhnNHiInNMyqQkKIGgiYQXxqoOTpPtmyAaYpPqcbBCQqXxpPQExXexXiIHLzZSswWlLlJUugGjoOrRJjqYyrxXRkoObFfDdBWwKkkKnBgGUubsSNqQpkcCKgGPdDgfFGxuUmCcfFmMSBLlDdPRrkzZKIigGfNiInFYyGtTbBgnsSzZhHoONgOoGbBTFfrRzZFfgQqGyYmsSMtnNCwpPWwWrQqRcoOBbhnNHjJpTtTtdbBfFSsDEebfFLlBbmGTtgMHhaAhHS"
;input: db "dabAcCaCBAcCcaDA"
static input:data

%define input_len 50000
;%define input_len 16

section .text

exit:
static exit:function
  mov rax, SYSCALL_EXIT
  mov rdi, 0
  syscall

write_int_to_stdout:
static write_int:function
  push rbp
  mov rbp, rsp

  sub rsp, 32

  %define ARG0 rdi
  %define N rax
  %define BUF rsi
  %define BUF_LEN r10
  %define BUF_END r9

  lea BUF, [rsp+32]
  mov BUF_LEN, 0
  lea BUF_END, [rsp]
  mov N, ARG0

  .loop:
    mov rcx, 10 ; Divisor.
    mov rdx, 0 ; Reset rem.
    div rcx ; rax /= rcx

    add rdx, '0' ; Convert to ascii.

    ; *(end--) = rem
    dec BUF_END
    mov [BUF_END], dl
    
    inc BUF_LEN

    cmp N, 0
    jnz .loop

  mov rax, SYSCALL_WRITE
  mov rdi, 1
  mov rsi, BUF_END
  mov rdx, BUF_LEN
  syscall


  %undef ARG0
  %undef N
  %undef BUF
  %undef BUF_LEN
  %undef BUF_END

  add rsp, 32
  pop rbp
  ret

solve:
static solve:function
  push rbp
  mov rbp, rsp

  %define INPUT_LEN r10
  %define CURRENT r9
  %define NEXT r11
  %define REMAINING_COUNT rax
  %define END r8

  lea CURRENT, [input] 
  lea NEXT, [input + 1] 
  mov INPUT_LEN, input_len
  mov REMAINING_COUNT, INPUT_LEN
  lea END, [input]
  add END, INPUT_LEN
  

.loop:
  movzx dx, BYTE [CURRENT]
  movzx cx, BYTE [NEXT]
  sub dx, cx
  imul dx, dx

  mov rcx, 32*32

  cmp rdx, rcx
  jnz .else
  .then:
    mov BYTE [CURRENT], 0
    mov BYTE [NEXT], 0

    sub REMAINING_COUNT, 2

    .reverse_search:
    dec CURRENT
    mov dl, [CURRENT]
    cmp dl, 0
    jz .reverse_search


    jmp .endif
  .else:
    mov CURRENT, NEXT
  .endif:

  inc NEXT
  cmp NEXT, END
  jl .loop

  %undef INPUT_LEN
  %undef CURRENT
  %undef NEXT
  %undef REMAINING_COUNT
  %undef END


  pop rbp
  ret

global _start
_start:
  call solve

  mov rdi, rax
  call write_int_to_stdout

  call exit
```
