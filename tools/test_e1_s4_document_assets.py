#!/usr/bin/env python3
"""End-to-end E1-S4 direct-stage and per-document asset-root checks."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(arguments: list[str], cwd: Path, expect_success: bool = True) -> str:
    result = subprocess.run(
        arguments, cwd=cwd, capture_output=True, text=True, check=False
    )
    output = result.stdout + result.stderr
    if expect_success and result.returncode:
        raise RuntimeError(
            f"command exited with {result.returncode}: {' '.join(arguments)}\n"
            + output
        )
    if (
        not expect_success
        and result.returncode == 0
        and "Direct track load failed" not in output
    ):
        raise RuntimeError(
            "document-first corrupt asset unexpectedly fell back to FATDATA"
        )
    return output


def copy_document_assets(assets: Path, document: Path) -> None:
    document.mkdir()
    for name in (
        "T3.DRH",
        "TRACK3.DRH",
        "BUILDING.DRH",
        "PALETTE.PAL",
    ):
        shutil.copyfile(assets / name, document / name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roller", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("scratch", type=Path)
    args = parser.parse_args()

    roller = args.roller.resolve()
    assets = args.assets.resolve()
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="e1-s4-assets-", dir=scratch) as td:
        root = Path(td)
        serialized = root / "serialized-temp"
        document_a = root / "document-a"
        document_b = root / "document-b"
        fallback_document = root / "fallback-document"
        empty_fallback = root / "empty-fallback"
        corrupt_document = root / "corrupt-document"
        missing_palette_document = root / "missing-palette-document"
        serialized.mkdir()
        fallback_document.mkdir()
        empty_fallback.mkdir()
        copy_document_assets(assets, document_a)
        copy_document_assets(assets, document_b)
        copy_document_assets(assets, corrupt_document)
        copy_document_assets(assets, missing_palette_document)

        track = serialized / "VISIBLE-DOCUMENT.TRK"
        shutil.copyfile(assets / "TRACK3.TRK", track)
        (corrupt_document / "T3.DRH").write_bytes(b"")
        (missing_palette_document / "PALETTE.PAL").unlink()

        common = [
            str(roller),
            "--no-crash-handler",
            "--whiplash-root",
            str(assets),
            "--snapshot",
            "INTRO1.GSS",
            "--frames",
            "60",
            "--track-path",
            str(track),
            "--verify-track-state",
        ]

        for index, document in enumerate(
            (document_a, document_b, fallback_document)
        ):
            output_dir = root / f"render-{index}"
            output_dir.mkdir()
            success_output = run(
                common
                + [
                    "--track-asset-root",
                    str(document),
                    "--track-fallback-root",
                    str(
                        assets
                        if document == fallback_document
                        else empty_fallback
                    ),
                    "--out",
                    str(output_dir),
                ],
                root,
            )
            if len(list(output_dir.glob("*.png"))) != 1:
                raise RuntimeError(
                    f"document render was not produced: {document}\n"
                    + success_output
                )

        corrupt_output = root / "corrupt-output"
        corrupt_output.mkdir()
        error = run(
            common
            + [
                "--track-asset-root",
                str(corrupt_document),
                "--track-fallback-root",
                str(assets),
                "--out",
                str(corrupt_output),
            ],
            root,
            expect_success=False,
        )
        corrupt_path = str(corrupt_document / "T3.DRH")
        normalized_error = error.lower().replace("\\", "/")
        normalized_path = corrupt_path.lower().replace("\\", "/")
        if normalized_path not in normalized_error or "failed staging" not in error:
            raise RuntimeError(f"asset error was not meaningful:\n{error}")

        missing_output = root / "missing-output"
        missing_output.mkdir()
        missing_error = run(
            common
            + [
                "--track-asset-root",
                str(missing_palette_document),
                "--track-fallback-root",
                str(empty_fallback),
                "--out",
                str(missing_output),
            ],
            root,
            expect_success=False,
        )
        if (
            "PALETTE.PAL" not in missing_error
            or str(missing_palette_document) not in missing_error
            or str(empty_fallback) not in missing_error
        ):
            raise RuntimeError(f"missing palette error was not meaningful:\n{missing_error}")

        print(
            "E1-S4 PASS: temp track used two independent document roots, "
            "FATDATA fallback, and document-first failure semantics"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
