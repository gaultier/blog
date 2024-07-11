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

fn extract_tags_for_article(markdown_content: []const u8, allocator: std.mem.Allocator) ![][]const u8 {
    var tags = std.ArrayList([]const u8).init(allocator);
    const prefix = "🏷️";
    const tags_begin = (std.mem.indexOf(u8, markdown_content, prefix) orelse return tags.toOwnedSlice()) + prefix.len;
    const tags_end = std.mem.indexOfScalar(u8, markdown_content[tags_begin..], '\n') orelse markdown_content.len;
    const tags_str = markdown_content[tags_begin..][0..tags_end];

    var i: usize = 0;
    while (i < tags_end) {
        switch (tags_str[i]) {
            '[' => {
                i += 1;
                const tag_end = std.mem.indexOfScalar(u8, tags_str[i..], ']') orelse @panic("missing closing ]");
                const tag_str = tags_str[i..][0..tag_end];

                try tags.append(try allocator.dupe(u8, tag_str));

                i += std.mem.indexOfScalar(u8, tags_str[i..], '(') orelse @panic("missing opening (");
                const link_end = std.mem.indexOfScalar(u8, tags_str[i..], ')') orelse @panic("missing closing )");
                const link = tags_str[i + 1 ..][0..link_end];
                std.debug.assert(std.mem.indexOfScalar(u8, link, ' ') == null);

                i += link_end + 1;
            },
            ' ' => i += 1,
            ',' => i += 1,
            '*' => break,
            else => @panic("unexpected character found"),
        }
    }

    return tags.toOwnedSlice();
}

fn convert_markdown_to_html(markdown: []const u8, title: []const u8, header: []const u8, footer: []const u8, html_file_path: []const u8, allocator: std.mem.Allocator) !void {
    const html_file = try std.fs.cwd().createFile(html_file_path, .{});
    defer html_file.close();

    const argv = &[_][]const u8{ "cmark", "--unsafe", "-t", "html" };

    var child = std.process.Child.init(argv, allocator);

    child.stdin_behavior = .Pipe;
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Pipe;

    try std.process.Child.spawn(&child);
    var stdin = child.stdin orelse @panic("no stdin");
    try stdin.writeAll(markdown);

    var stdout = std.ArrayList(u8).init(allocator);
    defer stdout.deinit();
    var stderr = std.ArrayList(u8).init(allocator);
    defer stderr.deinit();

    try child.collectOutput(&stdout, &stderr, 10 * 1024 * 1024);

    _ = try child.wait();

    // std.debug.assert(converter_cmd.stderr.len == 0);

    try html_file.writeAll(header);
    try std.fmt.format(html_file.writer(), html_prelude, .{title});
    try html_file.writeAll(stdout.items);
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
            metadata.title = try allocator.dupe(u8, title_str);
        } else if (std.mem.startsWith(u8, line, tags_prefix)) {
            const tags_str = std.mem.trim(u8, line[tags_prefix.len..], "\n ");

            var it_commas = std.mem.split(u8, tags_str, ", ");
            while (it_commas.next()) |tag_str| {
                const trimmed = std.mem.trim(u8, tag_str, "\n ");
                try tags.append(try allocator.dupe(u8, trimmed));
            }

            metadata.tags = try tags.toOwnedSlice();
        } else break;
    }

    return metadata;
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
        .metadata = parse_metadata(original_markdown_content, allocator),
        .allocator = allocator,
    };

    var markdown_content = std.ArrayList(u8).init(allocator);
    defer markdown_content.deinit();
    try markdown_content.appendSlice(original_markdown_content[article.metadata.end_offset..]);

    article.dates = try get_creation_and_modification_date_for_article(markdown_file_path, allocator);

    try convert_markdown_to_html(markdown_content.items, article.metadata.title, header, footer, article.output_file_name, allocator);

    std.log.info("generated {s} {s}", .{ article.output_file_name, article.tags });

    return article;
}

