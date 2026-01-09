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

    const exe_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    // Only sanitize C code when building in Debug mode
    // So that release builds are more "stable"
    exe_mod.sanitize_c = if (optimize == .Debug) .full else .off;
    exe_mod.addCSourceFiles(.{
        .files = &.{
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
            "PROJECTS/ROLLER/frontend.c",
            "PROJECTS/ROLLER/func2.c",
            "PROJECTS/ROLLER/func3.c",
            "PROJECTS/ROLLER/function.c",
            "PROJECTS/ROLLER/graphics.c",
            "PROJECTS/ROLLER/horizon.c",
            "PROJECTS/ROLLER/loadtrak.c",
            "PROJECTS/ROLLER/mouse.c",
            "PROJECTS/ROLLER/moving.c",
            "PROJECTS/ROLLER/network.c",
            "PROJECTS/ROLLER/plans.c",
            "PROJECTS/ROLLER/polyf.c",
            "PROJECTS/ROLLER/polytex.c",
            "PROJECTS/ROLLER/replay.c",
            "PROJECTS/ROLLER/roller.c",
            "PROJECTS/ROLLER/rollercomms.c",
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
            exe.linkSystemLibrary("ws2_32");
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

    exe_mod.linkLibrary(sdl_lib);
    exe_mod.linkLibrary(sdl_image_lib);
    exe_mod.linkLibrary(wildmidi_lib);

    const sdl_image_source = sdl_image.builder.dependency("SDL_image", .{
        .lto = .none,
    });

    var cflags = compile_flagz.addCompileFlags(b);
    cflags.addIncludePath(sdl.builder.path("include"));
    cflags.addIncludePath(sdl_image_source.builder.path("include"));
    cflags.addIncludePath(wildmidi.builder.path("include"));

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
