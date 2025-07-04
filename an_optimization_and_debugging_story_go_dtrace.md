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
  this->duration = (timestamp - self->t)/1000000;

  @histogram["NewMigrationBox"] = lquantize(this->duration, 0, 800, 1);
}
```

Explanation: `timestamp` is an automatically defined variable that stores the current monotonic time at the nanosecond granularity. When we enter the function, we read the current timestamp and store it in a thread-local variable `t` (with the `self->t` syntax). When we exit the function, we do the same again, compute the difference in terms of milliseconds, store it as a clause-local variable (with `this->duration`), and record it in a linear histogram with a minimum of 0 and a maximum of 800 (in milliseconds).

Due to the M:N concurrency model of Go,  in the general case, multiple goroutines run on the same thread concurrently, which means the thread-local variable `self->t` gets overriden by multiple goroutines all the time, and we observe as a result some non-sensical durations (negative or very high). The DTrace histogram is a nice way to see outliers and exclude them. The real fix would be to not use thread-local variables but instead goroutine-local variables... Which does not come out of the box with DTrace.

Fortunately I later found a way to avoid this pitfall, see the [addendum](#553173937-addendum-a-goroutine-aware-d-script) at the end.

Since the tests log verbose stuff by default and I do not know how to silence them, I save the output of DTrace in a separate file `/tmp/time.txt`: `dtrace -s time.d -c ./code.test.before -o /tmp/time.txt`

We see these results and the last line shows the aggregation (I trimmed empty lines for brevity):

```
  NewMigrationBox                                   
           value  ------------- Distribution ------------- count    
             < 0 |@@@@@@                                   3        
               0 |                                         0        
               1 |                                         0        
               2 |                                         0        
               3 |                                         0        
               4 |                                         0        
               5 |                                         0        
                 [...]
             173 |                                         0        
             174 |                                         0        
             175 |                                         0        
             176 |                                         0        
             177 |@@                                       1        
             178 |                                         0        
             179 |                                         0        
             180 |@@@@@@@@                                 4        
             181 |@@@@@@@@                                 4        
             182 |@@@@                                     2        
             183 |                                         0        
             184 |                                         0        
             185 |                                         0        
             186 |                                         0        
                 [...]
          >= 800 |@@@@@@@@@@@@                             6  
```

So the duration is ~180 ms, ignoring impossible values.

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

`copyinstr` is [required](https://illumos.org/books/dtrace/chp-user.html#chp-user) because our D script runs inside the kernel but we are trying to access user-space memory. 

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

A debugger would also work in that case (command line program running on a developer workstation), but it pretty much requires recompiling with different Go build options which kills iteration times. 

Contrary to popular belief, Go is not a crazy fast compiler. It's a smart compiler that avoids compiling stuff it already compiled in the past. But if a lot of code *does* need to be recompiled, it's not *that* fast.

If you're still not convinced to use DTrace yet, let me show you its superpower. It can show you *every* function call your program does! That's sooo useful when you do not know the codebase. Let's try it, but we are only interested in calls from within `NewMigrationBox`, and when we exit `NewMigrationBox`, we should stop tracing, because each invocation will anyway be the same:

```
pid$target:code.test.before:*NewMigrationBox:entry { self->t = 1}

pid$target:code.test.before:*NewMigrationBox:return { exit(0) }

pid$target:code.test.before:sort*: /self->t != 0/ {}
```

I have written more specific probes in this script by specifying more parts of the probe (the second part of the probe is the module name, here it is the executable name), to try to reduce noise (by accidentally matching probes we do not care about) and also to help with performance (the more probes are being matched, the more the performance tanks). Since we know that the performance issue is located in the sorting part, we only need to trace that.

For example if all your company code is under some prefix, like for me, `github.com/ory`, and you want to see all calls to company code, the probe can be `pid$target::github.com?ory*:`. The only issue is that the Go stdlib code has no prefix and we want to see it as well...

The `self->t` variable is used to toggle tracing on when we enter a specific function of interest, and to toggle it off when we leave the function. Very useful to reduce noise and avoid a post-processing filtering step.

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

For the uninitiated: `fs.Walkdir` recursively traverses a directory and calls the passed function on each entry.

Aaah... We are sorting the slice of files *every time we find a new file*. That explains it. The sort has `O(n * log(n))` complexity and we turned that into `O(n² * log(n))`. That's 'very very super-linear', as the scientists call it. 

Furthermore, most sort algorithms have worst-case performance when the input is already sorted, so we are paying full price each time, essentially doing `sort(sort(sort(...)))`.


Let's confirm this finding with DTrace by printing how many elements are being sorted in `sort.Sort`. We rely on the fact that the first thing `sort.Sort` does, is to call `.Len()` on its argument:

```
pid$target::*NewMigrationBox:entry { self->t = 1}

pid$target::*NewMigrationBox:return { self->t = 0}

pid$target::sort*Len:return /self->t != 0/ {printf("%d\n", uregs[0])}
```

The variable `uregs` is an array of user-space registers and the first one contains, in a `:return` probe, the return value of the function. So here we are simply printing the length of the slice being sorted.

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

With this done, we can measure again the duration of `NewMigrationBox` (with a lower maximum value for the histogram since we know it got faster):


```
$ sudo dtrace -s ~/scratch/time.d -c './code.test.after' 

  NewMigrationBox                                   
           value  ------------- Distribution ------------- count    
             < 0 |@@@@@@@@                                 4        
               0 |                                         0        
               1 |                                         0        
               2 |                                         0        
               3 |                                         0        
               4 |                                         0        
               5 |                                         0        
               6 |                                         0        
               7 |                                         0        
               8 |                                         0        
               9 |                                         0        
              10 |                                         0        
              11 |                                         0        
              12 |@@@@@@@@@@@@@@@@@@@@                     10       
              13 |@@@@@@@@                                 4        
              14 |                                         0        
              15 |                                         0        
              16 |                                         0        
              17 |                                         0        
                 [...]
          >= 100 |@@@@                                     2

