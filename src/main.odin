package main

import "cmark"
import "core:c"
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

// Plan:
// - Reader header & footer
// - Get all markdown files from git along with their creation/modification date
// - For each markdown file:
//   + Read from file
//   + Split metadata from markdown content
//   + Parse metadata
//   + Parse markdown
//   + Decorate titles (could be done on HTML or on markdown)
//   + Generate TOC
//   + Generate HTML
// - Generate tags page
// - Generate home page
// - Generate RSS feed

feed_uuid_str :: "9c065c53-31bc-4049-a795-936802a6b1df"
base_url :: "https://gaultier.github.io/blog"
back_link :: "<p><a href=\"/blog\"> ⏴ Back to all articles</a></p>\n"
html_prelude_fmt :: "<!DOCTYPE html>\n<html>\n<head>\n<title>%s</title>\n"
cmark_command :: []string {
	"cmark-gfm",
	"--validate-utf8",
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
	// Titles as a tree.
	titles:            ^Title,
}

// Hash.
TitleId :: u32

Title :: struct {
	content:               string,
	content_html_friendly: string,
	level:                 int,
	id:                    TitleId,
	parent:                ^Title,
	first_child:           ^Title,
	next_sibling:          ^Title,

	// Needed to convert the markdown titles to HTML titles with ids, interspersed with each section content.
	// Also since the `content` field is the trimmed original title, we cannot use its length to deduct those.
	title_start:           int,
	sub_content_start:     int,
	sub_content_len:       int,
}

// FNV hash of the full title path including direct ancestors.
// Conceptually: for `# A\n##B\n###C\n`, we do: `return fnv_hash("A/B/C")`.
title_make_id :: proc(title: ^Title, seed := u32(0x811c9dc5)) -> TitleId {
	// Reached root?
	if title == title.parent {return seed}

	h: u32 = seed
	for b in transmute([]u8)title.content {
		h = (h ~ u32(b)) * 0x01000193
	}
	h = (h ~ u32('/')) * 0x01000193

	return title_make_id(title.parent, h)
}


datetime_to_date :: proc(datetime: string) -> string {
	split := strings.split_n(datetime, "T", 2)
	return split[0]
}

// Metadata is in the form:
// ```
// Title: My great title
// Tags: Foo, Bar
// ---
// The quick brown fox jumps over the lazy dog. Lorem ipsum [...].
// ```
article_parse_metadata :: proc(
	markdown: string,
	path: string,
) -> (
	title: string,
	tags: []string,
	content_without_metadata: string,
) {
	metadata_lines := strings.split_lines_n(markdown, 4)

	assert(len(metadata_lines) >= 4)
	assert(metadata_lines[2] == metadata_delimiter)

	title_line_split := strings.split_n(metadata_lines[0], ": ", 2)
	assert(len(title_line_split) == 2)

	title = strings.clone(strings.trim_space(title_line_split[1]))

	tags_line_split := strings.split_n(metadata_lines[1], ": ", 2)
	assert(len(tags_line_split) == 2)
	tags_split := strings.split(tags_line_split[1], ", ")

	tags = make([]string, len(tags_split))
	for tag, i in tags_split {
		tags[i] = strings.clone(tag)
		assert(!strings.starts_with(tags[i], ","))
	}

	assert(!strings.starts_with(title, "Title:"))

	metadata_split := strings.split_n(markdown, metadata_delimiter + "\n", 2)
	content_without_metadata = metadata_split[1]

	assert(len(tags) > 0)
	return
}

