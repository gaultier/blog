package main

import "core:encoding/uuid"
import "core:encoding/uuid/legacy"
import "core:fmt"
import "core:mem"
import "core:mem/virtual"
import "core:os"
import "core:os/os2"
import "core:path/filepath"
import "core:slice"
import "core:strings"
import "core:unicode"

feed_uuid_str :: "9c065c53-31bc-4049-a795-936802a6b1df"
base_url :: "https://gaultier.github.io/blog"
back_link :: "<p><a href=\"/blog\"> ⏴ Back to all articles</a></p>\n"
html_prelude_fmt :: "<!DOCTYPE html>\n<html>\n<head>\n<title>%s</title>\n"
cmark_command :: []string {
	"cmark-gfm",
	"-e",
	"table",
	"-e",
	"strikethrough",
	"-e",
	"footnotes",
	"--unsafe",
	"-t",
	"html",
}
metadata_delimiter :: "---"

Article :: struct {
	output_file_name:  string,
	title:             string,
	tags:              []string,
	creation_date:     string,
	modification_date: string,
}

Title :: struct {
	content:   string,
	level:     int,
	unique_id: u32,
}

datetime_to_date :: proc(datetime: string) -> string {
	split := strings.split_n(datetime, "T", 2)
	return split[0]
}

parse_metadata :: proc(markdown: string, path: string) -> (title: string, tags: []string) {
	metadata_lines := strings.split_lines_n(markdown, 4)

	if len(metadata_lines) < 4 {
		panic(fmt.aprintf("file %s missing metadata", path))
	}
	if metadata_lines[2] != metadata_delimiter {
		panic(fmt.aprintf("file %s missing metadata delimiter", path))
	}

	title_line_split := strings.split_n(metadata_lines[0], ": ", 2)
	if len(title_line_split) != 2 {
		panic(fmt.aprintf("file %s has a malformed metadata title", path))
	}
	title = strings.clone(strings.trim_space(title_line_split[1]))

	tags_line_split := strings.split_n(metadata_lines[1], ": ", 2)
	if len(tags_line_split) != 2 {
		panic(fmt.aprintf("file %s has a malformed metadata tags", path))
	}
	tags_split := strings.split(tags_line_split[1], ", ")

	tags = make([]string, len(tags_split))
	for tag, i in tags_split {
		tags[i] = strings.clone(tag)
		assert(!strings.starts_with(tags[i], ","))
	}

	assert(!strings.starts_with(title, "Title:"))

	return
}

run_sub_process_and_get_stdout :: proc(
	command: []string,
	stdin: []byte,
) -> (
	stdout: []byte,
	err: os2.Error,
) {
	stdin_w: ^os2.File
	stdin_r: ^os2.File
	if len(stdin) > 0 {
		stdin_r, stdin_w = os2.pipe() or_return
	}

	stdout_r, stdout_w := os2.pipe() or_return
	desc := os2.Process_Desc {
		command = command,
		stdout  = stdout_w,
		stdin   = stdin_r,
	}

	process := os2.process_start(desc) or_return
	os2.close(stdin_r)
	os2.close(stdout_w)
	defer _ = os2.process_close(process)

	if stdin_w != nil {
		for cur := 0; cur < len(stdin); {
			n_written := os2.write(stdin_w, stdin[cur:]) or_return
			if n_written == 0 {break}
			cur += n_written
		}
		os2.close(stdin_w)
	}


	stdout_sb := make([dynamic]u8, 0, 4096)
	for {
		buf := [4096]u8{}
		read_n := os2.read(stdout_r, buf[:]) or_break
		if read_n == 0 {break}

		append(&stdout_sb, ..buf[:read_n])
	}

	process_state := os2.process_wait(process) or_return
	if !process_state.success {
		return {}, .Unknown
	}

	return stdout_sb[:], nil
}


GitStat :: struct {
	creation_date:     string,
	modification_date: string,
	path_rel:          string,
}

