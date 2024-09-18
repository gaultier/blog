package main

// import "base:runtime"
import "core:encoding/uuid"
import "core:encoding/uuid/legacy"
import "core:fmt"
import "core:mem"
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
cmark_command :: []string{"cmark", "--unsafe", "-t", "html"}
metadata_delimiter :: "---"

Article :: struct {
	output_file_name:  string,
	title:             string,
	tags:              []string,
	creation_date:     string,
	modification_date: string,
}

Title :: struct {
	content: string,
	level:   int,
}

datetime_to_date :: proc(datetime: string) -> string {
	split := strings.split_n(datetime, "T", 2, allocator = context.temp_allocator)
	return split[0]
}

parse_metadata :: proc(markdown: string, path: string) -> (title: string, tags: []string) {
	metadata_lines := strings.split_lines_n(markdown, 4, allocator = context.temp_allocator)

	if len(metadata_lines) < 4 {
		panic(fmt.aprintf("file %s missing metadata", path))
	}
	if metadata_lines[2] != metadata_delimiter {
		panic(fmt.aprintf("file %s missing metadata delimiter", path))
	}

	title_line_split := strings.split_n(
		metadata_lines[0],
		": ",
		2,
		allocator = context.temp_allocator,
	)
	if len(title_line_split) != 2 {
		panic(fmt.aprintf("file %s has a malformed metadata title", path))
	}
	title = strings.clone(strings.trim_space(title_line_split[1]))

	tags_line_split := strings.split_n(
		metadata_lines[1],
		": ",
		2,
		allocator = context.temp_allocator,
	)
	if len(tags_line_split) != 2 {
		panic(fmt.aprintf("file %s has a malformed metadata tags", path))
	}
	tags_split := strings.split(tags_line_split[1], ", ", allocator = context.temp_allocator)

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
	stdout: string,
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


	stdout_sb := strings.builder_make()
	for {
		buf := [4096]u8{}
		read_n := os2.read(stdout_r, buf[:]) or_break
		if read_n == 0 {break}

		strings.write_bytes(&stdout_sb, buf[:read_n])
	}

	process_state := os2.process_wait(process) or_return
	if !process_state.success {
		return {}, .Unknown
	}

	return strings.to_string(stdout_sb), nil
}


get_creation_and_modification_date_for_article :: proc(
	path: string,
) -> (
	creation_date: string,
	modification_date: string,
	err: os2.Error,
) {
	stdout := run_sub_process_and_get_stdout(
		[]string{"git", "log", "--format='%aI'", "--", path},
		{},
	) or_return
	defer delete(stdout)
	stdout = strings.trim_space(stdout)

	lines := strings.split_lines(stdout, context.temp_allocator)
	modification_date = strings.clone(strings.trim(lines[0], "' \n"))
	creation_date = strings.clone(strings.trim(lines[len(lines) - 1], "' \n"))

	return
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
		title_id := make_html_friendly_id(title_content, context.temp_allocator)

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
		append(&titles, Title{content = title_content, level = title_level})
	}

	return titles[:]
}

toc_write :: proc(sb: ^strings.Builder, titles: []Title) -> []Title {
	if len(titles) == 0 {return {}}

	title := titles[0]
	id := make_html_friendly_id(title.content, context.temp_allocator)

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
	titles := toc_lex_titles(markdown, context.temp_allocator)
	if len(titles) == 0 {return}

	fmt.println(titles, article_title)

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

	metadata_split := strings.split_n(
		markdown,
		metadata_delimiter + "\n",
		2,
		context.temp_allocator,
	)
	article_content := metadata_split[1]

	decorated_markdown := decorate_markdown_with_title_ids(article_content)
	defer delete(decorated_markdown)

	cmark_output, os2_err := run_sub_process_and_get_stdout(
		cmark_command,
		transmute([]u8)decorated_markdown,
	)
	if os2_err != nil {
		panic(fmt.aprintf("failed to run cmark: %v", os2.error_string(os2_err)))
	}
	defer delete(cmark_output)

	html_sb := strings.builder_make()
	defer strings.builder_destroy(&html_sb)

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
		strings.write_string(&html_sb, "  <span>🏷️")
	}

	for tag, i in article.tags {
		id := make_html_friendly_id(tag, context.temp_allocator)
		fmt.sbprintf(&html_sb, ` <a href="/blog/articles-by-tag.html#%s">%s</a>`, id, tag)

		if i < len(article.tags) - 1 {
			strings.write_string(&html_sb, ", ")
		}
	}

	if len(article.tags) > 0 {
		strings.write_string(&html_sb, "</span>\n")
	}
	strings.write_string(&html_sb, " </div>\n")

	append_article_toc(&html_sb, article_content, article.title)

	strings.write_rune(&html_sb, '\n')
	strings.write_string(&html_sb, cmark_output)
	strings.write_string(&html_sb, back_link)
	strings.write_string(&html_sb, footer)

	os.write_entire_file_or_err(
		article.output_file_name,
		transmute([]u8)strings.to_string(html_sb),
	) or_return

	return
}

