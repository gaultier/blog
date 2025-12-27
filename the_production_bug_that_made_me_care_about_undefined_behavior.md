Title: The production bug that made me care about undefined behavior
Tags: C++, Undefined behavior
---

Years ago, I maintained a big C++ codebase at my dayjob. It was the bread winner for the company and offered a public HTTP API for online payments. We are talking billions of euros worth of payments a year.

I was not yet a seasoned C++ developer yet. I knew about undefined behavior of course, but it was an abstract concept, something only beginners fall into. Oh boy was I wrong.

So, one day I receive a bug report. There is this HTTP endpoint that returns a simple response to  inform the client that the operation either succeeded or had an error:

```json
{
  "error": false,
  "succeeded": true,

  // some other fields
}
```

or 

```json
{
  "error": true,
  "succeeded": false,

  // some other fields
}

```

*The actual format was probably not JSON, it was probably form encoded, I cannot exactly remember, but that does not matter for this bug.*

This format is not ideal but that's what the software did.


Anyway, the bug report says that the client received this reply:

```json
{
  "error": true,
  "succeeded": true
}
```

Hmm ok. That should not be possible, it's a bug indeed.

I now look at the code. It's all in one big function, and it's doing lots of database operations, but the shape of the code is very simple:

```cpp
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

There's only one place that sets the `succeeded` field. And only one that sets the `error` field. No other place in the code touches these two fields.

So now I am flabbergasted. How is that possible that both fields are true? The code is straightforward.

Now I turn to the `Response` definition, it looks like this (simplified):

```cpp
struct Response {
  bool error;
  bool succeeded;

  std::vector<std::string> data;
};
```

At this point, my C developer spider senses are tingling: is `Response response;` the culprit? It has to be, right? In C, that's clear undefined behavior: The C struct is not initialized.

But right after, I stumble upon official C++ examples that use this syntax. So now I am confused. Are C++ intialization rules different from C? 

Cue a training montage with 80's music of me reading the C++ standard for hours. The short answer is: yes, the rules are different (enough to fill a book, and also they vary by C++ version) and *in some conditions*, `Response response;` is perfectly fine. In some other cases, this is undefined behavior.

In a nutshell: The [default initialization](https://en.cppreference.com/w/cpp/language/default_initialization) rule applies when a variable is declared without an initializer. 



