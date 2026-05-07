// Snapshot comparison harness for ROLLER's rendering snapshot tests.
//
// Walks the directory of expected baseline PNGs, decodes each baseline and
// the matching actual PNG, byte-compares the indexed pixel buffers, writes a
// diff PNG for any mismatch, and exits non-zero if any pair differs. With
// --update the actuals are copied over the baselines instead.
//
// Compares decoded indexed pixel data only (i.e. the pixel data after IDAT
// decompression). Palette differences are ignored as long as the index
// buffers match.

const std = @import("std");

const c = @cImport({
    @cInclude("lodepng.h");
    @cInclude("png_writer.h");
    @cInclude("png_diff.h");
});

const Image = struct {
    pixels: []u8,
    palette: [256 * 3]u8, // 8-bit RGB (matches the writer's PLTE chunk)
    width: u32,
    height: u32,

    fn deinit(self: *Image, allocator: std.mem.Allocator) void {
        allocator.free(self.pixels);
        self.* = undefined;
    }
};

fn decodeIndexedPng(allocator: std.mem.Allocator, path: []const u8) !Image {
    const file_data = try std.fs.cwd().readFileAlloc(allocator, path, 64 * 1024 * 1024);
    defer allocator.free(file_data);

    var state: c.LodePNGState = undefined;
    c.lodepng_state_init(&state);
    defer c.lodepng_state_cleanup(&state);

    state.info_raw.colortype = c.LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.decoder.color_convert = 0;

    var pixels_ptr: [*c]u8 = null;
    var w: c_uint = 0;
    var h: c_uint = 0;

    const err = c.lodepng_decode(&pixels_ptr, &w, &h, &state, file_data.ptr, file_data.len);
    if (err != 0) {
        std.debug.print("snapshot-walker: lodepng_decode failed for '{s}' (err {d})\n", .{ path, err });
        return error.DecodeFailed;
    }
    // lodepng allocates with its own malloc; copy into Zig-managed memory and
    // free the lodepng buffer immediately so we don't leak on later errors.
    const n = @as(usize, w) * @as(usize, h);
    const owned = try allocator.alloc(u8, n);
    @memcpy(owned, pixels_ptr[0..n]);
    std.c.free(pixels_ptr);

    var img = Image{
        .pixels = owned,
        .palette = [_]u8{0} ** (256 * 3),
        .width = @intCast(w),
        .height = @intCast(h),
    };

    // The decoded palette lives in info_png.color.palette as RGBA 8-bit.
    const pal_count = state.info_png.color.palettesize;
    const pal_src = state.info_png.color.palette;
    if (pal_src) |src| {
        var i: usize = 0;
        while (i < pal_count and i < 256) : (i += 1) {
            img.palette[i * 3 + 0] = src[i * 4 + 0];
            img.palette[i * 3 + 1] = src[i * 4 + 1];
            img.palette[i * 3 + 2] = src[i * 4 + 2];
        }
    }

    return img;
}

// Convert an 8-bit RGB palette into the game's 6-bit tColor layout for the
// diff generator (which expects byR/byG/byB with the field-name semantics
// despite the struct's RBG memory ordering).
fn paletteRgb8To6(rgb8: *const [256 * 3]u8) [256]c.tColor {
    var out: [256]c.tColor = undefined;
    var i: usize = 0;
    while (i < 256) : (i += 1) {
        const r: u8 = rgb8[i * 3 + 0];
        const g: u8 = rgb8[i * 3 + 1];
        const b: u8 = rgb8[i * 3 + 2];
        out[i].byR = @intCast(@as(u16, r) * 63 / 255);
        out[i].byG = @intCast(@as(u16, g) * 63 / 255);
        out[i].byB = @intCast(@as(u16, b) * 63 / 255);
    }
    return out;
}

