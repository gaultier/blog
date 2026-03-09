Title: How to make your own static site generator for your blog
Tags: Blog
---

I developed my own static site generator for this blog. Initially it was just a Makefile. Over the years I evolved it quite a bit. This is my lessons learned.



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
```

## List all articles

I wrote about it [before](/blog/making_my_static_blog_generator_11_times_faster.html). The easiest way is to list files on the file system, but if you want accurate 'created' and 'last modified' dates for each article, you probably will have to query `git` (short of using a full-on database).


The lesson learned here is that for performance, avoid the N+1 query trap: do one command for all articles, instead of one for each.

## Parsing

Chances are, you are authoring content in markdown or similar, which then must be converted to HTML. Even if you *are* writing HTML directly, this content needs to be post-processed quite a bit and thus must be parsed in the first place. No one wants to wrangle strings; structured data is the way to go.

I recommend using a library (or write your own) to parse the article, for example markdown, and this library *must* return Abstract Syntax Tree (AST). Since rich text is hierarchical, it is indeed best represented as a tree.  Think of a hyperlink in a table cell, or a bold text in a list element. These are trees.

Another reason to work on the AST is that you have total control on the HTML generation, including for code blocks. This opens the door to syntax highlighting at generation time. 

Currently, I implement syntax highlighting with JavaScript at runtime, but I may change this in the future. At least I have the ability to do it at build time.


## Linting

The linting step is much easier to implement on the AST. Here are a few examples of lints I have implemented: 

- Detect invalid links, e.g. linking to a markdown article, where it should be pointing to the HTML version for it.
- Code snippets without an explicit language declared, or an unknown language (this matters for syntax highlighting, e.g. `c++` was used but the canonical name is `cpp`). E.g. this is invalid:
    ```markdown
       ```
        foo := bar()
       ```
    ```
    And this is valid:
    ```markdown
       ```go
        foo := bar()
       ```
    ```
- Style: Prefer `1 KiB` over other variants e.g. `1 kb`, `1 KB`, `1 K`, etc
- Forbid titles that skip a level, for example `h2 -> h4`. This is subjective, perhaps some people like to have this ability - I don't, so I prevent it.

Other lints that could be also easily implemented based on walking the AST:

- Forbid lists with only one element
- Inline code elements whose content exceeds a certain length; these should be use the syntax for a multiline code block

If the content is huge, a search index might be required to be built. Having the AST is great to only index text and skip code blocks, inline HTML, etc when building the index.

## Generate the table of content

This also relies on the AST: we collect all title elements, including their depth and text content. Then, we insert at the beginning of the article the table of contents. 

The only interesting thing about this code is that it performs a linear scan of all titles in the article, which are stored in a flat array. In the past I used to build a tree of the titles, but it's unnecessary, slower, allocates more, and honestly not really more readable:


```rust
struct Title {
    text: String,
    depth: u8,
    start_md_offset: usize,
    slug: String,
}

fn md_render_toc(content: &mut Vec<u8>, titles: &[Title]) {
    if titles.is_empty() {
        return;
    }

    writeln!(
        content,
        r#"  <details class="toc"><summary>Table of contents</summary>
<ul>"#
    )
    .unwrap();

    let mut current_depth = titles[0].depth;

    for (i, title) in titles.iter().enumerate() {
        if title.depth > current_depth {
            for _ in 0..(title.depth - current_depth) {
                writeln!(content, "<ul>").unwrap();
            }
        } else if title.depth < current_depth {
            // Close the current <li>, then close the <ul> levels, then close the parent <li>.
            for _ in 0..(current_depth - title.depth) {
                writeln!(content, "</li>\n</ul>").unwrap();
            }
            writeln!(content, "</li>").unwrap(); // Close the <li> of the previous same-level item.
        } else if i > 0 {
            // Same level: just close the previous item.
            writeln!(content, "</li>").unwrap();
        }
        current_depth = title.depth;

        writeln!(
            content,
            r##"
  <li>
    <a href="#{}">{}</a>"##,
            title.slug, &title.text,
        )
        .unwrap();
    }

    // Final cleanup: close all remaining open tags.
    let base_depth = titles[0].depth;
    for _ in 0..=(current_depth - base_depth) {
        writeln!(content, "</li>\n</ul>").unwrap();
    }
    writeln!(content, "</details>\n").unwrap();
}

