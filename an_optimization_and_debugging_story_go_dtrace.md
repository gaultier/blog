Title: An optimization and debugging story with Go and DTrace
Tags: Go, Optimization, DTrace
---

Today at work, I was hacking on [Kratos](https://github.com/ory/kratos) (a Go application), and I noticed running the tests took a bit too long to my liking. So I profiled the tests, and unknowingly embarked on a fun optimization and debugging adventure. I thought it could be interesting and perhaps educational.

I just started this job, not two months ago. I want to show methods that make it feasible to understand and diagnose a big body of software you don't know, where you did not write any of it, but you still have to fix it.

If you want you can jump to the [PR](https://github.com/ory/x/pull/872) directly.

Oh, and if you always dreamt of playing the 'DTrace drinking game' with your friends, where you have to drink each time 'DTrace' gets mentioned, oh boy do I have something for you here. 


## Setting the stage

The nice thing when you work on an open-source project for work is that it's easy to write blog posts about it, and it's easy for readers to reproduce it! And when I'm done, it benefits the community. I like it.

So, [Kratos](https://github.com/ory/kratos) uses a database. Each schema change is done with a SQL migration file, like `add_some_column.up.sql` and its counterpart `add_some_column.down.sql`. 

Since each test is independent, each test creates a new database, collects all migrations, and applies them, before doing its thing. 

Now, there are a number of things we could do to speed things up, like only collect migrations once at startup, or merge them all in one (i.e. a snapshot). But that's how things are right now. And there are 10 years worth of SQL migrations piled up in the project.

## Profiling

When I profile the test suite (the profiler collects the raw data behind the scenes using [DTrace](https://illumos.org/books/dtrace/preface.html#preface)), I notice some weird things:

<object alt="CPU Profile" data="popx_profile.svg" type="image/svg+xml"></object>

- Pretty much all of the time (97%) in the test is spent in `NewMigrationBox` which applies SQL migrations. Strange.
- Pretty much all of the time (90+%) in `NewMigrationBox` is spent sorting. Maybe it is fine, but still surprising and worth investigating.

## Get a precise timing

The CPU profile unfortunately does not show how much time is spent exactly in `NewMigrationBox`. At this point, I also do not know how many SQL files are present. If there are a bazillion SQL migrations, maybe it's expected that sorting them indeed takes the most time. 

Let's find out with [DTrace](https://illumos.org/books/dtrace/preface.html#preface) how long the function really runs. 

### DTrace in 2 minutes

DTrace is a 'dynamic instrumentation tracing framework [...] to concisely answer arbitrary questions about the behavior of the operating system and user programs'. Let's unpack:

- 'Tracing' means that it can show which functions run, what is the value of their arguments, etc, at runtime.
- 'Instrumentation' means that it inserts code in programs of interest to see into what they are doing. When DTrace is not in use, there is no negative effect on performance. Thus, it can instrument programs that do not even know what DTrace is. Consequently, there is no need to recompile programs in a certain way, or even have debug information present, or even have the source code.
- 'Dynamic' means that it can be turned on and off at will, or with conditionals based on whatever you want, like the time of day, after a specific file gets opened on the system, etc. That's done by writing a script in a custom language (D).
    That's the core idea behind DTrace: it exists to answer questions *we did not know we would need to answer*. Ahead of time tracing is great and all, but the reality is that there will be plenty of cases where this is not sufficient.
- 'Framework' means that it is general purpose, with the D language, and also that it's more of a toolbox than a user-friendly product. You typically do not call a ready-made command to answer a question, you write a small custom script. Although, lots of reusable scripts are available on Github, and I suppose LLMs can help nowadays.
- 'Answer questions': DTrace is not something that is on all the time like logs or OpenTelemetry traces (all of these work well together and are not exclusive). It's more of a detective tool to investigate, akin to the forensic police on a crime scene.
- 'Operating system and user programs': That's the big advantage of DTrace, it can equally look into a user-space binary program, a program running in a virtual machine (like a Java or JavaScript program), the virtual machine itself (JVM, V8, etc), and the kernel, *all in one script*. That's really important when you are looking for a bug that could be at any level of the stack, in a complex program that perhaps does things in multiple programming languages: a web browser, an OS, a database, etc.

What should also be added is that DTrace comes with the system on macOS (requires disabling System Integrity), which is where I did this whole investigation, FreeBSD, Illumos, Windows, etc.

The advantage over a debugger is that DTrace does not stop the program. When inspecting a network heavy program, like a web server or a database, or a data race, or a scheduling bug, in production, this property is very important. Also, it can inspect the whole system, not just one single program: we can observe all writes happening on the system right now, or all files opened, etc. Sometimes this is a crucial property to have.

The disadvantage is that it is less granular: it cannot (without much effort and assembly knowledge) inspect local variables, inlined functions, etc. However DTrace also supports static tracing where we define traces in our source code for these cases, assuming we know ahead of time that we need this information.


### Timing a function

Back to the problem at hand.

We check if the function `NewMigrationBox` is visible to DTrace by listing (`-l`) all probes matching the pattern `*ory*` in the executable `code.test.before` (i.e. before the fix):

```sh
$ sudo dtrace -n 'pid$target:code.test.before:*ory*: ' -c ./code.test.before -l | grep NewMigrationBox
209591   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox return
209592   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox entry
209593   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 return
209594   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 entry
[...]
```

Ok, the first two are the ones we need. Let's time the function duration then with a D script `time.d`:

```
pid$target::*NewMigrationBox:entry { self->t=timestamp } 

pid$target::*NewMigrationBox:return {
  self->duration = (timestamp - self->t) / 1000000;

  if (self->duration < 1000) {
    printf("NewMigrationBox:%d\n", self->duration);

    @durations["NewMigrationBox"] = avg(self->duration);
  }
  self->duration = 0;
  self->t = 0;
}

```

> Explanation: `timestamp` is an automatically defined variable that stores the current monotonic time at the nanosecond granularity. When we enter the function, we read the current timestamp and store it in a thread-local variable `t` (with the `self->t` syntax). When we exit the function, we do the same again, compute the difference in terms of milliseconds, and print that.
> 
> Due to (I think) the M:N concurrency model of Go, sometimes the function starts running on one OS thread, yields back to the scheduler (due for example to doing some I/O), and gets moved to a different OS thread where it continues running. That, and the fact that the Go tests apparently spawn subprocesses, makes our calculations in this simple script fragile. So when we see an outlandish duration, that we know is not possible, we simply discard it.

The nice thing with DTrace is that it can also do aggregations (with `@`), so we compute the average of all durations with `avg()`. DTrace can also show histograms etc, but no need here (we see that all durations are pretty much identical, no outliers). Aggregations get printed at the end automatically.

Since the tests log verbose stuff by default and I do not know how to silence them, I save the output of DTrace in a separate file `/tmp/time.txt`: `dtrace -s time.d -c ./code.test.before -o /tmp/time.txt`

We see these results and the last line shows the aggregation (average):

```
CPU     ID                    FUNCTION:NAME
  4 130841 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

  9 130841 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:185

 13 130841 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:181

 [...]


  NewMigrationBox                                                 181
```

So the duration is ~180 ms.

## Establishing a baseline

When doing some (light) optimization work, it is crucial to establish a baseline. What is the ideal time? How much do we differ from this time? And to establish this baseline, it is very important to understand what kind of work we are doing:

- Are we doing any I/O at all or is the workload purely in-memory?
- How much work is there? How many items are we handling? Is it a handful, a few thousands, a few millions?

So let's see what the baseline is when simply finding all the SQL files on disk:

```sh
hyperfine --shell=none --warmup=5 "find ./persistence/sql/migrations -name '*.sql' -type f"
Benchmark 1: find ./persistence/sql/migrations -name '*.sql' -type f
  Time (mean ± σ):     206.6 ms ±   4.8 ms    [User: 2.4 ms, System: 5.0 ms]
  Range (min … max):   199.5 ms … 214.4 ms    14 runs
```

Ok, so 200ms, which is pretty close to our 180ms in Go...

Wait a minute... Are we doing any I/O in Go at all? Could it be that we *embed* all the SQL files in our binary at build time ?!

Let's print with DTrace the files that are being opened, whose extension is `.sql`:

```
syscall::open:entry { 
  self->filename = copyinstr(arg0);

  if (rindex(self->filename, ".sql") == strlen(self->filename)-4) {
    printf("%s\n", self->filename)
  }
} 
```

> `copyinstr` is [required](https://illumos.org/books/dtrace/chp-user.html#chp-user) because our D script runs inside the kernel but we are trying to access user-space memory. 

When we run this script on `go test -c` which builds the text executable, we see that all the SQL files are being opened by the Go compiler and subsequently embedded in the test binary:

```sh
$ sudo dtrace -s ~/scratch/popx_opens.dtrace -c 'go test -tags=sqlite -c'

CPU     ID                    FUNCTION:NAME
 10    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.cockroach.down.sql

  5    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.postgres.down.sql

[...]
```

And if we run the same script on the test binary, we see that no SQL files on disk are being opened.

Damn, so we are doing purely in-memory work. Alright, so is 180 ms still good in that light? How many files are we dealing with? According to `find`, ~1.6k. So, you're saying we are spending 180 ms to sort a measly 1.6k items in a linear array? That's *nothing* for a modern computer! It should be a small number of milliseconds!

## What the hell is my program even doing?

If you're like me, you're always asking yourself: what is taking my computer so damn long? Why is Microsoft Teams hanging again? (thankfully I do not have to use it anymore, thank God). Why is Slack taking 15s to simply start up? 

The problem with computer programs is that there are a black box. You normally have no idea what they are doing. If you're lucky they will print some stuff. If you're lucky.

If only there was a tool that shows me precisely what the hell my program is doing... And I could dynamically choose what to show and what to hide to avoid noise... Oh wait this has been existing for 20 years. DTrace of course!

If you're still not convinced to use DTrace yet, let me show you its superpower. It can show you *every* function call your program does! That's sooo useful when you do not know the codebase. Let's try it, but we are only interested in calls from within `NewMigrationBox`, and when we exit `NewMigrationBox`, we should stop tracing, because each invocation will anyway be the same:

```
pid$target:code.test.before:*NewMigrationBox:entry { self->t = 1}

pid$target:code.test.before:*NewMigrationBox:return { exit(0) }

pid$target:code.test.before:sort*: /self->t != 0/ {}
```

> I have written more specific probes in this script by specifying more parts of the probe, to try to reduce noise (by accidentally matching probes we do not care about) and also to help with performance (the more probes are being matched, the more the performance tanks). Since we know that the performance issue is located in the sorting part, we only need to trace that.
>
> For example if all your company code is under some prefix, like for me, `github.com/ory`, and you want to see all calls to company code, the probe can be `pid$target::github.com?ory*:`. The only issue is that the Go stdlib code has no prefix...
> 
> The `self->t` variable is used to toggle tracing on when we enter a specific function of interest, and to toggle it off when we leave the function. Very useful to reduce noise and avoid a post-processing filtering step.

So, let's run our script with the `-F` option to get a nicely formatted output:

```
$ sudo dtrace -s ~/scratch/popx_trace_calls.dtrace -c './code.test.before' -F

CPU FUNCTION                                 
  7  -> sort.Sort                             
  7    -> sort.pdqsort                        
  7      -> sort.insertionSort                
  7      <- sort.insertionSort                
  7    <- sort.Sort                           
  7    -> sort.Sort                           
  7      -> sort.pdqsort                      
  7        -> sort.insertionSort              
  7        <- sort.insertionSort              
  7      <- sort.Sort                         
  7      -> sort.Sort                         
  7        -> sort.pdqsort                    
  7          -> sort.choosePivot              
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7          <- sort.choosePivot              
  7          -> sort.partialInsertionSort     
  7          <- sort.partialInsertionSort 
[...]
```

The `->` arrow means we enter the function, and `<-` means we exit it. We see a nice call tree. Also I notice something weird (looking at the full output which is huge): we are calling `sort.Sort` way too much. It should be once, but we call it much more than that. 

That's the canary in the coal mine and it matches what we see on the CPU profile from the beginning.


## It's always DNS - wait no, it's always: superlinear algorithmic complexity

Time to inspect the code in `NewMigrationBox`, finally. Pretty quickly I stumble upon code like this (I simplified a bit - you can find the original code in the PR):

```go
fs.WalkDir(fm.Dir, ".", func(p string, info fs.DirEntry, err error) error {
    migrations = append(migrations, migration)
    mod := sort.Interface(migrations)
    sort.Sort(mod)

    return nil
}
```

> For the uninitiated: `fs.Walkdir` recursively traverses a directory and calls the passed function on each entry.

Aaah... We are sorting the slice of files *every time we find a new file*. That explains it. The sort has `O(n*log(n))` complexity and we turned that into `O(n²*log(n))`. That's 'very very super-linear', as the scientists call it. 

Furthermore, most sort algorithms have worst-case performance when the input is already sorted, so we are paying full price each time, essentially doing `sort(sort(sort(...)))`.


Let's confirm this finding with DTrace by printing how many elements are being sorted in `sort.Sort`. We rely on the fact that the first thing `sort.Sort` does, is to call `.Len()` on its argument:

```
pid$target::*NewMigrationBox:entry { self->t = 1}

pid$target::*NewMigrationBox:return { self->t = 0}

pid$target:code.test.before:sort*Len:return /self->t != 0/ {printf("%d\n", uregs[R_R0])}
```

> The register `R_R0` contains the return value on `aarch64` and `amd64` per the [Go ABI](https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md). And `uregs` is an array containing user-space registers. So here we are simply printing the return value of the function.

We see:

```
CPU     ID                    FUNCTION:NAME
  5  52085       sort.(*reverse).Len:return 1

  5  52085       sort.(*reverse).Len:return 2

  5  52085       sort.(*reverse).Len:return 3

  5  52085       sort.(*reverse).Len:return 4

  5  52085       sort.(*reverse).Len:return 5

  [...]

 11  52085       sort.(*reverse).Len:return 1690

 11  52085       sort.(*reverse).Len:return 1691

 11  52085       sort.(*reverse).Len:return 1692

 11  52085       sort.(*reverse).Len:return 1693
```

So it's confirmed.

Remember when I said at the beginning:

>  That's the core idea behind DTrace: it exists to answer questions *we did not know we would need to answer*.

This is a perfect example: we would never add an OpenTelemetry trace or a log to the `.Len()` function ahead of time - that would be too costly and almost never useful. But DTrace can dynamically instrument this function when we need it.

The fix is easy: collect all files into the slice and then sort them once at the end:

```
fs.WalkDir(fm.Dir, ".", func(p string, info fs.DirEntry, err error) error {
    migrations = append(migrations, migrations)
    return nil
}

mod := sort.Interface(migrations)
sort.Sort(mod)
```

The real fix uses `slices.SortFunc` instead of `sort.Sort` because the official docs mention the performance of the former is better than the latter. Likely because `slices.SortFunc` uses compile-time generics whereas `sort.Sort` uses runtime interfaces. And also we see that the Go compiler inlines the call to `slices.SortFunc`, which probably helps further.

With this done, we can measure again the duration of `NewMigrationBox`:


```
$ sudo dtrace -s ~/scratch/time.d -c './code.test.after' 
CPU     ID                    FUNCTION:NAME
 12  62559 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:12

 11  62559 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:11

 13  62559 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:12

 [...]


  NewMigrationBox                                                  11

```

## Conclusion

So we went from ~180 ms to ~11ms for the problematic function, a *16x* improvement that applies to every single test in the test suite. Not too bad. And we used DTrace at every turn along the way. 

What I find fascinating is that DTrace is a *general purpose* tool. Go was never designed with DTrace in mind, and vice-versa. Our code: same thing. And still, it works, no recompilation needed. I can instrument the highest levels of the stack to the kernel with one tool. That's pretty cool!

Of course DTrace is not perfect. User friendliness is, I think, pretty ~abysmal~ ~rough~ quirky. It's an arcane power tool for people who took the time to decipher incomplete manuals. For example registers on `aarch64` are not documented, but the ones on `SPARC` are (because that's were DTrace originated from...). My favorite quirk is when an error in the script leads to an error message pointing at the bytecode (like Java and others, the D script gets compiled to bytecode before being sent to the kernel). What am I supposed to do with this :) ?

But it's very often a life-saver. So thank you to their creators.

Another learning for me is that super-linear algorithms will go unnoticed and seem fine for the longest time and then bite you hard years later. If the issue is not addressed, each time a new item (here, a new SQL migration) is added, things will slow down until they halt to a crawl. So if you see something, say something. Or better yet, use DTrace to diagnose the problem!

## Addendum: The sorting function was wrong

If you look at the PR you'll see that the diff is bigger than what I described.

Initially I wanted to simply remove the sorting altogether, because `fs.WalkDir` already sorts lexically all the files, in order to always process the files in the same order. However this code uses a custom sorting logic so we need to still sort at the end, after walking the directory.

So, the diff is bigger because I noticed that the sorting function had a flaw and did not abide by the requirements of the Go standard library. Have a look, I think it is pretty clear.

Interestingly, `sort.Sort` and `slices.SortFunc` have different requirements! The first one requires the sorting function to be a `transitive ordering` whereas the second one requires it to be a `strict weak ordering`. The more you know!

I encourage you, if you write a custom sorting function, to carefully read which requirements you have to comply with, and write tests that ensure that these requirements are met, lest you face subtle sorting bugs.
