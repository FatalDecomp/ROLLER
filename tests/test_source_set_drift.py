from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import check_roller_core_manifest as manifest  # noqa: E402
import check_source_set_drift as drift  # noqa: E402


class SourceSetDriftTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build_zig = (REPOSITORY_ROOT / "build.zig").read_text(encoding="utf-8")
        cls.cmake_lists = (REPOSITORY_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "build.yml"
        ).read_text(encoding="utf-8")
        cls.manifest_entries = manifest.load_manifest(
            REPOSITORY_ROOT / "roller-core.srclist", REPOSITORY_ROOT
        )

    def validate(self, **overrides):
        values = {
            "build_zig": self.build_zig,
            "cmake_lists": self.cmake_lists,
            "workflow": self.workflow,
            "manifest_entries": self.manifest_entries,
            "repository_root": REPOSITORY_ROOT,
        }
        values.update(overrides)
        return drift.validate(**values)

    def test_repository_platform_source_sets_match(self) -> None:
        source_sets = self.validate()

        self.assertIn("PROJECTS/ROLLER/scene_render.c", source_sets["linux"])
        self.assertIn("PROJECTS/ROLLER/midi_player.c", source_sets["windows"])
        self.assertIn("external/rtmidi/RtMidi.cpp", source_sets["windows"])
        self.assertNotIn("PROJECTS/ROLLER/midi_player.c", source_sets["linux"])
        self.assertIn("PROJECTS/ROLLER/roller_web.c", source_sets["emscripten"])
        self.assertNotIn(
            "PROJECTS/ROLLER/scene_render_gpu.c", source_sets["emscripten"]
        )

    def test_every_game_module_source_block_is_parsed(self) -> None:
        calls = drift.parse_zig_source_calls(self.build_zig)

        self.assertEqual(len(calls), self.build_zig.count("exe_mod.addCSourceFiles"))
        self.assertTrue(
            any(
                call.platforms == frozenset(("windows",))
                and "external/rtmidi/rtmidi_c.cpp" in call.sources
                for call in calls
            )
        )
        self.assertTrue(
            any(
                call.platforms == frozenset(("emscripten",))
                and "PROJECTS/ROLLER/roller_web.c" in call.sources
                for call in calls
            )
        )

    def test_source_added_only_to_zig_is_rejected(self) -> None:
        build_zig = self.build_zig.replace(
            '            "PROJECTS/ROLLER/3d.c",\n',
            '            "PROJECTS/ROLLER/3d.c",\n'
            '            "PROJECTS/ROLLER/debug_overlay_stub.c",\n',
            1,
        )

        with self.assertRaisesRegex(drift.SourceSetError, "missing from CMake"):
            self.validate(build_zig=build_zig)

    def test_source_removed_only_from_cmake_is_rejected(self) -> None:
        cmake_lists = self.cmake_lists.replace(
            "    PROJECTS/ROLLER/scene_render.c\n", "", 1
        )

        with self.assertRaisesRegex(drift.SourceSetError, "missing from CMake"):
            self.validate(cmake_lists=cmake_lists)

    def test_windows_module_addition_cannot_drift(self) -> None:
        cmake_lists = self.cmake_lists.replace(
            "    PROJECTS/ROLLER/midi_player.c\n", "", 1
        )

        with self.assertRaisesRegex(
            drift.SourceSetError,
            "windows: sources present in Zig but missing from CMake",
        ):
            self.validate(cmake_lists=cmake_lists)

    def test_game_sources_must_remain_manifest_classified(self) -> None:
        entries = dict(self.manifest_entries)
        del entries["PROJECTS/ROLLER/scene_render.c"]

        with self.assertRaisesRegex(
            drift.SourceSetError, "not classified by roller-core.srclist"
        ):
            self.validate(manifest_entries=entries)

    def test_reusable_build_workflow_must_run_the_check(self) -> None:
        workflow = self.workflow.replace(
            "        run: python tools/check_source_set_drift.py\n", "", 1
        )

        with self.assertRaisesRegex(drift.SourceSetError, "reusable build workflow"):
            self.validate(workflow=workflow)


if __name__ == "__main__":
    unittest.main()
