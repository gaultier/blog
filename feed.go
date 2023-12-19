package main

import (
	"fmt"
	"os"
	"os/exec"
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

		markdownFileName := strings.TrimSuffix(e.Name(), "html") + "md"

		gitCmd := exec.Command("git", "log", "--follow", "--format=%ad", "--date", "iso-strict", "--", markdownFileName)
		gitOut, err := gitCmd.Output()
		if err != nil {
			panic(err)
		}
		gitOutStr := string(gitOut)
		lines := strings.Split(gitOutStr, "\n")
		if len(lines) == 0 {
			panic("No lines: " + gitOutStr)
		}
		publishedAt := lines[0]
		updatedAt := lines[len(lines)-2]

		content, err := os.ReadFile(e.Name())
		if err != nil {
			panic(err)
		}
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

		entryUuid := uuid.NewSHA1(feedUuid, []byte(e.Name()))
		out.WriteString(fmt.Sprintf(
			`
     <entry>
       <title>%s</title>
       <link href="%s"/>
       <id>urn:uuid:%s</id>
       <updated>%s</updated>
			 <published>%s</published>
     </entry>
`, title, link, entryUuid, updatedAt, publishedAt))

	}
	out.WriteString("</feed>")

	fmt.Println(out.String())
}
