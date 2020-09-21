<link rel="stylesheet" type="text/css" href="main.css">
<a href="/blog">All articles</a>

# Adventures in CI land, or how to speed up your CI

Every project has a Continuous Integration (CI) pipeline and every one of them complains its CI is too slow. It is more important than you might think; this can be the root cause of many problems, including lackluster productivity, low morale, high barrier of entry for newcomers, and overall suboptimal quality.

But this need not be. I have compiled here a lengthy list of various ways you can simplify your CI and make it faster, based on my experience on open-source projects and my work experience. You will definitely find something in here worth your time.

And finally, I hope you will realize this endeavour is not unlike optimizing a program: it requires some time and dedication but you will get tremendous results.

*This article assumes you are running a POSIX system. Windows developers, this is not the article you are looking for.*

## Reduce the size of everything

Almost certainly, your CI pipeline has to download 'something', be it a base docker image, a virtual machine image, some packages, maybe some company wide scripts. The thing is, you are downloading those every time it runs, every day of the year. Even a small size reduction can yield big speed ups. Remember, the network is usually the bottleneck. 

In no particular order: 

- Axe duplicate tools. `curl` and `wget` are equivalent, given the right command line options. Settle on using only one and stick to it. All my pipelines use: `curl --sSL --retry 5 ...`. You can customize further, but that's the gist of it. Other examples: `make` and `ninja`, `gcc` and `clang`, etc.
- Use POSIX tools. They are already present on whatever system you are using. When simply checking that a tool or an API returned 'OK', simply use `grep` and `awk`, no need for `ripgrep`. Prefer `sh` over `bash` for simple scripts, `make` over `rake` for builds, etc. It's most likely faster, more stable, and more documented, too. 
- Pay attention to the base image you are using. Prefer a small image where you install only what you need. I have seen docker base images over 1 Gb big. You will spend more time downloading it, uncompressing it, and checksumming it, than running your pipeline. Alpine Linux is great. Ubuntu is ok. When in doubt, inspect the content of the image. Look for stuff that should not be here. E.g: `X11`, man pages, etc.
- Don't install documentation. It's obvious but most people do it. While you are at it, don't install `man`, `apropos`, `info`, etc. Alpine Linux gets it right by splitting almost all packages between the package itself and its documentation. E.g: 


