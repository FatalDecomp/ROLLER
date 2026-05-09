#!/usr/bin/env python3
"""Static seam checks for issue #142.

These checks pin the architectural dependency we are changing: frontend car
previews must receive an explicit SceneRenderer and must not fall back to
legacy software rasterization through g_pGameRenderer == NULL.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text()


def extract_function(source: str, name: str) -> str:
    match = re.search(rf"^[\w\s\*]+\b{name}\s*\([^)]*\)\s*\{{", source, re.M)
    if not match:
        raise AssertionError(f"function {name} not found")
    start = match.start()
    brace = source.find("{", match.end() - 1)
    depth = 0
    for pos in range(brace, len(source)):
        ch = source[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError(f"function {name} body not closed")


def assert_true(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def main() -> int:
    for rel in [
        "PROJECTS/ROLLER/scene_render.h",
        "PROJECTS/ROLLER/scene_render.c",
        "PROJECTS/ROLLER/scene_render_software.h",
        "PROJECTS/ROLLER/scene_render_software.c",
    ]:
        assert_true((ROOT / rel).exists(), f"missing {rel}")

    func3_h = read("PROJECTS/ROLLER/func3.h")
    assert_true(
        "SceneRenderer *" in func3_h and "DrawCar(" in func3_h,
        "DrawCar must take an explicit SceneRenderer * in func3.h",
    )

    func3 = read("PROJECTS/ROLLER/func3.c")
    draw_car = extract_function(func3, "DrawCar")
    for forbidden in ["g_pGameRenderer", "subdivide(", "POLYTEX("]:
        assert_true(forbidden not in draw_car, f"DrawCar still uses {forbidden}")
    # POLYFLAT is allowed elsewhere in func3.c, but not in the preview car path.
    assert_true("POLYFLAT(" not in draw_car, "DrawCar still uses POLYFLAT directly")

    winner_screen = extract_function(func3, "winner_screen")
    assert_true("SceneRenderer *" in winner_screen, "winner_screen must own an explicit SceneRenderer")
    assert_true("DrawCar(scene" in winner_screen, "winner_screen must pass its SceneRenderer to DrawCar")

    menu_sw = read("PROJECTS/ROLLER/menu_render_software.c")
    assert_true("SceneRenderer *scene" in menu_sw, "software menu renderer must own a SceneRenderer")
    assert_true("scene_render_set_target" in menu_sw, "menu car preview must set an explicit scene target")
    assert_true("DrawCar(sw->scene" in menu_sw, "menu car preview must pass SceneRenderer to DrawCar")
    assert_true("DrawCar(scrbuf +" not in menu_sw, "menu car preview still uses magic pointer offset")

    game_render = read("PROJECTS/ROLLER/game_render.c")
    assert_true("SceneRenderer *scene" in game_render, "game renderer must own a SceneRenderer")
    assert_true("scene_render_quad_world_legacy" in game_render, "game renderer must delegate world quads to scene_render")

    set_projection = extract_function(game_render, "game_render_set_projection")
    assert_true(
        "scene_render_set_target" not in set_projection,
        "game_render_set_projection must not bind the scene target implicitly",
    )
    assert_true(
        "game_render_set_target" in game_render,
        "game renderer must expose an explicit target binding API",
    )

    game_render_sw_h = read("PROJECTS/ROLLER/game_render_software.h")
    game_render_sw = read("PROJECTS/ROLLER/game_render_software.c")
    for forbidden in [
        "game_render_sw_quad_world",
        "game_render_sw_quad_world_subdivide_type",
        "game_render_sw_subdivide_view_quad",
    ]:
        assert_true(forbidden not in game_render_sw_h, f"{forbidden} still exposed by game_render_software.h")
        assert_true(forbidden not in game_render_sw, f"{forbidden} still implemented by game_render_software.c")
    assert_true("subdivide(" not in game_render_sw, "game_render_software still owns direct subdivide dispatch")

    drawtrk3_h = read("PROJECTS/ROLLER/drawtrk3.h")
    drawtrk3 = read("PROJECTS/ROLLER/drawtrk3.c")
    car = read("PROJECTS/ROLLER/car.c")
    scene_render_sw = read("PROJECTS/ROLLER/scene_render_software.c")
    assert_true(
        not re.search(r"\bvoid\s+subdivide\s*\(", drawtrk3_h),
        "drawtrk3.h must not expose subdivide",
    )
    assert_true(
        not re.search(r"^void\s+subdivide\s*\(", drawtrk3, re.M),
        "drawtrk3.c must not define public subdivide",
    )
    assert_true("subdivide(" not in car, "car.c must not call subdivide directly")
    assert_true("POLYTEX(" not in car, "car.c must not use POLYTEX fallback directly")
    assert_true("POLYFLAT(" not in car, "car.c must not use POLYFLAT fallback directly")
    assert_true(
        re.search(r"^static\s+void\s+subdivide\s*\(", scene_render_sw, re.M),
        "scene_render_software.c must own static subdivide",
    )

    build_zig = read("build.zig")
    assert_true(
        "scene_render_seam_check.py" in build_zig,
        "scene_render_seam_check.py must be enforced by zig build",
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"scene_render_seam_check: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
