import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"

# The eleven track classes the editor's DisplaySettings dialog has always
# offered a surface and a wireframe checkbox for, plus the three object classes
# the canonical emitter added. Parity means every one of them is individually
# addressable.
TRACK_SURFACE_CLASSES = (
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
)
OBJECT_SURFACE_CLASSES = ("SIGN", "BUILDING", "TOWER")


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


def overlay_references(source: str):
    """Yields (symbol, is_inside_a_ROLLER_EDITOR_CORE_block) for each call."""
    stack = []
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if"):
            stack.append("ROLLER_EDITOR_CORE" in stripped)
        elif stripped.startswith("#elif"):
            if stack:
                stack[-1] = "ROLLER_EDITOR_CORE" in stripped
        elif stripped.startswith("#endif"):
            if stack:
                stack.pop()
        for match in re.finditer(r"roller_ed_overlay_\w+", line):
            yield match.group(0), any(stack)


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//.*", "", source)


class OverlayClassMaskAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_api.h").read_text(encoding="utf-8")

    def test_surface_classes_are_public_and_index_the_masks(self) -> None:
        # The values are published through tEdPrimitive.unSurfaceClass, so a
        # host cannot build a class mask while they live in an internal header.
        for name in TRACK_SURFACE_CLASSES + OBJECT_SURFACE_CLASSES:
            self.assertIn(f"ROLLER_ED_SURFACE_CLASS_{name}", self.header)
        self.assertIn("ROLLER_ED_SURFACE_CLASS_COUNT = 14u", self.header)
        self.assertNotIn(
            "ROLLER_ED_SURFACE_CLASS_CENTER",
            (ROLLER / "editor_surface.h").read_text(encoding="utf-8"),
        )

    def test_the_state_carries_a_mask_for_each_toggle(self) -> None:
        overlay = self.header[
            self.header.index("uiLastSelectedChunk;") : self.header.index(
                "} tEdOverlayState;"
            )
        ]
        self.assertIn("uint32_t uiSurfaceClassMask;", overlay)
        self.assertIn("uint32_t uiWireframeClassMask;", overlay)
        # 36 since E3A-S6 appended the test-car selection. What this story
        # owns is that its two masks keep the offsets it gave them, which the
        # loop below still checks; the total only has to stay asserted.
        self.assertIn(
            "ROLLER_ED_STATIC_ASSERT(sizeof(tEdOverlayState) == 36u", self.header
        )
        for field, offset in (
            ("uiSurfaceClassMask", 20),
            ("uiWireframeClassMask", 24),
        ):
            self.assertIn(
                f"offsetof(tEdOverlayState, {field}) == {offset}u", self.header
            )

    def test_only_the_overlay_struct_version_moved(self) -> None:
        # 3 since E3A-S6. The point of this test is not the number but that
        # tEdOverlayState is the *only* struct that has ever moved off 1: the
        # per-struct versions are independent of each other and of the API
        # version, which is what lets one struct gain a field cheaply.
        self.assertIn("#define ROLLER_ED_OVERLAY_STATE_VERSION 3u", self.header)
        for name in (
            "ROLLER_ED_BOOTSTRAP_INFO_VERSION",
            "ROLLER_ED_INIT_INFO_VERSION",
            "ROLLER_ED_CAMERA_STATE_VERSION",
            "ROLLER_ED_REFERENCE_MESH_VERSION",
            "ROLLER_ED_GEOMETRY_SIZES_VERSION",
        ):
            self.assertIn(f"#define {name} 1u", self.header)
        self.assertIn("#define ROLLER_ED_API_VERSION 1u", self.header)

    def test_the_masks_have_public_helpers(self) -> None:
        self.assertIn("ROLLER_ED_OVERLAY_CLASS_BIT(surface_class)", self.header)
        self.assertIn("ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES", self.header)


class FacadeValidationTests(unittest.TestCase):
    def test_each_struct_is_checked_against_its_own_version(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        validator = without_comments(
            function_body(source, "static eRollerEdResult roller_ed_validate_struct(")
        )
        # AD-12: a struct that gains a field bumps only its own version, so the
        # expected version cannot be the API version baked into the validator.
        self.assertIn("uiExpectedVersion", validator)
        self.assertNotIn("ROLLER_ED_API_VERSION", validator)
        for name in (
            "ROLLER_ED_BOOTSTRAP_INFO_VERSION",
            "ROLLER_ED_INIT_INFO_VERSION",
            "ROLLER_ED_CAMERA_STATE_VERSION",
            "ROLLER_ED_OVERLAY_STATE_VERSION",
            "ROLLER_ED_REFERENCE_MESH_VERSION",
            "ROLLER_ED_GEOMETRY_SIZES_VERSION",
        ):
            self.assertIn(name, source)

    def test_class_masks_beyond_the_last_class_are_refused(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(
                source, "eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState("
            )
        )
        self.assertIn("uiSurfaceClassMask", body)
        self.assertIn("uiWireframeClassMask", body)
        self.assertIn("ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES", body)
        self.assertLess(
            body.index("ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES"),
            body.index("roller_ed_legacy_scene_set_overlay_state("),
        )


class OverlayQueryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROLLER / "editor_overlay.h").read_text(encoding="utf-8")
        cls.source = (ROLLER / "editor_overlay.c").read_text(encoding="utf-8")

    def test_the_queries_are_the_master_flag_and_the_class_bit(self) -> None:
        for name, flag in (
            ("roller_ed_overlay_surface_class_visible", "SHOW_SURFACES"),
            ("roller_ed_overlay_wireframe_class_visible", "SHOW_WIREFRAME"),
        ):
            self.assertIn(name, self.header)
            body = without_comments(function_body(self.source, f"bool {name}("))
            self.assertIn(f"ROLLER_ED_OVERLAY_{flag}", body)
            self.assertIn("overlay_class_selected", body)
            self.assertIn("&&", body)

    def test_an_out_of_range_class_is_never_drawn(self) -> None:
        body = without_comments(
            function_body(self.source, "static bool overlay_class_selected(")
        )
        self.assertIn("ROLLER_ED_SURFACE_CLASS_COUNT", body)
        self.assertIn("return false;", body)

    def test_defaults_keep_every_class_solid_and_none_wireframe(self) -> None:
        self.assertIn(
            "#define ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK \\\n"
            "    ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES",
            self.header,
        )
        self.assertIn(
            "#define ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK 0u",
            self.header,
        )
        reset = function_body(self.source, "void roller_ed_overlay_reset(")
        self.assertIn("ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK", reset)
        self.assertIn("ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK", reset)


class RenderFilteringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

    def test_filtering_uses_canonical_identity_only(self) -> None:
        body = function_body(
            self.draw, "static void draw_emitted_surface(const tEdSurfaceEmission"
        )
        # AD-8: the class comes from the emission, never from a draw command.
        self.assertIn(
            "roller_ed_overlay_surface_class_visible(pSurface->unSurfaceClass)",
            body,
        )
        self.assertIn(
            "roller_ed_overlay_wireframe_class_visible(pSurface->unSurfaceClass)",
            body,
        )
        self.assertNotIn("renderChunkIdx", body)

    def test_the_game_build_is_not_affected(self) -> None:
        # Every per-surface overlay read sits inside a ROLLER_EDITOR_CORE
        # block, so the game's draw path is unchanged. Nesting is tracked
        # properly rather than by counting #endif, which any other conditional
        # in the file would throw off.
        outside = [
            name
            for name, gated in overlay_references(self.draw)
            if not gated
        ]
        # The deliberate exceptions are the two entry points that
        # editor_legacy_scene.c calls: it is in the game's source set, so they
        # cannot be gated, and the game never reaches the render path that
        # reaches them. Every such read must live inside one of the two.
        sync = function_body(
            self.draw, "void drawtrk3_editor_apply_overlay_selection("
        )
        helpers = function_body(
            self.draw, "void drawtrk3_editor_draw_helpers("
        )
        for name in outside:
            self.assertTrue(
                name in sync or name in helpers,
                f"{name} is reachable from the game build",
            )
        self.assertIn("roller_ed_overlay_selection_range", sync)
        self.assertIn("roller_ed_overlay_enabled", helpers)
        self.assertIn('#include "editor_overlay.h"', self.draw)

    def test_wireframe_is_drawn_from_the_emitted_surface(self) -> None:
        body = function_body(
            self.draw, "static void draw_emitted_surface_edges("
        )
        self.assertIn("ed_surface_wireframe_edge_quad(pSurface, uiEdge,", body)
        self.assertIn("ED_SURFACE_VERTEX_COUNT", body)
        self.assertIn("TEXTURE_HANDLE_INVALID", body)
        # Flat fill, same mechanism as the selection highlight: texture bits
        # cleared, palette colour in the low byte.
        flags = function_body(
            self.draw, "static uint32_t editor_edge_flags("
        )
        self.assertIn("SURFACE_FLAG_APPLY_TEXTURE", flags)
        self.assertIn("SURFACE_MASK_FLAGS", flags)
        self.assertIn("ED_WIREFRAME_PALETTE_COLOUR", self.draw)

    def test_a_hidden_surface_can_still_show_its_wireframe(self) -> None:
        body = without_comments(
            function_body(
                self.draw,
                "static void draw_emitted_surface(const tEdSurfaceEmission",
            )
        )
        hidden = body[: body.index("pFrontMaterial = ed_material_table_get(")]
        self.assertIn("draw_emitted_surface_edges(", hidden)
        self.assertIn("ED_WIREFRAME_PALETTE_COLOUR", hidden)
        self.assertIn("return;", hidden)


class WireframeGeometryTests(unittest.TestCase):
    def test_edges_are_built_by_the_emitter_not_the_renderer(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("bool ed_surface_wireframe_edge_quad(", header)
        # E3A-S7 extracted the ribbon math so a bare reference-mesh triangle
        # gets the identical width, bias, and winding; the surface entry point
        # is now a thin wrapper over it. The property this story owns is that
        # the ribbon is built here rather than by the renderer.
        wrapper = without_comments(
            function_body(source, "bool ed_surface_wireframe_edge_quad(")
        )
        self.assertIn("pSurface->fNormal", wrapper)
        self.assertIn("ed_surface_wireframe_edge_quad_points(", wrapper)

        body = without_comments(
            function_body(source, "bool ed_surface_wireframe_edge_quad_points(")
        )
        # In-plane ribbon: normal x direction, biased along the normal so it
        # wins the depth test against the surface it outlines.
        self.assertIn("ed_cross(afNormal, afDirection, afSideways)", body)
        self.assertIn("ED_WIREFRAME_WIDTH_RATIO", body)
        self.assertIn("ED_WIREFRAME_DEPTH_BIAS_RATIO", body)
        self.assertIn("ed_normalize(afDirection)", body)
        self.assertIn("return false;", body)

    def test_the_native_cases_run_in_the_default_suite(self) -> None:
        test = (ROOT / "tests" / "editor_surface_test.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "test_wireframe_edges_trace_the_surface_front_face();", test
        )
        self.assertIn("test_wireframe_refuses_degenerate_edges();", test)
        overlay_test = (ROOT / "tests" / "editor_overlay_test.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("roller_ed_overlay_surface_class_visible", overlay_test)
        self.assertIn("roller_ed_overlay_wireframe_class_visible", overlay_test)


if __name__ == "__main__":
    unittest.main()
