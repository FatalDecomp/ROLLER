from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

import check_roller_core_manifest as manifest  # noqa: E402


class RollerCoreManifestTests(unittest.TestCase):
    def test_repository_manifest_covers_every_translation_unit(self) -> None:
        entries = manifest.load_manifest(
            REPOSITORY_ROOT / "roller-core.srclist", REPOSITORY_ROOT
        )
        counts = manifest.validate_manifest(
            REPOSITORY_ROOT / "roller-core.srclist", REPOSITORY_ROOT
        )

        actual_source_count = len(
            list((REPOSITORY_ROOT / "PROJECTS/ROLLER").glob("*.c"))
        )
        self.assertEqual(len(entries), actual_source_count)
        self.assertEqual(sum(counts.values()), actual_source_count)
        self.assertEqual(entries["PROJECTS/ROLLER/editor_api.c"], "KEEP")
        self.assertEqual(entries["PROJECTS/ROLLER/editor_camera.c"], "KEEP")
        self.assertEqual(
            entries["PROJECTS/ROLLER/editor_legacy_scene.c"], "KEEP"
        )
        self.assertEqual(
            entries["PROJECTS/ROLLER/roller_core_error.c"], "KEEP"
        )
        self.assertEqual(
            entries["PROJECTS/ROLLER/editor_reference_mesh.c"], "KEEP"
        )
        self.assertEqual(entries["PROJECTS/ROLLER/editor_surface.c"], "KEEP")
        self.assertEqual(entries["PROJECTS/ROLLER/editor_track_loader.c"], "KEEP")
        self.assertEqual(
            entries["PROJECTS/ROLLER/editor_core_host.c"], "KEEP"
        )
        self.assertEqual(
            entries["PROJECTS/ROLLER/gpu_parity.c"], "PRESENT_BUT_DORMANT"
        )

    def test_unclassified_translation_unit_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_directory = root / "PROJECTS" / "ROLLER"
            source_directory.mkdir(parents=True)
            (source_directory / "kept.c").write_text("", encoding="ascii")
            (source_directory / "new.c").write_text("", encoding="ascii")
            manifest_path = root / "roller-core.srclist"
            manifest_path.write_text(
                "KEEP|PROJECTS/ROLLER/kept.c\n", encoding="ascii"
            )

            with self.assertRaisesRegex(
                manifest.ManifestError, "unclassified translation units"
            ):
                manifest.validate_manifest(manifest_path, root)

    def test_duplicate_translation_unit_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_directory = root / "PROJECTS" / "ROLLER"
            source_directory.mkdir(parents=True)
            (source_directory / "kept.c").write_text("", encoding="ascii")
            manifest_path = root / "roller-core.srclist"
            manifest_path.write_text(
                "KEEP|PROJECTS/ROLLER/kept.c\n"
                "EXCLUDE|PROJECTS/ROLLER/kept.c\n",
                encoding="ascii",
            )

            with self.assertRaisesRegex(manifest.ManifestError, "duplicate source"):
                manifest.validate_manifest(manifest_path, root)

    def test_stub_swap_requires_excluded_real_implementation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_directory = root / "PROJECTS" / "ROLLER"
            source_directory.mkdir(parents=True)
            (source_directory / "service.c").write_text("", encoding="ascii")
            (source_directory / "service_stub.c").write_text("", encoding="ascii")
            manifest_path = root / "roller-core.srclist"
            manifest_path.write_text(
                "KEEP|PROJECTS/ROLLER/service.c\n"
                "STUB_SWAP|PROJECTS/ROLLER/service_stub.c\n",
                encoding="ascii",
            )

            with self.assertRaisesRegex(
                manifest.ManifestError, "must replace an EXCLUDE entry"
            ):
                manifest.validate_manifest(manifest_path, root)


if __name__ == "__main__":
    unittest.main()