fn writeDiffPng(allocator: std.mem.Allocator, path: []const u8, expected: *const Image, actual: *const Image) !usize {
    if (expected.width != actual.width or expected.height != actual.height) {
        return error.DimensionMismatch;
    }
    const n = @as(usize, expected.width) * @as(usize, expected.height);
    const diff = try allocator.alloc(u8, n);
    defer allocator.free(diff);

    var palette_for_diff = paletteRgb8To6(&expected.palette);
    const differ_count = c.RollerComputePngDiff(
        expected.pixels.ptr,
        actual.pixels.ptr,
        &palette_for_diff[0],
        @intCast(expected.width),
        @intCast(expected.height),
        diff.ptr,
    );

    // Make sure the destination directory exists.
    if (std.fs.path.dirname(path)) |parent| {
        std.fs.cwd().makePath(parent) catch {};
    }

    const path_z = try allocator.dupeZ(u8, path);
    defer allocator.free(path_z);

    const diff_palette = c.RollerGetDiffPalette();
    const rc = c.RollerWriteIndexedPng(
        path_z.ptr,
        diff.ptr,
        diff_palette,
        @intCast(expected.width),
        @intCast(expected.height),
    );
    if (rc != 0) return error.DiffWriteFailed;

    return @intCast(differ_count);
}

fn copyFile(src: []const u8, dst: []const u8) !void {
    if (std.fs.path.dirname(dst)) |parent| {
        std.fs.cwd().makePath(parent) catch {};
    }
    try std.fs.cwd().copyFile(src, std.fs.cwd(), dst, .{});
}

const Args = struct {
    expected_dir: []const u8 = "tests/snapshots/expected",
    actual_dir: []const u8 = "zig-out/snapshot-actual",
    diff_dir: []const u8 = "zig-out/snapshot-diff",
    update: bool = false,
};

fn parseArgs(args: []const [:0]u8) !Args {
    var out = Args{};
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const a = args[i];
        if (std.mem.eql(u8, a, "--expected")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            out.expected_dir = args[i];
        } else if (std.mem.eql(u8, a, "--actual")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            out.actual_dir = args[i];
        } else if (std.mem.eql(u8, a, "--diff")) {
            i += 1;
            if (i >= args.len) return error.MissingArg;
            out.diff_dir = args[i];
        } else if (std.mem.eql(u8, a, "--update")) {
            out.update = true;
        } else {
            std.debug.print("snapshot-walker: unknown arg '{s}'\n", .{a});
            return error.UnknownArg;
        }
    }
    return out;
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const argv = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, argv);

    const args = parseArgs(argv) catch |err| {
        std.debug.print("usage: snapshot-walker [--expected DIR] [--actual DIR] [--diff DIR] [--update]\n", .{});
        return err;
    };

    var dir = std.fs.cwd().openDir(args.expected_dir, .{ .iterate = true }) catch |err| {
        if (err == error.FileNotFound) {
            // No baselines yet means nothing to compare. In update mode we'll
            // also walk the actual dir below; in compare mode this is a clean
            // pass.
            if (!args.update) return;
            try std.fs.cwd().makePath(args.expected_dir);
            return;
        }
        return err;
    };
    defer dir.close();

    var any_mismatch = false;
    var compared: usize = 0;
    var updated: usize = 0;

    var it = dir.iterate();
    while (try it.next()) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.name, ".png")) continue;

        const expected_path = try std.fs.path.join(allocator, &.{ args.expected_dir, entry.name });
        defer allocator.free(expected_path);
        const actual_path = try std.fs.path.join(allocator, &.{ args.actual_dir, entry.name });
        defer allocator.free(actual_path);

        if (args.update) {
            std.fs.cwd().access(actual_path, .{}) catch |err| {
                std.debug.print("snapshot-walker: missing actual for '{s}': {s}\n", .{ entry.name, @errorName(err) });
                continue;
            };
            try copyFile(actual_path, expected_path);
            updated += 1;
            continue;
        }

        std.fs.cwd().access(actual_path, .{}) catch |err| {
            std.debug.print("snapshot-walker: missing actual for baseline '{s}' ({s})\n", .{ entry.name, @errorName(err) });
            any_mismatch = true;
            continue;
        };

        var expected_img = decodeIndexedPng(allocator, expected_path) catch {
            any_mismatch = true;
            continue;
        };
        defer expected_img.deinit(allocator);
        var actual_img = decodeIndexedPng(allocator, actual_path) catch {
            any_mismatch = true;
            continue;
        };
        defer actual_img.deinit(allocator);

        compared += 1;

        const dim_match = expected_img.width == actual_img.width and
            expected_img.height == actual_img.height;
        const px_match = dim_match and std.mem.eql(u8, expected_img.pixels, actual_img.pixels);

        if (!px_match) {
            const diff_path = try std.fs.path.join(allocator, &.{ args.diff_dir, entry.name });
            defer allocator.free(diff_path);
            if (dim_match) {
                const differ_count = writeDiffPng(allocator, diff_path, &expected_img, &actual_img) catch |err| {
                    std.debug.print("snapshot-walker: diff write failed for '{s}': {s}\n", .{ entry.name, @errorName(err) });
                    any_mismatch = true;
                    continue;
                };
                std.debug.print("MISMATCH {s}: {d} pixels differ -> {s}\n", .{ entry.name, differ_count, diff_path });
            } else {
                std.debug.print("MISMATCH {s}: dimensions differ ({d}x{d} vs {d}x{d})\n", .{
                    entry.name,
                    expected_img.width,
                    expected_img.height,
                    actual_img.width,
                    actual_img.height,
                });
            }
            any_mismatch = true;
        }
    }

    if (args.update) {
        // Pick up any actuals that don't exist yet as baselines.
        var actual_dir = std.fs.cwd().openDir(args.actual_dir, .{ .iterate = true }) catch |err| {
            if (err == error.FileNotFound) {
                std.debug.print("snapshot-walker: updated {d} baseline(s)\n", .{updated});
                return;
            }
            return err;
        };
        defer actual_dir.close();

        var ait = actual_dir.iterate();
        while (try ait.next()) |entry| {
            if (entry.kind != .file) continue;
            if (!std.mem.endsWith(u8, entry.name, ".png")) continue;

            const expected_path = try std.fs.path.join(allocator, &.{ args.expected_dir, entry.name });
            defer allocator.free(expected_path);
            const actual_path = try std.fs.path.join(allocator, &.{ args.actual_dir, entry.name });
            defer allocator.free(actual_path);

            std.fs.cwd().access(expected_path, .{}) catch {
                try copyFile(actual_path, expected_path);
                updated += 1;
                continue;
            };
        }

        std.debug.print("snapshot-walker: updated {d} baseline(s)\n", .{updated});
        return;
    }

    if (any_mismatch) {
        std.debug.print("snapshot-walker: {d} baseline(s) compared, mismatches found\n", .{compared});
        std.process.exit(1);
    }
    std.debug.print("snapshot-walker: {d} baseline(s) compared, all match\n", .{compared});
}

