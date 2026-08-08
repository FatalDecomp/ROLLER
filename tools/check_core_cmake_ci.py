"""E6-S4: a change breaking roller-core's CMake build must fail ROLLER CI.

Before this story ROLLER CI validated the CMake *source lists*
(``check_source_set_drift.py``) and never compiled through CMake at all. The
only thing that built ``roller-core`` was TrackEditor's CI, and that builds the
pinned submodule commit rather than ROLLER master, so a break here stayed
invisible until somebody moved the pin.

This validates the job that closes that gap, and the one piece of it that can
silently rot: the pinned SDL versions in the Windows provisioning script have to
match the minimums CMakeLists.txt enforces, or the job provisions an SDL that
``find_package`` will refuse.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = REPOSITORY_ROOT / ".github" / "workflows" / "build.yml"
CMAKE_LISTS = REPOSITORY_ROOT / "CMakeLists.txt"
SDL_SCRIPT = REPOSITORY_ROOT / "scripts" / "install-sdl-windows.ps1"

JOB_NAME = "roller-core-cmake"
# roller-core is consumed by the editor on all three desktop platforms.
EXPECTED_PLATFORMS = ("Linux", "macOS", "Windows")


class CoreCmakeCiError(Exception):
    pass


def job_block(workflow: str) -> str:
    """The roller-core-cmake job, up to the start of the next job."""
    marker = f"\n  {JOB_NAME}:\n"
    if marker not in workflow:
        raise CoreCmakeCiError(f"no {JOB_NAME} job in {WORKFLOW.name}")
    block = workflow[workflow.index(marker) + 1 :]
    following = re.search(r"\n  [a-z][\w-]*:\n", block)
    return block[: following.start()] if following else block


def validate_job(workflow: str) -> None:
    block = job_block(workflow)

    # Core-only is how TrackEditor consumes it: no game, so no WildMidi and no
    # libcdio. Building the game here would test something else.
    for flag in ("-DROLLER_BUILD_EDITOR_CORE=ON", "-DROLLER_BUILD_GAME=OFF"):
        if flag not in block:
            raise CoreCmakeCiError(f"{JOB_NAME} does not configure with {flag}")

    if "cmake --build build-core" not in block:
        raise CoreCmakeCiError(f"{JOB_NAME} configures but never builds")

    # Compiling is not enough: ed0dd05 made ROLLER::core consumer-linkable, and
    # a missing symbol only appears when something links it.
    if "editor-core-link-test" not in block:
        raise CoreCmakeCiError(f"{JOB_NAME} never links a consumer of ROLLER::core")

    names = re.findall(r"^\s+- name:\s*(\S+)$", block, re.MULTILINE)
    if tuple(names) != EXPECTED_PLATFORMS:
        raise CoreCmakeCiError(
            f"{JOB_NAME} covers {names or 'nothing'}, expected {list(EXPECTED_PLATFORMS)}"
        )

    if "fail-fast: false" not in block:
        raise CoreCmakeCiError(
            f"{JOB_NAME} must not cancel sibling platforms: one compiler "
            "disagreeing is what the job exists to surface"
        )

    # The legacy sources are not warning-clean; turning this on would fail for
    # reasons that have nothing to do with the CMake build working.
    if "ROLLER_WARNINGS_AS_ERRORS=ON" in block:
        raise CoreCmakeCiError(
            f"{JOB_NAME} enables warnings-as-errors, which roller-core does not pass"
        )


def cmake_minimum(cmake_lists: str, variable: str) -> str:
    match = re.search(rf'set\({variable} "([^"]+)"\)', cmake_lists)
    if match is None:
        raise CoreCmakeCiError(f"{variable} is not set in CMakeLists.txt")
    return match.group(1)


def validate_sdl_pins(cmake_lists: str, script: str) -> dict[str, str]:
    """The provisioned SDL must satisfy the version find_package demands."""
    if "Get-FileHash" not in script or "SHA-256 mismatch" not in script:
        raise CoreCmakeCiError(
            "the Windows SDL script must verify what it downloads"
        )

    pins = {}
    for variable, package in (
        ("ROLLER_SDL3_MIN_VERSION", "SDL3"),
        ("ROLLER_SDL3_IMAGE_MIN_VERSION", "SDL3_image"),
    ):
        required = cmake_minimum(cmake_lists, variable)
        expected = f"{package}-{required}"
        if f'Directory = "{expected}"' not in script:
            raise CoreCmakeCiError(
                f"{package} is pinned to {required} by {variable}, but the "
                f"Windows script does not provision {expected}"
            )
        pins[package] = required
    return pins


def main() -> int:
    try:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        cmake_lists = CMAKE_LISTS.read_text(encoding="utf-8")
        script = SDL_SCRIPT.read_text(encoding="utf-8")
        validate_job(workflow)
        pins = validate_sdl_pins(cmake_lists, script)
    except (OSError, UnicodeError, CoreCmakeCiError) as error:
        print(f"roller-core CMake CI check failed: {error}", file=sys.stderr)
        return 1

    summary = ", ".join(f"{name}={version}" for name, version in pins.items())
    print(
        "roller-core CMake CI check passed: "
        f"{len(EXPECTED_PLATFORMS)} platforms, {summary}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
