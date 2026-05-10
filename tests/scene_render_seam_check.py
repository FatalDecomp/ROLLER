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


def extract_case(source: str, label: str, end_label: str) -> str:
    start = source.find(label)
    if start < 0:
        raise AssertionError(f"case label {label} not found")
    end = source.find(end_label, start)
    if end < 0:
        raise AssertionError(f"case end label {end_label} not found after {label}")
    return source[start:end]


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

    game_render_h = read("PROJECTS/ROLLER/game_render.h")
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
    assert_true("typedef struct GameRenderCarPose" in game_render_h, "public API must name the grouped car pose")
    assert_true("typedef struct GameRenderCarOptions" in game_render_h, "public API must name grouped car options")
    assert_true("tVec3 position" in game_render_h, "car pose position field must use idiomatic C naming")
    assert_true("int anim_frame" in game_render_h, "car options animation field must use idiomatic C naming")
    assert_true("const GameRenderCarPose *pose" in game_render_h, "game_render_draw_car must receive a grouped pose")
    assert_true("const GameRenderCarOptions *options" in game_render_h, "game_render_draw_car must receive grouped options")
    assert_true("worldX" not in game_render_h and "animFrame" not in game_render_h,
                "game_render_draw_car must not expose raw/camelCase car render arguments")

    assert_true("game_render_draw_horizon" not in game_render_h, "public API must replace game_render_draw_horizon")
    assert_true("game_render_draw_sky(GameRenderer *renderer," in game_render_h, "public API must expose game_render_draw_sky")
    assert_true(
        "const GameRenderCamera *camera" in game_render_h and "const GameRenderProjection *projection" in game_render_h,
        "game_render_draw_sky must receive explicit camera/projection",
    )
    draw_sky = extract_function(game_render, "game_render_draw_sky")
    assert_true(
        "game_render_sw_draw_sky(renderer->sw, camera, projection)" in draw_sky,
        "game_render_draw_sky must pass explicit camera/projection to software backend",
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
    assert_true("game_render_sw_draw_horizon" not in game_render_sw_h, "software API must replace draw_horizon")
    assert_true("game_render_sw_draw_horizon" not in game_render_sw, "software backend must replace draw_horizon")
    assert_true("game_render_sw_draw_sky(GameRendererSoftware *sw," in game_render_sw_h, "software API must expose draw_sky")
    assert_true(
        "const GameRenderCamera *camera" in game_render_sw_h and "const GameRenderProjection *projection" in game_render_sw_h,
        "software draw_sky must receive camera/projection",
    )
    sw_draw_sky = extract_function(game_render_sw, "game_render_sw_draw_sky")
    assert_true("game_render_sw_set_camera(sw, camera)" in sw_draw_sky, "software sky draw must install supplied camera before DrawHorizon")
    assert_true("game_render_sw_set_projection(sw, projection)" in sw_draw_sky, "software sky draw must install supplied projection before DrawHorizon")

    assert_true("const GameRenderCarPose *pose" in game_render_sw_h,
                "software car draw must receive grouped public pose")
    assert_true("const GameRenderCarOptions *options" in game_render_sw_h,
                "software car draw must receive grouped public options")
    sw_draw_car = extract_function(game_render_sw, "game_render_sw_draw_car")
    for used in ["pose", "options"]:
        assert_true(f"(void){used}" not in sw_draw_car, f"game_render_sw_draw_car still discards {used}")
    assert_true("DisplayCarWithPose" in sw_draw_car, "software car draw must pass explicit pose to legacy car renderer")

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
    car_h = read("PROJECTS/ROLLER/car.h")
    assert_true("typedef struct CarRenderPose" in car_h,
                "legacy car API must name the grouped render pose")
    assert_true("typedef struct CarRenderOptions" in car_h,
                "legacy car API must name grouped render options")
    assert_true("tVec3 position" in car_h and "int anim_frame" in car_h,
                "legacy grouped car render fields must use idiomatic C naming")
    assert_true("const CarRenderPose *pose" in car_h,
                "DisplayCarWithPose must receive grouped car pose")
    assert_true("const CarRenderOptions *options" in car_h,
                "DisplayCarWithPose must receive grouped car options")
    display_car_pose_start = car.find("void DisplayCarWithPose")
    assert_true(display_car_pose_start >= 0, "car.c must define DisplayCarWithPose")
    display_car_pose_end = car.find("\nvoid DisplayCar(", display_car_pose_start)
    assert_true(display_car_pose_end > display_car_pose_start, "DisplayCar wrapper must remain separate from DisplayCarWithPose")
    display_car_pose = car[display_car_pose_start:display_car_pose_end]
    for forbidden_pose_read in ["pCar->pos", "pCar->nYaw", "pCar->nPitch", "pCar->nRoll", "pCar->byWheelAnimationFrame"]:
        assert_true(forbidden_pose_read not in display_car_pose,
                    f"DisplayCarWithPose still reads pose from Car[] via {forbidden_pose_read}")
    assert_true(
        re.search(r"^static\s+void\s+subdivide\s*\(", scene_render_sw, re.M),
        "scene_render_software.c must own static subdivide",
    )

    assert_true(
        not re.search(r"\bdodivide\s*\(", drawtrk3_h),
        "drawtrk3.h must not expose dodivide",
    )
    assert_true(
        not re.search(r"^void\s+dodivide\s*\(", drawtrk3, re.M),
        "drawtrk3.c must not define public dodivide",
    )
    assert_true(
        re.search(r"^static\s+void\s+dodivide\s*\(", scene_render_sw, re.M),
        "scene_render_software.c must own static dodivide",
    )


    building = read("PROJECTS/ROLLER/building.c")
    draw_building = extract_function(building, "DrawBuilding")
    assert_true(
        "game_render_quad_screen" not in draw_building,
        "DrawBuilding must route building scene geometry through game_render_quad_world, not screen-space overlay quads",
    )
    assert_true(
        "game_render_quad_world" in draw_building,
        "DrawBuilding must submit building polygons through the world-space scene seam",
    )
    assert_true(
        "(float)BuildingSub[uiBuildingType] * subscale" in draw_building,
        "DrawBuilding must preserve BuildingSub[uiBuildingType] * subscale subdivision threshold",
    )

    scene_quad = extract_function(scene_render_sw, "scene_render_sw_quad_world_legacy")
    assert_true(
        "float directVz[4]" in scene_quad and "directVz[i] = (float)iVz[i];" in scene_quad,
        "building world-quads must keep unclamped view Z for subdivision threshold decisions",
    )
    assert_true(
        "options.subThreshold > 0.0f || subpolyType == SUBPOLY_BUILDING" in scene_quad,
        "building zero subdivision thresholds must remain valid direct-render thresholds",
    )

    assert_true(
        "const GameRenderCamera *camera" in drawtrk3_h
        and "const GameRenderProjection *projection" in drawtrk3_h,
        "DrawTrack3 must receive camera/projection explicitly",
    )
    assert_true("LightXYZ" not in drawtrk3_h, "LightXYZ must not be exposed by drawtrk3.h")
    assert_true("cube_faces" not in drawtrk3_h, "cube_faces must not be exposed by drawtrk3.h")
    assert_true("LightXYZ" not in drawtrk3, "start-light cube must not use LightXYZ screen/projection cache")
    assert_true("int cube_faces" not in drawtrk3, "cube face topology must not be a drawtrk3 global")
    assert_true(
        "draw_start_light_cube_world" in drawtrk3,
        "start-light cube rendering must be isolated in a helper",
    )
    start_light_case = extract_case(drawtrk3, "case 0xE:", "default:")
    assert_true(
        "game_render_quad_screen" not in start_light_case,
        "case 0xE must not submit screen-space quads",
    )
    start_light_helper = extract_function(drawtrk3, "draw_start_light_cube_world")
    assert_true(
        "game_render_quad_world" in start_light_helper,
        "start-light cube helper must submit world-space quads",
    )
    assert_true(
        "game_render_quad_screen" not in start_light_helper,
        "start-light cube helper must not submit screen-space quads",
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
