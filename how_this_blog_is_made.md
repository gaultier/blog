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

When someone types in the search box, the search index is fetched (once, because the browser is very good at caching it), and used like this:



