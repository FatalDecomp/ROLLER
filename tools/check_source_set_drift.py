#!/usr/bin/env python3
"""Compare platform-effective Zig and CMake game translation-unit sets."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Mapping

from check_roller_core_manifest import load_manifest, validate_manifest


PLATFORMS = ("linux", "macos", "windows", "android", "emscripten")
ALL_PLATFORMS = frozenset(PLATFORMS)
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx"))
CMAKE_SOURCE_COMPONENTS = {
    "ROLLER_GAME_BASE_SOURCES": ALL_PLATFORMS,
    "ROLLER_GAME_NATIVE_SOURCES": ALL_PLATFORMS - {"emscripten"},
    "ROLLER_GAME_WEB_SOURCES": frozenset(("emscripten",)),
    "ROLLER_GAME_WINDOWS_SOURCES": frozenset(("windows",)),
}


class SourceSetError(ValueError):
    pass


@dataclass(frozen=True)
class SourceCall:
    line_number: int
    platforms: frozenset[str]
    sources: tuple[str, ...]


def _mask_literals_and_comments(text: str) -> str:
    """Preserve code positions while hiding comments and quoted contents."""

    masked = list(text)
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if state == "code":
            if char == "#":
                masked[index] = " "
                index += 1
                state = "line_comment"
                continue
            if char == "/" and following == "/":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "block_comment"
                continue
            if char in ('"', "'"):
                quote = char
                masked[index] = " "
                index += 1
                state = "string"
                continue
        elif state == "line_comment":
            if char == "\n":
                state = "code"
            else:
                masked[index] = " "
            index += 1
            continue
        elif state == "block_comment":
            if char == "*" and following == "/":
                masked[index] = masked[index + 1] = " "
                index += 2
                state = "code"
                continue
            if char != "\n":
                masked[index] = " "
            index += 1
            continue
        else:
            if char == "\\" and following:
                masked[index] = " "
                if following != "\n":
                    masked[index + 1] = " "
                index += 2
                continue
            masked[index] = " "
            index += 1
            if char == quote:
                state = "code"
            continue

        index += 1

    if state in ("string", "block_comment"):
        raise SourceSetError(f"unterminated {state.replace('_', ' ')}")
    return "".join(masked)


def _find_matching(masked: str, opening_index: int, opening: str, closing: str) -> int:
    if opening_index >= len(masked) or masked[opening_index] != opening:
        raise SourceSetError(f"expected {opening!r} at offset {opening_index}")

    depth = 0
    for index in range(opening_index, len(masked)):
        char = masked[index]
        if char == opening:
            depth += 1
        elif char == closing:
            depth -= 1
            if depth == 0:
                return index
    raise SourceSetError(f"unmatched {opening!r} at offset {opening_index}")


def _skip_space(masked: str, index: int) -> int:
    while index < len(masked) and masked[index].isspace():
        index += 1
    return index


def _boolean_branch_ranges(masked: str) -> list[tuple[int, int, frozenset[str]]]:
    ranges: list[tuple[int, int, frozenset[str]]] = []
    pattern = re.compile(r"\bif\s*\(\s*(!\s*)?(bWasm|bAndroid)\s*\)\s*\{")

    for match in pattern.finditer(masked):
        opening = masked.find("{", match.start(), match.end())
        closing = _find_matching(masked, opening, "{", "}")
        variable = match.group(2)
        true_platforms = (
            frozenset(("emscripten",))
            if variable == "bWasm"
            else frozenset(("android",))
        )
        if match.group(1):
            true_platforms = ALL_PLATFORMS - true_platforms
        false_platforms = ALL_PLATFORMS - true_platforms
        ranges.append((opening + 1, closing, true_platforms))

        cursor = _skip_space(masked, closing + 1)
        if not masked.startswith("else", cursor):
            continue
        cursor = _skip_space(masked, cursor + len("else"))
        if cursor < len(masked) and masked[cursor] == "{":
            false_close = _find_matching(masked, cursor, "{", "}")
            ranges.append((cursor + 1, false_close, false_platforms))
            continue
        if masked.startswith("switch", cursor):
            switch_open = masked.find("{", cursor)
            if switch_open == -1:
                raise SourceSetError("platform switch has no body")
            switch_close = _find_matching(masked, switch_open, "{", "}")
            ranges.append((switch_open + 1, switch_close, false_platforms))

    return ranges


def _os_switch_arm_ranges(masked: str) -> list[tuple[int, int, frozenset[str]]]:
    ranges: list[tuple[int, int, frozenset[str]]] = []
    switch_pattern = re.compile(
        r"\bswitch\s*\(\s*target\s*\.\s*result\s*\.\s*os\s*\.\s*tag\s*\)\s*\{"
    )
    arm_pattern = re.compile(r"(?:\.(windows|linux|macos|emscripten)|else)\s*=>\s*\{")

    for switch_match in switch_pattern.finditer(masked):
        switch_open = masked.find("{", switch_match.start(), switch_match.end())
        switch_close = _find_matching(masked, switch_open, "{", "}")
        body = masked[switch_open + 1 : switch_close]
        explicit_platforms: set[str] = set()
        arms: list[tuple[re.Match[str], int, int]] = []

        for arm_match in arm_pattern.finditer(body):
            absolute_match = switch_open + 1 + arm_match.start()
            preceding = body[: arm_match.start()]
            depth = preceding.count("{") - preceding.count("}")
            if depth != 0:
                continue
            arm_open = masked.find(
                "{", absolute_match, switch_open + 1 + arm_match.end()
            )
            arm_close = _find_matching(masked, arm_open, "{", "}")
            arms.append((arm_match, arm_open, arm_close))
            if arm_match.group(1):
                explicit_platforms.add(arm_match.group(1))

        for arm_match, arm_open, arm_close in arms:
            platform = arm_match.group(1)
            allowed = (
                frozenset((platform,))
                if platform is not None
                else ALL_PLATFORMS - explicit_platforms
            )
            ranges.append((arm_open + 1, arm_close, allowed))

    return ranges


def _normalize_source(root: str, source: str, context: str) -> str:
    root_path = PurePosixPath(root)
    source_path = PurePosixPath(source)
    if root_path.is_absolute() or source_path.is_absolute():
        raise SourceSetError(f"{context}: absolute source paths are unsupported")
    combined = root_path / source_path
    if ".." in combined.parts or combined.suffix.lower() not in SOURCE_SUFFIXES:
        raise SourceSetError(
            f"{context}: invalid translation unit {combined.as_posix()!r}"
        )
    normalized = combined.as_posix()
    return normalized.removeprefix("./")


def parse_zig_source_calls(build_zig: str) -> list[SourceCall]:
    masked = _mask_literals_and_comments(build_zig)
    branch_ranges = _boolean_branch_ranges(masked) + _os_switch_arm_ranges(masked)
    call_pattern = re.compile(r"\bexe_mod\s*\.\s*addCSourceFiles\s*\(")
    calls: list[SourceCall] = []

    for match in call_pattern.finditer(masked):
        opening = masked.find("(", match.start(), match.end())
        closing = _find_matching(masked, opening, "(", ")")
        call_text = build_zig[opening + 1 : closing]
        call_masked = masked[opening + 1 : closing]
        line_number = build_zig.count("\n", 0, match.start()) + 1
        context = f"build.zig:{line_number}"

        root_match = re.search(
            r'\.root\s*=\s*b\s*\.\s*path\s*\(\s*"([^"\r\n]+)"\s*\)',
            call_text,
        )
        if re.search(r"\.root\s*=", call_masked) and root_match is None:
            raise SourceSetError(f"{context}: .root must be a literal b.path(...)")
        root = root_match.group(1) if root_match else "."
        files_match = re.search(r"\.files\s*=\s*&\s*\.\s*\{", call_masked)
        if files_match is None:
            raise SourceSetError(f"{context}: .files must be an inline .{{...}} list")
        files_open = call_masked.find("{", files_match.start(), files_match.end())
        files_close = _find_matching(call_masked, files_open, "{", "}")
        files_text = call_text[files_open + 1 : files_close]
        remaining_files_expression = _mask_literals_and_comments(files_text)
        if remaining_files_expression.strip(" \t\r\n,"):
            raise SourceSetError(
                f"{context}: source list must contain only string literals"
            )
        file_names = re.findall(r'"([^"\r\n]+)"', files_text)
        if not file_names:
            raise SourceSetError(f"{context}: source list is empty")

        platforms = ALL_PLATFORMS
        for range_start, range_end, allowed in branch_ranges:
            if range_start <= match.start() < range_end:
                platforms = platforms & allowed
        if not platforms:
            raise SourceSetError(f"{context}: source block has contradictory platforms")

        sources = tuple(
            _normalize_source(root, file_name, context) for file_name in file_names
        )
        calls.append(SourceCall(line_number, platforms, sources))

    if not calls:
        raise SourceSetError("build.zig has no exe_mod.addCSourceFiles blocks")
    return calls


def effective_zig_sources(build_zig: str) -> dict[str, frozenset[str]]:
    by_platform: dict[str, list[str]] = {platform: [] for platform in PLATFORMS}
    for call in parse_zig_source_calls(build_zig):
        for platform in call.platforms:
            by_platform[platform].extend(call.sources)

    result: dict[str, frozenset[str]] = {}
    for platform, sources in by_platform.items():
        duplicates = sorted({source for source in sources if sources.count(source) > 1})
        if duplicates:
            raise SourceSetError(
                f"build.zig adds duplicate {platform} sources:\n  "
                + "\n  ".join(duplicates)
            )
        result[platform] = frozenset(sources)
    return result


def _cmake_command_body(cmake_lists: str, command: str, argument: str) -> str:
    masked = _mask_literals_and_comments(cmake_lists)
    pattern = re.compile(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(argument)}\b"
    )
    matches = list(pattern.finditer(masked))
    if len(matches) != 1:
        raise SourceSetError(
            f"CMakeLists.txt must contain exactly one {command}({argument} ...) command"
        )
    opening = masked.find("(", matches[0].start(), matches[0].end())
    closing = _find_matching(masked, opening, "(", ")")
    return cmake_lists[opening + 1 : closing]


def parse_cmake_source_components(cmake_lists: str) -> dict[str, tuple[str, ...]]:
    components: dict[str, tuple[str, ...]] = {}
    for variable in CMAKE_SOURCE_COMPONENTS:
        body = _cmake_command_body(cmake_lists, "set", variable)
        paths = tuple(
            match.group(1)
            for match in re.finditer(
                r"(?<![A-Za-z0-9_./+-])"
                r"([A-Za-z0-9_./+-]+\.(?:c|cc|cpp|cxx))"
                r"(?![A-Za-z0-9_./+-])",
                body,
                flags=re.IGNORECASE,
            )
        )
        if not paths:
            raise SourceSetError(f"CMakeLists.txt {variable} is empty")
        components[variable] = tuple(
            _normalize_source(".", path, f"CMakeLists.txt {variable}")
            for path in paths
        )

    executable_body = _cmake_command_body(cmake_lists, "add_executable", "roller")
    if "${ROLLER_GAME_SOURCES}" not in executable_body:
        raise SourceSetError(
            "CMake roller target does not consume ${ROLLER_GAME_SOURCES}"
        )
    core_body = _cmake_command_body(cmake_lists, "add_library", "roller-core")
    if "${ROLLER_CORE_SOURCES}" not in core_body:
        raise SourceSetError(
            "CMake roller-core target does not consume ${ROLLER_CORE_SOURCES}"
        )

    masked = _mask_literals_and_comments(cmake_lists)
    required_wiring = (
        r"set\s*\(\s*ROLLER_GAME_SOURCES\s+\$\{ROLLER_GAME_BASE_SOURCES\}\s*\)",
        r"list\s*\(\s*APPEND\s+ROLLER_GAME_SOURCES\s+"
        r"\$\{ROLLER_GAME_NATIVE_SOURCES\}\s*\)",
        r"list\s*\(\s*APPEND\s+ROLLER_GAME_SOURCES\s+"
        r"\$\{ROLLER_GAME_WEB_SOURCES\}\s*\)",
        r"list\s*\(\s*APPEND\s+ROLLER_GAME_SOURCES\s+"
        r"\$\{ROLLER_GAME_WINDOWS_SOURCES\}\s*\)",
    )
    if any(re.search(pattern, masked) is None for pattern in required_wiring):
        raise SourceSetError(
            "CMake platform source components are not wired into "
            "ROLLER_GAME_SOURCES"
        )
    return components


def effective_cmake_sources(cmake_lists: str) -> dict[str, frozenset[str]]:
    components = parse_cmake_source_components(cmake_lists)
    by_platform: dict[str, list[str]] = {platform: [] for platform in PLATFORMS}
    for variable, platforms in CMAKE_SOURCE_COMPONENTS.items():
        for platform in platforms:
            by_platform[platform].extend(components[variable])

    result: dict[str, frozenset[str]] = {}
    for platform, sources in by_platform.items():
        duplicates = sorted({source for source in sources if sources.count(source) > 1})
        if duplicates:
            raise SourceSetError(
                f"CMake adds duplicate {platform} sources:\n  "
                + "\n  ".join(duplicates)
            )
        result[platform] = frozenset(sources)
    return result


def validate_integration(build_zig: str, workflow: str) -> None:
    step_match = re.search(
        r"const\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*b\s*\.\s*step\s*\(\s*"
        r'"check-source-set-drift"',
        build_zig,
    )
    if step_match is None or '"tools/check_source_set_drift.py"' not in build_zig:
        raise SourceSetError(
            "build.zig does not expose the check-source-set-drift build step"
        )
    step_variable = re.escape(step_match.group(1))
    if re.search(
        rf"test_step\s*\.\s*dependOn\s*\(\s*{step_variable}\s*\)", build_zig
    ) is None:
        raise SourceSetError(
            "build.zig test step does not depend on check-source-set-drift"
        )
    if "run: python tools/check_source_set_drift.py" not in workflow:
        raise SourceSetError(
            "reusable build workflow does not run the source-set drift check"
        )


def validate(
    *,
    build_zig: str,
    cmake_lists: str,
    workflow: str,
    manifest_entries: Mapping[str, str],
    repository_root: Path,
) -> dict[str, frozenset[str]]:
    validate_integration(build_zig, workflow)
    zig_sources = effective_zig_sources(build_zig)
    cmake_sources = effective_cmake_sources(cmake_lists)
    errors: list[str] = []

    for platform in PLATFORMS:
        zig_only = sorted(zig_sources[platform] - cmake_sources[platform])
        cmake_only = sorted(cmake_sources[platform] - zig_sources[platform])
        if zig_only:
            errors.append(
                f"{platform}: sources present in Zig but missing from CMake:\n  "
                + "\n  ".join(zig_only)
            )
        if cmake_only:
            errors.append(
                f"{platform}: sources present in CMake but missing from Zig:\n  "
                + "\n  ".join(cmake_only)
            )

    all_sources = set().union(*zig_sources.values(), *cmake_sources.values())
    for source in sorted(all_sources):
        if not (repository_root / Path(*PurePosixPath(source).parts)).is_file():
            errors.append(f"source-set entry does not exist: {source}")
        source_path = PurePosixPath(source)
        if source_path.parts[:2] == ("PROJECTS", "ROLLER"):
            if source_path.suffix != ".c" or source not in manifest_entries:
                errors.append(
                    "game source is not classified by roller-core.srclist: "
                    f"{source}"
                )

    for platform in PLATFORMS:
        sources = zig_sources[platform]
        for source, category in manifest_entries.items():
            if category != "STUB_SWAP" or source not in sources:
                continue
            source_path = PurePosixPath(source)
            implementation = source_path.with_name(
                source_path.stem.removesuffix("_stub") + ".c"
            ).as_posix()
            if implementation in sources:
                errors.append(
                    f"{platform}: real and stub implementations are both selected: "
                    f"{implementation}, {source}"
                )

    if errors:
        raise SourceSetError("\n".join(errors))
    return zig_sources


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    default_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=default_root,
        help="ROLLER checkout root (defaults to the parent of tools)",
    )
    parser.add_argument("--build-zig", type=Path)
    parser.add_argument("--cmake-lists", type=Path)
    parser.add_argument("--workflow", type=Path)
    parser.add_argument("--manifest", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository_root = args.repository_root.resolve()
    build_zig_path = args.build_zig or repository_root / "build.zig"
    cmake_lists_path = args.cmake_lists or repository_root / "CMakeLists.txt"
    workflow_path = (
        args.workflow or repository_root / ".github" / "workflows" / "build.yml"
    )
    manifest_path = args.manifest or repository_root / "roller-core.srclist"

    try:
        validate_manifest(manifest_path, repository_root)
        source_sets = validate(
            build_zig=build_zig_path.read_text(encoding="utf-8"),
            cmake_lists=cmake_lists_path.read_text(encoding="utf-8"),
            workflow=workflow_path.read_text(encoding="utf-8"),
            manifest_entries=load_manifest(manifest_path, repository_root),
            repository_root=repository_root,
        )
    except (OSError, UnicodeError, SourceSetError, ValueError) as error:
        print(f"source-set drift check failed: {error}", file=sys.stderr)
        return 1

    summary = ", ".join(
        f"{platform}={len(source_sets[platform])}" for platform in PLATFORMS
    )
    print(f"source-set drift check passed: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
