# This blog now has an Atom feed, and yours should probably too

*<svg aria-hidden="true" focusable="false" role="img" class="octicon octicon-tag" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" style="display:inline-block;user-select:none;overflow:visible"><path d="M1 7.775V2.75C1 1.784 1.784 1 2.75 1h5.025c.464 0 .91.184 1.238.513l6.25 6.25a1.75 1.75 0 0 1 0 2.474l-5.026 5.026a1.75 1.75 0 0 1-2.474 0l-6.25-6.25A1.752 1.752 0 0 1 1 7.775Zm1.5 0c0 .066.026.13.073.177l6.25 6.25a.25.25 0 0 0 .354 0l5.025-5.025a.25.25 0 0 0 0-.354l-6.25-6.25a.25.25 0 0 0-.177-.073H2.75a.25.25 0 0 0-.25.25ZM6 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2Z"></path></svg> [Feed](/blog/articles-per-tag.html#Feed), [Atom](/blog/articles-per-tag.html#Atom), [UUID](/blog/articles-per-tag.html#UUID)*

*Find it [here](https://gaultier.github.io/blog/feed.xml) or in the header on the top right-hand corner.*

Imagine a world where you can see the content of each website you like inside the app of your choosing, read the articles offline and save them on disk for later, be notified whenever the website has something new, and all of which is implemented with an open standard. Well that was most of the web some years ago and this blog now does all of that. 


![This feed inside the open-source app NewsFlash (https://flathub.org/apps/io.gitlab.news_flash.NewsFlash)](feed.png)

And it's not hard! The only thing we need is to serve a `feed.xml` file that lists articles with some metadata such as 'updated at' and a UUID to be able to uniquely identify an article. This XML file is an [Atom feed](https://en.wikipedia.org/wiki/Atom_(web_standard)) which has a nice [RFC](https://datatracker.ietf.org/doc/html/rfc4287).

I implemented that in under an hour, skimming at the RFC and examples. It's a bit hacky but it works. The script to do so is [here](https://github.com/gaultier/blog/blob/master/feed.go). And you can do too! Again, it's not hard. Here goes:

- We pick a UUID for our feed. I just generated one and stuck it as a constant in the script.
- The 'updated at' field for the feed is just `time.Now()`. It's not exactly accurate, it should probably be the most recent `mtime` across articles but it's good enough.
- For each article (`*.html`) file in the directory, we add an entry (`<entry>`) in the XML document with:
  * The link to the article, that's just the filename in my case.
  * The 'updated at' field, which is <s>just the `mtime` of the file locally</s> queried from git
  * The 'published at' field, which is <s>just the `ctime` of the file locally</s> queried from git
  * A UUID. Here I went with UUIDv5 which is simply the sha1 of the file name in the UUID format. It's nifty because it means that the script is stateless and idempotent. If the article is later updated, the UUID remains the same (but the `updated at` will still hint at the update).

And...that's it really. Enjoy reading these articles in your favorite app!

