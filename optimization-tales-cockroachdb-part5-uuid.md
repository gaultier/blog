Title: Optimization tales in production with CockroachDB: UUID generation
Tags: SQL, Optimization, CockroachDB
---

*This is part of a series of optimization tales using CockroachDB.*

I am ashamed to say that I discovered this trick very recently. It's so simple and yet powerful, and it's the building block for more optimizations I did later. 


So here it is: in [Kratos](https://github.com/ory/kratos), all database ids are UUIDs v4, meaning: 16 random bytes. And most of our tables are `REGIONAL BY ROW`, meaning: the data is sharded by region, and we can ensure at the database level that data for an entity (including all JOIN tables!) is located in one region. Which is fantastic for compliance and regulatory reasons!

Since an id has to be unique (this is the primary key in the table), the naive way to check unicity in a multi-region setup, when `INSERT`-ing a new entry, is to ask each region (in parallel): do you know this (uu)id already? If all of them reply with 'no', then we are good and we can use it for a new entry. 

You might be wondering: isn't there a TOCTOU window here? Meaning: could a remote region use this ID for an `INSERT` right after telling us it is not yet used, thus racing with our own `INSERT`? Well, thankfully no: an write in CockroachDB first sends a write intent to other regions (when there is a unique constraint on a field in the record), saying "I would like to do this write, is that ok?", and only then it tries to write the record, and the write is committed only when all regions have confirmed there is no constraint violation.

An astute reader may now be asking: well, if this value is 16 random bytes, there is essentially no chance of collision, it is unique by construction given a good enough random number generator, so why both asking the other regions, when the answer will be in 99.9999999[...]9999% of the cases: 'this id is unknown to me'?

Indeed, and that's why we use UUIDs (v4) in the first place, for unicity by construction. 

The good news is, CRDB developers know that and for this reason, they provide the built-in function `gen_random_uuid()` which, as you might expect, generates a UUID v4, but more importantly, *skips* the unique check for this field. That's huge: it means that if there are no other unique constraints on the table, we now can do an `INSERT` in multi-region mode *instantly*, without contacting the remote regions at all! 

Note that we are taking a (minuscule) risk: if there is indeed, by some massive cosmic bad luck, truly a collision between UUIDs, we would not notice it. But that chance is so mathematically unprobable, that this is a tradeoff we are willing to do.


So, the best example of this optimization taking place is this [change](TODO) where a very frequent `INSERT` went from ~250 ms (typical latency between regions) to ~5ms, simply by moving the UUID generation from the application to the database:

![Latency histogram](crdb_opt_5.png)


