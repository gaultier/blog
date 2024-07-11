const std = @import("std");

pub const std_options = .{
    .log_level = .info,
};

const tag_icon =
    \\ <svg aria-hidden="true" focusable="false" role="img" viewBox="0 0 16 16" width="16" height="16" fill="currentColor" style="display:inline-block;user-select:none;overflow:visible"><path d="M1 7.775V2.75C1 1.784 1.784 1 2.75 1h5.025c.464 0 .91.184 1.238.513l6.25 6.25a1.75 1.75 0 0 1 0 2.474l-5.026 5.026a1.75 1.75 0 0 1-2.474 0l-6.25-6.25A1.752 1.752 0 0 1 1 7.775Zm1.5 0c0 .066.026.13.073.177l6.25 6.25a.25.25 0 0 0 .354 0l5.025-5.025a.25.25 0 0 0 0-.354l-6.25-6.25a.25.25 0 0 0-.177-.073H2.75a.25.25 0 0 0-.25.25ZM6 5a1 1 0 1 1 0 2 1 1 0 0 1 0-2Z"></path></svg>
;
const back_link = "<p><a href=\"/blog\"> ⏴ Back to all articles</a>\n";
const base_url = "https://gaultier.github.io/blog";
const feed_uuid_str = "9c065c53-31bc-4049-a795-936802a6b1df";
const feed_uuid_raw = [16]u8{ 0x9c, 0x06, 0x5c, 0x53, 0x31, 0xbc, 0x40, 0x49, 0xa7, 0x95, 0x93, 0x68, 0x02, 0xa6, 0xb1, 0xdf };
const html_prelude = "<!DOCTYPE html>\n<html>\n<head>\n<title>{s}</title>\n";

const Article = struct {
    dates: Dates,
    output_file_name: []u8,
    metadata: Metadata,
    allocator: std.mem.Allocator,

    pub fn deinit(self: *Article) void {
        self.allocator.free(self.output_file_name);
        self.metadata.deinit();
    }
};

fn stringLess(ctx: void, a: []const u8, b: []const u8) bool {
    _ = ctx;
    return std.mem.order(u8, a, b) == .lt;
}

fn articlesPtrOrderedByCreationDateAsc(ctx: void, a: *const Article, b: *const Article) bool {
    return articlesOrderedByCreationDateAsc(ctx, a.*, b.*);
}

fn articlesOrderedByCreationDateAsc(ctx: void, a: Article, b: Article) bool {
    _ = ctx;
    return std.mem.lessThan(u8, &a.dates.creation_date, &b.dates.creation_date);
}

fn get_date(datetime: [iso_date_str_len]u8) []const u8 {
    return datetime[0..10];
}

fn do_generate_article(markdown_file_path: []const u8, header: []const u8, footer: []const u8, wait_group: *std.Thread.WaitGroup, articles: *std.ArrayList(Article), articles_mtx: *std.Thread.Mutex, allocator: std.mem.Allocator) void {
    defer wait_group.finish();

    const article = generate_article(markdown_file_path, header, footer, allocator) catch |err| {
        std.log.err("failed to generate article: {s} {}", .{ markdown_file_path, err });
        return;
    };

    articles_mtx.lock();
    articles.append(article) catch {
        @panic("oom");
    };
    articles_mtx.unlock();
}

const iso_date_str_len = "2023-12-15T12:23:43+01:00".len;
const Dates = struct {
    creation_date: [iso_date_str_len]u8,
    modification_date: [iso_date_str_len]u8,
};

