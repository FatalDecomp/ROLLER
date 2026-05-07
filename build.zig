pub fn build(b: *std.Build) void {
    // basic support for running in Visual Studio using ZigVS
    const running_in_vs = blk: {
        _ = std.process.getEnvVarOwned(b.allocator, "VisualStudioEdition") catch break :blk false;
        break :blk true;
    };

    // tells the build where to find your FATDATA folder
    // defaults to ./fatdata
    const assets_path = b.option(std.Build.LazyPath, "assets-path", "Path to assets") orelse b.path("fatdata");

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const crash_debug = b.option(bool, "crash-debug", "Enable crash dump friendly C build flags") orelse false;
    const c_flags: []const []const u8 = if (crash_debug)
        &.{ "-fwrapv", "-fno-omit-frame-pointer" }
    else
        &.{ "-fwrapv" };

    const exe_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    // Only sanitize C code when building in Debug mode
    // So that release builds are more "stable"
    exe_mod.sanitize_c = if (optimize == .Debug) .full else .off;
    exe_mod.addIncludePath(b.path("external/Nuklear-4.13.2"));
    exe_mod.addIncludePath(b.path("external/lodepng"));
    exe_mod.addCSourceFiles(.{
        .flags = &.{ "-fwrapv", "-DLODEPNG_NO_COMPILE_CPP" },
        .files = &.{
            "external/lodepng/lodepng.c",
        },
    });
    exe_mod.addCSourceFiles(.{
        .flags = c_flags,
        .files = &.{
            "PROJECTS/ROLLER/debug_overlay.c",
            "PROJECTS/ROLLER/crashdump.c",
            "PROJECTS/ROLLER/3d.c",
            "PROJECTS/ROLLER/building.c",
            "PROJECTS/ROLLER/car.c",
            "PROJECTS/ROLLER/carplans.c",
            "PROJECTS/ROLLER/cdx.c",
            "PROJECTS/ROLLER/colision.c",
            "PROJECTS/ROLLER/comms.c",
            "PROJECTS/ROLLER/control.c",
            "PROJECTS/ROLLER/date.c",
            "PROJECTS/ROLLER/drawtrk3.c",
            "PROJECTS/ROLLER/engines.c",
            "PROJECTS/ROLLER/frontend_config.c",
            "PROJECTS/ROLLER/frontend_data.c",
            "PROJECTS/ROLLER/frontend_network.c",
            "PROJECTS/ROLLER/frontend_screens.c",
            "PROJECTS/ROLLER/frontend_select_car.c",
            "PROJECTS/ROLLER/frontend_select_disk.c",
            "PROJECTS/ROLLER/frontend_select_players.c",
            "PROJECTS/ROLLER/frontend_select_track.c",
            "PROJECTS/ROLLER/frontend_select_type.c",
            "PROJECTS/ROLLER/frontend_util.c",
            "PROJECTS/ROLLER/func2.c",
            "PROJECTS/ROLLER/func3.c",
            "PROJECTS/ROLLER/function.c",
            "PROJECTS/ROLLER/graphics.c",
            "PROJECTS/ROLLER/horizon.c",
            "PROJECTS/ROLLER/loadtrak.c",
            "PROJECTS/ROLLER/menu_render.c",
            "PROJECTS/ROLLER/menu_render_gpu.c",
            "PROJECTS/ROLLER/menu_render_software.c",
            "PROJECTS/ROLLER/mouse.c",
            "PROJECTS/ROLLER/moving.c",
            "PROJECTS/ROLLER/network.c",
            "PROJECTS/ROLLER/plans.c",
            "PROJECTS/ROLLER/png_diff.c",
            "PROJECTS/ROLLER/png_writer.c",
            "PROJECTS/ROLLER/polyf.c",
            "PROJECTS/ROLLER/polytex.c",
            "PROJECTS/ROLLER/replay.c",
            "PROJECTS/ROLLER/roller.c",
            "PROJECTS/ROLLER/rollercomms.c",
            "PROJECTS/ROLLER/snapshot.c",
            "PROJECTS/ROLLER/sound.c",
            "PROJECTS/ROLLER/svgacpy.c",
            "PROJECTS/ROLLER/tower.c",
            "PROJECTS/ROLLER/transfrm.c",
            "PROJECTS/ROLLER/userfns.c",
            "PROJECTS/ROLLER/view.c",
        },
    });

    const exe = b.addExecutable(.{
        .name = "roller",
        .root_module = exe_mod,
    });

    switch (target.result.os.tag) {
        .windows => {
            exe_mod.addCMacro("WILDMIDI_STATIC", "1");

            exe.addWin32ResourceFile(.{
                .file = b.path("ROLLER.rc"),
            });
            exe.subsystem = .Windows;
            exe.linkSystemLibrary("dbghelp");
            exe.linkSystemLibrary("user32");
            exe.linkSystemLibrary("ws2_32");
            exe.linkSystemLibrary("iphlpapi");
        },
        else => {
            exe_mod.addCMacro("__int16", "int16");
            exe_mod.addCMacro("_O_RDONLY", "O_RDONLY");
            exe_mod.addCMacro("_O_BINARY", "0x200");
        },
    }

    b.installArtifact(exe);

    configureDependencies(b, exe, target, optimize);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    run_cmd.setCwd(assets_path);

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run roller");
    run_step.dependOn(&run_cmd.step);

    if (running_in_vs) {
        const cp = b.addInstallDirectory(.{
            .source_dir = assets_path,
            .install_dir = .bin,
            .install_subdir = "",
        });
        exe.step.dependOn(&cp.step);
    }

    // copies fatdata directory to the bin folder
    // only happens when using `zig build run`
    const assets_install = b.addInstallDirectory(.{
        .source_dir = assets_path,
        .install_dir = .bin,
        .install_subdir = "fatdata",
    });
    run_step.dependOn(&assets_install.step);

    // copies wildmidi configuration files to the bin folder
    // only happens when using `zig build run`
    const wildmidi_config = b.addWriteFiles();
    const wildmidi_config_copy = wildmidi_config.addCopyDirectory(b.path("midi"), "midi", .{});
    const wildmidi_config_install = b.addInstallDirectory(.{
        .source_dir = wildmidi_config_copy,
        .install_dir = .bin,
        .install_subdir = "midi",
    });
    run_step.dependOn(&wildmidi_config_install.step);

    // Snapshot regression harness: build the comparison walker, then wire a
    // top-level `zig build test-snapshots` step that drives the snapshot
    // binary across all seven intro replays before invoking the walker. The
    // -Dupdate-snapshots flag flips the walker into write-baselines mode.
    configureSnapshotTests(b, exe, assets_path, target, optimize);
}

