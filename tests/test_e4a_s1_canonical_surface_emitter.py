import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"


class CanonicalSurfaceEmitterContractTests(unittest.TestCase):
    def test_neutral_callback_carries_complete_surface_state(self):
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")

        self.assertRegex(
            header,
            r"typedef void \(\*tEdEmitSurfaceFn\)\("
            r"const tEdSurfaceEmission \*pSurface,\s*void \*pUserData\);",
        )
        for field in (
            "aVertices",
            "uiFrontMaterialId",
            "uiBackMaterialId",
            "uiChunkId",
            "uiRenderFlags",
            "iRenderSubdivideType",
            "fSubdivideThreshold",
            "unSurfaceClass",
            "unContentClass",
            "unFlags",
            "byTopology",
        ):
            self.assertIn(field, header)
        self.assertIn("iRenderU16_16", header)
        self.assertIn("iRenderV16_16", header)
        self.assertIn("fMaterialUV", header)

    def test_generic_emitter_owns_uv_material_and_skip_decisions(self):
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("bool ed_emit_surface(", header)
        self.assertIn("bool ed_surface_compute_render_uvs(", header)
        self.assertIn("SURFACE_FLAG_SKIP_RENDER", source)
        self.assertIn("ed_build_material", source)
        self.assertIn("uiBackMaterialId", source)
        self.assertNotIn("ed_emit_left_wall_surface", header + source)
        self.assertNotIn("world_verts_", header + source)

    def test_every_canonical_quad_producer_uses_the_callback_consumer(self):
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        tower = (ROLLER / "tower.c").read_text(encoding="utf-8")

        # One definition plus thirteen track/roof emission sites.
        self.assertGreaterEqual(draw.count("emit_track_surface_to_renderer("), 14)
        self.assertEqual(draw.count("game_render_quad_world_subdivide_type("), 1)
        self.assertEqual(building.count("drawtrk3_emit_surface_to_renderer("), 1)
        self.assertEqual(tower.count("drawtrk3_emit_surface_to_renderer("), 1)
        self.assertNotIn("game_render_quad_world_subdivide_type(", building)
        self.assertNotIn("game_render_quad_world(", tower)

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
            self.assertIn(f"ROLLER_ED_SURFACE_CLASS_{surface_class}", draw)

        self.assertIn("ROLLER_ED_SURFACE_CLASS_SIGN", building)
        self.assertIn("ROLLER_ED_CONTENT_AUTHORED_SIGN", building)
        self.assertIn("ROLLER_ED_SURFACE_CLASS_BUILDING", building)
        self.assertIn("ROLLER_ED_SURFACE_CLASS_TOWER", tower)

    def test_legacy_fixed_uv_globals_share_the_canonical_calculator(self):
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        match = re.search(
            r"void set_starts\(unsigned int uiType\)\s*\{(?P<body>.*?)\n\}",
            draw,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("ed_surface_compute_render_uvs", body)
        self.assertNotRegex(body, r"0x[0-9A-Fa-f]+")


if __name__ == "__main__":
    unittest.main()