```

## Conclusion

So we went from ~180 ms to ~11ms for the problematic function, a *16x* improvement that applies to every single test in the test suite. Not too bad. And we used DTrace at every turn along the way. 

What I find fascinating is that DTrace is a *general purpose* tool. Go was never designed with DTrace in mind, and vice-versa. Our code: same thing. And still, it works, no recompilation needed. I can instrument the highest levels of the stack to the kernel with one tool. That's pretty cool!

Of course DTrace is not perfect. User friendliness is, I think, pretty ~rough~ quirky. It's an arcane power tool for people who took the time to decipher incomplete manuals. For example registers on `aarch64` are not documented, but the ones on `SPARC` are (because that's were DTrace originated from...). Fortunately I found that piece of information after some digging: the file `/usr/lib/dtrace/arm64/regs_arm64.d` on macOS.

My favorite quirk is when an error in the script leads to an error message pointing at the bytecode (like Java and others, the D script gets compiled to bytecode before being sent to the kernel). What am I supposed to do with this :) ? It took me a while as a user to even understand that bytecode was involved at all.

But it's very often a life-saver. So thank you to their creators.

Another learning for me is that super-linear algorithms will go unnoticed and seem fine for the longest time and then bite you hard years later. If the issue is not addressed, each time a new item (here, a new SQL migration) is added, things will slow down until they halt to a crawl. So if you see something, say something. Or better yet, use DTrace to diagnose the problem!

## Addendum: The sorting function was wrong

If you look at the PR you'll see that the diff is bigger than what I described.

Initially I wanted to simply remove the sorting altogether, because `fs.WalkDir` already sorts lexically all the files, in order to always process the files in the same order. However this code uses a custom sorting logic so we need to still sort at the end, after walking the directory.

So, the diff is bigger because I noticed that the sorting function had a flaw and did not abide by the requirements of the Go standard library. Have a look, I think it is pretty clear.

Interestingly, `sort.Sort` and `slices.SortFunc` have different requirements! The first one requires the sorting function to be a `transitive ordering` whereas the second one requires it to be a `strict weak ordering`. The more you know!

I encourage you, if you write a custom sorting function, to carefully read which requirements you have to comply with, and write tests that ensure that these requirements are met, lest you face subtle sorting bugs.

## Addendum: A goroutine-aware D script

At the beginning I mentioned that `self->t = timestamp` means we are storing the current timestamp in a thread-local variable. However, since in the general case, multiple goroutines run on the same thread concurrently, this variable gets overriden by multiple goroutines all the time, and we observe as a result some non-sensical durations (negative or very high). I also mentioned that the fix would be to store this variable in a *goroutine-aware* way instead.

Well, the good news is, there is a way!

Reading carefully the [Go ABI](https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md) document again, we see that on ARM64 (a.k.a AARCH64), the register `R28` stores the current goroutine. Great! That means that we can treat the value in this register as the 'goroutine id' and we can store all timestamps per goroutine. 

My approach is to store all timestamps in a global map where the key is the goroutine id and the value is the timestamp. It's conceptually the same as thread-local DTrace variables, from the docs: 

> You can think of a thread-local variable as an associative array that is implicitly indexed by a tuple that describes the thread's identity in the system.


So here goes:

```
pid$target::*NewMigrationBox:entry { 
  this->goroutine_id = uregs[R_X28];
  durations_goroutines[this->goroutine_id] = timestamp;
} 

pid$target::*NewMigrationBox:return {
  this->goroutine_id = uregs[R_X28];
  this->duration = (timestamp - durations_goroutines[this->goroutine_id]) / 1000000;

  @histogram["NewMigrationBox"] = lquantize(this->duration, 0, 100, 1);

  durations_goroutines[this->goroutine_id] = 0;
}
```

At the end, when the duration has been duly recorded in the histogram, we set the value in the map to 0 per the [documentation](https://illumos.org/books/dtrace/chp-variables.html#chp-variables-2):

> Assigning an associative array element to zero causes DTrace to deallocate the underlying storage. This behavior is important because the dynamic variable space out of which associative array elements are allocated is finite; if it is exhausted when an allocation is attempted, the allocation will fail and an error message will be generated indicating a dynamic variable drop. Always assign zero to associative array elements that are no longer in use.


When we run it, we see that all durations are now nice and correct:

```
  NewMigrationBox                                   
           value  ------------- Distribution ------------- count    
              11 |                                         0        
              12 |@@@@@@@@@@@@@@@@@@@@@@                   11       
              13 |@@@@@@@@@@@@@@                           7        
              14 |@@@@                                     2        
              15 |                                         0      
```

This approach is:

- Safe: reading and writing to global variables, including global maps, is thread-safe by design in DTrace.
- Correct: because the (current) Go garbage collector is **non-moving**, and because goroutine pointers are handled specially by the Go runtime, it means in practice that between our two probes (the entry and the exit of the function being traced):
  + The 'goroutine id', which is a pointer to the current goroutine, cannot change. 
  + Another goroutine cannot take the place of the current goroutine pointed to by the goroutine pointer we read.

However, you should know that if our function body panics between the entry and return probes, and the panic is not recovered, the return probe will not fire, because of the way Go implements panics: the program simply exits the OS process in the middle of the function.

In conclusion, if you are using DTrace with Go, I would encourage you to use this trick. Note that the right register to use differs per architecture: `R14` on AMD64, `R28` on ARM64, etc.
