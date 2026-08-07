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


class FacadeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        cls.body = without_comments(
            function_body(
                cls.source,
                "eRollerEdResult ROLLER_ED_CALL RollerEd_SetReferenceMesh(",
            )
        )

    def test_the_entry_point_is_no_longer_a_stub(self) -> None:
        self.assertNotIn("ROLLER_ED_RESULT_UNSUPPORTED", self.body)
        self.assertIn("roller_ed_legacy_scene_set_reference_mesh", self.body)

    def test_the_header_is_still_validated_first(self) -> None:
        self.assertIn("roller_ed_validate_struct", self.body)
        self.assertLess(
            self.body.index("roller_ed_validate_struct"),
            self.body.index("roller_ed_legacy_scene_set_reference_mesh"),
        )
        self.assertIn("ROLLER_ED_REFERENCE_MESH_VERSION", self.body)

    def test_it_does_not_move_the_epoch(self) -> None:
        # AD-7d: a reference mesh is the host's scenery, not authored track
        # geometry, so E4A-S5's per-epoch extraction survives a replacement.
        self.assertNotIn("roller_ed_advance_geometry_epoch", self.body)

    def test_the_struct_stayed_at_version_one(self) -> None:
        # The mesh contract was frozen by F-S4a and E3A-S7 only consumes it.
        header = (ROLLER / "editor_api.h").read_text(encoding="utf-8")
        self.assertIn("#define ROLLER_ED_REFERENCE_MESH_VERSION 1u", header)


class StateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_reference_mesh.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROLLER / "editor_reference_mesh.c").read_text(
            encoding="utf-8"
        )

    def test_the_core_owns_exactly_one_mesh(self) -> None:
        for name in (
            "ed_reference_mesh_set_current",
            "ed_reference_mesh_reset_current",
            "ed_reference_mesh_triangle_count",
            "ed_reference_mesh_world_triangle",
            "ed_reference_mesh_wireframe",
        ):
            self.assertIn(name, self.header)
        self.assertIn("static tEdReferenceMeshState s_Current;", self.source)

    def test_replacement_goes_through_the_copy_layer(self) -> None:
        # AD-13: vertex and texture data are both copied during the call, and
        # a failed replacement leaves the previous mesh intact. That contract
        # already lives in ed_reference_mesh_replace, so the singleton must
        # not reimplement it.
        body = without_comments(
            function_body(
                self.source, "eEdReferenceMeshResult ed_reference_mesh_set_current("
            )
        )
        self.assertIn("ed_reference_mesh_replace", body)
        self.assertNotIn("malloc", body)
        self.assertNotIn("memcpy", body)

    def test_the_transform_is_scale_rotate_translate(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_reference_mesh_world_triangle(")
        )
        self.assertIn("fScale", body)
        self.assertIn("fRotation", body)
        self.assertIn("fPosition", body)
        # Scale is applied to the local vertex before the rotation matrix.
        self.assertLess(body.index("afScaled[i] ="), body.index("afRotation[i][0]"))

    def test_every_index_is_bounds_checked(self) -> None:
        body = without_comments(
            function_body(self.source, "bool ed_reference_mesh_world_triangle(")
        )
        self.assertIn("ed_reference_mesh_triangle_count()", body)
        self.assertIn("uiVertexCount", body)
        self.assertIn("return false;", body)

    def test_the_state_module_still_links_without_a_renderer(self) -> None:
        # It is unit-tested standalone, so it must not reach for the renderer.
        self.assertNotIn("game_render", self.source)
        self.assertNotIn("GameRenderer", self.source)


class RenderPassTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        cls.body = without_comments(
            function_body(cls.draw, "void drawtrk3_editor_draw_reference_mesh(")
        )

    def test_the_mesh_has_its_own_flag(self) -> None:
        self.assertIn("ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH", self.body)

    def test_it_composes_by_depth_rather_than_painting_over(self) -> None:
        # The story rejects a QPainter overlay. Going through the same
        # world-quad path as the track is what makes the renderer depth-test
        # the mesh against the scene instead of pasting it on top.
        self.assertIn("draw_helper_quad", self.body)
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        self.assertEqual(
            scene.count("drawtrk3_editor_draw_reference_mesh(g_pGameRenderer);"),
            2,
        )

    def test_wireframe_reuses_the_surface_ribbon(self) -> None:
        self.assertIn("ed_surface_wireframe_edge_quad_points", self.body)
        surface = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        shared = without_comments(
            function_body(
                surface, "bool ed_surface_wireframe_edge_quad_points("
            )
        )
        self.assertIn("ED_WIREFRAME_WIDTH_RATIO", shared)
        self.assertIn("ED_WIREFRAME_DEPTH_BIAS_RATIO", shared)

    def test_the_mesh_keeps_the_legacy_editors_colour(self) -> None:
        # The pre-modernization editor overwrote every reference-model vertex
        # UV with GetColorCenterCoordinates(0x8c), so it was already flat
        # light grey rather than textured.
        self.assertIn(
            "#define ED_REFERENCE_MESH_PALETTE_COLOUR 0x8Cu", self.draw
        )

    def test_shutdown_clears_the_mesh(self) -> None:
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        body = function_body(scene, "void roller_ed_legacy_scene_shutdown(")
        self.assertIn("ed_reference_mesh_reset_current();", body)


class ExporterBoundaryTests(unittest.TestCase):
    def test_the_mesh_never_reaches_the_canonical_emitter(self) -> None:
        # AD-6d: the reference mesh is the host's own scenery, so no exporter
        # may ever pick it up and it carries no chunk identity.
        combined = (
            (ROLLER / "editor_reference_mesh.h").read_text(encoding="utf-8")
            + (ROLLER / "editor_reference_mesh.c").read_text(encoding="utf-8")
        )
        self.assertNotIn("ed_emit_surface", combined)
        self.assertNotIn("tEdSurfaceEmission", combined)


class BuildRegistrationTests(unittest.TestCase):
    def test_the_acceptance_target_exists(self) -> None:
        build_zig = (ROOT / "build.zig").read_text(encoding="utf-8")
        self.assertIn("test-e3a-s7-reference-mesh", build_zig)


if __name__ == "__main__":
    unittest.main()
