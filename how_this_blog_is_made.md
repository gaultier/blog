Title: How to make your own static site generator for your blog
Tags: Blog
---

I developed my own static site generator for this blog. Over the years I evolved it quite a bit. This is my lessons learned.



At the core, a static site generator typically must perform these steps:

```plaintext
read header.html
read footer.html 

get the list of all articles

for each article:
    parse article
    lint content
    generate table of content 
    generate html
    save html file

generate rss feed
generate home page
generate search index
```

## List all articles

I wrote about it [before](/blog/making_my_static_blog_generator_11_times_faster.html). The easiest way is to list files on the file system, but if you want accurate 'created' and 'last modified' dates for each article, you probably will have to query `git` (short of using a full-on database).


The lesson learned here is that for performance, avoid the N+1 query trap by doing one command for all articles instead of one for each.

## Parse the article

Chances are, you are authoring content in markdown or similar, which then must be converted to HTML. Even if you *are* written HTML directly, this content needs to be post-processed quite a bit and thus must be parsed in the first place. No one wants to wrangle strings; structured data is the way to do.

I recommend using a library to parse the article, for example markdown, and this library *must* give you the Abstract Syntax Tree (AST) for it. Since rich text is hierarchical, it is indeed a tree.  Think of a hyperlink in a table cell, or a bold text in a list element. These are trees.

Another reason to work on the AST is that you have total control on the HTML generation, including for code blocks. This opens the door to syntax highlighting at generation time. 

Currently, I implement syntax highlighting with JavaScript at runtime, but I may change this in the future. At least I have the possibility.


## Lint the article content

Finally, the linting step is much easier to implement on the AST. Here are a few examples of lints I have implemented: 

- Detect dead links
- Code snippets without an explicit language declared, or an unknown language (this matters for syntax highlighting, e.g. `c++` was used but the canonical name is `cpp`)
- Style: Prefer `1 KiB` over other variants e.g. `1 kb`, `1 KB`, `1 K`, etc
- Titles that skip a level, for example `h2 -> h4`. This is subjective, perhaps some people like to have this ability - I don't, so I prevent it.

Other lints that could be also easily implemented based on walking the AST:

- Forbid lists with only one element
- Inline code elements whose content exceeds a certain length; these should be use the syntax for a multiline code block

Furthermore, we'll see in the section about search, that having the AST is great to only index text and skip code blocks, inline HTML, etc when building the search index.


## Generate the table of content

This also relies on the AST: we collect all title elements, including their depth and text content. Then, we insert at the beginning of the article the table of contents. 

This code is quite short and uninteresting, so not much to add here.

## Generate the HTML

We walk the AST and mechanically generate the appropriate HTML for each markdown element. 

We have total freedom here, and can add our own CSS classes, ids, data attributes, etc.

It is also straightforward to adapt this code to generate other formats, e.g. Latex (if you enjoy pain), etc.

As previously mentioned this is where doing static syntax highligting could take place.

## Generate the RSS feed

I have written about it [before](/blog/feed.html). This is very simple, we just generate a XML file listing all articles including the creation and modification date. I use UUID v5 to assign an id to each article because it's a good fit: the blog itself has a UUID which is the namespace, and each article has a UUID v5 based on this namespace.


We save the XML in the file `feed.xml` and mention this XML in the HTML in the `<head>` element:

```html
<link type="application/atom+xml" href="/blog/feed.xml" rel="self">
```

## Generate the home page

This is the `index.html`, typically. It generally lists all articles (in my case), or only the recent ones, or the most read. Not much to add here.

## Search


To implement the search function which is purely client side, an efficient way is to build a search index. I chose to collect all trigrams (group of 3 characters) present in each article.

This information is saved to a file in a compact manner (this is the search index). 

When someone types in the search box, the search index is fetched if it's not already present. This way, users who never use the search feature do not pay the price for it. The browser caches subsequent fetches. The search index lists how many times a certain trigram occurs in a certain file. Conceptually:

```json
{
    "cat": [["foo.html", 3], ["bar.html", 4]],
    "fox": [["bar.html", 1]]
}
```

Here, `cat` occurs 3 times in `foo.html`.

This JSON format was my first stab at it but it had two big, related issues: Encoding took too long, and the size was too big. JSON has a bad signal-to-noise ratio for small strings. In this example, almost half of the characters do not carry meaning for the search index, they are just separators between JSON tokens.