fn get_creation_and_modification_date_for_article(markdown_file_path: []const u8, allocator: std.mem.Allocator) !Dates {
    const git_cmd = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = &[_][]const u8{ "git", "log", "--format='%aI'", "--", markdown_file_path },
    });
    defer allocator.free(git_cmd.stdout);
    defer allocator.free(git_cmd.stderr);

    std.debug.assert(git_cmd.stderr.len == 0);

    var it = std.mem.splitScalar(u8, git_cmd.stdout, '\n');
    const modification_date = std.mem.trim(u8, it.first(), "' \n");

    var creation_date: []const u8 = undefined;
    while (it.next()) |line| {
        const trimmed = std.mem.trim(u8, line, "' \n");
        if (trimmed.len > 0) creation_date = trimmed;
    }

    std.debug.assert(creation_date.len == iso_date_str_len);
    std.debug.assert(modification_date.len == iso_date_str_len);

    for (creation_date) |c| {
        const ok = switch (c) {
            '-', '0'...'9', 'T', 'Z', ':', '+' => true,
            else => false,
        };
        std.debug.assert(ok);
    }
    for (modification_date) |c| {
        const ok = switch (c) {
            '-', '0'...'9', 'T', 'Z', ':', '+' => true,
            else => false,
        };
        std.debug.assert(ok);
    }

    return .{
        .creation_date = creation_date[0..iso_date_str_len].*,
        .modification_date = modification_date[0..iso_date_str_len].*,
    };
}

fn sha1_uuid(space_uuid: [16]u8, data: []const u8) [36]u8 {
    var sha1 = std.crypto.hash.Sha1.init(.{});
    sha1.update(&space_uuid);
    sha1.update(data);

    var uuid = sha1.finalResult();

    uuid[6] = (uuid[6] & 0x0f) | 80;
    uuid[8] = (uuid[8] & 0x3f) | 0x80;

    var res = [_]u8{0} ** 36;

    const hex1 = std.fmt.bytesToHex(uuid[0..4], .lower);
    std.mem.copyForwards(u8, res[0..8], &hex1);
    res[8] = '-';
    const hex2 = std.fmt.bytesToHex(uuid[4..6], .lower);
    std.mem.copyForwards(u8, res[9..13], &hex2);
    res[13] = '-';
    const hex3 = std.fmt.bytesToHex(uuid[6..8], .lower);
    std.mem.copyForwards(u8, res[14..18], &hex3);
    res[18] = '-';
    const hex4 = std.fmt.bytesToHex(uuid[8..10], .lower);
    std.mem.copyForwards(u8, res[19..23], &hex4);
    res[23] = '-';
    const hex5 = std.fmt.bytesToHex(uuid[10..16], .lower);
    std.mem.copyForwards(u8, res[24..], &hex5);

    return res;
}

fn generate_rss_feed_entry_for_article(writer: anytype, article: Article) !void {
    const template =
        \\<entry>
        \\  <title>{s}</title>
        \\  <link href="{s}/{s}"/>
        \\  <id>urn:uuid:{s}</id>
        \\  <updated>{s}</updated>
        \\  <published>{s}</published>
        \\</entry>
        \\
    ;
    const uuid = sha1_uuid(feed_uuid_raw, article.output_file_name);

    try std.fmt.format(writer, template, .{
        article.metadata.title,
        base_url,
        article.output_file_name,
        uuid,
        article.dates.modification_date,
        article.dates.creation_date,
    });
}

fn run_cmark_with_stdin_data(markdown: []const u8, allocator: std.mem.Allocator) ![]const u8 {
    const argv = &[_][]const u8{ "cmark", "--unsafe", "-t", "html" };

    var child = std.process.Child.init(argv, allocator);
    child.stdin_behavior = .Pipe;
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Pipe;

    try std.process.Child.spawn(&child);
    if (child.stdin) |*stdin| {
        try stdin.writeAll(markdown);

        stdin.close();
        child.stdin = null;
    } else {
        @panic("no stdin");
    }

    var stdout = std.ArrayList(u8).init(allocator);
    defer stdout.deinit();
    var stderr = std.ArrayList(u8).init(allocator);
    defer stderr.deinit();

    try child.collectOutput(&stdout, &stderr, 10 * 1024 * 1024);

    _ = try child.wait();
    if (stderr.items.len > 0) {
        std.log.err("cmark had errors: {s}", .{stderr.items});
    }
    return stdout.toOwnedSlice();
}

