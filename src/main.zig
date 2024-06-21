const std = @import("std");

pub const std_options = .{
    .log_level = .info,
};

const html_envelope = "<!DOCTYPE html>\n<html>\n<head>\n<title>{}</title>\n";

fn generate_article(markdown_file_path: []const u8, header: []const u8, allocator: std.mem.Allocator) !void {
    const markdown_file = try std.fs.cwd().openFile(markdown_file_path, .{});
    defer markdown_file.close();

    const markdown_content = try markdown_file.readToEndAlloc(allocator, 2048);
    _ = markdown_content;

    const stem = std.fs.path.stem(markdown_file_path);
    std.debug.assert(stem.len != 0);

    const html_file_path = try std.fs.path.join(allocator, &[2][]const u8{ stem, ".html" });
    const html_file = try std.fs.cwd().createFile(html_file_path, .{});

    try html_file.writeAll(header);
}

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const allocator = arena.allocator();

    const header_file = try std.fs.cwd().openFile("header.html", .{});
    const header = try header_file.readToEndAlloc(allocator, 2048);

    var cwd_iterator = std.fs.cwd().iterate();

    while (cwd_iterator.next()) |entry_opt| {
        if (entry_opt) |entry| {
            // Skip non markdown files.
            if (entry.kind != .file) continue;
            if (!std.mem.eql(u8, std.fs.path.extension(entry.name), ".md")) continue;

            try generate_article(entry.name, header, allocator);
        } else break; // End of directory.
    } else |err| {
        std.log.err("failed to iterate over directory entries: {}", .{err});
    }
}
