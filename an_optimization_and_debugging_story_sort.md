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

When I benchmark the test suite, I notice something weird:

- Simply collecting all migration files takes ~180 ms, even though there are only ~ 1.6k SQL files. This should not take this long because all the SQL files are embedded in the binary with `go:embed`. By comparison, a simple `find . -name '*.sql'` takes ~200ms. How come doing something purely in memory takes as much time as doing it with disk I/O?
- The profile show big clumps (in yellow) that are the function `findMigrations` whereas the rest of the profile is pretty uneventful.

![CPU profile of the test suite](x_popx_profile.png)




