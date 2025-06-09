Title: How should your mutexes be named?
Tags: Go, Awk, Search
---

I have started a new job recently and the main challenge, I find, is to ingest and adapt to a big codebase that does things slightly differently than what I am used to.

This article explores ways to make this phase go smoother.

---

The other day a Pull Request popped up at work, which made me think for a bit. It looked like this (Go, simplified):

```go
type Foo struct {
    bar int
    barMux sync.Mutex
}
```

It's a typical approach with concurrent code: a mutex protects the variable it is named after. So using the variable `bar` looks like this:

```go
barMux.Lock()
bar += 1
barMux.Unlock()
```

*Yes, in this simplistic case an atomic would likely be used instead of a mutex but that's just to illustrate.*

But I paused for a second: What should the mutex be named? I usually use the `xxxMtx` convention, so I'd have named it `barMtx`. 

To avoid a sterile 'you vs me' debate, I thought: What do other people do? What naming convention is in use in the project, if any? I'll demonstrate this method with the code of the Go standard library.

And more generally, what is the best way to find out what naming conventions or code patterns are used in a project you don't know? I need a good tool to find these answers quickly.

## Structural search

I use `ripgrep` and `awk` all the time when developing, probably at least once a minute, and these tools can give us the answers... kind of, since they operate on raw text. Complex code constructs or contextual searches e.g. 'A function call whose third argument is a number and the function name starts with `get`' may be impossible to find correctly.

What I often actually need to do is *search the code structurally*, meaning search the Abstract Syntax Tree (AST). And the good news is, there are tools nowadays than can do that! I never took the time to learn one, even though this came up a few times, so I felt this is finally the occasion.