get_articles_creation_and_modification_date :: proc() -> (res: []GitStat, err: os2.Error) {
	stdout_bin := run_sub_process_and_get_stdout(
	[]string {
		"git",
		"log",
		// Print the date in ISO format.
		"--format='%aI'",
		// Ignore merge commits since they do not carry useful information.
		"--no-merges",
		// Only interested in creation, modification, renaming.
		"--diff-filter=AMR",
		// Show which modification took place:
		// A: added, M: modified, RXXX: renamed (with percentage score), etc.
		"--name-status",
		"*.md",
	},
	{},
	) or_return
	// if len(stderr_bin) > 0 {
	// 	fmt.printf("git command stderr: %v %s\n", state, string(stderr_bin))
	// }
	stdout := strings.trim_space(string(stdout_bin))
	if len(stdout) == 0 {
		panic("empty git output")
	}


	stats_by_path := make(map[string]GitStat, allocator = context.temp_allocator)

	// For each entry.
	for {
		// Date
		date: string
		{
			line, ok := strings.split_lines_iterator(&stdout)
			if !ok do break

			assert(strings.starts_with(line, "'20"))
			line_without_quotes := line[1:len(line) - 1]
			date = strings.clone(strings.trim(line_without_quotes, "' \n"))
			assert(ok)
		}

		// Empty line
		{
			// Peek.
			line, ok := strings.split_lines_iterator(&stdout)
			assert(ok)
			assert(line == "")
		}

		// Files.
		for {
			// Peek.
			stdout_bck := stdout
			line: string

			{
				ok := false
				line, ok = strings.split_lines_iterator(&stdout_bck)
				if !ok do break

				// Reached the next entry?
				if strings.starts_with(line, "'20") do break

				// Commit.
				stdout = stdout_bck
			}

			action: u8
			{
				action_part, ok := strings.split_iterator(&line, "\t")
				assert(ok)
				assert(action_part != "")
				action = action_part[0]
				assert(action == 'A' || action == 'M' || action == 'R')
			}

			old_path: string
			new_path: string
			{
				path, ok := strings.split_iterator(&line, "\t")
				assert(ok)
				assert(path != "")

				if action == 'R' {
					old_path = path

					new_path, ok = strings.split_iterator(&line, "\t")
					assert(ok)
					assert(new_path != "")
				} else {
					old_path = path
					new_path = path
				}
			}

			_ = action
			git_stat, present := &stats_by_path[new_path]
			if !present {
				stats_by_path[new_path] = GitStat {
					path_rel          = strings.clone(new_path),
					// We inspect commits from newest to oldest so the first commit for a file is the newest i.e. the modification date.
					modification_date = date,
				}
			} else {
				assert(git_stat.path_rel != "")
				assert(git_stat.modification_date != "")
				git_stat.creation_date = date
			}
		}
	}

	git_stats := make([dynamic]GitStat)
	for _, v in stats_by_path {
		fmt.printf("%v\n", v)
		assert(v.path_rel != "")
		assert(v.creation_date != "")
		assert(v.modification_date != "")
		assert(v.creation_date <= v.modification_date)

		append(&git_stats, v)
	}

	return git_stats[:], nil
}

make_html_friendly_id :: proc(input: string, allocator := context.allocator) -> string {
	builder := strings.builder_make_len_cap(0, len(input) * 2, allocator)

	for c in input {
		switch c {
		case 'A' ..= 'Z', 'a' ..= 'z', '0' ..= '9':
			strings.write_rune(&builder, unicode.to_lower(c))
		case '+':
			strings.write_string(&builder, "plus")
		case '#':
			strings.write_string(&builder, "sharp")
		case:
			s := strings.to_string(builder)
			if len(s) > 0 && !strings.ends_with(s, "-") {
				strings.write_rune(&builder, '-')
			}
		}
	}

	return strings.trim(strings.to_string(builder), "-")
}

