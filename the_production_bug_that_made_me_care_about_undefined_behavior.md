Title: The production bug that made me care about undefined behavior
Tags: C++, Undefined behavior, Bug
---

Years ago, I maintained a big C++ codebase at my day job. This product was the bread winner for the company and offered a public HTTP API for online payments. We are talking billions of euros of processed payments a year.

I was not yet a seasoned C++ developer yet. I knew about undefined behavior of course, but it was an abstract concept, something only beginners fall into. Oh boy was I wrong.


Please note that I am not and never was a C++ expert, and it's been a few years since I have been writing C++ for a living, so hopefully I got the wording and details right, but please tell me if I did not.

*In this article I always say 'struct' when I mean 'struct or class'.*

## The bug report

So, one day I receive a bug report. There is this HTTP endpoint that returns a simple response to  inform the client that the operation either succeeded or had an error:

```json
{
  "error": false,
  "succeeded": true,
}
```

or 

```json
{
  "error": true,
  "succeeded": false,
}

```

*The actual format was probably not JSON, it was probably form encoded, I cannot exactly remember, but that does not matter for this bug.*

This data model is not ideal but that's what the software did. Obviously, either `error` or `succeeded` is set but not both or neither (it's a XOR).


Anyway, the bug report says that the client received this reply:

```json
{
  "error": true,
  "succeeded": true
}
```

Hmm ok. That should not be possible, it's a bug indeed.


## Investigating 

I now look at the code. It's all in one big function, and it's doing lots of database operations, but the shape of the code is very simple:

```cpp
struct Response {
  bool error;
  bool succeeded;

  std::string data;
};

void handle() {
  Response response;
  
  try {
    // [..] Lots of database operations *not* touching `response`.

    response.succeeded = true;
  } catch(...) {
    response.error = true;
  }
  response.write();
}
```

