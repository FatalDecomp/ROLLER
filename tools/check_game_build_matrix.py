#!/usr/bin/env python3
"""Validate the six-target E0-S4 game build contract."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


class MatrixError(ValueError):
    """Raised when the game build matrix no longer matches its contract."""


def _job_section(workflow: str, job_name: str, next_job_name: str | None) -> str:
    end = rf"(?=^  {re.escape(next_job_name)}:\s*$)" if next_job_name else r"\Z"
    match = re.search(
        rf"(?ms)^  {re.escape(job_name)}:\s*$\n(?P<body>.*?){end}",
        workflow,
    )
    if match is None:
        raise MatrixError(f"workflow job {job_name!r} is missing")
    return match.group("body")


def _yaml_scalar(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def _desktop_rows(build_job: str) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    current: dict[str, str] | None = None

    for line in build_job.splitlines():
        start = re.match(r"^          - target:\s*(.+?)\s*$", line)
        if start:
            if current is not None:
                rows.append(current)
            current = {"target": _yaml_scalar(start.group(1))}
            continue
        if current is None:
            continue
        field = re.match(r"^            (runs-on|os|arch):\s*(.+?)\s*$", line)
        if field:
            current[field.group(1)] = _yaml_scalar(field.group(2))
        elif line.startswith("    steps:"):
            break

    if current is not None:
        rows.append(current)
    return rows


def _single_quoted_values(text: str) -> list[str]:
    return re.findall(r"'([^']+)'", text)


def _required_match(pattern: str, text: str, description: str) -> re.Match[str]:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise MatrixError(f"could not find {description}")
    return match


def validate(
    contract: dict[str, Any],
    workflow: str,
    android_gradle: str,
    mise_config: str,
    build_wrapper: str,
) -> tuple[int, int, list[str]]:
    if contract.get("schema_version") != 1:
        raise MatrixError("game build contract schema_version must be 1")

    targets = contract.get("targets")
    if not isinstance(targets, list) or len(targets) != 6:
        raise MatrixError("game build contract must contain exactly six targets")
    ids = [target.get("id") for target in targets]
    if len(set(ids)) != len(ids) or any(not value for value in ids):
        raise MatrixError("game build contract target IDs must be nonempty and unique")

    zig_targets = [target for target in targets if target.get("builder") == "zig"]
    android_targets = [
        target for target in targets if target.get("builder") == "gradle"
    ]
    if len(zig_targets) != 5 or len(android_targets) != 1:
        raise MatrixError(
            "game build contract must contain five Zig targets and one Gradle target"
        )

    build_job = _job_section(workflow, "build", "android")
    expected_rows = [
        {
            "target": target["target"],
            "runs-on": target["runner"],
            "os": target["os"],
            "arch": target["arch"],
        }
        for target in zig_targets
    ]
    actual_rows = _desktop_rows(build_job)
    if actual_rows != expected_rows:
        raise MatrixError(
            "desktop Zig matrix does not match .github/game-build-targets.json\n"
            f"expected: {expected_rows}\nactual:   {actual_rows}"
        )
    if "mise run build \\" not in build_job:
        raise MatrixError("desktop matrix no longer invokes the canonical mise build task")
    if "--target=${{ matrix.target }}" not in build_job:
        raise MatrixError("desktop matrix no longer passes its Zig target to the build task")
    if 'run = "python scripts/build.py"' not in mise_config:
        raise MatrixError("mise build task no longer invokes scripts/build.py")
    if not re.search(r'cmd\s*=\s*\["zig",\s*"build"', build_wrapper):
        raise MatrixError("scripts/build.py no longer invokes zig build")
    if 'f"-Dtarget={target}"' not in build_wrapper:
        raise MatrixError("scripts/build.py no longer forwards its resolved Zig target")

    android = android_targets[0]
    android_job = _job_section(workflow, "android", None)
    runner = _required_match(
        r"^    runs-on:\s*(\S+)\s*$", android_job, "Android runner"
    ).group(1)
    if runner != android["runner"]:
        raise MatrixError(
            f"Android runner is {runner!r}; expected {android['runner']!r}"
        )
    for package in (
        f"platforms;android-{android['compile_sdk']}",
        f"build-tools;{android['build_tools']}",
        f"ndk;{android['ndk_version']}",
    ):
        if package not in android_job:
            raise MatrixError(f"Android workflow no longer installs {package}")
    if f"gradle-version: {android['gradle_version']}" not in android_job:
        raise MatrixError("Android workflow Gradle pin does not match the contract")
    if "gradle -p android assembleRelease --no-daemon" not in android_job:
        raise MatrixError("Android workflow no longer builds the release APK")

    compile_sdk = int(
        _required_match(
            r"^\s*compileSdk(?:\s*=)?\s+(\d+)\s*$",
            android_gradle,
            "Android compileSdk",
        ).group(1)
    )
    if compile_sdk != android["compile_sdk"]:
        raise MatrixError("Android compileSdk does not match the workflow contract")
    ndk_version = _required_match(
        r"^\s*ndkVersion(?:\s*=)?\s+['\"]([^'\"]+)['\"]\s*$",
        android_gradle,
        "Android NDK version",
    ).group(1)
    if ndk_version != android["ndk_version"]:
        raise MatrixError("Android Gradle NDK pin does not match the workflow contract")
    abi_line = _required_match(
        r"^\s*ndk\s*\{\s*abiFilters\s+(.+?)\s*\}\s*$",
        android_gradle,
        "Android abiFilters",
    ).group(1)
    actual_abis = _single_quoted_values(abi_line)
    if actual_abis != android["abis"]:
        raise MatrixError(
            f"Android ABIs are {actual_abis}; expected {android['abis']}"
        )

    return len(targets), len(zig_targets), actual_abis


def validate_repository(root: Path = REPOSITORY_ROOT) -> tuple[int, int, list[str]]:
    contract = json.loads(
        (root / ".github/game-build-targets.json").read_text(encoding="utf-8")
    )
    return validate(
        contract,
        (root / ".github/workflows/build.yml").read_text(encoding="utf-8"),
        (root / "android/app/build.gradle").read_text(encoding="utf-8"),
        (root / "mise.toml").read_text(encoding="utf-8"),
        (root / "scripts/build.py").read_text(encoding="utf-8"),
    )


def main() -> int:
    try:
        target_count, zig_count, android_abis = validate_repository()
    except (MatrixError, KeyError, json.JSONDecodeError) as error:
        print(f"E0-S4 game build contract failed: {error}", file=sys.stderr)
        return 1

    print(
        f"E0-S4 game build contract OK: {target_count} targets "
        f"({zig_count} Zig desktop + Android APK for {', '.join(android_abis)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