decorate_markdown_with_title_ids :: proc(markdown: string) -> string {
	inside_code_section := false
	unique_id := 0

	builder := strings.builder_make_len_cap(0, len(markdown) * 2)

	markdown_ptr := markdown
	for line in strings.split_lines_iterator(&markdown_ptr) {
		is_begin_html_title := strings.starts_with(line, "<h")
		assert(!is_begin_html_title)

		is_begin_markdown_title := strings.starts_with(line, "#")
		is_delimiter_markdown_code_section := strings.starts_with(line, "```")

		if is_delimiter_markdown_code_section && !inside_code_section {
			inside_code_section = true
			strings.write_string(&builder, line)
			strings.write_rune(&builder, '\n')
			continue
		}

		if is_delimiter_markdown_code_section && inside_code_section {
			inside_code_section = false
			strings.write_string(&builder, line)
			strings.write_rune(&builder, '\n')
			continue
		}

		if inside_code_section {
			strings.write_string(&builder, line)
			strings.write_rune(&builder, '\n')
			continue
		}

		if !is_begin_markdown_title {
			strings.write_string(&builder, line)
			strings.write_rune(&builder, '\n')
			continue
		}

		title_level := strings.count(line, "#")
		assert(1 <= title_level && title_level <= 6)

		title_content := strings.trim_space(line[title_level:])
		unique_id += 1
		title_id_raw := fmt.aprintf(
			"%d-%s",
			unique_id,
			title_content,
			allocator = context.temp_allocator,
		)
		title_id := make_html_friendly_id(title_id_raw)

		fmt.sbprintf(
			&builder,
			`<h%d id="%s">
	<a class="title" href="#%s">%s</a>
	<a class="hash-anchor" href="#%s" aria-hidden="true" onclick="navigator.clipboard.writeText(this.href);"></a>
</h%d>
			`,
			title_level,
			title_id,
			title_id,
			title_content,
			title_id,
			title_level,
		)
		strings.write_rune(&builder, '\n')
	}
	return strings.to_string(builder)
}

toc_lex_titles :: proc(markdown: string, allocator := context.allocator) -> []Title {
	titles := make([dynamic]Title, allocator)

	unique_id := u32(0)
	inside_code_section := false
	markdown_ptr := markdown
	for line in strings.split_lines_iterator(&markdown_ptr) {
		is_begin_html_title := strings.starts_with(line, "<h")
		assert(!is_begin_html_title)

		is_begin_markdown_title := strings.starts_with(line, "#")
		is_delimiter_markdown_code_section := strings.starts_with(line, "```")

		if is_delimiter_markdown_code_section && !inside_code_section {
			inside_code_section = true
			continue
		}

		if is_delimiter_markdown_code_section && inside_code_section {
			inside_code_section = false
			continue
		}

		if inside_code_section {continue}
		if !is_begin_markdown_title {continue}

		title_level := strings.count(line, "#")
		assert(1 <= title_level && title_level <= 6)

		title_content := strings.trim_space(line[title_level:])
		unique_id += 1
		append(&titles, Title{content = title_content, level = title_level, unique_id = unique_id})
	}

	return titles[:]
}

toc_write :: proc(sb: ^strings.Builder, titles: []Title) -> []Title {
	if len(titles) == 0 {return {}}

	title := titles[0]
	title_id_raw := fmt.aprintf(
		"%d-%s",
		title.unique_id,
		title.content,
		allocator = context.temp_allocator,
	)
	id := make_html_friendly_id(title_id_raw)

	fmt.sbprintf(sb, `
<li>
	<a href="#%s">%s</a>
		`, id, title.content)

	is_next_title_higher := len(titles) > 1 && titles[1].level > title.level
	is_next_title_lower := len(titles) > 1 && titles[1].level < title.level

	if is_next_title_lower {
		assert(titles[1].level + 1 == title.level)

		strings.write_string(sb, "</li>\n")

		// No recursion: return to the caller (parent title) to allow them to close the `<ul>` tag.
		// The parent will then handle the rest of the titles.
		// Otherwise said: the current title is a leaf.
		return titles[1:]
	} else if is_next_title_higher {
		assert(titles[1].level - 1 == title.level)

		strings.write_string(sb, "<ul>\n")
		// Recurse on children.
		// `remaining` is now a sibling or '(great-)uncle' of ours.
		remaining := toc_write(sb, titles[1:])
		assert(true if len(remaining) == 0 else remaining[0].level >= title.level)

		// Close the tags that need to be closed.
		strings.write_string(sb, "</ul>\n")
		strings.write_string(sb, "</li>\n")

		// And now handle the rest of the titles.
		return toc_write(sb, remaining)
	} else {
		// Easy case, next title is a sibling, just close our own `<li>` tag and handle the rest of the titles.
		strings.write_string(sb, "</li>\n")
		return toc_write(sb, titles[1:])
	}
}


