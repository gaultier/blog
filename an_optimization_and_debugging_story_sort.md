Title: An optimization and debugging story (your sort might be wrong)
Tags: Go, Optimization, Dtrace
---

Today at work, I was hacking on [Kratos](https://github.com/ory/kratos) (a Go application), and I noticed running the tests took a bit too long to my liking. So I profiled the tests, and unknowingly embarked on a fun optimization and debugging adventure. I thought it could be interesting and perhaps educational. I just started this job not two months ago. I want to show methods that make it feasible to understand and diagnose a big body of software you don't know.

If you want you can jump to the [PR](https://github.com/ory/x/pull/872) directly.


## Setting the stage

The nice thing when you work on an open-source project for work is that it's easy to write blog posts about it, and it's easy for readers to reproduce it! And when I'm done, it benefits the community. I like it.

Anyways, the nice thing about Kratos is that it supports many databases (which absolutely does not make my life hard at all when making a schema change :) ), and we can simply use sqlite when running tests locally. SQLite is simple and great. We can even use a in-memory SQLite for tests for speed. Cool. Each schema change is done with a SQL migration file, like `add_some_column.up.sql` and its counterpart `add_some_column.down.sql`. 

Since each test is independent, each test collects all migrations and applies them, before doing its thing. 

Now, there are a number of things we could do to speed things up, like only collect migrations once at startup, or merge them all in one (i.e. a snapshot). But that's how things are right now. And there are 10 years worth of SQL migrations piled up in the project.

## Profiling

When I profile the test suite, I notice some weird things:

![CPU profile of the test suite](x_popx_profile.png)

- The profile shows a few big clumps (in yellow) that are the function `findMigrations` whereas the rest of the profile is pretty uneventful.
- Pretty much all of the time (95%) in `findMigration` is spenting sorting. Perhaps it could be fine, but still surprising and worth investigating.

## Get a precise timing

The CPU profile unfortunately does not show how much time is spent exactly in `findMigrations`. At this point, I also do not know how many SQL files are present. If there are indeed a bazillion SQL migrations, maybe it's expected that sorting them indeed takes the most time. 

Let's first find out with dtrace how long the function runs. We'd like to dynamically trace `findMigrations`, but the Go compiler actually inlined it. We can see it on the profile, it's mark `inl` for inline. The profiler is clever enough to inspect the debug information and reconstruct this information. But dtrace inserts tracing code at runtime at the entry of the function - if it does not exist it's not feasible. So we trace the next best thing which is the caller of `findMigrations`: `NewMigrationBox`.

Let's first check it is visible to dtrace by listing (`-l`) all probes matching the pattern `*ory*` in the executable `code.test.before` (i.e. before the fix):

```sh
$ sudo dtrace -n 'pid$target:code.test.before:*ory*: ' -c ./code.test.before -l | grep NewMigrationBox
209591   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox return
209592   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox entry
209593   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 return
209594   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 entry
[...]
```

Ok, the first two are the ones we need. Let's time the function duration then with a D script `time.d`:

```dtrace
pid$target::*popx?NewMigrationBox:entry { self->t=timestamp } 

pid$target::*popx?NewMigrationBox:return {
  printf("NewMigrationBox:%d\n", (timestamp - self->t)/1000000)
}
```

Explanation: `timestamp` is an automatically defined variable that stores the current monotonic time at the nanosecond granularity. When we enter the function, we read the current timestamp and store it in a thread-local variable `t` (with the `self->t` syntax). When we exit the function, we do the same again, compute the difference in terms of milliseconds, and print that.

Since the tests log verbose stuff by default and I do not know how to silence them, I save the output of dtrace in a separate file `/tmp/time.txt`: `dtrace -s time.d -c ./code.test.before -o /tmp/time.txt`

We see these results (excerpt):

```
 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

 10   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

  4   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:179

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:179
```

> There are some outlier numbers, but I believe this is due to the `M:N` concurrency model of Go, where a function can start on one OS thread, but during its executation, yield back to the Go runtime due to doing I/O or such, and then be continued later, potentially on a different OS thread. Thus, our use of a thread-local variable is not strictly correct (but good enough). To be perfectly correct, we would need to also track with Dtrace the Go scheduler actions. Which is also possible but complicates the script.


Dtrace can also compute histograms which is typically a better approach when inspecting the runtime of something, for example the duration of a HTTP request, but here this is enough for us.

## Establishing a baseline

When doing some (light) optimization work, it is crucial to establish a baseline. What is the ideal time? How much do we differ from this time? And to establish this baseline, it is very important to understand what kind of work we are doing:

- Are we doing any I/O at all?
- Are we doing purely in-memory work?

So let's see what the baseline is to simply find all the SQL files:

```sh
hyperfine --shell=none --warmup=5 "find ./persistence/sql/migrations -name '*.sql' -type f"
Benchmark 1: find ./persistence/sql/migrations -name '*.sql' -type f
  Time (mean ± σ):     206.6 ms ±   4.8 ms    [User: 2.4 ms, System: 5.0 ms]
  Range (min … max):   199.5 ms … 214.4 ms    14 runs
```

Ok, so 200ms, which is pretty close to our 180ms in Go...

Wait a minute...*are we doing any I/O in Go at all???* Could it be that we *embed* all the SQL files in our binary at build time ?!

Let's check with dtrace:

```
syscall::open:entry { 
  filename = copyinstr(arg0);

  if (rindex(filename, ".sql") == strlen(filename)-4) {
    printf("%s\n", filename)
  }
} 
```

When we run this script on `go test -c` which builds the text executable, we see that all the SQL files are being opened by the Go compiler and subsequently embedded in the test binary:

```sh
$ sudo dtrace -s ~/scratch/popx_opens.dtrace -c 'go test -tags=sqlite -c'

CPU     ID                    FUNCTION:NAME
 10    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.cockroach.down.sql

  5    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.postgres.down.sql

[...]
```

Damn, so we are doing purely in-memory work. Alright, so is 180 ms still good in that light? How many files are we dealing with? According to `find`, ~1.6k. So, you're saying we are spending 180 ms to sort a measle 1.6k items in a linear array? That's *nothing* for a modern computer! It should be a small number of milliseconds!

## What the hell is my program even doing?

If you're like me, you're always asking yourself: what is taking my computer so f***ing long? Why is Microsoft Teams hanging again? (Joking, I do not have to use it, thank God). Why is Slack taking 15s to simply startup? 

The problem with computer programs is that there are a black box. You normally have no idea what they are doing. If you're lucky they will print some stuff. If you're lucky.

If only there was a tool that shows me precisely what the hell my program is doing...And I could dynamically choose what to show and what to hide to avoid noise...Oh wait this exists for 20 years. Dtrace of course!

If you're still not convinced to use dtrace yet, let me show you its superpower. It can show you *every* function call your program does! That's sooo useful when you do not know the codebase. Let's try it, but we are only interested in calls from within `NewMigrationBox`, and when we exit `NewMigrationBox`, we should stop, because each invocation will be anyway be the same:

```
pid$target:code.test.before:*NewMigrationBox:entry { self->t = 1}

pid$target:code.test.before:*NewMigrationBox:return { stop() }

pid$target:code.test.before:sort*: /self->t != 0/ {}
```

Note 1: `stop()` kills the current process and as such is a *destructive action*. Yes, dtrace can do destructive actions (enabled with the `-w` flag) like write arbitrary data to some place in memory, send signals to processes, run shell commands, etc. when some event triggers. It can even do that *inside the kernel*. 

Note 2: I have written more specific probes in this script to try to reduce noise (by accidentally matching probes we do not care about) and also to help with performance. Since we know that the performance issue is located in the sorting part, we only need to trace that.

So, let's run our script with the `-F` option to get a nicely formatted output:

```
$ sudo dtrace -s ~/scratch/popx_trace_calls.dtrace -c './code.test.before' -F -w

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

The `->` arrow means we enter the function, and `<-` means we exit it. We see a nice call tree. Also I notice something weird: we are calling `sort.Sort` way too much. It should be once, but we call it much more than that. 

That's the canary in the coal mine.


## It's always superlinear algorithmic complexity

Time to inspect the code, finally. Pretty quickly I stumble upon code like this:

```go
```


