| Feature / Database kind | SQLite | MySQL | PostgreSQL | CRDB |
| :--- | :--- | :--- | :--- | :--- |
| **RETURNING clause e.g.: `INSERT ... RETURNING id`** | ✅ | ❌ : must do two separate queries, preferably in the same transaction | ✅ | ✅ |
| **WITH (<data modifying clause>) … e.g.: `WITH (UPDATE ...) SELECT ...`** | ❌ | ❌ : must do two separate queries, preferably in the same transaction | ✅ | ✅ |
| **Partial index: `CREATE INDEX ... ON ... WHERE <condition>`** | ✅ | ❌ : must create the index for all table rows | ✅ | ✅ |
| **Empty list: `WHERE id IN ()`** | ✅ | ❌ : must transform the query in something equivalent e.g. `WHERE FALSE` or elide the condition completely | ❌ : must transform the query in something equivalent e.g. `WHERE FALSE` or elide the condition completely | ✅ |
| **Quoting syntax for table or columns that can clash with keywords e.g.: a column named `group` clashes with the keyword `group by`** | `` `table`.`field` `` | `` `table`.`field` `` | `"table.field"` | `"table.field”` |
| **`CREATE|DROP INDEX ... IF NOT EXISTS`** | ✅ | ❌ | ✅ | ✅ |
| **`ON CONFLICT DO NOTHING`** | ✅ | ❌ : must use a work-around: `ON DUPLICATE KEY UPDATE id=id` | ✅ | ✅ |
| **UUID type supported natively** | ✅ | ❌ : must use `varchar(36)` and perform validation in the application | ✅ | ✅ |
| **Zero value for `time.Time` is accepted for a timestamp column** | ✅ | ❌ : must provide a value e.g. `time.Now()` | ✅ | ✅ |
| **Type for a JSON column** | `JSONB` | `JSON` | `JSON` or `JSONB` | `JSON` or `JSONB` |
| **Type for a boolean column** | `BOOL` | `TINYINT(1)` | `BOOL` | `BOOL` |
| **Go driver requires a slash in the DSN between the hostname and the query parameters in case of an empty database name** | No | Yes (mysql://root:secret@tcp(127.0.0.1) is invalid: must be `mysql://root:secret@tcp(127.0.0.1)/` , note the trailing slash) | No | No |
| **Multi-region** | ❌ | ❌ | ❌ | ✅ |
| **Time to apply all migrations (a longer time entails having to use a database backup in practice)** | < 0.1s | < 10s (can be sped up with a trick to <1s) | < 0.1s | < 10m |
| **Nested transactions that write data** | ❌ : deadlocks | ✅ | ✅ | ✅ |
| **May have to retry in the application a transaction that deadlocked** | ❌ : only one writer is allowed so this cannot happen | ✅ | ? : need to investigate | ✅ : the database retries automatically some errors but the client has to retry some errors |
