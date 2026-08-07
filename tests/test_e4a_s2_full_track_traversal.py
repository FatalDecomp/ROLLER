import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"


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


class FullTrackTraversalContractTests(unittest.TestCase):
    def test_traversal_visits_the_loaded_range_in_canonical_order(self) -> None:
        header = (ROLLER / "drawtrk3.h").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        surface = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("bool drawtrk3_emit_full_track(", header)
        self.assertIn(
            "ed_traverse_full_track_chunks(\n"
            "        (uint32_t)TRAK_LEN, emit_full_track_chunk, &Context)",
            draw,
        )
        # E4A-S6 gave the scenery walk the same shape, so the ordered
        # stop-on-refusal loop lives in one place and both inherit it.
        self.assertIn(
            "ed_traverse_indices(uiLoadedChunkCount, pfnVisit, pUserData)",
            function_body(surface, "bool ed_traverse_full_track_chunks("),
        )
        traversal = function_body(surface, "static bool ed_traverse_indices(")
        self.assertIn("uiIndex = 0", traversal)
        self.assertIn("uiIndex < uiCount", traversal)
        self.assertIn("uiIndex++", traversal)
        self.assertIn("return false", traversal)

    def test_full_track_path_is_camera_visibility_and_batch_independent(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        producer = without_comments(
            function_body(draw, "static bool emit_track_chunk_surface(")
        )
        traversal = without_comments(
            function_body(draw, "bool drawtrk3_emit_full_track(")
        )
        combined = producer + traversal

        for forbidden in (
            "start_sect",
            "TrackSize",
            "TrackScreenXYZ",
            "GroundScreenXYZ",
            "facing_ok",
            "viewx",
            "viewy",
            "viewz",
            "render_queue_3d",
            "GameRenderCamera",
            "GameRenderProjection",
        ):
            self.assertNotIn(forbidden, combined)

        self.assertIn(
            "uiChunkId, aunSurfaceClasses[i], false", draw
        )
        self.assertIn(
            "uiChunkId, unSurfaceClass, true", draw
        )

    def test_every_chunk_surface_class_uses_the_shared_producer(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        full_chunk = function_body(draw, "static bool emit_full_track_chunk(")

        for surface_class in (
            "CENTER",
            "LEFT_SHOULDER",
            "RIGHT_SHOULDER",
            "LEFT_WALL",
            "RIGHT_WALL",
            "ROOF",
            "OUTER_WALL_FLOOR",
            "LEFT_LOWER_OUTER_WALL",
            "RIGHT_LOWER_OUTER_WALL",
            "LEFT_UPPER_OUTER_WALL",
            "RIGHT_UPPER_OUTER_WALL",
        ):
            self.assertIn(
                f"ROLLER_ED_SURFACE_CLASS_{surface_class}", full_chunk
            )

        self.assertIn("emit_track_chunk_surface(", full_chunk)
        self.assertGreaterEqual(
            draw.count("emit_track_chunk_surface_to_renderer("), 12
        )
        self.assertEqual(draw.count("ed_emit_surface("), 1)

    def test_ground_world_geometry_does_not_read_projection_cache(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        body = function_body(draw, "static const tVec3 *ground_world_point(")
        self.assertNotIn("TrackScreenXYZ", body)
        self.assertIn("GroundColour", body)
        self.assertIn("TrakPt", body)
        self.assertIn("GroundPt", body)


if __name__ == "__main__":
    unittest.main()
