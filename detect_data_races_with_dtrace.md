Title: Detect data races with DTrace in any language
Tags: DTrace, Concurrency
---

A data race is concurrent access to shared data in a way that does not respect the rules of the programming language. Some languages are stricter or looser when they establish how that can happen, but they all forbid *some* kinds of concurrent (unsynchronized) accesses, typically write-write or read-write.

For example [Go's memory model](https://go.dev/ref/mem#model) defines a data race as:


>  More generally, it can be shown that any Go program that is data-race-free, meaning it has no program executions with read-write or write-write data races, can only have outcomes explained by some sequentially consistent interleaving of the goroutine executions. (The proof is the same as Section 7 of Boehm and Adve's paper cited above.) This property is called DRF-SC.
> 
> The intent of the formal definition is to match the DRF-SC guarantee provided to race-free programs by other languages, including C, C++, Java, JavaScript, Rust, and Swift. 
