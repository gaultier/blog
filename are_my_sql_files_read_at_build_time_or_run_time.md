Title: Are my SQL files read at build time or run time?
Tags: Go, DTrace
---

Continuing my Go + DTrace series, but this time with is a small entry.

**tl;dr:** We use DTrace to monitor 'file opened' system-wide events to determine whether the Go compiler (build time) or our tests (run time) read some SQL files.

## The problem

Go can bake files into the final executable at build time with the `//go:embed` syntax. This is handy for example for SQL migration files, so that the application can apply pending SQL migrations (if any) at start-up.
 And files can also be read at runtime of course.

While debugging an issue with a failing test, I had to answer this question: are the SQL migration files being read at build time or run time (this topic has bit me [in the past](/blog/an_optimization_and_debugging_story_go_dtrace.html#establishing-a-baseline))? The source code had the annotation: `//go:embed migrations/*.sql`, but a test complained that the SQL migration file `some_file.go` could not be applied. That makes sense that throwing Go code at a SQL database would not go so well. 

So how is it possible? The glob pattern should only select SQL files, and only at build time! What is happening then? Are some files read at runtime?

My first instinct was to turn to [opensnoop](https://ss64.com/mac/opensnoop.html) but it does not have any way to filter by the file name and on a busy machine, with lots of files being opened all the time, this is simply too noisy.

I could also inspect system calls, but I would need watch `open`, `open_nocancel`, `open_extended`, on some platforms `open64`, and perhaps `mmap`, and also follow child processes (`opensnoop` uses this approach by the way)... There surely is an easier way? 

## The solution

Well, yes! The DTrace docs mention the [io](https://illumos.org/books/dtrace/chp-io.html#chp-io) provider which is exactly what we need. We are interested in `args[2]` which is a `fileinfo_t` structure, whose field `fi_pathname` has... the path name, you guessed it.

The D script is fairly straightforward:

```dtrace
io:::start 
/execname == "go" || execname == "migratest.test" /
{
    this->p = args[2]->fi_pathname;
    if (rindex(this->p, ".sql") == strlen(this->p)-4){
        printf("%s %s\n", execname, this->p);
    }
}
```

We watch for executables named `go` (the Go compiler - build time) or `migratest.test` (the faulty test - run time) that open files ending with `.sql`, in which case we print the file name and some additional metadata.

The nice thing is that we can keep our D script running forever, if we feel like it, even on a busy system, and run the test suite many times, it will work just fine.

This is useful for instance to avoid restarting a long-running application, to catch a bug that rarely happens, or just to iterate quickly on the problem.

When we run `go test`, it first builds the code and produces the test executable `migratest.test` which is subsequently run. So this one command obscures which phase reads the SQL files.

Assuming some code has been modified and Go needs to rebuild it before running the tests, we see with our D script something like this:

```shell
# In one terminal.
$ go test .

# In another terminal.
$ sudo dtrace -s ~/scratch/io.d -q

go ??/sql/20250623113513000000_hydra_secret_pagination.down.sql
go ??/sql/20250623113513000000_hydra_secret_pagination.up.sql
go ??/sql/20250624111454000000_keto_secret_pagination.down.sql
go ??/sql/20250624111454000000_keto_secret_pagination.up.sql

[...]

migratest.test ??/testdata/20231129123900_testdata.sql
migratest.test ??/testdata/20240113512800_testdata.sql
migratest.test ??/testdata/20240131100001_testdata.sql
```

Looking at the first few lines of output: The executable name is `go` (the first field) so these particular SQL files are read at build time and baked into the test executable.

Then, looking at the last few lines of output: The executable name is this time `migratest.test` so those other SQL files are read at run time.

## The root cause

So this was the problem: the test decided to walk the file system by itself at run time without any globbing, it stumbled on a `.go` file that happened to be in the `migrations` folder, and errored out.

With DTrace, we unveiled the truth in a few lines of D script: some SQL files (schema migrations) were read at build time, and some at run time (also schema migrations for some reason, as well as test data).

The question that started it all could be answered a number of different ways, but by using DTrace we have access to all the niceties it offers such as call stacks, etc.
