Title: I sped up the test suite by x2 with one simple change
Tags: Go, SQL
---

We have a giant test suite at work, mostly in Go. The test coverage is great, but it means that it's not *that* fast to run, and it only will get slower over time.  

An esteemed colleague of mine did some benchmarking and identified that a big chunk of time in tests was spent simply creating a SQLite database with the right schema, before the test code gets to even run! 

As mentioned in a [previous article](/blog/an_optimization_and_debugging_story_go_dtrace.html), thousands (!) of SQL migrations have accumulated over the years, and I had to fix a performance issue where we spent a lot of time simply gathering all migration files (not even applying them).

With that fix done, the next bottlneck was applying these migrations. A few reasons make this very appealing to optimize:

- Every test using a database does it
- Applying the migrations is done serially (one at a time) and no test code can run until migrations are applied
- It is entirely unnecessary to apply each migration one by one, nearly all tests are only interested in the latest schema

And so I decided to optimize it. When doing performance optimizations, it's important to spend some time first deciding if it's worth your time!


## Quick and dirty check

Optimization work can be very unrewarding: you spend a lot of time and at then end you measure, to see no difference (or perhaps worse performance than before!). 

So it's also very important, if possible, to do a quick and dirty check, to see if the optimization has any legs.

So in my case, I wanted to see: let's assume every test has access to a ready-made database, with an up-to-date schema. What's the runtime of the test suite then? That's the upper-bound for this work.


So I did something very simple: I put a breakpoint in one test, right after all database migrations ran. This means the test stopped at a point where a pristine SQLIte database was present on disk. I then copied it with `cp` to my home directory: this is now my golden (immutable) database. I finally modified the migration code that all tests start with, to never run apply any SQL migrations, and instead just copy the golden database file, and use that. 

And this is what I saw:

![golden_db_test.png]