fn generate_html_article(markdown: []const u8, article: Article, header: []const u8, footer: []const u8, html_file_path: []const u8, allocator: std.mem.Allocator) !void {
    const fixed_up_markdown = try fixup_markdown_with_title_ids(markdown, allocator);
    defer allocator.free(fixed_up_markdown);
    const child_stdout = try run_cmark_with_stdin_data(fixed_up_markdown, allocator);
    defer allocator.free(child_stdout);

    const html_file = try std.fs.cwd().createFile(html_file_path, .{});
    defer html_file.close();
    try std.fmt.format(html_file.writer(), html_prelude, .{article.metadata.title});
    try html_file.writeAll(header);
    try std.fmt.format(html_file.writer(),
        \\ <div class="article-prelude">
        \\   {s}
        \\   <p class="publication-date">Published on {s}</p>
        \\ </div>
        \\ 
        \\ <div class="article-title">
        \\   <h1>{s}</h1>
        \\   <span>🏷️ 
    , .{ back_link, get_date(article.dates.creation_date), article.metadata.title });

    for (article.metadata.tags, 0..) |tag, i| {
        const id = try make_html_friendly_id(tag, allocator);
        defer allocator.free(id);

        try std.fmt.format(html_file.writer(),
            \\ <a href="/blog/articles-by-tag.html#{s}">{s}</a>
        , .{ id, tag });

        if (i < article.metadata.tags.len - 1) {
            try html_file.writeAll(", ");
        }
    }
    try html_file.writeAll(
        \\</span>
        \\ </div>
        \\ 
    );

    const toc = try generate_toc_for_article(markdown, allocator);
    defer allocator.free(toc);

    if (toc.len > 0) {
        try html_file.writeAll("<strong>Table of contents</strong>\n");
        try html_file.writeAll(toc);
    }
    try html_file.writeAll("\n");

    try html_file.writeAll(child_stdout);
    try html_file.writer().writeAll(back_link);
    try html_file.writeAll(footer);
}

const Metadata = struct {
    title: []const u8,
    tags: []const []const u8,
    end_offset: usize,

    allocator: std.mem.Allocator,

    fn deinit(self: *Metadata) void {
        self.allocator.free(self.title);
        for (self.tags) |tag| {
            self.allocator.free(tag);
        }
        self.allocator.free(self.tags);
    }
};

fn parse_metadata(markdown: []const u8, allocator: std.mem.Allocator) !Metadata {
    const metadata_delimiter = "---\n";
    const metadata_delimiter_idx = std.mem.indexOf(u8, markdown, metadata_delimiter) orelse @panic("missing metadata");

    const metadata_str = markdown[0..metadata_delimiter_idx];

    var it_newline = std.mem.splitScalar(u8, metadata_str, '\n');
    const title_prefix = "Title: ";
    const tags_prefix = "Tags: ";

    var metadata: Metadata = .{
        .allocator = allocator,
        .title = undefined,
        .tags = undefined,
        .end_offset = metadata_delimiter_idx + metadata_delimiter.len,
    };
    var tags = std.ArrayList([]const u8).init(allocator);
    errdefer tags.deinit();

    while (it_newline.next()) |line| {
        if (std.mem.startsWith(u8, line, title_prefix)) {
            const title_str = std.mem.trim(u8, line[title_prefix.len..], "\n ");
            std.debug.assert(std.mem.indexOfScalar(u8, title_str, '#') == null);

            metadata.title = try allocator.dupe(u8, title_str);
        } else if (std.mem.startsWith(u8, line, tags_prefix)) {
            const tags_str = std.mem.trim(u8, line[tags_prefix.len..], "\n ");

            var it_commas = std.mem.split(u8, tags_str, ", ");
            while (it_commas.next()) |tag_str| {
                const trimmed = std.mem.trim(u8, tag_str, "\n ");
                try tags.append(try allocator.dupe(u8, trimmed));
            }
        } else break;
    }
    metadata.tags = try tags.toOwnedSlice();

    return metadata;
}

