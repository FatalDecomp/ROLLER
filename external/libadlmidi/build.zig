const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    lib_mod.link_libcpp = true;
    lib_mod.sanitize_c = .off;
    lib_mod.addIncludePath(b.path("include"));
    lib_mod.addIncludePath(b.path("src"));
    if (target.result.os.tag == .emscripten) {
        const sysroot = b.sysroot orelse
            @panic("the Emscripten target requires --sysroot <em-config CACHE>/sysroot");
        lib_mod.addSystemIncludePath(.{
            .cwd_relative = b.pathJoin(&.{ sysroot, "include" }),
        });
    }

    // DosBox OPL3 emulator only — disable all others
    const flags = &.{
        "-DDOSBOX_NO_MUTEX",
        "-DADLMIDI_DISABLE_NUKED_EMULATOR",
        "-DADLMIDI_DISABLE_OPAL_EMULATOR",
        "-DADLMIDI_DISABLE_JAVA_EMULATOR",
        "-DADLMIDI_DISABLE_ESFMU_EMULATOR",
        "-DADLMIDI_DISABLE_MAME_OPL2_EMULATOR",
        "-DADLMIDI_DISABLE_YMFM_EMULATOR",
    };

    lib_mod.addCSourceFiles(.{
        .flags = flags,
        .files = &.{
            "src/adlmidi.cpp",
            "src/adlmidi_load.cpp",
            "src/adlmidi_midiplay.cpp",
            "src/adlmidi_opl3.cpp",
            "src/adlmidi_private.cpp",
            "src/adlmidi_sequencer.cpp",
            "src/inst_db.cpp",
            "src/chips/dosbox_opl3.cpp",
            "src/chips/dosbox/dbopl.cpp",
            "src/wopl/wopl_file.c",
            "src/models/model_ail.c",
            "src/models/model_apogee.c",
            "src/models/model_dmx.c",
            "src/models/model_generic.c",
            "src/models/model_hmi_sos.c",
            "src/models/model_msadlib.c",
            "src/models/model_oconnell.c",
            "src/models/model_win9x.c",
        },
    });

    const lib = b.addLibrary(.{
        .name = "adlmidi",
        .linkage = .static,
        .root_module = lib_mod,
    });
    lib.installHeader(b.path("include/adlmidi.h"), "adlmidi.h");
    b.installArtifact(lib);
}
