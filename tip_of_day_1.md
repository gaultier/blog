Title: Tip of the day #1
Tags: Rust, Tip of the day
---

I have a Rust codebase at work. The other day, I was wondering how many lines of code were in there. Whether you use `wc -l ***.rs` or a more fancy tool like `tokei`, there is an issue: this will count the source code *as well as* tests. 

That's because in Rust and in some other languages, people write their tests in the same files as the implementation. Typically it looks like that:

```rust
// src/foo.rs

fn foo() { 
 ...
}

#[cfg(test)]
mod tests {
    fn test_foo(){
      ...
    }

    ...
}
```

But I only want to know how big is the implementation. I don't care about the tests. And `wc` or `tokei` will not show me that.

So I resorted to my trusty `awk`. Let's first count all lines, like `wc` does:

```sh
$ awk '{count += 1} END{print(count)}' src/***.rs
# Equivalent to:
$ wc -l src/***/.rs
```

On my open-source Rust [project](https://github.com/gaultier/kotlin-rs), this prints `11485`. 

Alright, now let's exclude the tests. When we encounter the line `mod tests`, we stop counting. Note that this name is just a convention, but that's one that followed pretty much universally in Rust code, and there is usually no more code after this section. Tweak the name if needed:

```sh
$ awk '/mod tests/{skip[FILENAME]=1}  !skip[FILENAME]{count += 1} END{print(count)}'  src/***.rs
```

And this prints in the same project: `10057`.

Let's unpack it:

- We maintain a hashtable called `skip` which is a mapping of filename to whether or not we should skip the rest of this file. In AWK we do not need to initialize variables, we can use them right away and they are zero initialized. AWK also automatically stores the name of the current file in the global builtin variable `FILENAME`.
- `/mod tests/`: this pattern matches the line containing `mod tests`. The action for this line is to flag this file as 'skipped'. 
- `!skip[FILENAME]{count += 1}`: If this file is not flagged as 'skipped', we increment for each line, the global counter. Most people think that AWK can only use patterns as clauses before the action, but in fact it also supports boolean conditions, and both can be use together, e.g.: `/foo/ && !skip[FILENAME] {print("hello")}`
- `END{print(count)}`: we print the count at the very end.

And that's it. AWK is always very nifty.

## Addendum: exit

Originally I implemented it wrongly, like this:


```sh
$ awk '/mod tests/{skip[FILENAME]=1}  skip[FILENAME]{exit 0} {count += 1} END{print(count)}'  src/***.rs
```

If the file is flagged as 'skipped', stop processing it altogether with the builtin statement `exit` ([docs](https://www.gnu.org/software/gawk/manual/html_node/Exit-Statement.html)).

Running this on the same Rust codebase prints: `1038` which is obviously wrong.

Why is it wrong then?

Well, as I understand it, AWK processes all inputs files one by one, as if it was one big sequential file (it will still fill the builtin constant `FILENAME` though, that's why the solution above works). Since there is no isolation between processing each file (AWK does not spawn a subprocess for each file), it means we simply stop altogether at the first encountered test in any file.
