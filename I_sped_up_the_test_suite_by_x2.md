Title: I sped up the test suite by x2 with one simple change
Tags: Go, SQL
---

We have a giant test suite at work, mostly in Go. The test coverage is great, but it means that it's not *that* fast to run, and it only will get slower over time.  

An esteemed colleague of mine did some benchmarking and identified that a big chunk of time in tests was spent simply creating a SQLite database with the right schema, before the test code gets to even run! Since every test has its own database for isolation and reproducibility, that's costly.

As mentioned in a [previous article](/blog/an_optimization_and_debugging_story_go_dtrace.html), thousands (!) of SQL migrations have accumulated over the years, and I had to fix a performance issue where we spent a lot of time simply gathering all migration files (not even applying them).

With that fix done, the next bottleneck was applying these migrations. A few reasons make this part very appealing to optimize:

- Every test using a database runs this code
- Applying the migrations is done serially (one at a time) and no test code can run until migrations are fully applied
- It is entirely unnecessary to apply each migration one by one, nearly all tests are only interested in the latest database schema

And so I decided to optimize it. When doing performance optimizations, it's important to spend some time first deciding if it's worth your time on paper!


## Quick and dirty check

Optimization work can be very unrewarding: you spend a lot of time and at then end you measure, to see no difference (or perhaps worse performance than before!). 

So it's also very important, if possible, to do a quick and dirty check at the beginning, to see if the optimization has any legs.

In my case, here's what I wanted to see: let's assume that every test has access to a ready-made database, with an up-to-date database schema. What's the runtime of the test suite then? That's the upper-bound for this work, where I 'optimized' the database migration code to take no time at all.


Thus I did something very simple: I put a breakpoint in one test, right after all database migrations ran. This means the test stopped at a point where a pristine SQLite database was present on disk. I then copied this file with `cp` to my home directory: this is now my golden (immutable) database. I finally modified the migration code (that all tests start with), to never apply any SQL migrations, and instead just copy the golden database file, and use that. 

And this is what I saw:

![Result](golden_db_test.png)


Alright, it's confirmed that this optimization is worth it!


## The implementation

We could do the same as in the test above: assume that a human or a tool maintains a golden database file up to date, and when a test starts, it copies this golden file, and uses it as its database. 

However, that requires some out-of-band process, since new SQL migrations get added every few days, and there is a risk that this golden file gets out of sync.

So I went the other way: at the start of each test, we either use the golden file if it exists, or we lazily (on demand) create it otherwise, and then start using it. 

Due to Go's test framework and the fact that we use a monorepo, running Go tests from many different and unrelated projects, there is no clear entry point for all tests, where we could run our logic. 

That means that each test must run this logic and we'll have a contention point when checking if the golden database file already exists, which we accept.

The approach is, I find, quite elegant:

1. In each test, call one function to apply all database migration files. We call it the 'migration box'.
2. In this function, first check if the golden database file exists. If it does, simply copy[^1] it to a uniquely named file, and immediately return this name, so that the test can then use it, fully isolated from the other tests.
3. If the golden database file does *not* exist, we need to apply all SQL migrations to a new database (file):
    1. Collect all SQL migrations files
    2. Compute a SHA256 hash of their content
    3. Create a new database file with a random name e.g. `/tmp/234970870`
    4. Apply all SQL migrations to this new database file.
    5. Rename this file to `/tmp/<SHA256 hash>`. We now have our golden database! This is using content addressing: a test can simply try to find the file using the SHA256 hash and be assured that the file has had all SQL migrations applied.
    5. Copy this golden database file to a uniquely named file and return that name. The calling test can now use it, and all other subsequent tests will find the golden database and use it.


A few points are critical to make it correct:

- SQL migrations are not applied to the golden database file (`/tmp/<SHA256 hash>`) directly: they are applied to a temporary file, which is then renamed to be the golden database file. This is crucial to avoid concurrent tests seeing a partially-written golden database file, where the file exists but not all SQL migrations have been applied yet. The golden database file either exists in its full-fledged form, or it doesn't, but it never exists in a partial form.
- The golden database file uses content-addressing (its name is the SHA256 hash) so that when a new SQL migration is added, the whole process works out of the box: the SHA256 hash will be different, and a new golden database file will be created, as if the old one never existed.
- No clean-up of old golden files is needed since they only exist in the temporary directory. They might get cleaned-up by the OS upon restart, and then the next time we run the tests, the golden file will be re-created automatically (at the cost of a longer runtime, once).


## The dirty details

SQLite boast about having only one database file, which is trivially shared with others, copied, etc. However this has not been the case for a long time: there is a journal file, a WAL file (when using WAL mode), shared memory files when multiple processes are accessing the same database (which is the case when running go tests for multiple packages), etc.

That means that simply using `cp` might work in some cases but not in all cases. SQLite comes with a better solution: the [backup API](https://sqlite.org/backup.html), which we use here, and it takes care of all these ancillary files, it works also in in-memory mode, etc.




