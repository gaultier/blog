Title: A silly Shell pitfall
Tags: Shell
---

Super short article today.

At [work](https://github.com/ory/sdk) we maintain an OpenAPI (formerly known as Swagger) specification of our APIs. 
Then we use generators we produce client code in various languages. 

I noticed that Swift was missing from the list so I wanted to add it.

Just to be sure that the generated code works, we have a step in CI that builds it. 

Thus I wrote, or rather extended, a Shell script like this:

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

Now to my surprise, the script got stuck in infinite recursion.

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
