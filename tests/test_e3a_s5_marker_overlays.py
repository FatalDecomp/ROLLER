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


class MarkerDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_helpers.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "editor_helpers.c").read_text(encoding="utf-8")

    def test_audio_markers_read_the_loaders_trigger_speed(self) -> None:
        # AD-6a: the marker has to stand where the game triggers the sample,
        # so it reads the array the loader filled rather than a re-parse of
        # the chunk row.
        body = without_comments(
            function_body(self.source, "bool ed_helper_chunk_has_audio(")
        )
        self.assertIn("samplespeed[", body)
        self.assertIn("helper_chunk_valid", body)

    def test_stunt_markers_read_the_loaded_ramps(self) -> None:
        count = without_comments(
            function_body(self.source, "uint32_t ed_helper_stunt_count(")
        )
        chunk = without_comments(
            function_body(self.source, "bool ed_helper_stunt_chunk(")
        )
        self.assertIn("totalramps", count)
        self.assertIn("ramp[", chunk)
        self.assertIn("iGeometryIdx", chunk)

    def test_the_ramp_table_is_never_read_past_its_bounds(self) -> None:
        # totalramps is a loader-driven count and ramp[] is a fixed array, so
        # the count is clamped and every slot is null- and range-checked.
        count = without_comments(
            function_body(self.source, "uint32_t ed_helper_stunt_count(")
        )
        chunk = without_comments(
            function_body(self.source, "bool ed_helper_stunt_chunk(")
        )
        self.assertIn("sizeof(ramp)", count)
        self.assertIn("ed_helper_stunt_count()", chunk)
        self.assertIn("!pStunt", chunk)
        self.assertIn("helper_chunk_valid", chunk)

    def test_markers_never_reach_the_canonical_emitter(self) -> None:
        # AD-6d: markers are editor furniture, so no exporter may see them and
        # they carry no chunk identity into the emitter.
        combined = self.header + self.source
        self.assertNotIn("ed_emit_surface", combined)
        self.assertNotIn("tEdSurfaceEmission", combined)

    def test_a_marker_is_two_quads_because_that_is_all_there_is(self) -> None:
        self.assertIn("#define ED_HELPER_MARKER_QUAD_COUNT 2u", self.header)
        body = without_comments(
            function_body(self.source, "bool ed_helper_marker_quad(")
        )
        self.assertIn("ED_HELPER_MARKER_QUAD_COUNT", body)
        self.assertIn("ED_HELPER_MARKER_AUDIO", body)
        self.assertIn("ED_HELPER_MARKER_STUNT", body)

    def test_marker_size_scales_with_the_chunk(self) -> None:
        # Same reason the E3A-S2 wireframe and the E3A-S4 ribbons scale: the
        # legacy absolute sizes only read right at the retail track scale.
        self.assertIn("ED_HELPER_MARKER_SIZE_RATIO", self.header)
        self.assertIn("ED_HELPER_MARKER_HOVER_RATIO", self.header)
        body = function_body(self.source, "bool ed_helper_marker_quad(")
        self.assertIn("ed_helper_road_width(uiChunkId)", body)

    def test_the_marker_frame_comes_from_the_cross_section(self) -> None:
        body = without_comments(
            function_body(self.source, "static bool helper_marker_frame(")
        )
        self.assertIn("ED_HELPER_POINT_LEFT_LANE", body)
        self.assertIn("ED_HELPER_POINT_RIGHT_LANE", body)
        self.assertIn("helper_cross", body)
        self.assertIn("ED_SURFACE_WORLD_UP_AXIS", body)


class MarkerRenderingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

    def test_each_marker_has_its_own_flag(self) -> None:
        body = without_comments(
            function_body(self.draw, "void drawtrk3_editor_draw_helpers(")
        )
        self.assertIn("ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS", body)
        self.assertIn("ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS", body)

    def test_markers_use_the_legacy_editor_colours(self) -> None:
        self.assertIn(
            "#define ED_AUDIO_MARKER_PALETTE_COLOUR 0x8Fu", self.draw
        )
        self.assertIn(
            "#define ED_STUNT_MARKER_PALETTE_COLOUR 0xFFu", self.draw
        )

    def test_a_marker_is_drawn_from_both_sides(self) -> None:
        # A flat icon across the track is only face-on from one end of it;
        # legacy emitted every marker triangle twice with reversed indices.
        body = without_comments(
            function_body(self.draw, "static void draw_helper_marker(")
        )
        self.assertEqual(body.count("draw_helper_quad("), 2)
        self.assertIn("afReversed", body)
        self.assertIn("afQuad[3u - uiVertex]", body)

    def test_markers_draw_after_the_lines_they_sit_over(self) -> None:
        body = function_body(
            self.draw, "void drawtrk3_editor_draw_helpers("
        )
        self.assertLess(
            body.index("ROLLER_ED_OVERLAY_SHOW_CENTER_LINE"),
            body.index("ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS"),
        )

    def test_only_triggered_chunks_and_loaded_ramps_draw(self) -> None:
        body = without_comments(
            function_body(self.draw, "void drawtrk3_editor_draw_helpers(")
        )
        self.assertIn("ed_helper_chunk_has_audio(", body)
        self.assertIn("ed_helper_stunt_count()", body)
        self.assertIn("ed_helper_stunt_chunk(", body)


class GameBuildTests(unittest.TestCase):
    def test_the_marker_pass_stays_out_of_the_game(self) -> None:
        # drawtrk3_editor_draw_helpers is reached only from
        # editor_legacy_scene.c's render path, which the game never enters.
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        self.assertEqual(
            draw.count("drawtrk3_editor_draw_helpers("), 1
        )
        self.assertEqual(
            scene.count("drawtrk3_editor_draw_helpers(g_pGameRenderer);"), 2
        )


if __name__ == "__main__":
    unittest.main()
