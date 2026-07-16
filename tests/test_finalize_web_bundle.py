#!/usr/bin/env python3
"""Tests for scripts/finalize_web_bundle.py."""

from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import finalize_web_bundle as bundle


def write_inputs(root: Path, data: bytes) -> tuple[Path, Path, Path]:
    stage = root / "stage"
    stage.mkdir()
    html = stage / "roller.html"
    html.write_text("<script src=\"roller.js\"></script>\n", encoding="ascii")
    (stage / "roller.js").write_text(
        'var PACKAGE_NAME="C:/cache/roller.data";'
        'fetch("roller.data");addRunDependency("C:/cache/roller.data");\n',
        encoding="ascii",
    )
    (stage / "roller.wasm").write_bytes(b"wasm")
    (stage / "roller.data").write_bytes(data)

    shell = root / "roller-shell.js"
    shell.write_text("var Module = {};\n", encoding="ascii")
    headers = root / "_headers"
    headers.write_text("/roller-*.data\n  Cache-Control: immutable\n", encoding="ascii")
    return html, shell, headers


class FinalizeWebBundleTests(unittest.TestCase):
    def setUp(self) -> None:
        self._stdout = io.StringIO()
        self._redirect = redirect_stdout(self._stdout)
        self._redirect.__enter__()

    def tearDown(self) -> None:
        self._redirect.__exit__(None, None, None)

    def test_hashes_rewrites_installs_and_removes_stale_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            html, shell, headers = write_inputs(root, b"demo payload")
            output = root / "web"
            output.mkdir()
            (output / "roller-deadbeef.data").write_bytes(b"stale")
            (output / "roller.data").write_bytes(b"legacy")

            manifest = bundle.finalize_bundle(html, shell, headers, output)
            digest = hashlib.sha256(b"demo payload").hexdigest()
            data_name = f"roller-{digest}.data"

            self.assertEqual(manifest["data_file"], data_name)
            self.assertEqual((output / data_name).read_bytes(), b"demo payload")
            loader = (output / "roller.js").read_text(encoding="ascii")
            self.assertEqual(loader.count(data_name), 3)
            self.assertNotIn("roller.data", loader)
            self.assertNotIn("C:/cache", loader)
            self.assertFalse((output / "roller-deadbeef.data").exists())
            self.assertFalse((output / "roller.data").exists())

            recorded = json.loads(
                (output / "bundle-manifest.json").read_text(encoding="ascii")
            )
            self.assertEqual(recorded, manifest)
            self.assertFalse(
                recorded["first_load"]["compression_applied_by_build"]
            )

    def test_stable_input_is_stable_and_one_byte_changes_name(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            html, shell, headers = write_inputs(root, b"abc")
            output = root / "web"

            first = bundle.finalize_bundle(html, shell, headers, output)
            repeated = bundle.finalize_bundle(html, shell, headers, output)
            self.assertEqual(first, repeated)

            (html.parent / "roller.data").write_bytes(b"abd")
            changed = bundle.finalize_bundle(html, shell, headers, output)
            self.assertNotEqual(first["data_file"], changed["data_file"])
            self.assertFalse((output / first["data_file"]).exists())
            self.assertTrue((output / changed["data_file"]).is_file())

    def test_missing_legacy_loader_reference_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            html, shell, headers = write_inputs(root, b"abc")
            (html.parent / "roller.js").write_text("no package here\n", encoding="ascii")

            with self.assertRaisesRegex(bundle.BundleError, "PACKAGE_NAME"):
                bundle.finalize_bundle(html, shell, headers, root / "web")

    def test_first_load_limit_is_enforced_before_install(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            html, shell, headers = write_inputs(root, b"abc")
            output = root / "web"

            with self.assertRaisesRegex(bundle.BundleError, "above limit"):
                bundle.finalize_bundle(
                    html,
                    shell,
                    headers,
                    output,
                    max_first_load=1,
                )
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
