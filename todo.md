Ideas for articles:

- [ ] HTTP server using arenas
- [ ] Migration from mbedtls to aws-lc
- [ ] ~~ASM crypto~~
- [ ] How this blog is made. Line numbers in code snippets.
- [ ] Banjo chords
- [ ] Dtrace VM
- [ ] From BIOS to bootloader to kernel to userspace
- [ ] A faster HTTP parser than NodeJS's one
- [ ] SHA1 multi-block hash
- [ ] How CGO calls are implemented in assembly
- [ ] How I develop Windows applications from the comfort of Linux
- [ ] Add a JS/Wasm live running version of the programs described in each article. As a fallback: Video/Gif.
    + [ ] Kahn's algorithm
- [ ] Blog search implementation
- [ ] C++ web server undefined behavior bug

Blog implementation:

- [x] Consider post-processing HTML instead of markdown to simplify e.g. to add title ids
- [x] Support markdown syntax in article title in metadata
- [x] Use libcmark to simplify parsing
- [ ] Link to related articles at the end (requires post-processing after all articles have been generated)
- [x] Search
  + May require proper markdown parsing (e.g. with libcmark) to avoid having html/markdown elements in search results, to get the (approximate) location of each results (e.g. parent title), and show a rendered excerpt in search results. Perhaps simply post-process html?
  + Results are shown inline
  + Results show (some) surrounding text
  + Results have a link to the page and surrounding html section
  + Implementation: trigrams in js/wasm? Fuzzy search? See https://pkg.odin-lang.org/search.js
  + Search code/data are only fetched lazily
  + Cache invalidation of the search blob e.g. when an article is created/modified? => split search code (rarely changes) and data (changes a lot)
  + Code snippets included in search results?
- [ ] Articles excerpt on the home page?
- [ ] Dark mode
- [ ] Browser live reload
  + Send the F5 key to the browser window (does not work in Wayland)
  + Somehow send an IPC message to the right browser process to reload the page?
  + HTTP server & HTML page communicate via websocket to reload content (complex)
  + HTTP2 force push to force the browser to get the new page version?
  + Server sent event to reload the page
- [ ] Built-in http server
- [ ] Syntax highlighting done statically

### Full text search reference

[Source](https://swtch.com/~rsc/regexp/regexp4.html).

> Before we can get to regular expression search, it helps to know a little about how word-based full-text search is implemented. The key data structure is called a posting list or inverted index, which lists, for every possible search term, the documents that contain that term.
> 
> For example, consider these three very short documents:
> 
> (1) Google Code Search
> (2) Google Code Project Hosting
> (3) Google Web Search
> 
> The inverted index for these three documents looks like:
> 
> Code: {1, 2}
> Google: {1, 2, 3}
> Hosting: {2}
> Project: {2}
> Search: {1, 3}
> Web: {3}
> 
> To find all the documents that contain both Code and Search, you load the index entry for Code {1, 2} and intersect it with the list for Search {1, 3}, producing the list {1}. To find documents that contain Code or Search (or both), you union the lists instead of intersecting them. Since the lists are sorted, these operations run in linear time.
> 
> To support phrases, full-text search implementations usually record each occurrence of a word in the posting list, along with its position:
> 
> Code: {(1, 2), (2, 2)}
> Google: {(1, 1), (2, 1), (3, 1)}
> Hosting: {(2, 4)}
> Project: {(2, 3)}
> Search: {(1, 3), (3, 4)}
> Web: {(3, 2)}
> 
> To find the phrase “Code Search”, an implementation first loads the list for Code and then scans the list for Search to find entries that are one word past entries in the Code list. The (1, 2) entry in the Code list and the (1, 3) entry in the Search list are from the same document (1) and have consecutive word numbers (2 and 3), so document 1 contains the phrase “Code Search”. 