append_article_toc :: proc(sb: ^strings.Builder, markdown: string, article_title: string) {
	titles := toc_lex_titles(markdown)
	if len(titles) == 0 {return}

	strings.write_string(sb, " <strong>Table of contents</strong>\n")
	strings.write_string(sb, "<ul>\n")
	toc_write(sb, titles)
	strings.write_string(sb, "</ul>\n")
}

generate_html_article :: proc(
	markdown: string,
	article: Article,
	header: string,
	footer: string,
) -> (
	err: os.Error,
) {
	assert(len(markdown) > 0)
	assert(len(header) > 0)
	assert(len(footer) > 0)

	context.allocator = context.temp_allocator
	defer free_all(context.allocator)

	metadata_split := strings.split_n(markdown, metadata_delimiter + "\n", 2)
	article_content := metadata_split[1]

	decorated_markdown := decorate_markdown_with_title_ids(article_content)

	cmark_output_bin, os2_err := run_sub_process_and_get_stdout(
		cmark_command,
		transmute([]u8)decorated_markdown,
	)
	if os2_err != nil {
		panic(fmt.aprintf("failed to run cmark: %v", os2.error_string(os2_err)))
	}

	html_sb := strings.builder_make()

	fmt.sbprintf(&html_sb, html_prelude_fmt, article.title)
	strings.write_string(&html_sb, header)
	fmt.sbprintf(
		&html_sb,
		`
		<div class="article-prelude">
			%s
			<p class="publication-date">Published on %s</p>
		</div>
		<div class="article-title">
		<h1>%s</h1>
		`,
		back_link,
		datetime_to_date(article.creation_date),
		article.title,
	)

	if len(article.tags) > 0 {
		strings.write_string(&html_sb, "  <div class=\"tags\">")
	}

	for tag in article.tags {
		id := make_html_friendly_id(tag)
		fmt.sbprintf(
			&html_sb,
			` <a href="/blog/articles-by-tag.html#%s" class="tag">%s</a>`,
			id,
			tag,
		)
	}

	if len(article.tags) > 0 {
		strings.write_string(&html_sb, "</div>\n")
	}
	strings.write_string(&html_sb, " </div>\n")

	append_article_toc(&html_sb, article_content, article.title)

	strings.write_rune(&html_sb, '\n')
	strings.write_string(&html_sb, string(cmark_output_bin))
	strings.write_string(&html_sb, back_link)
	strings.write_string(&html_sb, footer)

	os.write_entire_file_or_err(
		article.output_file_name,
		transmute([]u8)strings.to_string(html_sb),
	) or_return

	return
}

generate_article :: proc(
	git_stat: GitStat,
	header: string,
	footer: string,
) -> (
	article: Article,
	err: os.Error,
) {
	assert(len(git_stat.path_rel) > 0)
	assert(filepath.ext(git_stat.path_rel) == ".md")
	assert(len(git_stat.creation_date) > 0)
	assert(len(git_stat.modification_date) > 0)
	assert(len(header) > 0)
	assert(len(footer) > 0)

	defer free_all(context.temp_allocator)

	original_markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
		git_stat.path_rel,
		allocator = context.temp_allocator,
	) or_return

	stem := filepath.stem(git_stat.path_rel)

	article.title, article.tags = parse_metadata(original_markdown_content, git_stat.path_rel)

	article.creation_date = git_stat.creation_date
	article.modification_date = git_stat.modification_date

	article.output_file_name = strings.concatenate([]string{stem, ".html"})

	generate_html_article(original_markdown_content, article, header, footer) or_return
	fmt.printf("generated %s %v\n", article.output_file_name, article.tags)

	return
}

generate_all_articles_in_directory :: proc(
	header: string,
	footer: string,
) -> (
	articles: []Article,
	err: os.Error,
) {
	assert(len(header) > 0)
	assert(len(footer) > 0)

	articles_dyn := make([dynamic]Article)

	git_stats, os2_err := get_articles_creation_and_modification_date()
	if os2_err != nil {
		panic(fmt.aprintf("failed to run git: %v", os2.error_string(os2_err)))
	}

	for git_stat in git_stats {
		if git_stat.path_rel == "index.md" {continue}
		if git_stat.path_rel == "README.md" {continue}

		article := generate_article(git_stat, header, footer) or_return
		append(&articles_dyn, article)
	}

	return articles_dyn[:], nil
}

