Title: In Rust, `let _ = ...` and `let _unused = ...` are not the same
Tags: Rust
---

In Rust and some other languages, the compiler or linter warns about unused variables. To silence these warnings we can name the unused variable either `_` or prefix it with `_`:

```rust
let _ = foo();
let _bar = foo();
```

And for the longest time I thought these were the same. But they're not.

## Context

I realized it when writing code for this very blog, to implement live-reloading: one thread watches the file system, and when a change is noticed, it signals another thread, which sends an event to the browser, to reload the page:

```rust
fn live_reload(
    mut resp: BufWriter<TcpStream>,
    mtx_cond: Arc<(Mutex<usize>, Condvar)>,
) -> Result<(), ()> {
    write!(
        resp,
        "HTTP/1.1 200\r\nCache-Control: no-cache\r\nContent-Type: text/event-stream\r\n\r\n"
    )
    .map_err(|_| ())?;

    loop {
        let (lock, cvar) = &*mtx_cond;
        let guard = lock.lock().map_err(|_| ())?;

        let _ = cvar.wait(guard).map_err(|_| ())?;
        write!(resp, "data: changed\n\n").map_err(|_| ())?;
        resp.flush().map_err(|_| ())?;
        println!("🔃 sse event sent");
    }
}
```

[Condition variables](https://linux.die.net/man/3/pthread_cond_wait) were made for this: the waiting thread has nothing to do until a file is changed, and it should wait patiently without consuming any CPU cycles.

For context (although this is not needed for this article), this is the notifying thread:

```rust
fn watch(mtx_cond: Arc<(Mutex<usize>, Condvar)>) {
    loop {
        // [...]
        // On file changed:
        let (lock, cvar) = &*mtx_cond;
        let _unused = lock.lock().unwrap();
        cvar.notify_all();
    }
}
```

Note that this code technically suffers from possible spurious wake-ups by the OS as pointed out by the [Rust docs](https://doc.rust-lang.org/std/sync/struct.Condvar.html):

> Note that this function is susceptible to spurious wakeups. Condition
> variables normally have a boolean predicate associated with them, and
> the predicate must always be checked each time this function returns to
> protect against spurious wakeups.

This is why this API returns a value. By checking this value in a loop, we can detect if a wake-up is spurious. 

However for simplicity of the implementation, and since this should be rare, and the consequence is fine (one unnecessary hot-reload of the page in the browser), I do not do this, and the value is unused.

## The difference

The compiler gave me a warning for this code though, which puzzled me for a second:

```plaintext
error: non-binding let on a synchronization lock
    --> src/main.rs:1462:13
     |
1462 |         let _ = cvar.wait(guard).map_err(|_| ())?;
     |             ^ this lock is not assigned to a binding and is immediately dropped
     |
     = note: `#[deny(let_underscore_lock)]` (part of `#[deny(let_underscore)]`) on by default
help: consider binding to an unused variable to avoid immediately dropping the value
     |
1462 |         let _unused = cvar.wait(guard).map_err(|_| ())?;
     |              ++++++
     help: consider immediately dropping the value
     |
1462 -         let _ = cvar.wait(guard).map_err(|_| ())?;
1462 +         drop(cvar.wait(guard).map_err(|_| ())?);
     |
```

The two important parts are: `this lock is not assigned to a binding and is immediately dropped` and `consider binding to an unused variable to avoid immediately dropping the value`.

I was not aware of the difference between `_` and `_unused`. In fact I went through the [Rust reference](https://doc.rust-lang.org/reference/destructors.html) and I did not find anything about this (perhaps I missed it?).

This is the code that the compiler generates for `_`, conceptually:

```rust
        let mutex_guard = cvar.wait(guard).map_err(|_| ())?;
        mutex_guard.release();

        // [...] Rest of the code in the scope.
```

And this is the code for `_unused`:

```rust
        let mutex_guard = cvar.wait(guard).map_err(|_| ())?;

        // [...] Rest of the code in the scope.

        // At the end of the scope:
        mutex_guard.release();
```

Since in this code, dropping the mutex guard releases the mutex that guards the condition variable, this is a big difference in semantics.


## Learnings

The same can happen for all resource-holding variables in Rust (files, sockets, memory allocation, etc): dropping releases the underlying resource (invisibly), and we need to be cognizant of [when the drop happens](/blog/perhaps_rust_needs_defer.html). To do that, we can log inside the `drop` function, set a breakpoint in the debugger, use DTrace, read the assembly, etc.

Sometimes, like in this very case, it is fine that the drop happens immediately, since code executing right after does not use the resource, and that typically helps performance: the critical section is shorter.
