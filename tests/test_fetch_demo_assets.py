#!/usr/bin/env python3
"""Network-free tests for scripts/fetch_demo_assets.py."""

from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import fetch_demo_assets as assets


def artifact_identity(content: bytes) -> assets.ArtifactIdentity:
    return assets.ArtifactIdentity(
        url="https://example.invalid/demo.zip",
        filename="demo.zip",
        size=len(content),
        sha256=hashlib.sha256(content).hexdigest(),
    )


def tree_identity(files: dict[str, bytes]) -> assets.TreeIdentity:
    entries = [
        assets.ManifestEntry(
            path=path,
            size=len(content),
            sha256=hashlib.sha256(content).hexdigest(),
        )
        for path, content in sorted(files.items())
    ]
    return assets.TreeIdentity(
        file_count=len(entries),
        total_size=sum(entry.size for entry in entries),
        sha256=assets.tree_digest(entries),
    )


def write_zip(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for name, content in files.items():
            package.writestr(name, content)


class InterruptedResponse:
    def __init__(self) -> None:
        self.read_count = 0

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        return None

    def read(self, size: int) -> bytes:
        del size
        self.read_count += 1
        if self.read_count == 1:
            return b"partial"
        raise OSError("connection interrupted")


class FetchDemoAssetsTests(unittest.TestCase):
    def setUp(self) -> None:
        self._stdout = io.StringIO()
        self._redirect = redirect_stdout(self._stdout)
        self._redirect.__enter__()

    def tearDown(self) -> None:
        self._redirect.__exit__(None, None, None)

    def test_manifest_must_remain_outside_asset_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "fatdata-demo"
            with self.assertRaisesRegex(assets.AssetError, "manifest must be outside"):
                assets.main(
                    [
                        "--source",
                        str(Path(temporary) / "unused.zip"),
                        "--output",
                        str(output),
                        "--manifest",
                        str(output / "manifest.json"),
                    ]
                )

    def test_verified_cache_hit_skips_network(self) -> None:
        content = b"verified archive"
        identity = artifact_identity(content)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            cached = cache / identity.cache_filename
            cached.write_bytes(content)

            def fail_open(url: str, timeout: float):
                self.fail(f"network opened for cache hit: {url}, {timeout}")

            result = assets.acquire_archive(
                cache,
                identity=identity,
                open_url=fail_open,
            )
            self.assertEqual(result, cached)

    def test_bad_cache_entry_is_deleted_and_rejected(self) -> None:
        content = b"expected content"
        identity = artifact_identity(content)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            cached = cache / identity.cache_filename
            cached.write_bytes(b"x" * len(content))

            with self.assertRaisesRegex(assets.AssetError, "removed invalid cache entry"):
                assets.acquire_archive(cache, identity=identity)
            self.assertFalse(cached.exists())

    def test_explicit_bad_source_is_not_deleted(self) -> None:
        content = b"expected content"
        identity = artifact_identity(content)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.zip"
            source.write_bytes(b"x" * len(content))

            with self.assertRaisesRegex(assets.AssetError, "SHA-256 mismatch"):
                assets.acquire_archive(Path(temporary) / "cache", source, identity)
            self.assertTrue(source.exists())

    def test_interrupted_download_removes_partial_file(self) -> None:
        content = b"complete archive"
        identity = artifact_identity(content)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)

            with self.assertRaisesRegex(assets.AssetError, "download failed"):
                assets.acquire_archive(
                    cache,
                    identity=identity,
                    open_url=lambda url, timeout: InterruptedResponse(),
                )

            self.assertEqual(list(cache.iterdir()), [])

    def test_extracts_only_fatdata_and_is_idempotent(self) -> None:
        files = {"A.BIN": b"alpha", "NESTED/B.BIN": b"bravo"}
        identity = tree_identity(files)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "demo.zip"
            output = root / "fatdata-demo"
            manifest = root / "manifest.json"
            write_zip(
                archive,
                {
                    "README.TXT": b"ignored",
                    "FATDATA/A.BIN": files["A.BIN"],
                    "FATDATA/NESTED/B.BIN": files["NESTED/B.BIN"],
                },
            )

            entries = assets.materialize_assets(
                archive,
                output,
                manifest,
                tree_identity=identity,
                required_paths=(),
                forbidden_paths=(),
            )
            self.assertEqual([entry.path for entry in entries], sorted(files))
            self.assertEqual((output / "A.BIN").read_bytes(), b"alpha")
            self.assertFalse((output / "README.TXT").exists())

            archive.unlink()
            repeated = assets.materialize_assets(
                archive,
                output,
                manifest,
                tree_identity=identity,
                required_paths=(),
                forbidden_paths=(),
            )
            self.assertEqual(repeated, entries)

    def test_manifest_failure_preserves_previous_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "demo.zip"
            output = root / "fatdata-demo"
            output.mkdir()
            (output / "old.txt").write_text("keep", encoding="ascii")
            write_zip(archive, {"FATDATA/NEW.BIN": b"new"})
            wrong_identity = assets.TreeIdentity(1, 3, "0" * 64)

            with self.assertRaisesRegex(assets.AssetError, "tree SHA-256"):
                assets.materialize_assets(
                    archive,
                    output,
                    root / "manifest.json",
                    tree_identity=wrong_identity,
                    required_paths=(),
                    forbidden_paths=(),
                )

            self.assertEqual((output / "old.txt").read_text(encoding="ascii"), "keep")
            self.assertFalse((output / "NEW.BIN").exists())

    def test_unsafe_member_preserves_previous_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "demo.zip"
            output = root / "fatdata-demo"
            output.mkdir()
            (output / "old.txt").write_text("keep", encoding="ascii")
            write_zip(archive, {"FATDATA/../escape.bin": b"bad"})

            with self.assertRaisesRegex(assets.AssetError, "unsafe FATDATA"):
                assets.materialize_assets(
                    archive,
                    output,
                    root / "manifest.json",
                    tree_identity=assets.TreeIdentity(1, 3, "0" * 64),
                    required_paths=(),
                    forbidden_paths=(),
                )

            self.assertEqual((output / "old.txt").read_text(encoding="ascii"), "keep")
            self.assertFalse((root / "escape.bin").exists())


if __name__ == "__main__":
    unittest.main()
