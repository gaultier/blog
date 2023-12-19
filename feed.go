package main

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/google/uuid"
)

const baseUrl string = "https://gaultier.github.io/blog/"

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
     <link href="%s"/>
     <updated>%s</updated>
     <author>
       <name>Philippe Gaultier</name>
     </author>
     <id>urn:uuid:%s</id>
	`, baseUrl, now, feedUuidStr))

	for _, e := range entries {
		if !strings.HasSuffix(e.Name(), ".md") {
			continue
		}
		markdownFileName := e.Name()
		if e.Name() == "README.md" || e.Name() == "index.md" {
			continue
		}

		htmlFileName := strings.TrimSuffix(e.Name(), "md") + "html"

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
		updatedAt := lines[0]
		publishedAt := lines[len(lines)-2]

		content, err := os.ReadFile(markdownFileName)
		if err != nil {
			panic(err)
		}
		link := baseUrl + htmlFileName

		titleStartIndex := strings.Index(string(content), "# ")
		if titleStartIndex == -1 {
			panic(fmt.Sprintf("Failed to find `# ` in %s", e.Name()))
		}
		titleEndIndex := strings.Index(string(content), "\n")
		if titleEndIndex == -1 {
			panic(fmt.Sprintf("Failed to find title end in %s", e.Name()))
		}
		title := strings.TrimSpace(string(content)[titleStartIndex+len("# ") : titleEndIndex])

		entryUuid := uuid.NewSHA1(feedUuid, []byte(htmlFileName))
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
