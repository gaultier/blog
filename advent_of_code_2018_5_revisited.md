<link rel="stylesheet" type="text/css" href="main.css">
<a href="/blog">All articles</a>

# Optimizing a past solution for Advent of Code 2018 challenge  in assembly

A few days ago I was tweaking the appearance of this blog and I stumbled upon my [first article](/blog/advent_of_code_2018_5) which is about solving a simple problem from Advent of Code. I'll let you read the first paragraph to get to know the problem and come back here.

Immediately, I thought I could do better: 
- In the Lisp solution, there are lots of allocations and the code is not straightforward.
- In the C solution, there is no allocation apart from the input but we do a lot of unnecessary work.


This coincided with me listening to an interview from the VLC developers saying there wrote hundred of thousand of lines of (multi platform!) Assembly code by hand in their new AV1 decoder. I thought that was intriguing, who still writes assembly by hand in 2023? Well these guys are no idiots so I should try it as well.


I came up with a new algorithm, which on paper does less work. It's one linear pass on the input.

Since the result we care about is the number of remaining characters, we simply maintain the count as we sift through the input.

We maintain two pointers, `current` and `next`, which we compare to decide whether we should merge the characters they point to. 'Merging' means setting the two characters to `0` (it's basically a tombstone) and decrementing the count.

`next` is always incremented by one in each loop iteration, that's the easy one.

`current` is always pointing to a character before `current`, but not always directly because they may be tombstones, i.e. zeroes, in-between.


In pseudo-code:

```
remaining_count = len(input).
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

The 'hard' case is merging: we set the two tombstones, decrement the count, and now we are in a pickle: `current` needs to go backwards, but we do not know to where. There might be a number of zeroes preceding the character `current` points to. So we have to do a backwards search for the first non zero character.
We could memoize this location, thus having 3 pointers: `current`, `next` and `previous`. I have not tested this, it might be even faster.

Astute readers might have noticed a potential issue with the backwards search: We may underflow the `input` and go out of bounds! To avoid that, we could clamp `current`, but the branch misprediction is costly (an earlier implementation of mine did this), and we can simplify the code as well as improve the performance by simply prefixing the `input` with a non-zero value that has no chance of being merged with the rest oof the input, say, `1`. 

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

which outputs nothing for now, of course.



