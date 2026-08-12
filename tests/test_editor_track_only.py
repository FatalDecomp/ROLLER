from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SOURCES = REPOSITORY_ROOT / "PROJECTS" / "ROLLER"


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"^[\w\s\*]+\b{name}\s*\([^)]*\)\s*\{{", source, re.M)
    if not match:
        raise AssertionError(f"function {name} not found")
    brace = source.find("{", match.end() - 1)
    depth = 0
    for position in range(brace, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : position + 1]
    raise AssertionError(f"function {name} body not closed")


class EditorTrackOnlyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.loader = (SOURCES / "loadtrak.c").read_text(encoding="utf-8")
        cls.adapter = (SOURCES / "editor_legacy_scene.c").read_text(
            encoding="utf-8"
        )
        cls.game = (SOURCES / "3d.c").read_text(encoding="utf-8")
        cls.track_draw = (SOURCES / "drawtrk3.c").read_text(encoding="utf-8")

    def test_facade_selects_dedicated_track_only_install(self) -> None:
        install = extract_function(self.adapter, "roller_ed_legacy_scene_install")
        self.assertIn("loadtrack_from_stage_with_assets_editor_ex", install)

        normal = extract_function(
            self.loader, "loadtrack_from_stage_with_assets_ex"
        )
        editor = extract_function(
            self.loader, "loadtrack_from_stage_with_assets_editor_ex"
        )
        self.assertRegex(normal, r"iPreviewMode,\s*0,\s*szError")
        self.assertRegex(editor, r"iPreviewMode,\s*-1,\s*szError")

    def test_track_only_zeroes_cars_before_initialization(self) -> None:
        internal = extract_function(self.loader, "loadtrack_internal")
        zero_cars = internal.index("numcars = 0")
        first_car_loop = internal.index("if (numcars > 0)")
        place_cars = internal.index("placecars()")
        self.assertLess(zero_cars, first_car_loop)
        self.assertLess(zero_cars, place_cars)
        self.assertRegex(internal, r"if \(!bEditorTrackOnly\)\s+placecars\(\)")
        self.assertRegex(
            internal,
            r"if \(!bEditorTrackOnly\) \{[\s\S]*?initcarview\(ViewType\[0\], 0\)",
        )

    def test_explicit_editor_camera_precedes_car_derived_view(self) -> None:
        draw_road = extract_function(self.game, "draw_road")
        editor_camera = draw_road.index(
            "!roller_ed_track_only_active() || !roller_ed_camera_apply()"
        )
        legacy_camera = draw_road.index("calculateview(")
        self.assertLess(editor_camera, legacy_camera)
        self.assertRegex(
            draw_road,
            r"if \(!roller_ed_track_only_active\(\)\) \{[\s\S]*?DrawCars\(",
        )

    def test_editor_visibility_uses_camera_across_the_full_track(self) -> None:
        visibility = extract_function(self.track_draw, "CalcVisibleTrackEditor")
        self.assertIn("iChunk < TRAK_LEN", visibility)
        self.assertIn("localdata[iChunk].pointAy[3]", visibility)
        self.assertIn("viewx", visibility)
        self.assertIn("viewy", visibility)
        self.assertIn("viewz", visibility)
        self.assertNotIn("Car[", visibility)
        self.assertNotIn("numcars", visibility)
        self.assertIn("g_fDrawDistanceFraction", visibility)
        self.assertIn("TrakView[iCurrChunk].byForwardMainChunks", visibility)
        self.assertIn("TrakView[iCurrChunk].byBackwardMainChunks", visibility)
        self.assertIn("(TRAK_LEN - 1) - TrackSize", visibility)
        self.assertIn("first_size = TrackSize", visibility)
        self.assertIn("gap_size = 6 * TRAK_LEN", visibility)

        draw_road = extract_function(self.game, "draw_road")
        self.assertRegex(
            draw_road,
            r"if \(roller_ed_track_only_active\(\)\)\s+"
            r"iRenderChunkIdx = CalcVisibleTrackEditor\(uiVisibilityViewMode\)",
        )
        self.assertIn(".renderChunkIdx = iRenderChunkIdx", draw_road)

    def test_cars_start_cubes_and_hud_are_absent(self) -> None:
        self.assertRegex(
            self.track_draw,
            r"if \(!roller_ed_track_only_active\(\)\s*&& countdown > -72",
        )
        render = extract_function(self.adapter, "roller_ed_legacy_scene_render")
        self.assertIn("draw_road", render)
        for gameplay_overlay in (
            "PrintPanel",
            "display_messages",
            "display_frame_rate",
            "game_copypic",
        ):
            self.assertNotIn(gameplay_overlay, render)


if __name__ == "__main__":
    unittest.main()