generate_home_page :: proc(
	articles: []Article,
	header: string,
	footer: string,
) -> (
	err: os.Error,
) {
	assert(len(articles) > 0)
	assert(len(header) > 0)
	assert(len(footer) > 0)

	context.allocator = context.temp_allocator
	defer free_all(context.allocator)

	slice.sort_by(articles, compare_articles_by_creation_date_desc)

	markdown_file_path :: "index.md"
	html_file_path :: "index.html"

	sb := strings.builder_make()

	fmt.sbprintf(&sb, html_prelude_fmt, "Philippe Gaultier's blog")
	strings.write_string(&sb, header)
	strings.write_string(
		&sb,
		`
		<div class="articles">
		  <h2 id="articles">Articles</h2>
			<ul>
		`,
	)

	for a in articles {
		assert(len(a.tags) > 0)

		if a.output_file_name == "body_of_work.html" {continue}

		fmt.sbprintf(
			&sb,
			`
	<li>
		<div class="home-link"> 
			<span class="date">%s</span>
			<a href="/blog/%s">%s</a>
		</div>
		<div class="tags">
	`,
			datetime_to_date(a.creation_date),
			a.output_file_name,
			a.title,
		)
		for tag in a.tags {
			id := make_html_friendly_id(tag)
			fmt.sbprintf(
				&sb,
				` <a href="/blog/articles-by-tag.html#%s" class="tag">%s</a>`,
				id,
				tag,
			)
		}
		fmt.sbprint(&sb, "</div></li>")

	}

	strings.write_string(&sb, " </ul>\n </div>\n")

	{
		markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
			markdown_file_path,
		) or_return
		decorated_markdown := decorate_markdown_with_title_ids(markdown_content)

		cmark_stdout_bin, os2_err := run_sub_process_and_get_stdout(
			cmark_command,
			transmute([]u8)decorated_markdown,
		)
		if os2_err != nil {
			panic(fmt.aprintf("failed to run cmark %v", os2_err))
		}
		strings.write_string(&sb, string(cmark_stdout_bin))
	}
	strings.write_string(&sb, footer)

	os.write_entire_file_or_err(html_file_path, transmute([]u8)strings.to_string(sb)) or_return

	fmt.printf("generated %s\n", html_file_path)

	return
}

generate_page_articles_by_tag :: proc(
	articles: []Article,
	header: string,
	footer: string,
) -> (
	err: os.Error,
) {
	assert(len(articles) > 0)
	assert(len(header) > 0)
	assert(len(footer) > 0)

	context.allocator = context.temp_allocator
	defer free_all(context.allocator)

	articles_by_tag := make(map[string][dynamic]Article)

	for a in articles {
		for tag in a.tags {
			assert(len(tag) > 0)

			if _, present := articles_by_tag[tag]; !present {
				articles_by_tag[tag] = make([dynamic]Article)
			}
			append(&articles_by_tag[tag], a)
		}
	}


	sb := strings.builder_make()
	strings.write_string(&sb, header)
	strings.write_string(&sb, back_link)
	strings.write_string(&sb, "<h1>Articles by tag</h1>\n")
	strings.write_string(&sb, "<ul>\n")

	tags_lexicographically_ordered := make([]string, len(articles_by_tag))

	i := 0
	for tag in articles_by_tag {
		tags_lexicographically_ordered[i] = tag
		i += 1
	}
	slice.sort(tags_lexicographically_ordered)

	for tag in tags_lexicographically_ordered {
		articles_for_tag := articles_by_tag[tag]

		slice.sort_by(articles_for_tag[:], compare_articles_by_creation_date_asc)
		tag_id := make_html_friendly_id(tag)

		fmt.sbprintf(&sb, `<li id="%s"><span class="tag">%s</span><ul>`, tag_id, tag)

		for a in articles_for_tag {
			fmt.sbprintf(
				&sb,
				`
	<li>
		<span class="date">%s</span>
		<a href="%s">%s</a>
	</li>
				`,
				datetime_to_date(a.creation_date),
				a.output_file_name,
				a.title,
			)
		}
		strings.write_string(&sb, "</ul></li>\n")
	}
	strings.write_string(&sb, "</ul>\n")
	strings.write_string(&sb, footer)

	html_file_name :: "articles-by-tag.html"
	os.write_entire_file_or_err(html_file_name, transmute([]u8)strings.to_string(sb)) or_return

	return
}

