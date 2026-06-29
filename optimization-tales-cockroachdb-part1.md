Title: Optimization tales with CockroachDB, part 1
Tags: SQL, Optimization, CockroachDB
---

It all started one morning, I opened Slack as usual to start my working day, only to find a message from the CTO:

> Hello this query reads like 700k rows

To engineers replying:

> The plan looks ok IMHO, indices are being used:
> <plan>

And the CTO answering:

> This query should not read 700k rows, but like 10 or something.


And spoiler alert, he was dead right. I started to look into it and this turned out much more interesting than just 'it was a full table scan, I added the right index and moved on'.


As always, the code, and fix, are [open-source](https://github.com/ory/kratos/commit/c445e40e077ef5aeeedd6642830aba4fc6e36845)!

This query actually runs against all 4 databases we support (SQLite, PostgreSQL, MySQL, CockroachDB), but here I will focus on CockroachDB because that is the database that we run in production and because it has important differences, which make this investigation interesting.


## The context

The software is [Kratos](https://github.com/ory/kratos), a widely used authentication and identity management service. Users of this software are humans. Humans often register with an email and password (Kratos also supports passwordless schemes such as passkeys, webauthn, etc, but the proverbial email+password approach remains very much in use). Humans also tend to forget their password. That's why Kratos like any identity management service worth its salt, supports password recovery. 

The user enter one of their addresses (email, phone number, etc), and if this address is in the system, a list of masked addresses is shown to them, they pick one, and a recovery link or code is sent to them on that address. Using that link or code, they can setup a new password. Pretty standard.


This is done with one SQL query (slightly simplified from the real one):


```sql
SELECT *
 FROM identity_recovery_addresses AS a
   JOIN identity_recovery_addresses AS b
    ON a.identity_id = b.identity_id
    AND a.nid = b.nid
   WHERE b.value = ?
    AND a.nid = _
```

*Kratos supports multi-tenancy, so each tenant as an id called `nid`, each row stores `nid`, and each query clause contains `WHERE nid = ?` to isolate each tenant. But you can ignore that for this article.*

The approach is relatively straightforward with a self-join:

- Given the provided address, for example `foo@bar.com`, find the identity (i.e. the user account) for it.
- Now that we have the identity id, find all addresses for that identity.
- Return the list of addresses for that identity (up to 10, we do not expect a user to have more than a handful).

Now, Kratos can show the list of masked addresses e.g. `+15234****56` if it's a phone number, or `foo@****.com` if it's an email address. The masking logic is pretty smart so accidental information disclosure is avoided. Kratos also pretends to send the recovery link/code to a non-existing address, so that it's not possible for an attacker to probe a website for certain addresses. The last point can actually have real life consequences in certain countries for certain websites, e.g. LGBT ones. 

(Always remember that your code can impact lives).


Anyways, I am the one who actually wrote this query some months ago and I remember confirming in production that the query plan was sensical.

Now, the query is impacting the application and the database with its bad performance. It's not clear to me if:
- the performance was always subpar and no one noticed, or 
- some characteristics about the data in this table changed, which made the optimizer pick a different, worse plan, or
- the performance was originally okay, but the table grew over time and performance slowly deteriorated until it was unbearable


In any event: time to fix it.


## Investigation

### Statistics 

The CTO actually linked in its original message a link to the statement in the CockroachDB dashboard, which shows very surprising statistics:

![Statement statistics](crdb_recovery_addresses_1.png)


| Metric | Value |
|---|---|
| Failure Count | 294 |
| Full scan? | No |
| Vectorized execution? | Yes |
| Transaction type | Implicit |
| Statement Time | 1.1 s (Execution: 1.1 s / Planning: 1.1 ms) |
| Rows Processed | 837.3 k Reads / 0 Writes |
| Execution Retries | 293 |
| Execution Count | 27.3 k |
| Contention Time | 14.0 ms |
| SQL CPU Time | 144.4 ms |
| Client Wait Time | 0.0 ns |




![Statement charts](crdb_recovery_addresses_2.png)




| Chart | Series | Approx. Peak | Notes |
|---|---|---|---|
| Statement Times | Execution + Planning | ~1.5 s | Mostly 1.0–1.3 s; activity ~06/07–06/15 |
| Rows Processed | Rows Read / Rows Written | ~2 m (spike) | Baseline ~0.5 m–1 m reads; one spike near 06/12 |
| Execution Retries | Retries | ~11 | Bursts of 4–11 between 06/07–06/15 |
| Execution Count | Execution Counts | ~380 | Periodic bursts, ~100–380 per interval |
| Contention Time | Contention (ms) | ~72 ms | Spiky, ranging ~20–72 ms |
| SQL CPU Time | CPU (s) | ~1.3 s (spike) | Baseline ~0.2–0.4 s |


Immediately the metrics that jump out to me (and to my CTO) are:

- Rows read: millions. This is simply not tenable, as mentioned, we expect ~10.
- SQL CPU time: 144ms: Normal queries take <1ms in CPU. This shows that a lot of rows are loaded in memory and processed somehow. This is also not scalable.

The other metrics are interesting but less important at the moment. For example, there is a relatively large number of retries and contention time. They probably are a by-product of the millons of rows scanned. Since looking for all recovery addresses of one identity (i.e. user) scans (but does not return) unrelated rows, it creates unintentional, and unneeded, contention on these rows.






The next step is to inspect the plan being used in production using `EXPLAIN ANALYZE <query>`, or for even more details: `EXPLAIN ANALYZE (debug) <query>`.

### Plan

The first thing the plan does is this:

```plaintext
 table: identity_recovery_addresses@identity_recovery_addresses_status_via_uq_idx  
 spans: [/'gcp-asia-northeast1'/'000e377a-062c-45b1-961c-1b28d682df6a' - /'gcp-asia-northeast1'/'000e377a-062c-45b1-961c-1b28d682df6a'] [/'gcp-europe-west3'/'000e377a-062c-45b1-961c-1b28d682df6a' - /'gcp-europe-west3'/'000e377a-062c-45b1-961c-1b28d682df6a'] [/'gcp-us-east4'/'000e377a-062c-45b1-961c-1b28d682df6a' - /'gcp-us-east4'/'000e377a-062c-45b1-961c-1b28d682df6a'] [/'gcp-us-west2'/'000e377a-062c-45b1-961c-1b28d682df6a' - /'gcp-us-west2'/'000e377a-062c-45b1-961c-1b28d682df6a']
```

We see that it is using the right index `identity_recovery_addresses_status_via_uq_idx (nid ASC, via ASC, value ASC)`. We see that it fans-out to every region: asia, europe, us, etc. This is expected: we originally do not know in which region the identity is stored, so we have to do that.


But there is a problem. Can you spot it? Unless you are an advanced CockroachDB user, I'd be surprised if you do. I know I did not spot anything at first.



I'll explain the plan is layman terms. This is what the query planner is doing, when a user enters `foo@bar.com` in the recovery screen:

1. Fan out to each region (this is fine and required). In each region:
  1. Find the row with the address `foo@bar.com`. It is linked to an identity (`identity_id`) and a tenant (`nid`).
  1. Load all rows for this tenant in memory
  1. Filter these rows where the identity_id is the one found in step 1.1. Throw out the rest.

So each time a user wants to reset their password, we load all recovery addresses of all users for this tenant, in memory. That is really not great and becomes worse and worse over time. 

This explains the high latency and CPU usage!


### Why?


In CockroachDB, indexes are a tuple, e.g. `(nid, via, value)`. Conceptually, this is how the index looks like:


![Index](crdb_index.svg)


To use it the most efficiently, we specify all the fields, e.g.: `WHERE nid = '1' AND via = 'email' AND value = zzz@accounting.com`. The database can then do a 'point lookup', meaning trace a path from the root of the index to a leaf (i.e. a row):


![Point lookup](crdb_index2.svg)


Only one row is scanned, this is optimal. 

What happens then when only the first field in the tuple is provided, for example, only the `nid`? Well, the path in the index is very short, and all nodes underneath (the whole subtree) must be scanned and inspected:

![Only one tuple field provided](crdb_index4.svg)

This is what happens to use, and it is very wasteful.

But wait, there's more: the order of the fields in the tuple also matters. CockroachDB recommends for performance to have the most discriminating fields first .


Here, `nid` is the first field of the tuple, meaning: the tenant id. This is fine, because by providing the tenant id, we automatically avoid scanning rows from other tenants. Then, the second field of the index is `via`, which an enum of two values: `email` (in case the address is an email address) or `sms` (a phone number). This column is unfortunately not very discriminating: if each user has signed up with both an email and a phone number, simply providing `email` only eliminates 50% of the rows - ok, not great.

And finally the last field of the index is `value`, which is the address itself. It is very discriminating: by providing the address, we eliminate every row in the table, except one.

One reason: we did not provide one of the fields in the tuple of the index: `via`.

So the query planner decided to use this index, this tuple of `(nid ASC, via ASC, value ASC)`, but it only knows the first value, `nid` (the tenant). So it can only eliminate rows from other tenants. For a tenant with many many users, performance is really bad. It's not quite a full table scan, but it's a full *tenant* table scan!