run_sub_process_with_stdin :: proc(
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
		defer os2.close(stdin_w)

		for cur := 0; cur < len(stdin); {
			n_written := os2.write(stdin_w, stdin[cur:]) or_return
			if n_written == 0 {break}
			cur += n_written
		}
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

// See: https://gaultier.github.io/blog/making_my_static_blog_generator_11_times_faster.html .
git_get_articles_creation_and_modification_date :: proc() -> ([]GitStat, os2.Error) {
	free_all(context.temp_allocator)
	defer free_all(context.temp_allocator)

	state, stdout_bin, stderr_bin, err := os2.process_exec(
		{
			command = []string {
				"git",
				"log",
				// Print the date in ISO format.
				"--format='%aI'",
				// Ignore merge commits since they do not carry useful information.
				"--no-merges",
				// Only interested in creation, modification, renaming, deletion.
				"--diff-filter=AMRD",
				// Show which modification took place:
				// A: added, M: modified, RXXX: renamed (with percentage score), etc.
				"--name-status",
				"--reverse",
				"*.md",
			},
		},
		context.temp_allocator,
	)
	_ = state
	_ = stderr_bin
	assert(err == nil)

	stdout := strings.trim_space(string(stdout_bin))
	assert(stdout != "")

	stats_by_path := make(map[string]GitStat, 300, allocator = context.temp_allocator)

	// Sample git output:
	// 2024-10-31T16:09:02+01:00
	// 
	// M       lessons_learned_from_a_successful_rust_rewrite.md
	// A       tip_of_day_3.md
	// 2025-02-18T08:07:55+01:00
	//
	// R100    sha.md  making_my_debug_build_run_100_times_faster.md

	// For each commit.
	for {
		// Date
		date: string
		{
			line := strings.split_lines_iterator(&stdout) or_break

			assert(strings.starts_with(line, "'20"))
			line_without_quotes := line[1:len(line) - 1]
			date = strings.clone(strings.trim(line_without_quotes, "' \n"))
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
			// Start of a new commit?
			if strings.starts_with(stdout, "'20") do break

			line := strings.split_lines_iterator(&stdout) or_break
			assert(line != "")

			action := line[0]
			assert(action == 'A' || action == 'M' || action == 'R' || action == 'D')

			old_path: string
			new_path: string
			{
				// Skip the 'action' part.
				_, ok := strings.split_iterator(&line, "\t")
				assert(ok)

				old_path, ok = strings.split_iterator(&line, "\t")
				assert(ok)
				assert(old_path != "")

				if action == 'R' { 	// Rename has two operands.
					new_path, ok = strings.split_iterator(&line, "\t")
					assert(ok)
					assert(new_path != "")
				} else { 	// The others have only one.
					new_path = old_path
				}
			}


			if action == 'D' {
				delete_key(&stats_by_path, old_path)
				continue
			}

			if action == 'R' {
				delete_key(&stats_by_path, old_path)
				// Still need to insert the new entry in the map below.
			}

			_, v, inserted, err := map_entry(&stats_by_path, new_path)
			assert(err == nil)

			if inserted {
				v.modification_date = date
				v.creation_date = date
			} else {
				assert(v.modification_date != "")
				assert(v.creation_date != "")
				assert(v.creation_date <= v.modification_date)
				// Keep updating the modification date, when we reach the end of the commit log, it has the right value.
				v.modification_date = date
				assert(v.creation_date <= v.modification_date)
			}
		}
	}

	git_stats := make([dynamic]GitStat)
	for k, v in stats_by_path {
		assert(k != "")
		assert(v.creation_date != "")
		assert(v.modification_date != "")
		assert(v.creation_date <= v.modification_date)

		git_stat := GitStat {
			path_rel          = strings.clone(k),
			creation_date     = strings.clone(v.creation_date),
			modification_date = strings.clone(v.modification_date),
		}
		append(&git_stats, git_stat)
	}

	return git_stats[:], nil
}

// Replace non-alphanumeric letters by alphanumeric (and underscore) letters
// for use in the `id` field of HTML elements.
html_make_id :: proc(input: string, allocator := context.allocator) -> string {
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

// Replace plain markdown section title (e.g. `## Lunch and dinner`) by 
// a HTML title with an id (conceptually `<h2 id="456123-lunch-and-dinner">Lunch and dinner</h2>`),
// so that the links in the TOC can point to it.
// In reality it's a bit more HTML so that each title can be a link to itself, and we can copy
// this link to the clipboard by clicking on it.
article_decorate_markdown_titles_with_id :: proc(markdown: string, root: ^Title) -> string {
	assert(root.next_sibling == nil)

	if root.first_child == nil { 	// Nothing to do.
		return markdown
	}
	sb := strings.builder_make_len_cap(0, len(markdown) * 2)
	strings.write_string(&sb, markdown[0:root.first_child.title_start])

	do_rec :: proc(title: ^Title, markdown: string, sb: ^strings.Builder) {
		if title == nil {return}

		fmt.sbprintf(
			sb,
			`<h%d id="%d-%s">
	<a class="title" href="#%d-%s">%s</a>
	<a class="hash-anchor" href="#%d-%s" aria-hidden="true" onclick="navigator.clipboard.writeText(this.href);"></a>
</h%d>
			`,
			title.level,
			title.id,
			title.content_html_friendly,
			title.id,
			title.content_html_friendly,
			title.content,
			title.id,
			title.content_html_friendly,
			title.level,
		)
		strings.write_rune(sb, '\n')

		strings.write_string(
			sb,
			markdown[title.sub_content_start:title.sub_content_start + title.sub_content_len],
		)
		do_rec(title.first_child, markdown, sb)
		do_rec(title.next_sibling, markdown, sb)
	}

	do_rec(root.first_child, markdown, &sb)

	assert(len(sb.buf) > len(markdown))

	return strings.to_string(sb)
}

markdown_parse_titles :: proc(markdown: string, allocator := context.allocator) -> ^Title {
	// Check that we parse markdown without metadata.
	assert(!strings.starts_with(markdown, "Title:"))

	max_titles := 50
	titles := make([dynamic]Title, 0, max_titles, allocator)

	inside_code_section := false
	pos := 0
	root := new(Title, allocator)
	root.level = 1
	root.parent = root

	for {
		idx := strings.index_byte(markdown[pos:], '\n')
		if idx == -1 {break}
		line := markdown[pos:pos + idx]
		pos += idx + 1

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
		assert(1 < title_level && title_level <= 6)

		title_content := strings.trim_space(line[title_level:])
		title := Title {
			content               = title_content,
			content_html_friendly = html_make_id(title_content),
			level                 = title_level,
			sub_content_start     = pos,
			sub_content_len       = len(markdown) - pos, // Will be backpatched.
			title_start           = pos - (idx + 1),
			parent                = root, // Will be backpatched.
			/* Other fields backpatched. */
		}
		append(&titles, title)
	}
	assert(len(titles) <= max_titles)


	// Backpatch `parent`, `sub_content_len` fields.
	for &title, i in titles {
		if i > 0 {
			previous := &titles[i - 1]
			assert(previous.sub_content_start > 0)
			assert(title.title_start > 0)
			previous.sub_content_len = title.title_start - previous.sub_content_start

			level_diff := previous.level - title.level

			if level_diff > 0 { 	// The current title is a (great-)uncle of the current title.
				for _ in 0 ..< level_diff {
					assert(title.parent != nil)
					title.parent = title.parent.parent
				}
			} else if level_diff < 0 { 	// The current title is a direct descendant of `previous`.
				// Check that we do not skip levels e.g. prevent `## Foo\n#### Bar\n`
				assert(level_diff == -1)
				title.parent = previous
			} else if level_diff == 0 { 	// Sibling.
				title.parent = previous.parent
			}
		}
		assert(title.parent.level + 1 == title.level)

		child := title.parent.first_child

		// Add the node as last child of the parent.
		for child != nil && child.next_sibling != nil {
			child = child.next_sibling
		}
		// Already one child present.
		if child != nil {
			child.next_sibling = &title
		} else { 	// First child.
			title.parent.first_child = &title
		}
	}

	// Backpatch `id` field which is a hash of the full path to this node including ancestors.
	for &title in titles {
		title.id = title_make_id(&title)
	}

	assert(root.next_sibling == nil)
	return root
}


// TODO: Use title tree?
article_write_toc_rec :: proc(sb: ^strings.Builder, title: ^Title) {
	if title == nil {return}

	if title.level > 1 {
		fmt.sbprintf(
			sb,
			`
	<li>
		<a href="#%d-%s">%s</a>
		`,
			title.id,
			title.content_html_friendly,
			title.content,
		)
	}


	if title.first_child != nil {strings.write_string(sb, "<ul>\n")}
	article_write_toc_rec(sb, title.first_child)
	if title.first_child != nil {strings.write_string(sb, "</ul>\n")}

	if title.level > 1 {
		strings.write_string(sb, "  </li>\n")
	}

	article_write_toc_rec(sb, title.next_sibling)
}


article_write_toc :: proc(sb: ^strings.Builder, root: ^Title) {
	if root.first_child == nil {return}

	strings.write_string(sb, " <strong>Table of contents</strong>\n")
	article_write_toc_rec(sb, root)
}

article_generate_html_file :: proc(
	article_content: string,
	article: Article,
	header: string,
	footer: string,
) -> (
	err: os.Error,
) {
	assert(len(article_content) > 0)
	assert(len(header) > 0)
	assert(len(footer) > 0)
	assert(len(article.tags) > 0)
	assert(article.titles != nil)
	assert(len(article.creation_date) > 0)
	assert(len(article.modification_date) > 0)
	assert(len(article.output_file_name) > 0)

	context.allocator = context.temp_allocator

	decorated_markdown := article_decorate_markdown_titles_with_id(article_content, article.titles)

	mem := cmark.get_default_mem_allocator()
	parser := cmark.parser_new_with_mem(
		c.int(cmark.OPT_UNSAFE | cmark.OPT_VALIDATE_UTF8 | cmark.OPT_FOOTNOTES),
		mem,
	)

	cmark.core_extensions_ensure_registered()
	ext_table := cmark.find_syntax_extension("table")
	assert(ext_table != nil)
	cmark.parser_attach_syntax_extension(parser, ext_table)

	ext_strikethrough := cmark.find_syntax_extension("strikethrough")
	assert(ext_strikethrough != nil)
	cmark.parser_attach_syntax_extension(parser, ext_strikethrough)

	cmark.parser_feed(parser, raw_data(decorated_markdown), u32(len(decorated_markdown)))
	cmark_parsed := cmark.parser_finish(parser)

	cmark_output_lib := string(cmark.render_html(cmark_parsed, 0, nil))


	// cmark_output_bin, os2_err := run_sub_process_with_stdin(
	// 	cmark_command,
	// 	transmute([]u8)decorated_markdown,
	// )
	// assert(os2_err == nil)
	// cmark_output_bin_s := string(cmark_output_bin)
	// assert(cmark_output_bin_s == cmark_output_lib)

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

	strings.write_string(&html_sb, "  <div class=\"tags\">")
	for tag in article.tags {
		id := html_make_id(tag)
		fmt.sbprintf(
			&html_sb,
			` <a href="/blog/articles-by-tag.html#%s" class="tag">%s</a>`,
			id,
			tag,
		)
	}
	strings.write_string(&html_sb, "</div>\n")

	strings.write_string(&html_sb, " </div>\n")

	article_write_toc(&html_sb, article.titles)

	strings.write_rune(&html_sb, '\n')
	strings.write_string(&html_sb, cmark_output_lib)
	strings.write_string(&html_sb, back_link)
	strings.write_string(&html_sb, footer)

	os.write_entire_file_or_err(
		article.output_file_name,
		transmute([]u8)strings.to_string(html_sb),
	) or_return

	return
}


title_print :: proc(handle: os.Handle, title: ^Title) {
	if title == nil {return}

	assert(title.level > 0)
	for _ in 0 ..< title.level - 2 {
		fmt.fprintf(handle, "  ")
	}
	if title.level == 1 {
		fmt.fprintf(handle, ".\n")
	} else {
		fmt.fprintf(handle, "title='%s' id=%d\n", title.content, title.id)
	}

	title_print(handle, title.first_child)
	title_print(handle, title.next_sibling)
}


article_generate :: proc(
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

	free_all(context.temp_allocator)
	defer free_all(context.temp_allocator)

	original_markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
		git_stat.path_rel,
		allocator = context.temp_allocator,
	) or_return


	content_without_metadata: string
	article.title, article.tags, content_without_metadata = article_parse_metadata(
		original_markdown_content,
		git_stat.path_rel,
	)

	article.creation_date = git_stat.creation_date
	article.modification_date = git_stat.modification_date

	stem := filepath.stem(git_stat.path_rel)
	article.output_file_name = strings.concatenate([]string{stem, ".html"})
	article.titles = markdown_parse_titles(content_without_metadata)
	title_print(os.stdout, article.titles)

	article_generate_html_file(content_without_metadata, article, header, footer) or_return
	fmt.printf("generated article: title=%s\n", article.title)

	return
}

// Note: Only markdown files tracked by `git` are considered.
articles_generate :: proc(header: string, footer: string) -> (articles: []Article, err: os.Error) {
	assert(len(header) > 0)
	assert(len(footer) > 0)

	articles_dyn := make([dynamic]Article)

	git_stats, os2_err := git_get_articles_creation_and_modification_date()
	assert(os2_err == nil)

	for git_stat in git_stats {
		// The home page is generate separately. The logic is different from an article.
		if git_stat.path_rel == "index.md" {continue}

		// Skip the readme.
		if git_stat.path_rel == "README.md" {continue}

		article := article_generate(git_stat, header, footer) or_return
		append(&articles_dyn, article)
	}

	return articles_dyn[:], nil
}

home_page_generate :: proc(
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
	free_all(context.temp_allocator)
	defer free_all(context.allocator)

	slice.sort_by(articles, article_cmp_by_creation_date_desc)

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
			id := html_make_id(tag)
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

		root := markdown_parse_titles(markdown_content)
		title_print(os.stdout, root)
		decorated_markdown := article_decorate_markdown_titles_with_id(markdown_content, root)

		cmark_stdout_bin, os2_err := run_sub_process_with_stdin(
			cmark_command,
			transmute([]u8)decorated_markdown,
		)
		assert(os2_err == nil)

		strings.write_string(&sb, string(cmark_stdout_bin))
	}
	strings.write_string(&sb, footer)

	os.write_entire_file_or_err(html_file_path, transmute([]u8)strings.to_string(sb)) or_return

	fmt.printf("generated %s\n", html_file_path)

	return
}

