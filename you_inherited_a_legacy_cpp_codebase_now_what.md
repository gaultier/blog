# You've just inherited a legacy C++ codebase, now what?

You were minding your own business, and out of nowhere something fell on your lap. Maybe you started a new job, or perhaps changed teams, or someone experienced just left.

And now you are responsible for a C++ codebase. It's big, complex, idiosyncratic; you stare too long at it and it breaks in various interesting ways. In a word, legacy. 

But somehow bugs still need to be fixed, the odd feature to be added. In short, you can't just ignore it or better yet nuke it out of existence. It matters. At least to someone who's paying your salary. So, it matters to you. 

What do you do now? 

Well, fear not, because I have experience this many times in numerous places (the snarky folks in the back will mutter: what C++ codebase isn't exactly like I described above), and there is a way out, that's not overly painful and will make you able to actually fix the bugs, add features, and, one can dream, even rewrite it some day.

So join me on a recollection of what worked for me and what one should absolutely avoid.

And to be fair to C++, I do not hate it (per se), it just happens to be one of these languages that people abuse and invariably leads to a horrifying mess and poor C++ is just the victim here and the C++ committee will fix it in C++45, worry not, by adding `std::cmake` to the standard library and you'll see how it's absolutely a game changer, and - Ahem, ok let's go back to the topic at hand.

So here's an overview of the steps to take:

1. Get it to work locally, by only doing the minimal changes required in the code and build system, ideally none. No big refactorings yet, even if itches really bad!
2. Get out the chainsaw and rip out everything that's not absolutely required to provide the features your company/open source project is advertising and selling
3. Make the project enter the 21st century by adding CI, linters, fuzzing, auto-formatting, etc
4. Finally we get to make small, incremental changes to the code, Rinse and repeat until you're not awaken every night by nightmares of Russian hackers p@wning your application after a few seconds of poking at it
5. If you can, contemplate rewrite some parts in a memory safe language


The overarching goal is exerting the least amount of effort to get the project in an acceptable state in terms of security, developer experience, correctness, and performance. It's crucial to always keep that in mind. It's not about 'clean code', using the new hotness language features, etc.

Ok, let's dive in!

*By the way, everything here applies to a pure C codebase or a mixed C and C++ codebase, so if that's you, keep reading!*

**Table of contents**

-   [Get buy-in](#get-buy-in)
-   [Write down the platforms you support](#write-down-the-platforms-you-support)
-   [Get the build working on your
    machine](#get-the-build-working-on-your-machine)
-   [Get the tests passing on your
    machine](#get-the-tests-passing-on-your-machine)
-   [Write down in the README how to build and test the
    application](#write-down-in-the-readme-how-to-build-and-test-the-application)
-   [Find low hanging fruits to speed up the build and
    tests](#find-low-hanging-fruits-to-speed-up-the-build-and-tests)
-   [Remove all unnecessary code](#remove-all-unnecessary-code)
-   [Linters](#linters)
-   [Code formatting](#code-formatting)
-   [Sanitizers](#sanitizers)
-   [Add a CI pipeline](#add-a-ci-pipeline)
-   [Incremental code improvements](#incremental-code-improvements)
-   [Rewrite in a memory safe
    language?](#rewrite-in-a-memory-safe-language)
-   [Conclusion](#conclusion)
-   [Addendum: Dependency
    management](#addendum-dependency-management)

## Get buy-in

You thought I was going to compare the different sanitizers, compile flags, or build systems? No sir, before we do any work, we talk to people. Crazy, right?

Software engineering needs to be a sustainable practice, not something you burn out of after a few months or years. We cannot do this after hours, on a death march, or even, alone! We need to convince people to support this effort, have them understand what we are doing, and why. And that encompasses everyone: your boss, your coworkers, even non-technical folks. And who knows, maybe you'll go on vacation and return to see that people are continuing this effort when you're out of office.

All of this only means: explain in layman terms the problem with a few simple facts, the proposed solution, and a timebox. Simple right? For example (to quote South Park: *All characters and events in this show—even those based on real people—are entirely fictional*):

- Hey boss, the last hire took 3 weeks to get the code building on his machine and make his first contribution. Wouldn't it be nice if, with minimal effort, we could make that a few minutes?
- Hey boss, I put quickly together a simple fuzzing setup ('inputting random data in the app like a monkey and seeing what happens'), and it manages to crash the app 253 times within a few seconds. I wonder what would happen if people try to do that in production with our app?
- Hey boss, the last few urgent bug fixes took several people and 2 weeks to be deployed in production because the app can only be built by this one build server with this ancient operating system that has not been supported for 8 years (FreeBSD 9, for the curious) and it kept failing. Oh by the way whenever this server dies we have no way to deploy anymore, like at all. Wouldn't it be nice to be able to build our app on any cheap cloud instance?
- Hey boss, we had a cryptic bug in production affecting users, it took weeks to figure out and fix, and it turns out if was due to undefined behavior ('a problem in the code that's very hard to notice') corrupting data, and when I run this industry standard linter ('a program that finds issues in the code') on our code, it detects the issue instantly. We should run that tool every time we make a change!
- Hey boss, the yearly audit is coming up and the last one took 7 months to pass because the auditor was not happy with what they saw. I have ideas to make that smoother.
- Hey boss, there is a security vulnerability in the news right now about being able to decrypt encrypted data and stealing secrets, I think we might be affected, but I don't know for sure because the cryptography library we use has been vendored ('copy-pasted') by hand with some changes on top that were never reviewed by anyone. We should clean that up and setup something so that we get alerted automatically if there is a vulnerability that affects us.

And here's what to avoid, again totally, super duper fictional, never-really-happened-to-me examples:

- We are not using the latest C++ standard, we should halt all work for 2 weeks to upgrade, also I have no idea if something will break because we have no tests
- I am going to change a lot of things in the project on a separate branch and work on it for months. It's definitely getting merged at some point! (*narrator's voice:* it wasn't)
- We are going to rewrite the project from scratch, it should take a few weeks tops
- We are going to improve the codebase, but no idea when it will be done or even what we are going to do exactly


Ok, let's say that now you have buy-in from everyone that matters, let's go over the process:

- Every change is small and incremental. The app works before and works after. Tests pass, linters are happy, nothing was bypassed to apply the change (exceptions do happen but that's what they are, exceptional)
- If an urgent bug fix has to be made, it can be done as usual, nothing is blocked
- Every change is a measurable improvement and can be explained and demoed to non experts
- If the whole effort has to be suspended or stopped altogether (because of priorities shifting, budget reasons, etc), it's still a net gain overall compared to before starting it (and that gain is in some form *measurable*)

In my experience, with this approach, you keep everyone happy and can do the improvements that you really need to do.

Alright, let's get down to business now!

## Write down the platforms you support

This is so important and not many projects do it. Write in the README (you do have a README, right?). It's just a list of `<architecture>-<operating-system>` pair, e.g. `x86_64-linux` or `aarch64-darwin`, that your codebase officially supports. This is crucial for getting the build working on every one of them but also and we'll see later, removing cruft for platforms you do *not* support.

If you want to get fancy, you can even write down which version of the architecture such as ARMV6 vs ARMv7, etc.

That helps answer important questions such as:

- Can we rely on having hardware support for floats, or SIMD, or SHA256?
- Do we even care about supporting 32 bits?
- Are we ever running on a big-endian platform? (The answer is very likely: no, never did, never will - if you do, please email me with the details because that sounds interesting).
- Can a `char` be 7 bits?

And an important point: This list should absolutely include the developers workstations. Which leads me to my next point:

## Get the build working on your machine

You'd be amazed at how many C++ codebase in the wild that are a core part of a successful product earning millions and they basically do not compile. Well, if all the stars are aligned they do. But that's not what I'm talking about. I'm talking about reliably, consistently building on all platforms you support. No fuss, no 'I finally got it building after 3 weeks of hair-pulling' (this brings back some memories). It just works(tm).

A small aparte here. I used to be really into Karate. We are talking 3, 4 training sessions a week, etc. And I distinctly remember one of my teachers telling me (picture a wise Asian sifu - hmm actually my teacher was a bald white guy... picture Steve Ballmer then):

> You do not yet master this move. Sometimes you do and sometimes you don't, so you don't. When eating with a spoon, do you miss your mouth one out of five times?

And I carried that with me as a Software Engineer. 'The new feature works' means it works every time. Not four out of five times. And so the build is the same.

Experience has shown me that the best way to produce software in a fast and efficient way is to be able to build on your machine, and ideally even run it on your machine.

Now if your project is humongous that may be a problem, your system might not even have enough RAM to complete the build. A fallback is to rent a big server somewhere and run your builds here. It's not ideal but better than nothing.

Another hurdle is the code requiring some platform specific API, for example `io_uring` on Linux. What can help here is to implement a shim, or build inside a virtual machine on your workstation. Again, not ideal but better than nothing. 

I have done all of the above in the past and that works but building directly on your machine is still the best option.

## Get the tests passing on your machine

First, if there are no tests, I am sorry. This is going to be really difficult to do any change at all. So go write some tests before doing any change to the code, make them pass, and come back. The easiest way is to capture inputs and outputs of the program running in the real world and write end-to-end tests based on that, the more varied the better. It will ensure there are no regressions when making changes, not that the behavior was correct in the first place, but again, better than nothing.

So, now you have a test suite. If some tests fail, disable them for now. Make them pass, even if the whole test suite takes hours to run. We'll worry about that later.

## Write down in the README how to build and test the application

Ideally it's one command to build and one for testing. At first it's fine if it's more involved, in that case the respective commands can be put in a `build.sh` and `test.sh` that encapsulate the madness.

The goal is to have a non C++ expert be able to build the code and run the tests without having to ask you anything. 


Here some folks would recommend documenting the project layout, the architecture, etc. Since the next step is going to rip out most of it, I'd say don't waste your time now, do that at the end.


## Find low hanging fruits to speed up the build and tests

Emphasis on 'low hanging'. No change of the build system, no heroic efforts (I keep repeating that in this article but this is so important).

Again, in a typical C++ project, you'd be amazed at how much work the build system is doing without having to do it at all. Try these ideas below and measure if that helps or not:

- Building and running tests *of your dependencies*. In a project which was using `unittest++` as a test framework, built as a CMake subproject, I discovered that the default behavior was to build the tests of the test framework, and run them, every time! That's crazy. Usually there is a CMake variable or such to opt-out of this.
- Building and running example programs *of your dependencies*. Same thing as above, the culprit that time was `mbedtls`. Again, setting a CMake variable to opt-out of that solved it.
- Building and running the tests of your project by default when it's being included as a subproject of another parent project. Yeah the default behavior we just laughed at in our dependencies? It turns out we're doing the same to other projects! I am no CMake expert but it seems that there is no standard way to exclude tests in a build. So I recommend adding a build variable called `MYPROJECT_TEST` unset by default and only build and run tests when it is set. Typically only developers working on the project directly will set it. Same with examples, generating documentation, etc.
- Building all of a third-party dependency when you only need a small part of it: `mbedtls` comes to mind as a good citizen here since it exposes many compile-time flags to toggle lots of parts you might not need. Beware of the defaults, and only build what you need!
- Wrong dependencies listed for a target leading to rebuilding the world when it does not have to: most build systems have a way to output the dependency graph from their point of view and that can really help diagnose these issues. Nothing feels worse than waiting for minutes or hours for a rebuild, when deep inside, you know it should have only rebuilt a few files.
- Experiment with a faster linker: `mold` is one that can dropped in and really help at no cost. However that really depends on how many libraries are being linked, whether that's a bottleneck overall, etc. 
- Experiment with a different compiler, if you can: I have seen projects where clang is twice as fast as gcc, and others where there is no difference. 


Once that's done, here are a few things to additionally try, although the gains are typically much smaller or sometimes negative:

- LTO: off/on/thin
- Split debug information
- Make vs Ninja
- The type of file system in use, and tweaking its settings

Once the iteration cycle feels ok, the code gets to go under the microscope. If the build takes ages, it's not realistic to want to modify the code.


## Remove all unnecessary code

Dad, I see dead lines of code.

(Get the reference? Well, ok then.)

I have seen 30%, sometimes more, of a codebase, being completely dead code. That's lines of code you pay for every time you compile, you want to make a refactoring, etc. So let's rip them out.

Here are some ways to go about it:

- The compiler has a bunch of `-Wunused-xxx` warnings, e.g. `-Wunused-function`. They catch some stuff, but not everything. Every single instance of these warnings should be addressed. Usually it's as easy as deleting the code, rebuilding and re-running the tests, done. In rare cases it's a symptom of a bug where the wrong function was called. So I'd be somewhat reluctant to fully automate this step. But if you're confident in your test suite, go for it.
- Linters can find unused functions or class fields, e.g. `cppcheck`. In my experience there are quite a lot of false positives especially regarding virtual functions in the case of inheritance, but the upside is that these tools absolutely find unused things that the compilers did not notice. So, a good excuse for adding a linter to your arsenal, if not to the CI (more on that later).
- I have seen more exotic techniques were the linker is instructed to put each function in its own section and print every time a section is removed because it's detected to be unused at link time, but that results in so much noise e.g. about standard library functions being unused, that I have not found that really practical. Others inspect the generated assembly and compare which functions are present there with the source code, but that does not work for virtual functions. So, maybe worth a shot, depending on your case?
- Remember the list of supported platforms? Yeah, time to put it to use to kill all the code for unsupported platforms. Code trying to support ancient versions of Solaris on a project that exclusively ran on FreeBSD?  Out of the window it goes. Code trying to provide its own random number generator because maybe the platform we run on does not have one (of course it turned out that was never the case)? To the bin. Hundred of lines of code in case POSIX 2001 is not supported, when we only run on modern Linux and macOS? Nuke it. Checking if the host CPU is big-endian and swapping bytes if it is? Ciao (when was the last time you shipped code for a big-endian CPU? And if yes, how are you finding IBM?). That code introduced years ago for a hypothetical feature that never came? Hasta la vista.


And the bonus for doing all of this, is not only that you sped up the build time by a factor of 5 with zero downside, is that, if your boss is a tiny bit technical, they'll love seeing PRs deleting thousands of lines of code. And your coworkers as well.


## Linters

Don't go overboard with linter rules, add a few basic ones, incorporate them in the development life cycle, incrementally tweak the rules and fix the issues that pop up, and move on. Don't try to enable all the rules, it's just a rabbit hole of diminishing returns. I have used `clang-tidy` and `cppcheck` in the past, they can be helpful, but also incredibly slow and noisy, so be warned. Having no linter is not an option though. The first time you run the linter, it'll catch so many real issues that you'll wonder why the compiler is not detecting anything even with all the warnings on.

## Code formatting

Wait for the appropriate moment where no branches are active (otherwise people will have horrendous merge conflicts), pick a code style at random, do a one time formatting of the entire codebase (no exceptions), typically with `clang-format`, commit the configuration, done. Don't waste any bit of saliva arguing about the actual code formatting. It only exists to make diffs smaller and avoid arguments, so do not argue about it!

## Sanitizers

Same as linters, it can be a rabbit hole, unfortunately it's absolutely required to spot real, production affecting, hard to detect, bugs and to be able to fix them. `-fsanitize=address,undefined` is a good baseline. They usually do not have false positives so if something gets detected, go fix it. Run the tests with it so that issues get detected there as well. I even heard of people running the production code with some sanitizers enabled, so if your performance budget can allow it, it could be a good idea.

If the compiler you (have to) use to ship the production code does not support sanitizers, you can at least use clang or such when developing and running tests. That's when the work you did on the build system comes in handy, it should be relatively easy to use different compilers.

One thing is for sure: even in the best codebase in the world, with the best coding practices and developers, the second you enable the sanitizers, you absolutely will uncover horrifying bugs and memory leaks that went undetected for years. So do it. Be warned that fixing these can require a lot of work and refactorings. 
Each sanitizer also has options so it could be useful to inspect them if your project is a special snowflake.

One last thing: ideally, all third-party dependencies should also be compiled with the sanitizers enabled when running tests, to spot [issues](https://github.com/rxi/microui/pull/67) in them as well.

## Add a CI pipeline

As Bryan Cantrill once said (quoting from memory), 'I am convinced most firmware just comes out of the home directory of a developer's laptop'. Setting up a CI is quick, free, and automates all the good things we have set up so far (linters, code formatting, tests, etc). And that way we can produce in a pristine environment the production binaries, on every change. If you're not doing this already as a developer, I don't think you really have entered the 21st century yet. 

Cherry on the cake: most CI systems allow for running the steps on a matrix of different platforms! So you can demonstrably check that the list of supported platforms is not just theory, it is real.

Typically the pipeline just looks like `make all test lint fmt`  so it's not rocket science. Just make sure that issues that get reported by the tools (linters, sanitizers, etc) actually fail the pipeline, otherwise no one will notice and fix them.


## Incremental code improvements

Well that's known territory so I won't say much here. Just that lots of code can often be dramatically simplified.

I remember iteratively simplifying a complicated class that manually allocated and (sometimes) deallocated memory, was meant to handle generic things, and so on. All the class did, as it turned out, was allocate a pointer, later check whether the pointer was null or not, and...that's it. Yeah that's a boolean in my book. True/false, nothing more to it.

I feel that's the step that's the hardest to timebox because each round of simplification opens new avenues to simplify further. Use your best judgment here and stay on the conservative side. Focus on tangible goals such as security, correctness and performance, and stray away from subjective criteria such as 'clean code'.

In my experience, upgrading the C++ standard in use in the project can at times help with code simplifications, for example to replace code that manually increments iterators by a `for (auto x : items)` loop, but remember it's just a means to an end, not an end in itself. If all you need is `std::clamp`, just write it yourself.

## Rewrite in a memory safe language?

I am doing this right now at work, and that deserves an article of its own. Lots of gotchas there as well. Only do this with a compelling reason.


## Conclusion

Well, there you have it. A tangible, step-by-step plan to get out of the finicky situation that's a complex legacy C++ codebase. I have just finished going through that at work on a project, and it's become much more bearable to work on it now. I have seen coworkers, who previously would not have come within a 10 mile radius of the codebase, now make meaningful contributions. So it feels great.

There are important topics that I wanted to mention but in the end did not, such as the absolute necessity of being able to run the code in a debugger locally, fuzzing, dependency scanning for vulnerabilities, etc. Maybe for the next article!

If you go through this on a project, and you found this article helpful, shoot me an email! It's nice to know that it helped someone.

## Addendum: Dependency management

*This section is very subjective, it's just my strong, biased opinion.*

There's a hotly debated topic that I have so far carefully avoided and that's dependency management. So in short, in C++ there's none. Most people resort to using the system package manager, it's easy to notice because their README looks like this:

```
On Ubuntu 20.04: `sudo apt install [100 lines of packages]`

On macOS: `brew install [100 lines of packages named slightly differently]`

Any other: well you're out of luck buddy. I guess you'll have to pick a mainstream OS and reinstall ¯\_(ツ)_/¯
```

Etc. I have done it myself. And I think this is a terrible idea. Here's why:

- The installation instructions, as we've seen above, are OS and distribution dependent. Worse, they're dependent on the version of the distribution. I remember a project that took months to move from Ubuntu 20.04 to Ubuntu 22.04, because they ship different versions of the packages (if they ship the same packages at all), and so upgrading the distribution also means upgrading the 100 dependencies of your project at the same time. Obviously that's a very bad idea. You want to upgrade one dependency at a time, ideally.
- There's always a third-party dependency that has no package and you have to build it from source anyway.
- The packages are never built with the flags you want. Fedora and Ubuntu have debated for years whether to build packaged with the frame pointer enabled (they finally do since very recently). Remember the section about sanitizers? How are you going to get dependencies with sanitizer enabled? It's not going to happen. But there are way more examples: LTO, `-march`, debug information, etc. Or they were built with a different C++ compiler version from the one you are using and they broke the C++ ABI between the two.
- You want to easily see the source of the dependency when auditing, developing, debugging, etc, *for the version you are currently using*.
- You want to be able to patch a dependency easily if you encounter a bug, and rebuild easily without having to change the build system extensively
- You never get the exact same version of a package across systems, e.g. when developer Alice is on macOS, Bob on Ubuntu and the production system on FreeBSD. So you have weird discrepancies you cannot reproduce and that's annoying.
- Corollary of the point above: You don't know exactly which version(s) you are using across systems and it's hard to produce a Bill of Material (BOM) in an automated fashion, which is required (or going to be required very soon? Anyway it's a good idea to have it) in some fields.
- The packages sometimes do not have the version of the library you need (static or dynamic)

So you're thinking, I know, I will use those fancy new package managers for C++, Conan, vcpkg and the like! Well, not so fast:

- They require external dependencies so your CI becomes more complex and slower (e.g. figuring out which exact version of Python they require, which surely will be different from the version of Python your project requires)
- They do not have all versions of a package. Example: [Conan and mbedtls](https://conan.io/center/recipes/mbedtls), it jumps from version `2.16.12` to `2.23.0`. What happened to the versions in between? Are they flawed and should not be used? Who knows! Security vulnerabilities are not listed anyways for the versions available! Of course I had a project in the past where I had to use version `2.17`...
- They might not support some operating systems or architectures you care about (FreeBSD, ARM, etc)

I mean, if you have a situation where they work for you, that's great, it's definitely an improvement over using system packages in my mind. It's just that I never encountered (so far) a project where I could make use of them - there was always some blocker.


So what do I recommend? Well, the good old git submodules and compiling from source approach. It's cumbersome, yes, but also:

- It's dead simple
- It's better than manually vendoring because git has the history and the diff functionalities
- You know exactly, down to the commit, which version of the dependency is in use
- Upgrading the version of a single dependency is trivial, just run `git checkout`
- It works on every platform
- You get to choose exactly the compilation flags, compiler, etc to build all the dependencies. And you can even tailor it per dependency!
- Developers know it already even if they have no C++ experience
- Fetching the dependencies is secure and the remote source is in git. No one is changing that sneakily.
- It works recursively (i.e.: transitively, for the dependencies of your dependencies)

Compiling each dependency in each submodule can be as simple as `add_subdirectory` with CMake, or `git submodule foreach make` by hand. 

If submodules are really not an option, an alternative is to still compile from source but do it by hand, with one script, that fetches each dependency and builds it. Example in the wild: Neovim.

Of course, if your dependency graph visualized in Graphviz looks like a Rorschach test and has to build thousands of dependencies, it is not easily doable, but it might be still possible, using a build system like Buck2, which does hybrid local-remote builds, and reuses build artifacts between builds from different users. 

If you look at the landscape of package managers for compiled languages (Go, Rust, etc), all of them that I know of compile from source. It's the same approach, minus git, plus the automation.