generate_article :: proc(
	markdown_file_path: string,
	header: string,
	footer: string,
) -> (
	article: Article,
	err: os.Error,
) {
	assert(len(markdown_file_path) > 0)
	assert(filepath.ext(markdown_file_path) == ".md")
	assert(len(header) > 0)
	assert(len(footer) > 0)

	original_markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
		markdown_file_path,
	) or_return
	defer delete(original_markdown_content)

	stem := filepath.stem(markdown_file_path)

	article.title, article.tags = parse_metadata(original_markdown_content, markdown_file_path)

	os2_err: os2.Error
	article.creation_date, article.modification_date, os2_err =
		get_creation_and_modification_date_for_article(markdown_file_path)
	if os2_err != nil {
		panic(fmt.aprintf("failed to run git: %v", os2.error_string(os2_err)))
	}

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

	cwd := os.open(".") or_return
	defer os.close(cwd)

	files := os.read_dir(cwd, 0) or_return
	defer os.file_info_slice_delete(files)

	for f in files {
		if f.is_dir {continue}
		if filepath.ext(f.name) != ".md" {continue}
		if f.name == "index.md" {continue}
		if f.name == "README.md" {continue}

		article := generate_article(f.name, header, footer) or_return
		append(&articles_dyn, article)

		free_all(context.temp_allocator)
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

	markdown_file_path :: "index.md"
	html_file_path :: "index.html"

	sb := strings.builder_make()
	defer strings.builder_destroy(&sb)

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
		if a.output_file_name == "body_of_work.html" {continue}

		fmt.sbprintf(
			&sb,
			`
	<li>
		<span class="date">%s</span>
		<a href="/blog/%s">%s</a>
	</li>`,
			datetime_to_date(a.creation_date),
			a.output_file_name,
			a.title,
		)
	}

	strings.write_string(&sb, " </ul>\n </div>\n")

	{
		markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
			markdown_file_path,
		) or_return
		defer delete(markdown_content)
		decorated_markdown := decorate_markdown_with_title_ids(markdown_content)
		defer delete(decorated_markdown)

		cmark_stdout, os2_err := run_sub_process_and_get_stdout(
			cmark_command,
			transmute([]u8)decorated_markdown,
		)
		if os2_err != nil {
			panic(fmt.aprintf("failed to run cmark %v", os2_err))
		}
		defer delete(cmark_stdout)
		strings.write_string(&sb, cmark_stdout)
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

	articles_by_tag := make(map[string][dynamic]Article)
	defer delete(articles_by_tag)
	defer for _, a in articles_by_tag {delete(a)}

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
	defer strings.builder_destroy(&sb)
	strings.write_string(&sb, header)
	strings.write_string(&sb, back_link)
	strings.write_string(&sb, "<h1>Articles by tag</h1>\n")
	strings.write_string(&sb, "<ul>\n")

	tags_lexicographically_ordered := make([]string, len(articles_by_tag), context.temp_allocator)

	i := 0
	for tag in articles_by_tag {
		tags_lexicographically_ordered[i] = tag
		i += 1
	}
	slice.sort(tags_lexicographically_ordered)

	for tag in tags_lexicographically_ordered {
		articles_for_tag := articles_by_tag[tag]

		slice.sort_by(articles_for_tag[:], compare_articles_by_creation_date_asc)
		tag_id := make_html_friendly_id(tag, allocator = context.temp_allocator)

		fmt.sbprintf(&sb, `<li id="%s">%s<ul>
		`, tag_id, tag)

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

generate_rss_feed_for_article :: proc(sb: ^strings.Builder, article: Article) {
	base_uuid, err := uuid.read(feed_uuid_str)
	assert(err == nil)
	article_uuid := legacy.generate_v5_string(base_uuid, article.output_file_name)
	article_uuid_str := uuid.to_string(article_uuid, allocator = context.temp_allocator)

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

	sb := strings.builder_make()
	defer strings.builder_destroy(&sb)

	fmt.sbprintf(
		&sb,
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

run :: proc() -> (err: os.Error) {
	// arena: runtime.Arena
	// if err := runtime.arena_init(&arena, 1 << 20, context.allocator); err != nil {
	// 	fmt.eprintln(err)
	// 	os.exit(1)
	// }
	// context.allocator = runtime.arena_allocator(&arena)
	when ODIN_DEBUG {
		track: mem.Tracking_Allocator
		mem.tracking_allocator_init(&track, context.allocator)
		context.allocator = mem.tracking_allocator(&track)

		defer {
			if len(track.allocation_map) > 0 {
				fmt.eprintf("=== %v allocations not freed: ===\n", len(track.allocation_map))
				for _, entry in track.allocation_map {
					fmt.eprintf("- %v bytes @ %v\n", entry.size, entry.location)
				}
			}
			if len(track.bad_free_array) > 0 {
				fmt.eprintf("=== %v incorrect frees: ===\n", len(track.bad_free_array))
				for entry in track.bad_free_array {
					fmt.eprintf("- %p @ %v\n", entry.memory, entry.location)
				}
			}
			mem.tracking_allocator_destroy(&track)
		}
	}
	header := transmute(string)os.read_entire_file_from_filename_or_err("header.html") or_return
	defer delete(header)
	footer := transmute(string)os.read_entire_file_from_filename_or_err("footer.html") or_return
	defer delete(footer)

	articles := generate_all_articles_in_directory(header, footer) or_return
	defer delete(articles)
	defer for &article in articles {
		delete(article.title)
		for tag in article.tags {
			delete(tag)
		}
		delete(article.tags)
		delete(article.creation_date)
		delete(article.modification_date)
		delete(article.output_file_name)
	}
	slice.sort_by(articles, compare_articles_by_creation_date_asc)
	generate_home_page(articles, header, footer) or_return
	generate_page_articles_by_tag(articles, header, footer) or_return
	generate_rss_feed(articles) or_return

	return
}

main :: proc() {
	if err := run(); err != nil {
		panic(fmt.aprintf("%v", err))
	}
}