// -----------------------------------------------------------------------------
// Tests

test "diff: all-match yields zero differing pixels and no red" {
    const allocator = std.testing.allocator;

    const w: u32 = 4;
    const h: u32 = 3;
    const n = w * h;

    const buf = try allocator.alloc(u8, n);
    defer allocator.free(buf);
    const diff = try allocator.alloc(u8, n);
    defer allocator.free(diff);

    var i: usize = 0;
    while (i < n) : (i += 1) buf[i] = @intCast(i % 64);

    var palette: [256]c.tColor = undefined;
    i = 0;
    while (i < 256) : (i += 1) {
        palette[i].byR = @intCast(i % 64);
        palette[i].byG = @intCast(i % 64);
        palette[i].byB = @intCast(i % 64);
    }

    const differ_count = c.RollerComputePngDiff(buf.ptr, buf.ptr, &palette[0], @intCast(w), @intCast(h), diff.ptr);
    try std.testing.expectEqual(@as(c_int, 0), differ_count);
    for (diff) |px| {
        try std.testing.expect(px != 255);
    }
}

test "diff: all-differ yields N differing pixels and all red" {
    const allocator = std.testing.allocator;

    const w: u32 = 4;
    const h: u32 = 3;
    const n = w * h;

    const a = try allocator.alloc(u8, n);
    defer allocator.free(a);
    const b = try allocator.alloc(u8, n);
    defer allocator.free(b);
    const diff = try allocator.alloc(u8, n);
    defer allocator.free(diff);

    var i: usize = 0;
    while (i < n) : (i += 1) {
        a[i] = 1;
        b[i] = 2;
    }

    var palette: [256]c.tColor = std.mem.zeroes([256]c.tColor);
    const differ_count = c.RollerComputePngDiff(a.ptr, b.ptr, &palette[0], @intCast(w), @intCast(h), diff.ptr);
    try std.testing.expectEqual(@as(c_int, @intCast(n)), differ_count);
    for (diff) |px| {
        try std.testing.expectEqual(@as(u8, 255), px);
    }
}

