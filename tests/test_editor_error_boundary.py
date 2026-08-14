from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


class EditorErrorBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        sources = REPOSITORY_ROOT / "PROJECTS" / "ROLLER"
        cls.api = (sources / "editor_api.c").read_text(encoding="ascii")
        cls.api_header = (sources / "editor_api.h").read_text(encoding="ascii")
        cls.loader = (sources / "loadtrak.c").read_text(encoding="utf-8")
        cls.loader_header = (sources / "loadtrak.h").read_text(encoding="utf-8")
        cls.core_error = (
            sources / "roller_core_error.c"
        ).read_text(encoding="ascii")
        cls.queue = (
            sources / "render_queue_3d.c"
        ).read_text(encoding="utf-8")
        cls.game = (sources / "3d.c").read_text(encoding="utf-8")

    def test_facade_commits_only_successful_staged_loads(self) -> None:
        load_body = self.api.split(
            "static eRollerEdResult roller_ed_load_track_file(", 1
        )[1].split(
            "eRollerEdResult ROLLER_ED_CALL RollerEd_LoadTrackFile(", 1
        )[0]
        self.assertIn("ed_track_file_stage", load_body)
        self.assertIn("ROLLER_ED_SCENE_FAILED", load_body)
        self.assertIn("roller_ed_advance_geometry_epoch", load_body)
        self.assertIn("roller_ed_advance_track_generation", load_body)
        self.assertNotIn("not implemented", load_body)

    def test_failed_load_contract_is_published(self) -> None:
        self.assertIn("failed file load clears the renderable scene", self.api_header)
        self.assertIn("advances only the geometry", self.api_header)
        self.assertRegex(
            self.loader_header,
            r"eRollerEdResult\s+loadtrack\(",
        )
        self.assertRegex(
            self.loader_header,
            r"eRollerEdResult\s+loadtrack_from_path_ex\(",
        )

    def test_legacy_loader_has_no_internal_fatal_dialog(self) -> None:
        internal = self.loader.split("static eRollerEdResult loadtrack_internal", 1)[1]
        internal = internal.split("eRollerEdResult loadtrack(", 1)[0]
        self.assertNotIn("ErrorBoxExit", internal)
        self.assertNotRegex(internal, r"\b(exit|abort)\s*\(")
        self.assertGreater(
            internal.rfind("++g_iTrackLoadGeneration"),
            internal.rfind("LoadTextures()"),
        )

    def test_core_fatal_shim_never_exits_or_opens_a_dialog(self) -> None:
        self.assertIn("roller_core_error_pending", self.core_error)
        self.assertNotRegex(self.core_error, r"\b(exit|abort|_exit)\s*\(")
        self.assertNotRegex(
            self.core_error,
            r"SDL_Show(?:Simple)?MessageBox|SDL_ShowMessageBox|MessageBoxA",
        )
        self.assertIn("#if defined(ROLLER_EDITOR_CORE)", self.game)
        self.assertIn("ErrorBoxExit(\"legacy code requested a process exit\")", self.game)

    def test_render_queue_overflow_is_latched_not_fatal(self) -> None:
        self.assertIn("pQueue->overflowed = 1", self.queue)
        self.assertIn("render_queue_3d_overflowed", self.queue)
        self.assertNotRegex(self.queue, r"\b(exit|abort)\s*\(")


if __name__ == "__main__":
    unittest.main()
