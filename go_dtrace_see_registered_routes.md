Title: What HTTP routes does your application provide?
Tags: Go, DTrace
---

Quick one today. I have a [Go HTTP application](https://github.com/ory/kratos) that has many HTTP routes. These routes get dynamically registered at startup based on feature flags, configuration, whether an enterprise license was found, etc. So it's hard to know what routes exist. 

Since this application uses the Go HTTP router from the standard library, this question is quickly answered by DTrace. The two Go functions of interest are located in the `net/http` package:

```go
func (mux *ServeMux) HandleFunc(pattern string, handler func(ResponseWriter, *Request))

func HandleFunc(pattern string, handler func(ResponseWriter, *Request)) 
```

We are interested only in the first argument (`pattern`), which gets passed as a pointer in `arg1` and length in `arg2`.
And so the DTrace invocation is:

```sh
$ sudo dtrace -n 'pid$target::net?http*HandleFunc:entry {printf("%s\n", stringof(copyin(arg1, arg2)));}' -c "./kratos serve -c $HOME/.kratos.yml --dev" -x strsize=16K -b 4G -q
```

And we see no less than 248 HTTP routes are registered, and we also see the HTTP method for each route (that's how the Go API works, the method is passed in the same string as the route):

```text
GET /self-service/login/browser
GET /self-service/login/browser/{$}
GET /self-service/login/api
GET /self-service/login/api/{$}
GET /self-service/login/flows
GET /self-service/login/flows/{$}
POST /self-service/login
POST /self-service/login/{$}
[...]
GET /admin/identities/{id}/sessions/{$}
DELETE /admin/identities/{id}/sessions
DELETE /admin/identities/{id}/sessions/{$}
PATCH /admin/sessions/{id}/extend
PATCH /admin/sessions/{id}/extend/{$}
DELETE /admin/sessions
DELETE /admin/sessions/{$}
[...]
```

*The `{$}` string is not erroneous data, it's part of the syntax this Go API uses, see the [docs](https://pkg.go.dev/net/http#ServeMux).*

This is very useful to know what features are enabled at runtime.


One thing to note is that these Go functions are concurrency-safe, meaning they could get called concurrently from different goroutines just fine (a mutex is used in the Go implementation to enable that). This application does not do that to my knowledge.

It that was the case, our DTrace script would likely mix the different strings in the output. The standard solution for that case, is to store all these strings in map, and at the end, print the map. The order of registration is lost but that should not matter since this API defines pretty well route precedence based on how specific a route is, and not based on registration order. 
And for an API where that registration order *did* matter, we could also store a incrementing number in the map (globals are thread-safe in DTrace and can be safely mutated concurrently) to remember and print the order.

Once again, DTrace shines!