test "diff: partial-differ marks only the differing pixels red" {
    const allocator = std.testing.allocator;

    const w: u32 = 8;
    const h: u32 = 1;
    const n = w * h;

    const a = try allocator.alloc(u8, n);
    defer allocator.free(a);
    const b = try allocator.alloc(u8, n);
    defer allocator.free(b);
    const diff = try allocator.alloc(u8, n);
    defer allocator.free(diff);

    @memcpy(a, &[_]u8{ 0, 1, 2, 3, 4, 5, 6, 7 });
    @memcpy(b, &[_]u8{ 0, 9, 2, 3, 4, 9, 6, 9 });

    var palette: [256]c.tColor = undefined;
    var i: usize = 0;
    while (i < 256) : (i += 1) {
        palette[i].byR = 31;
        palette[i].byG = 31;
        palette[i].byB = 31;
    }

    const differ_count = c.RollerComputePngDiff(a.ptr, b.ptr, &palette[0], @intCast(w), @intCast(h), diff.ptr);
    try std.testing.expectEqual(@as(c_int, 3), differ_count);
    try std.testing.expect(diff[0] != 255);
    try std.testing.expectEqual(@as(u8, 255), diff[1]);
    try std.testing.expect(diff[2] != 255);
    try std.testing.expect(diff[3] != 255);
    try std.testing.expect(diff[4] != 255);
    try std.testing.expectEqual(@as(u8, 255), diff[5]);
    try std.testing.expect(diff[6] != 255);
    try std.testing.expectEqual(@as(u8, 255), diff[7]);
}

test "diff palette: index 255 is red, others are grayscale" {
    const palette = c.RollerGetDiffPalette();
    var i: usize = 0;
    while (i < 255) : (i += 1) {
        try std.testing.expectEqual(palette[i].byR, palette[i].byG);
        try std.testing.expectEqual(palette[i].byG, palette[i].byB);
    }
    try std.testing.expectEqual(@as(u8, 63), palette[255].byR);
    try std.testing.expectEqual(@as(u8, 0), palette[255].byG);
    try std.testing.expectEqual(@as(u8, 0), palette[255].byB);
}

test "encode/decode round-trip preserves indexed pixels and palette" {
    const allocator = std.testing.allocator;

    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();

    const path_rel = "round.png";
    const tmp_dir_path = try tmp.dir.realpathAlloc(allocator, ".");
    defer allocator.free(tmp_dir_path);
    const path = try std.fs.path.join(allocator, &.{ tmp_dir_path, path_rel });
    defer allocator.free(path);
    const path_z = try allocator.dupeZ(u8, path);
    defer allocator.free(path_z);

    const w: u32 = 16;
    const h: u32 = 8;
    const n = w * h;

    const buf = try allocator.alloc(u8, n);
    defer allocator.free(buf);
    var i: usize = 0;
    while (i < n) : (i += 1) buf[i] = @intCast(i & 0xff);

    var palette: [256]c.tColor = undefined;
    i = 0;
    while (i < 256) : (i += 1) {
        palette[i].byR = @intCast(i % 64);
        // Note: tColor's struct field order is byR, byB, byG by name, but the
        // semantics of byG/byB still hold. We just populate both to known
        // values.
        palette[i].byG = @intCast((i * 2) % 64);
        palette[i].byB = @intCast((i * 3) % 64);
    }

    const rc = c.RollerWriteIndexedPng(path_z.ptr, buf.ptr, &palette[0], @intCast(w), @intCast(h));
    try std.testing.expectEqual(@as(c_int, 0), rc);

    var img = try decodeIndexedPng(allocator, path);
    defer img.deinit(allocator);

    try std.testing.expectEqual(@as(u32, w), img.width);
    try std.testing.expectEqual(@as(u32, h), img.height);
    try std.testing.expectEqualSlices(u8, buf, img.pixels);
}
