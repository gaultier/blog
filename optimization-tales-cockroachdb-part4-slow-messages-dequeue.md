Title: Optimization tales with CockroachDB: the slow dequeue of messages (part 4)
Tags: SQL, Optimization, CockroachDB
---

In the [last part](/blog/optimization-tales-cockroachdb-part3-slow-list-messages.html), we optimized listing all messages in the table of messages `courier_messages`. This is a big table with millions of rows and a lot of churns: this is where all SMS or emails notifications are stored before being sent. It is essentially a work queue. And there is one problem: way too many retries to get the next messages to work on:

![Too many retries](crdb_slow4_1.png)

Worse: the failure count is identical to the number of retries:

![Too many failures](crdb_slow4_2.png)


In CockroachDB, the default number of retries for a transaction is 50, after which an error is returned. So we retry to the fullest, and to no avail. This also means that the tail latencies (p95, p99) are really inflated.


All other metrics look fine. Weird. Time to investigate.

## Context

As mentioned, this table acts as a work queue. The simplified schema is:

```sql
CREATE TABLE public.courier_messages (
  id UUID NOT NULL,
  status INT8 NOT NULL
  
  -- [...] 
);
```

And new messages are added with the status `queued`.

So to get the next batch of messages to deliver, the query does:

```sql
SELECT *
FROM courier_messages
WHERE status = 'queued'
ORDER BY id ASC
LIMIT 100
```

I do not remember what the exact limit is, but it's something like that.


It's pretty simple, and from the plan and metrics, we notice that the correct index is used. 

So why so many retries?


## Investigation