fn fixup_markdown_with_title_ids(markdown: []const u8, allocator: std.mem.Allocator) ![]const u8 {
    var it_lines = std.mem.splitScalar(u8, markdown, '\n');

    var res = std.ArrayList(u8).init(allocator);
    defer res.deinit();

    var inside_code_section = false;
    while (it_lines.next()) |line| {
        const is_begin_title_html = std.mem.startsWith(u8, line, "<h");
        std.debug.assert(!is_begin_title_html);

        const is_begin_title = std.mem.startsWith(u8, line, "#");
        const is_begin_code_section = std.mem.startsWith(u8, line, "```");

        if (is_begin_code_section and !inside_code_section) {
            inside_code_section = true;
            try res.appendSlice(line);
            try res.append('\n');
            continue;
        }
        if (is_begin_code_section and inside_code_section) {
            inside_code_section = false;
            try res.appendSlice(line);
            try res.append('\n');
            continue;
        }

        if (inside_code_section) {
            try res.appendSlice(line);
            try res.append('\n');
            continue;
        }
        if (!is_begin_title) {
            try res.appendSlice(line);
            try res.append('\n');
            continue;
        }

        const first_space_pos = std.mem.indexOf(u8, line, " ") orelse 0;
        const level = first_space_pos;
        std.debug.assert(1 <= level and level <= 6);

        const title = std.mem.trim(u8, line[first_space_pos..], &[_]u8{ ' ', '\n' });
        const id = try make_html_friendly_id(title, allocator);

        try std.fmt.format(res.writer(),
            \\<h{d} id="{s}">{s}</h{d}>
            \\
        , .{ level, id, title, level });
    }
    return res.toOwnedSlice();
}

