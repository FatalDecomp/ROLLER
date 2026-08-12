"""E1-S9: reload robustness.

*AC:* hundreds of load/render/load cycles including malformed files leave no
leaks or stale GPU resources under ASan/Valgrind or platform equivalent; the
soak test runs in CI.

The soak itself cannot run here -- it needs a real track, its textures, and
several minutes -- so this file pins the properties that would otherwise rot
quietly: that the cycle really does include a malformed load, that the
invariants it checks are the ones that catch an accumulating facade, that the
resource accounting it reads counts bounded tables, and that CI still runs it
under a sanitizer.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
SOAK = ROOT / "tests" / "editor_reload_soak_acceptance.c"
BUILD_ZIG = ROOT / "build.zig"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class SoakShapeTests(unittest.TestCase):
    def test_the_soak_exists_and_drives_the_real_facade(self) -> None:
        source = read(SOAK)

        # Not a stub with a mocked scene: the public facade, on a worker
        # thread, exactly as the editor drives it.
        for call in (
            "RollerEd_Bootstrap(",
            "RollerEd_Init(",
            "RollerEd_LoadTrackFile(",
            "RollerEd_RenderFrame(",
            "RollerEd_UnloadTrack(",
            "RollerEd_Shutdown(",
            "RollerEd_Teardown(",
        ):
            self.assertIn(call, source)
        self.assertIn("SDL_CreateThread(", source)

    def test_every_cycle_includes_a_malformed_load(self) -> None:
        body = function_body(read(SOAK), "static int soak_run_phase(")

        # Unconditional, at the top of the cycle -- a malformed file every
        # cycle is the difference between this and a reload loop.
        self.assertIn("soak_load_malformed(", body)
        self.assertNotIn("if (iCycle % SOAK_MALFORMED", body)
        self.assertIn("soak_load_valid(", body)

    def test_the_malformed_inputs_cover_the_real_failure_shapes(self) -> None:
        body = function_body(read(SOAK), "static int soak_build_malformed_inputs(")

        # Derived from the real track rather than checked in, so they stay
        # malformed in the ways real files go wrong.
        self.assertIn("soak_truncated.trk", body)
        self.assertIn("soak_corrupt.trk", body)
        self.assertIn("soak_empty.trk", body)
        # Never written: a path that does not exist is the commonest failure.
        self.assertIn("soak_missing.trk", body)
        self.assertIn("remove(pContext->szMalformed[3])", body)

    def test_a_refused_load_may_not_advance_the_committed_generation(self) -> None:
        body = function_body(read(SOAK), "static int soak_load_malformed(")

        self.assertIn("ROLLER_ED_SCENE_FAILED", body)
        self.assertIn("After.uiTrackGeneration != Before.uiTrackGeneration", body)
        # AD-7d: the epoch must move even on failure, or a stale fill would
        # still be accepted against a scene that no longer exists.
        self.assertIn("After.uiGeometryEpoch == Before.uiGeometryEpoch", body)
        self.assertIn("ROLLER_ED_RESULT_NO_SCENE", body)

    def test_a_successful_reload_advances_the_generation_exactly_once(self) -> None:
        body = function_body(read(SOAK), "static int soak_load_valid(")

        self.assertIn("Before.uiTrackGeneration + 1u", body)
        self.assertIn("ROLLER_ED_SCENE_READY", body)
        # Reloading one track must keep producing one track.
        self.assertIn("uiBaselineVertexCount", body)
        self.assertIn("uiBaselinePrimitiveCount", body)


class LeakDetectionTests(unittest.TestCase):
    """The three detectors are independent and catch different failures."""

    def test_renderer_texture_resources_must_return_to_a_steady_state(self) -> None:
        body = function_body(read(SOAK), "static int soak_run_phase(")

        # A still-reachable slot is not a Valgrind leak, but the tables are
        # bounded, so an editor would die of it long before memory ran out.
        self.assertIn("soak_texture_counts(", body)
        self.assertIn("soak_counts_match(", body)

    def test_live_sdl_allocations_must_be_flat_over_identical_cycles(self) -> None:
        source = read(SOAK)
        body = function_body(source, "static int soak_run_phase(")

        # The "or platform equivalent" half of the criterion: this is the only
        # detector of the three that works where Valgrind does not. SDL's own
        # counter is compiled out of the vendored build, so the hooks are ours.
        self.assertIn("SDL_SetMemoryFunctions(soak_malloc", source)
        self.assertIn("soak_live_allocations()", body)
        self.assertIn("iAllocationsAtEnd > iAllocationsAtMidpoint", body)

    def test_the_allocation_assertion_is_gated_on_the_gpu_backend(self) -> None:
        body = function_body(read(SOAK), "static int soak_run_phase(")

        # Not on the selected renderer: selecting software back does not
        # detach the GPU backend, and texture loads keep it synchronized, so a
        # software-after-GPU phase is still allocating through the driver's
        # deferred-destroy pools.
        self.assertIn(
            "if (!game_render_get_gpu(g_pGameRenderer)\n"
            "            && iAllocationsAtEnd > iAllocationsAtMidpoint) {",
            body,
        )

    def test_software_frames_are_compared_byte_for_byte(self) -> None:
        source = read(SOAK)
        body = function_body(source, "static int soak_run_phase(")

        # E1-S7 drew this line: software output is pixel-exact, GPU output is
        # not claimed to be. So the checksum comparison is gated on software.
        self.assertIn("if (bSoftware) {", body)
        self.assertIn("ullSoftwareChecksum", body)
        self.assertIn("bSoftware = eRenderer == ROLLER_ED_RENDERER_SOFTWARE", body)

    def test_bootstrap_falls_back_to_the_dummy_video_driver(self) -> None:
        source = read(SOAK)
        body = function_body(source, "int main(")

        # A hosted runner has no video device and the soak needs no window.
        # The fallback is what makes this runnable in CI at all -- and it is
        # after a real driver has been refused, so a developer machine keeps
        # the GPU phase.
        self.assertIn('SDL_HINT_VIDEO_DRIVER, "dummy"', body)
        self.assertIn("SDL_HINT_OVERRIDE", body)
        first, _, rest = body.partition("SDL_HINT_VIDEO_DRIVER")
        self.assertIn("RollerEd_Bootstrap(&BootstrapInfo)", first)
        self.assertIn("RollerEd_Bootstrap(&BootstrapInfo)", rest)

    def test_the_gpu_phase_skips_rather_than_fails_on_a_hostless_runner(self) -> None:
        body = function_body(read(SOAK), "static int SDLCALL soak_worker(")

        # Availability is a capability answer, not a promise a device can be
        # created. A hosted runner advertising GPU and refusing it must not
        # turn into a red nightly.
        self.assertIn("ROLLER_ED_RESULT_RENDERER_UNAVAILABLE", body)
        self.assertIn("ROLLER_ED_RESULT_GPU_FAILED", body)


class ResourceAccountingTests(unittest.TestCase):
    def test_the_counts_come_from_bounded_tables(self) -> None:
        software = read(ROLLER / "scene_render_software.c")
        gpu = read(ROLLER / "scene_render_gpu.c")

        sw_body = function_body(
            software, "int scene_render_sw_texture_slots_in_use("
        )
        self.assertIn("SCENE_RENDER_MAX_TEXTURE_SLOTS", sw_body)

        slots_body = function_body(
            gpu, "int scene_render_gpu_texture_slots_in_use("
        )
        self.assertIn("SCENE_GPU_MAX_TEXTURE_SLOTS", slots_body)

        # The object count is what proves a released slot released its
        # SDL_GPUTexture objects too, rather than just forgetting them.
        resident_body = function_body(gpu, "int scene_render_gpu_textures_resident(")
        self.assertIn("tileTextures", resident_body)
        self.assertIn("pairTextures", resident_body)

    def test_the_accounting_is_read_only(self) -> None:
        for path, signature in (
            (
                ROLLER / "scene_render_software.c",
                "int scene_render_sw_texture_slots_in_use(",
            ),
            (
                ROLLER / "scene_render_gpu.c",
                "int scene_render_gpu_texture_slots_in_use(",
            ),
            (
                ROLLER / "scene_render_gpu.c",
                "int scene_render_gpu_textures_resident(",
            ),
        ):
            body = function_body(read(path), signature)
            for mutation in ("free(", "SDL_Release", "memset(", "in_use ="):
                self.assertNotIn(mutation, body, f"{signature} mutates state")

    def test_the_aggregate_stays_software_only_on_wasm(self) -> None:
        body = function_body(
            read(ROLLER / "scene_render.c"), "void scene_render_get_texture_counts("
        )

        self.assertIn("#if !defined(IS_WASM)", body)
        self.assertIn("scene_render_sw_texture_slots_in_use(", body)


class BuildAndCiTests(unittest.TestCase):
    def test_the_soak_is_its_own_step_and_is_valgrind_wrapped(self) -> None:
        build = read(BUILD_ZIG)

        self.assertIn('"test-e1-s9-reload-soak"', build)
        self.assertRegex(
            build, r"runArtifact\(b,\s*editor_reload_soak_exe,\s*under_valgrind\)"
        )

    def test_the_soak_is_not_in_the_default_test_step(self) -> None:
        build = read(BUILD_ZIG)

        # It needs assets and minutes, like every other retail acceptance, so
        # `zig build test` must not depend on it.
        self.assertNotIn("test_step.dependOn(editor_reload_soak_tests)", build)

    def test_the_track_and_cycle_count_are_options(self) -> None:
        build = read(BUILD_ZIG)

        # Hosted CI has no FATDATA tree, so a hard-coded retail path would
        # have made this the one acceptance that could never run in CI.
        self.assertIn('"soak-track"', build)
        self.assertIn('"soak-cycles"', build)
        self.assertIn("assets_path.path(b, soak_track)", build)

    def test_ci_runs_the_soak_under_valgrind_on_the_schedule(self) -> None:
        workflow = read(BUILD_WORKFLOW)
        marker = "\n  soak-sanitizer:\n"
        self.assertIn(marker, workflow)
        block = workflow[workflow.index(marker) + 1 :]
        following = re.search(r"\n  [a-z][\w-]*:\n", block)
        block = block[: following.start()] if following else block

        self.assertIn("zig build test-e1-s9-reload-soak -Dvalgrind", block)
        self.assertIn("fetch_demo_assets.py", block)
        self.assertIn("if: inputs.run_soak", block)

        cycles = re.search(r"-Dsoak-cycles=(\d+)", block)
        self.assertIsNotNone(cycles)
        self.assertGreaterEqual(int(cycles.group(1)), 200)


if __name__ == "__main__":
    unittest.main()
