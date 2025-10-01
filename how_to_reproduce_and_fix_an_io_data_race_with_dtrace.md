Title: How to reproduce and fix an I/O data race with DTrace
Tags: DTrace, Go
---

Today I was confronted at work with a bizzare test failure happening only in CI, in a project I do not know. An esteemed [colleague](https://github.com/zepatrik) of mine hypothesized this was a data race on a file. A component is writing to the file, another component is concurrently reading from this file, and due to improper synchronization, the latter sometimes reads empty or partial data. This would only happen in CI, sometimes, due to slow I/O in this environment.

Ok, so can we try to confirm this idea, without knowing anything about the codebase, armed only with the knowledge of the line and test file that fails?

First, we can 