compare_articles_by_creation_date_asc :: proc(a: Article, b: Article) -> bool {
	return a.creation_date < b.creation_date
}

compare_articles_by_creation_date_desc :: proc(a: Article, b: Article) -> bool {
	return a.creation_date > b.creation_date
}

generate_rss_feed_for_article :: proc(sb: ^strings.Builder, article: Article) {

	base_uuid, err := uuid.read(feed_uuid_str)
	assert(err == nil)
	article_uuid := legacy.generate_v5_string(base_uuid, article.output_file_name)
	article_uuid_str := uuid.to_string(article_uuid)

	fmt.sbprintf(
		sb,
		`
	<entry>
  <title>%s</title>
  <link href="%s/%s"/>
  <id>urn:uuid:%s</id>
  <updated>%s</updated>
  <published>%s</published>
  </entry>
	`,
		article.title,
		base_url,
		article.output_file_name,
		article_uuid_str,
		article.modification_date,
		article.creation_date,
	)
}

generate_rss_feed :: proc(articles: []Article) -> (err: os.Error) {
	assert(len(articles) > 0)

	context.allocator = context.temp_allocator
	defer free_all(context.allocator)

	slice.sort_by(articles, compare_articles_by_creation_date_asc)

	sb := strings.builder_make()

	fmt.sbprintf(
		&sb,
		`<?xml version="1.0" encoding="utf-8"?>
  <feed xmlns="http://www.w3.org/2005/Atom">
    <title>Philippe Gaultier's blog</title>
    <link href="%s"/>
    <updated>%s</updated>
    <author>
      <name>Philippe Gaultier</name>
    </author>
    <id>urn:uuid:%s</id>
`,
		base_url,
		articles[len(articles) - 1].modification_date,
		feed_uuid_str,
	)

	for a in articles {
		generate_rss_feed_for_article(&sb, a)
	}

	strings.write_string(&sb, "</feed>")

	fmt.printf("generated RSS feed for %d articles\n", len(articles))

	os.write_entire_file_or_err("feed.xml", transmute([]u8)strings.to_string(sb)) or_return
	return
}

run :: proc() -> (os_err: os.Error) {

	header := transmute(string)os.read_entire_file_from_filename_or_err("header.html") or_return
	footer := transmute(string)os.read_entire_file_from_filename_or_err("footer.html") or_return

	articles := generate_all_articles_in_directory(header, footer) or_return
	generate_home_page(articles, header, footer) or_return
	generate_page_articles_by_tag(articles, header, footer) or_return
	generate_rss_feed(articles) or_return


	return
}

main :: proc() {
	arena: virtual.Arena
	defer fmt.println(arena)

	{
		arena_size := uint(1) * mem.Megabyte
		mmaped, err := virtual.reserve_and_commit(arena_size)
		if err != nil {
			panic(fmt.aprintf("failed to mmap %v", err))
		}
		if err = virtual.arena_init_buffer(&arena, mmaped); err != nil {
			panic(fmt.aprintf("failed to create main arena %v", err))
		}
	}
	context.allocator = virtual.arena_allocator(&arena)


	tmp_arena: virtual.Arena
	defer fmt.println(tmp_arena)
	{
		tmp_arena_size := uint(1) * mem.Megabyte
		tmp_mmaped, err := virtual.reserve_and_commit(tmp_arena_size)
		if err != nil {
			panic(fmt.aprintf("failed to create mmap %v", err))
		}
		if err = virtual.arena_init_buffer(&tmp_arena, tmp_mmaped); err != nil {
			panic(fmt.aprintf("failed to create temp arena %v", err))
		}
	}
	context.temp_allocator = virtual.arena_allocator(&tmp_arena)

	if err := run(); err != nil {
		panic(fmt.aprintf("%v", err))
	}
}
