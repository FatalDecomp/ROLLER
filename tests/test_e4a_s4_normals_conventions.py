import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROLLER = ROOT / "PROJECTS" / "ROLLER"
ADR = ROOT / "docs" / "adr" / "0003-canonical-geometry-conventions.md"


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


class GeneratedNormalContractTests(unittest.TestCase):
    def test_emissions_carry_surface_and_per_vertex_normals(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        vertex = header[header.index("float fPosition[3];") :]
        vertex = vertex[: vertex.index("} tEdSurfaceVertex;")]
        self.assertIn("float fNormal[3];", vertex)

        emission = header[header.index("aVertices[ED_SURFACE_VERTEX_COUNT];") :]
        emission = emission[: emission.index("} tEdSurfaceEmission;")]
        self.assertIn("float fNormal[3];", emission)

        self.assertIn("bool ed_surface_compute_normals(", header)
        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )
        # The render vertex carries position and UV only, so the emitter is
        # the one place normals can come from.
        self.assertIn("ed_surface_compute_normals(", emitter)
        self.assertIn("Surface.fNormal", emitter)
        self.assertIn("Surface.aVertices[i].fNormal", emitter)

    def test_surface_normal_uses_all_four_corners(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "bool ed_surface_compute_normals(")
        )

        # Newell's method: every edge of the quad contributes, so a twisted
        # section is not reduced to a normal read off three of its corners.
        self.assertIn("afNewell", body)
        self.assertIn("(i + 1u) % ED_SURFACE_VERTEX_COUNT", body)
        self.assertIn("i < ED_SURFACE_VERTEX_COUNT", body)

        # Per-vertex normals come from that vertex's own adjacent edges.
        self.assertIn("ED_SURFACE_VERTEX_COUNT - 1u", body)
        self.assertIn("afToNext", body)
        self.assertIn("afToPrev", body)
        self.assertIn("ed_cross(afToNext, afToPrev, afVertexNormals[i])", body)
        self.assertIn("afSurfaceNormal", body)

    def test_degenerate_geometry_zeroes_instead_of_producing_nan(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "static bool ed_normalize(")
        )

        self.assertIn("0.0f", body)
        self.assertIn("return false", body)
        # Guarding on "> 0.0" rather than "!= 0.0" also rejects a NaN input.
        self.assertIn("!(dLength > 0.0)", body)

    def test_pinched_corners_fall_back_to_the_quad_normal(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "bool ed_surface_compute_normals(")
        )

        # Real track sections pinch a corner to a sub-unit edge at coordinates
        # in the tens of thousands. The sliver triangle there is not a usable
        # shading normal, so the corner takes the whole-quad normal -- judged
        # relative to the quad's own size, not an absolute epsilon.
        self.assertIn("ED_NORMAL_DEGENERATE_RATIO", body)
        self.assertIn("dNewellLength", body)
        self.assertIn("memcpy(afVertexNormals[i], afSurfaceNormal", body)
        self.assertIn(
            "#define ED_NORMAL_DEGENERATE_RATIO", source
        )


class GeometryConventionContractTests(unittest.TestCase):
    def test_conventions_are_written_down(self) -> None:
        self.assertTrue(ADR.exists(), "ADR 0003 must exist")
        adr = ADR.read_text(encoding="utf-8")

        for section in (
            "### Coordinate system",
            "### Scale",
            "### Winding",
            "### Normals",
            "### UV origin",
        ):
            self.assertIn(section, adr)

        # The load-bearing claims, each of which a test below also asserts
        # against the source.
        self.assertIn("world +Z is up", adr)
        self.assertIn("Newell", adr)
        self.assertIn("right-hand rule", adr)
        self.assertIn("top-left", adr)
        self.assertIn("legacy track units", adr)
        self.assertIn("SURFACE_FLAG_CONCAVE", adr)
        self.assertIn("ED_NORMAL_DEGENERATE_RATIO", adr)

    def test_surfaces_the_renderer_never_culls_are_marked_two_sided(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )

        # CONCAVE makes the renderer bypass its facing test outright, so those
        # surfaces are visible from both sides. Exporting them single-sided
        # would leave holes in the environment skirt.
        two_sided = emitter[
            emitter.index("ROLLER_ED_SURFACE_FLAG_TWO_SIDED") - 200 :
            emitter.index("ROLLER_ED_SURFACE_FLAG_TWO_SIDED") + 60
        ]
        self.assertIn("SURFACE_FLAG_FLIP_BACKFACE", two_sided)
        self.assertIn("SURFACE_FLAG_CONCAVE", two_sided)

    def test_full_track_normal_acceptance_target_exists(self) -> None:
        build = (ROOT / "build.zig").read_text(encoding="utf-8")
        harness = ROOT / "tests" / "editor_geometry_conventions_acceptance.c"

        self.assertTrue(harness.exists())
        self.assertIn("test-e4a-s4-geometry-conventions", build)
        self.assertIn("editor_geometry_conventions_acceptance.c", build)

        text = harness.read_text(encoding="utf-8")
        # The claims this harness has to check on real geometry.
        self.assertIn("drawtrk3_emit_full_track(", text)
        self.assertIn("uiFacingMismatches", text)
        self.assertIn("uiVertexNormalMismatches", text)
        self.assertIn("uiNonUnitNormals", text)
        self.assertIn("count_winding_reversals(", text)
        # Two cameras must produce identical geometry and identical normals.
        self.assertIn("CameraA", text)
        self.assertIn("CameraB", text)
        self.assertIn("memcmp(First.pSurfaces, Second.pSurfaces", text)

    def test_header_summary_matches_the_adr(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")

        self.assertIn("0003-canonical-geometry-conventions.md", header)
        self.assertIn("#define ED_SURFACE_WORLD_UP_AXIS 2u", header)
        for claim in (
            "world +Z is up",
            "right-hand rule",
            "Newell",
            "top-left",
        ):
            self.assertIn(claim, header)

    def test_emitter_applies_no_scale_and_preserves_winding(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )

        # Positions are copied straight through: no scale factor and no axis
        # swap can hide in a memcpy of the caller's array.
        self.assertIn(
            "memcpy(Surface.aVertices[i].fPosition, afWorldVertices[i]",
            emitter,
        )

        # The flip flags permute UVs only. If they ever touched fPosition the
        # winding, and therefore the front face, would depend on them.
        flips = emitter[emitter.index("SURFACE_FLAG_FLIP_HORIZ") :]
        flips = flips[: flips.index("ed_build_material")]
        self.assertIn("fMaterialUV", flips)
        self.assertNotIn("fPosition", flips)
        self.assertNotIn("fNormal", flips)

    def test_uv_corner_table_puts_the_origin_at_the_top_left(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        body = function_body(source, "bool ed_surface_compute_render_uvs(")

        # v0/v1 on V=0 and v2/v3 on V=1 is what makes V grow downward, which
        # is the top-left origin the texture rows already use.
        self.assertIn("aiUCorner[ED_SURFACE_VERTEX_COUNT] = { 1, 0, 0, 1 }", body)
        self.assertIn("aiVCorner[ED_SURFACE_VERTEX_COUNT] = { 0, 0, 1, 1 }", body)


if __name__ == "__main__":
    unittest.main()
