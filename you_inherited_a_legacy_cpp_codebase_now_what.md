# You inherited a legacy C++ codebase, now what?

You minded your own business, and out of nowhere something fell on your lap. Maybe you started a new job, or perhaps changed teams, or someone experienced just left.

And now you are responsible for a C++ codebase. It's big, complex, idiosyncratic; you stare too long at it and it breaks in various interesting ways. In a word, legacy. 

But somehow bugs still need to be fixed, the odd feature to be added. In short, you can't just ignore it or better yet nuke it out of existence. It matters. Well not to you per se, but to someone who's paying your salary. So, it matters to you. 

What do you do now? 

Well, fear not, because I have experience this many times in numerous places (the snarky folks in the back will mutter: what C++ codebase isn't exactly like I described above), and there is a way out, that's not overly painful and will make you able to actually fix the bugs, add features, and, one can dream, even rewrite it some day.

So join me on a recollection of what worked for me and what one should absolutely avoid.

And to be fair to C++, I do not hate it (per se), it just happens to be one of these languages that people abuse and invariably leads to a horryfying mess and poor C++ is just the victim here and the C++ committee will fix it in C++45, worry not, by adding `std::cmake` to the standard library and you'll see how it's absolutely a game changer, and - Ok let's go back to the topic at hand.

So here's an overview of the steps to take:

1. Get it to work locally, by only doing the minimal changes required in the code and build system, ideally none. No big refactorings yet, even if itches really bad!
2. Get out the chainsaw and rip out everything that's not absolutely required to provide the features your company/open source project is advertising and selling
3. Make the project enter the 21st century by adding CI, linters, fuzzing, auto-formatting, etc
4. Finally we get to make small, incremental changes to the code, Rinse and repeat until you're not awaken every night by nightmares of Russian hackers p@wning your application after a few seconds of poking at it
5. If you can, contemplate rewrite some parts in a memory safe language


Ok, let's dive in!

## Yeah, it builds!

You'd be amazed at how many C++ codebase in the wild that are a core part of a successful product earning millions and they basically do not compile. Well, if all the stars are aligned they do. But that's not what I'm talking about. I'm talking about reliably, consistently building on all platforms you support. No fuss, no 'I finally got it building after 3 weeks of hair-pulling' (this brings back some memories). It just works(tm).

A small aparte here. I used to be really into Karate. We are talking about 3, 4 training sessions a week, etc. And I distincly remember one of my teachers telling me (picture a wise Asian sifu - actualy my teacher was a bald white guy kind of looking like Steve Ballmer):

> You do not yet master this move. Sometimes you do and sometimes you don't, so you don't. When eating with a spoon, do you miss your mouth one of five times?

And I carried that with me as a Software Engineer. 'The new feature works' means it works everytime. Not one out of five times. And so the build is the same.


### Write down the platforms you support

This is so important and not many projects do it. Write in the README (you do have a README, right?). It's just a list of `<architecture>-<operating-system>` pair, e.g. `x86_64-linux` or `aarch64-darwin`, that your codebase officially supports. This is crucial for getting the build working on every one of them but also and we'll see later, removing cruft for platforms you do *not* support.

If you want to get fancy, you can even write down which version of the architecture such as ARMV6 vs ARMv7, etc.

That helps answer important questions such as:

- Can we rely on having hardware support for floats, or SIMD, or SHA256?
- Do we even care about supporting 32 bits?
- Are we ever running on a big-endian platform? (The answer is very likely: no, never did, never will - if you do, please email me with the details because that sounds interesting).
- Can a `char` be 7 bits?

And an important point: This list should absolutely include the developers workstations. Which leads me to my next point:

## Get the build working on your machine

Experience has shown me that the best way to produce software in a fast and efficient way is to be able to build on your machine, and ideally even run it on your machine.

Now if your project is humongous that may be a problem, your system might not even have enough RAM to complete the build. A fallback is to rent a big server somewhere and run your builds here. It's not ideal but better than nothing.

Another hurdle is the code requiring some platform specific API, for example `io_uring` on Linux. What can help here is to implement a shim, or build inside a virtual machine on your workstation. Again, not ideal but better than nothing. 

I have done all of the above in the past and that works but building directly on your machine is still the best option.

## Get the tests passing on your machine

First, if there no tests, I am sorry. This is going to be really difficult to do any change at all. So go write some tests before doing any change to the code, make them pass, and come back. The easiest way is to capture inputs and outputs of the program running in the real world and write end-to-end tests based on that, the more varied the better. It will ensure there no regressions when making changes, not that the behavior was correct in the first place, but again, better than nothing.

So, now you have a test suite. If some tests fail, disable them for now. Make them pass, even if the whole test suite takes hours to run. We'll worry about that later.

## Write down in the README how to build and test the application

Ideally it's one command to build and one for testing. At first it's fine if it's more involved, in that case the respective commands can be put in a `build.sh` and `test.sh` that encapsulate the madness.

The goal is to have a non C++ expert be able to build the code and run the tests without having to ask you anything. 


Here some folks would recommend documenting the project layout, the architecture, etc. Since the next step is going to rip out most of it, I'd say don't waste your time now.
