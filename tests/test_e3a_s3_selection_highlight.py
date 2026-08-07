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


class SelectionRangeTests(unittest.TestCase):
    def test_a_chunk_range_covers_every_surface_class(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        # The editor selects chunks, not classes, so the range has to span
        # them all -- while F-S4b's single-class form keeps working.
        self.assertIn("#define ED_SURFACE_SELECTION_ANY_CLASS 0xFFFFu", header)
        matcher = without_comments(
            function_body(source, "bool ed_surface_selection_matches(")
        )
        self.assertIn("ED_SURFACE_SELECTION_ANY_CLASS", matcher)
        self.assertIn("unSurfaceClass != pSelection->unSurfaceClass", matcher)
        # Reversed ranges are still ordered here, not at the boundary.
        self.assertIn("uiFirstChunkId > uiLastChunkId", matcher)

    def test_the_facade_range_reaches_the_renderer_once_per_frame(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        scene = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")

        body = without_comments(
            function_body(draw, "void drawtrk3_editor_apply_overlay_selection(")
        )
        self.assertIn("roller_ed_overlay_selection_range(", body)
        self.assertIn("ED_SURFACE_SELECTION_ANY_CLASS", body)
        self.assertIn("ED_SELECTION_HIGHLIGHT_PALETTE_COLOUR", body)
        self.assertIn("drawtrk3_editor_selection_clear()", body)

        render = function_body(scene, "eRollerEdResult roller_ed_legacy_scene_render(")
        self.assertIn("drawtrk3_editor_apply_overlay_selection();", render)
        # Once per frame, before anything draws.
        self.assertLess(
            render.index("drawtrk3_editor_apply_overlay_selection();"),
            render.index("draw_road("),
        )

    def test_the_highlight_keeps_the_legacy_selection_colour(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        self.assertIn(
            "#define ED_SELECTION_HIGHLIGHT_PALETTE_COLOUR 0xDAu", draw
        )


class SelectionRenderingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        cls.body = without_comments(
            function_body(
                cls.draw, "static void draw_emitted_surface(const tEdSurfaceEmission"
            )
        )

    def test_selection_outlines_rather_than_recolours(self) -> None:
        # E3A-S3 chose outlines: the fill keeps its own flags and its texture,
        # so Select All does not flatten the whole track.
        self.assertIn("uiRenderFlags = pSurface->uiRenderFlags;", self.body)
        self.assertNotIn(
            "uiRenderFlags = ed_surface_selection_render_flags(", self.body
        )
        # The texture lookup no longer consults the selection, so a selected
        # surface keeps the texture the maintainer is editing against.
        lookup_start = self.body.index("hTexture = game_render_get_texture_handle")
        guard = self.body.rindex("if (", 0, lookup_start)
        self.assertNotIn("bSelected", self.body[guard:lookup_start])

    def test_the_outline_uses_the_selection_flags_helper(self) -> None:
        # F-S4b's helper still produces the flat highlight flags; E3A-S3
        # applies them to the outline ribbons instead of the fill.
        self.assertIn("ed_surface_selection_render_flags(", self.body)
        self.assertIn("draw_emitted_surface_edges(", self.body)

    def test_selection_wins_over_the_wireframe_colour(self) -> None:
        # Both passes would draw the same coincident ribbons; drawing only one
        # keeps them from fighting, and the selection is the one that matters.
        self.assertIn("if (bSelected)", self.body)
        self.assertIn("else if (roller_ed_overlay_wireframe_class_visible(", self.body)

    def test_a_hidden_surface_still_shows_its_selection(self) -> None:
        hidden = self.body[: self.body.index("pFrontMaterial = ed_material_table_get(")]
        self.assertIn("if (bSelected)", hidden)
        self.assertIn("ed_surface_selection_render_flags(", hidden)

    def test_matching_uses_canonical_identity(self) -> None:
        # AD-8: the chunk id comes from the emission, never from a batched
        # draw command.
        self.assertIn(
            "ed_surface_selection_matches(\n        pContext->pSelection, pSurface)",
            self.body,
        )
        self.assertNotIn("renderChunkIdx", self.body)


class CoverageTests(unittest.TestCase):
    def test_the_native_case_runs_in_the_default_suite(self) -> None:
        test = (ROOT / "tests" / "editor_surface_test.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("test_chunk_range_selection_covers_every_class();", test)
        self.assertIn("ED_SURFACE_SELECTION_ANY_CLASS", test)


if __name__ == "__main__":
    unittest.main()
