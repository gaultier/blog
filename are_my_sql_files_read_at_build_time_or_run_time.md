Title: Are my SQL files read at build time or run time?
Tags: Go, DTrace
---

Continuing my Go + DTrace series, but this time this is a small entry.

Go can bake files into the final executable at build time with the `//go:embed` syntax. And files can also be read at runtime of course. This is handy for example for SQL migration files, so that the application can apply pending SQL migrations (if any) at start-up.

While debugging an issue with a failing test, I had to answer this question: are the SQL migration files being read at build time or run time (this topic has bit me [in the past](/blog/an_optimization_and_debugging_story_go_dtrace.html#establishing-a-baseline))? The source code had the annotation: `//go:embed *.sql`, but a test complained that the SQL migration file `some_file.go` could not be applied. Which makes sense. So how is it possible? The glob pattern should only select SQL files!

My first instinct was to turn to `opensnoop` but it does not have any way to filter by the file name and on a busy machine, with lots of files being opened all the time, this is simply too noisy.

I could also inspect system calls, but I would need to both watch `open` and `openat`, and perhaps `mmap`, and also follow child processes... There surely is an easier way?

Well, yes! The DTrace docs mention the `io` provider which is exactly what we need (and what `opensnoop` uses under the covers, I suspect). We are interested in `args[2]` which is a `fileinfo_t` structure, whose field `fi_pathname` has... the path name, you guessed it.

The D script is fairly straightforward:

```dtrace
io:::start 
/execname == "go" || execname == "migratest.test" /
{
    this->p = args[2]->fi_pathname;
    
    if (rindex(this->p, ".sql") == strlen(this->p)-4){
        printf("%s %s\n", execname, this->p);
        print(*args[2]);
    }
}
```

When we run `go test`, it first builds the code and produces the test executable `migratest.test` which is subsequently run. Assuming some code has been modified and Go needs to rebuild it, we see something like this:

```shell
$ go test .

 11 499892               buf_strategy:start go ??/sql/20250912000000000000_kratos_secret_pagination.autocommit.up.sql
fileinfo_t {
    string fi_name = [ "20250912000000000000_kratos_secret_pagination.autocommit.up.sql" ]
    string fi_dirname = [ "sql" ]
    string fi_pathname = [ "??/sql/20250912000000000000_kratos_secret_pagination.autocommit.up.sql" ]
    offset_t fi_offset = 0
    string fi_fs = [ "apfs" ]
    string fi_mount = [ "Data" ]
    int fi_oflags = 0
}
[...]

  6 499892               buf_strategy:start migratest.test ??/testdata/20250623113513_testdata.sql
fileinfo_t {
    string fi_name = [ "20250623113513_testdata.sql" ]
    string fi_dirname = [ "testdata" ]
    string fi_pathname = [ "??/testdata/20250623113513_testdata.sql" ]
    offset_t fi_offset = 0
    string fi_fs = [ "apfs" ]
    string fi_mount = [ "Data" ]
    int fi_oflags = 0
}
```

Looking at the first few lines of output: The executable name is `go` (the fourth field) so this SQL file is read at build time and baked into the test executable.

Then, looking at the last few lines of output: The executable name is this time `migratest.test` so this SQL file is read at run time.


So this was the problem: the test decided to walk the file system itself at run time without any globbing, stumbled on a `.go` file, and errored out.

With DTrace, we unveiled the truth in a few lines of D script: some SQL files were read at build time, and some at run time.
