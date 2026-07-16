#!/usr/bin/env python3
"""Install a deterministic, content-hashed ROLLER web bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile


DEFAULT_MAX_FIRST_LOAD = 30 * 1024 * 1024
LEGACY_DATA_NAME = "roller.data"


class BundleError(RuntimeError):
    """Raised when generated web artifacts do not form a safe bundle."""


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def read_regular_file(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise BundleError(f"required bundle input is not a regular file: {path}")
    return path.read_bytes()


def atomic_write(path: Path, content: bytes) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def file_record(content: bytes) -> dict[str, object]:
    return {
        "sha256": sha256_bytes(content),
        "size": len(content),
    }


def rewrite_loader(loader: bytes, data_name: str) -> bytes:
    legacy = LEGACY_DATA_NAME.encode("ascii")
    package_pattern = re.compile(rb'var PACKAGE_NAME="([^"\r\n]*roller\.data)";')
    package_matches = package_pattern.findall(loader)
    if len(package_matches) != 1:
        raise BundleError(
            "generated loader does not contain exactly one expected "
            f"PACKAGE_NAME for {LEGACY_DATA_NAME}"
        )

    generated_package_name = package_matches[0]
    rewritten = loader.replace(generated_package_name, data_name.encode("ascii"))
    rewritten = rewritten.replace(legacy, data_name.encode("ascii"))
    if legacy in rewritten:
        raise BundleError(f"failed to replace every {LEGACY_DATA_NAME} loader reference")
    return rewritten


def remove_stale_payloads(output_dir: Path, current_name: str) -> None:
    candidates = list(output_dir.glob("roller-*.data"))
    candidates.append(output_dir / LEGACY_DATA_NAME)
    for candidate in candidates:
        if candidate.name == current_name:
            continue
        if not candidate.exists() and not candidate.is_symlink():
            continue
        if candidate.is_symlink() or not candidate.is_file():
            raise BundleError(f"refusing to remove non-file payload path: {candidate}")
        candidate.unlink()


def finalize_bundle(
    html_path: Path,
    shell_js_path: Path,
    headers_path: Path,
    output_dir: Path,
    max_first_load: int = DEFAULT_MAX_FIRST_LOAD,
) -> dict[str, object]:
    stage_dir = html_path.parent
    html = read_regular_file(html_path)
    loader = read_regular_file(stage_dir / "roller.js")
    wasm = read_regular_file(stage_dir / "roller.wasm")
    data = read_regular_file(stage_dir / LEGACY_DATA_NAME)
    shell_js = read_regular_file(shell_js_path)
    headers = read_regular_file(headers_path)

    data_sha256 = sha256_bytes(data)
    data_name = f"roller-{data_sha256}.data"
    loader = rewrite_loader(loader, data_name)

    files = {
        "index.html": html,
        "roller-shell.js": shell_js,
        "roller.js": loader,
        "roller.wasm": wasm,
        data_name: data,
        "_headers": headers,
    }
    first_load_names = (
        "index.html",
        "roller-shell.js",
        "roller.js",
        "roller.wasm",
        data_name,
    )
    raw_first_load = sum(len(files[name]) for name in first_load_names)
    if raw_first_load > max_first_load:
        raise BundleError(
            f"raw first-load bundle is {raw_first_load} bytes, above limit {max_first_load}"
        )

    manifest: dict[str, object] = {
        "schema": 1,
        "asset_mount": "/demo/fatdata",
        "data_file": data_name,
        "first_load": {
            "compression_applied_by_build": False,
            "raw_size": raw_first_load,
            "transferred_size": None,
        },
        "files": {
            name: file_record(content)
            for name, content in sorted(files.items())
        },
    }
    manifest_bytes = (
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    ).encode("ascii")

    output_dir.mkdir(parents=True, exist_ok=True)
    if output_dir.is_symlink() or not output_dir.is_dir():
        raise BundleError(f"bundle output is not a regular directory: {output_dir}")

    for name, content in files.items():
        atomic_write(output_dir / name, content)
    atomic_write(output_dir / "bundle-manifest.json", manifest_bytes)
    remove_stale_payloads(output_dir, data_name)

    print(
        f"Web bundle: {output_dir} ({raw_first_load} raw first-load bytes, "
        f"payload {data_name})"
    )
    return manifest


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Rewrite Emscripten's generated data URL to its content-hashed name "
            "and install an inventoried browser bundle."
        )
    )
    parser.add_argument("--html", required=True, type=Path)
    parser.add_argument("--shell-js", required=True, type=Path)
    parser.add_argument("--headers", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--max-first-load",
        type=int,
        default=DEFAULT_MAX_FIRST_LOAD,
        help=f"maximum raw first-load bytes (default: {DEFAULT_MAX_FIRST_LOAD})",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    finalize_bundle(
        html_path=args.html.resolve(),
        shell_js_path=args.shell_js.resolve(),
        headers_path=args.headers.resolve(),
        output_dir=args.output_dir.resolve(),
        max_first_load=args.max_first_load,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BundleError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
