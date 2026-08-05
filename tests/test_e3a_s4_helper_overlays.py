import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"


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


class HelperGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_helpers.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "editor_helpers.c").read_text(encoding="utf-8")

    def test_helpers_are_derived_from_the_loaded_legacy_arrays(self) -> None:
        # AD-6a: geometry authority is ROLLER. The lines must come from the
        # same arrays the game drives on, not from re-derived scalar rows.
        self.assertIn("TrakPt[", self.source)
        self.assertIn("localdata[", self.source)
        self.assertIn("fAILine1", self.source)

    def test_helpers_never_reach_the_canonical_emitter(self) -> None:
        # AD-6d: editor furniture is not track content, so no exporter may
        # ever see it.
        combined = self.header + self.source
        self.assertNotIn("ed_emit_surface", combined)
        self.assertNotIn("tEdSurfaceEmission", combined)
        self.assertNotIn("uiChunkId, uiChunkId", combined)

    def test_an_ai_line_follows_the_real_cross_section(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_helper_ai_line_point(")
        )
        # Across the road first, then up the shoulder chord, so banking and
        # shoulder slope come from the geometry rather than from trigonometry.
        self.assertIn("ED_HELPER_POINT_LEFT_LANE", body)
        self.assertIn("ED_HELPER_POINT_LEFT_OUTER", body)
        self.assertIn("ED_HELPER_POINT_RIGHT_LANE", body)
        self.assertIn("ED_HELPER_POINT_RIGHT_OUTER", body)
        self.assertIn("fOffset > 0.0f", body)

    def test_every_chunk_and_line_index_is_bounds_checked(self) -> None:
        guard = without_comments(
            function_body(self.source, "static bool helper_chunk_valid(")
        )
        self.assertIn("TRAK_LEN", guard)
        for name in (
            "bool ed_helper_center_point(",
            "bool ed_helper_ai_line_point(",
            "bool ed_helper_environment_floor_quad(",
        ):
            body = function_body(self.source, name)
            self.assertIn("helper_chunk_valid", body)

    def test_the_floor_uses_the_height_the_loader_kept(self) -> None:
        body = function_body(
            self.source, "bool ed_helper_environment_floor_quad("
        )
        self.assertIn("TrackFloorHeight", body)
        self.assertIn("ED_SURFACE_WORLD_UP_AXIS", body)


class LoaderTests(unittest.TestCase):
    def test_the_header_floor_height_is_retained(self) -> None:
        source = (ROLLER / "loadtrak.c").read_text(encoding="utf-8")
        header = (ROLLER / "loadtrak.h").read_text(encoding="utf-8")

        # The value was already parsed as the origin's up component and then
        # discarded; the editor has always called it the floor depth.
        self.assertIn("TrackFloorHeight = (float)dWallCalc1;", source)
        self.assertIn("extern float TrackFloorHeight;", header)


class HelperRenderingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

    def test_each_helper_has_its_own_flag(self) -> None:
        body = without_comments(
            function_body(self.draw, "void drawtrk3_editor_draw_helpers(")
        )
        for flag in (
            "ROLLER_ED_OVERLAY_SHOW_ENVIRONMENT_FLOOR",
            "ROLLER_ED_OVERLAY_SHOW_AI_LINES",
            "ROLLER_ED_OVERLAY_SHOW_CENTER_LINE",
        ):
            self.assertIn(flag, body)
        self.assertEqual(body.count("roller_ed_overlay_enabled("), 3)

    def test_all_four_ai_lines_are_drawn(self) -> None:
        body = function_body(self.draw, "void drawtrk3_editor_draw_helpers(")
        self.assertIn("ED_HELPER_AI_LINE_COUNT", body)

    def test_the_helper_pass_runs_after_the_scene(self) -> None:
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        # Twice: the software and GPU render paths.
        self.assertEqual(
            scene.count("drawtrk3_editor_draw_helpers(g_pGameRenderer);"), 2
        )
        for body in (
            scene[scene.index("if (s_eActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE)") :],
        ):
            self.assertLess(
                body.index("draw_road("),
                body.index("drawtrk3_editor_draw_helpers("),
            )


class BuildRegistrationTests(unittest.TestCase):
    def test_the_translation_unit_is_registered_everywhere(self) -> None:
        manifest = (ROOT / "roller-core.srclist").read_text(encoding="utf-8")
        self.assertIn("KEEP|PROJECTS/ROLLER/editor_helpers.c", manifest)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("PROJECTS/ROLLER/editor_helpers.c", cmake)
        build_zig = (ROOT / "build.zig").read_text(encoding="utf-8")
        self.assertIn('"PROJECTS/ROLLER/editor_helpers.c"', build_zig)
        self.assertIn('"tests/editor_helpers_test.c"', build_zig)
        self.assertIn(
            "editor_api_tests.dependOn(&run_editor_helpers.step);", build_zig
        )


if __name__ == "__main__":
    unittest.main()
