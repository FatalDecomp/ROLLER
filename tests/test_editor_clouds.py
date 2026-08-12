"""Editor cloud lifecycle contract.

The renderer already owns the horizon/cloud draw. This test pins the missing
editor-only setup: resolve and load GENTEX.DRH, initialize the cloud dome once,
and keep a missing optional bank from turning a valid track into a failed load.
"""

from pathlib import Path
import unittest


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
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class EditorCloudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.adapter = (ROLLER / "editor_legacy_scene.c").read_text(
            encoding="utf-8"
        )
        cls.graphics = (ROLLER / "graphics.c").read_text(encoding="utf-8")
        cls.graphics_header = (ROLLER / "graphics.h").read_text(encoding="utf-8")
        cls.soak = (ROOT / "tests" / "editor_reload_soak_acceptance.c").read_text(
            encoding="utf-8"
        )

    def test_editor_prepares_the_existing_cloud_renderer_dependencies(self) -> None:
        prepare = function_body(
            self.adapter, "static void editor_scene_prepare_clouds(void)"
        )
        self.assertIn("loadtrack_resolve_editor_asset(gencartex_name", prepare)
        self.assertIn("LoadGenericCarTexturesFromFile(szResolved)", prepare)
        self.assertIn("game_render_get_texture_handle(g_pGameRenderer, 18)", prepare)
        self.assertIn("initclouds();", prepare)
        self.assertIn("textures_off &= ~TEX_OFF_CLOUDS", prepare)

    def test_cloud_placement_is_stable_across_edit_reloads(self) -> None:
        prepare = function_body(
            self.adapter, "static void editor_scene_prepare_clouds(void)"
        )
        attempted = prepare.index("if (s_bEditorCloudsAttempted)")
        initialize = prepare.index("initclouds();")
        self.assertLess(attempted, initialize)
        install = function_body(
            self.adapter, "eRollerEdResult roller_ed_legacy_scene_install("
        )
        self.assertEqual(install.count("editor_scene_prepare_clouds();"), 1)

    def test_missing_optional_cloud_assets_do_not_fail_track_install(self) -> None:
        prepare = function_body(
            self.adapter, "static void editor_scene_prepare_clouds(void)"
        )
        missing = prepare.index("!loadtrack_resolve_editor_asset")
        disabled = prepare.index("textures_off |= TEX_OFF_CLOUDS", missing)
        returned = prepare.index("return;", disabled)
        self.assertLess(missing, disabled)
        self.assertLess(disabled, returned)

        install = function_body(
            self.adapter, "eRollerEdResult roller_ed_legacy_scene_install("
        )
        prepare_call = install.index("editor_scene_prepare_clouds();")
        self.assertIn("return eResult;", install[prepare_call:])

    def test_game_wrapper_keeps_its_original_generic_texture_path(self) -> None:
        self.assertIn(
            "void LoadGenericCarTexturesFromFile(const char *szTextureFile);",
            self.graphics_header,
        )
        wrapper = function_body(self.graphics, "void LoadGenericCarTextures()")
        self.assertIn("LoadGenericCarTexturesFromFile(gencartex_name);", wrapper)

    def test_reload_soak_checks_texture_and_all_cloud_placements(self) -> None:
        check = function_body(
            self.soak, "static int soak_clouds_ready("
        )
        self.assertIn("num_textures[18] < 13", check)
        self.assertIn("game_render_get_texture_handle(g_pGameRenderer, 18)", check)
        self.assertIn("for (int iCloud = 0; iCloud < 40; ++iCloud)", check)
        self.assertIn("cloud[iCloud].fRadius", check)


if __name__ == "__main__":
    unittest.main()
