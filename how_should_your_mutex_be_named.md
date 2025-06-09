Title: How should your mutexes be named?
Tags: Go, Concurrency
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
barMu.Lock()
bar += 1
barMu.Unlock()
```

*Yes, in this particular case an atomic would be used instead of a mutex but that's just to illustrate.*

But I paused for a second: What should the mutex be named? I usually use the `xxxMtx` convention. 

To avoid a sterile 'you vs me' debate, I thought: What do other people do? What naming convention does the Go standard library use, if any? 

And more generally, what is the best way to find out what naming conventions are used in a project? Since I just started a new job, it's a prevalent question which will come again and again. Thus, I need a good tool to find the answers quickly.

## Structural search

I use `ripgrep`, `ag` and `awk` all the time when developing (probably at least once a minute), and they can do that, kind of, since they operate on raw text. What I often actually need to do is *search the code structurally*, meaning search the Abstract Syntax Tree (AST). And the good news, there are tools nowadays than can do that! I never took the time to learn one, so I felt this is the occasion.

Enter [ast-grep](https://github.com/ast-grep/ast-grep). Suprisingly, the main way to search with it is to write a rule file in YAML. A command line search also exists but seems much more limited. Let's search for structure fields that are a mutex:

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


When we run the tool on the Go project source code, we get something like this (excerpt):

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

Very useful. The tool can do much more, such as rewriting code, etc, but that's enough for us to discover that there isn't *one* naming convention in this case. Also, the mutex is not always named after the variable it protects. So, is there at least a convention used in the majority of cases?

## A naming convention to rule them all

Unfortunately I did not find a built-in way to post-process the results from `ast-grep`, so I resorted to goold ol' AWK:

```awk
/^[0-9]+/ {
  if ($3 ~ /(m|M)u$/) { 
    stats["mu"] += 1
  }
  else if ($3 ~ /(m|M)ux$/) { 
    stats["mux"] += 1
  }
  else if ($3 ~ /(m|M)tx$/) { 
    stats["mtx"] += 1
  }
  else if ($3 ~ /(m|M)utex$/) { 
    stats["mutex"] += 1
  }
  else if ($3 ~ /(l|L)ock$/) { 
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

```sh
$ ast-grep scan --rule ~/scratch/mtx.yaml | awk -f ~/scratch/ast-grep-post.awk 
lock 6
other 11
mutex 11
mu 131
```

So in conclusion: If you want to follow the same naming conventions as the Go project, use `xxxMu` as a name for your mutexes. I would also add, and this is subjective: name the mutex after the variable it protects for clarity, e.g.: `bar` and `barMu`. In nearly every case in the Go project where this rule was not followed, a code comment explained which variable the mutex protected. Might as well have it in the name I think. Even for cases where the mutex protects multiple variables, the Go developers often picked one of the variables and named  the variable after it:

```
note[find-mtx-fields]: Mutex fields found
     ┌─ cmd/internal/obj/link.go:1154:2
     │
1154 │     hashmu             sync.Mutex       // protects hash, funchash
     │     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

## Limitations

- Most if not all structural search tools only work on a valid AST
- Speed can be an issue: `ast-grep` is relatively fast but still slower than `ripgrep` and it states that it is one of the fastest in its category. It takes on my (admittedly very old laptop) ~10s to scan ~2 millions LOC. Which is pretty good! It's just that `ripgrep` takes ~100ms and `find + awk` ~1.5s.
- The rule syntax is arcane and in parts language specific.


## Low-tech alternatives

A quick and dirty way to achieve the same with a regexp is:

```
rg -t go '^\s*\w+\s+sync\.Mutex$'
```

This works since Go is a language with only one way to define a struct field, but some languages would be more difficult.

Another way to only find field declarations would be to use awk:

```awk
/\s+struct\s+/ { in_struct = 1 }

in_struct && /\s+\w+\s+sync\.Mutex/ { print }

in_struct && /^}$/ { in_struct = 0 }
```

But this might not work with complex constructs such as defining a struct within a struct e.g.:

```go

type Foo struct {
	bar struct {
		x int
		y int
	}
	barMtx sync.Mutex
}
```

## Conclusion

I think one structural search program is a very useful tool to have in your toolbox as a software developer, especially if you intend to use it as a linter and mass refactoring tool. 

If all you want to do is a one-time search, text search programs such as `ripgrep` and `awk` probably should be your first stab at it.

Also, I think I would prefer using a SQL-like syntax to define rules over YAML with pseudo-code constructs like `all`.
