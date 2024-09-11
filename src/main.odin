package main

import "core:fmt"
import "core:os"
import "core:os/os2"
import "core:path/filepath"
import "core:strings"

Article :: struct {
	output_file_name:  string,
	title:             string,
	tags:              []string,
	creation_date:     string,
	modification_date: string,
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

get_creation_and_modification_date_for_article :: proc(
	path: string,
) -> (
	creation_date: string,
	modification_date: string,
	err: os2.Error,
) {
	stdout_r, _ := os2.pipe() or_return

	desc := os2.Process_Desc {
		command = []string{"git", "log", "--format='%aI'", "--", path},
		stdout  = stdout_r,
	}

	process := os2.process_start(desc) or_return
	process_state := os2.process_wait(process) or_return
	if !process_state.success {
		return {}, {}, .Unknown
	}

	process_output := transmute(string)os2.read_entire_file_from_file(
		stdout_r,
		context.temp_allocator,
	) or_return
	first_line, ok := strings.split_lines_iterator(&process_output)
	assert(ok)
	creation_date = strings.clone(first_line)

	line: string

	for ; ok; line, ok = strings.split_lines_iterator(&process_output) {}

	modification_date = strings.clone(line)

	return
}

generate_article :: proc(
	markdown_file_path: string,
	header: []byte,
	footer: []byte,
) -> (
	article: Article,
	err: os2.Error,
) {
	original_markdown_content := os2.read_entire_file_from_path(
		markdown_file_path,
		context.allocator,
	) or_return

	stem := filepath.stem(markdown_file_path)

	article.title, article.tags = parse_metadata(
		transmute(string)original_markdown_content,
		markdown_file_path,
	)
	article.creation_date, article.modification_date =
		get_creation_and_modification_date_for_article(markdown_file_path) or_return
	fmt.println(markdown_file_path, article)

	article.output_file_name = strings.concatenate([]string{stem, ".html"})


	return
}

generate_all_articles_in_directory :: proc(
	header: []byte,
	footer: []byte,
) -> (
	articles: []Article,
	err: os2.Error,
) {
	articles_dyn := make([dynamic]Article)

	cwd := os2.open(".") or_return
	defer os2.close(cwd)

	files := os2.read_dir(cwd, 0, context.allocator) or_return
	defer delete(files)

	for f in files {
		if f.type != .Regular {continue}
		if filepath.ext(f.name) != ".md" {continue}
		if f.name == "index.md" {continue}
		if f.name == "README.md" {continue}

		article := generate_article(f.name, header, footer) or_return
		append(&articles_dyn, article)
	}

	return articles_dyn[:], nil
}

run :: proc() -> (err: os2.Error) {
	header := os2.read_entire_file_from_path("header.html", context.allocator) or_return
	footer := os2.read_entire_file_from_path("footer.html", context.allocator) or_return

	articles := generate_all_articles_in_directory(header, footer) or_return
	defer delete(articles)

	return
}

main :: proc() {
	if err := run(); err != nil {
		panic(fmt.aprintf("%v", err))
	}
}
