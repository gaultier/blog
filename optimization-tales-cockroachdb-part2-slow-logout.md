Title: Optimization tales with CockroachDB: the slow logout
Tags: SQL, Optimization, CockroachDB
---

Quick question: What do you do when there is downtime at work? Read the news, tidy your inbox... Hunt for slow SQL queries?

I'm in the last bucket. Embolded by my last success of speeding up the [password reset flow](/blog/optimization-tales-cockroachdb-part1.html), where too many rows were scanned, I stumbled upon one query that looked so simple, yet was very slow and did *thousands* of retries... This peaked my interest.


## The context
