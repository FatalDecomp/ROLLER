"""Durable E1-S1 windowless/fixed-format GPU contract checks."""

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


class EditorWindowlessGpuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        sources = REPOSITORY_ROOT / "PROJECTS" / "ROLLER"
        cls.renderer = (sources / "scene_render_gpu.c").read_text(
            encoding="utf-8"
        )
        cls.renderer_header = (sources / "scene_render_gpu.h").read_text(
            encoding="utf-8"
        )
        cls.parity = (sources / "gpu_parity.c").read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "ci.yml"
        ).read_text(encoding="utf-8")

        cls.renderer_struct = cls.renderer.split(
            "struct SceneRendererGPU {", 1
        )[1].split("\n};", 1)[0]
        cls.constructor = cls.renderer.split(
            "SceneRendererGPU *scene_render_gpu_create(", 1
        )[1].split(
            "\nSceneRendererGPU *scene_render_gpu_create_windowless", 1
        )[0]

    def test_all_scene_color_targets_use_the_renderer_format(self) -> None:
        self.assertIn(
            "SDL_GPUTextureFormat colorTargetFormat;", self.renderer_struct
        )
        self.assertEqual(
            len(
                re.findall(
                    r"\.format\s*=\s*r->colorTargetFormat", self.renderer
                )
            ),
            17,
            "all 17 E1-S1 color-target sites must use the renderer format",
        )
        self.assertEqual(
            self.renderer.count("SDL_GetGPUSwapchainTextureFormat"),
            1,
            "only the windowed constructor may inspect the swapchain format",
        )

    def test_constructor_selects_windowed_or_fixed_editor_format(self) -> None:
        self.assertRegex(
            self.constructor,
            r"r->colorTargetFormat\s*=\s*window\s*"
            r"\?\s*SDL_GetGPUSwapchainTextureFormat\(device, window\)\s*"
            r":\s*SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM\s*;",
        )
        self.assertIn(
            "SceneRendererGPU *scene_render_gpu_create_windowless",
            self.renderer_header,
        )
        windowless_constructor = self.renderer.split(
            "SceneRendererGPU *scene_render_gpu_create_windowless", 1
        )[1].split("\n}", 1)[0]
        self.assertIn(
            "return scene_render_gpu_create(device, NULL);",
            windowless_constructor,
        )

    def test_constructor_checks_every_pipeline_member(self) -> None:
        pipeline_members = re.findall(
            r"SDL_GPUGraphicsPipeline\s*\*\s*(\w+)\s*;",
            self.renderer_struct,
        )
        self.assertGreater(len(pipeline_members), 0)
        unchecked = [
            member
            for member in pipeline_members
            if f"!r->{member}" not in self.constructor
        ]
        self.assertEqual(
            unchecked,
            [],
            f"renderer construction must fail for unchecked pipelines: {unchecked}",
        )

    def test_backend_parity_covers_both_construction_modes(self) -> None:
        self.assertIn(
            "pWindowed = scene_render_gpu_create(pDevice, pWindow);",
            self.parity,
        )
        self.assertIn(
            "pWindowless = scene_render_gpu_create_windowless(pDevice);",
            self.parity,
        )
        self.assertIn("if (!pWindowed)", self.parity)
        self.assertIn("if (!pWindowless)", self.parity)
        self.assertIn(
            "F-S1 PASS: all 16 windowed/windowless comparisons passed",
            self.parity,
        )

        for runner, backend in (
            ("ubuntu-latest", "vulkan"),
            ("windows-latest", "direct3d12"),
            ("macos-15", "metal"),
        ):
            self.assertIn(f"runner: {runner}", self.workflow)
            self.assertIn(f"backend: {backend}", self.workflow)
        self.assertIn("--gpu-parity vulkan", self.workflow)
        self.assertIn("'--gpu-parity', 'direct3d12'", self.workflow)
        self.assertIn("--gpu-parity metal", self.workflow)


if __name__ == "__main__":
    unittest.main()
