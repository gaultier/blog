const std = @import("std");

pub const std_options = .{
    .log_level = .info,
};

const html_prelude = "<!DOCTYPE html>\n<html>\n<head>\n<title>{s}</title>\n";

fn generate_article(markdown_file_path: []const u8, header: []const u8, footer: []const u8, allocator: std.mem.Allocator) !void {
    const is_index_page = std.mem.eql(u8, markdown_file_path, "index.md");

    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    const markdown_content = try markdown_file.readToEndAlloc(allocator, 1 * 1024 * 1024);
    const stem = std.fs.path.stem(markdown_file_path);
    std.debug.assert(stem.len != 0);

    const html_file_path = try std.mem.concat(allocator, u8, &[2][]const u8{ stem, ".html" });
    std.log.info("create html file {s} {s}", .{ markdown_file_path, html_file_path });
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
        std.debug.assert(pandoc_output.stderr.len == 0);

        try html_file.writeAll(pandoc_output.stdout);
    }

    try html_file.writeAll(footer);

    std.log.info("generated {s}", .{html_file_path});
}

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const allocator = arena.allocator();

    const header_file = try std.fs.cwd().openFile("header.html", .{});
    const header = try header_file.readToEndAlloc(allocator, 2048);

    const footer_file = try std.fs.cwd().openFile("footer.html", .{});
    const footer = try footer_file.readToEndAlloc(allocator, 2048);

    const cwd = try std.fs.cwd().openDir(".", .{ .iterate = true });
    var cwd_iterator = cwd.iterate();

    while (cwd_iterator.next()) |entry_opt| {
        if (entry_opt) |entry| {
            // Skip non markdown files.
            if (entry.kind != .file) continue;
            if (!std.mem.eql(u8, std.fs.path.extension(entry.name), ".md")) continue;

            if (std.mem.eql(u8, entry.name, "README.md")) continue; // Skip.

            try generate_article(entry.name, header, footer, allocator);
        } else break; // End of directory.
    } else |err| {
        std.log.err("failed to iterate over directory entries: {}", .{err});
    }
}
