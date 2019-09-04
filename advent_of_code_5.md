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

## Scheme in 10 seconds

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

With this, believe it or not, we have enough to get started, and we will learn while doing.

## The problem

It boils down to: we have a long string looking like this: `AabcdZz`, and we
want to merge, or remove, neighbouring letters which have opposite casing, e.g
`Aa` would disappear while `bc` remains. Once we are finished with our example,
it is: `bcd`.

The final ouput is the number of results in the final string, so in this case, `3`.

## Working with the REPL to iteratively close in on a solution

First, let's define our input, which is a string: 

```scheme
(define input "aAbxXBctTCz")
```

`define` defines a name for a value, within a module (we do not care about
modules in our case since we will only have one file). It looks like a function,
and that's enough to know for us.

Strings are written like you would expect, no surprises here.

You can then use it like this: `(display input)`, which will print `input`.  Of
course, if you are working in the REPL, and you should be (or in your editor,
sending each form to the integrated REPL), you can just write `input` and it
will evaluate its value, in this case the string `"aAbxXBctTCz"`.


