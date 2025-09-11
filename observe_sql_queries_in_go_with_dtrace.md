Title: Observe live SQL queries in Go with DTrace
Tags: Go, DTrace
---

You have a Go program that does *something* with a SQL database. What exactly? You don't know. You'd like to see live what SQL queries are being done.

Well, this is easy to do with DTrace!

## Level 1: See the SQL query, without arguments

Since most Go programs use the standard library package `database/sql`, we'll observe the function:

```go
func (db *DB) QueryContext(ctx context.Context, query string, args ...any) (*Rows, error)
```

As a first step, we'll only print the query string. In some cases, that's sufficient, for example if the query is `SELECT * from users`: there are no arguments.

The only challenge here is to know which registers the string is passed in. One good resource is the [Go ABI documentation](https://github.com/golang/go/blob/master/src/cmd/compile/abi-internal.md).

Alternatively, we can brute-force our way by first printing all registers and inferring from their value what is what:

```dtrace
pid$target::database?sql.*.QueryContext:entry {
  printf("%p %p %p %p %p %p %p %p %p\n", arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
}

```

And we'll see something like this when the application being observed does a SQL query:

```txt
dtrace: description 'pid$target::database?sql.*.QueryContext:entry ' matched 4 probes
CPU     ID                    FUNCTION:NAME
 12 140734 database/sql.(*DB).QueryContext:entry 140023cea90 1024bce78 14002519b30 140017c5400 25b 14000e08b40 4 4 100bf02ac 2
```

From the Go ABI documentation, we can infer that:

- The object whose method is being called (`db *DB`) is passed in the first register: `arg0`
- The first argument (`ctx context.Context`) is an interface, which is 2 pointers, and it's very likely that each field is passed in a register: `arg1` and `arg2`
- The second argument (`query string`) is a string, which is a pointer and a length, and it's very likely that each field is passed in a register: `arg3` and `arg4`. So here the string is `0x25b` bytes long, or 603 bytes
- Remaining arguments (`args ...any`) are variadic, and Go passes them to the function as a slice (i.e. `[]any`), which is a pointer, a length, and a capacity, and it's very likely that each field is passed in a register: `arg5`, `arg6`, `arg7`. Here there are `4` query arguments. We'll ignore them for now.


So let's for now only print the query string:

```dtrace
pid$target::database?sql.*.QueryContext:entry {
  this->query = stringof(copyin(arg3, arg4)); 

  printf("%s\n", this->query);
}
```

An important note: since the string is quite long, we should pass to the DTrace command `-x strsize=16K` or use the pragma `#pragma D option strsize=16K` to allocate more memory for temporary strings. Otherwise DTrace will truncate our string.

We see something like this:

```sql
SELECT courier_messages.body, courier_messages.channel, courier_messages.created_at, courier_messages.id, courier_messages.nid, courier_messages.recipient, courier_messages.send_count, courier_messages.status, courier_messages.subject, courier_messages.template_data, courier_messages.template_type, courier_messages.type, courier_messages.updated_at FROM courier_messages AS courier_messages WHERE nid=? AND ("courier_messages"."created_at" < ? OR ("courier_messages"."created_at" = ? AND "courier_messages"."id" > ?)) ORDER BY "courier_messages"."created_at" DESC, "courier_messages"."id" ASC LIMIT 11
```

Which is already very useful in my opinion.

## Level 2: See query arguments

This technique is useful for all Go functions that have arguments of type `any`, which is simply an alias for `interface{}`, which is again just two pointers.

Let's go step-wise: first we'll print each `any` argument along with their RTTI information, and the value will be an opaque pointer. In a second step we'll print the value.

We create corresponding DTrace types for Go's `runtime.Type` as defined [here](https://github.com/golang/go/blob/release-branch.go1.25/src/internal/abi/type.go#L20).

```dtrace
typedef struct {
  uintptr_t size;  
  uintptr_t ptr_bytes;  
  uint32_t hash;
  uint8_t tflag;
  uint8_t align;
  uint8_t field_align;
  uint8_t kind;
  void* equal_func;
  uint8_t* gc_data;
  int32_t name_offset;
  int32_t ptr_to_this;
} GoType;

typedef struct {
  GoType* rtti;
  void* ptr;
} GoInterface;
```

We can then print the RTTI information of each of the four `any` arguments passed. Since DTrace intentionally does not have loops, we do that manually.
The only challenge is to remember to use `copyin`, since our DTrace script executes in kernel space but we are inspecting user-space memory.

```dtrace
pid$target::database?sql.*.QueryContext:entry {
  this->query = stringof(copyin(arg3, arg4)); // Query string.
  printf("%s\n", this->query);

  this->args_ptr = arg5;

  this->iface0 = (GoInterface*) copyin(this->args_ptr, sizeof(GoInterface));
  this->rtti0 = (GoType*) copyin((user_addr_t)this->iface0->rtti, sizeof(GoType));
  print(*(this->rtti0));
  printf("\n");

  this->iface1 = (GoInterface*) copyin(this->args_ptr + 1*sizeof(GoInterface), sizeof(GoInterface));
  this->rtti1 = (GoType*) copyin((user_addr_t)this->iface1->rtti, sizeof(GoType));
  print(*(this->rtti1));
  printf("\n");

  this->iface2 = (GoInterface*) copyin(this->args_ptr + 2*sizeof(GoInterface), sizeof(GoInterface));
  this->rtti2 = (GoType*) copyin((user_addr_t)this->iface2->rtti, sizeof(GoType));
  print(*(this->rtti2));
  printf("\n");

  this->iface3 = (GoInterface*) copyin(this->args_ptr + 3*sizeof(GoInterface), sizeof(GoInterface));
  this->rtti3 = (GoType*) copyin((user_addr_t)this->iface3->rtti, sizeof(GoType));
  print(*(this->rtti3));
  printf("\n");
}
```

And we see:

```txt
GoType {
    uintptr_t size = 0x10
    uintptr_t ptr_bytes = 0
    uint32_t hash = 0xd84999d2
    uint8_t tflag = 0xf
    uint8_t align = 0x1
    uint8_t field_align = 0x1
    uint8_t kind = 0x11
    void *equal_func = 0x1024976c0
    uint8_t *gc_data = 0x101f57c38
    int32_t name_offset = 0x147f6
    int32_t ptr_to_this = 0x448a20
}
GoType {
    uintptr_t size = 0x10
    uintptr_t ptr_bytes = 0x8
    uint32_t hash = 0xf88732b8
    uint8_t tflag = 0x7
    uint8_t align = 0x8
    uint8_t field_align = 0x8
    uint8_t kind = 0x18
    void *equal_func = 0x1024976a0
    uint8_t *gc_data = 0x101f48910
    int32_t name_offset = 0x8c14
    int32_t ptr_to_this = 0xd0960
}
GoType {
    uintptr_t size = 0x10
    uintptr_t ptr_bytes = 0x8
    uint32_t hash = 0xf88732b8
    uint8_t tflag = 0x7
    uint8_t align = 0x8
    uint8_t field_align = 0x8
    uint8_t kind = 0x18
    void *equal_func = 0x1024976a0
    uint8_t *gc_data = 0x101f48910
    int32_t name_offset = 0x8c14
    int32_t ptr_to_this = 0xd0960
}
GoType {
    uintptr_t size = 0x10
    uintptr_t ptr_bytes = 0x8
    uint32_t hash = 0xf88732b8
    uint8_t tflag = 0x7
    uint8_t align = 0x8
    uint8_t field_align = 0x8
    uint8_t kind = 0x18
    void *equal_func = 0x1024976a0
    uint8_t *gc_data = 0x101f48910
    int32_t name_offset = 0x8c14
    int32_t ptr_to_this = 0xd0960
}
```

