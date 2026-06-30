Title: Optimization tales with CockroachDB: the slow logout
Tags: SQL, Optimization, CockroachDB
---

Quick question: What do you do when there is downtime at work? Read the news, tidy your inbox... Hunt for slow SQL queries?

I'm in the last bucket. Embolded by my last success of speeding up the [password reset flow](/blog/optimization-tales-cockroachdb-part1.html), where too many rows were scanned, I stumbled upon one query that looked so simple, yet was very slow and did *thousands* of retries... This peaked my interest.


## The context

150K retries, 500 ms statement time:

![Statement statistics](crdb_slow_logout2.png)


Contrary to part 1, this time, rows scanned and CPU time are completely fine. But there are way too many retries. 


This query (and many variations of it, all suffering from the same issue), are executed when logging out a user. The session tokens are stored in the `sessions` table by setting `active` to false:

```sql
UPDATE sessions SET active = false WHERE token = ?`
```

This is such a simple query. It uses the right index. And yet it misbehaves. Time to investigate!


## Investigation


CockroachDB gives us a nice warning on the same page:

```plaintext
Error Code: 40001

Error Message: TransactionRetryWithProtoRefreshError: ReadWithinUncertaintyIntervalError: read at time 1781712863.936725360,0 encountered previous write with future timestamp 1781712863.957518522,0 within uncertainty interval 't<= (local=1781712864.036725360,0, global=1781712864.036725360,0)'; observed timestamps: [{25 1781712864.055700939,0} {62 1781712864.179153461,0} {63 1781712863.936725360,0}]: "sq| txn" meta={id=920488bd key=/Min iso=Serializable pri=0.00655660 epo=0 ts=1781712863.936725360,0 min=1781712863.936725360,0 seq=0} lock=false stat=PENDING
```

If it looks like gibberish to you... Know that it did to me initially. Let's unpack it slowly:

- `TransactionRetryWithProtoRefreshError`: the transaction was aborted by the server and the client was instructed to retry. We'll see why in a second. This is completely expected and normal behavior in CockroachDB in the default isolation level (Serializable).
- `ReadWithinUncertaintyIntervalError: read [...] encountered previous write`: We tried to read the row containing the session token, in order to write to it, but another write happened concurrently on this row, and won the race against us. So we have to restart from the top: re-read the fresh row data.


At that time I am wondering: how can I confirm that there are multiple writes on the same row? Fortunately there is an internal table in CockroachDB that contains exactly this information, `crdb_internal.transaction_contention_events`, and it tracks for each table the contention events:

```sql

SELECT encode(blocking_txn_fingerprint_id, 'hex')  AS blocking_txn,                                                                                                                                                                                       
       encode(waiting_txn_fingerprint_id, 'hex')   AS waiting_txn,                                                                                                                                                                                      
       encode(waiting_stmt_fingerprint_id, 'hex')  AS waiting_stmt,                                                                                                                                                                                     
       contention_type,                                                                                                                                                                                                                                    
       count(*)                                    AS events,                                                                                                                                                                                              
       sum(contention_duration)                    AS total_blocked,                                                                                                                                                                                       
       min(collection_ts)                          AS first_seen,                                                                                                                                                                                          
       max(collection_ts)                          AS last_seen                                                                                                                                                                                            
FROM crdb_internal.transaction_contention_events                                                                                                                                                                                                           
WHERE table_name = 'sessions'                                                                                                                                                                                                                              
GROUP BY 1,2,3,4                                                                                                                                                                                                                                           
ORDER BY events DESC                                                                                                                                                                                                                                       
LIMIT 20;                                                                                                                                                                                                                                                  
  blocking_txn  |  waiting_txn  | waiting_stmt  | contention_type | events |  total_blocked  |          first_seen           |           last_seen
-------------------+------------------+------------------+-----------------+--------+-----------------+-------------------------------+--------------------------------
  8485e... | 848... | 2be... | LOCK_WAIT       |   5897 | 00:00:22.35228  | 2026-05-29 17:11:19.015611+00 | 2026-06-18 08:39:00.112332+00
  00000... | 000... | 2be... | LOCK_WAIT       |   2659 | 00:00:03.555509 | 2026-06-04 15:31:28.400474+00 | 2026-06-16 14:38:08.950834+00
  467d8... | 467... | 44d... | LOCK_WAIT       |    756 | 00:00:02.879378 | 2026-05-29 18:14:51.876694+00 | 2026-06-18 08:30:30.21639+00
  [...]
