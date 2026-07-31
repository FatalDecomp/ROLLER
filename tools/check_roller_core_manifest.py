#!/usr/bin/env python3
"""Validate the E0-S1 roller-core translation-unit partition."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
import sys


CATEGORIES = (
    "EXCLUDE",
    "STUB_SWAP",
    "KEEP",
    "PRESENT_BUT_DORMANT",
)


class ManifestError(ValueError):
    pass


def load_manifest(manifest_path: Path, repository_root: Path) -> dict[str, str]:
    entries: dict[str, str] = {}

    try:
        lines = manifest_path.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise ManifestError(f"cannot read {manifest_path}: {error}") from error

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        fields = line.split("|")
        if len(fields) != 2:
            raise ManifestError(
                f"{manifest_path}:{line_number}: expected CATEGORY|path"
            )

        category, source_text = (field.strip() for field in fields)
        if category not in CATEGORIES:
            raise ManifestError(
                f"{manifest_path}:{line_number}: unknown category {category!r}"
            )

        source_path = PurePosixPath(source_text)
        if (
            source_path.is_absolute()
            or ".." in source_path.parts
            or source_path.parent != PurePosixPath("PROJECTS/ROLLER")
            or source_path.suffix != ".c"
        ):
            raise ManifestError(
                f"{manifest_path}:{line_number}: invalid source path {source_text!r}"
            )

        normalized_path = source_path.as_posix()
        if normalized_path in entries:
            raise ManifestError(
                f"{manifest_path}:{line_number}: duplicate source {normalized_path}"
            )
        if not (repository_root / Path(*source_path.parts)).is_file():
            raise ManifestError(
                f"{manifest_path}:{line_number}: source does not exist: "
                f"{normalized_path}"
            )

        entries[normalized_path] = category

    if not entries:
        raise ManifestError(f"{manifest_path}: manifest contains no source entries")

    return entries


def validate_manifest(manifest_path: Path, repository_root: Path) -> Counter[str]:
    entries = load_manifest(manifest_path, repository_root)
    source_directory = repository_root / "PROJECTS" / "ROLLER"
    actual_sources = {
        source_path.relative_to(repository_root).as_posix()
        for source_path in source_directory.glob("*.c")
        if source_path.is_file()
    }
    manifest_sources = set(entries)

    missing = sorted(actual_sources - manifest_sources)
    stale = sorted(manifest_sources - actual_sources)
    errors: list[str] = []
    if missing:
        errors.append("unclassified translation units:\n  " + "\n  ".join(missing))
    if stale:
        errors.append("manifest entries without source files:\n  " + "\n  ".join(stale))

    for source_path, category in entries.items():
        if category != "STUB_SWAP":
            continue
        source = PurePosixPath(source_path)
        if not source.stem.endswith("_stub"):
            errors.append(f"STUB_SWAP source lacks _stub suffix: {source_path}")
            continue
        implementation = source.with_name(source.stem.removesuffix("_stub") + ".c")
        implementation_path = implementation.as_posix()
        if entries.get(implementation_path) != "EXCLUDE":
            errors.append(
                f"STUB_SWAP source {source_path} must replace an EXCLUDE entry "
                f"at {implementation_path}"
            )

    if errors:
        raise ManifestError("\n".join(errors))

    return Counter(entries.values())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    default_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=default_root,
        help="ROLLER checkout root (defaults to the parent of tools)",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="manifest path (defaults to <repository-root>/roller-core.srclist)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository_root = args.repository_root.resolve()
    manifest_path = (
        args.manifest.resolve()
        if args.manifest is not None
        else repository_root / "roller-core.srclist"
    )

    try:
        category_counts = validate_manifest(manifest_path, repository_root)
    except ManifestError as error:
        print(f"roller-core manifest check failed: {error}", file=sys.stderr)
        return 1

    total = sum(category_counts.values())
    summary = ", ".join(
        f"{category}={category_counts[category]}" for category in CATEGORIES
    )
    print(f"roller-core manifest check passed: {total} translation units ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
