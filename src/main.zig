const std = @import("std");

pub const std_options = .{
    .log_level = .info,
};

const html_prelude = "<!DOCTYPE html>\n<html>\n<head>\n<title>{s}</title>\n";

fn do_generate_article(markdown_file_path: []const u8, header: []const u8, footer: []const u8, wait_group: *std.Thread.WaitGroup, allocator: std.mem.Allocator) void {
    defer wait_group.finish();

    generate_article(markdown_file_path, header, footer, allocator) catch |err| {
        std.log.err("failed to generate article: {s} {}", .{ markdown_file_path, err });
    };
}

const Dates = struct {
    creation_date: [10]u8,
    modification_date: [10]u8,
};

fn get_creation_and_modification_date_for_article(markdown_file_path: []const u8, allocator: std.mem.Allocator) !Dates {
    const git_cmd = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = &[_][]const u8{ "git", "log", "--format='%as'", "--", markdown_file_path },
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

    std.log.info("{s} {s} {s}", .{ markdown_file_path, creation_date, modification_date });
    return .{
        .creation_date = creation_date[0..10].*,
        .modification_date = modification_date[0..10].*,
    };
}

fn generate_article(markdown_file_path: []const u8, header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !void {
    std.debug.assert(std.mem.eql(u8, std.fs.path.extension(markdown_file_path), ".md"));
    std.debug.assert(header.len > 0);
    std.debug.assert(footer.len > 0);

    const is_index_page = std.mem.eql(u8, markdown_file_path, "index.md");

    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    const markdown_content = try markdown_file.readToEndAlloc(allocator, 1 * 1024 * 1024);
    defer allocator.free(markdown_content);

    const stem = std.fs.path.stem(markdown_file_path);
    std.debug.assert(stem.len != 0);

    const html_file_path = try std.mem.concat(allocator, u8, &[2][]const u8{ stem, ".html" });
    defer allocator.free(html_file_path);

    const html_file = try std.fs.cwd().createFile(html_file_path, .{});

    {
        var title: []const u8 = "Philippe Gaultier's blog";

        if (!is_index_page) {
            const first_newline_pos = std.mem.indexOf(u8, markdown_content, "\n") orelse 0;
            std.debug.assert(first_newline_pos > 0);
            title = std.mem.trim(u8, markdown_content[0..first_newline_pos], &[_]u8{ '#', ' ' });
        }

        try std.fmt.format(html_file.writer(), html_prelude, .{title});
    }

    try html_file.writeAll(header);

    if (!is_index_page) {
        const dates = try get_creation_and_modification_date_for_article(markdown_file_path, allocator);
        try std.fmt.format(html_file.writer(), "<p id=\"publication_date\">Published on {s}.</p>\n", .{std.mem.trim(u8, &dates.creation_date, &[_]u8{ ' ', '\n', '\'' })});
    }

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

    try html_file.writeAll(footer);

    std.log.info("generated {s}", .{html_file_path});
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

        const is_begin_title = std.mem.startsWith(u8, line.items, "#");
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
        const level = first_space_pos;
        const title = std.mem.trim(u8, line.items[first_space_pos..], &[_]u8{ ' ', '\n' });

        try toc.appendNTimes(' ', level * 2);
        try toc.appendSlice(" - [");
        try toc.appendSlice(title);
        try toc.appendSlice("](#");

        for (title) |c| {
            if (c == ' ') {
                try toc.append('-');
            } else if (c == '-') {
                try toc.append(c);
            } else if (std.ascii.isAlphanumeric(c)) {
                try toc.append(std.ascii.toLower(c));
            }
        }
        try toc.appendSlice(")\n");
    } else |err| switch (err) {
        error.EndOfStream => {},
        else => return err, // Propagate error
    }

    return toc.toOwnedSlice();
}

fn generate_all_articles_in_dir(header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !void {
    const cwd = try std.fs.cwd().openDir(".", .{ .iterate = true });
    var cwd_iterator = cwd.iterate();

    var pool: std.Thread.Pool = undefined;
    try std.Thread.Pool.init(&pool, .{ .allocator = allocator });
    var wait_group = std.Thread.WaitGroup{};

    while (cwd_iterator.next()) |entry_opt| {
        if (entry_opt) |entry| {
            // Skip non markdown files.
            if (entry.kind != .file) continue;
            if (!std.mem.eql(u8, std.fs.path.extension(entry.name), ".md")) continue;

            if (std.mem.eql(u8, entry.name, "README.md")) continue;

            wait_group.start();
            pool.spawnWg(&wait_group, do_generate_article, .{
                try allocator.dupe(u8, entry.name),
                header,
                footer,
                &wait_group,
                allocator,
            });
        } else break; // End of directory.
    } else |err| {
        std.log.err("failed to iterate over directory entries: {}", .{err});
    }

    wait_group.wait();
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    const allocator = gpa.allocator();

    var args_iterator = try std.process.argsWithAllocator(allocator);
    defer args_iterator.deinit();
    _ = args_iterator.skip();

    const cmd = args_iterator.next() orelse {
        std.log.err("missing first argument", .{});
        std.process.exit(1);
    };

    const header_file = try std.fs.cwd().openFile("header.html", .{});
    const header = try header_file.readToEndAlloc(allocator, 2048);

    const footer_file = try std.fs.cwd().openFile("footer.html", .{});
    const footer = try footer_file.readToEndAlloc(allocator, 2048);

    if (std.mem.eql(u8, cmd, "gen_all")) {
        try generate_all_articles_in_dir(header, footer, allocator);
    } else if (std.mem.eql(u8, cmd, "gen")) {
        const file = args_iterator.next() orelse {
            std.log.err("missing second argument", .{});
            std.process.exit(1);
        };
        try generate_article(file, header, footer, allocator);
    } else if (std.mem.eql(u8, cmd, "toc")) {
        const file = args_iterator.next() orelse {
            std.log.err("missing second argument", .{});
            std.process.exit(1);
        };

        const toc = try generate_toc_for_article(file, allocator);
        defer allocator.free(toc);

        try std.io.getStdOut().writeAll(toc);
    }
}