```

We see a lot of contention events of type `LOCK_WAIT` over a long period of time. So this is not due to some weird DoS attack or load test, this is due to normal traffic, and we need to address it.

By the way, CockroachDB has even [more statistics](https://www.cockroachlabs.com/docs/stable/monitor-and-analyze-transaction-contention) about contention, but in our case we know enough already.


## But why?


In a nutshell: we do an unconditional write to the row. It might not look like it because there is a `WHERE` clause. But this `WHERE` clause is actually only used to find the one row to update (using the session token). Once we have found the row, we write `active = false` to it *every time*. Even if `active` is already false! We do not check `active` at all. So two or more concurrent writes will all compete on the same row. 

Due to the implicit transaction wrapping our update, using the default isolation level `SERIALIZABLE` (the strictest), we fall victim to 'read refreshing'.


Quoting the [docs](https://www.cockroachlabs.com/docs/v26.2/performance-best-practices-overview#transaction-contention):

> By default under SERIALIZABLE isolation, transactions that operate on the same index key values (specifically, that operate on the same column family for a given index key) are strictly serialized. To maintain this isolation, SERIALIZABLE transactions refresh their reads at commit time to verify that the values they read were not subsequently updated by other, concurrent transactions. If read refreshing is unsuccessful, then the transaction must be retried.

But this is wasted work, because the first write to succeed is enough: once we have marked the row as `active = false`, no code ever toggles `active` back to `true`, this is a final state. So all subsequent writes to this row should be no-ops, instead of retrying a number of times, and finally succeeding, having achieved *nothing*!

## First optimization: conditional write

So, let's make the write conditional: we'll only write to the row if `active` is `true`:


```sql
UPDATE sessions SET active = false WHERE active = true AND token = ?
```


There is no semantic change, and yet, this means way fewer writes (up to 1 now) and fewer retries. Why? Because [transaction conflicts](https://www.cockroachlabs.com/docs/v26.2/architecture/transaction-layer#transaction-conflicts) in CockroachDB happen in two cases, write-write and write-read:

> CockroachDB's transactions allow the following types of conflicts that involve running into a write intent:
>
>    Write-write, where two transactions create write intents or acquire a lock on the same key.
>    Write-read, when a read encounters an existing write intent with a timestamp less than its own. 

We now avoid the write-write scenario, yay! We still have the other case than can happen: 

1. Transaction A starts
1. Transaction B starts
1. Transaction A writes to the row
1. Transaction A commits
1. Transaction B reads from the row => write-read conflict


Let's tackle that now.

## Second optimization: read committed

CockroachDB supports (only) two isolation levels: `SERIALIZABLE` (the default) and `READ COMMITTED`. The latter can unlock some performance (meaning: drastically reduce retries) at the cost of [concurrency anomalies](https://www.cockroachlabs.com/docs/v26.2/read-committed#concurrency-anomalies). If these anomalies are acceptable, then we could use that level. Per the [docs](https://www.cockroachlabs.com/docs/v26.2/read-committed):

> READ COMMITTED isolation is appropriate in the following scenarios:
>    Your application needs to maintain a high workload concurrency with minimal transaction retries, and it can tolerate potential concurrency anomalies. Predictable query performance at high concurrency is more valuable than guaranteed transaction serializability.

Let's see what these anomalies are:

- Non-repeateable reads: "Non-repeatable reads return different row values because a concurrent transaction updated the values in between reads:". Since we do not do more than one read, we are not affected by that.
- Phantom reads: "Phantom reads return different rows because a concurrent transaction changed the set of rows that satisfy the row search": we do not write to any of the columns included in the `WHERE` criteria, these are constant (e.g. the session token). So we are not affected by that either.
- Lost update anomaly: "The READ COMMITTED conditions that permit non-repeatable reads and phantom reads also permit lost update anomalies, where an update from a transaction appears to be "lost" because it is overwritten by a concurrent transaction". We do not care of that because as long as one write `active = false` on the row succeeds, all subsequent writes are irrelevant. In fact, this is our goal: to avoid redundant updates.
- Write skew anomaly: "two concurrent transactions each read values that the other subsequently updates". We also are fine with that: in the worst case, our transaction will read `active` as `true` when another concurrent transaction has already set it to `false`, and our transaction will do one redundant write. Completely fine.

Ok, so let's do it:


```sql
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;

UPDATE sessions SET active = false WHERE active = true AND token = ?;

COMMIT;
```

## Final results

- p99 was halved (from ~2.2s to 1.1s) 
- p95 went from ~1.6s to 1s
- Overall all latencies decreased and the extremes are less extreme (as expected)
- Contention and CPU time of the query went to essentially 0
