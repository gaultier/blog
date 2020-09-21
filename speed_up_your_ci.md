<link rel="stylesheet" type="text/css" href="main.css">
<a href="/blog">All articles</a>

# Adventures in CI land, or how to speed up your CI

Every project has a Continuous Integration (CI) pipeline and every one of them complains its CI is too slow. It is more important than you might think; this can be the root cause of many problems, including lackluster productivity, low morale, high barrier of entry for newcomers, and overall suboptimal quality.

But this need not be. I have compiled here a lengthy list of various ways you can simplify your CI and make it faster, based on my experience on open-source projects and my work experience. You will definitely find something in here worth your time.

And finally, I hope you will realize this endeavour is not unlike optimizing a program: it requires some time and dedication but you will get tremendous results. Also, almost incidentally, it will be more secure and easier to audit.

*This article assumes you are running a POSIX system. Windows developers, this is not the article you are looking for.*

## Reduce the size of everything

Almost certainly, your CI pipeline has to download 'something', be it a base docker image, a virtual machine image, some packages, maybe some company wide scripts. The thing is, you are downloading those every time it runs, every day of the year. Even a small size reduction can yield big speed ups. Remember, the network is usually the bottleneck. 

In no particular order: 

- Only fetch required git objects. That means running `git clone my-repo.git --depth 1 --branch shiny-feature`, instead of cloning the whole repository every time, along with every branch and that one class file that your cowoker accidentally committed once.
- Axe duplicate tools. `curl` and `wget` are equivalent, given the right command line options. Settle on using only one and stick to it. All my pipelines use: `curl --sSL --retry 5 ...`. You can customize further, but that's the gist of it. Other examples: `make` and `ninja`, `gcc` and `clang`, etc.
- Use POSIX tools. They are already present on whatever system you are using. When simply checking that a tool or an API returned 'OK', simply use `grep` and `awk`, no need for `ripgrep`. Prefer `sh` over `bash` for simple scripts, `make` over `rake` for builds, etc. It's most likely faster, more stable, and more documented, too. 
- Pay attention to the base image you are using. Prefer a small image where you install only what you need. I have seen docker base images over 1 Gb big. You will spend more time downloading it, uncompressing it, and checksumming it, than running your pipeline. Alpine Linux is great. Ubuntu is ok. When in doubt, inspect the content of the image. Look for stuff that should not be here. E.g: `X11`, man pages, etc.
- Don't install documentation. It's obvious but most people do it. While you are at it, don't install `man`, `apropos`, `info`, etc. Alpine Linux gets it right by splitting almost all packages between the package itself and its documentation. E.g: `cmake` and `cmake-doc`.
- On the same vein: don't install shell autocompletions. Same thing. Again, on Alpine they are not part of the main package. E.g: `cmake` and `cmake-bash-completion`.
- Don't install aggregate packages (or meta-packages)! Those are for convenience only when developping. E.g: `build-base` on Alpine is a meta-package gathering `make`, `file`, `gcc`, etc. It will bring lots of things you do not need. Cherry-pick only what you really need and stay away from those packages.
- Learn how Docker image layers work: avoid doing `RUN rm archive.tar`, since it simply creates a new layer without removing the file from the previous layer. Prefer: `RUN curl -sSL --retry 5 foo.com/archive.tar && tar -xf archive.tar && rm archive.tar` which will not add the tar archive to the Docker image.
- Use multi-stage Docker builds. It is old advice at this point but it bears repeating.
- Only copy files you need from a previous stage instead of globbing wildly, thus defeating the purpose of multi-stages.
- Distinguish between the development and the release variant of packages. For example: on Ubuntu, when using the SDL2 library, it comes in two flavors: `libsdl2-dev` and `libsdl2-2.0`. The former is the development variant which you only need when building code that needs the headers and the libraries of the SDL2, while the latter is only useful with software needing the dynamic libraries at runtime. The development packages are usually bigger in size. You can astutely use multi-stage Docker builds to have first a build stage using the development packages, and then a final stage which only has the non-development packages. In CI, you almost never need both variants installed at the same time.


## Be lazy: Don't do things you don't need to do

- Some features you are not using are enabled by default. Be explicit instead of relying on obscure, ever changing defaults. Example: `CGO_ENABLED=0 go build ...` because it is (at the time of writing) enabled by default. The gradle build system also has the annoying habit to run stuff behind your back. Use `gradle foo -x baz` to run `foo` and not `baz`.
- Don't run tests from your dependencies. This can happen if you are using git submodules or vendoring dependencies in some way. You usually always want to build them, but not run the tests for them. Again, `gradle` is the culprit here. If you are storing your git submodules in a `submodules/` directory for example, you can run only your project tests with: `gradle test -x submodules:test`.
- 



