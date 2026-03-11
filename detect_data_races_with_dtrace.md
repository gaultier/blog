Title: Detect data races with DTrace in any language
Tags: DTrace, Concurrency
---

A data race is concurrent access to shared data in a way that does not respect the rules of the programming language. Some languages are stricter or looser when they establish how that can happen, but they all forbid *some* kinds of (unsynchronized) accesses, typically write-write or read-write.