fn generate_article(markdown_file_path: []const u8, header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !Article {
    std.debug.assert(std.mem.eql(u8, std.fs.path.extension(markdown_file_path), ".md"));
    std.debug.assert(header.len > 0);
    std.debug.assert(footer.len > 0);

    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    const original_markdown_content: []const u8 = try markdown_file.readToEndAlloc(allocator, 1 * 1024 * 1024);
    defer allocator.free(original_markdown_content);

    const stem = std.fs.path.stem(markdown_file_path);
    std.debug.assert(stem.len != 0);

    var article: Article = .{
        .dates = undefined,
        .output_file_name = try std.mem.concat(allocator, u8, &[2][]const u8{ stem, ".html" }),
        .metadata = try parse_metadata(original_markdown_content, allocator),
        .allocator = allocator,
    };

    var markdown_content = std.ArrayList(u8).init(allocator);
    defer markdown_content.deinit();
    try markdown_content.appendSlice(original_markdown_content[article.metadata.end_offset..]);

    article.dates = try get_creation_and_modification_date_for_article(markdown_file_path, allocator);

    try generate_html_article(markdown_content.items, article, header, footer, article.output_file_name, allocator);

    std.log.info("generated {s} {s}", .{ article.output_file_name, article.metadata.tags });

    return article;
}

fn generate_page_articles_by_tag(articles: []Article, header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !void {
    const tags_file = try std.fs.cwd().createFile("articles-by-tag.html", .{});
    defer tags_file.close();

    var buffered_writer = std.io.bufferedWriter(tags_file.writer());
    try buffered_writer.writer().writeAll(header);
    try buffered_writer.writer().writeAll(back_link);
    try buffered_writer.writer().writeAll("<h1>Articles by tag</h1>\n");

    var articles_by_tag = std.StringArrayHashMap(std.ArrayList(*const Article)).init(allocator);
    defer articles_by_tag.deinit();
    defer for (articles_by_tag.values()) |v| {
        v.deinit();
    };

    for (articles, 0..) |article, i| {
        for (article.metadata.tags) |tag| {
            std.debug.assert(tag.len > 0);

            var entry = try articles_by_tag.getOrPut(tag);
            if (!entry.found_existing) {
                entry.value_ptr.* = std.ArrayList(*const Article).init(allocator);
            }
            try entry.value_ptr.append(&articles[i]);
        }
    }

    try buffered_writer.writer().writeAll("<ul>\n");
    const keys = try allocator.dupe([]const u8, articles_by_tag.keys());
    defer allocator.free(keys);
    std.mem.sort([]const u8, keys, {}, stringLess);

    for (keys) |tag| {
        const articles_for_tag = articles_by_tag.get(tag) orelse undefined;
        std.mem.sort(*const Article, articles_for_tag.items, {}, articlesPtrOrderedByCreationDateAsc);

        const tag_id = try make_html_friendly_id(tag, allocator);
        defer allocator.free(tag_id);

        try std.fmt.format(buffered_writer.writer(), "<li id=\"{s}\">{s}<ul>\n", .{ tag_id, tag });

        for (articles_for_tag.items) |article_for_tag| {
            try std.fmt.format(buffered_writer.writer(), "<li><span class=\"date\">{s}</span> <a href={s}>{s}</a></li>\n", .{ get_date(article_for_tag.dates.creation_date), article_for_tag.output_file_name, article_for_tag.metadata.title });
        }

        try buffered_writer.writer().writeAll("</ul></li>\n");
    }
    try buffered_writer.writer().writeAll("</ul>\n");

    try buffered_writer.writer().writeAll(footer);

    try buffered_writer.flush();
}

fn generate_rss_feed(articles: []Article) !void {
    const feed_file = try std.fs.cwd().createFile("feed.xml", .{});
    defer feed_file.close();

    var buffered_writer = std.io.bufferedWriter(feed_file.writer());

    const template_prelude =
        \\<?xml version="1.0" encoding="utf-8"?>
        \\  <feed xmlns="http://www.w3.org/2005/Atom">
        \\    <title>Philippe Gaultier's blog</title>
        \\    <link href="{s}"/>
        \\    <updated>{s}</updated>
        \\    <author>
        \\      <name>Philippe Gaultier</name>
        \\    </author>
        \\    <id>urn:uuid:{s}</id>
        \\
    ;
    std.debug.assert(articles.len > 10);

    try std.fmt.format(buffered_writer.writer(), template_prelude, .{
        base_url,
        articles[articles.len - 1].dates.modification_date,
        feed_uuid_str,
    });

    for (articles) |article| {
        try generate_rss_feed_entry_for_article(buffered_writer.writer(), article);
    }

    try buffered_writer.writer().writeAll("</feed>");
    try buffered_writer.flush();

    std.log.info("generated RSS feed for {} articles", .{articles.len});
}

fn generate_toc_for_article(markdown: []const u8, allocator: std.mem.Allocator) ![]u8 {
    var it_lines = std.mem.splitScalar(u8, markdown, '\n');

    var inside_code_section = false;

    const Title = struct { title: []const u8, level: usize };
    var titles = std.ArrayList(Title).init(allocator);
    defer titles.deinit();
    {
        while (it_lines.next()) |line| {
            const is_begin_title_html = std.mem.startsWith(u8, line, "<h");
            std.debug.assert(!is_begin_title_html);

            const is_begin_title = std.mem.startsWith(u8, line, "#");
            const is_begin_code_section = std.mem.startsWith(u8, line, "```");

            if (is_begin_code_section and !inside_code_section) {
                inside_code_section = true;
                continue;
            }
            if (is_begin_code_section and inside_code_section) {
                inside_code_section = false;
                continue;
            }

            if (inside_code_section) continue;
            if (!is_begin_title) continue;

            const first_space_pos = std.mem.indexOf(u8, line, " ") orelse 0;
            const level = first_space_pos;
            std.debug.assert(1 <= level and level <= 6);

            const title = std.mem.trim(u8, line[first_space_pos..], &[_]u8{ ' ', '\n' });
            try titles.append(.{ .title = title, .level = level });
        }
    }

    if (titles.items.len == 0) return &.{};

    var toc = std.ArrayList(u8).init(allocator);
    try toc.ensureTotalCapacity(4096);

    {
        var level_old: usize = 0;
        for (titles.items) |title| {
            if (title.level < level_old) {
                try toc.appendSlice("</ul>\n");
            }
            if (title.level > level_old) {
                try toc.appendSlice("<ul>\n");
            }

            const id = try make_html_friendly_id(title.title, allocator);
            defer allocator.free(id);

            try std.fmt.format(toc.writer(),
                \\ <li><a href="#{s}">{s}</a></li>
                \\
            , .{ id, title.title });

            level_old = title.level;
        }
        try toc.appendSlice("</ul>\n");
    }
    return toc.toOwnedSlice();
}

fn make_html_friendly_id(s: []const u8, allocator: std.mem.Allocator) ![]const u8 {
    var id = std.ArrayList(u8).init(allocator);
    try id.ensureTotalCapacity(s.len * 2);
    defer id.deinit();

    for (s) |c| {
        if (std.ascii.isAlphanumeric(c)) {
            try id.append(std.ascii.toLower(c));
        } else if (c == '+') {
            try id.appendSlice("plus");
        } else if (id.items.len > 0 and id.items[id.items.len - 1] != '-') {
            try id.append('-');
        }
    }
    const final_id = std.mem.trim(u8, id.items, "-");

    return allocator.dupe(u8, final_id);
}

fn generate_home_page(header: []const u8, articles: []const Article, allocator: std.mem.Allocator) !void {
    std.debug.assert(header.len > 0);

    const markdown_file_path = "index.md";
    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    const markdown_content = try markdown_file.readToEndAlloc(allocator, 1 * 1024 * 1024);
    defer allocator.free(markdown_content);

    const output_file_name = "index.html";
    const html_file = try std.fs.cwd().createFile(output_file_name, .{});
    defer html_file.close();

    try std.fmt.format(html_file.writer(), html_prelude, .{"Philippe Gaultier's blog"});
    try html_file.writeAll(header);

    try html_file.writeAll(
        \\ <div class="articles">
        \\ <h2 id="articles">Articles</h2>
        \\ <ul>
        \\
    );

    for (articles) |article| {
        if (std.mem.eql(u8, article.output_file_name, "body_of_work.html")) continue;

        try std.fmt.format(html_file.writer(),
            \\<li>
            \\  <span class="date">{s}</span>
            \\  <a href="/blog/{s}">{s}</a>
            \\</li>
            \\
        , .{ get_date(article.dates.creation_date), article.output_file_name, article.metadata.title });
    }

    try html_file.writeAll(
        \\ </ul>
        \\ </div>
        \\
    );

    {
        const fixed_up_markdown = try fixup_markdown_with_title_ids(markdown_content, allocator);
        defer allocator.free(fixed_up_markdown);
        const child_stdout = try run_cmark_with_stdin_data(fixed_up_markdown, allocator);
        defer allocator.free(child_stdout);

        try html_file.writeAll(child_stdout);
    }

    std.log.info("generated {s}", .{output_file_name});
}

fn generate_all_articles_in_dir(header: []const u8, footer: []const u8, allocator: std.mem.Allocator) ![]Article {
    const cwd = try std.fs.cwd().openDir(".", .{ .iterate = true });
    var cwd_iterator = cwd.iterate();

    var pool: std.Thread.Pool = undefined;
    try std.Thread.Pool.init(&pool, .{ .allocator = allocator });
    var wait_group = std.Thread.WaitGroup{};

    var articles = std.ArrayList(Article).init(allocator);
    var articles_mtx = std.Thread.Mutex{};

    while (cwd_iterator.next()) |entry_opt| {
        if (entry_opt) |entry| {
            // Skip non markdown files.
            if (entry.kind != .file) continue;
            if (!std.mem.eql(u8, std.fs.path.extension(entry.name), ".md")) continue;

            if (std.mem.eql(u8, entry.name, "index.md")) continue;
            if (std.mem.eql(u8, entry.name, "README.md")) continue;

            wait_group.start();
            pool.spawnWg(&wait_group, do_generate_article, .{
                try allocator.dupe(u8, entry.name),
                header,
                footer,
                &wait_group,
                &articles,
                &articles_mtx,
                allocator,
            });
        } else break; // End of directory.
    } else |err| {
        std.log.err("failed to iterate over directory entries: {}", .{err});
    }

    wait_group.wait();

    return articles.toOwnedSlice();
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    const allocator = gpa.allocator();

    var args_iterator = try std.process.argsWithAllocator(allocator);
    defer args_iterator.deinit();
    _ = args_iterator.skip();

    const header_file = try std.fs.cwd().openFile("header.html", .{});
    const header = try header_file.readToEndAlloc(allocator, 2048);

    const footer_file = try std.fs.cwd().openFile("footer.html", .{});
    const footer = try footer_file.readToEndAlloc(allocator, 2048);

    const articles = try generate_all_articles_in_dir(header, footer, allocator);
    std.mem.sort(Article, articles, {}, articlesOrderedByCreationDateAsc);
    try generate_home_page(header, articles, allocator);
    try generate_page_articles_by_tag(articles, header, footer, allocator);
    try generate_rss_feed(articles);
}
