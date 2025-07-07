Title: Adventures in CI land, or how to speed up your CI
Tags: CI, Optimization
---

Every project has a Continuous Integration (CI) pipeline and every one of them complains its CI is too slow. It is more important than you might think; this can be the root cause of many problems, including lackluster productivity, low morale, high barrier of entry for newcomers, and overall suboptimal quality.

But this need not be. I have compiled here a lengthy list of various ways you can simplify your CI and make it faster, based on my experience on open-source projects and my work experience. I sure wish you will find something in here worth your time.

And finally, I hope you will realize this endeavour is not unlike optimizing a program: it requires some time and dedication but you will get tremendous results. Also, almost incidentally, it will be more secure and easier to audit.

Lastly, remember to measure and profile your changes. If a change has made no improvements, it should be reverted.

*This article assumes you are running a POSIX system. Windows developers, this is not the article you are looking for.*


## Reduce the size of everything

Almost certainly, your CI pipeline has to download 'something', be it a base docker image, a virtual machine image, some packages, maybe a few company wide scripts. The thing is, you are downloading those every time it runs, 24/7, every day of the year. Even a small size reduction can yield big speed ups. Remember, the network is usually the bottleneck. 

In no particular order: 

- Only fetch required git objects. That means running `git clone my-repo.git --depth 1 --branch shiny-feature`, instead of cloning the whole repository every time, along with every branch and that one class file that your coworker accidentally committed once.
- Axe duplicate tools. `curl` and `wget` are equivalent, given the right command line options. Settle on using only one and stick to it. All my pipelines use: `curl --sSL --retry 5`. You can customize further, but that's the gist of it. Other examples: `make` and `ninja`, `gcc` and `clang`, etc.
- Use POSIX tools. They are already present on whatever system you are using. When purely checking that a tool or an API returned 'OK', simply use `grep` and `awk`, no need for `ripgrep`. Prefer `sh` over `bash` for simple scripts, `make` over `rake` for builds, etc. It's most likely faster, more stable, and more documented, too. 
- Pay attention to the base image you are using. Prefer a small image where you install only what you need. I have seen docker base images over 1 Gb big. You will spend more time downloading it, uncompressing it, and checksumming it, than running your pipeline. Alpine Linux is great. Debian and Ubuntu are fine. When in doubt, inspect the content of the image. Look for stuff that should not be here. E.g.: `X11`, man pages, etc.
- Don't install documentation. It's obvious but most people do it. While you are at it, don't install `man`, `apropos`, `info`, etc. Alpine Linux gets it right by splitting almost all packages between the package itself and its documentation. E.g.: `cmake` and `cmake-doc`.
- On the same vein: don't install shell autocompletions. Same idea. Again, on Alpine they are not part of the main package. E.g.: `cmake` and `cmake-bash-completion`.
- Stay away from aggregate packages (or meta-packages)! Those are for convenience only when developing. E.g.: `build-base` on Alpine is a meta-package gathering `make`, `file`, `gcc`, etc. It will bring lots of things you do not need. Cherry-pick only what you really required and steer clear of those packages.
- Learn how Docker image layers work: avoid doing `RUN rm archive.tar`, since it simply creates a new layer without removing the file from the previous layer. Prefer: `RUN curl -sSL --retry 5 foo.com/archive.tar && tar -xf archive.tar && rm archive.tar` which will not add the tar archive to the Docker image.
- Use multi-stage Docker builds. It is old advice at this point but it bears repeating.
- When using multi-stage: Only copy files you need from a previous stage instead of globbing wildly, thus defeating the purpose of multi-stages.
- Tell apart the development and the release variant of a package. For example: on Ubuntu, when using the SDL2 library, it comes in two flavors: `libsdl2-dev` and `libsdl2-2.0`. The former is the development variant which you only need when building code that needs the headers and the libraries of the SDL2, while the latter is only useful with software needing the dynamic libraries at runtime. The development packages are usually bigger in size. You can astutely use multi-stage Docker builds to have first a build stage using the development packages, and then a final stage which only has the non-development packages. In CI, you almost never need both variants installed at the same time.
- Opt-out of 'recommended' packages. Aptitude on Debian/Ubuntu is the culprit here: `apt-get install foo` will install much more than `foo`. It will also install recommended packages that most of the time are completely unrelated. Always use `apt-get install --no-install-recommends foo`.
- Don't create unnecessary files: you can use heredoc and shell pipelines to avoid creating intermediary files.


## Be lazy: Don't do things you don't need to do

- Some features you are not using are enabled by default. Be explicit instead of relying on obscure, ever changing defaults. Example: `CGO_ENABLED=0 go build ...` because it is (at the time of writing) enabled by default. The Gradle build system also has the annoying habit to run stuff behind your back. Use `gradle foo -x baz` to run `foo` and not `baz`.
- Don't run tests from your dependencies. This can happen if you are using git submodules or vendoring dependencies in some way. You generally always want to build them, but not run their tests. Again, `gradle` is the culprit here. If you are storing your git submodules in a `submodules/` directory for example, you can run only your project tests with: `gradle test -x submodules:test`.
- Disable the generation of reports files. They frequently come in the form of HTML or XML form, and once again, `gradle` gets out of his way to clutter your filesystem with those. Of debatable usefulness locally, they are downright wasteful in CI. And it takes some precious time, too! Disable it with: 
    ```kotlin
     tasks.withType<Test> {
         useJUnitPlatform()
         reports.html.isEnabled = false
         reports.junitXml.isEnabled = false
     }
     ```
