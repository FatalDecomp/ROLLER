"""E6-S5: the load/render/reload soak runs under a sanitizer on a schedule,
and a failure blocks the release.

Two things here are load-bearing and easy to undo by accident.

The first is *where the job lives*. It is a job inside the reusable build
workflow rather than a standalone scheduled workflow, because the nightly
release job depends on that whole workflow: a memory error fails the workflow
and the release is never created. A standalone scheduled workflow would report
the failure and publish the nightly anyway, which satisfies "runs on schedule"
while quietly failing "failures block release".

The second is that ``-Dvalgrind`` must actually reach the run steps. Without it
the job still passes -- it just stops being a sanitizer job.

**Scope, recorded honestly:** the specification says "run E1-S9's reload soak".
E1-S9 has not been written, so what runs today is the E0-S7 facade lifecycle
suite -- init, load, render, unload, shutdown across valid, malformed, and
oversized tracks. That is the right surface, but not yet the long soak. When
E1-S9 lands it goes behind the same ``-Dvalgrind`` flag and this same job, and
this check should grow to require it.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = REPOSITORY_ROOT / ".github" / "workflows"
BUILD_WORKFLOW = WORKFLOWS / "build.yml"
NIGHTLY_WORKFLOW = WORKFLOWS / "nightly-build.yml"
BUILD_ZIG = REPOSITORY_ROOT / "build.zig"

JOB_NAME = "soak-sanitizer"
SOAK_STEP = "zig build test-editor-api -Dvalgrind"


class SoakCiError(Exception):
    pass


def job_block(workflow: str, name: str) -> str:
    marker = f"\n  {name}:\n"
    if marker not in workflow:
        raise SoakCiError(f"no {name} job in {BUILD_WORKFLOW.name}")
    block = workflow[workflow.index(marker) + 1 :]
    following = re.search(r"\n  [a-z][\w-]*:\n", block)
    return block[: following.start()] if following else block


def validate_job(workflow: str) -> None:
    block = job_block(workflow, JOB_NAME)

    if SOAK_STEP not in block:
        raise SoakCiError(
            f"{JOB_NAME} does not run `{SOAK_STEP}`; without -Dvalgrind the job "
            "passes without sanitizing anything"
        )
    if "install -y valgrind" not in block:
        raise SoakCiError(f"{JOB_NAME} never installs Valgrind")
    if "if: inputs.run_soak" not in block:
        raise SoakCiError(
            f"{JOB_NAME} is not gated on run_soak; it would run on every push"
        )


def validate_release_is_blocked(build_workflow: str, nightly: str) -> None:
    """The soak must sit where a failure stops the release, not beside it."""
    if "run_soak:" not in build_workflow:
        raise SoakCiError("build.yml declares no run_soak input")

    # The nightly is the release path, and it must ask for the soak.
    if not re.search(r"^\s+run_soak:\s*true\s*$", nightly, re.MULTILINE):
        raise SoakCiError("the nightly does not enable run_soak, so it never soaks")

    release = job_block(nightly, "release")
    needs = re.search(r"needs:\s*\[([^\]]*)\]", release)
    if needs is None or "build" not in needs.group(1):
        raise SoakCiError(
            "the nightly release job does not depend on the build workflow, so a "
            "soak failure would not block it"
        )

    # A standalone scheduled soak workflow would not gate anything.
    strays = [
        path.name
        for path in WORKFLOWS.glob("*.yml")
        if path.name not in {"build.yml", "ci.yml", "nightly-build.yml", "markdown-lint.yml"}
        and "valgrind" in path.read_text(encoding="utf-8").lower()
    ]
    if strays:
        raise SoakCiError(
            f"the soak must gate the release from inside build.yml; found a "
            f"separate sanitizer workflow: {strays}"
        )


def validate_build_option(build_zig: str) -> None:
    if '"valgrind"' not in build_zig:
        raise SoakCiError("build.zig defines no -Dvalgrind option")
    if "--error-exitcode=1" not in build_zig:
        raise SoakCiError(
            "the Valgrind wrapper must use --error-exitcode so a memory error "
            "fails the step instead of being buried in the log"
        )

    wrapped = len(re.findall(r"runArtifact\(b,\s*\w+,\s*under_valgrind\)", build_zig))
    if wrapped == 0:
        raise SoakCiError("no test executable is wrapped by the Valgrind runner")
    return wrapped


def main() -> int:
    try:
        build_workflow = BUILD_WORKFLOW.read_text(encoding="utf-8")
        nightly = NIGHTLY_WORKFLOW.read_text(encoding="utf-8")
        build_zig = BUILD_ZIG.read_text(encoding="utf-8")
        validate_job(build_workflow)
        validate_release_is_blocked(build_workflow, nightly)
        wrapped = validate_build_option(build_zig)
    except (OSError, UnicodeError, SoakCiError) as error:
        print(f"soak sanitizer CI check failed: {error}", file=sys.stderr)
        return 1

    print(
        "soak sanitizer CI check passed: "
        f"{wrapped} executables wrapped, nightly release gated"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