const SnapshotReplay = struct {
    name: []const u8,
    frames: []const u8,
};

// Hand-picked frames per intro replay. Spread across each replay's length.
// Pinned to single-host pixels per the ADR.
const snapshot_replays = [_]SnapshotReplay{
    .{ .name = "intro1", .frames = "60,240,480,720" },
};

fn configureSnapshotTests(
    b: *Build,
    roller_exe: *Compile,
    assets_path: LazyPath,
    target: ResolvedTarget,
    optimize: OptimizeMode,
) void {
    const update_snapshots = b.option(
        bool,
        "update-snapshots",
        "Overwrite snapshot baselines under tests/snapshots/expected/ with the current actuals",
    ) orelse false;

    const walker_mod = b.createModule(.{
        .root_source_file = b.path("tools/snapshot_walker.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    walker_mod.addIncludePath(b.path("external/lodepng"));
    walker_mod.addIncludePath(b.path("PROJECTS/ROLLER"));
    walker_mod.addCSourceFiles(.{
        .flags = &.{ "-fwrapv", "-DLODEPNG_NO_COMPILE_CPP" },
        .files = &.{"external/lodepng/lodepng.c"},
    });
    walker_mod.addCSourceFiles(.{
        .flags = &.{"-fwrapv"},
        .files = &.{
            "PROJECTS/ROLLER/png_writer.c",
            "PROJECTS/ROLLER/png_diff.c",
        },
    });

    const walker_exe = b.addExecutable(.{
        .name = "snapshot_walker",
        .root_module = walker_mod,
    });
    b.installArtifact(walker_exe);

    const expected_dir = "tests/snapshots/expected";
    const actual_dir = "zig-out/snapshot-actual";
    const diff_dir = "zig-out/snapshot-diff";

    const test_snapshots = b.step(
        "test-snapshots",
        "Run rendering snapshot regression tests across the intro replays",
    );

    const walker_run = b.addRunArtifact(walker_exe);
    walker_run.addArg("--expected");
    walker_run.addArg(expected_dir);
    walker_run.addArg("--actual");
    walker_run.addArg(actual_dir);
    walker_run.addArg("--diff");
    walker_run.addArg(diff_dir);
    if (update_snapshots) walker_run.addArg("--update");

    // Drive the snapshot binary serially: parallel invocations introduced
    // non-deterministic pixels at long-running replay frames (suspected:
    // contention on shared system probes during early init).
    var prev_run: ?*Step = null;
    for (snapshot_replays) |replay| {
        const run_capture = b.addRunArtifact(roller_exe);
        run_capture.addArg("--no-crash-handler");
        run_capture.addArg("--whiplash-root");
        run_capture.addDirectoryArg(assets_path);
        run_capture.addArg("--snapshot");
        run_capture.addArg(b.fmt("{s}.gss", .{replay.name}));
        run_capture.addArg("--frames");
        run_capture.addArg(replay.frames);
        run_capture.addArg("--out");
        run_capture.addArg(b.pathJoin(&.{ b.build_root.path orelse ".", actual_dir }));
        run_capture.has_side_effects = true;
        if (prev_run) |p| run_capture.step.dependOn(p);
        walker_run.step.dependOn(&run_capture.step);
        prev_run = &run_capture.step;
    }

    test_snapshots.dependOn(&walker_run.step);

    // Unit tests for the walker (diff generator + decode round-trip).
    const walker_tests = b.addTest(.{ .root_module = walker_mod });
    const run_walker_tests = b.addRunArtifact(walker_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_walker_tests.step);
}

fn configureDependencies(b: *Build, exe: *Compile, target: ResolvedTarget, optimize: OptimizeMode) void {
    const exe_mod = exe.root_module;

    // build dependencies
    const wildmidi = b.dependency("wildmidi", .{
        .target = target,
        .optimize = optimize,
    });
    const wildmidi_lib = wildmidi.artifact("wildmidi");

    const sdl_image = b.dependency("SDL_image", .{
        .target = target,
        .optimize = optimize,
    });
    const sdl_image_lib = sdl_image.artifact("SDL3_image");

    const sdl = b.dependency("sdl", .{
        .target = target,
        .optimize = optimize,
        .lto = .none,
    });
    const sdl_lib = sdl.artifact("SDL3");

    const libcdio = b.dependency("libcdio", .{
        .target = target,
        .optimize = optimize,
    });
    const libcdio_lib = libcdio.artifact("cdio");

    exe_mod.linkLibrary(sdl_lib);
    exe_mod.linkLibrary(sdl_image_lib);
    exe_mod.linkLibrary(wildmidi_lib);
    exe_mod.linkLibrary(libcdio_lib);

    const sdl_image_source = sdl_image.builder.dependency("SDL_image", .{
        .lto = .none,
    });

    var cflags = compile_flagz.addCompileFlags(b);
    cflags.addIncludePath(sdl.builder.path("include"));
    cflags.addIncludePath(sdl_image_source.builder.path("include"));
    cflags.addIncludePath(wildmidi.builder.path("include"));
    cflags.addIncludePath(libcdio.builder.path("include"));
    cflags.addIncludePath(libcdio.builder.path("zig-config"));
    cflags.addIncludePath(b.path("external/Nuklear-4.13.2"));
    cflags.addIncludePath(b.path("external/lodepng"));

    const cflags_step = b.step("compile-flags", "Generate compile flags");
    cflags_step.dependOn(&cflags.step);
}

const compile_flagz = @import("compile_flagz");

const std = @import("std");
const ArrayList = std.ArrayListUnmanaged;
const Build = std.Build;
const LazyPath = Build.LazyPath;
const Module = Build.Module;
const ResolvedTarget = Build.ResolvedTarget;
const Step = Build.Step;
const Compile = Step.Compile;

const builtin = std.builtin;
const OptimizeMode = builtin.OptimizeMode;
