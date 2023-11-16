<link rel="stylesheet" type="text/css" href="main.css">

# Alternative technical articles you won't find elsewhere

- Sep 5 2019: [Getting started with Scheme by solving an Advent of Code 2018 challenge](/blog/advent_of_code_2018_5)
- Sep 7 2020: [How to compile LLVM, Clang, LLD, and Ziglang from source on Alpine Linux](/blog/compile_ziglang_from_source_on_alpine_2020_9)
- Sep 21 2020: [Adventures in CI land, or how to speed up your CI](/blog/speed_up_your_ci.html)
- May 31 2023: [Learn x86-64 assembly by writing a GUI from scratch](/blog/x11_x64.html)
- June 5 2023: [Cycle detection in graphs does not have to be hard: A lesser known, simple way with Kahn's algorithm](/blog/kahns_algorithm.html)
- October 5 2023: [Optimizing an Advent of Code solution in assembly](/blog/advent_of_code_2018_5_revisited.html)
- October 13 2023: [Learn Wayland by writing a GUI from scratch](/blog/wayland_from_scratch.html)

## About me

I am a Senior Software Engineer from France living in Bavaria, Germany. By day, I work for a Fintech company, and by night I write some fun projects in C, Rust, Odin, Zig, and Assembly. I like to work on low-level systems.

In my free time, I like running, lifting weights, gardening, and the outdoors.

Get in touch:

- [Linkedin](https://www.linkedin.com/in/philippegaultier/)
- [Github](https://github.com/gaultier)


## Portfolio

*In chronological order, which is roughly the ascending order of technical difficulty:*

[Prototype using the Oculus Rift](https://github.com/gaultier/Simulation_Stars_OpenGL): This was my second internship and the first time I owned a project from start to finish. This was a blast and possibly the most fun I had in all my career. This is back in 2014 and the first version of the Oculus Rift was all the hype back then. The goal of the project in the astronomy/CERN lab I was at was to explore how to teach kids about the solar system and astronomy by having them put the Oculus Rift on and experiment it for themselves, flying through the stars. It was great! I got to learn about OpenGL, SDL and multiplying specific 4x4 matrices together to slightly move the camera for each eye so that the VR effect happens and your brain is tricked.
In terms of implementation this is pretty subpar C++ code in retrospect - I went all in on OOP and trying to use every C++ feature under the sun. But it worked, kind of, each star was a cube because I ran out of time.
Part of this project was also to add a VR mode to an existing 3D application written in C; that was quite a big codebase and I did indeed add this mode; however it never worked quite right, there was some constant stuttering which might have been due to loss of precision in floating point calculations. 
Overall a ton of fun.

---

[lox-ocaml](https://github.com/gaultier/lox-ocaml) is the first compiler and interpreter I wrote while following along the excellent [Crafting Interpreters](http://craftinginterpreters.com/the-lox-language.html). The first part of the book is a Java implementation which I wanted to avoid and thus went with OCaml which I anyway wanted to dig deep on. It fit because it remains a garbage collected language like Java and has support for imperative code if I needed to follow closely the official code. It went really well, I had a blast and OCaml is pretty nice; however I will never understand why it pushes the developer to use linked list and it is so hard to use dynamic arrays instead. The ecosystem and tooling is a bit split in half but overall it was a great experience.

---

[kotlin-rs](https://github.com/gaultier/kotlin-rs) is a Kotlin compiler written in Rust and my first attempt at it. It is not finished but can still compile Kotlin functions, expressions and statements to JVM bytecode and run that.
This was my first compiler project on my own, I chose Kotlin because I was working with Kotlin at the time and was suffering from the excruciatingly slow official compiler at the time (it improved somewhat since). It allowed me to skip the language design part and focus on the implementation.
I think it still holds up to this day although I would definitely change how the AST is modeled and avoid having each node being a `Box<T>` (see below on another take on the subject). One big topic I did not tackle was generating Stack Map Frames which are a JVM concept to verify bytecode in linear time before running it.

---

[microkt](https://github.com/gaultier/microkt) is my second take on a Kotlin compiler, this time written in C and targeting x86_64 (no JVM). I think it is nice to have the luxury to revisit a past project in a different language and reevaluating past choices. I got about equally as far as the `kotlin-rs` in terms of language support and stopped while implementing the Garbage Collector which is an can of worms by itself. The reason I stopped is that I noticed I was being regularly stopped in my tracks by bugs in the generated x86_64 assembly code, notably due to 'move with zero extend' issues and register allocation. I thus decided to dig deep on this subject before returning to the compiler which I never did because I got two kids right after.
The implementation still holds, however I would definitely revist the assembly generation part as mentioned and use a structured representation instead of a textual one (no one wants to do string programming).

---

[My C monorepo](https://github.com/gaultier/c) is a big mix a small and big programs all written in C. The most noteworthy are:
- `torrent`: A bittorrent client. It works well but only handles one file and is probably a nest of security vulnerabilities now that I think of it
- `crash-reporter`: A crash reporter for macOS x86_64. It gives a full stacktrace at any point in the program. It does so by parsing the DWARF information embedded in the  mach-o executable format.
- `clone-gitlab-api`: A small CLI that downloads all projects from a Gitlab instance. Especially useful since my employer at the time had all projects hosted on a private Gitlab instance and there was no search feature. So I figured that the easiest way is to fetch the list of all projects, clone them locally and use grep on them. I worked great! This is conceptually `curl` + `parallel` + `git clone/pull`. I additionally wrote an equivalent [Rust version](https://github.com/gaultier/gitlab-clone-all). I also later wrote [something similar](https://github.com/gaultier/gitlab-events) to poll Gitlab for notifications to watch repository changes. Yeah Gitlab is/was pretty limited in functionality.

---

[micro-kotlin](https://github.com/gaultier/micro-kotlin): The third (and last?) take on a Kotlin compiler, in C. It goes much further than the two previous attempts both in terms of language support and in terms of implementation:
- Expressions, statements, control flow, and functions (including recursion) are implemented
- It implements Stack Map Frames contrary the [kotlin-rs](https://github.com/gaultier/kotlin-rs) so the latest JVM versions work seamlessly with it and bytecode generated by the compiler is verified at load time by the JVM
- It implements type constraints in the type checker to infer which function the user intended to call (and the rules in Kotlin for that are so very complex). That's one of the thorniest topics in Kotlin due to the language trying to have every programming language feature under the sun, namely: type inference, function overloading, default parameters, generics, implicit callers (`it` and such), variadic functions, object inheritance with method overriding, etc.
- It explores the class path so that external libraries can be use including the Java and Kotlin standard libraries
- Some Java/Kotlin interop is supported (mostly, calling Java functions from Kotlin)
- It understands .jmod, .class, .jar files, even with compression
- Out-of-order definitions of functions and variables are supported
- Some support for function inlining is supported (inlining the body of a called function)
- All allocations are done with an arena allocator and there is support for a memory dump with stacktraces
- It's only 10k lines of code and the final stripped executable is 145 Kib!


I think my most noteworthy projects are compilers both because I tend to be attracted to that domain and also because small CLI tools are less interesting. Compilers for real programming languages are hard!

## CV

You can find my resume [online](https://gaultier.github.io/resume/resume)
or download it as [PDF](https://github.com/gaultier/resume/raw/master/Philippe_Gaultier_resume_en.pdf).

## License

This blog is [open-source](https://github.com/gaultier/blog)!

The content of this blog as well as the code snippets are under the [BSD-3 License](https://en.wikipedia.org/wiki/BSD_licenses) which I also usually use for all my personal projects. It's basically free for every use but you have to mention me as the original author.



## Support


If you liked an article and you want to support me, with whatever amount you want and can afford: [Paypal](https://paypal.me/philigaultier?country.x=DE&locale.x=en_US)
