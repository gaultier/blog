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
C-like language you would use: `foo(arg1, arg2, arg3)`

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

, which returns `9`.


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

, which will print `input`.  Of
course, if you are working in the REPL, and you should be (or in your editor,
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

We only deal with ascii, so it is safe to compare ascii codes (type `man ascii`
in your terminal to see these). 

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

