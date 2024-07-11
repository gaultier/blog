# You've just inherited a legacy C++ codebase, now what?

*<svg aria-hidden="true" focusable="false" role="img" class="octicon octicon-tag" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" style="display:inline-block;user-select:none;overflow:visible"><path d="M1 7.775V2.75C1 1.784 1.784 1 2.75 1h5.025c.464 0 .91.184 1.238.513l6.25 6.25a1.75 1.75 0 0 1 0 2.474l-5.026 5.026a1.75 1.75 0 0 1-2.474 0l-6.25-6.25A1.752 1.752 0 0 1 1 7.775Zm1.5 0c0 .066.026.13.073.177l6.25 6.25a.25.25 0 0 0 .354 0l5.025-5.025a.25.25 0 0 0 0-.354l-6.25-6.25a.25.25 0 0 0-.177-.073H2.75a.25.25 0 0 0-.25.25ZM6 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2Z"></path></svg> [C++](/blog/articles-per-tag.html#C++), [C](/blog/articles-per-tag.html#C), [Legacy](/blog/articles-per-tag.html#Legacy), [CI](/blog/articles-per-tag.html#CI), [Git](/blog/articles-per-tag.html#Git), [Rewrite](/blog/articles-per-tag.html#Rewrite)*


*This article was discussed on [Hacker News](https://news.ycombinator.com/item?id=39549486), [Lobster.rs](https://lobste.rs/s/lf8b9r/you_ve_just_inherited_legacy_c_codebase) and [Reddit](https://old.reddit.com/r/programming/comments/1b3143w/youve_just_inherited_a_legacy_c_codebase_now_what/). I've got great suggestions from the comments, see the addendum at the end!*

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
-   [Addendum: suggestions from readers](#addendum-suggestions-from-readers)

<h2 id="get-buy-in">Get buy-in</h2>

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

<h2 id="write-down-the-platforms-you-support">Write down the platforms you support</h2>

This is so important and not many projects do it. Write in the README (you do have a README, right?). It's just a list of `<architecture>-<operating-system>` pair, e.g. `x86_64-linux` or `aarch64-darwin`, that your codebase officially supports. This is crucial for getting the build working on every one of them but also and we'll see later, removing cruft for platforms you do *not* support.

If you want to get fancy, you can even write down which version of the architecture such as ARMV6 vs ARMv7, etc.

That helps answer important questions such as:

- Can we rely on having hardware support for floats, or SIMD, or SHA256?
- Do we even care about supporting 32 bits?
- Are we ever running on a big-endian platform? (The answer is very likely: no, never did, never will - if you do, please email me with the details because that sounds interesting).
- Can a `char` be 7 bits?

And an important point: This list should absolutely include the developers workstations. Which leads me to my next point:

<h2 id="get-the-build-working-on-your-machine">Get the build working on your machine</h2>

You'd be amazed at how many C++ codebase in the wild that are a core part of a successful product earning millions and they basically do not compile. Well, if all the stars are aligned they do. But that's not what I'm talking about. I'm talking about reliably, consistently building on all platforms you support. No fuss, no 'I finally got it building after 3 weeks of hair-pulling' (this brings back some memories). It just works(tm).

A small aparte here. I used to be really into Karate. We are talking 3, 4 training sessions a week, etc. And I distinctly remember one of my teachers telling me (picture a wise Asian sifu - hmm actually my teacher was a bald white guy... picture Steve Ballmer then):

> You do not yet master this move. Sometimes you do and sometimes you don't, so you don't. When eating with a spoon, do you miss your mouth one out of five times?

And I carried that with me as a Software Engineer. 'The new feature works' means it works every time. Not four out of five times. And so the build is the same.

Experience has shown me that the best way to produce software in a fast and efficient way is to be able to build on your machine, and ideally even run it on your machine.

Now if your project is humongous that may be a problem, your system might not even have enough RAM to complete the build. A fallback is to rent a big server somewhere and run your builds here. It's not ideal but better than nothing.

Another hurdle is the code requiring some platform specific API, for example `io_uring` on Linux. What can help here is to implement a shim, or build inside a virtual machine on your workstation. Again, not ideal but better than nothing. 

I have done all of the above in the past and that works but building directly on your machine is still the best option.

<h2 id="get-the-tests-passing-on-your-machine">Get the tests passing on your machine</h2>

First, if there are no tests, I am sorry. This is going to be really difficult to do any change at all. So go write some tests before doing any change to the code, make them pass, and come back. The easiest way is to capture inputs and outputs of the program running in the real world and write end-to-end tests based on that, the more varied the better. It will ensure there are no regressions when making changes, not that the behavior was correct in the first place, but again, better than nothing.

So, now you have a test suite. If some tests fail, disable them for now. Make them pass, even if the whole test suite takes hours to run. We'll worry about that later.

<h2 id="write-down-in-the-readme-how-to-build-and-test-the-application">Write down in the README how to build and test the application</h2>

Ideally it's one command to build and one for testing. At first it's fine if it's more involved, in that case the respective commands can be put in a `build.sh` and `test.sh` that encapsulate the madness.

The goal is to have a non C++ expert be able to build the code and run the tests without having to ask you anything. 


Here some folks would recommend documenting the project layout, the architecture, etc. Since the next step is going to rip out most of it, I'd say don't waste your time now, do that at the end.


<h2 id="find-low-hanging-fruits-to-speed-up-the-build-and-tests">Find low hanging fruits to speed up the build and tests</h2>

Emphasis on 'low hanging'. No change of the build system, no heroic efforts (I keep repeating that in this article but this is so important).

Again, in a typical C++ project, you'd be amazed at how much work the build system is doing without having to do it at all. Try these ideas below and measure if that helps or not:

- Building and running tests *of your dependencies*. In a project which was using `unittest++` as a test framework, built as a CMake subproject, I discovered that the default behavior was to build the tests of the test framework, and run them, every time! That's crazy. Usually there is a CMake variable or such to opt-out of this.
- Building and running example programs *of your dependencies*. Same thing as above, the culprit that time was `mbedtls`. Again, setting a CMake variable to opt-out of that solved it.
- Building and running the tests of your project by default when it's being included as a subproject of another parent project. Yeah the default behavior we just laughed at in our dependencies? It turns out we're doing the same to other projects! I am no CMake expert but it seems that there is no standard way to exclude tests in a build. So I recommend adding a build variable called `MYPROJECT_TEST` unset by default and only build and run tests when it is set. Typically only developers working on the project directly will set it. Same with examples, generating documentation, etc.
- Building all of a third-party dependency when you only need a small part of it: `mbedtls` comes to mind as a good citizen here since it exposes many compile-time flags to toggle lots of parts you might not need. Beware of the defaults, and only build what you need!
- Wrong dependencies listed for a target leading to rebuilding the world when it does not have to: most build systems have a way to output the dependency graph from their point of view and that can really help diagnose these issues. Nothing feels worse than waiting for minutes or hours for a rebuild, when deep inside, you know it should have only rebuilt a few files.
- Experiment with a faster linker: `mold` is one that can be dropped in and really help at no cost. However that really depends on how many libraries are being linked, whether that's a bottleneck overall, etc.
- Experiment with a different compiler, if you can: I have seen projects where clang is twice as fast as gcc, and others where there is no difference. 


Once that's done, here are a few things to additionally try, although the gains are typically much smaller or sometimes negative:

- LTO: off/on/thin
- Split debug information
- Make vs Ninja
- The type of file system in use, and tweaking its settings

Once the iteration cycle feels ok, the code gets to go under the microscope. If the build takes ages, it's not realistic to want to modify the code.


<h2 id="remove-all-unnecessary-code">Remove all unnecessary code</h2>

Dad, I see dead lines of code.

(Get the reference? Well, ok then.)

I have seen 30%, sometimes more, of a codebase, being completely dead code. That's lines of code you pay for every time you compile, you want to make a refactoring, etc. So let's rip them out.

Here are some ways to go about it:

- The compiler has a bunch of `-Wunused-xxx` warnings, e.g. `-Wunused-function`. They catch some stuff, but not everything. Every single instance of these warnings should be addressed. Usually it's as easy as deleting the code, rebuilding and re-running the tests, done. In rare cases it's a symptom of a bug where the wrong function was called. So I'd be somewhat reluctant to fully automate this step. But if you're confident in your test suite, go for it.
- Linters can find unused functions or class fields, e.g. `cppcheck`. In my experience there are quite a lot of false positives especially regarding virtual functions in the case of inheritance, but the upside is that these tools absolutely find unused things that the compilers did not notice. So, a good excuse for adding a linter to your arsenal, if not to the CI (more on that later).
- I have seen more exotic techniques were the linker is instructed to put each function in its own section and print every time a section is removed because it's detected to be unused at link time, but that results in so much noise e.g. about standard library functions being unused, that I have not found that really practical. Others inspect the generated assembly and compare which functions are present there with the source code, but that does not work for virtual functions. So, maybe worth a shot, depending on your case?
- Remember the list of supported platforms? Yeah, time to put it to use to kill all the code for unsupported platforms. Code trying to support ancient versions of Solaris on a project that exclusively ran on FreeBSD?  Out of the window it goes. Code trying to provide its own random number generator because maybe the platform we run on does not have one (of course it turned out that was never the case)? To the bin. Hundred of lines of code in case POSIX 2001 is not supported, when we only run on modern Linux and macOS? Nuke it. Checking if the host CPU is big-endian and swapping bytes if it is? Ciao (when was the last time you shipped code for a big-endian CPU? And if yes, how are you finding IBM?). That code introduced years ago for a hypothetical feature that never came? Hasta la vista.


And the bonus for doing all of this, is not only that you sped up the build time by a factor of 5 with zero downside, is that, if your boss is a tiny bit technical, they'll love seeing PRs deleting thousands of lines of code. And your coworkers as well.


<h2 id="linters">Linters</h2>

Don't go overboard with linter rules, add a few basic ones, incorporate them in the development life cycle, incrementally tweak the rules and fix the issues that pop up, and move on. Don't try to enable all the rules, it's just a rabbit hole of diminishing returns. I have used `clang-tidy` and `cppcheck` in the past, they can be helpful, but also incredibly slow and noisy, so be warned. Having no linter is not an option though. The first time you run the linter, it'll catch so many real issues that you'll wonder why the compiler is not detecting anything even with all the warnings on.

<h2 id="code-formatting">Code formatting</h2>

Wait for the appropriate moment where no branches are active (otherwise people will have horrendous merge conflicts), pick a code style at random, do a one time formatting of the entire codebase (no exceptions), typically with `clang-format`, commit the configuration, done. Don't waste any bit of saliva arguing about the actual code formatting. It only exists to make diffs smaller and avoid arguments, so do not argue about it!

<h2 id="sanitizers">Sanitizers</h2>

Same as linters, it can be a rabbit hole, unfortunately it's absolutely required to spot real, production affecting, hard to detect, bugs and to be able to fix them. `-fsanitize=address,undefined` is a good baseline. They usually do not have false positives so if something gets detected, go fix it. Run the tests with it so that issues get detected there as well. I even heard of people running the production code with some sanitizers enabled, so if your performance budget can allow it, it could be a good idea.

If the compiler you (have to) use to ship the production code does not support sanitizers, you can at least use clang or such when developing and running tests. That's when the work you did on the build system comes in handy, it should be relatively easy to use different compilers.

One thing is for sure: even in the best codebase in the world, with the best coding practices and developers, the second you enable the sanitizers, you absolutely will uncover horrifying bugs and memory leaks that went undetected for years. So do it. Be warned that fixing these can require a lot of work and refactorings. 
Each sanitizer also has options so it could be useful to inspect them if your project is a special snowflake.

One last thing: ideally, all third-party dependencies should also be compiled with the sanitizers enabled when running tests, to spot [issues](https://github.com/rxi/microui/pull/67) in them as well.

<h2 id="add-a-ci-pipeline">Add a CI pipeline</h2>

As Bryan Cantrill once said (quoting from memory), 'I am convinced most firmware just comes out of the home directory of a developer's laptop'. Setting up a CI is quick, free, and automates all the good things we have set up so far (linters, code formatting, tests, etc). And that way we can produce in a pristine environment the production binaries, on every change. If you're not doing this already as a developer, I don't think you really have entered the 21st century yet. 

Cherry on the cake: most CI systems allow for running the steps on a matrix of different platforms! So you can demonstrably check that the list of supported platforms is not just theory, it is real.

Typically the pipeline just looks like `make all test lint fmt`  so it's not rocket science. Just make sure that issues that get reported by the tools (linters, sanitizers, etc) actually fail the pipeline, otherwise no one will notice and fix them.


<h2 id="incremental-code-improvements">Incremental code improvements</h2>

Well that's known territory so I won't say much here. Just that lots of code can often be dramatically simplified.

I remember iteratively simplifying a complicated class that manually allocated and (sometimes) deallocated memory, was meant to handle generic things, and so on. All the class did, as it turned out, was allocate a pointer, later check whether the pointer was null or not, and...that's it. Yeah that's a boolean in my book. True/false, nothing more to it.

I feel that's the step that's the hardest to timebox because each round of simplification opens new avenues to simplify further. Use your best judgment here and stay on the conservative side. Focus on tangible goals such as security, correctness and performance, and stray away from subjective criteria such as 'clean code'.

In my experience, upgrading the C++ standard in use in the project can at times help with code simplifications, for example to replace code that manually increments iterators by a `for (auto x : items)` loop, but remember it's just a means to an end, not an end in itself. If all you need is `std::clamp`, just write it yourself.

<h2 id="rewrite-in-a-memory-safe-language">Rewrite in a memory safe language?</h2>

I am doing this right now at work, and that deserves an article of its own. Lots of gotchas there as well. Only do this with a compelling reason.


<h2 id="conclusion">Conclusion</h2>

Well, there you have it. A tangible, step-by-step plan to get out of the finicky situation that's a complex legacy C++ codebase. I have just finished going through that at work on a project, and it's become much more bearable to work on it now. I have seen coworkers, who previously would not have come within a 10 mile radius of the codebase, now make meaningful contributions. So it feels great.

There are important topics that I wanted to mention but in the end did not, such as the absolute necessity of being able to run the code in a debugger locally, fuzzing, dependency scanning for vulnerabilities, etc. Maybe for the next article!

If you go through this on a project, and you found this article helpful, shoot me an email! It's nice to know that it helped someone.

<h2 id="addendum-dependency-management">Addendum: Dependency management</h2>

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

<h2 id="addendum-suggestions-from-readers">Addendum: suggestions from readers</h2>

I've gathered here some great ideas and feedback from readers (sometimes it's the almagamation of multiple comments from different people, and I am paraphrasing from memory so sorry if it's not completely accurate):

- *You should put more emphasis on tests (expanding the test suite, the code coverage, etc) - but also: a test suite in C++ is only worth anything when running it under sanitizers, otherwise you get lured into a false sense of safety.* 100% agree. Modifying complex foreign code without tests is just not possible in my opinion. And yes, sanitizers will catch so many issues in the tests that you should even consider running your tests suite multiple time in CI with different sanitizers enabled.
- *vcpkg is a good dependency manager for C++ that solves all of your woes.* I've never got the chance to use it so I'll add it to my toolbox to experiment with. If it matches the requirements I listed, as well as enabling cross-compilation, then yes it's absolutely a win over git submodules.
- *Nix can serve as a good dependency manager for C++.* I must admit that I was beaten into submission by Nix's complexity and slowness. Maybe in a few years when it has matured?
- *You should not invest so much time in refactoring a legacy codebase if all you are going to do is one bug fix a year.* Somewhat agree, but it really is a judgement call. In my experience it's never one and only one bug fix, whatever management says. And all the good things such as removing dead code, sanitizers etc will be valuable even for the odd bug fix and also lead to noticing more bugs and fixing them. As one replier put it: *If you are going to own a codebase, then own it for real.* 
- *It's very risky to remove code, in general you never know for sure if it's being used or not, and if someone relies on this exact behavior in the wild.* That's true, that's why I advocate for removing code that is never called using static analysis tools, so that you know *for sure*. But yes, when in doubt, don't. My pet peeve here are virtual methods that are very resistant to static analysis (since the whole point is to pick which exact method to call at runtime), these usually cannot be as easily removed. Also, talk to your sales people, your product managers, heck even your users if you can. Most of the time, if you ask them whether a given feature or platform is in use or not, you'll get a swift yes or no reply, and you'll know how to proceed. We engineers sometimes forget that a 15 minute talk with people can simplify  so much technical work.
- *Stick all your code in a LLM and start asking it questions*: As a anti LLM person, I must admit that this idea never crossed my mind. However I think it might be worth a shot, if you can do that in a legally safe way, ideally fully locally, and take everything with a grain a salt. I'm genuinely curious to see what answers it comes up with!
- *There are tools that analyze the code and produce diagrams, class relationships etc to get an overview of the code*: I never used these tools but that's a good idea and I will definitely try one in the future
- *Step 0 should be to add the code in a source control system if that's not the case already*: For sure. I was lucky enough to never encounter that, but heck yeah, even the worst source control system is better than no source control system at all. And I say this after having had to use Visual Source Safe, the one where modifying a file means acquiring an exclusive lock on it, that you then have to release manually.
- *Setting up a CI should be step 1*: Fair point, I can totally see this perspective. I am quicker locally, but fair.
- *Don't be a code beauty queen, just do the fixes you need*: Amen.
- *If you can drop a platform that's barely used to reduce the combinatorial complexity, and that enables you to do major simplifications, go for it*: Absolutely. Talk to your sales people and stakeholders and try to convince them. In my case it was ancient FreeBSD versions long out of support, I think we used the security angle to convince everyone to drop them.
- *Reproducible builds*: This topic came up and was quite the debate. Honestly I don't think achieving a fully reproducible build in a typical C++ codebase is realistic. The compiler and standard library version alone are a problem since they usually are not considered in the build inputs. Achieving a reliable build though is definitely realistic. Docker came up on that topic. Now I have used Docker in anger since 2013 and I don't think it brings as much value as people generally think it does. But again - if all you can do is get the code building inside Docker, it's better than nothing at all.
- *Git can be instructed to ignore one commit e.g. the one formatting the whole codebase so that git blame still works and the history still makes sense*: Fantastic advice that I did not know before, so thanks! I'll definitely try that.
- *Use the VCS statistics from the history to identify which parts of the codebase have the most churn and which ones get usually changed together*: I never tried that, it's an interesting idea, but I also see lots of caveats. Probably worth a try?
- *This article applies not only to C++ but also to legacy codebases in other languages*: Thank you! I have the most experience in C++, so that was my point of view, but I'm glad to hear that. Just skip the C++-specific bits like sanitizers.
- *The book 'Working effectively with Legacy Code' has good advice*: I don't think I have ever read it from start to finish, so thanks for the recommendation. I seem to recall I skimmed it and found it very object-oriented specific with lots of OOP design patterns, and that was not helpful to me at the time, but my memory is fuzzy.
- *Generally, touch as little as possible, focus on what adds value (actual value, like, sales dollars).*: I agree generally (see the point: don't be beauty queen), however in a typical big C++ codebase, the moment you start to examine it under the lens of security, you will find lots and lots a security vulnerabilities that need fixing. And that does not translate in a financial gain - it's reducing risk. And I find that extremely valuable. Although, some fields are more sensitive than others.

