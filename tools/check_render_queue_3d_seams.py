#!/usr/bin/env python3
"""Pin migrated render_queue_3d command seams.

Cars, buildings, start lights, and road/lane surfaces have migrated away from raw legacy
priority submission. Keep priorities 5, 6, 7, 11, 13, and 14 out of drawtrk3's direct
writes and out of the temporary unmigrated-priority compatibility path.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DRAWTRK3 = ROOT / "PROJECTS" / "ROLLER" / "drawtrk3.c"
ROLLER_SRC = ROOT / "PROJECTS" / "ROLLER"

MIGRATED_PRIORITIES = {
    5: "road-center",
    6: "left-lane",
    7: "right-lane",
    11: "car",
    13: "building",
    14: "start-light",
}

REQUIRED_DRAWTRK3_APIS = {
    "render_queue_3d_add_road_center": "road center",
    "render_queue_3d_add_left_lane": "left lane",
    "render_queue_3d_add_right_lane": "right lane",
    "render_queue_3d_add_car": "cars",
    "render_queue_3d_add_building": "buildings",
    "render_queue_3d_add_start_light": "start lights",
}


def fail(message: str) -> int:
    print(f"render_queue_3d seam check failed: {message}", file=sys.stderr)
    return 1


def direct_priority_write_pattern(priority: int) -> re.Pattern[str]:
    return re.compile(rf"\.nRenderPriority\s*=\s*{priority}\b|->nRenderPriority\s*=\s*{priority}\b")


def compatibility_call_pattern(function_name: str, priority: int) -> re.Pattern[str]:
    return re.compile(rf"{function_name}\s*\([^;]*\b{priority}\b", re.DOTALL)


def main() -> int:
    drawtrk3 = DRAWTRK3.read_text(encoding="utf-8")

    for priority, name in MIGRATED_PRIORITIES.items():
        if direct_priority_write_pattern(priority).search(drawtrk3):
            return fail(f"drawtrk3.c writes {name} legacy priority {priority} directly")

    for api_name, label in REQUIRED_DRAWTRK3_APIS.items():
        if api_name not in drawtrk3:
            return fail(f"drawtrk3.c does not submit {label} through {api_name}")

    if "render_queue_3d_set_legacy_count" in drawtrk3:
        return fail("drawtrk3.c still syncs raw TrackView writes with render_queue_3d_set_legacy_count")

    if re.search(r"\bnum_bits\b", drawtrk3):
        return fail("drawtrk3.c still maintains num_bits instead of querying render_queue_3d_count")

    if "render_queue_3d_add_legacy_priority" in drawtrk3:
        return fail("drawtrk3.c still uses old legacy-priority compatibility API name")

    for path in ROLLER_SRC.glob("*.c"):
        if path.name == "render_queue_3d.c":
            continue
        text = path.read_text(encoding="utf-8")
        for priority, name in MIGRATED_PRIORITIES.items():
            if compatibility_call_pattern("render_queue_3d_add_unmigrated_legacy_priority", priority).search(text):
                return fail(
                    f"{path.relative_to(ROOT)} submits migrated {name} priority {priority} "
                    "through unmigrated compatibility API"
                )
            if compatibility_call_pattern("render_queue_3d_add_legacy_priority", priority).search(text):
                return fail(
                    f"{path.relative_to(ROOT)} submits migrated {name} priority {priority} "
                    "through old compatibility API"
                )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
