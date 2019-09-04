# Getting started with Scheme by solving an Advent of Code challenge 

Chicken Scheme is a wonderful small and performant implementation of Scheme, a
programming language in the family of LISPs. It's very easy to install and
learn. I learn by doing, so let's solve an Advent of Code challenge with a tiny
Scheme program.

Many people have the feeling that LISPs are slow and cryptic with all those
parentheses. I hope to show that it is in fact very approchable, easy to work
with, and even fast to run!

## Installing Chicken Scheme

There should be a package in your favorite package manager. For example on macOS
you would do:

```sh
$ brew install chicken
```

and...that's pretty much it! You should now be able to enter the REPL with
`csi` and type something:

```sh
$ csi
CHICKEN
(c) 2008-2019, The CHICKEN Team
(c) 2000-2007, Felix L. Winkelmann
Version 5.1.0 (rev 8e62f718)
macosx-unix-clang-x86-64 [ 64bit dload ptables ]

#;1> (+ 1 2)
3
```

If you want to more complete setup, you can also
install the documentation with the chicken package manager:

```sh
$ chicken-install -s apropos chicken-doc
```


I definitely recommend setting up your favorite editor to work with Scheme.

## Scheme in 30 seconds

All you need to remember is this syntax:

```scheme
(foo arg1 arg2 arg3)
```

This calls `foo` with `arg1`, `arg2`, and `arg3` as arguments. In a
C-like language you would use: `foo(arg1, arg2, arg3)`.