tags_page_generate :: proc(
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
	free_all(context.temp_allocator)
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

		slice.sort_by(articles_for_tag[:], article_cmp_by_creation_date_asc)
		tag_id := html_make_id(tag)

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

article_cmp_by_creation_date_asc :: proc(a: Article, b: Article) -> bool {
	return a.creation_date < b.creation_date
}

article_cmp_by_creation_date_desc :: proc(a: Article, b: Article) -> bool {
	return a.creation_date > b.creation_date
}

article_rss_generate :: proc(sb: ^strings.Builder, article: Article) {
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

rss_generate :: proc(articles: []Article) -> (err: os.Error) {
	assert(len(articles) > 0)

	context.allocator = context.temp_allocator
	free_all(context.temp_allocator)
	defer free_all(context.allocator)

	slice.sort_by(articles, article_cmp_by_creation_date_asc)

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
		article_rss_generate(&sb, a)
	}

	strings.write_string(&sb, "</feed>")

	fmt.printf("generated RSS feed for %d articles\n", len(articles))

	os.write_entire_file_or_err("feed.xml", transmute([]u8)strings.to_string(sb)) or_return
	return
}

run :: proc() -> (os_err: os.Error) {

	header := transmute(string)os.read_entire_file_from_filename_or_err("header.html") or_return
	footer := transmute(string)os.read_entire_file_from_filename_or_err("footer.html") or_return

	articles := articles_generate(header, footer) or_return
	home_page_generate(articles, header, footer) or_return
	tags_page_generate(articles, header, footer) or_return
	rss_generate(articles) or_return

	return
}

main :: proc() {
	arena: virtual.Arena
	defer fmt.println(arena)

	{
		arena_size := uint(1) * mem.Megabyte
		mmaped, err := virtual.reserve_and_commit(arena_size)
		assert(err == nil)

		err = virtual.arena_init_buffer(&arena, mmaped)
		assert(err == nil)
	}
	context.allocator = virtual.arena_allocator(&arena)


	tmp_arena: virtual.Arena
	defer fmt.println(tmp_arena)
	{
		tmp_arena_size := uint(1) * mem.Megabyte
		tmp_mmaped, err := virtual.reserve_and_commit(tmp_arena_size)
		assert(err == nil)

		err = virtual.arena_init_buffer(&tmp_arena, tmp_mmaped)
		assert(err == nil)
	}
	context.temp_allocator = virtual.arena_allocator(&tmp_arena)

	err := run()
	assert(err == nil)
}
