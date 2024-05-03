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

So join me on this journey, here's the guide to rewrite a C++ codebase successfully.


## The project

This project is a C++ library that exposes a C API. The final artifacts are a `.a` library and a `.h` header. It is used to talk to applets on a [smart card](https://en.wikipedia.org/wiki/Smart_card) like your credit card, ID, passport or driving license (yes, smart cards are nowadays everywhere - you probably carry several on you right now), since they use a ~~bizzarre~~ interesting communication protocol. The library also implements a home-grown protocol on top of the well-specified smart card protocol, encryption, and business logic.

This library is used in:

- Android applications through JNI
- Go back-end services running in the cloud through CGO
- iOS applications trough Swift FFI
- C and C++ applications running on very small 32 bits ARM boards similar to the first Raspberry Pi

Additionally, developers are using macOS (x64 and arm64) and Linux so the library needs to build and run on these platforms.




