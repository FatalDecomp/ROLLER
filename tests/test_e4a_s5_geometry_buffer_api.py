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


class QueryContractTests(unittest.TestCase):
    def test_query_stays_readable_in_every_scene_state(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "eRollerEdResult ROLLER_ED_CALL RollerEd_QueryGeometrySizes(")
        )

        # AD-7d: the call reports OK whenever initialized, so uiSceneState is
        # readable even when there is nothing to extract.
        self.assertNotIn("ROLLER_ED_RESULT_NO_SCENE", body)
        self.assertIn("Sizes.uiSceneState = s_eSceneState;", body)
        self.assertIn("ROLLER_ED_SCENE_READY", body)

        # Counts are published only for a READY scene; EMPTY and FAILED keep
        # the zeroed struct.
        self.assertIn("memset(&Sizes, 0, sizeof(Sizes));", body)
        for field in (
            "uiVertexCount",
            "uiIndexCount",
            "uiPrimitiveCount",
            "uiMaterialCount",
        ):
            self.assertIn(f"Sizes.{field} = s_GeometryExtract.{field};", body)

    def test_published_strides_come_from_the_public_structs(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = function_body(
            source,
            "eRollerEdResult ROLLER_ED_CALL RollerEd_QueryGeometrySizes(",
        )

        self.assertIn("Sizes.uiVertexStride = sizeof(tEdVertex);", body)
        self.assertIn("Sizes.uiPrimitiveStride = sizeof(tEdPrimitive);", body)
        self.assertIn("Sizes.uiMaterialStride = sizeof(tEdMaterial);", body)


class FillContractTests(unittest.TestCase):
    def test_epoch_and_capacity_are_checked_before_any_write(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "eRollerEdResult ROLLER_ED_CALL RollerEd_FillGeometry(")
        )

        # Order is load-bearing (AD-7a): every refusal has to be decided
        # before the first byte is copied.
        no_scene = body.index("ROLLER_ED_RESULT_NO_SCENE")
        stale = body.index("ROLLER_ED_RESULT_STALE")
        too_small = body.index("ROLLER_ED_RESULT_BUFFER_TOO_SMALL")
        first_write = body.index("memcpy(")
        self.assertLess(no_scene, stale)
        self.assertLess(stale, too_small)
        self.assertLess(too_small, first_write)

        # AD-7d: fill validates the geometry epoch, never the generation.
        self.assertIn("uiExpectedGeometryEpoch != s_uiGeometryEpoch", body)
        self.assertNotIn("s_uiTrackGeneration", body)

        for field in (
            "uiVertexCapacity < s_GeometryExtract.uiVertexCount",
            "uiIndexCapacity < s_GeometryExtract.uiIndexCount",
            "uiPrimitiveCapacity < s_GeometryExtract.uiPrimitiveCount",
            "uiMaterialCapacity < s_GeometryExtract.uiMaterialCount",
        ):
            self.assertIn(field, body)

    def test_no_core_owned_pointer_escapes(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "eRollerEdResult ROLLER_ED_CALL RollerEd_FillGeometry(")
        )

        # Every output is a copy out of the cache; nothing assigns a cache
        # pointer to a caller-visible one.
        self.assertEqual(body.count("memcpy("), 4)
        for destination in ("pVerts", "puiIndices", "pPrims", "pMats"):
            self.assertRegex(body, rf"memcpy\({destination},\s*s_GeometryExtract\.")
        self.assertNotRegex(body, r"\*p(Verts|Prims|Mats)\s*=\s*s_GeometryExtract")


