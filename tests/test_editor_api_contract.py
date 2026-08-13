from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


class EditorApiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            REPOSITORY_ROOT / "PROJECTS" / "ROLLER" / "editor_api.h"
        ).read_text(encoding="ascii")
        cls.implementation = (
            REPOSITORY_ROOT / "PROJECTS" / "ROLLER" / "editor_api.c"
        ).read_text(encoding="ascii")
        cls.workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "ci.yml"
        ).read_text(encoding="utf-8")

    def test_public_header_is_a_literal_c_boundary(self) -> None:
        self.assertIn("#include <stdint.h>", self.header)
        self.assertIn("#include <stddef.h>", self.header)
        self.assertIn('extern "C"', self.header)
        self.assertIn("ROLLER_ED_API", self.header)
        self.assertIn("ROLLER_ED_CALL", self.header)
        self.assertIn("#  pragma pack(push, 8)", self.header)
        self.assertIn("ROLLER_ED_STATIC_ASSERT", self.header)
        self.assertNotIn('#include "types.h"', self.header)

    def test_public_result_and_geometry_contract_is_complete(self) -> None:
        for name in (
            "ROLLER_ED_RESULT_OUT_OF_MEMORY",
            "ROLLER_ED_RESULT_IO_FAILED",
            "ROLLER_ED_RESULT_UNSUPPORTED",
            "ROLLER_ED_RESULT_INTERNAL_ERROR",
            "ROLLER_ED_RESULT_INVALID_STATE",
            "ROLLER_ED_RESULT_WRONG_THREAD",
            "uiGeometryEpoch",
            "uiTrackGeneration",
            "uiSceneState",
            "uiVertexStride",
            "uiPrimitiveStride",
            "uiMaterialStride",
            "uiFrontMaterialId",
            "uiBackMaterialId",
            "uiDarkenLevel",
            "fAtlasScale",
            "fAtlasBias",
            "ROLLER_ED_OVERLAY_SHOW_SURFACES",
            "ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH",
            "ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS",
        ):
            self.assertIn(name, self.header)

    def test_all_fallible_facade_calls_return_named_results(self) -> None:
        for function_name in (
            "RollerEd_Bootstrap",
            "RollerEd_Teardown",
            "RollerEd_Init",
            "RollerEd_Shutdown",
            "RollerEd_SelectRenderer",
            "RollerEd_SetGraphicsSettings",
            "RollerEd_LoadTrackFile",
            "RollerEd_UnloadTrack",
            "RollerEd_SetCamera",
            "RollerEd_AdvanceStunts",
            "RollerEd_RenderFrame",
            "RollerEd_SetOverlayState",
            "RollerEd_SetReferenceMesh",
            "RollerEd_QueryTowerCount",
            "RollerEd_QueryTower",
            "RollerEd_QueryGeometrySizes",
            "RollerEd_FillGeometry",
        ):
            self.assertRegex(
                self.header,
                rf"eRollerEdResult\s+ROLLER_ED_CALL\s+{function_name}\s*\(",
            )

    def test_editor_queue_tags_are_not_in_the_public_header(self) -> None:
        self.assertNotIn("tEdRenderRequestTag", self.header)
        self.assertNotIn("tEdRenderResultTag", self.header)
        self.assertNotIn("ullDocumentId", self.header)
        self.assertNotIn("ullRequestId", self.header)

    def test_sdl_ownership_is_ref_counted_and_scoped(self) -> None:
        self.assertIn("SDL_IsMainThread", self.implementation)
        self.assertIn("SDL_InitSubSystem(SDL_INIT_VIDEO)", self.implementation)
        self.assertIn("SDL_QuitSubSystem(SDL_INIT_VIDEO)", self.implementation)
        self.assertNotRegex(self.implementation, r"\bSDL_Quit\s*\(")
        self.assertIn("eRollerEdLifecycleState", self.implementation)

    def test_native_ci_runs_the_main_thread_lifecycle_test(self) -> None:
        self.assertIn(
            "zig build test-editor-api -Doptimize=ReleaseSafe", self.workflow
        )


if __name__ == "__main__":
    unittest.main()
