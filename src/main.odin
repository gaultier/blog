package main

import "core:fmt"
import "core:os"
import "core:os/os2"
import "core:path/filepath"
import "core:strings"

back_link :: `<p><a href="/blog"> ⏴ Back to all articles</a>\n`
html_prelude_fmt :: "<!DOCTYPE html>\n<html>\n<head>\n<title>%s</title>\n"

Article :: struct {
	output_file_name:  string,
	title:             string,
	tags:              []string,
	creation_date:     string,
	modification_date: string,
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
	if metadata_lines[2] != "---" {
		panic(fmt.aprintf("file %s missing metadata delimiter", path))
	}

	title_line_split := strings.split(metadata_lines[0], ": ", allocator = context.temp_allocator)
	if len(title_line_split) < 2 {
		panic(fmt.aprintf("file %s has a malformed metadata title", path))
	}
	title = strings.clone(title_line_split[1])

	tags_line_split := strings.split(metadata_lines[1], ": ", allocator = context.temp_allocator)
	if len(tags_line_split) != 2 {
		panic(fmt.aprintf("file %s has a malformed metadata tags", path))
	}
	tags_split := strings.split(tags_line_split[1], ", ", allocator = context.temp_allocator)

	tags = make([]string, len(tags_split))
	for tag, i in tags_split {
		tags[i] = strings.clone(tag)
	}

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

	return strings.trim_space(strings.to_string(stdout_sb)), nil
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
		[]u8{},
	) or_return
	defer delete(stdout)

	lines := strings.split_lines(stdout, context.temp_allocator)
	creation_date = strings.clone(lines[0])
	modification_date = strings.clone(lines[len(lines) - 1])

	return
}

make_html_friendly_id :: proc(title_content: string) -> string {
	builder := strings.builder_make_len_cap(0, len(title_content) * 2)

	for c in title_content {
		switch c {
		case 'A' ..= 'Z', 'a' ..= 'z', 0 ..= 9:
			strings.write_rune(&builder, c)
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

	return strings.to_string(builder)
}

fixup_markdown_with_title_ids :: proc(markdown: ^string) -> string {
	inside_code_section := false

	builder := strings.builder_make_len_cap(0, len(markdown) * 2)

	for line in strings.split_lines_iterator(markdown) {
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
		title_id := make_html_friendly_id(title_content)

		fmt.sbprintf(
			&builder,
			`<h%d id="%s"><a class="title" href="#%s">%s</a><a class="hash-anchor" href="#%s" aria-hidden="true" onclick="navigator.clipboard.writeText(this.href);"></a></h%d>`,
			title_level,
			title_id,
			title_content,
			title_id,
			title_level,
		)
	}
	return strings.to_string(builder)
}

generate_html_article :: proc(
	markdown: ^string,
	article: Article,
	header: string,
	footer: string,
) -> (
	err: os.Error,
) {
	fixed_up_markdown := fixup_markdown_with_title_ids(markdown)
	defer delete(fixed_up_markdown)

	cmark_output, os2_err := run_sub_process_and_get_stdout(
		[]string{"cmark", "--unsafe", "-t", "html"},
		transmute([]u8)fixed_up_markdown,
	)
	if os2_err != nil {
		return .Unknown
	}

	html_sb := strings.builder_make()
	fmt.sbprintf(&html_sb, html_prelude_fmt, article.title)
	strings.write_string(&html_sb, header)
	fmt.sbprintf(
		&html_sb,
		`<div class="article-prelude">%s <p class="publication-date">Published on %s</p></div><div class="article-title"><h1>%s</h1>`,
		back_link,
		datetime_to_date(article.creation_date),
		article.title,
	)

	if len(article.tags) > 0 {
		strings.write_string(&html_sb, "  <span>🏷️")
	}

	for tag, i in article.tags {
		id := make_html_friendly_id(tag)
		defer delete(id)

		fmt.sbprintf(&html_sb, ` <a href="/blog/articles-by-tag.html#%s">%s</a>`, id, tag)

		if i < len(article.tags) - 1 {
			strings.write_string(&html_sb, ", ")
		}
	}

	if len(article.tags) > 0 {
		strings.write_string(&html_sb, "</span>\n")
	}
	strings.write_string(&html_sb, "</div>\n")

	// TODO: TOC

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
	original_markdown_content := transmute(string)os.read_entire_file_from_filename_or_err(
		markdown_file_path,
	) or_return

	stem := filepath.stem(markdown_file_path)

	article.title, article.tags = parse_metadata(original_markdown_content, markdown_file_path)

	os2_err: os2.Error
	article.creation_date, article.modification_date, os2_err =
		get_creation_and_modification_date_for_article(markdown_file_path)
	if os2_err != nil {
		panic(fmt.aprintf("failed to get dates %v", os2_err))
	}

	article.output_file_name = strings.concatenate([]string{stem, ".html"})

	fmt.println(markdown_file_path, article)

	generate_html_article(&original_markdown_content, article, header, footer) or_return

	return
}

generate_all_articles_in_directory :: proc(
	header: string,
	footer: string,
) -> (
	articles: []Article,
	err: os.Error,
) {
	articles_dyn := make([dynamic]Article)

	cwd := os.open(".") or_return
	defer os.close(cwd)

	files := os.read_dir(cwd, 0) or_return
	defer delete(files)

	for f in files {
		if f.is_dir {continue}
		if filepath.ext(f.name) != ".md" {continue}
		if f.name == "index.md" {continue}
		if f.name == "README.md" {continue}

		article := generate_article(f.name, header, footer) or_return
		append(&articles_dyn, article)
	}

	return articles_dyn[:], nil
}

run :: proc() -> (err: os.Error) {
	header := transmute(string)os.read_entire_file_from_filename_or_err("header.html") or_return
	footer := transmute(string)os.read_entire_file_from_filename_or_err("footer.html") or_return

	articles := generate_all_articles_in_directory(header, footer) or_return
	defer delete(articles)

	return
}

main :: proc() {
	if err := run(); err != nil {
		panic(fmt.aprintf("%v", err))
	}
}
