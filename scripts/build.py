#!/usr/bin/env python3

import sys
import subprocess
import platform
import argparse
from pathlib import Path


def c_string_literal(value):
    return value.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")


def write_build_info(args):
    build_target = args.build_target or args.target
    header_path = Path("PROJECTS/ROLLER/build_info.h")
    header_text = """#ifndef _ROLLER_BUILD_INFO_H
#define _ROLLER_BUILD_INFO_H
//-------------------------------------------------------------------------------------------------

#define BUILD_VERSION  "{version}"
#define BUILD_GIT_HASH "{git_hash}"
#define BUILD_DATE     "{build_date}"
#define BUILD_TARGET   "{build_target}"

//-------------------------------------------------------------------------------------------------
#endif
""".format(
        version=c_string_literal(args.version),
        git_hash=c_string_literal(args.git_hash),
        build_date=c_string_literal(args.build_date),
        build_target=c_string_literal(build_target),
    )
    header_path.write_text(header_text)

def main():
    parser = argparse.ArgumentParser(description="Build ROLLER for specific platform")
    _ = parser.add_argument("-r", "--run", action="store_true", help="Run after building")
    _ = parser.add_argument("--target", default="native", help="Build target")
    _ = parser.add_argument("--release", default="fast", help="Release mode")
    _ = parser.add_argument("--git-hash", default="unknown", help="Full git hash for build identity")
    _ = parser.add_argument("--version", default="local", help="Version string for build identity")
    _ = parser.add_argument("--build-date", default="unknown", help="Build date for build identity")
    _ = parser.add_argument("--build-target", default=None, help="Target string for build identity")
    _ = parser.add_argument("--crash-debug", action="store_true", help="Enable crash dump friendly build flags")
    _ = parser.add_argument("--build-id", default=None, help="Zig build ID style")

    args = parser.parse_args()

    print("Building ROLLER...")
    write_build_info(args)

    # Check for unsupported cross-compilation
    if "macos" in args.target and platform.system() != "Darwin":
        print("Error: Cross-compilation to macOS requires running on macOS")
        print("Please use a macOS system or GitHub Actions with macos runners")
        return 1

    # For macOS builds on macOS, use native for ARM64 and provide sysroot for x86_64
    if platform.system() == "Darwin" and "macos" in args.target:
        if args.target == "aarch64-macos":
            # Use native build for ARM64 to avoid cross-compilation issues
            target = "native"
            print(f"Building natively on macOS (target changed from {args.target} to native)")
        elif args.target == "x86_64-macos" and platform.machine() == "x86_64":
            # Use native build for x86_64 on x86_64 hosts
            target = "native"
            print(f"Building natively on macOS (target changed from {args.target} to native)")
        else:
            # For x86_64-macos, cross-compile with sysroot
            target = args.target
            try:
                sdk_path = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                print(f"Using macOS SDK: {sdk_path}")
            except subprocess.CalledProcessError:
                print("xcrun failed, using fallback sysroot")
                sdk_path = "/"
    else:
        target = args.target

    # Build command arguments
    cmd = ["zig", "build", f"--release={args.release}", f"-Dtarget={target}"]

    if args.crash_debug:
        cmd.append("-Dcrash-debug=true")

    if args.build_id:
        cmd.append(f"--build-id={args.build_id}")

    # Add sysroot for macOS cross-compilation (but not native builds)
    if platform.system() == "Darwin" and "macos" in args.target and target != "native":
        cmd.extend(["--sysroot", sdk_path])

    if args.run:
        cmd.append("run")

    # Print build info
    print(f"Building for {target}...")

    # Run the build command
    result = subprocess.run(cmd)

    # Check if build succeeded
    if result.returncode == 0:
        print("Build completed successfully")
        print("Binary location: zig-out/bin/roller")
        return 0
    else:
        print("Build failed")
        return 1

if __name__ == "__main__":
    sys.exit(main())
