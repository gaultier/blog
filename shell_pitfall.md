Title: A silly shell pitfall
Tags: Shell
---

Super short article today.

At [work](https://github.com/ory/sdk) we maintain an OpenAPI (formerly known as Swagger) specification of our APIs. 
Then we use generators we produce client code in various languages. 

I noticed that Swift was missing from the list so I wanted to add it.

Just to be sure that the generated code works, we have a step in CI that builds it. 

Thus I wrote, or rather extended, a [shell script](https://github.com/ory/sdk/blob/master/scripts/test.sh) like this:

```shell
#!/bin/sh

swift () {
  echo "Building Swift..."
  (cd "./swift" && swift build)
}

swift
```


Now dear reader, take a guess at what is wrong here. Any ideas? Just a hint: the (marvelous) [shellcheck](https://www.shellcheck.net) linter does not bat an eye here. Everything is fine... 


---

Now to my surprise, the script got stuck in infinite recursion, until it finally errors when trying to enter the non-existent directory `swift`. If you go deep enough in the file system, ultimately you reach the end and `cd` fails.

That's because when the shell sees `swift build`, it interprets it as: call the shell function called `swift` with the argument `build`. Whereas I intended to call the CLI command `swift`.


The fix is either to rename the shell function to something else, like `run_swift`, or to use the `exec` builtin to disambiguate:

```diff
--- test.sh	2026-03-04 17:50:38
+++ test_fixed.sh	2026-03-04 17:50:34
@@ -4,7 +4,7 @@
 
 swift () {
   echo "Running Swift..."
-  (cd "my-swift-project" && swift build)
+  (cd "my-swift-project" && exec swift build)
 }
 
 swift
```


Now, any language worth its salt will warn you that this is infinite recursion:

```rust
fn main() {
    println!("hello");

    main();

    [...]
}
```

The compiler warns us:

```shell
$ cargo c
warning: function cannot return without recursing
    --> src/main.rs:1279:1
     |
1279 | fn main() {
     | ^^^^^^^^^ cannot return without recursing
...
1282 |     main();
     |     ------ recursive call site
     |
     = help: a `loop` may express intention better if this is on purpose
     = note: `#[warn(unconditional_recursion)]` on by default
```

*Ahem... While writing this article and testing with a few languages, I noticed Go does **not** warn us in this case...*


So that will be my advice: the shell is fine for one-liners. Anything else, just use your favorite general purpose programming language, it'll be simpler, better, faster, stronger.