*Here is a [godbolt](https://godbolt.org/z/5Wbbq3P7a) link with roughly this code.*

There's only one place that sets the `succeeded` field. And only one that sets the `error` field. No other place in the code touches these two fields.

So now I am flabbergasted. How is that possible that both fields are true? The code is straightforward. Each field is only set once and exclusively. It should be impossible to have both fields with the value `true`.

## Just enough rope to hang yourself

At this point, my C developer spider senses are tingling: is `Response response;` the culprit? It has to be, right? In C, that's clear undefined behavior to read fields from `response`: The C struct is not initialized.

But right after, I stumble upon official C++ examples that use this syntax. So now I am confused. C++ initialization rules are different from C, after all.

Cue a training montage with 80's music of me reading the C++ standard for hours. The short answer is: yes, the rules are different (enough to fill a book, and also they vary by C++ version) and *in some conditions*, `Response response;` is perfectly fine. In some other cases, this is undefined behavior.

In a nutshell: The [default initialization](https://en.cppreference.com/w/cpp/language/default_initialization) rule applies when a variable is declared without an initializer. It's quite complex but I'll try to simplify it here.

Default initialization occurs under certain circumstances when using the syntax `T object;` :

1. If `T` is a non class, non array type, e.g. `int a;`, no initialization is performed at all. This is obvious undefined behavior.
1. If `T` is an array, e.g. `std::string a[10];`, this is fine: each element is default-initialized. But note that some types do not have default initialization, such as `int`: `int a[10]` would leave each element uninitialized.
1. If `T` is a [POD](https://en.cppreference.com/w/cpp/named_req/PODType) (Plain Old Data, pre C++11. The wording in the standard changed with C++11 but the idea remains) struct, e.g. `Foo foo;` no initialization is performed at all. This is akin to doing `int a;` and then reading `a`. This is obvious undefined behavior.
1. If `T` is a non-POD struct, e.g. `Bar bar;` the default constructor is called, and it is responsible for initializing all fields. It is easy to miss one, or even forget to implement a default constructor entirely, leading to undefined behavior.

It's important to distinguish the first and last case: in the first case, no call to the default constructor is emitted by the compiler. In the last case, the default constructor is called. If no default constructor is declared in the struct, the compiler generates one for us, and calls it. This can be confirmed by inspecting the generated assembly.

With this bug, we are in the last case: the `Response` type is a non-POD struct (due to the `std::string data` field), so the default constructor is called. `Response` does not implement a default constructor. This means  that the compiler generates a default constructor for us, and in this generated code, each struct field is default initialized. So, the `std::string` constructor is called for the `data` field and all is well. Except, the other two fields are *not* initialized in any way. Oops.


Thus, the only way to fix the struct without having to fix all call sites is to implement a default constructor that properly initializes every field:

```c++
struct Response {
  bool error;
  bool succeeded;

  std::string data;

  Response(): error{false}, succeeded{false}, data{} 
  {
  }
};
```

*Here is a [godbolt](https://godbolt.org/z/bveKbxGeM) link with this code.*

Of course, due to the rule of 6 (is it 6 these days? When I started to learn C++ it was 3?), we now have to implement the default destructor, the default move constructor etc etc etc.

## Aftermath

My fix at the time was to simply change the call site to:

```c++
  Response response{};
```
*Here is a [godbolt](https://godbolt.org/z/rTqernMfq) link with this code.*

That forces zero initialization of the `error` and `succeeded` fields as well as default initialization of the `data` field. And no need to change the struct definition. 

This was my recommendation to my teammates at the time: do not tempt the devil, just *always* zero initialize when declaring a variable.


---

It is important to note that in some cases, the declaration syntax `Response response;` is perfectly correct, provided that:

- The type is an array, or the type is a non-POD struct and
- Each field has a default constructor

Then, the default constructor of the struct is invoked, which invokes the default constructor of each field.

For example:

```c++
struct Bar {
  std::string s;
  std::vector<std::string> vec;
};

int main() {
  Bar bar;

  // Prints: s=`` v.len=0
  // No undefined behavior.
  printf("s=%s v.len=%zu\n", bar.s.c_str(), bar.vec.size());
}
```

But to know that, you need to inspect each field (recursively) of the struct, or assume that every default constructor initializes each field.


Finally, it's also worth nothing that it is only undefined behavior to read an uninitialized value. Simply having uninitialized fields is not undefined behavior. If the fields are never read, or written to with a known value, before being read, there is no undefined behavior.


## Static analysis to the rescue?

The compiler (`clang`) does not catch this issue even with all warnings enabled. This is frustrating because the compiler happily generates, and calls, a default constructor that does not initialize all the fields. So, the caller is expected to set all the uninitialized fields to some value manually? This is nonsense to me.

`clang-tidy` catches the issue. However at the time it was imperfect, quoting my notes from back then:

> `clang-tidy` reports this issue when trying to pass such a variable as argument to a function, but that's all. We want to detect all problematic locations, even when the variable is not passed to a function. Also, `clang-tidy` only reports one location and exits.

But now, it seems it has improved, and reports all problematic locations, and not only in function calls, which is great.

I also wrote in my notes at the time that `cppcheck` 'spots this without issues', but when I try it today, it does not spot anything even with `--enable=all`. So, maybe it's a regression, or I am not using it correctly.


## Runtime analysis to the rescue

Most experienced C or C++ developers are probably screaming at their screen right now, thinking: just use Address Sanitizer!

Let's try it on the problematic code:

```shell
$ clang++ main.cpp -Weverything -std=c++11 -g -fsanitize=address,undefined -Wno-padded
$ ./a.out
a.out(46953,0x1f7f4a0c0) malloc: nano zone abandoned due to inability to reserve vm space.
main.cpp:21:41: runtime error: load of value 8, which is not a valid value for type 'bool'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior main.cpp:21:41 
error=0 success=1
```

Great, the undefined behavior is spotted! Even if the error message is not super clear. 

We alternatively could also have used Valgrind to the same effect.

But: it means that we now need to have 100% test coverage to be certain that our code does not have undefined behavior. That's a big ask.

Also, in my testing, Address Sanitizer did not always report the issue. That's the nature of the tool: it is meant to be conservative and avoid false positives, to avoid alerting fatigue, but that means it won't catch all issues. 

Additionally, these tools have a performance cost and can make the build process a bit more complex.


## The aftermath

I wrote a `libclang` plugin at the time to catch other instances of this problem in the codebase at build time: [https://github.com/gaultier/c/tree/master/libclang-plugin](https://github.com/gaultier/c/tree/master/libclang-plugin) . 

Amazingly, there was only one other case in the whole codebase, and it was a false positive because by chance, the caller set the uninitialized fields right after, like this:

```c++
Response response;
response.error = false;
response.success = true;
```

I have no idea if this `libclang` plugin still works today because I have heard that the `libclang` API often has breaking changes.


## Conclusion


In my opinion, this bug is C++ in a nutshell:

- Syntax that looks like C but *sometimes* does something completely different than C, invisibly. This syntax can be perfectly correct (e.g. in the case of an array, or a non POD type in some cases) or be undefined behavior. This makes code review really difficult. C and C++ really are two different languages.
- The compiler does not warn about undefined behavior and we have to rely on third-party tools, and these have limitations, and are usually slow
- The compiler happily generates a default constructor that leaves the object in a half-initialized state
- The rules in the standard are intricate and change with every new standard version (or at least the language they use). I just noticed that C++26 changed again these rules and introduced new language. Urgh.
- So many ways to initialize a variable, and most are wrong.
- For the code to behave correctly, the developer must not only consider the call site, but also the full struct definition, and whether it is a POD type.
- Adding or removing one struct field (e.g. the `data` field) makes the compiler generate completely different code at the call sites.
- You need a PhD in programming legalese to understand what is undefined behavior in the standard, and how you can trigger it

In contrast I really, really like the 'POD' approach that many languages have taken, from C, to Go, to Rust: a struct is just plain data. Either the compiler forces you to set each field in the struct when creating it, or it does not force you, and in this case, it zero-initializes all unmentioned fields. This is so simple it is obviously correct (but let's not talk about uninitialized padding between fields in C :/ ).

In the end I am thankful for this bug, because it made me aware for the first time that undefined behavior is real and dangerous, for one simple reason: it makes your program behave completely differently than the code. By reading the code, you cannot predict the behavior of the program in any way. The code stopped being the source of truth. Impossible values appear in the program, as if a cosmic ray hit your machine and flipped some bits.
And you can very easily, and invisibly, trigger undefined behavior. 

We programmers are only humans, and we only internalize that something (data corruption, undefined behavior, data races, etc) is a big real issue when we have been bitten by it and it ruined our day.


---

Post-Scriptum: This is not a hit piece on C++: C++ paid my bills for 10 years. I have been able to take a mortgage and build a house thanks to C++. But it is also a deeply flawed language, and I would not start a new professional project in C++ today without a very good reason. If you like C++, all the power to you. I just want to raise awareness on this (perhaps) little-known rule in the language that might trip you up.









