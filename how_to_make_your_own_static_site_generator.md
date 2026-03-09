Title: How to make your own static site generator
Tags: Blog
---

I developed my [own](https://github.com/gaultier/blog/blob/master/src/main.rs) static site generator for this blog. Initially it was just a Makefile. Over the years it evolved quite a bit. 

At some point it took several seconds. Now it takes ~120 ms for a clean build and ~50 ms for an incremental build.

This is my lessons learned.



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

generate rss feed
generate home page
```

The first version was this 3 lines Makefile:


```makefile
%.html: %.md header.html footer.html
        cat header.html >> $@
        pandoc --toc $< >> $@
        cat footer.html >> $@
```

It did not have a RSS feed, no search, no tags, no linting, the home page listing articles was manually kept in sync, and it was slower than my current custom-grown generator!

## List all articles

I wrote about it [before](/blog/making_my_static_blog_generator_11_times_faster.html). The easiest way is to list files on the file system, but if you want accurate 'created' and 'last modified' dates for each article, especially when working from multiple computers, you probably will have to query `git` (short of using a full-on database).


The lesson learned here is that for performance, avoid the N+1 query trap. Do one command for all articles, instead of one for each:

```shell
$ git log --format='%aI' --name-status --no-merges --diff-filter=AMDR --reverse '*.md'
```

The output of that one command is large, but it contains everything needed, and the overhead of process spawning is surpringly big, so spawning N processes is to be avoided at all costs.


Once in a while, cleaning up is a good idea to improve Git's performance:

```shell
$ git gc --aggressive --prune=now
```

I am considering adding this command as a cron job on my computer, or as a background job in my generator.

## Parsing

Chances are, you are authoring content in markdown or similar, which then must be converted to HTML. Even if you *are* writing HTML directly, this content needs to be post-processed quite a bit and thus must be parsed in the first place. No one wants to wrangle strings; structured data is the way to go.

I recommend using a library (or write your own) to parse the article, for example markdown, and this library *must* return Abstract Syntax Tree (AST). Since rich text is hierarchical, it is indeed best represented as a tree.  Think of a hyperlink in a table cell, or a bold text in a list element. These are trees.

Another reason to work on the AST is that you have total control on the HTML generation, including for code blocks. This opens the door to syntax highlighting at generation time. 

Currently, I implement syntax highlighting with JavaScript at runtime, but I may change this in the future. At least I have the ability to do it at build time.

If the content is huge, a search index might be required to be built. Having the AST is great to only index text and skip code blocks, inline HTML, etc.

## Linting

The linting step is much easier to implement on the AST. Here are a few examples of lints I have implemented: 

- Detect invalid links, e.g. linking to a markdown article, where it should be pointing to the HTML version for it.
- Code snippets without an explicit language declared, or an unknown language (this matters for syntax highlighting). E.g. these are invalid:
    ```markdown
       ```
        foo := bar()
       ```
    ```
    ```markdown
       ```g0
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
- Forbid images without an `alt` attribute for accessibility
- Forbid media (images, audio, video) without fallback format(s)


## Generate the table of content

This also relies on the AST: we collect all title elements, including their depth and text content. Then, we insert at the beginning of the article the table of contents. 

These titles:

```markdown
## Foo

### Bar

## Baz
```

Get collected into this array, conceptually:

```json
[
  {
    "title": "Foo",
    "depth": 2
  },
  {
    "title": "Bar",
    "depth": 3
  },
  {
    "title": "Baz",
    "depth": 2
  }
]
```

And that gets turned into this HTML, which is just nested lists with links:

```html
<ul>
  <li>
      <a href="/Foo">Foo</a>
    <ul>
        <li>
            <a href="/Bar">Bar</a>
        </li>
    </ul>
  </li>

  <li>
      <a href="/Baz">Baz</a>
  </li>
</ul>
```




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

If the same title appears multiple times (in different sections), a counter (`1`, `2`, etc) is appended to the link so that each link is unique on the page. This [article](/blog/making_my_debug_build_run_100_times_faster.html) is a good example.


Finally, a bit of CSS gets us section numbers before each title, e.g. `3.4.1`:

```css
.toc ul { 
  counter-reset: section;
}

.toc li::before {
  counter-increment: section;
  content: counters(section, ".") " ";
}
```

## Generate the HTML

I walk the AST and mechanically generate the appropriate HTML for each markdown element. 

I have total freedom here, and can add our own CSS classes, ids, data attributes, etc.

It is also straightforward to adapt this code to generate other formats, e.g. Latex (if you enjoy that kind of thing), etc.

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

I have written about it [before](/blog/feed.html). This is very simple, we just generate a XML file listing all articles including the creation and modification date. I use [UUID v5](https://en.wikipedia.org/wiki/Universally_unique_identifier) to assign an id to each article because it's a good fit: the blog itself has a UUID which is the namespace, and the UUID for each article is `sha1(blog_namespace + article_file_path)`.


I save the XML in the file `feed.xml` and mention this XML in the HTML in the `<head>` element:

```html
<link type="application/atom+xml" href="/blog/feed.xml" rel="self">
```


## Line numbers in code snippets

Line numbers in code snippets are done entirely in CSS. Each line of the code snippet is preceded with an empty span with the CSS class `line-number`.

Then, I let the browser compute the right line number:

```css
.line-number {
  user-select: none;
}

.line-number::before {
  counter-increment: line-count;
  content: counter(line-count);
}
```

Thanks to `user-select: none`, copy-pasting code works out of the box: the line numbers will not be part of the selection.

And to gain a bit of space, line numbers are not displayed on small screens:

```css
@media only screen and (max-width: 650px) {
  .line-number { display: none; }
}
```


## Generate the home page

This is the `index.html`, typically. It generally lists all articles (in my case), or only the recent ones, or the most read. The only thing worth mentioning is that in my case, each article has manually defined tags, such as `Go`, `Rust`, etc. So this page shows the tags for each article, and there is also a [page](/blog/articles-by-tag.html) showing articles by tags. 

## Search


To implement the search function which is purely client side, I used to have a search index with trigrams. However I realized that at my scale, a linear search is just fast enough (< 1ms), and it is not much data to transfer (3 MiB uncompressed for all articles, Github pages applies gzip compression automatically with a 2-10x compression ratio).

When someone types in the search box for the first time, the HTML content for each article is fetched in parallel. This way, users who never use the search feature do not pay the price for it. The browser caches future fetches. If a loyal reader already has read all articles, then they are already cached in their browser and the fetch is a no-op.

To avoid searching for irrelevant content, I ignore some DOM elements, e.g. the header, footer, code snippets, etc.

When a user types in the search box, the content of all articles is linearly searched with `indexOf()`. Since this function is very likely implemented with SIMD, it is lightning fast. Then, for each match, the link to the article, as well as surrounding text, is shown.



## Drafts

Some site generators have a `drafts` folder, or some metadata field at the beginning of the article e.g. `Draft: true`, to write an article that is not yet publicly accessible.

I just create a new Git branch when I want to write a new article. The new article is created and written as any other article. 

Whenever the branch is merged, the article will appear on Github pages, which uses the files on the master branch. 

I think that's just the easiest way to do it, the site generator does not need to model drafts in any way. 

## Live reloading


I always wanted to add live-reloading to have a nicer writing experience, which I'm convinced helps write more and better articles. The goal was to have the whole cycle take under 100 ms. Currently it takes ~50 ms which is great. Nearly all of this time is taken by Git to get the list of articles including the 'created at' and 'modified at' dates. Not much to optimize here, short of doing a manual `stat(2)` call to circumvent Git.

The way it works is, in the same process:

1. At start-up, all articles are generated. This is a clean build (because the cache is not stored on disk and only exists in memory, it is empty at start-up) and takes ~120 ms on my machine.
1. An HTTP server is started. It serves static files and also has a `/live-reload` endpoint. It uses [server-sent events (SSE)](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events) to tell the client: hey, a file changed, reload the page.
1. A thread is spawned to watch the file system for changes. When a relevant file is changed, an event is broadcast to all listening threads (the SSE serving threads) with `cvar.notify_all()`. While no file is changed, the thread watching the file system is idle since it is blocked on a system call (`kqueue`, `inotify`, etc), and the SSE threads are also idle, waiting on the condition variable with `cvar.wait()`. That means 0% CPU consumption until a file is changed.

The whole code for it is ~100 lines of code. It would be even less if I found a small HTTP server library that correctly handles SSE, without having to use complex asynchronous code. I implement SSE by hand, and since the format is super simple (newline delimited text events), it's not much work at all:

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

The last two lines mean: I only try to live-reload locally, not when the page is served from Github pages.

This works beautifully. A prior version used WebSockets and that proved to be a headache compared to SSE. If the flow of events is strictly unidirectional, from the server to the client, SSE is much simpler. 


`location.reload()` is a bit heavy-handed since it reloads the page completely. The nice thing is that every asset is reloaded including CSS, images, etc. However there is a visible flash when the page is being re-rendered. 

A more advanced live reloading approach, which is also more work, is to tell the browser what changed exactly. If an asset file changed, reload the page. If the content changed, send the new content to the browser, and replace it in the DOM. This can be taken to the extreme by doing precise diffing and only replacing, say, the one character that changed.

Perhaps I'll consider doing that in the future.

## Caching and performance

To avoid regenerating files that have not changed, I added a cache which is just a `Map<hash of inputs, generated output>`. 

In the case of a cache miss, when the work for an article is done, the output is stored in the cache. 

If an input changed, then the hash is different, it's a cache miss, and the article will be regenerated. If the content for one article changed, only this article will have to be regenerated. If `header.html` or `footer.html` changed, then all articles will be regenerated, automatically.

```rust
fn hash_article_inputs(html_header: &[u8], html_footer: &[u8], md_content: &[u8]) -> u64 {
    let mut hasher = DefaultHasher::new();
    html_header.hash(&mut hasher);
    html_footer.hash(&mut hasher);
    md_content.hash(&mut hasher);
    hasher.finish()
}

fn md_render_article(
    git_stat: GitStat,
    html_header: &[u8],
    html_footer: &[u8],
    cache: &mut HashMap<u64, Article>,
) -> Article {
    let md_content_bytes = fs::read(&git_stat.path_from_git_root).unwrap();
    let hash = Cache::hash(&html_header, &html_footer, &md_content_bytes);
    if let Some(article) = cache.get(&hash) {
        return article.clone();
    }

    // [...] Do the work.


    cache.insert(hash, article.clone());

    article
}
```

Technically I could use the [nohash](https://docs.rs/nohash-hasher/0.2.0/nohash_hasher/) crate to optimize a bit, since the key in the map is already a hash, no need to hash it a second time. But I don't bother for now.
 
I never clear the cache, because my computer has so much memory. This has one advantage: if I undo a change when writing an article, and the work had already finish, I will hit the existing cache entry again.

Skipping all this work is fine for one reason only: generating the HTML for an article is a pure function with immutable arguments. If it mutated a variable (for example a search index), we could not easily skip this work.


This was an interesting lesson for me: no shared mutable variables (except the cache) makes parallel and incremental computations possible.

Right now I do not (yet) generate the HTML for each article in parallel, because it's already plenty fast, but conceptually I could, since each article is fully independent from the others (again, except for the cache). 


Caching is I think a spectrum, some operations are so cheap and fast that caching them is not worth it. It can be taken to the extreme: [Salsa](https://salsa-rs.github.io/salsa/reference/algorithm.html#the-red-green-algorithm), [Buck](https://buck.build/concept/what_makes_buck_so_fast.html). In my experience, there is usually one main expensive operation in the system (in my case: Markdown parsing), and adding a cache in front of that is generally sufficient.

Finally, caching should not be a band-aid for general slowness. If some operation is unnecessarily slow, try to optimize it first, and ensure it is really needed. For example, I initially had the search index encoded as JSON, and it took ~600 ms to build and marshal it. I optimized it to only take ~10 ms. In the end, I realized I don't need a search index at all and removed all of this code. Do less, go faster.


This suprised me: in most cases, we deal with data that's just not that big, and linear operations (array, linear scan), are often just fast enough, especially with SIMD and the CPU prefetcher.


## Light & Dark mode

Modern CSS has proper support for light and dark mode with the [light-dark()](https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Values/color_value/light-dark) function.

This is enabled with:

```javascript
document.body.style['color-scheme'] = 'light dark';
```

And used like this:

```css
div {
  background: light-dark(#fbf1c7, #282828);
  color: light-dark(#282828, #fbf1c7);
}
```

The first argument is the color in light mode, and the second argument the color in dark mode.

The browser detects the current mode if the OS supports that concept and sets the value for our page automatically. Then, it picks the right CSS color that was provided with `light-dark()`.

Light or dark mode can also be set manually (for example when the user clicks the light/dark mode button):

```javascript
console.log(document.body.style.colorScheme);

document.body.style.colorScheme = 'light';

document.body.style.colorScheme = 'dark';
```

Easy!


## Conclusion

If you use an existing static site generator and you're satisfied, then great!

Otherwise, I hope I have shown that writing your own is not much work at all. All of it is ~1.5 kLoC. And it's a great way to experiment and learn new things, for example SSE, search index, etc. 

At work I sometimes have to use *very* slow site generators, that take *minutes* to build, and I am left really confused. Modern computers can do a *lot* in just 1 second!


Something else that stroke me is how similar a static site generator is to a compiler: AST parsing, caching, linting, generate the output by walking the AST, etc.
