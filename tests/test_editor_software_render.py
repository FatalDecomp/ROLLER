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


class EditorSoftwareRenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.software = (SOURCES / "game_render_software.c").read_text(
            encoding="utf-8"
        )
        cls.adapter = (SOURCES / "editor_legacy_scene.c").read_text(
            encoding="utf-8"
        )
        cls.facade = (SOURCES / "editor_api.c").read_text(encoding="utf-8")

    def test_readback_finishes_without_window_presentation(self) -> None:
        readback = extract_function(
            self.software, "game_render_sw_end_frame_readback"
        )
        windowed = extract_function(self.software, "game_render_sw_end_frame")
        self.assertIn("game_render_sw_finish_frame(sw)", readback)
        self.assertNotIn("UpdateSDLWindow", readback)
        self.assertIn("UpdateSDLWindow", windowed)

    def test_rgba_conversion_and_letterbox_contract_are_explicit(self) -> None:
        readback = extract_function(
            self.software, "game_render_sw_end_frame_readback"
        )
        self.assertIn("pColour->byR * 255u / 63u", readback)
        self.assertIn("pColour->byG * 255u / 63u", readback)
        self.assertIn("pColour->byB * 255u / 63u", readback)
        self.assertRegex(readback, r"pbyRow\[uiX \* 4u \+ 3u\] = 255u")
        self.assertIn("uiOffsetX = (uiRGBAWidth - uiScaledWidth) / 2u", readback)
        self.assertIn("uiOffsetY = (uiRGBAHeight - uiScaledHeight) / 2u", readback)
        self.assertIn("uiX * uiNativeWidth / uiScaledWidth", readback)
        self.assertIn("uiY * uiNativeHeight / uiScaledHeight", readback)

    def test_adapter_selects_native_software_path_before_gpu_readback(self) -> None:
        render = extract_function(self.adapter, "roller_ed_legacy_scene_render")
        software_branch = render.index(
            "s_eActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE"
        )
        gpu_readback = render.index("scene_render_gpu_end_frame_readback")
        self.assertLess(software_branch, gpu_readback)
        self.assertIn("uiNativeWidth = XMAX", render)
        self.assertIn("uiNativeHeight = YMAX", render)
        self.assertIn("game_render_end_frame_software_readback", render)

    def test_editor_builds_software_transparency_shades_after_palette_load(self) -> None:
        install = extract_function(
            self.adapter, "roller_ed_legacy_scene_install"
        )
        self.assertLess(
            install.index("loadtrack_from_stage_with_assets_editor_ex"),
            install.index("FindShades()"),
        )
        self.assertLess(
            install.index("FindShades()"),
            install.index("editor_scene_prepare_clouds()"),
        )

    def test_init_preference_and_fallback_reach_scene_install(self) -> None:
        load = extract_function(self.facade, "roller_ed_load_track_file")
        ensure = extract_function(self.adapter, "editor_scene_ensure_renderer")
        self.assertRegex(
            load,
            r"roller_ed_legacy_scene_install\([\s\S]*?"
            r"s_ePreferredRenderer, s_uiAllowSoftwareFallback",
        )
        self.assertIn(
            "ePreferredRenderer == ROLLER_ED_RENDERER_SOFTWARE", ensure
        )
        self.assertIn("!uiAllowSoftwareFallback", ensure)
        self.assertIn("editor_scene_create_software_renderer", ensure)


if __name__ == "__main__":
    unittest.main()
