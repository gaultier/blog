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
        const git_output = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = &[_][]const u8{ "git", "log", "--format='%as'", "--reverse", "--", markdown_file_path },
        });
        defer allocator.free(git_output.stdout);
        defer allocator.free(git_output.stderr);

        std.debug.assert(git_output.stderr.len == 0);

        const first_newline_pos = std.mem.indexOf(u8, git_output.stdout, "\n") orelse 0;
        std.debug.assert(first_newline_pos > 0);

        const markdown_file_creation_date = git_output.stdout[0..first_newline_pos];
        try std.fmt.format(html_file.writer(), "<p id=\"publication_date\">Published on {s}.</p>\n", .{std.mem.trim(u8, markdown_file_creation_date, &[_]u8{ ' ', '\n', '\'' })});
    }

    {
        const pandoc_output = try std.process.Child.run(.{
            .allocator = allocator,
            .argv = &[_][]const u8{ "pandoc", "--toc", markdown_file_path },
            .max_output_bytes = 1 * 1024 * 1024,
        });
        defer allocator.free(pandoc_output.stdout);
        defer allocator.free(pandoc_output.stderr);

        std.debug.assert(pandoc_output.stderr.len == 0);

        try html_file.writeAll(pandoc_output.stdout);
    }

    try html_file.writeAll(footer);

    std.log.info("generated {s}", .{html_file_path});
}

fn generate_toc_for_article(markdown_file_path: []const u8, allocator: std.mem.Allocator) ![]u8 {
    const pandoc_output = try std.process.Child.run(.{
        .allocator = allocator,
        .argv = &[_][]const u8{ "pandoc", "-s", "--toc", markdown_file_path, "-f", "markdown", "-t", "markdown" },
        .max_output_bytes = 1 * 1024 * 1024,
    });
    defer allocator.free(pandoc_output.stdout);
    defer allocator.free(pandoc_output.stderr);

    std.debug.assert(pandoc_output.stderr.len == 0);

    const first_pound_pos = std.mem.indexOf(u8, pandoc_output.stdout, "\n#") orelse 0;
    std.debug.assert(first_pound_pos > 0);

    const toc = pandoc_output.stdout[0..first_pound_pos];
    const toc_trimmed = std.mem.trim(u8, toc, &[_]u8{ '\n', ' ' });

    return allocator.dupe(u8, toc_trimmed);
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
