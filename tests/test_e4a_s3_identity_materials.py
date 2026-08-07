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


class EmittedIdentityContractTests(unittest.TestCase):
    def test_identity_is_validated_before_any_surface_is_emitted(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("bool ed_surface_identity_valid(", header)
        validator = without_comments(
            function_body(source, "bool ed_surface_identity_valid(")
        )
        # Chunk-bound primitives stay inside the legacy chunk range; anything
        # else must use the explicit sentinel rather than a made-up number.
        self.assertIn("ROLLER_ED_INVALID_CHUNK_ID", validator)
        self.assertIn("MAX_TRACK_CHUNKS", validator)
        self.assertIn("ROLLER_ED_SURFACE_CLASS_TOWER", validator)
        self.assertIn("ROLLER_ED_CONTENT_RUNTIME_SCENERY", validator)
        self.assertIn("ROLLER_ED_TOPOLOGY_QUAD", validator)

        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )
        self.assertIn("ed_surface_identity_valid(pInfo)", emitter)

    def test_every_producer_states_an_explicit_content_class(self) -> None:
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")
        tower = (ROLLER / "tower.c").read_text(encoding="utf-8")

        # Authored track geometry, authored advert panels, fixed authored
        # scenery, and camera-facing runtime scenery are each stated at the
        # producer, so no exporter re-derives object class from surface class.
        self.assertIn("ROLLER_ED_CONTENT_AUTHORED_TRACK", draw)
        self.assertIn("ROLLER_ED_CONTENT_AUTHORED_SIGN", building)
        self.assertIn("ROLLER_ED_CONTENT_AUTHORED_SCENERY", building)
        self.assertIn("ROLLER_ED_CONTENT_RUNTIME_SCENERY", building)
        self.assertIn("ROLLER_ED_CONTENT_RUNTIME_SCENERY", tower)

        for source, name in (
            (building, "building.c"),
            (tower, "tower.c"),
        ):
            self.assertIn(
                "ROLLER_ED_INVALID_CHUNK_ID",
                source,
                f"{name} must use the sentinel for unbound objects",
            )


