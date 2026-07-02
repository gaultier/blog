Title: Optimization tales with CockroachDB: the slow dequeue of messages (part 4)
Tags: SQL, Optimization, CockroachDB
---

In the [last part](/blog/optimization-tales-cockroachdb-part3-slow-list-messages.html), we optimized listing all messages in the table of messages `courier_messages`. This is a big table with millions of rows and a lot of churns: this is where all SMS or emails notifications are stored before being sent. It is essentially a work queue. And there is one problem: way too many retries to get the next messages to work on:

![Too many retries](crdb_slow4_4.png)
