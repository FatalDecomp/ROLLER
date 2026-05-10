#!/usr/bin/env python3
"""Pin migrated render_queue_3d command seams.

Cars, buildings, start lights, road/lane surfaces, wall/lower-wall surfaces, and ground/roof
surfaces have migrated away from raw legacy priority submission. Keep priorities 0, 1, 2,
3, 4, 5, 6, 7, 8, 9, 10, 11, 13, and 14 out of drawtrk3's direct writes and out of the
temporary unmigrated-priority compatibility path.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DRAWTRK3 = ROOT / "PROJECTS" / "ROLLER" / "drawtrk3.c"
FRAME_SRC = ROOT / "PROJECTS" / "ROLLER" / "3d.c"
RENDER_QUEUE_SRC = ROOT / "PROJECTS" / "ROLLER" / "render_queue_3d.c"
RENDER_QUEUE_HDR = ROOT / "PROJECTS" / "ROLLER" / "render_queue_3d.h"
SNAPSHOT_SRC = ROOT / "PROJECTS" / "ROLLER" / "snapshot.c"
SNAPSHOT_HDR = ROOT / "PROJECTS" / "ROLLER" / "snapshot.h"
ROLLER_SRC = ROOT / "PROJECTS" / "ROLLER"

MIGRATED_PRIORITIES = {
    0: "left-wall",
    1: "right-wall",
    2: "ground",
    3: "left-lower-wall",
    4: "right-lower-wall",
    5: "road-center",
    6: "left-lane",
    7: "right-lane",
    8: "left-high-wall",
    9: "right-high-wall",
    10: "roof",
    11: "car",
    13: "building",
    14: "start-light",
}

REQUIRED_DRAWTRK3_APIS = {
    "render_queue_3d_add_left_wall": "left wall",
    "render_queue_3d_add_right_wall": "right wall",
    "render_queue_3d_add_left_lower_wall": "left lower wall",
    "render_queue_3d_add_right_lower_wall": "right lower wall",
    "render_queue_3d_add_left_high_wall": "left high wall",
    "render_queue_3d_add_right_high_wall": "right high wall",
    "render_queue_3d_add_ground": "ground",
    "render_queue_3d_add_next_section_roof": "next-section roof",
    "render_queue_3d_add_current_section_roof": "current-section roof",
    "render_queue_3d_add_road_center": "road center",
    "render_queue_3d_add_left_lane": "left lane",
    "render_queue_3d_add_right_lane": "right lane",
    "render_queue_3d_add_car": "cars",
    "render_queue_3d_add_building": "buildings",
    "render_queue_3d_add_start_light": "start lights",
}

REQUIRED_FRAME_PHASE_MARKERS = {
    "Gameplay frame phase: camera/projection setup": "camera/projection setup phase",
    "Gameplay frame phase: atmosphere": "atmosphere phase",
    "Gameplay frame phase: visibility/entity production": "visibility/entity production phase",
    "Gameplay frame phase: 3D queue production and sorted dispatch": "3D queue production/dispatch phase",
    "Gameplay frame phase: capture/present": "capture/present phase",
}

REQUIRED_DRAWTRACK_PHASE_MARKERS = {
    "RenderQueue3D *pRenderQueue3D = render_queue_3d_global();": "local render queue pointer",
    "Gameplay 3D queue phase: project visible track geometry": "track projection phase",
    "Gameplay 3D queue phase: produce world commands": "queue production phase",
    "Gameplay 3D queue phase: sorted dispatch": "sorted dispatch phase",
}

GAMEPLAY_3D_RENDER_QUEUE_FILES = {
    "drawtrk3.c",
    "render_queue_3d.c",
    "render_queue_3d.h",
}

REQUIRED_SNAPSHOT_RECORD_MARKERS = {
    "SnapshotApplyFixedRecords();": "snapshot-mode record fixture application",
    "if (!g_bSnapshotMode) SaveRecords();": "snapshot-mode SaveRecords guard",
    "void SnapshotApplyFixedRecords(void)": "fixed snapshot record implementation",
    "SnapshotRecordFixture": "fixed snapshot record fixture data",
    "HUMAN": "baseline menu-select-track record holder",
    "6127": "baseline menu-select-track record lap centiseconds",
}


def fail(message: str) -> int:
    print(f"render_queue_3d seam check failed: {message}", file=sys.stderr)
    return 1


def direct_priority_write_pattern() -> re.Pattern[str]:
    return re.compile(r"\.nRenderPriority\s*=|->nRenderPriority\s*=")


def compatibility_call_pattern(function_name: str, priority: int) -> re.Pattern[str]:
    return re.compile(rf"{function_name}\s*\([^;]*\b{priority}\b", re.DOTALL)


def main() -> int:
    drawtrk3 = DRAWTRK3.read_text(encoding="utf-8")
    frame_src = FRAME_SRC.read_text(encoding="utf-8")
    snapshot_src = SNAPSHOT_SRC.read_text(encoding="utf-8")
    snapshot_hdr = SNAPSHOT_HDR.read_text(encoding="utf-8")
    render_queue_src = RENDER_QUEUE_SRC.read_text(encoding="utf-8")
    render_queue_hdr = RENDER_QUEUE_HDR.read_text(encoding="utf-8")

    if direct_priority_write_pattern().search(drawtrk3):
        return fail("drawtrk3.c writes legacy render priorities directly")

    for api_name, label in REQUIRED_DRAWTRK3_APIS.items():
        if api_name not in drawtrk3:
            return fail(f"drawtrk3.c does not submit {label} through {api_name}")

    if "render_queue_3d_set_legacy_count" in drawtrk3:
        return fail("drawtrk3.c still syncs raw TrackView writes with render_queue_3d_set_legacy_count")

    if re.search(r"\bnum_bits\b", drawtrk3):
        return fail("drawtrk3.c still maintains num_bits instead of querying render_queue_3d_count")

    if "TrackView" in drawtrk3:
        return fail("drawtrk3.c must not use the legacy TrackView queue alias")

    if "render_queue_3d_add_legacy_priority" in drawtrk3:
        return fail("drawtrk3.c still uses old legacy-priority compatibility API name")

    if "render_queue_3d_add_unmigrated_legacy_priority" in render_queue_src + render_queue_hdr:
        return fail("temporary unmigrated legacy-priority compatibility API must be removed")

    for marker, label in REQUIRED_FRAME_PHASE_MARKERS.items():
        if marker not in frame_src:
            return fail(f"3d.c does not name the gameplay {label}")

    for marker, label in REQUIRED_DRAWTRACK_PHASE_MARKERS.items():
        if marker not in drawtrk3:
            return fail(f"drawtrk3.c does not name the gameplay {label}")

    if drawtrk3.count("render_queue_3d_global()") != 1:
        return fail("DrawTrack3 should use one local render queue pointer instead of repeated render_queue_3d_global() calls")

    snapshot_text = "\n".join([frame_src, snapshot_src, snapshot_hdr])
    for marker, label in REQUIRED_SNAPSHOT_RECORD_MARKERS.items():
        if marker not in snapshot_text:
            return fail(f"snapshot harness does not pin {label}")

    for path in ROLLER_SRC.glob("*.[ch]"):
        if path.name in GAMEPLAY_3D_RENDER_QUEUE_FILES:
            continue
        text = path.read_text(encoding="utf-8")
        if "render_queue_3d.h" in text or re.search(r"\brender_queue_3d_", text):
            return fail(f"{path.relative_to(ROOT)} reaches into gameplay render_queue_3d")

    save_records_callers = "\n".join(path.read_text(encoding="utf-8") for path in ROLLER_SRC.glob("*.c"))
    if save_records_callers.count("SaveRecords();") != save_records_callers.count("if (!g_bSnapshotMode) SaveRecords();"):
        return fail("snapshot mode must guard every SaveRecords() call")
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
