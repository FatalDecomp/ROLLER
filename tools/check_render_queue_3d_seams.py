#!/usr/bin/env python3
"""Pin migrated render_queue_3d command seams.

Cars, buildings, and start lights have migrated away from raw legacy priority submission.
Keep priorities 11, 13, and 14 out of drawtrk3's direct writes and out of the temporary
legacy-priority compatibility path.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DRAWTRK3 = ROOT / "PROJECTS" / "ROLLER" / "drawtrk3.c"


def fail(message: str) -> int:
    print(f"render_queue_3d seam check failed: {message}", file=sys.stderr)
    return 1


def main() -> int:
    drawtrk3 = DRAWTRK3.read_text(encoding="utf-8")

    if re.search(r"\.nRenderPriority\s*=\s*11\b|->nRenderPriority\s*=\s*11\b", drawtrk3):
        return fail("drawtrk3.c writes car legacy priority 11 directly")

    if re.search(r"\.nRenderPriority\s*=\s*13\b|->nRenderPriority\s*=\s*13\b", drawtrk3):
        return fail("drawtrk3.c writes building legacy priority 13 directly")

    if re.search(r"\.nRenderPriority\s*=\s*14\b|->nRenderPriority\s*=\s*14\b", drawtrk3):
        return fail("drawtrk3.c writes start-light legacy priority 14 directly")

    if "render_queue_3d_add_building" not in drawtrk3:
        return fail("drawtrk3.c does not submit buildings through render_queue_3d_add_building")

    if "render_queue_3d_add_start_light" not in drawtrk3:
        return fail("drawtrk3.c does not submit start lights through render_queue_3d_add_start_light")

    if "render_queue_3d_add_car" not in drawtrk3:
        return fail("drawtrk3.c does not submit cars through render_queue_3d_add_car")

    legacy_named_priority = re.compile(r"render_queue_3d_add_legacy_priority\s*\([^;]*\b(?:11|13|14)\b", re.DOTALL)
    for path in (ROOT / "PROJECTS" / "ROLLER").glob("*.c"):
        if path.name == "render_queue_3d.c":
            continue
        text = path.read_text(encoding="utf-8")
        if legacy_named_priority.search(text):
            return fail(f"{path.relative_to(ROOT)} submits priority 11, 13, or 14 through legacy compatibility API")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
