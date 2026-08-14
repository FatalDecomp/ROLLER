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


class TowerMarkerFacadeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.api = (ROLLER / "editor_api.h").read_text(encoding="ascii")
        cls.overlay_h = (ROLLER / "editor_overlay.h").read_text(
            encoding="ascii"
        )
        cls.overlay_c = (ROLLER / "editor_overlay.c").read_text(
            encoding="ascii"
        )

    def test_bit_12_is_public_and_accepted_without_a_layout_bump(self) -> None:
        self.assertRegex(
            self.api,
            r"ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS\s*=\s*1u\s*<<\s*12",
        )
        self.assertIn(
            "ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS",
            self.overlay_h[
                self.overlay_h.index("ROLLER_ED_OVERLAY_KNOWN_FLAGS") :
                self.overlay_h.index("ROLLER_ED_OVERLAY_DEFAULT_FLAGS")
            ],
        )
        self.assertIn("ROLLER_ED_OVERLAY_STATE_VERSION 3u", self.api)
        self.assertIn("sizeof(tEdOverlayState) == 36u", self.api)

    def test_marker_visibility_ignores_surface_master_and_class_mask(self) -> None:
        body = without_comments(
            function_body(
                self.overlay_c,
                "bool roller_ed_overlay_surface_class_visible(",
            )
        )
        tower = body.index("ROLLER_ED_SURFACE_CLASS_TOWER")
        marker = body.index("ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS")
        surfaces = body.index("ROLLER_ED_OVERLAY_SHOW_SURFACES")
        mask = body.index("overlay_class_selected")
        self.assertLess(tower, marker)
        self.assertLess(marker, surfaces)
        self.assertLess(marker, mask)


class SharedTowerBillboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tower_h = (ROLLER / "tower.h").read_text(encoding="ascii")
        cls.tower_c = (ROLLER / "tower.c").read_text(encoding="ascii")
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

    def test_drawtower_delegates_its_added_body_to_the_shared_seam(self) -> None:
        self.assertIn(
            "void tower_emit_marker(int iTowerIdx, float fScale);",
            self.tower_h,
        )
        legacy = without_comments(
            function_body(self.tower_c, "void DrawTower(")
        )
        for gate in ("NearTow", "iEnabled > -1", "fOriginalZ", "xp", "yp"):
            self.assertIn(gate, legacy)
        self.assertIn("tower_emit_marker(iTowerIdx, 1.0f);", legacy)
        self.assertNotIn("drawtrk3_emit_surface_to_renderer", legacy)

    def test_the_seam_is_the_original_camera_facing_surface(self) -> None:
        seam = without_comments(
            function_body(self.tower_c, "void tower_emit_marker(")
        )
        for value in (
            "TowerX[iTowerIdx]",
            "TowerY[iTowerIdx]",
            "TowerZ[iTowerIdx]",
            "vk1",
            "vk2",
            "vk4",
            "vk5",
            "vk7",
            "vk8",
            "scr_size",
            "VIEWDIST",
            "fScale",
            "SURFACE_FLAG_FLIP_BACKFACE | 0xE7",
            "ROLLER_ED_SURFACE_CLASS_TOWER",
            "ROLLER_ED_CONTENT_RUNTIME_SCENERY",
            "drawtrk3_emit_surface_to_renderer",
        ):
            self.assertIn(value, seam)
        self.assertIn("TowerBase[iTowerIdx].iChunkIdx", seam)
        self.assertNotIn("NearTow", seam)
        self.assertNotIn("iEnabled", seam)

    def test_editor_draws_every_tower_after_existing_markers(self) -> None:
        helpers = without_comments(
            function_body(self.draw, "void drawtrk3_editor_draw_helpers(")
        )
        tower_flag = helpers.index("ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS")
        self.assertLess(
            helpers.index("ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS"), tower_flag
        )
        self.assertIn("iTowerIdx < NumTowers", helpers[tower_flag:])
        self.assertIn("tower_emit_marker(iTowerIdx, 1.0f)", helpers[tower_flag:])
        self.assertNotIn("DrawTower(", helpers)

    def test_game_dispatch_remains_dead_and_canonical_export_ignores_markers(
        self,
    ) -> None:
        self.assertNotIn("render_queue_3d_add_tower", self.draw)
        canonical = without_comments(
            function_body(self.draw, "bool drawtrk3_emit_full_track(")
        )
        self.assertNotIn("DrawTower(", canonical)
        self.assertNotIn("tower_emit_marker(", canonical)
        self.assertEqual(
            self.tower_c.count("drawtrk3_emit_surface_to_renderer("), 1
        )


class TowerMarkerAcceptanceTests(unittest.TestCase):
    def test_native_seam_fixture_covers_every_mode_and_zoom(self) -> None:
        native = (ROOT / "tests" / "tower_marker_test.c").read_text(
            encoding="ascii"
        )
        self.assertIn("const uint32_t uiFixtureTowers = 25u", native)
        self.assertIn("{ -1, -4, -2, -5, -3 }", native)
        self.assertIn("i / 5u", native)
        self.assertIn("tower_emit_marker((int)i, 1.0f)", native)
        self.assertIn("fCentreX", native)
        self.assertIn("fCentreY", native)
        self.assertIn("fCentreZ", native)
        self.assertIn("ROLLER_ED_SURFACE_CLASS_TOWER", native)
        self.assertIn("ROLLER_ED_CONTENT_RUNTIME_SCENERY", native)

        soak = (ROOT / "tests" / "editor_reload_soak_acceptance.c").read_text(
            encoding="ascii"
        )
        self.assertIn("static const int aiModeNibbles[] = { 0, 1, 3, 4, 5 }", soak)
        self.assertIn("static const int aiEnabledModes[] = { -1, -4, -2, -5, -3 }", soak)
        self.assertIn("return (iChunk % 25) / 5", soak)

    def test_retail_renderer_aims_at_every_queried_position(self) -> None:
        acceptance = (
            ROOT / "tests" / "editor_overlay_toggle_acceptance.c"
        ).read_text(encoding="ascii")
        self.assertIn("RollerEd_QueryTowerCount(&uiTowerCount)", acceptance)
        self.assertIn("RollerEd_QueryTower(uiTower, &Info)", acceptance)
        self.assertIn("pTower->fWorldPosition[0]", acceptance)
        self.assertIn("pTower->fWorldPosition[1]", acceptance)
        self.assertIn("pTower->fWorldPosition[2]", acceptance)
        self.assertIn("ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS, 0u, 0u", acceptance)
        self.assertIn("ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION", acceptance)

    def test_overlay_changes_remain_view_only(self) -> None:
        api = (ROLLER / "editor_api.c").read_text(encoding="ascii")
        body = without_comments(
            function_body(
                api,
                "eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(",
            )
        )
        self.assertNotIn("roller_ed_advance_geometry_epoch", body)
        self.assertNotIn("roller_ed_release_geometry_cache", body)


if __name__ == "__main__":
    unittest.main()