This is why I switched to a different encoding: [postcard](https://postcard.jamesmunns.com/wire-format). The search index went from 600 ms encoding time and 600 KiB size to 10 ms and 200 KiB.

The Javascript in the browser now needs to manually decode it, but it's very little code because the format is simple.


The way the search index is then used in the browser is to quickly find files with matches. For each trigram in the search text, the corresponding trigram is inspected in the search index, to know which files contain it. The results of the search is a `Map<file name, search score>`. The score is is higher for files with more matches. This way, the UI can show the 'best' match first.

```javascript
function search_text(needle) {
  const file_scores = new Map();

  // For each trigram in search text.
  for (let i = 0; i <= needle.length - 3; i++) {
    const trigram = needle[i] + needle[i+1] + needle[i+2];

    const match = search_index.trigram_to_file_idx.get(trigram);
    if (match === undefined) {
      // This trigram does not exist in the search index, continue.
      continue;
    }

    const docs_with_this_trigram = match.length;

    for (const m of match) {
      const [file, count] = m;
      const score = file_scores.get(file) || 0;

      // Record the file with the match along with a score.
      // Different trigrams for the same file increase its score.
      file_scores.set(file, score + count * (1/docs_with_this_trigram) );
    }
  }
  
  return file_scores;
}
```

I experimented a bit with how the search score is computed. I think this way strikes a balance between simplicity and accuracy.

Finally, the JavaScript fetches the HTML for each article that has a match, to show an excerpt of the text surrounding the match. These queries are done in parallel for performance, and should get cached by the browser after the first time. 

I have heard about [people](https://tigerbeetle.com/blog/2025-02-27-why-we-designed-tigerbeetles-docs-from-scratch/#simple-is-beautiful) using a Service Worker to always fetch all articles in the background so that they know for sure that the content of each article is always there. This is just eager vs. lazy loading, both work. 

The advantage of fetching all content in advance is that now, a search index is not necessary anymore. Technically, for the amount of text my blog has, a search index is not required, but I always wanted to learn how a search index works and this was the perfect opportunity.

## Live reloading


I always wanted to add live-reloading to have a nicer writing experience, which I'm convinced helps write more and better articles. The goal was to have the whole cycle take under 100 ms. Currently it takes ~70 ms which is great. 

The way it works is:

1. At start-up, all articles are generated. This takes ~180 ms.
1. An HTTP server is started. It serves static files and also has a `/live-reload` endpoint. It uses [server-sent events (SSE)](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events) to tell the client: hey, a file changed, reload the page.
1. A thread is spawned to watch the file system for changes. When a relevant file is changed, an 'event' is broadcast to all listening threads (the SSE serving threads) with `cvar.notify_all()`. While no file is changed, the thread watching the file system is idle since it is blocked on a system call (`kqueue`, `inotify`, etc), and the SSE threads are also idle, waiting on the condition variable with `cvar.wait()`. That means 0% CPU consumption.

The whole code for it is ~ 100 lines of code. It would be even less if I found a library that correctly handles SSE. I implement SSE by hand, and since the format is super simple (newline delimited text events), it's not much work at all:

```rust
fn live_reload(
    mut resp: BufWriter<TcpStream>,
    mtx_cond: Arc<(Mutex<()>, Condvar)>,
) -> Result<(), ()> {
    write!(
        resp,
        "HTTP/1.1 200\r\nCache-Control: no-cache\r\nContent-Type: text/event-stream\r\n\r\n"
    )
    .map_err(|_| ())?;

    loop {
        let (lock, cvar) = &*mtx_cond;
        let guard = lock.lock().map_err(|_| ())?;

        drop(cvar.wait(guard).map_err(|_| ())?);
        write!(resp, "data: foobar\n\n").map_err(|_| ())?;
        resp.flush().map_err(|_| ())?;
        println!("🔃 sse event sent");
    }
}
```

And the JavaScript side is also very short:

```javascript
function sse_connect() {
  const eventSource = new EventSource('/blog/live-reload');

  eventSource.onopen = (_event) => {
    console.log("connected");
  }

  eventSource.onmessage = (event) => {
    console.log("New message:", event.data);
    location.reload();
  };

  eventSource.onerror = (err) => {
    console.error("EventSource failed:", err);
    // The browser will automatically attempt to reconnect 
    // after a short delay unless `eventSource.close()` is called.
  };
}

if (!location.origin.includes("github")) {
  sse_connect();
}
```

The last two lines mean: We only try to live-reload locally, not when the page is served from Github pages.

This works beautifully. A prior version used WebSockets and that proved to be a headache compared to SSE. If the flow of events is strictly unidirectional, from the server to the client, SSE is much simpler. 

### Caching

To avoid regenerating files that have not changed, I added a cache which is just a `Map<file path, (hash of markdown content, generated output)>`. 

If a file has changed, the entry for it is removed from the cache. If a file that impacts all articles changes, e.g. `header.html` and `footer.html`, the entire cache is cleared and all articles are re-generated.

To make this work efficiently, generating one article should ideally be a pure function that takes in immutable arguments, and outputs the generated HTML (and metadata such as `created_at`, `modified_at`). This way, the first thing I do when handling an article is check the cache. If it's a cache hit, I just return the value from the cache. 

The only thing I had to tweak was building the search index. Initially, generating an article would also add entries in the search index in a mutable way. 

Before:


```json
{
    "cat": [["foo.html", 3]]
}
```

After handling `bar.html`:

```json
{
    "cat": [["foo.html", 3], ["bar.html", 4]],
    "fox": [["bar.html", 1]],
}
```

Now, imagine that I edit the text in `bar.html`. Since `bar.html` could appear in every entry of the search index, every entry is possibly stale, and we have to scan all articles again, to rebuild the search index from scratch. We ideally want to build the search index incrementally, only scanning the file that changes.


So I changed the approach so that each article tracks which trigrams are present in the content, e.g. for `bar.html`:

```json
// bar.html
["cat", "fox"]

// foo.html
["cat"]
```

This way each article is completely independent, and the final search index is the result of merging together the trigrams for each article. The final search index is the same.

This was an interesting lesson for me: no shared mutable variables (except from the cache) makes parallel and incremental computations possible.


