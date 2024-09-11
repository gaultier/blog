package main

import "core:fmt"
import "core:os"
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

generate_article :: proc(
	markdown_file_path: string,
	header: []byte,
	footer: []byte,
) -> (
	article: Article,
	err: os.Error,
) {
	original_markdown_content := os.read_entire_file_from_filename_or_err(
		markdown_file_path,
	) or_return

	stem := filepath.stem(markdown_file_path)

	article.title, article.tags = parse_metadata(
		transmute(string)original_markdown_content,
		markdown_file_path,
	)
	fmt.println(markdown_file_path, article.title, article.tags)

	article.output_file_name = strings.concatenate([]string{stem, ".html"})


	return
}

generate_all_articles_in_directory :: proc(
	header: []byte,
	footer: []byte,
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
	header := os.read_entire_file_from_filename_or_err("header.html") or_return
	footer := os.read_entire_file_from_filename_or_err("footer.html") or_return

	articles := generate_all_articles_in_directory(header, footer) or_return
	defer delete(articles)

	return
}

main :: proc() {
	if err := run(); err != nil {
		panic(fmt.aprintf("%v", err))
	}
}
