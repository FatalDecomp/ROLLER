#!/usr/bin/env python3
"""End-to-end F-S2 state/render parity plus F-S3 live reload soak."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


CHUNK_RECORD_0 = " ".join(["0"] * 22)
CHUNK_RECORD_1 = " ".join(["0"] * 18)
CHUNK_RECORD_2 = " ".join(["0"] * 30)


def run_checked(arguments: list[str], cwd: Path) -> None:
    result = subprocess.run(
        arguments,
        cwd=cwd,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode:
        raise RuntimeError(
            f"command exited with {result.returncode}: "
            + " ".join(arguments)
        )


def write_uncompressed_track(path: Path, record_1: str, texture: str) -> None:
    text = (
        "  1 0 0 0\r\n"
        f"{CHUNK_RECORD_0}\r\n"
        f"{record_1}\r\n"
        f"{CHUNK_RECORD_2}\r\n"
        f"T:{texture}\r\n"
        "BLD:BUILDING.DRH\r\n"
        "BACKS:\r\n"
        "-1 -1\r\n"
    )
    path.write_bytes(text.encode("ascii"))


def only_png(directory: Path) -> Path:
    paths = list(directory.glob("*.png"))
    if len(paths) != 1:
        raise RuntimeError(
            f"expected one snapshot PNG in {directory}, found {len(paths)}"
        )
    return paths[0]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roller", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("scratch", type=Path)
    args = parser.parse_args()

    roller = args.roller.resolve()
    assets = args.assets.resolve()
    scratch = args.scratch.resolve()
    replay = "INTRO1.GSS"
    replay_path = assets / replay

    if not roller.is_file():
        raise RuntimeError(f"ROLLER executable not found: {roller}")
    if not replay_path.is_file():
        raise RuntimeError(f"F-S2 fixture replay not found: {replay_path}")
    with replay_path.open("rb") as replay_file:
        replay_track = replay_file.read(1)
    if len(replay_track) != 1:
        raise RuntimeError(f"F-S2 fixture replay is empty: {replay_path}")
    source_track = assets / f"TRACK{replay_track[0]}.TRK"
    if not source_track.is_file():
        raise RuntimeError(f"F-S2 fixture track not found: {source_track}")
    scratch.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(
        prefix="epic-f-path-reload-", dir=scratch
    ) as temporary:
        root = Path(temporary)
        outside = root / "absolute-fixture"
        indexed_out = root / "indexed"
        direct_out = root / "direct"
        soak_out = root / "soak"
        outside.mkdir()
        indexed_out.mkdir()
        direct_out.mkdir()
        soak_out.mkdir()

        direct_track = outside / "TRACK3.TRK"
        bad_backref = outside / "BAD_BACKREF.TRK"
        bad_text = outside / "BAD_TEXT.TRK"
        missing_asset = outside / "MISSING_ASSET.TRK"
        shutil.copyfile(source_track, direct_track)
        bad_backref.write_bytes(struct.pack("<I", 3) + b"\x80\x00")
        write_uncompressed_track(
            bad_text,
            "X " + " ".join(["0"] * 17),
            "TRACK3.DRH",
        )
        write_uncompressed_track(
            missing_asset,
            CHUNK_RECORD_1,
            "NOFILE.DRH",
        )

        common = [
            str(roller),
            "--no-crash-handler",
            "--whiplash-root",
            str(assets),
            "--snapshot",
            replay,
            "--frames",
            "60",
        ]
        run_checked(common + ["--out", str(indexed_out)], root)
        run_checked(
            common
            + [
                "--out",
                str(direct_out),
                "--track-path",
                str(direct_track),
                "--verify-track-state",
            ],
            root,
        )
        run_checked(
            common
            + [
                "--out",
                str(soak_out),
                "--track-path",
                str(direct_track),
                "--verify-track-state",
                "--track-reload-malformed",
                str(bad_backref),
                "--track-reload-malformed",
                str(bad_text),
                "--track-reload-malformed",
                str(missing_asset),
                "--track-reload-cycles",
                "4",
            ],
            root,
        )

        indexed_png = only_png(indexed_out)
        direct_png = only_png(direct_out)
        soak_png = only_png(soak_out)
        indexed_hash = sha256(indexed_png)
        direct_hash = sha256(direct_png)
        soak_hash = sha256(soak_png)
        if indexed_hash != direct_hash:
            raise RuntimeError(
                "F-S2 render mismatch: "
                f"indexed={indexed_hash} direct={direct_hash}"
            )
        if direct_hash != soak_hash:
            raise RuntimeError(
                "F-S3 recovery render mismatch: "
                f"before={direct_hash} after={soak_hash}"
            )
        print(
            "F-S2 PASS: absolute path outside TRACKS rendered identically "
            f"with populated community state ({direct_hash})"
        )
        print(
            "F-S3 PASS: live scene survived corrupt back-reference, "
            "mid-text, and missing-asset fixtures before final render"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