fn generate_page_articles_by_tag(articles: []Article, header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !void {
    const tags_file = try std.fs.cwd().createFile("articles-per-tag.html", .{});
    defer tags_file.close();

    var buffered_writer = std.io.bufferedWriter(tags_file.writer());
    try buffered_writer.writer().writeAll(header);
    try buffered_writer.writer().writeAll(back_link);
    try buffered_writer.writer().writeAll("<h1>Articles per tag</h1>\n");

    var articles_per_tag = std.StringArrayHashMap(std.ArrayList(*const Article)).init(allocator);
    defer articles_per_tag.deinit();
    defer for (articles_per_tag.values()) |v| {
        v.deinit();
    };

    for (articles, 0..) |article, i| {
        for (article.metada.tags) |tag| {
            std.debug.assert(tag.len > 0);

            var entry = try articles_per_tag.getOrPut(tag);
            if (!entry.found_existing) {
                entry.value_ptr.* = std.ArrayList(*const Article).init(allocator);
            }
            try entry.value_ptr.append(&articles[i]);
        }
    }

    try buffered_writer.writer().writeAll("<ul>\n");
    const keys = try allocator.dupe([]const u8, articles_per_tag.keys());
    defer allocator.free(keys);
    std.mem.sort([]const u8, keys, {}, stringLess);

    for (keys) |tag| {
        const articles_for_tag = articles_per_tag.get(tag) orelse undefined;
        std.mem.sort(*const Article, articles_for_tag.items, {}, articlesPtrOrderedByCreationDateAsc);

        const tag_id = try allocator.dupe(u8, tag);
        defer allocator.free(tag_id);

        std.mem.replaceScalar(u8, tag_id, ' ', '-');
        try std.fmt.format(buffered_writer.writer(), "<li id=\"{s}\">{s}<ul>\n", .{ tag_id, tag });

        for (articles_for_tag.items) |article_for_tag| {
            try std.fmt.format(buffered_writer.writer(), "<li><span class=\"date\">{s}</span> <a href={s}>{s}</a></li>\n", .{ get_date(article_for_tag.dates.creation_date), article_for_tag.output_file_name, article_for_tag.title });
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

fn generate_toc_for_article(markdown_file_path: []const u8, allocator: std.mem.Allocator) ![]u8 {
    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    var buf_reader = std.io.bufferedReader(markdown_file.reader());
    const reader = buf_reader.reader();

    var line = std.ArrayList(u8).init(allocator);
    defer line.deinit();

    const writer = line.writer();

    var toc = std.ArrayList(u8).init(allocator);
    try toc.ensureTotalCapacity(4096);

    var inside_code_section = false;
    while (reader.streamUntilDelimiter(writer, '\n', null)) {
        // Clear the line so we can reuse it.
        defer line.clearRetainingCapacity();

        const is_begin_title_markdown = std.mem.startsWith(u8, line.items, "#");
        const is_begin_title_html = std.mem.startsWith(u8, line.items, "<h");
        const is_begin_title = is_begin_title_markdown or is_begin_title_html;
        const is_begin_code_section = std.mem.startsWith(u8, line.items, "```");

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

        const first_space_pos = std.mem.indexOf(u8, line.items, " ") orelse 0;
        const level = if (is_begin_title_markdown) first_space_pos else (line.items[2] - '0');
        std.debug.assert(1 <= level and level <= 6);
        const title = if (is_begin_title_markdown) std.mem.trim(u8, line.items[first_space_pos..], &[_]u8{ ' ', '\n' }) else blk: {
            const start = 1 + (std.mem.indexOfScalar(u8, line.items, '>') orelse @panic("html title end tag not found"));
            const end = std.mem.indexOfScalar(u8, line.items[start..], '<') orelse @panic("html title start tag not found");
            break :blk line.items[start..][0..end];
        };

        try toc.appendNTimes(' ', level * 2);
        try toc.appendSlice(" - [");
        try toc.appendSlice(title);
        try toc.appendSlice("](#");

        var id = std.ArrayList(u8).init(allocator);
        try id.ensureTotalCapacity(title.len);
        defer id.deinit();

        for (title) |c| {
            if (std.ascii.isAlphanumeric(c)) {
                try id.append(std.ascii.toLower(c));
            } else if (id.items.len > 0 and id.items[id.items.len - 1] != '-') {
                try id.append('-');
            }
        }
        const final_id = std.mem.trim(u8, id.items, "-");
        try toc.appendSlice(final_id);

        try toc.appendSlice(")\n");

        // Sanity check.
        if (is_begin_title_html) {
            const needle = "id=\"";
            const id_start = std.mem.indexOf(u8, line.items, needle);
            if (id_start) |start| {
                const id_end = std.mem.indexOfScalar(u8, line.items[start + needle.len ..], '"') orelse @panic("unterminated id");
                const id_found = line.items[start + needle.len ..][0..id_end];

                if (!std.mem.eql(u8, id_found, final_id)) {
                    std.log.warn("mismatched title ids: found:{s} expected:{s}", .{ id_found, final_id });
                    std.process.exit(1);
                }
            }
        }
    } else |err| switch (err) {
        error.EndOfStream => {},
        else => return err, // Propagate error
    }

    return toc.toOwnedSlice();
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
        , .{ get_date(article.dates.creation_date), article.output_file_name, article.title });
    }

    try html_file.writeAll(
        \\ </ul>
        \\ </div>
        \\
    );

    {
        const converter_cmd = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = &[_][]const u8{ "cmark", "--unsafe", "-t", "html", markdown_file_path },
            .max_output_bytes = 1 * 1024 * 1024,
        });
        defer allocator.free(converter_cmd.stdout);
        defer allocator.free(converter_cmd.stderr);

        std.debug.assert(converter_cmd.stderr.len == 0);

        try html_file.writeAll(converter_cmd.stdout);
    }

    std.log.info("generated {s}", .{output_file_name});
}

fn generate_all_articles_in_dir(header: []const u8, footer: []const u8, allocator: std.mem.Allocator) ![]Article {
    const cwd = try std.fs.cwd().openDir(".", .{ .iterate = true });
    var cwd_iterator = cwd.iterate();

    var pool: std.Thread.Pool = undefined;
    try std.Thread.Pool.init(&pool, .{ .allocator = allocator, .n_jobs = 1 });
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

    const cmd = args_iterator.next() orelse "";

    const header_file = try std.fs.cwd().openFile("header.html", .{});
    const header = try header_file.readToEndAlloc(allocator, 2048);

    const footer_file = try std.fs.cwd().openFile("footer.html", .{});
    const footer = try footer_file.readToEndAlloc(allocator, 2048);

    if (std.mem.eql(u8, cmd, "toc")) {
        const file = args_iterator.next() orelse {
            std.log.err("missing second argument", .{});
            std.process.exit(1);
        };

        const toc = try generate_toc_for_article(file, allocator);
        defer allocator.free(toc);

        try std.io.getStdOut().writeAll(toc);
    } else {
        const articles = try generate_all_articles_in_dir(header, footer, allocator);
        std.mem.sort(Article, articles, {}, articlesOrderedByCreationDateAsc);
        try generate_home_page(header, articles, allocator);
        try generate_page_articles_by_tag(articles, header, footer, allocator);
        try generate_rss_feed(articles);
    }
}
