import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
TESTS = ROOT / "tests"

OVERLAY_FLAGS = (
    "ROLLER_ED_OVERLAY_SHOW_SURFACES",
    "ROLLER_ED_OVERLAY_SHOW_WIREFRAME",
    "ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION",
    "ROLLER_ED_OVERLAY_SHOW_AI_LINES",
    "ROLLER_ED_OVERLAY_SHOW_CENTER_LINE",
    "ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS",
    "ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS",
    "ROLLER_ED_OVERLAY_SHOW_TEST_CAR",
    "ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH",
    "ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS",
    "ROLLER_ED_OVERLAY_DETACH_LAST",
)


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


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


class FacadeEntryPointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.api = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        cls.body = without_comments(
            function_body(
                cls.api,
                "eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(",
            )
        )

    def test_overlay_state_is_stored_instead_of_refused(self) -> None:
        self.assertNotIn("ROLLER_ED_RESULT_UNSUPPORTED", self.body)
        self.assertNotIn("not implemented yet", self.body)
        self.assertIn("roller_ed_legacy_scene_set_overlay_state(", self.body)

    def test_size_and_version_are_validated_before_the_payload(self) -> None:
        # AD-12: the header fields are read first, so a caller that sent a
        # smaller or older struct is never read past.
        validate = self.body.index("roller_ed_validate_struct(")
        self.assertIn('"tEdOverlayState"', self.body)
        self.assertLess(self.body.index("!pState"), validate)
        self.assertLess(validate, self.body.index("uiFlags"))
        self.assertLess(
            validate, self.body.index("roller_ed_legacy_scene_set_overlay_state(")
        )

    def test_flags_outside_this_api_version_are_refused(self) -> None:
        self.assertIn("ROLLER_ED_OVERLAY_KNOWN_FLAGS", self.body)
        self.assertIn("ROLLER_ED_RESULT_INVALID_ARGUMENT", self.body)
        forward = self.body.index("roller_ed_legacy_scene_set_overlay_state(")
        self.assertLess(self.body.index("ROLLER_ED_OVERLAY_KNOWN_FLAGS"), forward)

    def test_the_call_runs_on_the_render_worker(self) -> None:
        self.assertIn("roller_ed_require_worker()", self.body)

    def test_overlay_changes_do_not_disturb_geometry_state(self) -> None:
        # AD-7d: an overlay toggle that advanced the epoch would throw away
        # E4A-S5's per-epoch extraction on every menu click.
        self.assertNotIn("roller_ed_advance_geometry_epoch", self.body)
        self.assertNotIn("roller_ed_advance_track_generation", self.body)
        self.assertNotIn("roller_ed_release_geometry_cache", self.body)
        self.assertNotIn("s_eSceneState =", self.body)


class OverlayModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_overlay.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "editor_overlay.c").read_text(encoding="utf-8")

    def test_the_module_owns_the_state_and_its_queries(self) -> None:
        for symbol in (
            "roller_ed_overlay_reset",
            "roller_ed_overlay_set",
            "roller_ed_overlay_get",
            "roller_ed_overlay_flags",
            "roller_ed_overlay_enabled",
            "roller_ed_overlay_selection_range",
            "roller_ed_overlay_track_segment_visible",
        ):
            self.assertIn(symbol, self.header)
            self.assertIn(symbol, self.source)

    def test_every_public_flag_is_in_the_known_mask(self) -> None:
        mask = self.header[self.header.index("ROLLER_ED_OVERLAY_KNOWN_FLAGS") :]
        mask = mask[: mask.index("#define ROLLER_ED_OVERLAY_DEFAULT_FLAGS")]
        for flag in OVERLAY_FLAGS:
            self.assertIn(flag, mask)

    def test_defaults_reproduce_the_track_only_view(self) -> None:
        self.assertIn(
            "#define ROLLER_ED_OVERLAY_DEFAULT_FLAGS "
            "ROLLER_ED_OVERLAY_SHOW_SURFACES",
            self.header,
        )
        body = without_comments(
            function_body(self.source, "void roller_ed_overlay_reset(")
        )
        self.assertIn("ROLLER_ED_OVERLAY_DEFAULT_FLAGS", body)
        self.assertIn("ROLLER_ED_INVALID_CHUNK_ID", body)

    def test_the_state_module_stays_out_of_the_render_graph(self) -> None:
        includes = re.findall(r'#include\s+"([^"]+)"', self.source)
        self.assertEqual(includes, ["editor_overlay.h"])
        header_includes = re.findall(r'#include\s+"([^"]+)"', self.header)
        self.assertEqual(header_includes, ["editor_api.h"])

    def test_a_reversed_selection_range_is_ordered_for_consumers(self) -> None:
        body = without_comments(
            function_body(self.source, "bool roller_ed_overlay_selection_range(")
        )
        self.assertIn("ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION", body)
        self.assertIn("ROLLER_ED_INVALID_CHUNK_ID", body)
        self.assertIn("uiFirstChunk > uiLastChunk", body)


class AttachLastRenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

    def test_the_closing_track_surface_and_helper_ribbons_share_the_filter(self) -> None:
        surface = function_body(
            self.draw, "static bool emit_track_chunk_surface_to_renderer("
        )
        helpers = function_body(
            self.draw, "void drawtrk3_editor_draw_helpers("
        )
        helper_line = function_body(self.draw, "static void draw_helper_line(")
        self.assertIn("roller_ed_overlay_track_segment_visible(", surface)
        self.assertIn("ROLLER_ED_OVERLAY_DETACH_LAST", helpers)
        self.assertIn("bAttachLast", helper_line)
        self.assertIn("iChunk == TRAK_LEN - 1", helper_line)

    def test_canonical_export_geometry_remains_closed(self) -> None:
        canonical = function_body(self.draw, "bool drawtrk3_emit_full_track(")
        self.assertNotIn("roller_ed_overlay", canonical)
        self.assertIn("ed_traverse_full_track_chunks(", canonical)


class SeamAndLifecycleTests(unittest.TestCase):
    def test_the_seam_is_declared_next_to_the_camera_one(self) -> None:
        header = (ROLLER / "editor_legacy_scene.h").read_text(encoding="utf-8")
        self.assertIn("roller_ed_legacy_scene_set_overlay_state(", header)
        self.assertIn("const tEdOverlayState *pState", header)

    def test_the_adapter_forwards_into_the_state_module(self) -> None:
        source = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "eRollerEdResult roller_ed_legacy_scene_set_overlay_state(")
        )
        self.assertIn("roller_ed_overlay_set(pState);", body)
        self.assertIn("ROLLER_ED_RESULT_INVALID_ARGUMENT", body)
        self.assertNotIn("epoch", body)

    def test_worker_shutdown_resets_overlay_state(self) -> None:
        source = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        body = function_body(source, "void roller_ed_legacy_scene_shutdown(")
        self.assertIn("roller_ed_overlay_reset();", body)


class BuildRegistrationTests(unittest.TestCase):
    def test_the_translation_unit_is_registered_everywhere(self) -> None:
        manifest = (ROOT / "roller-core.srclist").read_text(encoding="utf-8")
        self.assertIn("KEEP|PROJECTS/ROLLER/editor_overlay.c", manifest)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("PROJECTS/ROLLER/editor_overlay.c", cmake)
        build_zig = (ROOT / "build.zig").read_text(encoding="utf-8")
        self.assertIn('"PROJECTS/ROLLER/editor_overlay.c"', build_zig)

    def test_the_native_tests_run_in_the_editor_api_step(self) -> None:
        build_zig = (ROOT / "build.zig").read_text(encoding="utf-8")
        self.assertIn('"tests/editor_overlay_test.c"', build_zig)
        self.assertIn(
            "editor_api_tests.dependOn(&run_editor_overlay.step);", build_zig
        )

    def test_the_lifecycle_seam_covers_the_new_entry_point(self) -> None:
        lifecycle = (TESTS / "editor_api_lifecycle_test.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "eRollerEdResult roller_ed_legacy_scene_set_overlay_state(", lifecycle
        )
        self.assertIn("RollerEd_SetOverlayState(&Overlay)", lifecycle)
        self.assertIn("RollerEd_SetOverlayState(NULL)", lifecycle)
        self.assertIn("ROLLER_ED_RESULT_INVALID_VERSION", lifecycle)


if __name__ == "__main__":
    unittest.main()
