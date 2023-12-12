<link rel="stylesheet" type="text/css" href="main.css">
<link rel="stylesheet" href="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/styles/default.min.css">
<script src="https://unpkg.com/@highlightjs/cdn-assets@11.8.0/highlight.min.js"></script>
<script>
window.addEventListener("load", (event) => {
  hljs.highlightAll();
});
</script>

<div id="banner">
    <a id="name" href="/blog"><img id="me" src="me.jpeg"></img> Philippe Gaultier</a>

- [Linkedin](https://www.linkedin.com/in/philippegaultier/)
- [Github](https://github.com/gaultier)
</div>
<div class="body">

# Getting started with Scheme by solving an Advent of Code 2018 challenge 

I started learning [Scheme](https://en.wikipedia.org/wiki/Scheme_(programming_language)) very recently. [Chicken Scheme](http://wiki.call-cc.org/) is a wonderful small and
performant implementation of Scheme, a programming language in the family of
LISPs.
Since I learn by doing, let's solve the [Advent of Code 2018 day 5 challenge](https://adventofcode.com/2018/day/5) with a tiny Scheme program.
I encourage you to check out [Advent of
Code](https://adventofcode.com/2018/about) and try to solve the challenges yourself.

Many people have the feeling that LISPs are slow and cryptic with all those
parentheses. I hope to show that it is in fact very approachable, easy to work
with, and even fast to run!

I will not go through installing Chicken Scheme and learning the basics, because
it was [already done better than I can](http://blog.klipse.tech/scheme/2016/09/11/scheme-tutorial-1.html).


**Table of Contents**

- [Getting started with Scheme by solving an Advent of Code 2018 challenge](#getting-started-with-scheme-by-solving-an-advent-of-code-2018-challenge)
    - [The problem](#the-problem)
    - [Working with the REPL to iteratively close in on a solution](#working-with-the-repl-to-iteratively-close-in-on-a-solution)
        - [A small detour: pattern matching](#a-small-detour-pattern-matching)
        - [Using pattern matching to solve our problem](#using-pattern-matching-to-solve-our-problem)
    - [The final solution](#the-final-solution)
    - [Conclusion](#conclusion)

 
## The problem

We have a string looking like this: `AabcdZZqQ` which represents a chain of
chemical units. Adjacent units of the same type (i.e letter) and opposite
polarity (i.e casing) react together and disappear.
It means we want to remove adjacent characters which are the same letter and have opposite casing, e.g
`Aa` and `qQ` disappear while `bc` and `ZZ` remain. Once we are finished, we have: `bcdZZ`.

The final output is the number of characters in the final string, i.e, `5`.

## Working with the REPL to iteratively close in on a solution

First, let's define our input, which is a string: 

    (define input "aAbxXBctTCz")

    input

Later, we will read our input string from a file, but for now it is simpler to
just hard-code it.

Most functions in Scheme are immutable, meaning they do not
modify their arguments, they instead return a new item which is slightly different. 

We could work with strings, but it turns out it is simpler to work with lists
instead in our case. We do not want to keep track of indices, risking doing off-by-one mistakes.
Also, LISPs are good at handling lists (LISP stands for LISt Processor), and
we'll that we can use pattern matching to make the code very concise. I am not
aware of pattern matching capabilities on string, so let's use lists:


    (string->list input)

Here, the
`string->list` function just returns a list of characters for a string (in other
languages it is usually named `split`).


Now, we need to detect if two characters are the same latter, with opposite casing.
Let's write a `char-opposite-casing?` function to do just that. It will take 2
arguments, the letters we are inspecting, and will return a boolean. 
For now, let's just make it always return true:

    (define (char-opposite-casing? a b) #\t)

We only deal with ASCII, so it is safe to compare ASCII codes to detect casing. 

What is the ASCII code of `A`? Let's try it by using the function `char->integer`:

    (char->integer #\A) 


What about `a`?

    (char->integer #\a)

So there is a difference of `32` between the same ASCII letter in lowercase and
uppercase. Peeking at `man ascii` in the terminal confirms this hunch for all
letters of the alphabet.

So, time to implement `char-opposite-casing?`: 

    (define (char-case-opposite-casing? a b)
      (let* ((a-code (char->integer a))
             (b-code (char->integer b))
             (diff (- a-code b-code)))
        (= (* 32 32) (* diff diff))))

Let's try it with `a` and `A`:

    (char-case-opposite-casing? #\a #\A) 


And flipped:

    (char-case-opposite-casing? #\A #\a)

And `A` and `b`:

    (char-case-opposite-casing? #\A #\b)


`let*` is used to define local bindings which are only visible in this function.
It evaluates each binding in order which means we can define `diff` in terms of
`a` and `b` (contrary to `let`).

We could have done without it but it makes the function more readable.

The only hurdle is not caring
about the sign of the difference: if the difference is `32` or `-32`, it is the
same. We could compare the absolute value, but I (arbitrarily) chose to implement it without
branches, by comparing the squared values (which swallows the signs).

---

Now let's work on the central problem: how to remove
characters in a list, in a functional, immutable way?

The idea is to write a recursive function taking two arguments: an accumulator
(let's call it `acc` from now on),
which will be eventually the end result, and the input list (`input`), from which we
gradually remove items until it is empty. We can view the first list as the work
we have done, and the second list as the work to do.


Let's first define the function. For now, it just returns the empty list:

    (define (chem-react acc input)
      '())


At first, the accumulator is the empty list, so we will always call our function like
this:

    (chem-react '() (string->list input))


It is import to know that most list functions do not work on the empty list in
Chicken Scheme. For example, to get the first element of a list, we use the `car` function:


    (define my-list (list 1 2 3))

    ;; Note that this doest **not** mutate `my-list`
    (car my-list)

But it won't work on the empty list:

    (define my-list '())

    (car my-list)

So we need to treat the case of the empty list (both for the first and the
second argument) explicitly. We could do that by using lots of `if`, but it is
more readable and concise to use pattern matching.

### A small detour: pattern matching

Scheme has a minimalist core, so we do not get pattern matching out of
the box, but we can easily add it with the package `matchable`. Let's install
it in the terminal:

    $ chicken-install matchable


Now we can import it at the top of our code:

    (import matchable)

    ;; At this point we can refer to any function in this module `matchable`.
    ;; No need to prefix them either with `matchable`.


Let's try to match the empty list in our function, and return (as an example) a
number, e.g `42`. We also want to match the case of both lists containing one
element, and returning the sum of those 2 elements:

    (define (chem-react acc input)
      (match (list acc input)
        [(_ ()) 42]
        [((a) (b)) (+ a b)]))

    (chem-react '() '()) ;; => 42

    (chem-react (list 2) (list 3)) ;; => 5

A few interesting things here: `_` allows us to match anything, so the first
case is equivalent to checking if the second list is
empty. Additionally, we can bind variables to our patterns: we do that in the
second case, binding the first element of the first list to `a`, and the fist
element of the second list to `b`, and summing the two.


Note that not all possible cases are covered here, and we will get a (runtime)
error if we trigger one of them, for example with a list containing several numbers:

    (chem-react (list 1 2) (list 3)) ;; => Error: (match) "no matching pattern": ()

Let's go ahead and match the case of a list of one or more elements (`(a . arest)`) to avoid that:

    (define (chem-react acc input)
      (match (list acc input)
        [(_ ()) 42]
        [((a) (b)) (+ a b)]
        [((a . arest) (b . brest)) (* a b)]))

    (chem-react (list 2 3) (list 4)) ;; => 8

Here we choose to (arbitrarily) return the product of the first elements of both
list, to show that pattern matching is also a way to do destructuring.

### Using pattern matching to solve our problem

If the second list (the input) is empty, it means we are
finished, so we return the first list (`acc`):


    (define (chem-react acc input)
      (match (list acc input)
        [(_ ()) acc]))


Our recursion will work as follows: we look at the first element of the second
list (`input`, which is the work to do), let's call it `b`, and the first element of the first
list (`acc`, the work done), let's call it `a`.


If `a` and `b` are the same letter of opposite casing, we 'drop' the two. Otherwise, we
add `b` to the first list, and 'continue'. 'drop' and 'continue' are put in
quotes because that is vocabulary from imperative languages such as C; we'll see
in a minute how we implement it in a functional way.


If the first list is empty, this is our starting case: the only thing we can do
is mark `b` as 'processed', i.e add it to the first list, and call ourselves
with the remainder of `input`. Indeed, we can only work with two characters, so
if we only have one, we cannot do much.

It's time to learn about a new function: `cons`. `cons` just adds an item to a list, and
returns the new list with the added item:



    (define my-list (list 2 3))

    ;; Note: `my-list` is **not** modified
    (cons 1 my-list) 



We can now use `cons` to implement the new case:

    (define (chem-react acc input)
      (match (list acc input)
        [(_ ()) acc]
        [(() (b . brest)) (chem-react (cons b acc) brest)]))


    (chem-react '() '(#\A)) ;; => (#\A)


This new pattern is required for the recursion to
work, but it also covers the trivial case of an input string of only one character.


Now, let's treat the main case: we have at least an element `a` in `acc` and at
least an element `b` in `input`. If they are the same letters of opposite casing, we
call ourselves with the remainder of `acc` and the remainder of `input`, which
is equivalent to 'drop' `a` and `b`. Otherwise, we add `b` to `acc`, and we call
ourselves with the remainder of `input`, which is the equivalent of 'continuing':

    (define (chem-react acc input)
      (match (list acc input)
        [(_ ()) acc]
        [(() (b . brest)) (chem-react (cons b acc) brest)]
        [((a . arest) (b . brest)) (if (char-case-opposite-casing? a b)
                                       (chem-react arest brest)
                                       (chem-react (cons b acc) brest))]))


    (chem-react '() (list #\A #\a #\b)) ;; => (#\b)
    (chem-react '() (string->list "aAbxXBctTCz")) ;; => (#\z)


But wait a minute...Doesn't it look familiar? Yes, what we are doing here is a
fold (sometimes called reduce)!

Let's replace our custom recursion by `fold`. `chem-react` becomes the reduction
function. It becomes simpler because `fold` will not call it on the empty list,
so we only need to patter match `acc` (which is the empty list at the beginning): 


    (define (chem-react acc x)
      (match acc
        [() (cons x acc)]
        [(a . arest) (if (char-case-opposite-casing? a x)
                         arest
                         (cons x acc))]))


    (foldl chem-react '() input) ;; => (#\z)

My experience writing code in a LISP is that I usually find a solution that is
relatively big, and I start replacing parts of it with standard functions such
as `fold` and it ends up very small.


> How do I read the input from a file?

It's quite simple: we use the modules `chicken.file.posix` and `chicken.io`:

    (import chicken.file.posix
            chicken.io)

    (read-line (open-input-file "/Users/pgaultier/Downloads/aoc5.txt")) ;; => "a big string..."


## The final solution

Here I use the package `clojurian` (`chicken-install clojurian`) to have access
to the `->>` macro which makes code more readable. It works like the pipe in the
shell. Instead of writing:


    (foo (bar "foo" (baz 1 2)))

We write:


    (->> (baz 1 2)
         (bar "foo")
         (foo))

The macro reorders the functions calls to make it flat and avoid nesting.
It is not strictly required, but I like that my code looks like a
pipeline of data transformations.


The final code:

    (import matchable
            clojurian.syntax
            chicken.file.posix
            chicken.io)

    (define (char-case-opposite-casing? a b)
      (let* ((a-code (char->integer a))
             (b-code (char->integer b))
             (diff (- a-code b-code)))
        (= (* 32 32) (* diff diff))))

    (define (chem-react acc x)
      (match acc
        [() (cons x acc)]
        [(a . arest) (if (char-case-opposite-casing? a x)
                         arest
                         (cons x acc))]))

    (->> (open-input-file "/Users/pgaultier/Downloads/aoc5.txt")
         (read-line)
         (string->list)
         (foldl chem-react '())
         (length)
         (print))



> But we will get a stack overflow on a big input!

Scheme has a nice requirement for all implementations: they must implement
tail-call optimization, which is to say that the compiler can transform our function into an
equivalent for-loop. So we won't get a stack overflow, and it will be quite
efficient in terms of memory and time.


> But we are making thousands of copies, it will be slow as hell!

Let's benchmark it on the real input (50 000 characters), with `-O3` to enable optimizations:

*Note 1: The real output of the program is not shown to avoid spoiling the final result*


*Note 2: This is a simplistic way to do benchmarking. A more correct way would
be: warming up the file cache, making many runs, averaging the results, etc. 
I did exactly that and it did not change the results in a significant manner.*


    $ csc aoc5.scm -o aoc5 -O3 && time ./aoc5
    ./aoc5  0.01s user 0.00s system 82% cpu 0.021 total


It takes 21 milliseconds. Not too bad for a garbage collected, functional,
immutable program.

Here is a hand-written C version which only does one allocation and removes
letters in place:


```c
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  int fd = open("/home/pg/Downloads/aoc2020_5.txt", O_RDONLY);
  if (fd == -1)
    return errno;

  struct stat st = {0};
  if (stat("/home/pg/Downloads/aoc2020_5.txt", &st) == -1)
    return errno;

  int64_t input_len = st.st_size;
  char *const input = calloc(input_len, 1);

  if (read(fd, input, input_len) != input_len)
    return errno;

  while (input[input_len - 1] == '\n' || input[input_len - 1] == ' ')
    input_len--;

  int64_t i = 0;
  while (i < input_len) {
    if (abs(input[i] - input[i + 1]) == 32) {
      memmove(input + i, input + i + 2, input_len - i - 2);
      input_len -= 2;
      i = i > 0 ? i - 1 : 0;
    } else
      i++;
  }

  printf("`%zu`\n", input_len);
}
```

Let's benchmark it on the same input:


    $ cc -std=c99 -O3 -Weverything aoc5.c -march=native && time ./a.out
    ./a.out  0.01s user 0.00s system 86% cpu 0.012 total

It took 12 milliseconds. So the scheme version is very close, and takes an
acceptable amount of time.

> Can't we use strings and not lists?

Yes, of course. However we need to be careful about how strings are implemented
and what we we do with those. Most runtimes (e.g the JVM) use immutable strings,
meaning we could end up allocating thousands of big strings, and being quite slow.

## Conclusion

That's it, we solved the fifth Advent of Code challenge in Scheme. The solution
is under 30 lines of code, is (hopefully) simple and readable, and has a
performance close to C, while having memory safety (I had several segfaults
while doing the C version).

But more than that, I think the real value in LISPs is
interactive programming, instead of the classical write-compile-execute-repeat,
which is much more time consuming. It is really important to get feedback as
early as possible, and LISPs give us that.

I hope it gave you a glance at what Scheme can do, and stay tuned for more blog
posts about programming. I intend to post more solutions to other coding
challenges, solved with a variety of programming languages.


<div class="body">
