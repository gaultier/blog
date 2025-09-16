Title: Observe live SQL queries in Go with DTrace
Tags: Go, DTrace
---

*Discussions: [/r/golang](https://old.reddit.com/r/golang/comments/1ne6rvs/observe_live_sql_queries_in_go_with_dtrace/).*

*For a gentle introduction to DTrace especially in conjunction with Go, see my past article: [An optimization and debugging story with Go and DTrace](/blog/an_optimization_and_debugging_story_go_dtrace.html).*

You have a Go program that does *something* with a SQL database. What exactly? You don't know. You'd like to see live what SQL queries are being done.


That's what happened to me at work: I had to tweak an endpoint that does pagination, and the [Go code](https://github.com/ory/kratos/blob/afb43c39e/persistence/sql/persister_courier.go#L36) was using an ORM to build the SQL query, making it really hard to know what was the final query being executed.

Well, this is easy to do with DTrace, without any code modification or even restarting the running program!

And since there were in my case a number of optional query arguments (essentially search parameters), passed as variadic `any` arguments, I was not sure if DTrace could handle that. Well, it turns out it can!

**tl;dr**: We'll use DTrace to peek into the Go runtime's memory, inspect function arguments, and reconstruct the full SQL query with its parameters.

## Level 1: See the SQL query, without arguments

Since most Go programs use the standard library package `database/sql`, we want to observe the function:

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

We can infer that:

- The object (`db *DB`) whose method is being called is passed in the first register: `arg0`
- The first argument (`ctx context.Context`) is an interface, which is 2 pointers, and it's very likely that each field is passed separately in a register: `arg1` and `arg2`
- The second argument (`query string`) is a string, which is a pointer and a length: `arg3` and `arg4`. So here the string is `0x25b` bytes long, or 603 bytes
- Remaining arguments (`args ...any`) are variadic, and Go passes them to the function as a slice (i.e. `[]any`), which is a pointer, a length, and a capacity: `arg5`, `arg6`, `arg7`. Here we observe that there are `4` query arguments. We'll come back to them later.
- Unused registers for passing function arguments are here `arg8` and `arg9`, they should be ignored.


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

Which is already very useful in my opinion. But wouldn't it be nice to also see which arguments were passed to the query?

## Level 2: See string query arguments

This technique is useful for all Go functions that have arguments of type `any`, which is simply an alias for `interface{}`, which is again just two pointers.

Let's go step-wise: first we'll print each `any` argument along with their RTTI (Run-Time Type Information), and the value will be an opaque pointer. In a second step we'll print the value if the RTTI indicates that the argument is in fact a string.

This RTTI is defined by Go as `runtime.Type` [here](https://github.com/golang/go/blob/release-branch.go1.25/src/internal/abi/type.go#L20):

```go
type Type struct {
	Size_       uintptr
	PtrBytes    uintptr // number of (prefix) bytes in the type that can contain pointers
	Hash        uint32  // hash of type; avoids computation in hash tables
	TFlag       TFlag   // extra type information flags
	Align_      uint8   // alignment of variable with this type
	FieldAlign_ uint8   // alignment of struct field with this type
	Kind_       Kind    // enumeration for C
	// function for comparing objects of this type
	// (ptr to object A, ptr to object B) -> ==?
	Equal func(unsafe.Pointer, unsafe.Pointer) bool
	// GCData stores the GC type data for the garbage collector.
	// Normally, GCData points to a bitmask that describes the
	// ptr/nonptr fields of the type. The bitmask will have at
	// least PtrBytes/ptrSize bits.
	// If the TFlagGCMaskOnDemand bit is set, GCData is instead a
	// **byte and the pointer to the bitmask is one dereference away.
	// The runtime will build the bitmask if needed.
	// (See runtime/type.go:getGCMask.)
	// Note: multiple types may have the same value of GCData,
	// including when TFlagGCMaskOnDemand is set. The types will, of course,
	// have the same pointer layout (but not necessarily the same size).
	GCData    *byte
	Str       NameOff // string form
	PtrToThis TypeOff // type for pointer to this type, may be zero
}
```

We thus define the corresponding DTrace types:

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

You might be thinking... that's not very useful... We'll, most fields are indeed not relevant. The only ones to look at, really, are `kind`, which indicates the type, and `size`. This is an enum value defined [here](https://github.com/golang/go/blob/release-branch.go1.25/src/internal/abi/type.go#L55). 

The value `0x18` is `String`. So the last 3 variadic arguments are strings. Great, let's print them:


```dtrace
pid$target::database?sql.*.QueryContext:entry {
  // [...]

  this->go_str1 = (GoString*)copyin((user_addr_t)this->iface1->ptr, sizeof(GoString));
  this->str1 = stringof(copyin((user_addr_t)this->go_str1->ptr, this->go_str1->len));
  printf("str1=%s\n", this->str1);

  this->go_str2 = (GoString*)copyin((user_addr_t)this->iface2->ptr, sizeof(GoString));
  this->str2 = stringof(copyin((user_addr_t)this->go_str2->ptr, this->go_str2->len));
  printf("str2=%s\n", this->str2);

  this->go_str3 = (GoString*)copyin((user_addr_t)this->iface3->ptr, sizeof(GoString));
  this->str3 = stringof(copyin((user_addr_t)this->go_str3->ptr, this->go_str3->len));
  printf("str3=%s\n", this->str3);
}
```

And we see (for example):

```txt
str1=2200-12-31 23:59:59
str2=2200-12-31 23:59:59
str3=00000000-0000-0000-0000-000000000000
```

Alright, pretty nice already.


## Level 3: See array query arguments

The first variadic argument is of `kind = 0x11`, which is `Array`. In Go, `Array` is a fixed-size array which is passed as a pointer only. The size is known by the compiler at build time, and as such does not appear at all at runtime, except in the RTTI. From the RTTI, we see that `size=0x10` so this is an array of 16 elements.

We cannot print the values in the array yet, because we do not know the size of each individual element. To learn that, we have to explore a bit more the RTTI landscape.

The `GoType` we defined at the beginning is just the base type, and Go defines several additional types based on this type (you could say that they are child classes). The one of interest is `ArrayType` defined [here](https://github.com/golang/go/blob/release-branch.go1.25/src/internal/abi/type.go#L271):

```go
type ArrayType struct {
	Type
	Elem  *Type // array element type
	Slice *Type // slice type
	Len   uintptr
}
```

This is pretty easy to map to DTrace:

```dtrace
typedef struct {
  GoType type;
  GoType* elem;
  GoType* slice;
  uintptr_t len;
} GoArrayType;
```

We can now print the RTTI for the element type to finally learn its size. For good measure we can also print the RTTI for the slice type:

```dtrace
  this->go_arr0 = (GoArrayType*)copyin((user_addr_t)this->iface0->rtti, sizeof(GoArrayType));
  print(*(this->go_arr0));
  printf("\n");

  this->go_arr0_elem = (GoType*)copyin((user_addr_t)this->go_arr0->elem, sizeof(GoType));
  print(*(this->go_arr0_elem));
  printf("\n");

  this->go_arr0_slice = (GoType*)copyin((user_addr_t)this->go_arr0->slice, sizeof(GoType));
  print(*(this->go_arr0_slice));
  printf("\n");
```

```text
GoArrayType {
    GoType type = {
        uintptr_t size = 0x10
        uintptr_t ptr_bytes = 0
        uint32_t hash = 0xd84999d2
        uint8_t tflag = 0xf
        uint8_t align = 0x1
        uint8_t field_align = 0x1
        uint8_t kind = 0x11
        void *equal_func = 0x103fdf6a0
        uint8_t *gc_data = 0x103a9ff40
        int32_t name_offset = 0x147f6
        int32_t ptr_to_this = 0x448a00
    }
    GoType *elem = 0x103c1b7a0
    GoType *slice = 0x103be1760
    uintptr_t len = 0x10
}
GoType {
    uintptr_t size = 0x1
    uintptr_t ptr_bytes = 0
    uint32_t hash = 0x6a8c7679
    uint8_t tflag = 0xf
    uint8_t align = 0x1
    uint8_t field_align = 0x1
    uint8_t kind = 0x8
    void *equal_func = 0x103fdf6e8
    uint8_t *gc_data = 0x103a9ff40
    int32_t name_offset = 0x2f98
    int32_t ptr_to_this = 0xd09e0
}
GoType {
    uintptr_t size = 0x18
    uintptr_t ptr_bytes = 0x8
    uint32_t hash = 0x7efbbf65
    uint8_t tflag = 0x2
    uint8_t align = 0x8
    uint8_t field_align = 0x8
    uint8_t kind = 0x17
    void *equal_func = 0
    uint8_t *gc_data = 0x103a90c18
    int32_t name_offset = 0xb4c4
    int32_t ptr_to_this = 0x1062e0
}
```

We discover that the element size is just `0x1`, or 1. So this array is an array of 16 bytes. Time to finally print it with `tracemem` which shows a raw memory dump:

```dtrace
  this->go_str0 = (uint8_t*)copyin((user_addr_t)this->iface0->ptr, 16);
  tracemem(this->go_str0, 16);
```

Which shows:

```txt
             0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
         0: 9c 62 c1 62 20 11 4d 3f 8f dd bc c6 25 73 5c b9  .b.b .M?....%s\.
```

This is in fact a UUID v4 (16 bytes): `9c62c162-2011-4d3f-8fdd-bcc625735cb9`.


## Caveats

- Go sometimes decides to pass arguments to functions using the stack. That makes things much more difficult to print them from DTrace: determining the right stack offset is non-trivial. 
- Go also sometimes inlines functions and eliminates interfaces completely. For example, this program does not use any interface after optimization (as seen in the generated assembly), it simply prints the values directly as if there never were any interfaces at play:
    ```go
    package main

    import (
        "fmt"
    )

    type Bar struct {
        A int
        B string
    }

    func Foo(s string, x uint, bar Bar) []any {
        res := make([]any, 3)
        res[0] = s
        res[1] = x
        res[2] = bar
        return res
    }

    func main() {
        fmt.Println(Foo("foo", 123, Bar{A: 99, B: "bar"}))
    }
    ```
    Which is great for performance, but not so great for introspection using our approach. That means we often have to introspect a different Go function in the call stack.
- the Go `runtime` types we have used could change in the future and break our script.
- Determining how function arguments are passed in Go is machine, ABI, version, and function specific. 


## Print the name of the type from DTrace?

A promising avenue I have not explored is printing the name of the type using the `name_offset` field. That's because Go stores at compile time in the executable this RTTI including the human readable name of all the user defined types.

This data is used when doing `println(reflect.TypeOf(someAnyArgument).Name())`. That prints `uuid.UUID`. 

Useful, but tricky to use from DTrace:

- The linker relocates at link time this data
- PIE makes it nigh impossible to know where this data resides in memory at runtime from run to run
- This data is in fact a linked list, with one node for each package. This linked list must be traversed to find the right node where we can use our offset.
- Finally, the length of the name must be varint decoded which is probably not easy to do in DTrace


## Alternatives

- We could print the network data that is sent and received. With some knowledge of the wire protocol used by each database, we could get a hold of the same information. However that is very tedious, database specific, and does not work with SQLite which does not do network calls.
- We could instrument the database directly, but that is also database specific.

## Conclusion and remaining work

Our final output is pretty helpful:

```txt
3 140734 database/sql.(*DB).QueryContext:entry SELECT courier_messages.body, courier_messages.channel, courier_messages.created_at, courier_messages.id, courier_messages.nid, courier_messages.recipient, courier_messages.send_count, courier_messages.status, courier_messages.subject, courier_messages.template_data, courier_messages.template_type, courier_messages.type, courier_messages.updated_at FROM courier_messages AS courier_messages WHERE nid=? AND ("courier_messages"."created_at" < ? OR ("courier_messages"."created_at" = ? AND "courier_messages"."id" > ?)) ORDER BY "courier_messages"."created_at" DESC, "courier_messages"."id" ASC LIMIT 11

             0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
         0: 9c 62 c1 62 20 11 4d 3f 8f dd bc c6 25 73 5c b9  .b.b .M?....%s\.
str1=2200-12-31 23:59:59
str2=2200-12-31 23:59:59
str3=00000000-0000-0000-0000-000000000000
```


With relatively little work (under 90 lines of DTrace including defining all types), we can inspect live SQL queries in our Go programs.

Furthermore, we have focused on the Go function `QueryContext` in this article, but the exact same can be done for `ExecContext`.

Printing each remaining Go type is left as an exercise to the reader but should be very similar. 


More importantly, this technique to print Go variadic function arguments of type `any` can be applied everywhere, not just in the context of SQL queries.

This is a bit unfortunate that the Go team and community never took an interest in adding static DTrace probes in key places in the Go runtime and standard library, like numerous other programming languages have done. That forces us to do gymnastics to get the information we need. 


Finally, DTrace limitations (no loops, difficult to write complex logic), prevent us from writing a generic D script that can print all arguments of all queries (our script is ad-hoc). Perhaps this is doable with a program that generates the right D script at runtime, possibly tailored to each query in order to print the arguments correctly, and this program also post-processes the DTrace output.

I wonder if eBPF on Linux is more powerful in that regard? I am sure the same approach as outlined in this article can be done with eBPF. 

I fully understand why DTrace is so limited, to be able to run D scripts without fear on mission-critical production systems. However, I often yearn for a 'development' mode where arbitrary logic and control flow are allowed, even if that could crash my system.



## Addendum: the full code

<details>
  <summary>The full code</summary>

```dtrace
#!/usr/sbin/dtrace -s

#pragma D option strsize=16K

typedef struct {
  uint8_t* ptr;
  size_t len;
} GoString; 

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


typedef struct {
  GoType type;
  GoType* elem;
  GoType* slice;
  uintptr_t len;
} GoArrayType;

pid$target::database?sql.*.QueryContext:entry {
  this->query = stringof(copyin(arg3, arg4)); // Query string.
  printf("%p %d\n", arg5, arg6);

  this->args_ptr = arg5;

  printf("%s\n", this->query);
  this->iface0 = (GoInterface*) copyin(this->args_ptr, sizeof(GoInterface));
  this->rtti0 = (GoType*) copyin((user_addr_t)this->iface0->rtti, sizeof(GoType));
  print(*(this->rtti0));
  printf("\n");

  this->go_arr0 = (GoArrayType*)copyin((user_addr_t)this->iface0->rtti, sizeof(GoArrayType));
  print(*(this->go_arr0));
  printf("\n");

  this->go_arr0_elem = (GoType*)copyin((user_addr_t)this->go_arr0->elem, sizeof(GoType));
  print(*(this->go_arr0_elem));
  printf("\n");

  this->go_arr0_slice = (GoType*)copyin((user_addr_t)this->go_arr0->slice, sizeof(GoType));
  print(*(this->go_arr0_slice));
  printf("\n");

  this->go_str0 = (uint8_t*)copyin((user_addr_t)this->iface0->ptr, 16);
  tracemem(this->go_str0, 16);

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

  this->go_str1 = (GoString*)copyin((user_addr_t)this->iface1->ptr, sizeof(GoString));
  this->str1 = stringof(copyin((user_addr_t)this->go_str1->ptr, this->go_str1->len));
  printf("str1=%s\n", this->str1);

  this->go_str2 = (GoString*)copyin((user_addr_t)this->iface2->ptr, sizeof(GoString));
  this->str2 = stringof(copyin((user_addr_t)this->go_str2->ptr, this->go_str2->len));
  printf("str2=%s\n", this->str2);

  this->go_str3 = (GoString*)copyin((user_addr_t)this->iface3->ptr, sizeof(GoString));
  this->str3 = stringof(copyin((user_addr_t)this->go_str3->ptr, this->go_str3->len));
  printf("str3=%s\n", this->str3);
}
```

</details>

