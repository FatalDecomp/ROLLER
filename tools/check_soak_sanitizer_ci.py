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

**Scope.** E6-S5 landed before E1-S9 existed, so for a while this job ran only
the E0-S7 facade lifecycle suite -- the right surface, but not a long soak.
E1-S9 has since been written, and this check now requires both: the lifecycle
suite, which proves each transition is correct once, and
``test-e1-s9-reload-soak``, which proves nothing accumulates when they repeat
hundreds of times against a real track. Dropping either from the job would
leave a green sanitizer run that no longer sanitizes what it claims to.

The soak needs a real track, so the job also has to provision the freeware demo
assets and point ``-Dassets-path`` at them; without that step the soak step
fails on a missing file, which is loud, but a job that quietly stopped passing
``-Dassets-path`` would be soaking the wrong tree.
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
LIFECYCLE_STEP = "zig build test-editor-api -Dvalgrind"
SOAK_STEP = "zig build test-e1-s9-reload-soak -Dvalgrind"
SOAK_SOURCE = REPOSITORY_ROOT / "tests" / "editor_reload_soak_acceptance.c"
# E1-S9's acceptance criterion is "hundreds of load/render/load cycles".
MINIMUM_SOAK_CYCLES = 200


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

    for step in (LIFECYCLE_STEP, SOAK_STEP):
        if step not in block:
            raise SoakCiError(
                f"{JOB_NAME} does not run `{step}`; without -Dvalgrind the job "
                "passes without sanitizing anything"
            )

    # Every Valgrind step must pin a decodable CPU. The build otherwise targets
    # the runner's native CPU, and an ubuntu-latest Ice Lake Xeon gets AVX-512
    # that Valgrind 3.22 cannot decode -- it raises SIGILL inside
    # ed_emit_surface, which reads like a wild jump in ROLLER and is nothing of
    # the kind.
    for step in block.split("- name:"):
        if "-Dvalgrind" in step and "-Dcpu=" not in step:
            title = step.strip().splitlines()[0]
            raise SoakCiError(
                f"the `{title}` step runs Valgrind without pinning -Dcpu; a "
                "native-CPU build can emit instructions Valgrind cannot decode"
            )
    if "install -y valgrind" not in block:
        raise SoakCiError(f"{JOB_NAME} never installs Valgrind")
    if "if: inputs.run_soak" not in block:
        raise SoakCiError(
            f"{JOB_NAME} is not gated on run_soak; it would run on every push"
        )

    # The soak renders a real track, which the repository does not carry.
    if "fetch_demo_assets.py" not in block:
        raise SoakCiError(
            f"{JOB_NAME} never provisions the freeware demo assets the soak "
            "loads"
        )
    if "-Dassets-path=" not in block:
        raise SoakCiError(
            f"{JOB_NAME} does not point the soak at the assets it provisioned"
        )

    cycles = re.search(r"-Dsoak-cycles=(\d+)", block)
    if cycles is None:
        raise SoakCiError(f"{JOB_NAME} does not pin the soak's cycle count")
    if int(cycles.group(1)) < MINIMUM_SOAK_CYCLES:
        raise SoakCiError(
            f"{JOB_NAME} soaks for {cycles.group(1)} cycles; E1-S9 asks for "
            f"hundreds, so at least {MINIMUM_SOAK_CYCLES}"
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
    if not re.search(
        r"runArtifact\(b,\s*editor_reload_soak_exe,\s*under_valgrind\)", build_zig
    ):
        raise SoakCiError(
            "the E1-S9 soak executable is not wrapped by the Valgrind runner, so "
            "the scheduled job would run it unsanitized"
        )
    if not SOAK_SOURCE.exists():
        raise SoakCiError(f"{SOAK_SOURCE.name} is missing")
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
