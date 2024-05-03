# How to rewrite a C++ codebase successfully

I recently wrote about [inheriting a legacy C++ codebase](/blog/you_inherited_a_legacy_cpp_codebase_now_what.html). At some point, although I cannot pinpoint exactly when, a few things became clear to me:

- No one in the team but me is able - or feels confident enough - to make a change in this codebase
- This is a crucial project for the company and will live for years if not decades
- The code is pretty bad on all the criteria we care about: correctness, maintainability, security, you name it. I don't blame the original developers, they were understaffed and it was written as a prototype (the famous case of the prototype which becomes the production code).
- No hiring of C++ developers is planned

So it was apparent that sticking with C++ was a dead end. The only solution would be to train everyone in the team on C++ and dedicate a significant amount of time rewriting the most problematic parts of the codebase to perhaps reach a good enough state. It's a judgement call in the end, but that seemed to be more effort than 'simply' introducing a new language and doing a rewrite.

I don't actually like the term 'rewrite'. Folks on the internet will eagerly repeat that rewrites are a bad idea, will indubtedly fail, and are a sign of hubris and naivity. I have experienced such rewrites, from scratch, and yes that does not end well.

However, I claim, because I've done it, and many others before me, that an **incremental** rewrite can be successful, and is absolutely worth it. It's all about how it is being done, so here's how I proceeded and I hope it can be applied in other cases, and people find it useful.

I think it's a good case study because whilst not a big codebase, it is a complex codebase, and it's used in production on 10+ different operating systems and architectures, including by external customers. This is not a toy. 

So join me on this journey, here's the guide to rewrite a C++ codebase successfully. And also what not do!


## The project

This project is a library that exposes a C API but the implementation is C++. The final artifacts are a `libfoo.a` static library and a `libfoo.h` C header. It is used to talk to applets on a [smart card](https://en.wikipedia.org/wiki/Smart_card) like your credit card, ID, passport or driving license (yes, smart cards are nowadays everywhere - you probably carry several on you right now), since they use a ~~bizzarre~~ interesting communication protocol. The library also implements a home-grown protocol on top of the well-specified smart card protocol, encryption, and business logic.

This library is used in:

- Android applications, through JNI
- Go back-end services running in the Kubernetes, through CGO
- iOS applications, through Swift FFI
- C and C++ applications running on very small 32 bits ARM boards similar to the first Raspberry Pi

Additionally, developers are using macOS (x64 and arm64) and Linux so the library needs to build and run on these platforms.

Since external customers also integrate their applications with our library and we do not control these environments, we also need to work with either the glibc and the musl C libraries, as well a clang and gcc, and expose a C89-ish API, to maximize compatibility.

Alright, now that the stage is set, let's go through the steps of rewriting this project.

## Improve the existing codebase

That's basically all the steps in [Inheriting a legacy C++ codebase](/blog/you_inherited_a_legacy_cpp_codebase_now_what.html). We need to start the rewrite with a codebase that builds and runs on every platform we support, with tests passing, and a clear README explaining how to setup the project locally. This is a small investment (1 or 2 weeks) that will pay massive dividends in the future. 

But I think the most important point is to trim all the unused code which is typically the majority of the codebase! No one wants to spend time and effort on rewriting completely unused code.

Additionally, if you fail to convince your team and the stakeholders to do the rewrite, you at least have improved the codebase you are now stuck with.

## Get buy-in

Same as in my previous article: Buy-in from teammates and stakeholders is probably the most important thing to get, and maintain. 

It's a big investment in time and thus money we are talking about, it can only work with everyone on board.

Here I think the way to go is showing the naked truth and staying very factual, in terms managers and non-technical people can understand. This is roughly what I presented:

- The bus factor for this project is 1
- Tool X shows that there are memory leaks at the rate of Y MiB/hour which means the application using our library will be OOM killed after around Z hours.
- Quick and dirty fuzzing manages to make the library crash 133 times in 10 seconds
- Linter X detects hundreds of real issues we need to fix
- All of these points make it really likely a hacker can exploit our library to gain Remote Code Execution (RCE) or steal secrets

Essentially, it's a matter of genuinely presenting the alternative of rewriting being cheaper in terms of time and effort compared to improving the project with pure C++. If your teammates and boss are rationale, it should be a straightforward decision.



## Preparing to introduce the new language

Once I reached this point, I created a Git tag `last-before-rust` (spoiler alert!). The commit right after introduced the first lines of code in the new language.

This proved invaluable, because when rewriting the legacy code, I found tens of bugs lying around, and I think that's very typical. Also, this rewriting effort requires time, during which other team members or external customers may report bugs they just found.

Every time such a bug appeared, I switched to this Git tag, and tried to reproduce the bug. Almost every time, the bug was already present before the rewrite. That's a very important information (for me, it was a relief!) for solving the bug, and also for stakeholders. That's the difference in their eye between: We are improving the product by fixing long existing bugs; or: we are introducing new bugs with our risky changes and we should maybe stop the effort completely because it's harming the product.

Stakeholder support is a big deal.
