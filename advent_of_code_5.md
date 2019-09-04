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
`csi`:

```sh
$ csi
CHICKEN
(c) 2008-2019, The CHICKEN Team
(c) 2000-2007, Felix L. Winkelmann
Version 5.1.0 (rev 8e62f718)
macosx-unix-clang-x86-64 [ 64bit dload ptables ]

#;1>

```

You can try to type something:

```sh
#;1> (+ 1 2)
3
#;2>

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

So, how do you add 2 numbers? Well, `+` is just a function, so that is simply: 

```scheme
(+ 1 2)
```

Most operations in Scheme are just functions (or things that look like
functions, such as macros, but we won't get into that).

We can compose expressions in a straight-forward manner:

```scheme
(* (+ 1 2) 3)
```

which returns `9`.


Note that using the prefix notation with s-expressions (as we call those groups
of parentheses) removes entirely the need for a table of operator precedence,
which is very nice. We first evaluate the inner-most form: `(+ 1 2)`, which is
`3`, and then we evaluate the outer form: `(* 3 3)`, which is `9`.

One more thing we need to learn, is how to define variables and functions. Note
that Scheme is mostly an immutable functional language, so there is not really a
concept of 'variable', instead we talk about 'bindings', which really are just
aliases.

Here's how to define a binding called `foo` to the value `1`:

```scheme
(define foo 1)
```

We can read this as such: from now on, when I refer to `foo`, what I really mean
is `1`. That's it, it's just an alias.

We can check it worked by printing it with:

```scheme
(display foo)
```

Defining a function is quite the same:

```scheme
(define (compute a b) (+ a b 3))
```

This defines a function called `compute` which adds its 2 arguments to `3` and
returns that.
Written in a way we are used to: `a + b + 3`. It works because `+` is a function
that takes a variable number of arguments and adds them all.

Le'ts call it:

```scheme
(compute 1 2)
```

 returns `6`.

Note that we do not need any `return` keyword like in most languages. Instead,
the last s-expression is the return value of the function.


With this, believe it or not, we have enough to get started, and we will learn while doing.


## But how do I run my code?

Just save it to a file with the `.scm` extension and run it with:

```sh
$ csi -s foo.scm
```



You can put that as a shebang on top of the file:

```scheme
#!/usr/local/bin/csi -s
```



Another way is to compile the code to an executable and run that:

```sh
$ csc foo.scm -o foo && ./foo
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
