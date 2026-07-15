#!/usr/bin/env python3
"""Install the Emscripten SDK version pinned for the ROLLER web build."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


EMSDK_VERSION = "4.0.20"
EMSDK_REPOSITORY = "https://github.com/emscripten-core/emsdk.git"


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    emsdk_root = repo_root / ".tools" / "emsdk"
    marker = emsdk_root / ".roller-version"
    emsdk_name = "emsdk.bat" if os.name == "nt" else "emsdk"
    emsdk = emsdk_root / emsdk_name
    emcc_name = "emcc.bat" if os.name == "nt" else "emcc"
    emcc = emsdk_root / "upstream" / "emscripten" / emcc_name

    if not emsdk_root.exists():
        emsdk_root.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                EMSDK_REPOSITORY,
                str(emsdk_root),
            ],
            repo_root,
        )
    elif not emsdk.exists():
        raise RuntimeError(f"Existing directory is not an emsdk checkout: {emsdk_root}")

    installed = (
        emcc.exists()
        and marker.exists()
        and marker.read_text(encoding="ascii").strip() == EMSDK_VERSION
    )
    if not installed:
        run([str(emsdk), "install", EMSDK_VERSION], emsdk_root)
    run([str(emsdk), "activate", EMSDK_VERSION], emsdk_root)
    marker.write_text(EMSDK_VERSION + "\n", encoding="ascii")

    env = os.environ.copy()
    env["EMSDK"] = str(emsdk_root)
    env["EM_CONFIG"] = str(emsdk_root / ".emscripten")
    result = subprocess.run(
        [str(emcc), "--version"],
        cwd=emsdk_root,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    version_line = result.stdout.splitlines()[0] if result.stdout else ""
    if EMSDK_VERSION not in version_line:
        raise RuntimeError(f"Expected emcc {EMSDK_VERSION}, got: {version_line}")
    print(version_line)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