- Check alternative repositories for a dependency instead of building it from source. It can happen that a certain dependency you need is not in the main repositories of the package manager of your system. You can however inspect other repositories before falling back to building it yourself. On Alpine, you can simply add the URL of the repository to `/etc/apk/repositories`. For example, in the main Alpine Docker image, the repository `https://<mirror-server>/alpine/edge/testing` is not enabled. More information [here](https://wiki.alpinelinux.org/wiki/Enable_Community_Repository). Other example: on OpenBSD or FreeBSD, you can opt-in to use the `current` branch to get the newest and latest changes, and along them the newest dependencies.
- Don't build the static and dynamic variants of the same library (in C or C++). You probably only want one, preferably the static one. Otherwise, you are doing twice the work!
- Fetch statically built binaries instead of building them from source. Go, and sometimes Rust, are great for this. As long as the OS and the architecture are the same, of course. E.g.: you can simply fetch `kubectl` which is a Go static binary instead of installing lots of Kubernetes packages, if you simply need to talk to a Kubernetes cluster. Naturally, the same goes for single file, dependency-less script: shell, awk, python, lua, perl, and ruby, assuming the interpreter is the right one. But this case is rarer and you might as well vendor the script at this point.
- Groom your 'ignore' files. `.gitignore` is the mainstream one, but were you aware Docker has the mechanism in the form of a `.dockerignore` file? My advice: whitelist the files you need, e.g.:
    ```text
    **/*
    !**/*.js
    ```

  This can have a huge impact on performance since Docker will copy all the files inside the Docker context directory inside the container (or virtual machine on macOS) and it can be a lot. You don't want to copy build artifacts, images, and so on each time which your image does not need.
- Use an empty Docker context if possible: you sometimes want to build an image which does not need any local files. In that case you can completely bypass copying any files into the image with the command: `docker build . -f - < Dockerfile`.
- Don't update the package manager cache: you typically need to start your Dockerfile by updating the package manager cache, otherwise it will complain the dependencies you want to install are not found. E.g.: `RUN apk update && apk add curl`. But did you know it is not always required? You can simply do: `RUN apk --no-cache add curl` when you know the package exists and you can bypass the cache.
- Silence the tools: most command line applications accept the `-q` flag which reduces their verbosity. Most of their output is likely to be useless, some CI systems will struggle storing big pipeline logs, and you might be bottlenecked on stdout! Also, it will simplify troubleshooting *your* build if it is not swamped in thousands of unrelated logs.


## Miscellenaous tricks

- Use `sed` to quickly edit big files in place. E.g.: you want to insert a line at the top of a Javascript file to skip linter warnings. Instead of doing: 
    ```shell
    $ printf '/* eslint-disable */\n\n' | cat - foo.js > foo_tmp && mv foo_tmp foo.js
    ```

    which involves reading the whole file, copying it, and renaming it, we can do: 

    ```shell
    $ sed -i '1s#^#/* eslint-disable */ #' foo.js
    ```

    which is simpler.
- Favor static linking and LTO. This will simplify much of your pipeline because you'll have to deal with fewer files, ideally one statically built executable.
- Use only one CI job. That is because the startup time of a job is very high, in the order of minutes. You can achieve task parallelism with other means such as `parallel` or `make -j`.
- Parallelize all the things! Some tools do not run tasks in parallel by default, e.g. `make` and `gradle`. Make sure you are always using a CI instance with multiple cores and are passing `--parallel` to Gradle and `-j$(nproc)` to make. In rare instances you might have to tweak the exact level of parallelism to your particular task for maximum performance. Also, `parallel` is great for parallelizing tasks.
- Avoid network accesses: you should minimize the amount of things you are downloading from external sources in your CI because it is both slow and a source of flakiness. Some tools will unfortunately always try to 'call home' even if all of your dependencies are present. You should disable this behavior explicitly, e.g. with Gradle: `gradle build --offline`.
- In some rare cases, you will be bottlenecked on a slow running script. Consider using a faster interpreter: for shell scripts, there is `ash` and `dash` which are said to be much faster than `bash`. For `awk` there is `gawk` and `mawk`. For Lua there is `LuaJIT`.
- Avoid building inside Docker if you can. Building locally, and then copying the artifacts into the image, is always faster. It only works under certain constraints, of course:
    * same OS and architecture, or
    * a portable artifact format such as `jar`, and not using native dependencies, or
    * your toolchain supports cross-compilation

## A note on security

- Always use https
- Checksum files you fetched from third-parties with `shasum`.
- Favor official package repositories, docker images, and third-parties over those of individuals.
- Never bypass certificate checks (such as `curl -k`)

## I am a DevOps Engineer, what can I do?

Most of the above rules can be automated with a script, assuming the definition of a CI pipeline is in a text format (e.g. Gitlab CI). I would suggest starting here, and teaching developers about these simple tips than really make a difference.

I would also suggest considering adding strict firewall rules inside CI pipelines, and making sure the setup/teardown of CI runners is very fast. Additionally, I would do everything to avoid a situation where no CI runner is available, preventing developers from working and deploying.

Finally, I would recommend leading by example with the pipelines for the tools made by DevOps Engineers in your organization.

## Closing words

I wish you well on your journey towards a fast, reliable and simple CI pipeline. 

I noticed in my numerous projects with different tech stacks that some are friendlier than others towards CI pipelines than others (I am looking at you, Gradle!). If you have the luxury of choosing your technical stack, do consider how it will play out with your pipeline. I believe this is a much more important factor than discussing whether $LANG has semicolons or not because I am convinced it can completely decide the outcome of your project.

