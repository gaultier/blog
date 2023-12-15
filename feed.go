package main

import (
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/google/uuid"
)

const feedUuidStr string = `9c065c53-31bc-4049-a795-936802a6b1df`

func main() {
	feedUuid := uuid.MustParse(feedUuidStr)

	entries, err := os.ReadDir(".")
	if err != nil {
		panic(err)
	}

	now := time.Now().UTC().Format(time.RFC3339)

	out := strings.Builder{}
	out.Grow(100 * 1024)
	out.WriteString(fmt.Sprintf(
		`
 <?xml version="1.0" encoding="utf-8"?>
   <feed xmlns="http://www.w3.org/2005/Atom">

     <title>Philippe Gaultier's blog</title>
     <link href="https://gaultier.github.io/blog"/>
     <updated>%s</updated>
     <author>
       <name>Philippe Gaultier</name>
     </author>
     <id>urn:uuid:%s</id>
	`, now, feedUuidStr))

	for _, e := range entries {
		if !strings.HasSuffix(e.Name(), ".html") {
			continue
		}
		if e.Name() == "README.html" || e.Name() == "index.html" {
			continue
		}

		content, err := os.ReadFile(e.Name())
		if err != nil {
			panic(err)
		}
		stat, err := os.Stat(e.Name())
		if err != nil {
			panic(err)
		}
		updatedAt := stat.ModTime().UTC().Format(time.RFC3339)
		link := "/blog/" + e.Name()

		h1StartIndex := strings.Index(string(content), "<h1>")
		if h1StartIndex == -1 {
			panic(fmt.Sprintf("Failed to find <h1> in %s", e.Name()))
		}
		h1EndIndex := strings.Index(string(content), "</h1>")
		if h1EndIndex == -1 {
			panic(fmt.Sprintf("Failed to find </h1> in %s", e.Name()))
		}
		title := string(content)[h1StartIndex+4 : h1EndIndex]

		entryUuid := uuid.NewSHA1(feedUuid, content)
		out.WriteString(fmt.Sprintf(
			`
     <entry>
       <title>%s</title>
       <link href="%s"/>
       <id>urn:uuid:%s</id>
       <updated>%s</updated>
<content type="html">
      <![CDATA[
			%s
]]>
    </content>
     </entry>
`, title, link, entryUuid, updatedAt, content))

	}
	out.WriteString("</feed>")

	fmt.Println(out.String())
}