```

## Generate the HTML

We walk the AST and mechanically generate the appropriate HTML for each markdown element. 

We have total freedom here, and can add our own CSS classes, ids, data attributes, etc.

It is also straightforward to adapt this code to generate other formats, e.g. Latex (if you enjoy pain), etc.

As previously mentioned this is where doing static syntax highlighting could take place.

The only things to watch for are:

- Escape special HTML chars (we do not have to be super defensive since this is our content, not arbitrary user generated content)
- Footnotes need to be collected to list them at the end in the HTML.

Here's an excerpt:

```rust
fn md_to_html_rec(
    content: &mut Vec<u8>,
    footnote_defs: &mut Vec<FootnoteDefinition>,
    node: &Node,
    titles: &[Title],
    inside_thead: bool,
) {
    match node {
        Node::InlineCode(inline_code) => {
            let sanitized = text_sanitize_for_html(&inline_code.value, false);
            write!(content, "<code>{}</code>", sanitized).unwrap();
        }
        Node::Delete(delete) => {
            write!(content, "<del>").unwrap();
            for child in &delete.children {
                md_to_html_rec(content, footnote_defs, child, titles, false);
            }
            write!(content, "</del>").unwrap();
        }
        Node::Image(image) => {
            write!(
                content,
                r#"<img src="{}" alt="{}" />"#,
                image.url, image.alt
            )
            .unwrap();
        }

        // [...]
    }
}
```

## Generate the RSS feed

I have written about it [before](/blog/feed.html). This is very simple, we just generate a XML file listing all articles including the creation and modification date. I use [UUID v5](https://en.wikipedia.org/wiki/Universally_unique_identifier_ to assign an id to each article because it's a good fit: the blog itself has a UUID which is the namespace, and the UUID for each article is `sha1(blog_namespace + article_file_path)`.


We save the XML in the file `feed.xml` and mention this XML in the HTML in the `<head>` element:

```html
<link type="application/atom+xml" href="/blog/feed.xml" rel="self">
```

## Generate the home page

This is the `index.html`, typically. It generally lists all articles (in my case), or only the recent ones, or the most read. The only thing worth mentioning is that in my case, each article has manually defined tags, such as `Go`, `Rust`, etc. So this page shows the tags for each article, and there is also a [page](/blog/articles-by-tag.html) showing articles by tags. That page is built with a simple map:

```rust
    let mut tag_to_articles = BTreeMap::new();

    for article in articles {
        for tag in &article.tags {
            tag_to_articles
                .entry(tag.clone())
                .or_insert_with(Vec::new)
                .push(article);
        }
    }
```

## Search


To implement the search function which is purely client side, I used to have a search index with trigrams. However I realized that at my scale, a linear search is just fast enough (< 1ms), and it is not much data to transfer (3 MiB uncompressed for all articles, Github pages applies gzip compression automatically with a 2-10x compression ratio).

When someone types in the search box for the first time, the content for each article is fetched in parallel. This way, users who never use the search feature do not pay the price for it. The browser caches future fetches. 

When a user types in the search box, the content of all articles is linearly searched with `indexOf()`. Since this very likely is implemented with SIMD, it is lightning fast. Then, for each match, the link to the article, as well as surrounding text, is shown.


## Live reloading


I always wanted to add live-reloading to have a nicer writing experience, which I'm convinced helps write more and better articles. The goal was to have the whole cycle take under 100 ms. Currently it takes ~70 ms which is great. 

The way it works is, in the same process:

1. At start-up, all articles are generated. This takes ~120 ms on my machine.
1. An HTTP server is started. It serves static files and also has a `/live-reload` endpoint. It uses [server-sent events (SSE)](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events) to tell the client: hey, a file changed, reload the page.
1. A thread is spawned to watch the file system for changes. When a relevant file is changed, an 'event' is broadcast to all listening threads (the SSE serving threads) with `cvar.notify_all()`. While no file is changed, the thread watching the file system is idle since it is blocked on a system call (`kqueue`, `inotify`, etc), and the SSE threads are also idle, waiting on the condition variable with `cvar.wait()`. That means 0% CPU consumption until a file is changed.

The whole code for it is ~100 lines of code. It would be even less if I found a small HTTP server library that correctly handles SSE, without having to use tokio, etc. I implement SSE by hand, and since the format is super simple (newline delimited text events), it's not much work at all:

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

### Caching and performance

To avoid regenerating files that have not changed, I added a cache which is just a `Map<file path, (hash of markdown content, generated output)>`. 

If a file has changed, the entry for it is removed from the cache, which is why the key is the file path. If a file that impacts all articles changes, e.g. `header.html` and `footer.html`, the entire cache is cleared and all articles are re-generated.

To make this work efficiently, generating one article should ideally be a pure function that takes in immutable arguments, and outputs the generated HTML (and metadata such as `created_at`, `modified_at`). Thus, the first thing I do when handling an article is check the cache. If it's a cache hit, I just return the value from the cache. 

This way, each article is completely independent.

This was an interesting lesson for me: no shared mutable variables (except from the cache) makes parallel and incremental computations possible.


Caching is I think a spectrum, some operations are so cheap and fast that caching them is not worth it. It can be taken to the extreme: [Salsa](https://salsa-rs.github.io/salsa/reference/algorithm.html#the-red-green-algorithm), [Buck](https://buck.build/concept/what_makes_buck_so_fast.html). In my experience, there is usually one main expensive operation, and adding a cache in front of that, which is just a map, is generally sufficient.

Finally, caching should not be a band-aid for general slowness. If some operation is unnecessarily slow, try to optimize it first, and ensure it is really needed. For example, I initially had the search index encoded as JSON, and it took ~600 ms to build and marshal it. I optimized it to only take ~10 ms. In the end, I realized I don't need a search index at all and removed all of this code. Do less, go faster.


This suprised me: in many cases, we deal with data that's just not that big, and linear operations (array, linear scan), are often just fast enough, especially with SIMD and the prefetcher.

## Conclusion

If you use an existing static site generator and you're satisfied, then great! If you're not, I hope I have shown that writing your own is not much work at all. All of it is ~1.5 kLoC. And it's a great way to experiment and learn new things, for example SSE. 

At work I sometimes have to use *very* slow site generators, that take *minutes* to build, and I am left really confused. Modern computers can do a *lot* in just 1 second. 