class ExtractionContractTests(unittest.TestCase):
    def test_extraction_is_cached_per_geometry_epoch(self) -> None:
        source = (ROLLER / "editor_api.c").read_text(encoding="utf-8")

        sync = without_comments(
            function_body(source, "static eRollerEdResult roller_ed_sync_geometry_cache(")
        )
        self.assertIn("s_uiGeometryExtractEpoch == s_uiGeometryEpoch", sync)
        self.assertIn("roller_ed_legacy_scene_extract_geometry(", sync)

        # Anything that advances the epoch has invalidated the cache, so the
        # release is wired to the epoch helper rather than to each caller.
        advance = without_comments(
            function_body(source, "static void roller_ed_advance_geometry_epoch(")
        )
        self.assertIn("roller_ed_release_geometry_cache();", advance)

        # And the worker teardown frees it.
        release = without_comments(
            function_body(source, "static void roller_ed_release_worker_resources(")
        )
        self.assertIn("roller_ed_release_geometry_cache();", release)

    def test_extraction_runs_over_the_canonical_traversal(self) -> None:
        header = (ROLLER / "editor_legacy_scene.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")

        self.assertIn("eRollerEdResult roller_ed_legacy_scene_extract_geometry(", header)
        self.assertIn("void roller_ed_legacy_scene_release_geometry(", header)

        body = without_comments(
            function_body(source, "eRollerEdResult roller_ed_legacy_scene_extract_geometry(")
        )
        # The canonical producers are the only geometry source, and both
        # passes reach them through one helper so they cannot diverge
        # (E4A-S6 added the scenery half beside E4A-S2's track walk).
        emit_all = without_comments(
            function_body(source, "static bool editor_geometry_emit_all(")
        )
        self.assertIn("drawtrk3_emit_full_track(", emit_all)
        self.assertIn("drawtrk3_emit_full_scenery(", emit_all)
        self.assertIn("editor_geometry_emit_all(", body)
        self.assertNotIn("drawtrk3_emit_full", body)
        self.assertIn("editor_geometry_probe(", body)
        # A second pass that disagrees with the sizing pass is a defect.
        self.assertIn("Fill.uiPrimitiveIndex != uiSurfaceCount", body)
        self.assertIn("Table.uiCount != uiMaterialCount", body)

    def test_quads_are_published_as_a_triangle_list(self) -> None:
        source = (ROLLER / "editor_legacy_scene.c").read_text(encoding="utf-8")
        body = without_comments(
            function_body(source, "static void editor_geometry_fill_surface(")
        )

        # Both triangles start at v0 so the emitter's winding, and therefore
        # the front face, survives triangulation (ADR 0003).
        for offset, corner in enumerate((0, 1, 2, 0, 2, 3)):
            self.assertIn(
                f"puiIndices[uiBaseIndex + {offset}u] = uiBaseVertex + {corner}u;",
                body,
            )
        self.assertIn("ROLLER_ED_TOPOLOGY_TRIANGLE_LIST", body)

        # The public vertex takes the generated normal and the material-local
        # UV; the internal fixed-point render UVs stay internal.
        self.assertIn("pSource->fNormal", body)
        self.assertIn("pSource->fMaterialUV", body)
        self.assertNotIn("iRenderU16_16", body)
        self.assertNotIn("iRenderV16_16", body)

        # Internal and public flag vocabularies are mapped, not aliased.
        self.assertIn("ROLLER_ED_SURFACE_FLAG_ALPHA", body)
        self.assertIn("ROLLER_ED_PRIMITIVE_FLAG_ALPHA_BLEND", body)
        self.assertIn("ROLLER_ED_SURFACE_FLAG_TWO_SIDED", body)
        self.assertIn("ROLLER_ED_PRIMITIVE_FLAG_TWO_SIDED", body)


class AcceptanceCoverageTests(unittest.TestCase):
    def test_stale_paths_are_covered_against_the_facade(self) -> None:
        lifecycle = (ROOT / "tests" / "editor_api_lifecycle_test.c").read_text(
            encoding="utf-8"
        )

        # A larger track loaded between query and fill, which is the case that
        # would have overflowed buffers sized for the previous track.
        self.assertIn("szLargeTrack", lifecycle)
        self.assertTrue((ROOT / "tests" / "fixtures" / "e0_s7_large.trk").exists())
        self.assertIn("ROLLER_ED_RESULT_STALE", lifecycle)
        self.assertIn("ROLLER_ED_RESULT_BUFFER_TOO_SMALL", lifecycle)
        # The v5 hole: a failed load leaves the generation alone, so the test
        # asserts the generation really did not move while the call was still
        # refused.
        self.assertIn("uiReadyGeneration == Sizes.uiTrackGeneration", lifecycle)

    def test_retail_acceptance_target_exists(self) -> None:
        build = (ROOT / "build.zig").read_text(encoding="utf-8")
        harness = ROOT / "tests" / "editor_geometry_buffer_acceptance.c"

        self.assertTrue(harness.exists())
        self.assertIn("test-e4a-s5-geometry-buffers", build)
        self.assertIn("editor_geometry_buffer_acceptance.c", build)

        text = harness.read_text(encoding="utf-8")
        self.assertIn("RollerEd_QueryGeometrySizes(", text)
        self.assertIn("RollerEd_FillGeometry(", text)
        self.assertIn("ROLLER_ED_RESULT_STALE", text)
        self.assertIn("ROLLER_ED_RESULT_BUFFER_TOO_SMALL", text)
        self.assertIn("ROLLER_ED_RESULT_NO_SCENE", text)
        # Camera movement must not invalidate authored geometry (AD-7d).
        self.assertIn("RollerEd_SetCamera(", text)
        self.assertIn("advanced the geometry epoch", text)


if __name__ == "__main__":
    unittest.main()
