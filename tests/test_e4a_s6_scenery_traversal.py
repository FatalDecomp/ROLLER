import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
ADR = ROOT / "docs" / "adr" / "0005-camera-independent-scenery-traversal.md"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


class SceneryTraversalContractTests(unittest.TestCase):
    def test_the_producer_walks_every_placed_object_in_index_order(self) -> None:
        header = (ROLLER / "drawtrk3.h").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        surface = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("bool drawtrk3_emit_full_scenery(", header)
        self.assertIn(
            "ed_traverse_full_scenery_objects(\n"
            "        (uint32_t)NumBuildings, emit_full_scenery_object, &Context)",
            draw,
        )
        traversal = function_body(surface, "static bool ed_traverse_indices(")
        self.assertIn("uiIndex = 0", traversal)
        self.assertIn("uiIndex < uiCount", traversal)
        self.assertIn("uiIndex++", traversal)

    def test_the_scenery_path_reads_nothing_the_camera_moves(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        producer = without_comments(
            function_body(draw, "static bool emit_full_scenery_object(")
        )
        traversal = without_comments(
            function_body(draw, "bool drawtrk3_emit_full_scenery(")
        )
        transform = without_comments(
            function_body(building, "uint32 building_transform_plan_coords(")
        )
        identity = without_comments(
            function_body(building, "bool building_polygon_surface_info(")
        )
        combined = producer + traversal + identity

        for forbidden in (
            "start_sect",
            "TrackSize",
            "VisibleBuildings",
            "NumVisibleBuildings",
            "BuildingZOrder",
            "bldZcmp",
            "facing_ok",
            "viewx",
            "viewy",
            "viewz",
            "worlddirn",
            "render_queue_3d",
            "GameRenderCamera",
            "GameRenderProjection",
        ):
            self.assertNotIn(forbidden, combined)

        # The shared transform is the one place worlddirn survives, and only
        # behind the render-path yaw source.
        self.assertIn("BUILDING_YAW_RENDER", transform)
        self.assertIn("worlddirn", transform)
        self.assertIn("BUILDING_YAW_AUTHORED", producer)
        self.assertNotIn("BUILDING_YAW_RENDER", producer)

    def test_render_toggles_reach_the_renderer_but_not_the_export(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")

        # Same rule emit_track_chunk_surface follows: true for the renderer,
        # false for the canonical stream.
        self.assertIn("(int)uiBuildingIdx, pPolygon, false, &Info", draw)
        self.assertIn("building_polygon_surface_info(iBuildingIdx, pPolygon, true,", building)

    def test_the_renderer_and_the_traversal_share_one_transform(self) -> None:
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        drawbuilding = function_body(building, "void DrawBuilding(")

        # DrawBuilding must not carry its own copy of either shared step.
        self.assertIn("building_transform_plan_coords(", drawbuilding)
        self.assertIn("building_polygon_surface_info(", drawbuilding)
        self.assertNotIn("BuildingAngles[3 * iBuildingIdx", drawbuilding)
        self.assertNotIn("advert_list[", drawbuilding)
        self.assertEqual(building.count("advert_list[iBuildingIdx]"), 1)
        self.assertEqual(draw.count("building_transform_plan_coords("), 1)

    def test_runtime_scenery_never_reaches_the_canonical_stream(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        producer = function_body(draw, "static bool emit_full_scenery_object(")

        self.assertIn(
            "Info.unContentClass == ROLLER_ED_CONTENT_RUNTIME_SCENERY",
            without_comments(producer),
        )
        # Towers are runtime sprites built from the view basis, so there is no
        # authored geometry to walk: no producer touches them.
        for body in (
            producer,
            function_body(draw, "bool drawtrk3_emit_full_scenery("),
            function_body(draw, "bool drawtrk3_emit_full_track("),
        ):
            self.assertNotIn("Tower", body)

    def test_authored_vertex_order_is_emitted_without_the_draw_time_flip(
        self,
    ) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        producer = without_comments(
            function_body(draw, "static bool emit_full_scenery_object(")
        )

        # The producer indexes the plan's own verts[] directly.
        self.assertIn("pPolygon->verts[v]", producer)
        self.assertNotIn("isBackFace", producer)
        self.assertNotIn("iOrder", producer)
        # ...while the render path keeps the flip it has always had.
        self.assertIn("iOrder[0] = iV3;", building)

    def test_two_sidedness_survives_the_advert_texture_substitution(
        self,
    ) -> None:
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        identity = without_comments(
            function_body(building, "bool building_polygon_surface_info(")
        )

        # The cull test reads the plan's uiTex, so the plan's flag is what
        # decides. Reading it after the advert substitution would call every
        # retail balloon single-sided.
        self.assertIn(
            "isTwoSided = (pPolygon->uiTex & SURFACE_FLAG_FLIP_BACKFACE) != 0",
            identity,
        )
        self.assertLess(
            identity.index("isTwoSided ="),
            identity.index("uiTex = advert_list[iBuildingIdx]"),
        )
        # Published as additive information, never by putting the flag back
        # into the renderer's own word.
        self.assertIn(
            "pInfo->unFlags |= ROLLER_ED_SURFACE_FLAG_TWO_SIDED", identity
        )
        self.assertNotIn("uiTex |= SURFACE_FLAG_FLIP_BACKFACE", identity)
        self.assertNotIn(
            "gpuSurfFlags |= SURFACE_FLAG_FLIP_BACKFACE", identity
        )

    def test_an_unresolvable_tile_drops_one_surface_not_the_traversal(
        self,
    ) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        producer = function_body(draw, "static bool emit_full_scenery_object(")

        self.assertIn("bool ed_surface_material_resolvable(", header)
        self.assertIn(
            "if (!ed_surface_material_resolvable(pContext->pMaterials, &Info))\n"
            "            continue;",
            producer,
        )

    def test_both_producers_run_in_the_probe_and_the_fill(self) -> None:
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        emit_all = function_body(scene, "static bool editor_geometry_emit_all(")

        self.assertIn("drawtrk3_emit_full_track(", emit_all)
        self.assertIn("drawtrk3_emit_full_scenery(", emit_all)
        # Neither pass may call a producer directly, or the two could diverge.
        self.assertEqual(scene.count("drawtrk3_emit_full_track("), 1)
        self.assertEqual(scene.count("drawtrk3_emit_full_scenery("), 1)
        self.assertEqual(scene.count("editor_geometry_emit_all("), 3)
        self.assertIn("MAX_VISIBLE_BUILDINGS", scene)

    def test_the_conventions_are_recorded_in_a_superseding_adr(self) -> None:
        self.assertTrue(ADR.is_file(), f"missing ADR: {ADR}")
        adr = ADR.read_text(encoding="utf-8")
        self.assertIn("**Status:** Accepted", adr)
        self.assertIn("0003-canonical-geometry-conventions.md", adr)

        older = (
            ROOT / "docs" / "adr" / "0003-canonical-geometry-conventions.md"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "0005-camera-independent-scenery-traversal.md", older
        )
        self.assertIn("superseded by ADR 0005", older)


if __name__ == "__main__":
    unittest.main()
