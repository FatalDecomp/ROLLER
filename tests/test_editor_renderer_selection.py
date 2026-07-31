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


class EditorRendererSelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.facade = (SOURCES / "editor_api.c").read_text(encoding="utf-8")
        cls.adapter = (SOURCES / "editor_legacy_scene.c").read_text(
            encoding="utf-8"
        )
        cls.game = (SOURCES / "game_render.c").read_text(encoding="utf-8")
        cls.scene = (SOURCES / "scene_render.c").read_text(encoding="utf-8")

    def test_facade_reports_adapter_mask_and_commits_only_success(self) -> None:
        available = extract_function(self.facade, "RollerEd_GetAvailableRenderers")
        select = extract_function(self.facade, "RollerEd_SelectRenderer")
        self.assertIn("roller_ed_legacy_scene_get_available_renderers()", available)
        delegate = select.index("roller_ed_legacy_scene_select_renderer")
        success = select.index("eResult == ROLLER_ED_RESULT_OK", delegate)
        commit = select.index("s_ePreferredRenderer = eKind", success)
        self.assertLess(delegate, success)
        self.assertLess(success, commit)

    def test_availability_always_has_software_and_probes_gpu_support(self) -> None:
        available = extract_function(
            self.adapter, "roller_ed_legacy_scene_get_available_renderers"
        )
        self.assertIn("uiAvailable = ROLLER_ED_RENDERER_SOFTWARE", available)
        self.assertIn("SDL_GPUSupportsShaderFormats", available)
        self.assertIn("uiAvailable |= ROLLER_ED_RENDERER_GPU", available)

    def test_scene_gpu_attachment_is_transactional(self) -> None:
        attach = extract_function(self.scene, "scene_render_attach_gpu_device")
        create = attach.index("candidate = scene_render_gpu_create")
        populate = attach.index("scene_render_sw_for_each_texture", create)
        failure = attach.index("if (!attach.success)", populate)
        commit = attach.index("renderer->gpu = candidate", failure)
        self.assertLess(create, populate)
        self.assertLess(populate, failure)
        self.assertLess(failure, commit)
        self.assertIn("scene_render_gpu_destroy(candidate)", attach[failure:commit])

    def test_failed_gpu_switch_keeps_software_active(self) -> None:
        select = extract_function(
            self.adapter, "roller_ed_legacy_scene_select_renderer"
        )
        failed_attach = select.index("!game_render_attach_gpu_device")
        destroy_candidate = select.index(
            "SDL_DestroyGPUDevice(pCandidateDevice)", failed_attach
        )
        gpu_commit = select.index(
            "s_eActiveRenderer = ROLLER_ED_RENDERER_GPU", destroy_candidate
        )
        self.assertLess(failed_attach, destroy_candidate)
        self.assertLess(destroy_candidate, gpu_commit)
        self.assertIn("ROLLER_ED_RESULT_GPU_FAILED", select[failed_attach:gpu_commit])

    def test_game_backend_replacement_commits_after_scene_attachment(self) -> None:
        attach = extract_function(self.game, "game_render_attach_gpu_device")
        scene_attach = attach.index("scene_render_attach_gpu_device")
        destroy_old = attach.index("game_render_hw_destroy(renderer->hw)")
        commit = attach.index("renderer->gpu = scene_render_get_gpu")
        self.assertLess(scene_attach, destroy_old)
        self.assertLess(destroy_old, commit)


if __name__ == "__main__":
    unittest.main()