Most operations in Scheme are just functions (or things that look like
functions, such as macros, but we won't get into that).

Let's do a quick tour Scheme:

```scheme
;; Numbers:
1 ;; => 1

;; Strings:
"hello, world" ;; => "hello, world" 

;; Characters:
;; The character `a`
#\a ;; => #\a

;; Expressions:
;; + is just a function like any other
(+ 1 2) ;; => 3 

;; We can compose expressions in a straight-forward manner:
(* (+ 1 2) 3) ;; => 9
;; Note that using the prefix notation with s-expressions (as we call those groups
;; of parentheses) removes entirely the need for a table of operator precedence,
;; which is very nice. We first evaluate the inner-most form: `(+ 1 2)`, which is
;; `3`, and then we evaluate the outer form: `(* 3 3)`, which is `9`.

;; Variables:
;; Scheme is mostly an immutable functional language, so there isn't really a
;; concept of 'variable', instead we talk about 'bindings', which really are just
;; aliases.
;; Let's define a binding called `foo` to the value `1`:
(define foo 1)
;; We can read this as such: from now on, when I refer to `foo`, what I really mean
;; is `1`. It's just an alias.
;; Let's print it:
(display foo)

;; Functions:
;; Defining a function is quite the same:
(define (compute a b) (+ a b 3))
;; This defines a function called `compute` which adds its 2 arguments to `3` and
;; returns that.
;; It works because `+` is a function that takes a variable number of arguments and adds them all.
;; Let's call it:
(compute 1 2) ;; => 6
;; Note that we do not need any `return` keyword like in most languages. Instead,
;; the last s-expression is the return value of the function.
```

With this, believe it or not, we have enough to get started, and we will learn while doing.


## But how do I run my code?

Just save it to a file with the `.scm` extension and run it with:

```sh
$ csi -s foo.scm # run it 

$ csc foo.scm -o foo && ./foo # Alternatively, compile it to an executable, and run it
```

## The problem

It boils down to: we have a string looking like this: `AabcdZz`, and we
want to remove neighbouring letters which are the same letter and have opposite casing, e.g
`Aa` disappears while `bc` remains. Once we are finished processing our example,
we have: `bcd`.

The final ouput is the number of characters in the final string, i.e, `3`.

## Working with the REPL to iteratively close in on a solution

First, let's define our input, which is a string: 

```scheme
(define input "aAbxXBctTCz")
```

`define` defines a name for a value, within a module (we do not care about
modules in our case since we will only have one file). It looks like a function,
and that's enough to know for us.

Strings are written like you would expect, no surprises here.

You can then use it like this:

```scheme
(display input)
```

which will print `input`.  Of course, if you are working in the REPL, and you should be (or in your editor,
sending each form to the integrated REPL), you can just write `input` and it
will evaluate its value, in this case the string `"aAbxXBctTCz"`.

Later, we will read our input string from a file, but for now it is simpler to
just hard-code it.

Most operations on strings in Scheme are in an immutable fashion, meaning they doe not
modify the string, they instead return a new string which is slightly different. 
Since the input string is quite big (around 50 000 characters), it might not be
very efficient. Also, we do not really want to keep track of indices, this is a
good way to do off-by-one mistakes. 

Instead, since LISPs are good at handling lists (LISP stands for List Processor), let's use a list of characters instead:

```scheme
(string->list input)
```


Note that Scheme allows a wide range of characters in identifier names,
including `-` and `>`, so we can be very expressive in our naming. Here, the
`string->list` function just returns a list of characters for a string.


Now, we need to detect if two characters are the same latter, with opposite casing.
Let's write a `char-opposite-casing?` function to do just that. It will take 2
arguments, the letters we are inspecting, and will return a boolean. 
For now, let's just make it always return true:

```scheme
(define (char-opposite-casing? a b) #\t)
```

True is written `#t` and false `#\f`.

We only deal with ascii, so it is safe to compare ascii codes. 

What is the ascii code of`A`? Let's try it:

```scheme
(char->integer #\A)
```

`char->integer` is just another function that gives the ascii code of a
character. A character is written with the prefix `#\`, so the character `A` is `#\A`.

We see it returns `65`. What about `a`?


```scheme
(char->integer #\a)
```

returns `97`

So there is a difference of `32` between the same ascii letter in lowercase and
uppercase. Peeking at `man ascii` in the terminal confirms this hunch for all
letters of the alphabet.

So, time to implement `char-opposite-casing?`! 

```scheme
(define (char-case-opposite-casing? a b)
  (let* ((a-code (char->integer a))
         (b-code (char->integer b))
         (diff (- a-code b-code)))
    (= (* 32 32) (* diff diff))))
```


`let*` is used to define local bindings which are only visible in this function.
We could have done without it but it makes the function more readable.

The only hurdle is not caring
about the sign of the difference: if the difference is `32` or `-32`, it is the
same. We could use `abs` but I (arbitrarily) chose to implement it without
branches, by comparing the squared values (which swallows the signs).

Let's test our function:

```scheme
(char-case-opposite-casing? #\a #\A) ;; => #t

(char-case-opposite-casing? #\A #\a) ;; => #t

(char-case-opposite-casing? #\A #\b) ;; => #f
```

It works as intended. Now let's work on the central problem: how to remove
characters in a list, in a functional, immutable way?

The idea is to write a recursive function taking two arguments: an accumulator,
which will be eventually the end result, and the input list, from which we
gradually remove items until it is empty. We can view the first list as the work
we have done, and the second list as the work to do.


Let's first define the function. For now, it just returns the empty list, noted
as `'()`:

```scheme
(define (chem-react acc input)
  '()
```


At first, the accumulator is the empty list, so we will always call our function like
this:

```scheme
(chem-react '() (list->chars input))
```


It is import to know that most list functions do not work on the empty list. For
example, to get the first element of a list, we use the `car` function:


```scheme
(define my-list (list 1 2 3))

(car my-list) ;; => 1
```

But it won't work on the empty list:

```scheme
(define my-list '())

(car my-list) ;; => Error: (car) bad argument type: ()
```

So we need to treat the case of the empty list (both for the first and the
second argument) explicitely. We could do that by using lots of `if`, but it is
more readable to use pattern matching.

Now, Scheme has a minimalistic core, so we do not get pattern matching out of
the box, but we can easily add it with the package `matchable`. Let's install
it:

```sh
$ chicken-install matchable
```


Now we can import it at the top of our code:

```scheme
(import matchable)
```



Let's try to match the empty list in our function, and return (as an example) a
number, e.g `42`. We also want to match the case of both lists containing one
element, and returning the sum of those 2 elements:

```scheme
(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) 42]
    [((a) (b)) (+ a b)]))

(chem-react '() '()) ;; => 42

(chem-react (list 2) (list 3)) ;; => 5
```

A few interesting things here: `_` allows us to match anything, so the first
case could just be replaced by a simple check to see if the second list is
empty. Additionally, we can bind variables to our patterns: we do that in the
second case, binding the first element of the first list to `a`, and the fist
element of the second list to `b`, and summing the two.


Note that not all possible cases are covered here, and we will get a (runtime)
error if we trigger one of them:

```scheme
(chem-react (list 1 2) (list 3)) ;; => Error: (match) "no matching pattern": ()
```

Let's go ahead and match the case of a list of one or more elements to avoid that:

```scheme
(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) 42]
    [((a) (b)) (+ a b)]
    [((a . arest) (b . brest)) (* a b)]))

(chem-react (list 2 3) (list 4)) ;; => 8
```

Here we choose to (arbitrarily) return the product of the first elements of both list.


<hr>

Back to our problem: if the second list (the input) is empty, it means we are
finished, so we return the first list (`acc`):


```scheme
(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) acc]))

```

Our recursion will work as follows: we look at the first element of the second
list (`input`, which is the work to do), let's call it `b`, and the first element of the first
list (`acc`, the work done), let's call it `a`.



If `a` and `b` are the same letter of opposite casing, we 'drop' the two. Otherwise, we
add `b` to the first list, and 'continue'. 'drop' and 'continue' are put in
quotes because that is vocabulary from imperative languages such as C; we'll see
in a minute how we implement it.


If the first list is empty, this is our starting case: the only thing we can do
is mark `b` as 'processed', i.e add it to the first list, and call ourselves
with the remainder of `input`:


```scheme
(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) acc]
    [(() (b . brest)) (chem-react (cons b acc) brest)]))

```

Here we see a new function, `cons`. `cons` just adds an item to a list, and
returns the new list.

Let's try it:

```scheme

(define my-list (list 2 3))

(cons 1 my-list) ;; => (1 2 3)

```


Let's try our function on a trivial case to trigger the new pattern:

```scheme
(chem-react '() '(#\A)) ;; => (#\A)
```

This makes sense: if we only have one character, there is not much we can do
with it. Note that the input list is **not** modified:

```scheme

(define my-list (list #\A))

(chem-react '() my-list)

(display my-list) => ;; displays: (A)

```

Now, let's treat the main case: we have at least an element `a` in `acc` and at
least an element `b` in `input`. If they are the same letters of opposite casing, we
call ourselves with the remainder of `acc` and the remainder of `input`, which
is equivalent to 'drop' `a` and `b`. Otherwise, we add `b` to `acc`, and we call
ourselves with the remainder of `input`, which is the equivalent of 'continuing':

```scheme
(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) acc]
    [(() (b . brest)) (chem-react (cons b acc) brest)]
    [((a . arest) (b . brest)) (if (char-case-opposite-casing? a b)
                                   (chem-react arest brest)
                                   (chem-react (cons b acc) brest))]))


(chem-react '() (list #\A #\a #\b)) ;; => (#\b)
(chem-react '() (string->list "aAbxXBctTCz")) ;; => (#\z)
```


It works!

> How do I read the input from a file?

It's quite simple: we use the modules `chicken.file.posix` and `chicken.io`:

```scheme
(import chicken.file.posix
        chicken.io)

(read-line (open-input-file "/Users/pgaultier/Downloads/aoc5.txt")) ;; => a big string
```



Everything put together:

```scheme
(import matchable
        clojurian.syntax
        chicken.file.posix
        chicken.io)

(define (char-case-opposite-casing? a b)
  (let* ((a-code (char->integer a))
         (b-code (char->integer b))
         (diff (- a-code b-code)))
    (= (* 32 32) (* diff diff))))

(define (chem-react acc input)
  (match (list acc input)
    [(_ ()) acc]
    [(() (b . brest)) (chem-react (cons b acc) brest)]
    [((a . arest) (b . brest)) (if (char-case-opposite-casing? a b)
                                   (chem-react arest brest)
                                   (chem-react (cons b acc) brest))]))

(->> (open-input-file "/Users/pgaultier/Downloads/aoc5.txt")
     (read-line)
     (string->list)
     (chem-react '())
     (length)
     (print))
```

Here I use the package `clojurian` (`chicken-install clojurian`) to have access
to the `->>` macro which makes code more readable. It works like the pipe in the
shell: instead of writing:


```scheme
(foo (bar "foo" (baz 1 2)))
```

We write:


```scheme
(->> (baz 1 2)
     (bar "foo")
     (foo))
```

It is not strictly required, but I like the fact that my code looks like a
pipeline of transformations.


> But we will get a stack overflow on a big input!

Scheme has a nice requirement for all implementations: they must implement tail
recursion, which is to say that the compiler can transform our function into an
equivalent for-loop. So we won't get a stack overflow, and it will be quite
efficient in terms of memory and time!


> But we are making thousands of copies, it will be slow as hell!

Let's benchmark it on the real input (50 000 characters), with `-O3` to enable optimizations!

*Note: The real output of the program is not shown to avoid spoiling the final result*

```sh
$ csc aoc5.scm -o aoc5 -O3 && time ./aoc5
./aoc5  0.01s user 0.00s system 82% cpu 0.021 total
```



It takes 21 miliseconds. Not too bad!

Here is a hand-written C version which only does one allocation and uses mutations, for the input
string:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE* const f = fopen("/Users/pgaultier/Downloads/aoc5.txt", "r");
    fseek(f, 0, SEEK_END);
    size_t string_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    char* const string = calloc(string_size + 1, 1);

    fread(string, 1, string_size, f);
    fclose(f);

    size_t i = 0;
    while (i < string_size) {
        if (abs(string[i] - string[i + 1]) == 32) {
            memmove(string + i, string + i + 2, string_size - i - 2);
            string_size -= 2;
            i = i > 0 ? i - 1 : 0;
        } else
            i++;
    }

    printf("`%zu`\n", string_size - 1);
}
```

Let's benchmark it on the same input:


```sh
$ cc -std=c99 -O3 -Weverything aoc5.c -march=native && time ./a.out
./a.out  0.01s user 0.00s system 86% cpu 0.012 total
```


It took 12 miliseconds. So the scheme version is very close!

*Note: I know that those are not good benchmarks. To do it correctly, you would
need to warm up the file cache, making many runs and averaging the results, etc. 
I did exactly that and it did not change the results.*


I like C, but I personally find the Scheme version much more readable.


## Conclusion

That's it, we solved the fifth Advent of Code challenge in Scheme! The solution
is under 30 lines of code, and is (hopefully) simple and readable.

I hope it gave you a glance at what LISPs can do, and stay tuned for more blog
posts about programming. I intend to post more solutions to other coding
challenges, solved with different programming languages.


