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

## Dtrace

The CPU profile unfortunately does not show how much time is spent exactly in `findMigrations`. At this point, I also do not know how many SQL files are present. If there are indeed a bazillion SQL migrations, maybe it's expected that sorting them indeed takes the most time. 

Let's first find out with dtrace how long the function runs. We'd like to dynamically trace `findMigrations`, but the Go compiler actually inlined it. We can see it on the profile, it's mark `inl` for inline. The profiler is clever enough to inspect the debug information and reconstruct this information. But dtrace inserts tracing code at runtime at the entry of the function - if it does not exist it's not feasible. So we trace the next best thing which is the caller of `findMigrations`: `NewMigrationBox`.

Let's first check it is visible to dtrace by listing (`-l`) all probes matching the pattern `*ory*` in the executable `code.test.before` (i.e. before the fix):

```sh
$ sudo dtrace -n 'pid$target:code.test.before:*ory*: ' -c ./code.test.before -l | grep NewMigrationBox
209591   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox return
209592   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox entry
209593   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 return
209594   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 entry
209595   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4.deferwrap1 return
209596   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4.deferwrap1 entry
209597   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2 return
209598   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2 entry
209599   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2.1 return
209600   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2.1 entry
209601   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1 return
209602   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1 entry
209603   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1.1 return
209604   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1.1 entry
209605   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.ParameterizedMigrationContent.func3 return
209606   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.ParameterizedMigrationContent.func3 entry
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

We see these results:

```
```



