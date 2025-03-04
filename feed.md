Title: This blog now has an Atom feed, and yours should probably too
Tags: Feed, Atom, UUID
---

*Find it [here](https://gaultier.github.io/blog/feed.xml) or in the header on the top right-hand corner.*

Imagine a world where you can see the content of each website you like inside the app of your choosing, read the articles offline and save them on disk for later, be notified whenever the website has something new, and all of which is implemented with an open standard. Well that was most of the web some years ago and this blog now does all of that. 


![This feed inside the open-source app NewsFlash (https://flathub.org/apps/io.gitlab.news_flash.NewsFlash)](feed.png)

And it's not hard! The only thing we need is to serve a `feed.xml` file that lists articles with some metadata such as 'updated at' and a UUID to be able to uniquely identify an article. This XML file is an [Atom feed](https://en.wikipedia.org/wiki/Atom_(web_standard)) which has a nice [RFC](https://datatracker.ietf.org/doc/html/rfc4287).

I implemented that in under an hour, skimming at the RFC and examples. It's a bit hacky but it works. The script to do so is [here](https://github.com/gaultier/blog/blob/master/feed.go). And you can do too! Again, it's not hard. Here goes:

- We pick a UUID for our feed. I just generated one and stuck it as a constant in the script.
- The 'updated at' field for the feed is just `time.Now()`. It's not exactly accurate, it should probably be the most recent `mtime` across articles but it's good enough.
- For each article (`*.html`) file in the directory, we add an entry (`<entry>`) in the XML document with:
  * The link to the article, that's just the filename in my case.
  * The 'updated at' field, which is ~~just the `mtime` of the file locally~~ queried from git
  * The 'published at' field, which is ~~just the `ctime` of the file locally~~ queried from git
  * A UUID. Here I went with UUIDv5 which is simply the sha1 of the file name in the UUID format. It's nifty because it means that the script is stateless and idempotent. If the article is later updated, the UUID remains the same (but the `updated at` will still hint at the update).

And...that's it really. Enjoy reading these articles in your favorite app!

