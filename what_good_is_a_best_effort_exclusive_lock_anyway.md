Title: What good is a best effort exclusive lock, anyway?
Tags: Concurrency, SQL, CockroachDB
---

An interesting paradoxon came up last week at work. A colleague implemented an exclusive lock in [Kratos](http://github.com/ory/kratos) on a row in the database to avoid two different requests from using a one time code. I'm simplifying but that's the idea. So correctness is vital.

For mainstream databases like Postgres and MySQL, that's simply done with `SELECT ... FOR UPDATE`, end of story. No one else can concurrently read (in a locking way) or write the selected rows: they block on it, until the surrounding SQL transaction is finished (either committed or rolled back).

But we also support CockroachDB, which does also support that syntax, except this does something *a bit* [different](https://docs.cockroachlabs.com/docs/v26.2/select-for-update#lock-behavior-under-serializable-isolation):

> When running under SERIALIZABLE isolation, SELECT ... FOR UPDATE and SELECT ... FOR SHARE locks should be thought of as best-effort, and should not be relied upon for correctness.

and 

> The desired ordering of concurrent accesses to one or more rows of a table expressed by your use of SELECT ... FOR UPDATE may not be preserved (that is, a transaction B against some table T that was supposed to wait behind another transaction A operating on T may not wait for transaction A).

I pointed it out to my colleague. His first instinct was to open the SQL transaction in the isolation level `READ COMMITTED`, which is the default for PostgreSQL but not for CockroachDB (`SERIALIZABLE` is the default).
That way, `FOR UPDATE` does the expected thing: full exclusive lock, done.


That works... but it has [downsides](/blog/optimization-tales-cockroachdb-part2-slow-logout.html#second-optimization-read-committed). Turns out, the guarantees from `SERIALIZABLE` are really nice. Under weaker isolation levels, a littany of concurrency anomalies will occur, and that's hard to keep the application logic correct. Especially when concurrent transactions with different isolation levels exist in the codebase, possibly touching the same rows.

## An best effort exclusive lock, come again?

Ok, so, if you're like me, you're probably currently reading the quote from the CockroachDB docs again and wondering: wait, what's a 'best effort exclusive lock' ? Why does it even exist? It is either exclusive, or it is not!

Imagine a mutex that *sometimes* works. *Sometimes* it guarantees exclusive access to the shared resource, *sometimes* not. The application would crash and burn very quickly!


The key here is: our SQL statement is running inside a `SERIALIZABLE` transaction. CockroachDB detects read-write or write-write conflicts from other concurrent transactions on the same rows (to simplify a bit[^1]).

If such a conflict happens, CockroachDB guarantees that up to one transaction commits and the others are forced to restart from the top. 

> From : (Second Informal Review Draft) ISO/IEC 9075:1992, Database Language SQL- July 30, 1992: The execution of concurrent SQL-transactions at isolation level SERIALIZABLE is guaranteed to be serializable.
> A serializable execution is defined to be an execution of the operations of concurrently executing SQL-transactions that produces the same effect as some serial execution of those same SQL-transactions.
> A serial execution is one in which each SQL-transaction executes to completion before the next SQL-transaction begins.

There is a serial (i.e. sequential) order of all the transactions that happened in the system, as if their execution never overlapped.

So, this means that, assuming the `SERIALIZABLE` transaction 'touches' all the right rows at the start (by doing a dummy `SELECT` or `UPDATE my_table set id = id ...`), we actually do not need any lock.

But then, why did the CockroachDB even implement `SELECT FOR UPDATE`? Was it just for standard compliance?


## Why does it even exist?

It turns out, there is a real reason. Imagine a concert ticket sale with a thundering herd of a 10 000 concurrent requests that try to all update the same row to buy their ticket when the sale opens, e.g.: 

```sql
BEGIN;

-- Lots of expensive SQL for billing, credit card stuff ...

UPDATE attendants SET count = count + 1 WHERE concert_id = ?;

COMMIT; -- The ticket is bought!
```



Only one transaction commits and all others retry from the start. That's great for correctness: when all transactions (finally) finish, the count will have the right value, no double increments or lost increments can happen. 

However it is potentially very expensive to retry from the start of the transaction (even though CockroachDB can automatically retry the transaction server-side in some cases, transparently to the application). 

This is the definition of contention: it is slow for everyone even though it does not have to be: the operation is not costly in itself.

Instead, if it's possible, we could like to simply wait for our turn to update the row. And that's exactly why `SELECT ... FOR UPDATE` exists in CockroachDB. 

And it's fine if two or more transactions try at the same time: this is just a mitigation strategy to avoid *all* of them from trying at the same time. 

As my colleague put it: if just half of them do wait, that's already a big win for performance.


## Conclusion

`SERIALIZABLE` is great but sometimes expensive as I have written in the past. Sometimes, [making the writes conditional](/blog/optimization-tales-cockroachdb-part2-slow-logout.html) is the answer. 
Other times, [writing to another range than the hot one](/blog/optimization-tales-cockroachdb-part3-slow-list-messages.html) is the solution. And sometimes, a little 'best effort' wait is all you need.


[^1]: I have written about the details [here](http://127.0.0.1:8001/blog/optimization-tales-cockroachdb-part5-uuid.html): CockroachDB observes conflicts on *key intervals*, to be precise.
