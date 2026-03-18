const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const upstream = b.dependency("SDL_shadercross", .{});
    const spirv_cross_dep = b.dependency("SPIRV-Cross", .{});
    const spirv_headers = b.dependency("SPIRV-Headers", .{});
    const sdl = b.dependency("sdl", .{
        .target = target,
        .optimize = optimize,
    });

    // Build SPIRV-Cross as a static C++ library
    const spv_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });

    spv_mod.addCMacro("SPIRV_CROSS_C_API_GLSL", "1");
    spv_mod.addCMacro("SPIRV_CROSS_C_API_HLSL", "1");
    spv_mod.addCMacro("SPIRV_CROSS_C_API_MSL", "1");
    spv_mod.addCMacro("SPIRV_CROSS_C_API_REFLECT", "1");

    const spv_cross_flags: []const []const u8 = &.{"-std=c++14"};
    spv_mod.addCSourceFiles(.{
        .files = &.{
            "spirv_cross.cpp",
            "spirv_parser.cpp",
            "spirv_cross_parsed_ir.cpp",
            "spirv_cfg.cpp",
            "spirv_glsl.cpp",
            "spirv_msl.cpp",
            "spirv_hlsl.cpp",
            "spirv_reflect.cpp",
            "spirv_cross_util.cpp",
            "spirv_cross_c.cpp",
        },
        .flags = spv_cross_flags,
        .root = spirv_cross_dep.path("."),
    });

    const spirv_cross = b.addLibrary(.{
        .linkage = .static,
        .name = "spirv-cross-c",
        .root_module = spv_mod,
    });

    // Build SDL_shadercross static library (with DXC support via system-built dylib)
    const sc_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    sc_mod.linkLibrary(sdl.artifact("SDL3"));
    sc_mod.linkLibrary(spirv_cross);
    sc_mod.addIncludePath(spirv_cross_dep.path("."));
    sc_mod.addIncludePath(spirv_headers.path("include/spirv/1.2/"));
    sc_mod.addIncludePath(upstream.path("include"));
    sc_mod.addCMacro("SDL_SHADERCROSS_DXC", "1");
    sc_mod.addCSourceFile(.{
        .file = upstream.path("src/SDL_shadercross.c"),
        .flags = &.{"-std=c99"},
    });

    // Link DXC dynamic library from lib/
    sc_mod.addLibraryPath(b.path("lib"));
    sc_mod.addRPath(.{ .cwd_relative = b.pathFromRoot("lib") });

    const sdl_shadercross = b.addLibrary(.{
        .linkage = .static,
        .name = "SDL_shadercross",
        .root_module = sc_mod,
    });

    b.installArtifact(sdl_shadercross);

    // CLI executable
    const exe_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    exe_mod.addCSourceFile(.{ .file = upstream.path("src/cli.c") });
    exe_mod.addIncludePath(upstream.path("include"));
    exe_mod.linkLibrary(sdl_shadercross);
    exe_mod.linkLibrary(sdl.artifact("SDL3"));
    exe_mod.addLibraryPath(b.path("lib"));
    exe_mod.addRPath(.{ .cwd_relative = b.pathFromRoot("lib") });
    exe_mod.linkSystemLibrary("dxcompiler", .{
        .preferred_link_mode = .dynamic,
    });

    const exe = b.addExecutable(.{
        .name = "shadercross",
        .root_module = exe_mod,
    });

    b.installArtifact(exe);
}