Enter [ast-grep](https://github.com/ast-grep/ast-grep). Surprisingly, the main way to search with it is to write a rule file in YAML. A command line search also exists but seems much more limited. 

Let's thus search for 'structure fields' whose type is a 'mutex':

```yaml
id: find-mtx-fields
message: Mutex fields found
severity: info
language: go
rule:
  kind: field_declaration
  all:
    - has: 
        field: name
        # Ignore nameless fields.
        regex: ".+"
    - has:
        field: type
        regex: "Mutex"
```

A potential match must pass all conditions under the `all` section to be a result. There are other ways to write this rule, but this works. Note that the regexp is loose enough to match different kinds of mutex types such as `sync.Mutex` and `sync.RWMutex`.


When we run the tool on the Go standard library, we get something like this (excerpt):

```sh
$ ast-grep scan --rule ~/scratch/mtx.yaml
[...]

note[find-mtx-fields]: Mutex fields found
   ┌─ net/textproto/pipeline.go:29:2
   │
29 │     mu       sync.Mutex
   │     ^^^^^^^^^^^^^^^^^^^


note[find-mtx-fields]: Mutex fields found
    ┌─ net/rpc/server.go:191:2
    │
191 │     reqLock    sync.Mutex // protects freeReq
    │     ^^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields]: Mutex fields found
   ┌─ net/rpc/jsonrpc/server.go:31:2
   │
31 │     mutex   sync.Mutex // protects seq, pending
   │     ^^^^^^^^^^^^^^^^^^

[...]
```

Very useful. The tool can do much more, but that's enough for us to discover that there isn't *one* naming convention in use. Also, the mutex is not always named after the variable it protects (e.g.: `mutex` protects `seq`).


So, is there at least a convention used in the majority of cases? How can we get aggregate the results?

## A naming convention to rule them all

Unfortunately I did not find a built-in way to post-process the results from `ast-grep`, so I resorted to outputting the matches as JSON, extracting the matched text with `jq`, and finally aggregating the results with good old AWK:

```sh
$ ast-grep scan --rule ~/scratch/mtx.yaml --json | jq '.[].text' -r | awk -f ~/scratch/ast-grep-post.awk 
```

And this is the ad-hoc AWK script:

```awk
# ~/scratch/ast-grep-post.awk

# `$1` is the variable name.
{
  if ($1 ~ /(m|M)u$/) { 
    stats["mu"] += 1
  }
  else if ($1 ~ /(m|M)ux$/) { 
    stats["mux"] += 1
  }
  else if ($1 ~ /(m|M)tx$/) { 
    stats["mtx"] += 1
  }
  else if ($1 ~ /(m|M)utex$/) { 
    stats["mutex"] += 1
  }
  else if ($1 ~ /(l|L)ock$/) { 
    stats["lock"] += 1
  } else {
    stats["other"] += 1
  }
}

END {
  for (k in stats) {
    print k, stats[k]
  }
}
```

And here are the statistics (commit `7800f4f`, 2025-06-08):

|Variable name suffix|Count|
|--------------------|-----|
| `mu`               | 131 |
| `mutex`            | 11  |
| something else     | 11  |
| `lock`             | 6   |
| `mux`              | 0   |

So according to these statistics: if you want to follow the same naming convention as the Go project primary one, **use `xxxMu` as a name for your mutexes**.

I would also add, and this is subjective: **name the mutex after the variable it protects for clarity, e.g.: `bar` and `barMu`**. In nearly every case in the Go project where this rule of thumb was not followed, a code comment was present to explain which variable the mutex protects. We might as well have this information in the mutex variable name.

Even for cases where the mutex protects multiple variables, the Go developers often picked one of the variables and named the mutex after it:

```go
type Link struct {
	hashmu             sync.Mutex       // protects hash, funchash
	hash               map[string]*LSym 
	funchash           map[string]*LSym

        [...]
}
```

## Low-tech alternatives

A quick and dirty way to achieve the same with a regexp is:

```
$ rg -t go '^\s+\w+\s+sync\.Mutex$'
```

This works since Go is a language with only one way to define a `struct` field, but some languages would be more difficult.

A slightly smarter way, to only find field declarations, would be to use AWK to remember whether or not we are inside a `struct` definition:

```awk
/\s+struct\s+/ { in_struct = 1 }

in_struct && /\s+\w+\s+sync\.Mutex/ { print }

in_struct && /^}$/ { in_struct = 0 }
```

But this might not work, or at least need to be adapted, to cover complex constructs such as defining a `struct` within a `struct`:

```go
type Foo struct {
	bar struct {
		x int
		y int
	}
	barMtx sync.Mutex
}
```

These approaches are not bullet-proof, but they will find most relevant code locations, which is enough.

## Limitations of structural search tools

- Most if not all structural search tools only work on a valid AST, and not every language is supported by every tool.
- The rule syntax is arcane and in parts language specific (see the addendum for details).
- Speed can be an issue: `ast-grep` is relatively fast but still slower than `ripgrep` and it states that it is one of the fastest in its category. On my (admittedly very old) laptop:
  + `ast-grep` takes  ~10s to scan ~2 millions LOC. Which is pretty good! 
  + `find + awk` takes ~1.5s.
  + `ripgrep` takes ~100ms (I'm impressed).



## Conclusion

I think having one structural search program in your toolbox is a good idea, especially if you intend to use it as a linter and mass refactoring tool. 

If all you want to do is a one-time search, text search programs such as `ripgrep` and `awk` should probably be your first stab at it.

Also, I think I would prefer using a SQL-like syntax to define rules, over writing YAML with pseudo-code constructs.


## Addendum: How were mutexes named in the C implementation of the Go compiler?

I wondered if the way mutexes are named in the Go project actually comes from the time were the Go compiler and much of the runtime were implemented in C. 
We can easily check this out with the same approach. This illustrates that `ast-grep` works for different languages, and also the slight differences.

`1.4` was the last major version to use the C compiler and runtime apparently, so we checkout this commit:

```sh
$ git checkout go1.4beta1
```

I initially simply changed the `language: go` field in the rule to `language: c` but was surprised nothing turned up. After toying with the [treesitter playground](https://tree-sitter.github.io/tree-sitter/7-playground.html) (`treesitter` is used under the covers), I realized that for C, the AST is structured differently and the nodes have different names. Here is the rule working for `struct` fields:


```yaml
id: find-mtx-fields-struct-fields
message: Mutex fields found
severity: info
language: c
rule:
  kind: field_declaration
  all:
    - has: 
        field: declarator
        regex: ".+"
    - has:
        field: type
        regex: "(M|m)utex"
```

Here are the results of this rule (excerpt):

```
note[find-mtx-fields]: Mutex fields found
    ┌─ runtime/malloc.h:430:2
    │
430 │     Mutex   specialLock;    // guards specials list
    │     ^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields]: Mutex fields found
    ┌─ runtime/malloc.h:451:2
    │
451 │     Mutex  lock;
    │     ^^^^^^^^^^^^
```

---

Right after, I realized that some mutexes are defined as global variables, so here is an additional rule file for that:

```yaml
id: find-mtx-fields-vars
message: Mutex variables found
severity: info
language: c
rule:
  kind: declaration
  all:
    - has: 
        field: declarator
        regex: ".+"
    - has:
        field: type
        regex: "(M|m)utex"
```

And here are the results (excerpt):

```
note[find-mtx-fields-vars]: Mutex variables found
   ┌─ runtime/panic.c:18:1
   │
18 │ static Mutex paniclk;
   │ ^^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields-vars]: Mutex variables found
    ┌─ runtime/panic.c:162:3
    │
162 │         static Mutex deadlock;
    │         ^^^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields-vars]: Mutex variables found
   ┌─ runtime/mem_plan9.c:15:1
   │
15 │ static Mutex memlock;
   │ ^^^^^^^^^^^^^^^^^^^^^

note[find-mtx-fields-vars]: Mutex variables found
    ┌─ runtime/os_windows.c:586:2
    │
586 │     static Mutex lock;
```

---

Funnily, it seems that: 1) the C code has much less variability in naming than the Go code: it's mostly `xxxlock`, and 2) The naming in Go code does not stem from the C code since it's completely different.
