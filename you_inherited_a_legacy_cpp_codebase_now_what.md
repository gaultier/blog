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


The overarching goal is exerting the least amount of effort to get the project in an acceptable state in terms of security, developer experience, correctness, and performance. It's crucial to always keep that in mind. It's not about 'clean code', using the new hotness language features, etc.

Ok, let's dive in!

## Get buy-in

You thought I was going to compare the different sanitizers, compile flags, or build systems? No sir, before we do any work, we talk to people. Crazy, right?

Software engineering needs to be a sustainable practice, not something you burn out of after a few months or years. We cannot do this after hours, on a death march, or even, alone! We need to convince people to support this effort, have them understand what we are doing, and why. And that encompasses everyone: your boss, your coworkers, even non-technical folks. 

All of this only means: explain in layman terms the problem with a few simple facts, the proposed solution, and a timebox. Simple right? For example (to quote South Park: *All characters and events in this show—even those based on real people—are entirely fictional*):
- Hey boss,, the last hire took 3 weeks to get the code building on his machine and make his first contribution. Wouldn't it be nice if, with minimal effort, we could make that a few minutes?
- Hey boss, I put quickly together a simple fuzzing setup ('fuzzing is just inputting random data in the app like a monkey and see what happens'), and it manages to crashe the app 253 times within a few seconds. I wonder what would happen if people try to do that in production with our app?
- Hey boss, the last few urgent bug fixes took several people and 2 weeks to be deployed in production because the app can only be built by this one build server with this ancient operating system that is has not been supported for 8 years (FreeBSD 9, for the curious) and it kept failing. Oh by the way whenever this server dies we have no way to deploy anymore, like at all. Wouldn't it be nice to be able to build our app on any cheap cloud instance?
- Hey boss, we had a cryptic bug in production affecting users, it tooks weeks to figure out and fix, and it turns out if was due to undefined behavior ('a problem in the code that's very hard to notice'), and when I run this industry standard linter ('a program that finds issues in the code'), it detects the issue instantly. We should run that tool every time we make a change!
- Hey boss, the yearly audit is coming up and the last one took 7 months to pass because the auditor was not happy with what they saw, I have ideas to make that smoother.


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


Here some folks would recommend documenting the project layout, the architecture, etc. Since the next step is going to rip out most of it, I'd say don't waste your time now, do that at the end.


## Find low hanging fruits to speed up the build and tests

Emphasis on 'low hanging'. No change of the build system, no heroic efforts (I keep repeating that in this article but this is so important).

Again, in a typical C++ project, you'd be amazed at how much work the build system is doing without having to do it at all. Try these ideas below and measure if that helps or not:
- Building and running tests *of your dependencies*. In a project which was using `unittest++` as a test framework, built as a cmake subproject, I discovered that the default behavior was to build the tests of the test framework, and run them, every time! That's crazy. Usually there is a CMake variable or such to opt-out of this.
- Building and running example programs *of your dependencies*. Same thing as above, the culprit that time was `mbedtls`. Again, setting a CMake variable to opt-out of that solved it.
- Building and running the tests of your project by default when it's being included as a subproject of another parent project. Yeah the default behavior we just laughed at in our dependencies? It turns out we're doing the same to other projects! I am no CMake expert but it seems that there is no standard way to exclude tests in a build. So I recommend adding a build variable called `MYPROJECT_TEST` unset by default and only build and run tests when it is set. Typically only developers working on the project directly will set it. Same with examples, generating documentation, etc.
- Building all of a third-party dependency when you only need a small part of it: `mbedtls` comes to mind as a good citizen here since it exposes many compile-time flags to toggle lots of parts you might not need. Beware of the defaults, and only build what you need!
- Wrong dependencies listed for a target leading to rebuilding the world when it does not have to: most build systems have a way to output the dependency graph from their point of view and that can really help diagnose these issues. Nothing feels worse than waiting for minutes or hours for a rebuild when you deep inside now it should have only rebuilt a few files.
- Experiment with a faster linker: `mold` is one that can dropped in and really help at no cost. However that really depends on how many libraries are being linked, whether that's a bottleneck overall, etc. 
- Experiment with a different compiler, if you can: I have seen projects where clang is twice as fast as gcc, and others where there is no difference. 


Once that's done, here are a few things to additionally try, although the gains are typically much smaller or sometimes negative:

- LTO: off/on/thin
- Split debug information
- Make vs Ninja
- The type of filesystem in use, and tweaking its settings

Once the iteration cycle feels ok, the code gets to go under the microscope.


## Remove all unnecessary code

Dad, I see dead lines of code.

(Get the reference? Well, ok then.)

I have seen 30%, sometimes more, of a codebase, being completely dead code. That's lines of code you pay for every time you compile, you want to make a refactoring, etc. So let's rip them out.

Here are some ways to go about it:
- The compiler has a bunch of `-Wunused-xxx` warnings, e.g. `-Wunused-function`. They catch some stuff, but not all. Every single instance of these warnings should be addressed. Usually it's as easy as deleting the code, rebuilding and re-running the tests, done. In rare cases it's a symptom of a bug where the wrong function was called. So I'd be somewhat relucant to fully automate this step. But if you're confident in your test suite, go for it.
- Linters can find unused functions or class fields, e.g. `cppcheck`. In my experience there are quite a lot of false positives especially regarding virtual functions in the case of inheritance, but the upside is that these tools absolutely find unused things that the compilers did not notice. So, a good excuse for adding a linter to your arsenal if not to the CI (more on that later).
- I have seen more exotic techniques were the linker is instructed to put each function in its own section and print everytime a section is removed because it's detected to be unused at link time, but that results in so much noise e.g. about standard library functions being unused, that I have not found that really practical. Others inspect the generated assembly and compare which functions are present there with the source code, but that does not work for virtual functions. So, maybe worth a shot, depending on your case?
- Remember the list of supported platforms? Yeah, time to put it to use to kill all the code for unsupported platforms. Code trying to support ancient versions of Solaris on a project that exclusively ran on FreeBSD?  Out of the window it goes. Code trying to provide its own random number generator because maybe the platform we run on does not have one (of course it turned out that was never the case)? To the bin. Hundred of lines of code in case POSIX 2001 is not supported, when we only run on modern Linux and macOS? Nuke it. Checking if the host CPU is big-endian and swapping bytes if it is? Ciao. That code introduced years ago for a hypothetical feature that never came? Hasta la vista.


And the bonus for doing all of this, is not that you sped up at zero cost the build time by a factor of 5, is that if your boss is a tiny bit technical, they'll love seeing PRs deleting thousands of lines of code. And your coworkers as well.



