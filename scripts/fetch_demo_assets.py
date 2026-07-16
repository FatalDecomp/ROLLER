#!/usr/bin/env python3
"""Fetch, verify, and extract the pinned Whiplash playable demo assets."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import shutil
import stat
import sys
import tempfile
import urllib.error
import urllib.request
import uuid
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CACHE_DIR = REPOSITORY_ROOT / "zig-out" / "demo-cache"
DEFAULT_OUTPUT_DIR = REPOSITORY_ROOT / "zig-out" / "fatdata-demo"

DEMO_URL = "https://archive.org/download/WhiplashDemo/whipdemo.zip"
DEMO_FILENAME = "whipdemo.zip"
DEMO_SIZE = 5_398_262
DEMO_SHA256 = "eca6da4f64b97a400016ec8bd43fba713dcd237274d58f167d93e4149528414c"

DEMO_FILE_COUNT = 190
DEMO_TOTAL_SIZE = 6_246_800
# SHA-256 over sorted lines formatted as: file-sha256 TAB size TAB path LF.
# Filled from the verified archive selected by ADR 0002.
DEMO_TREE_SHA256 = "5848cc23a44b02174f5838a1da7b3d32ccbe7ab6216bcfe2e3edf2fd44195c77"

COPY_BUFFER_SIZE = 1024 * 1024


class AssetError(RuntimeError):
    """A useful, expected acquisition or extraction failure."""


@dataclass(frozen=True)
class ArtifactIdentity:
    url: str
    filename: str
    size: int
    sha256: str

    @property
    def cache_filename(self) -> str:
        stem = Path(self.filename).stem
        suffix = Path(self.filename).suffix
        return f"{stem}-{self.sha256}{suffix}"


@dataclass(frozen=True)
class TreeIdentity:
    file_count: int
    total_size: int
    sha256: str


@dataclass(frozen=True)
class ManifestEntry:
    path: str
    size: int
    sha256: str


DEMO_IDENTITY = ArtifactIdentity(
    url=DEMO_URL,
    filename=DEMO_FILENAME,
    size=DEMO_SIZE,
    sha256=DEMO_SHA256,
)
DEMO_TREE_IDENTITY = TreeIdentity(
    file_count=DEMO_FILE_COUNT,
    total_size=DEMO_TOTAL_SIZE,
    sha256=DEMO_TREE_SHA256,
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(COPY_BUFFER_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def verify_archive(path: Path, identity: ArtifactIdentity) -> None:
    if not path.is_file():
        raise AssetError(f"archive does not exist or is not a file: {path}")

    size = path.stat().st_size
    if size != identity.size:
        raise AssetError(
            f"archive size mismatch for {path}: expected {identity.size}, got {size}"
        )

    digest = sha256_file(path)
    if digest != identity.sha256:
        raise AssetError(
            f"archive SHA-256 mismatch for {path}: expected {identity.sha256}, got {digest}"
        )


def open_demo_url(url: str, timeout: float):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "ROLLER-demo-assets/1"},
    )
    return urllib.request.urlopen(request, timeout=timeout)


def acquire_archive(
    cache_dir: Path,
    source: Path | None = None,
    identity: ArtifactIdentity = DEMO_IDENTITY,
    timeout: float = 60.0,
    open_url=open_demo_url,
) -> Path:
    """Return a verified source archive, downloading to an immutable cache if needed."""

    if source is not None:
        source = source.resolve()
        verify_archive(source, identity)
        print(f"Using verified source archive: {source}")
        return source

    cache_dir.mkdir(parents=True, exist_ok=True)
    cached = cache_dir / identity.cache_filename
    if cached.exists():
        try:
            verify_archive(cached, identity)
        except AssetError as error:
            cached.unlink(missing_ok=True)
            raise AssetError(f"{error}; removed invalid cache entry") from error
        print(f"Using verified cache entry: {cached}")
        return cached

    file_descriptor, part_name = tempfile.mkstemp(
        dir=cache_dir,
        prefix=f".{cached.name}.",
        suffix=".part",
    )
    os.close(file_descriptor)
    part = Path(part_name)

    print(f"Downloading {identity.url}")
    try:
        try:
            with open_url(identity.url, timeout) as response, part.open("wb") as output:
                while chunk := response.read(COPY_BUFFER_SIZE):
                    output.write(chunk)
        except (OSError, urllib.error.URLError) as error:
            raise AssetError(f"download failed for {identity.url}: {error}") from error

        try:
            verify_archive(part, identity)
        except AssetError as error:
            raise AssetError(f"downloaded archive failed verification: {error}") from error

        os.replace(part, cached)
    finally:
        part.unlink(missing_ok=True)

    print(f"Cached verified archive: {cached}")
    return cached


def validate_member_path(info: zipfile.ZipInfo) -> tuple[str, ...] | None:
    name = info.filename
    if "\\" in name or "\x00" in name:
        raise AssetError(f"unsafe ZIP member path: {name!r}")

    parts = name.split("/")
    if not parts or parts[0] != "FATDATA":
        return None

    relative_parts = parts[1:]
    if info.is_dir() and relative_parts and relative_parts[-1] == "":
        relative_parts = relative_parts[:-1]
    if info.is_dir() and not relative_parts:
        return None
    if not relative_parts or any(
        part in ("", ".", "..")
        or ":" in part
        or any(ord(character) < 32 for character in part)
        for part in relative_parts
    ):
        raise AssetError(f"unsafe FATDATA ZIP member path: {name!r}")

    mode = (info.external_attr >> 16) & 0xFFFF
    file_type = stat.S_IFMT(mode)
    if info.is_dir():
        if file_type and not stat.S_ISDIR(mode):
            raise AssetError(f"ZIP member has conflicting directory type: {name!r}")
        return None
    if file_type and not stat.S_ISREG(mode):
        raise AssetError(f"ZIP member is not a regular file: {name!r}")
    if info.flag_bits & 0x1:
        raise AssetError(f"encrypted ZIP member is not supported: {name!r}")

    return tuple(relative_parts)


def extract_fatdata(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    seen_names: set[str] = set()
    extracted = 0

    with zipfile.ZipFile(archive, "r") as package:
        for info in package.infolist():
            relative_parts = validate_member_path(info)
            if relative_parts is None:
                continue

            relative = PurePosixPath(*relative_parts).as_posix()
            folded = relative.casefold()
            if folded in seen_names:
                raise AssetError(f"duplicate FATDATA ZIP member: {relative}")
            seen_names.add(folded)

            output = destination.joinpath(*relative_parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            with package.open(info, "r") as source, output.open("xb") as target:
                shutil.copyfileobj(source, target, length=COPY_BUFFER_SIZE)

            timestamp = datetime(*info.date_time, tzinfo=timezone.utc).timestamp()
            os.utime(output, (timestamp, timestamp))
            extracted += 1

    if extracted == 0:
        raise AssetError(f"archive contains no files under FATDATA/: {archive}")


def build_manifest_entries(root: Path) -> list[ManifestEntry]:
    if root.is_symlink() or not root.is_dir():
        raise AssetError(f"asset output is not a regular directory: {root}")

    entries: list[ManifestEntry] = []
    paths = sorted(root.rglob("*"), key=lambda path: path.relative_to(root).as_posix())
    for path in paths:
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise AssetError(f"asset output contains a symlink: {relative}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise AssetError(f"asset output contains a non-file entry: {relative}")
        entries.append(
            ManifestEntry(
                path=relative,
                size=path.stat().st_size,
                sha256=sha256_file(path),
            )
        )
    return entries


def tree_digest(entries: list[ManifestEntry]) -> str:
    digest = hashlib.sha256()
    for entry in entries:
        line = f"{entry.sha256}\t{entry.size}\t{entry.path}\n"
        digest.update(line.encode("utf-8"))
    return digest.hexdigest()


def verify_tree(
    root: Path,
    identity: TreeIdentity,
    required_paths: tuple[str, ...] = (),
    forbidden_paths: tuple[str, ...] = (),
) -> list[ManifestEntry]:
    entries = build_manifest_entries(root)
    file_count = len(entries)
    total_size = sum(entry.size for entry in entries)
    digest = tree_digest(entries)

    problems: list[str] = []
    if file_count != identity.file_count:
        problems.append(f"files expected {identity.file_count}, got {file_count}")
    if total_size != identity.total_size:
        problems.append(f"bytes expected {identity.total_size}, got {total_size}")
    if digest != identity.sha256:
        problems.append(f"tree SHA-256 expected {identity.sha256}, got {digest}")

    paths = {entry.path.casefold() for entry in entries}
    for required in required_paths:
        if required.casefold() not in paths:
            problems.append(f"required file missing: {required}")
    for forbidden in forbidden_paths:
        if forbidden.casefold() in paths:
            problems.append(f"unexpected file present: {forbidden}")

    if problems:
        raise AssetError(f"demo FATDATA verification failed for {root}: " + "; ".join(problems))
    return entries


def manifest_document(entries: list[ManifestEntry]) -> dict[str, object]:
    return {
        "schema": 1,
        "artifact": asdict(DEMO_IDENTITY),
        "tree": {
            "file_count": len(entries),
            "total_size": sum(entry.size for entry in entries),
            "sha256": tree_digest(entries),
        },
        "files": [asdict(entry) for entry in entries],
    }


def write_manifest(path: Path, entries: list[ManifestEntry]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(manifest_document(entries), indent=2, sort_keys=True) + "\n"
    file_descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(file_descriptor, "w", encoding="ascii", newline="\n") as output:
            output.write(text)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def replace_directory(staged: Path, output: Path) -> None:
    if output.parent == output:
        raise AssetError(f"refusing to replace a filesystem root: {output}")
    if output.is_symlink():
        raise AssetError(f"refusing to replace a symlink output: {output}")
    if output.exists() and not output.is_dir():
        raise AssetError(f"refusing to replace non-directory output: {output}")

    backup: Path | None = None
    if output.exists():
        backup = output.with_name(f".{output.name}.old-{uuid.uuid4().hex}")
        output.rename(backup)

    try:
        staged.rename(output)
    except BaseException:
        if backup is not None and backup.exists() and not output.exists():
            backup.rename(output)
        raise
    else:
        if backup is not None:
            shutil.rmtree(backup)


def materialize_assets(
    archive: Path,
    output: Path,
    manifest: Path,
    tree_identity: TreeIdentity = DEMO_TREE_IDENTITY,
    required_paths: tuple[str, ...] = ("TRACK5.TRK",),
    forbidden_paths: tuple[str, ...] = ("TRACK1.TRK",),
) -> list[ManifestEntry]:
    if output.exists():
        try:
            entries = verify_tree(output, tree_identity, required_paths, forbidden_paths)
        except AssetError as error:
            print(f"Replacing incomplete or stale output: {error}")
        else:
            write_manifest(manifest, entries)
            print(f"Demo FATDATA is already verified: {output}")
            return entries

    output.parent.mkdir(parents=True, exist_ok=True)
    staged = Path(tempfile.mkdtemp(dir=output.parent, prefix=f".{output.name}.tmp-"))
    staged.rmdir()
    try:
        extract_fatdata(archive, staged)
        entries = verify_tree(staged, tree_identity, required_paths, forbidden_paths)
        replace_directory(staged, output)
        write_manifest(manifest, entries)
        return entries
    finally:
        if staged.exists():
            shutil.rmtree(staged)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download and verify the ADR 0002 Whiplash demo, then safely extract "
            "its FATDATA tree for local or CI web packaging."
        )
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=DEFAULT_CACHE_DIR,
        help=f"immutable download cache (default: {DEFAULT_CACHE_DIR})",
    )
    parser.add_argument(
        "--source",
        type=Path,
        help="use an already downloaded archive instead of the network",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"ready-to-package FATDATA output (default: {DEFAULT_OUTPUT_DIR})",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="output manifest path (default: OUTPUT.manifest.json)",
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help=(
            "verify an existing output tree and refresh its manifest without "
            "opening the archive cache or network"
        ),
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="network timeout in seconds (default: 60)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.verify_only and args.source is not None:
        raise AssetError("--source cannot be combined with --verify-only")

    output = args.output.resolve()
    manifest = (
        args.manifest.resolve()
        if args.manifest is not None
        else output.with_name(f"{output.name}.manifest.json")
    )
    try:
        manifest.relative_to(output)
    except ValueError:
        pass
    else:
        raise AssetError("manifest must be outside the FATDATA output directory")

    if args.verify_only:
        entries = verify_tree(
            output,
            DEMO_TREE_IDENTITY,
            required_paths=("TRACK5.TRK",),
            forbidden_paths=("TRACK1.TRK",),
        )
        write_manifest(manifest, entries)
        print(f"Demo FATDATA verified without acquisition: {output}")
    else:
        archive = acquire_archive(
            cache_dir=args.cache_dir.resolve(),
            source=args.source,
            timeout=args.timeout,
        )
        entries = materialize_assets(archive, output, manifest)
    print(
        f"Ready: {output} ({len(entries)} files, "
        f"{sum(entry.size for entry in entries)} bytes)"
    )
    print(f"Manifest: {manifest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssetError, OSError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