class MaterialResolutionContractTests(unittest.TestCase):
    def test_material_table_describes_one_atlas_per_texture_set(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("#define ED_MATERIAL_MAX_TEXTURE_SETS", header)
        self.assertIn("bool ed_material_table_set_atlas(", header)
        self.assertIn("const tEdTextureAtlas *ed_material_table_atlas(", header)

        atlas = header[
            header.index("typedef struct", header.index("ED_MATERIAL_MAX_TEXTURE_SETS")) :
        ]
        atlas = atlas[: atlas.index("} tEdTextureAtlas;")]
        self.assertIn("uiTextureSet", atlas)
        self.assertIn("uiTileCount", atlas)

        table = header[header.index("tEdMaterial *pMaterials;") :]
        table = table[: table.index("} tEdMaterialTable;")]
        self.assertIn("aAtlases[ED_MATERIAL_MAX_TEXTURE_SETS]", table)
        self.assertNotIn("tEdTextureAtlas Atlas;", table)

        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )
        self.assertIn(
            "ed_material_table_atlas(pMaterials, pInfo->uiTextureSet)", emitter
        )
        # The builder resolves one atlas, never the whole table, so it cannot
        # silently size a building tile against the track bank.
        self.assertIn("static bool ed_build_material(const tEdTextureAtlas *", source)

    def test_every_legacy_texture_bank_is_registered(self) -> None:
        header = (ROLLER / "drawtrk3.h").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

        self.assertIn("bool drawtrk3_editor_texture_atlas(", header)
        self.assertIn("bool drawtrk3_init_editor_material_table(", header)

        table = function_body(
            draw, "bool drawtrk3_init_editor_material_table("
        )
        for bank in (
            "TEXTURE_BANK_TRACK",
            "TEXTURE_BANK_BUILDING",
            "TEXTURE_BANK_CARGEN",
        ):
            self.assertIn(bank, table)
        self.assertIn("ed_material_table_set_atlas(", table)

        renderer = function_body(draw, "bool drawtrk3_emit_surface_to_renderer(")
        self.assertIn("drawtrk3_init_editor_material_table(", renderer)

    def test_material_kinds_cover_every_legacy_surface_path(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        builder = without_comments(
            function_body(source, "static bool ed_build_material(")
        )

        for kind in (
            "ROLLER_ED_MATERIAL_TEXTURED_TILE",
            "ROLLER_ED_MATERIAL_TEXTURED_PAIR",
            "ROLLER_ED_MATERIAL_FLAT_PALETTE_COLOR",
            "ROLLER_ED_MATERIAL_SCREEN_DARKEN",
        ):
            self.assertIn(kind, builder)
        self.assertIn("uiDarkenLevel", builder)
        self.assertIn("uiPaletteColour", builder)
        self.assertIn("fAtlasScale", builder)
        self.assertIn("fAtlasBias", builder)
        self.assertIn("ROLLER_ED_PAIR_TEXTURE_TILE_SPAN", builder)

        # Tile identity belongs to the textured branch only: the non-textured
        # kinds must not publish a tile index or an atlas rectangle.
        applied = builder.index("SURFACE_FLAG_APPLY_TEXTURE")
        textured = builder[
            applied : builder.index("SURFACE_FLAG_TRANSPARENT", applied)
        ]
        self.assertIn("pMaterial->uiTileIndex = uiTileIndex;", textured)
        self.assertEqual(builder.count("pMaterial->uiTileIndex"), 1)

    def test_back_material_follows_the_draw_time_substitution(self) -> None:
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        draw = (ROLLER / "drawtrk3.c").read_text(encoding="utf-8")

        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )
        self.assertIn("SURFACE_FLAG_BACK", emitter)
        self.assertIn("uiBackTile != uiTileIndex", emitter)
        self.assertIn("uiBackTile < pAtlas->uiTileCount", emitter)
        self.assertIn("SURFACE_MASK_FLAGS", emitter)
        # Absent, identical, or out-of-range substitutes leave the sentinel.
        self.assertIn("Surface.uiBackMaterialId = ED_MATERIAL_ID_NONE;", emitter)

        substitution = without_comments(
            function_body(draw, "static bool emit_surface_to_consumer(")
        )
        self.assertIn("texture_back[256 * Info.uiTextureSet + uiTile]", substitution)
        self.assertIn("TEXTURE_BANK_BUILDING", substitution)

    def test_pair_textures_follow_the_renderer_availability_rule(self) -> None:
        header = (ROLLER / "editor_surface.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")
        building = (ROLLER / "building.c").read_text(encoding="utf-8")

        self.assertIn("bool ed_atlas_pair_available(", header)
        self.assertIn("bool ed_atlas_pair_wraps_row(", header)

        available = without_comments(
            function_body(source, "bool ed_atlas_pair_available(")
        )
        self.assertIn("uiTileIndex + 1u < pAtlas->uiTileCount", available)

        emitter = without_comments(
            function_body(source, "bool ed_emit_surface(")
        )
        # Material kind and render UV span fall back together, so an emission
        # never claims a pair the renderer would not bind.
        self.assertIn("ed_atlas_pair_available(pAtlas, uiTileIndex)", emitter)
        self.assertIn("ROLLER_ED_RENDER_UV_TILE", emitter)

        # Pair textures are a track-wall feature; the GPU path gates them on
        # !isBuilding and SW keeps building quads on the plain tile span.
        # E4A-S6 moved this out of DrawBuilding so the canonical scenery
        # traversal cannot disagree with the renderer about it.
        info = without_comments(
            function_body(building, "bool building_polygon_surface_info(")
        )
        self.assertIn("pInfo->bPairTextureEnabled = false;", info)
        self.assertNotIn("ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL", info)
        self.assertNotIn("bPairTextureEnabled", function_body(
            building, "void DrawBuilding("))

    def test_wrapped_pair_transforms_are_flagged_not_silently_wrong(self) -> None:
        api = (ROLLER / "editor_api.h").read_text(encoding="utf-8")
        source = (ROLLER / "editor_surface.c").read_text(encoding="utf-8")

        self.assertIn("ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW", api)
        builder = without_comments(
            function_body(source, "static bool ed_build_material(")
        )
        self.assertIn("ed_atlas_pair_wraps_row(pAtlas, uiTileIndex)", builder)
        self.assertIn("ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW", builder)


if __name__ == "__main__":
    unittest.main()
