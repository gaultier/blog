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


This is done with one SQL query (slightly simplified because the real one has to deal with multi-tenancy):


```sql
SELECT *
 FROM identity_recovery_addresses AS a
   JOIN identity_recovery_addresses AS b
    ON a.identity_id = b.identity_id
   WHERE b.value IN ?
 LIMIT 10
```

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
