#include "editor_surface.h"
#include "types.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    tEdSurfaceEmission Surface;
    int iCalls;
} tEmissionCapture;

typedef struct
{
    uint32_t auiChunkIds[16];
    uint32_t uiCount;
    uint32_t uiFailAt;
    float fUnrelatedCameraPosition[3];
} tChunkTraversalCapture;

static void capture_emission(const tEdSurfaceEmission *pSurface,
                             void *pUserData)
{
    tEmissionCapture *pCapture = pUserData;
    pCapture->Surface = *pSurface;
    pCapture->iCalls++;
}

static bool capture_chunk(uint32_t uiChunkId, void *pUserData)
{
    tChunkTraversalCapture *pCapture = pUserData;
    if (uiChunkId == pCapture->uiFailAt)
        return false;
    assert(pCapture->uiCount
           < sizeof(pCapture->auiChunkIds) / sizeof(pCapture->auiChunkIds[0]));
    pCapture->auiChunkIds[pCapture->uiCount++] = uiChunkId;
    return true;
}

static void assert_float_near(float fActual, float fExpected)
{
    assert(fabsf(fActual - fExpected) < 0.000001f);
}

static void assert_no_atlas_identity(const tEdMaterial *pMaterial)
{
    assert(pMaterial->uiTileIndex == 0u);
    assert_float_near(pMaterial->fAtlasScale[0], 0.0f);
    assert_float_near(pMaterial->fAtlasScale[1], 0.0f);
    assert_float_near(pMaterial->fAtlasBias[0], 0.0f);
    assert_float_near(pMaterial->fAtlasBias[1], 0.0f);
}

static tEdSurfaceInfo make_info(uint32_t uiFlags)
{
    tEdSurfaceInfo Info = {
        .uiChunkId = 417u,
        .uiRenderFlags = uiFlags,
        .uiBackSurfaceFlags = ED_MATERIAL_ID_NONE,
        .uiTextureSet = 0u,
        .fSubdivideThreshold = 1234.5f,
        .bPairTextureEnabled =
            (uiFlags & SURFACE_FLAG_TEXTURE_PAIR) != 0,
        .bHighVariant = true,
        .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
        .unContentClass = ROLLER_ED_CONTENT_AUTHORED_TRACK,
        .byTopology = ROLLER_ED_TOPOLOGY_QUAD,
        .byRenderUVLayout = (uiFlags & SURFACE_FLAG_TEXTURE_PAIR)
            ? ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL
            : ROLLER_ED_RENDER_UV_TILE,
        .iRenderSubdivideType = 29
    };
    return Info;
}

static void test_exact_fixed_uvs_and_identity(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = {
        { 10.0f, 20.0f, 30.0f },
        { 40.0f, 50.0f, 60.0f },
        { 70.0f, 80.0f, 90.0f },
        { 11.0f, 22.0f, 33.0f }
    };
    static const int32_t aiExpectedU[ED_SURFACE_VERTEX_COUNT] = {
        0x7FF000, 0, 0, 0x7FF000
    };
    static const int32_t aiExpectedV[ED_SURFACE_VERTEX_COUNT] = {
        0, 0, 0x3FF000, 0x3FF000
    };
    tEdMaterial aMaterials[2];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE
        | SURFACE_FLAG_TEXTURE_PAIR
        | SURFACE_FLAG_FLIP_BACKFACE
        | 2u);

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.iCalls == 1);
    assert(Capture.Surface.uiVertexCount == ED_SURFACE_VERTEX_COUNT);
    assert(Capture.Surface.uiChunkId == 417u);
    assert(Capture.Surface.unSurfaceClass
           == ROLLER_ED_SURFACE_CLASS_LEFT_WALL);
    assert(Capture.Surface.unContentClass
           == ROLLER_ED_CONTENT_AUTHORED_TRACK);
    assert(Capture.Surface.byTopology == ROLLER_ED_TOPOLOGY_QUAD);
    assert(Capture.Surface.uiBackMaterialId == ED_MATERIAL_ID_NONE);
    assert((Capture.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_TWO_SIDED) != 0);
    assert((Capture.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_TEXTURED) != 0);
    assert((Capture.Surface.unFlags
            & ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE) != 0);
    assert((Capture.Surface.unFlags
            & ROLLER_ED_SURFACE_FLAG_HIGH_VARIANT) != 0);
    assert_float_near(Capture.Surface.fSubdivideThreshold, 1234.5f);
    assert(Capture.Surface.iRenderSubdivideType == 29);

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        assert(memcmp(Capture.Surface.aVertices[i].fPosition,
                      afWorld[i], sizeof(afWorld[i])) == 0);
        assert(Capture.Surface.aVertices[i].iRenderU16_16
               == aiExpectedU[i]);
        assert(Capture.Surface.aVertices[i].iRenderV16_16
               == aiExpectedV[i]);
    }

    const tEdMaterial *pFront = ed_material_table_get(
        &Table, Capture.Surface.uiFrontMaterialId);
    assert(pFront);
    assert(pFront->uiKind == ROLLER_ED_MATERIAL_TEXTURED_PAIR);
    assert(pFront->uiTileIndex == 2u);
    assert_float_near(pFront->fAtlasScale[0], 0.5f);
    assert_float_near(pFront->fAtlasScale[1], 0.5f);
    assert_float_near(pFront->fAtlasBias[0], 0.5f);
    assert_float_near(pFront->fAtlasBias[1], 0.0f);
}

static void test_low_resolution_fixed_uvs(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[1];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 32u, 32u, 8u
    };
    tEdSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR);

    assert(ed_material_table_init(&Table, aMaterials, 1u, Atlas));
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.Surface.aVertices[0].iRenderU16_16 == 0x3FF000);
    assert(Capture.Surface.aVertices[2].iRenderV16_16 == 0x1FF000);
}

static void test_reverse_material_and_generated_back_face(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[2];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK | 1u);
    Info.uiBackSurfaceFlags = 6u;
    Info.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.Surface.uiBackMaterialId != ED_MATERIAL_ID_NONE);
    assert(Capture.Surface.uiBackMaterialId
           != Capture.Surface.uiFrontMaterialId);

    const tEdMaterial *pFront = ed_material_table_get(
        &Table, Capture.Surface.uiFrontMaterialId);
    const tEdMaterial *pBack = ed_material_table_get(
        &Table, Capture.Surface.uiBackMaterialId);
    float afFrontAtlasUV[2];
    float afGeneratedBackAtlasUV[2];
    assert(pFront && pBack);
    assert(pFront->uiTileIndex == 1u);
    assert(pBack->uiTileIndex == 6u);

    ed_material_resolve_uv(
        pFront, Capture.Surface.aVertices[0].fMaterialUV, afFrontAtlasUV);
    ed_material_resolve_uv(
        pBack, Capture.Surface.aVertices[0].fMaterialUV,
        afGeneratedBackAtlasUV);
    assert(afFrontAtlasUV[0] < 0.5f);
    assert(afGeneratedBackAtlasUV[0] >= 0.5f);
    assert(afGeneratedBackAtlasUV[1] >= 0.5f);
}

static void test_paired_mapping_in_both_directions(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aForwardMaterials[1];
    tEdMaterial aReverseMaterials[1];
    tEdMaterialTable ForwardTable;
    tEdMaterialTable ReverseTable;
    tEmissionCapture Forward = { 0 };
    tEmissionCapture Reverse = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo ForwardInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR);
    tEdSurfaceInfo ReverseInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR
        | SURFACE_FLAG_FLIP_HORIZ);

    assert(ed_material_table_init(
        &ForwardTable, aForwardMaterials, 1u, Atlas));
    assert(ed_material_table_init(
        &ReverseTable, aReverseMaterials, 1u, Atlas));
    assert(ed_emit_surface(
        afWorld, &ForwardInfo, &ForwardTable, capture_emission, &Forward));
    assert(ed_emit_surface(
        afWorld, &ReverseInfo, &ReverseTable, capture_emission, &Reverse));

    assert(Forward.Surface.aVertices[0].fMaterialUV[0]
           > Forward.Surface.aVertices[1].fMaterialUV[0]);
    assert(Reverse.Surface.aVertices[0].fMaterialUV[0]
           < Reverse.Surface.aVertices[1].fMaterialUV[0]);

    const tEdMaterial *pForwardMaterial = ed_material_table_get(
        &ForwardTable, Forward.Surface.uiFrontMaterialId);
    const tEdMaterial *pReverseMaterial = ed_material_table_get(
        &ReverseTable, Reverse.Surface.uiFrontMaterialId);
    float afForward0[2];
    float afForward1[2];
    float afReverse0[2];
    float afReverse1[2];
    ed_material_resolve_uv(
        pForwardMaterial, Forward.Surface.aVertices[0].fMaterialUV,
        afForward0);
    ed_material_resolve_uv(
        pForwardMaterial, Forward.Surface.aVertices[1].fMaterialUV,
        afForward1);
    ed_material_resolve_uv(
        pReverseMaterial, Reverse.Surface.aVertices[0].fMaterialUV,
        afReverse0);
    ed_material_resolve_uv(
        pReverseMaterial, Reverse.Surface.aVertices[1].fMaterialUV,
        afReverse1);
    assert(afForward0[0] > afForward1[0]);
    assert(afReverse0[0] < afReverse1[0]);
    assert_float_near(afForward0[0], 0.499755859375f);
    assert_float_near(afForward1[0], 0.0f);
    assert_float_near(afReverse0[0], 0.0f);
    assert_float_near(afReverse1[0], 0.499755859375f);
}

static void test_export_mapping_uses_material_transform(void)
{
    tEdMaterial Material;
    float afMaterialUV[2] = { 0.25f, 0.75f };
    float afAtlasUV[2] = { 0 };
    memset(&Material, 0, sizeof(Material));

    /* This deliberately does not correspond to uiTileIndex. A consumer that
     * derives tile arithmetic from the index instead of using the material
     * record cannot produce these coordinates. */
    Material.uiTileIndex = 203u;
    Material.fAtlasScale[0] = 0.125f;
    Material.fAtlasScale[1] = 0.25f;
    Material.fAtlasBias[0] = 0.375f;
    Material.fAtlasBias[1] = 0.5f;
    ed_material_resolve_uv(&Material, afMaterialUV, afAtlasUV);
    assert_float_near(afAtlasUV[0], 0.40625f);
    assert_float_near(afAtlasUV[1], 0.6875f);
}

static void test_generic_identity_layout_and_skip(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    int32_t aiRenderU[ED_SURFACE_VERTEX_COUNT];
    int32_t aiRenderV[ED_SURFACE_VERTEX_COUNT];
    tEdMaterial aMaterials[1];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 64u, 64u, 0u
    };
    tEdSurfaceInfo Info = make_info(
        SURFACE_FLAG_TRANSPARENT | SURFACE_FLAG_FLIP_BACKFACE | 5u);
    Info.uiChunkId = ROLLER_ED_INVALID_CHUNK_ID;
    Info.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_SIGN;
    Info.unContentClass = ROLLER_ED_CONTENT_AUTHORED_SIGN;
    Info.bHighVariant = false;

    assert(ed_surface_compute_render_uvs(
        ROLLER_ED_RENDER_UV_PAIR_VERTICAL, false,
        aiRenderU, aiRenderV));
    assert(aiRenderU[0] == 0x3FF000);
    assert(aiRenderV[2] == 0x7FF000);

    assert(ed_material_table_init(&Table, aMaterials, 1u, Atlas));
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.iCalls == 1);
    assert(Capture.Surface.uiChunkId == ROLLER_ED_INVALID_CHUNK_ID);
    assert(Capture.Surface.unSurfaceClass == ROLLER_ED_SURFACE_CLASS_SIGN);
    assert(Capture.Surface.unContentClass == ROLLER_ED_CONTENT_AUTHORED_SIGN);
    assert((Capture.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_ALPHA) != 0);
    assert((Capture.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_TWO_SIDED) != 0);
    const tEdMaterial *pMaterial = ed_material_table_get(
        &Table, Capture.Surface.uiFrontMaterialId);
    assert(pMaterial);
    assert(pMaterial->uiKind == ROLLER_ED_MATERIAL_SCREEN_DARKEN);

    Capture.iCalls = 0;
    Info.uiRenderFlags |= SURFACE_FLAG_SKIP_RENDER;
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.iCalls == 0);
}

/*
 * The main-track and building/sign banks have independent tile counts, so a
 * single stream carrying both must resolve tile identity and the atlas
 * transform against the surface's own set.
 */
static void test_separate_texture_sets_keep_their_own_tile_identity(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[4];
    tEdMaterialTable Table;
    tEmissionCapture Track = { 0 };
    tEmissionCapture Building = { 0 };
    tEdTextureAtlas TrackAtlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdTextureAtlas BuildingAtlas = {
        ROLLER_ED_TEXTURE_SET_BUILDING_SIGN, 256u, 256u, 64u, 15u
    };
    tEdSurfaceInfo TrackInfo = make_info(SURFACE_FLAG_APPLY_TEXTURE | 5u);
    tEdSurfaceInfo BuildingInfo = make_info(SURFACE_FLAG_APPLY_TEXTURE | 5u);
    TrackInfo.bPairTextureEnabled = false;
    BuildingInfo.bPairTextureEnabled = false;
    BuildingInfo.uiTextureSet = ROLLER_ED_TEXTURE_SET_BUILDING_SIGN;
    BuildingInfo.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_BUILDING;
    BuildingInfo.unContentClass = ROLLER_ED_CONTENT_AUTHORED_SCENERY;

    assert(ed_material_table_init(&Table, aMaterials, 4u, TrackAtlas));
    assert(ed_material_table_set_atlas(&Table, BuildingAtlas));
    assert(ed_material_table_atlas(&Table, ROLLER_ED_TEXTURE_SET_TRACK));
    assert(ed_material_table_atlas(
        &Table, ROLLER_ED_TEXTURE_SET_BUILDING_SIGN));
    assert(!ed_material_table_atlas(&Table, 3u));

    assert(ed_emit_surface(
        afWorld, &TrackInfo, &Table, capture_emission, &Track));
    assert(ed_emit_surface(
        afWorld, &BuildingInfo, &Table, capture_emission, &Building));

    const tEdMaterial *pTrack = ed_material_table_get(
        &Table, Track.Surface.uiFrontMaterialId);
    const tEdMaterial *pBuilding = ed_material_table_get(
        &Table, Building.Surface.uiFrontMaterialId);
    assert(pTrack && pBuilding);
    assert(Track.Surface.uiFrontMaterialId
           != Building.Surface.uiFrontMaterialId);
    assert(pTrack->uiTextureSet == ROLLER_ED_TEXTURE_SET_TRACK);
    assert(pBuilding->uiTextureSet == ROLLER_ED_TEXTURE_SET_BUILDING_SIGN);
    assert(pTrack->uiTileIndex == 5u && pBuilding->uiTileIndex == 5u);

    /* Same tile index, different bank height: the V transform differs. */
    assert_float_near(pTrack->fAtlasBias[0], 0.25f);
    assert_float_near(pTrack->fAtlasBias[1], 0.5f);
    assert_float_near(pTrack->fAtlasScale[1], 0.5f);
    assert_float_near(pBuilding->fAtlasBias[0], 0.25f);
    assert_float_near(pBuilding->fAtlasBias[1], 0.25f);
    assert_float_near(pBuilding->fAtlasScale[1], 0.25f);

    /* A textured surface in an unregistered set has no tile identity. */
    tEdSurfaceInfo UnknownInfo = TrackInfo;
    UnknownInfo.uiTextureSet = 3u;
    assert(!ed_emit_surface(
        afWorld, &UnknownInfo, &Table, capture_emission, &Track));

    /* Atlases must agree on the globally selected legacy tile size. */
    tEdTextureAtlas MismatchedAtlas = {
        ROLLER_ED_TEXTURE_SET_BUILDING_SIGN, 256u, 128u, 32u, 15u
    };
    assert(!ed_material_table_set_atlas(&Table, MismatchedAtlas));
}

/*
 * The renderer only has a pair texture while a following tile exists and
 * falls back to the plain tile otherwise, so the emitted material kind and
 * the render UV span must fall back together.
 */
static void test_pair_falls_back_when_the_atlas_has_no_successor(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[4];
    tEdMaterialTable Table;
    tEmissionCapture Paired = { 0 };
    tEmissionCapture LastTile = { 0 };
    tEmissionCapture Wrapped = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo PairedInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR | 1u);
    tEdSurfaceInfo LastTileInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR | 7u);
    tEdSurfaceInfo WrappedInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR | 3u);

    assert(ed_atlas_pair_available(&Atlas, 6u));
    assert(!ed_atlas_pair_available(&Atlas, 7u));
    assert(!ed_atlas_pair_wraps_row(&Atlas, 2u));
    assert(ed_atlas_pair_wraps_row(&Atlas, 3u));

    assert(ed_material_table_init(&Table, aMaterials, 4u, Atlas));
    assert(ed_emit_surface(
        afWorld, &PairedInfo, &Table, capture_emission, &Paired));
    assert(ed_emit_surface(
        afWorld, &LastTileInfo, &Table, capture_emission, &LastTile));
    assert(ed_emit_surface(
        afWorld, &WrappedInfo, &Table, capture_emission, &Wrapped));

    const tEdMaterial *pPaired = ed_material_table_get(
        &Table, Paired.Surface.uiFrontMaterialId);
    const tEdMaterial *pLastTile = ed_material_table_get(
        &Table, LastTile.Surface.uiFrontMaterialId);
    const tEdMaterial *pWrapped = ed_material_table_get(
        &Table, Wrapped.Surface.uiFrontMaterialId);
    assert(pPaired && pLastTile && pWrapped);

    assert(pPaired->uiKind == ROLLER_ED_MATERIAL_TEXTURED_PAIR);
    assert_float_near(pPaired->fAtlasScale[0], 0.5f);
    assert((Paired.Surface.unFlags
            & ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE) != 0);
    assert(Paired.Surface.aVertices[0].iRenderU16_16 == 0x7FF000);
    assert((pPaired->uiFlags
            & ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW) == 0);

    /* Tile 7 is the last tile: no pair texture exists for it. */
    assert(pLastTile->uiKind == ROLLER_ED_MATERIAL_TEXTURED_TILE);
    assert_float_near(pLastTile->fAtlasScale[0], 0.25f);
    assert((LastTile.Surface.unFlags
            & ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE) == 0);
    assert(LastTile.Surface.aVertices[0].iRenderU16_16 == 0x3FF000);

    /* Tile 3 ends its atlas row, so its pair's right half comes from the
     * next row and the transform is flagged as an approximation. */
    assert(pWrapped->uiKind == ROLLER_ED_MATERIAL_TEXTURED_PAIR);
    assert((pWrapped->uiFlags
            & ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW) != 0);
}

/*
 * Back materials must reproduce the draw-time texture_back[] substitution,
 * including the sentinel cases where no alternate reverse material exists.
 */
static void test_back_material_matches_the_draw_time_substitution(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[4];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK | 2u);
    Info.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 4u, Atlas));

    /* Identical substitute tile: no alternate reverse material. */
    Info.uiBackSurfaceFlags = 2u;
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.Surface.uiBackMaterialId == ED_MATERIAL_ID_NONE);

    /* Substitute tile past the bank's tile count: the renderer ignores it. */
    Info.uiBackSurfaceFlags = 9u;
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.Surface.uiBackMaterialId == ED_MATERIAL_ID_NONE);

    /* No SURFACE_FLAG_BACK: the reverse side reuses the front material. */
    Info.uiRenderFlags &= ~(uint32_t)SURFACE_FLAG_BACK;
    Info.uiBackSurfaceFlags = 6u;
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    assert(Capture.Surface.uiBackMaterialId == ED_MATERIAL_ID_NONE);

    /* A distinct in-range substitute keeps the front surface's other flags
     * and only replaces the tile index. */
    Info.uiRenderFlags |= SURFACE_FLAG_BACK;
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));
    const tEdMaterial *pFront = ed_material_table_get(
        &Table, Capture.Surface.uiFrontMaterialId);
    const tEdMaterial *pBack = ed_material_table_get(
        &Table, Capture.Surface.uiBackMaterialId);
    assert(pFront && pBack);
    assert(pFront->uiTileIndex == 2u && pBack->uiTileIndex == 6u);
    assert(pBack->uiTextureSet == pFront->uiTextureSet);
    assert(pBack->uiKind == pFront->uiKind);
    assert(pBack->uiFlags == pFront->uiFlags);

    /* An untextured surface has no tile to substitute. */
    tEdSurfaceInfo FlatInfo = make_info(SURFACE_FLAG_BACK | 2u);
    FlatInfo.bPairTextureEnabled = false;
    FlatInfo.uiBackSurfaceFlags = 6u;
    assert(ed_emit_surface(
        afWorld, &FlatInfo, &Table, capture_emission, &Capture));
    assert(Capture.Surface.uiBackMaterialId == ED_MATERIAL_ID_NONE);
}

/*
 * Non-textured legacy surface paths must reach the export representation as
 * an explicit material kind rather than as a texture nobody can resolve.
 */
static void test_non_textured_surfaces_carry_their_material_kind(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[4];
    tEdMaterialTable Table;
    tEmissionCapture Flat = { 0 };
    tEmissionCapture Darken = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo FlatInfo = make_info(231u);
    tEdSurfaceInfo DarkenInfo = make_info(SURFACE_FLAG_TRANSPARENT | 3u);
    FlatInfo.bPairTextureEnabled = false;
    DarkenInfo.bPairTextureEnabled = false;
    FlatInfo.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_TOWER;
    FlatInfo.unContentClass = ROLLER_ED_CONTENT_RUNTIME_SCENERY;
    FlatInfo.uiChunkId = ROLLER_ED_INVALID_CHUNK_ID;

    assert(ed_material_table_init(&Table, aMaterials, 4u, Atlas));
    assert(ed_emit_surface(
        afWorld, &FlatInfo, &Table, capture_emission, &Flat));
    assert(ed_emit_surface(
        afWorld, &DarkenInfo, &Table, capture_emission, &Darken));

    const tEdMaterial *pFlat = ed_material_table_get(
        &Table, Flat.Surface.uiFrontMaterialId);
    const tEdMaterial *pDarken = ed_material_table_get(
        &Table, Darken.Surface.uiFrontMaterialId);
    assert(pFlat && pDarken);

    assert(pFlat->uiKind == ROLLER_ED_MATERIAL_FLAT_PALETTE_COLOR);
    assert(pFlat->uiPaletteColour == 231u);
    assert(pFlat->uiDarkenLevel == 0u);
    assert(pDarken->uiKind == ROLLER_ED_MATERIAL_SCREEN_DARKEN);
    assert(pDarken->uiDarkenLevel == 3u);
    assert(pDarken->uiPaletteColour == 0u);

    /* Neither kind claims tile identity or an atlas rectangle. */
    assert_no_atlas_identity(pFlat);
    assert_no_atlas_identity(pDarken);
    assert((Flat.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_TEXTURED) == 0);
    assert((Darken.Surface.unFlags & ROLLER_ED_SURFACE_FLAG_ALPHA) != 0);
}

/* Identity is rejected outright rather than emitted as a misleading value. */
static void test_invalid_identity_is_refused(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = { 0 };
    tEdMaterial aMaterials[2];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Valid = make_info(SURFACE_FLAG_APPLY_TEXTURE | 1u);
    Valid.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_surface_identity_valid(&Valid));

    tEdSurfaceInfo LastChunk = Valid;
    LastChunk.uiChunkId = MAX_TRACK_CHUNKS - 1u;
    assert(ed_surface_identity_valid(&LastChunk));

    tEdSurfaceInfo NotChunkBound = Valid;
    NotChunkBound.uiChunkId = ROLLER_ED_INVALID_CHUNK_ID;
    assert(ed_surface_identity_valid(&NotChunkBound));

    tEdSurfaceInfo OutOfRangeChunk = Valid;
    OutOfRangeChunk.uiChunkId = MAX_TRACK_CHUNKS;
    assert(!ed_surface_identity_valid(&OutOfRangeChunk));
    assert(!ed_emit_surface(
        afWorld, &OutOfRangeChunk, &Table, capture_emission, &Capture));

    tEdSurfaceInfo BadContentClass = Valid;
    BadContentClass.unContentClass =
        (uint16_t)(ROLLER_ED_CONTENT_RUNTIME_SCENERY + 1u);
    assert(!ed_surface_identity_valid(&BadContentClass));
    assert(!ed_emit_surface(
        afWorld, &BadContentClass, &Table, capture_emission, &Capture));

    tEdSurfaceInfo BadSurfaceClass = Valid;
    BadSurfaceClass.unSurfaceClass =
        (uint16_t)(ROLLER_ED_SURFACE_CLASS_TOWER + 1u);
    assert(!ed_surface_identity_valid(&BadSurfaceClass));
    assert(!ed_emit_surface(
        afWorld, &BadSurfaceClass, &Table, capture_emission, &Capture));

    tEdSurfaceInfo BadTopology = Valid;
    BadTopology.byTopology = (uint8_t)(ROLLER_ED_TOPOLOGY_QUAD + 1u);
    assert(!ed_surface_identity_valid(&BadTopology));

    tEdSurfaceInfo BadTile = Valid;
    BadTile.uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE | 8u;
    assert(ed_surface_identity_valid(&BadTile));
    assert(!ed_emit_surface(
        afWorld, &BadTile, &Table, capture_emission, &Capture));

    assert(Capture.iCalls == 0);
}

static void cross3(const float afLeft[3],
                   const float afRight[3],
                   float afOut[3])
{
    afOut[0] = afLeft[1] * afRight[2] - afLeft[2] * afRight[1];
    afOut[1] = afLeft[2] * afRight[0] - afLeft[0] * afRight[2];
    afOut[2] = afLeft[0] * afRight[1] - afLeft[1] * afRight[0];
}

static float dot3(const float afLeft[3], const float afRight[3])
{
    return afLeft[0] * afRight[0]
         + afLeft[1] * afRight[1]
         + afLeft[2] * afRight[2];
}

static void assert_unit_length(const float afVector[3])
{
    assert_float_near(sqrtf(dot3(afVector, afVector)), 1.0f);
}

/*
 * The renderer decides front versus back with bnrm = (v1-v0) x (v3-v0)
 * (scene_render_gpu.c), calling the surface front-facing when that vector
 * points at the camera. The emitted normal must be that same direction, or
 * exporters would generate reversed faces relative to the front material.
 */
static void test_normal_agrees_with_the_renderer_front_face_rule(void)
{
    /* A flat quad in the world XY plane, wound so its front faces +Z up. */
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = {
        {   0.0f,   0.0f, 100.0f },
        { 200.0f,   0.0f, 100.0f },
        { 200.0f, 300.0f, 100.0f },
        {   0.0f, 300.0f, 100.0f }
    };
    static const float afFrontCamera[3] = { 100.0f, 150.0f, 900.0f };
    static const float afBackCamera[3] = { 100.0f, 150.0f, -900.0f };
    tEdMaterial aMaterials[2];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info = make_info(SURFACE_FLAG_APPLY_TEXTURE | 1u);
    float afEdgeNext[3];
    float afEdgePrev[3];
    float afRendererNormal[3];
    float afToFront[3];
    float afToBack[3];
    Info.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_surface(
        afWorld, &Info, &Table, capture_emission, &Capture));

    for (uint32_t i = 0; i < 3; i++) {
        afEdgeNext[i] = afWorld[1][i] - afWorld[0][i];
        afEdgePrev[i] = afWorld[3][i] - afWorld[0][i];
        afToFront[i] = afFrontCamera[i] - afWorld[0][i];
        afToBack[i] = afBackCamera[i] - afWorld[0][i];
    }
    cross3(afEdgeNext, afEdgePrev, afRendererNormal);

    /* Same direction as the renderer's facing vector, and unit length. */
    assert_unit_length(Capture.Surface.fNormal);
    assert(dot3(Capture.Surface.fNormal, afRendererNormal) > 0.0f);
    assert_float_near(Capture.Surface.fNormal[0], 0.0f);
    assert_float_near(Capture.Surface.fNormal[1], 0.0f);
    assert_float_near(Capture.Surface.fNormal[2], 1.0f);

    /* Positive toward a camera the renderer would call front-facing, and
     * negative toward one it would apply texture_back[] for. */
    assert(dot3(Capture.Surface.fNormal, afToFront) > 0.0f);
    assert(dot3(Capture.Surface.fNormal, afToBack) < 0.0f);

    /* A flat quad's vertex normals all equal the surface normal. */
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        assert_unit_length(Capture.Surface.aVertices[i].fNormal);
        for (uint32_t iAxis = 0; iAxis < 3; iAxis++)
            assert_float_near(Capture.Surface.aVertices[i].fNormal[iAxis],
                              Capture.Surface.fNormal[iAxis]);
    }

    /* Reversing the winding reverses the front face, and nothing else. */
    float afReversed[ED_SURFACE_VERTEX_COUNT][3];
    tEmissionCapture ReversedCapture = { 0 };
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++)
        memcpy(afReversed[i],
               afWorld[ED_SURFACE_VERTEX_COUNT - 1u - i],
               sizeof(afReversed[i]));
    assert(ed_emit_surface(
        afReversed, &Info, &Table, capture_emission, &ReversedCapture));
    assert_float_near(ReversedCapture.Surface.fNormal[2], -1.0f);
}

/*
 * Track sections may twist, so a normal taken from three of four corners is
 * not good enough: each vertex carries its own adjacent-edge normal.
 */
static void test_twisted_quads_get_per_vertex_normals(void)
{
    /* v2 lifted out of the plane of the other three. */
    static const float afTwisted[ED_SURFACE_VERTEX_COUNT][3] = {
        {   0.0f,   0.0f,   0.0f },
        { 100.0f,   0.0f,   0.0f },
        { 100.0f, 100.0f, 100.0f },
        {   0.0f, 100.0f,   0.0f }
    };
    float afSurfaceNormal[3];
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3];

    assert(ed_surface_compute_normals(
        afTwisted, afSurfaceNormal, afVertexNormals));
    assert_unit_length(afSurfaceNormal);
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        assert_unit_length(afVertexNormals[i]);
        /* Every corner still faces the same hemisphere as the quad. */
        assert(dot3(afVertexNormals[i], afSurfaceNormal) > 0.0f);
    }
    /* The lifted corner genuinely differs from the whole-quad normal, which
     * is the whole reason for carrying per-vertex normals at all. */
    assert(dot3(afVertexNormals[2], afSurfaceNormal) < 0.999f);
}

/*
 * TRACK3 chunk 124's right shoulder pinches v0 onto v1 to within a unit at
 * coordinates near 90000. The sliver triangle at those corners is nearly
 * perpendicular to the quad, so its cross product is not a usable shading
 * normal; those corners must take the whole-quad normal instead.
 */
static void test_pinched_corners_fall_back_to_the_quad_normal(void)
{
    static const float afPinched[ED_SURFACE_VERTEX_COUNT][3] = {
        { -54661.0f,   -92631.8f, 19056.1f },
        { -54660.3f,   -92632.3f, 19056.5f },
        { -54148.5f,   -91660.8f, 20060.2f },
        { -54499.6f,   -91889.7f, 20682.2f }
    };
    float afSurfaceNormal[3];
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3];

    assert(ed_surface_compute_normals(
        afPinched, afSurfaceNormal, afVertexNormals));
    assert_unit_length(afSurfaceNormal);

    /* The two pinched corners take the quad normal verbatim. */
    for (uint32_t i = 0; i < 2u; i++) {
        for (uint32_t iAxis = 0; iAxis < 3; iAxis++)
            assert_float_near(afVertexNormals[i][iAxis],
                              afSurfaceNormal[iAxis]);
    }
    /* The two well-formed corners keep their own, and still face the same
     * hemisphere as the quad. */
    for (uint32_t i = 2u; i < ED_SURFACE_VERTEX_COUNT; i++) {
        assert_unit_length(afVertexNormals[i]);
        assert(dot3(afVertexNormals[i], afSurfaceNormal) > 0.0f);
    }
}

/* Degenerate geometry publishes an absent normal, never a NaN. */
static void test_degenerate_quads_publish_zero_normals(void)
{
    static const float afCollapsed[ED_SURFACE_VERTEX_COUNT][3] = {
        { 5.0f, 6.0f, 7.0f },
        { 5.0f, 6.0f, 7.0f },
        { 5.0f, 6.0f, 7.0f },
        { 5.0f, 6.0f, 7.0f }
    };
    /* All four corners collinear along X, so the quad encloses no area. */
    static const float afSliver[ED_SURFACE_VERTEX_COUNT][3] = {
        {   0.0f, 0.0f, 0.0f },
        { 100.0f, 0.0f, 0.0f },
        { 200.0f, 0.0f, 0.0f },
        { 100.0f, 0.0f, 0.0f }
    };
    float afSurfaceNormal[3];
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3];

    assert(!ed_surface_compute_normals(
        afCollapsed, afSurfaceNormal, afVertexNormals));
    for (uint32_t iAxis = 0; iAxis < 3; iAxis++)
        assert(afSurfaceNormal[iAxis] == 0.0f);
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        for (uint32_t iAxis = 0; iAxis < 3; iAxis++)
            assert(afVertexNormals[i][iAxis] == 0.0f);
    }

    assert(!ed_surface_compute_normals(
        afSliver, afSurfaceNormal, afVertexNormals));
    for (uint32_t iAxis = 0; iAxis < 3; iAxis++)
        assert(afSurfaceNormal[iAxis] == 0.0f);

    /* Per-vertex output is optional; a null destination is not an error. */
    assert(!ed_surface_compute_normals(afSliver, afSurfaceNormal, NULL));
    assert(!ed_surface_compute_normals(NULL, afSurfaceNormal, NULL));
}

/*
 * World +Z is up and the emitter applies no scale or axis conversion: the
 * positions it publishes are the ones the producer handed it.
 */
static void test_world_axes_and_scale_pass_through_unchanged(void)
{
    /* A wall standing upright: constant world Z along each edge pair, and a
     * normal that lies flat in the XY plane because +Z is the up axis. */
    static const float afWall[ED_SURFACE_VERTEX_COUNT][3] = {
        {   0.0f, 0.0f, 500.0f },
        { 400.0f, 0.0f, 500.0f },
        { 400.0f, 0.0f,   0.0f },
        {   0.0f, 0.0f,   0.0f }
    };
    tEdMaterial aMaterials[2];
    tEdMaterialTable Table;
    tEmissionCapture Capture = { 0 };
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info = make_info(SURFACE_FLAG_APPLY_TEXTURE | 1u);
    Info.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_surface(
        afWall, &Info, &Table, capture_emission, &Capture));

    /* Positions are byte-identical: no scale factor, no axis swap. */
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++)
        assert(memcmp(Capture.Surface.aVertices[i].fPosition,
                      afWall[i], sizeof(afWall[i])) == 0);

    /* An upright wall's normal has no component along the up axis. */
    assert_unit_length(Capture.Surface.fNormal);
    assert_float_near(Capture.Surface.fNormal[ED_SURFACE_WORLD_UP_AXIS], 0.0f);

    /* Vertices sharing a world-Z value are the ones sharing a V coordinate,
     * which is what makes the UV origin the top-left corner. */
    assert(afWall[0][ED_SURFACE_WORLD_UP_AXIS]
           == afWall[1][ED_SURFACE_WORLD_UP_AXIS]);
    assert_float_near(Capture.Surface.aVertices[0].fMaterialUV[1], 0.0f);
    assert_float_near(Capture.Surface.aVertices[1].fMaterialUV[1], 0.0f);
    assert(Capture.Surface.aVertices[2].fMaterialUV[1] > 0.9f);
    assert(Capture.Surface.aVertices[3].fMaterialUV[1] > 0.9f);
    /* U runs left to right: v1 is the left edge, v0 the right. */
    assert(Capture.Surface.aVertices[1].fMaterialUV[0]
           < Capture.Surface.aVertices[0].fMaterialUV[0]);
}

static void test_selection_uses_only_canonical_identity(void)
{
    tEdSurfaceSelection Selection = {
        .uiFirstChunkId = 12u,
        .uiLastChunkId = 10u,
        .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
        .byHighlightColour = 0x8Fu,
        .bEnabled = true
    };
    tEdSurfaceEmission Surface;
    memset(&Surface, 0xA5, sizeof(Surface));
    Surface.uiChunkId = 9u;
    Surface.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL;
    Surface.uiRenderFlags =
        SURFACE_FLAG_APPLY_TEXTURE
        | SURFACE_FLAG_TEXTURE_PAIR
        | SURFACE_FLAG_FLIP_BACKFACE
        | 7u;

    assert(!ed_surface_selection_matches(&Selection, &Surface));
    Surface.uiChunkId = 10u;
    assert(ed_surface_selection_matches(&Selection, &Surface));
    Surface.uiChunkId = 11u;
    assert(ed_surface_selection_matches(&Selection, &Surface));
    Surface.uiChunkId = 12u;
    assert(ed_surface_selection_matches(&Selection, &Surface));
    Surface.uiChunkId = 13u;
    assert(!ed_surface_selection_matches(&Selection, &Surface));

    Surface.uiChunkId = 11u;
    Surface.unSurfaceClass =
        (uint16_t)(ROLLER_ED_SURFACE_CLASS_LEFT_WALL + 1u);
    assert(!ed_surface_selection_matches(&Selection, &Surface));
    Surface.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL;

    uint32_t uiHighlightFlags =
        ed_surface_selection_render_flags(&Selection, &Surface);
    assert((uiHighlightFlags & SURFACE_FLAG_APPLY_TEXTURE) == 0);
    assert((uiHighlightFlags & SURFACE_FLAG_TRANSPARENT) == 0);
    assert((uiHighlightFlags & SURFACE_FLAG_PARTIAL_TRANS) == 0);
    assert((uiHighlightFlags & SURFACE_FLAG_TEXTURE_PAIR) != 0);
    assert((uiHighlightFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0);
    assert((uiHighlightFlags & SURFACE_MASK_TEXTURE_INDEX) == 0x8Fu);

    Selection.bEnabled = false;
    assert(!ed_surface_selection_matches(&Selection, &Surface));
    assert(ed_surface_selection_render_flags(&Selection, &Surface)
           == Surface.uiRenderFlags);
}

static void test_full_track_chunk_traversal_is_complete_and_camera_free(void)
{
    tChunkTraversalCapture CameraA = {
        .uiFailAt = UINT32_MAX,
        .fUnrelatedCameraPosition = { 1.0f, 2.0f, 3.0f }
    };
    tChunkTraversalCapture CameraB = {
        .uiFailAt = UINT32_MAX,
        .fUnrelatedCameraPosition = { -9000.0f, 0.0f, 4500.0f }
    };
    tChunkTraversalCapture StopsOnFailure = {
        .uiFailAt = 2u
    };

    assert(ed_traverse_full_track_chunks(7u, capture_chunk, &CameraA));
    assert(ed_traverse_full_track_chunks(7u, capture_chunk, &CameraB));
    assert(CameraA.uiCount == 7u);
    assert(CameraB.uiCount == 7u);
    assert(memcmp(CameraA.auiChunkIds, CameraB.auiChunkIds,
                  CameraA.uiCount * sizeof(CameraA.auiChunkIds[0])) == 0);
    for (uint32_t i = 0; i < CameraA.uiCount; i++)
        assert(CameraA.auiChunkIds[i] == i);

    assert(ed_traverse_full_track_chunks(0u, capture_chunk, &CameraA));
    assert(!ed_traverse_full_track_chunks(
        5u, capture_chunk, &StopsOnFailure));
    assert(StopsOnFailure.uiCount == 2u);
    assert(!ed_traverse_full_track_chunks(1u, NULL, NULL));
}

/*
 * E4A-S6. The scenery walk is the same ordered, camera-free, stop-on-refusal
 * traversal over placed object indices, so it gets the same guarantees.
 */
static void test_full_scenery_traversal_is_complete_and_camera_free(void)
{
    tChunkTraversalCapture CameraA = {
        .uiFailAt = UINT32_MAX,
        .fUnrelatedCameraPosition = { 1.0f, 2.0f, 3.0f }
    };
    tChunkTraversalCapture CameraB = {
        .uiFailAt = UINT32_MAX,
        .fUnrelatedCameraPosition = { -9000.0f, 0.0f, 4500.0f }
    };
    tChunkTraversalCapture StopsOnFailure = {
        .uiFailAt = 3u
    };

    assert(ed_traverse_full_scenery_objects(5u, capture_chunk, &CameraA));
    assert(ed_traverse_full_scenery_objects(5u, capture_chunk, &CameraB));
    assert(CameraA.uiCount == 5u);
    assert(CameraB.uiCount == 5u);
    assert(memcmp(CameraA.auiChunkIds, CameraB.auiChunkIds,
                  CameraA.uiCount * sizeof(CameraA.auiChunkIds[0])) == 0);
    for (uint32_t i = 0; i < CameraA.uiCount; i++)
        assert(CameraA.auiChunkIds[i] == i);

    /* A track with no placed scenery is an ordinary, successful traversal. */
    assert(ed_traverse_full_scenery_objects(0u, capture_chunk, &CameraA));
    assert(!ed_traverse_full_scenery_objects(
        9u, capture_chunk, &StopsOnFailure));
    assert(StopsOnFailure.uiCount == 3u);
    assert(!ed_traverse_full_scenery_objects(1u, NULL, NULL));
}

/*
 * E4A-S6. The one refusal a producer is allowed to swallow: a textured surface
 * whose tile is not in its bank. The renderer already drops such a quad, and
 * retail TRACK5's advert list contains one, so failing the whole extraction
 * over it would make that track unexportable.
 */
static void test_unresolvable_texture_is_distinguishable_from_other_refusals(
    void)
{
    tEdMaterial aStorage[8];
    tEdMaterialTable Table;
    tEdTextureAtlas Atlas = {
        ROLLER_ED_TEXTURE_SET_TRACK, 256u, 128u, 64u, 8u
    };
    tEdSurfaceInfo Info;

    assert(ed_material_table_init(&Table, aStorage, 8u, Atlas));

    memset(&Info, 0, sizeof(Info));
    Info.uiChunkId = 3u;
    Info.uiTextureSet = ROLLER_ED_TEXTURE_SET_TRACK;
    Info.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_BUILDING;
    Info.unContentClass = ROLLER_ED_CONTENT_AUTHORED_SIGN;
    Info.byTopology = ROLLER_ED_TOPOLOGY_QUAD;
    Info.byRenderUVLayout = ROLLER_ED_RENDER_UV_TILE;

    /* Last tile in the bank resolves; one past the end does not. */
    Info.uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE | 7u;
    assert(ed_surface_material_resolvable(&Table, &Info));
    Info.uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE | 8u;
    assert(!ed_surface_material_resolvable(&Table, &Info));

    /* An untextured surface has no tile to resolve, whatever the low byte
     * happens to hold -- it is a palette colour or a darken level. */
    Info.uiRenderFlags = 200u;
    assert(ed_surface_material_resolvable(&Table, &Info));

    /* A bank the table never registered cannot resolve anything. */
    Info.uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE | 1u;
    Info.uiTextureSet = ROLLER_ED_TEXTURE_SET_TRACK + 3u;
    assert(!ed_surface_material_resolvable(&Table, &Info));
    assert(!ed_surface_material_resolvable(NULL, &Info));
    assert(!ed_surface_material_resolvable(&Table, NULL));
}

/*
 * E3A-S2. The wireframe ribbon has to satisfy three things the renderer cares
 * about: it lies in the surface's plane (so it traces the edge rather than
 * cutting through neighbours), it is wound front-face-out by the same
 * right-hand rule E4A-S4 recorded (so the facing test does not cull it), and
 * it is biased toward the front (so it wins the depth test against its own
 * surface).
 */
/* Ribbon corners are sums of track-scale coordinates, where a float's own
 * resolution is already coarser than assert_float_near's fixed tolerance. */
static void assert_float_within(float fActual, float fExpected,
                                float fTolerance)
{
    assert(fabsf(fActual - fExpected) <= fTolerance);
}

static void test_wireframe_edges_trace_the_surface_front_face(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = {
        {   0.0f,   0.0f, 100.0f },
        { 200.0f,   0.0f, 100.0f },
        { 200.0f, 300.0f, 100.0f },
        {   0.0f, 300.0f, 100.0f }
    };
    tEdSurfaceEmission Surface;
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3];

    memset(&Surface, 0, sizeof(Surface));
    Surface.uiVertexCount = ED_SURFACE_VERTEX_COUNT;
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        Surface.aVertices[i].fPosition[0] = afWorld[i][0];
        Surface.aVertices[i].fPosition[1] = afWorld[i][1];
        Surface.aVertices[i].fPosition[2] = afWorld[i][2];
    }
    assert(ed_surface_compute_normals(afWorld, Surface.fNormal, NULL));
    /* Wound counter-clockwise seen from +Z, so the front face is +Z up. */
    assert_float_near(Surface.fNormal[2], 1.0f);

    for (uint32_t uiEdge = 0; uiEdge < ED_SURFACE_VERTEX_COUNT; uiEdge++) {
        const float *pfStart = Surface.aVertices[uiEdge].fPosition;
        const float *pfEnd =
            Surface.aVertices[(uiEdge + 1u) % ED_SURFACE_VERTEX_COUNT]
                .fPosition;
        float afEdgeNormal[3];
        float afCentre[3] = { 0.0f, 0.0f, 0.0f };

        assert(ed_surface_wireframe_edge_quad(&Surface, uiEdge, afEdgeQuad));
        assert(ed_surface_compute_normals(
            (const float (*)[3])afEdgeQuad, afEdgeNormal, NULL));
        /* Same front face as the surface it outlines. */
        assert_float_within(afEdgeNormal[0], Surface.fNormal[0], 0.00001f);
        assert_float_within(afEdgeNormal[1], Surface.fNormal[1], 0.00001f);
        assert_float_within(afEdgeNormal[2], Surface.fNormal[2], 0.00001f);

        for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
            /* In the surface plane apart from the depth bias, which is
             * strictly toward the front. */
            assert(afEdgeQuad[i][2] > 100.0f);
            assert(afEdgeQuad[i][2] < 102.0f);
            for (uint32_t iAxis = 0; iAxis < 3u; iAxis++)
                afCentre[iAxis] += afEdgeQuad[i][iAxis] * 0.25f;
        }
        /* The ribbon straddles its edge: its centre is the edge midpoint. */
        assert_float_within(afCentre[0], (pfStart[0] + pfEnd[0]) * 0.5f,
                            0.01f);
        assert_float_within(afCentre[1], (pfStart[1] + pfEnd[1]) * 0.5f,
                            0.01f);
    }

    /* Width scales with the quad, not with the individual edge, so all four
     * ribbons on one surface are the same thickness. */
    {
        float afWidths[ED_SURFACE_VERTEX_COUNT];

        for (uint32_t uiEdge = 0; uiEdge < ED_SURFACE_VERTEX_COUNT;
             uiEdge++) {
            assert(ed_surface_wireframe_edge_quad(
                &Surface, uiEdge, afEdgeQuad));
            afWidths[uiEdge] = sqrtf(
                (afEdgeQuad[3][0] - afEdgeQuad[0][0])
                    * (afEdgeQuad[3][0] - afEdgeQuad[0][0])
                + (afEdgeQuad[3][1] - afEdgeQuad[0][1])
                    * (afEdgeQuad[3][1] - afEdgeQuad[0][1]));
        }
        for (uint32_t uiEdge = 1; uiEdge < ED_SURFACE_VERTEX_COUNT; uiEdge++)
            assert_float_within(afWidths[uiEdge], afWidths[0], 0.001f);
        /* 2 * 0.012 * 300, the longest edge. */
        assert(afWidths[0] > 7.1f && afWidths[0] < 7.3f);
    }

    /* Out-of-range edges and malformed emissions draw nothing rather than
     * NaN geometry. */
    assert(!ed_surface_wireframe_edge_quad(
        &Surface, ED_SURFACE_VERTEX_COUNT, afEdgeQuad));
    assert(!ed_surface_wireframe_edge_quad(&Surface, 0u, NULL));
    assert(!ed_surface_wireframe_edge_quad(NULL, 0u, afEdgeQuad));
    Surface.uiVertexCount = 3u;
    assert(!ed_surface_wireframe_edge_quad(&Surface, 0u, afEdgeQuad));
}

static void test_wireframe_refuses_degenerate_edges(void)
{
    tEdSurfaceEmission Surface;
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3];

    memset(&Surface, 0, sizeof(Surface));
    Surface.uiVertexCount = ED_SURFACE_VERTEX_COUNT;
    Surface.fNormal[2] = 1.0f;
    /* v0 and v1 coincide: a zero-length edge has no direction and no ribbon,
     * but its neighbours still do. */
    Surface.aVertices[1].fPosition[0] = 0.0f;
    Surface.aVertices[2].fPosition[0] = 100.0f;
    Surface.aVertices[3].fPosition[0] = 100.0f;
    Surface.aVertices[2].fPosition[1] = 50.0f;
    Surface.aVertices[3].fPosition[1] = 0.0f;

    assert(!ed_surface_wireframe_edge_quad(&Surface, 0u, afEdgeQuad));
    assert(ed_surface_wireframe_edge_quad(&Surface, 1u, afEdgeQuad));

    /* A fully collapsed quad has no longest edge at all. */
    memset(&Surface.aVertices, 0, sizeof(Surface.aVertices));
    Surface.uiVertexCount = ED_SURFACE_VERTEX_COUNT;
    for (uint32_t uiEdge = 0; uiEdge < ED_SURFACE_VERTEX_COUNT; uiEdge++)
        assert(!ed_surface_wireframe_edge_quad(&Surface, uiEdge, afEdgeQuad));
}

/*
 * E3A-S3. The editor selects a chunk range, not a class, so the range has to
 * cover every class in it. F-S4b's original single-class form still works for
 * a caller that wants one.
 */
static void test_chunk_range_selection_covers_every_class(void)
{
    tEdSurfaceSelection Selection = {
        .uiFirstChunkId = 40u,
        .uiLastChunkId = 12u, /* deliberately reversed */
        .unSurfaceClass = ED_SURFACE_SELECTION_ANY_CLASS,
        .byHighlightColour = 0xDAu,
        .bEnabled = true
    };
    tEdSurfaceEmission Surface;

    memset(&Surface, 0, sizeof(Surface));
    Surface.uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE | 3u;

    for (uint16_t unClass = 0; unClass < ROLLER_ED_SURFACE_CLASS_COUNT;
         unClass++) {
        Surface.unSurfaceClass = unClass;
        Surface.uiChunkId = 11u;
        assert(!ed_surface_selection_matches(&Selection, &Surface));
        Surface.uiChunkId = 12u;
        assert(ed_surface_selection_matches(&Selection, &Surface));
        Surface.uiChunkId = 26u;
        assert(ed_surface_selection_matches(&Selection, &Surface));
        Surface.uiChunkId = 40u;
        assert(ed_surface_selection_matches(&Selection, &Surface));
        Surface.uiChunkId = 41u;
        assert(!ed_surface_selection_matches(&Selection, &Surface));
    }

    /* The outline takes its flags from the same helper the flat highlight
     * used: texture bits cleared, the highlight colour in the low byte. */
    Surface.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_ROOF;
    Surface.uiChunkId = 20u;
    {
        uint32_t uiFlags =
            ed_surface_selection_render_flags(&Selection, &Surface);

        assert((uiFlags & SURFACE_FLAG_APPLY_TEXTURE) == 0);
        assert((uiFlags & SURFACE_MASK_TEXTURE_INDEX) == 0xDAu);
    }

    /* A single-class selection still excludes the others. */
    Selection.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_ROOF;
    assert(ed_surface_selection_matches(&Selection, &Surface));
    Surface.unSurfaceClass = ROLLER_ED_SURFACE_CLASS_CENTER;
    assert(!ed_surface_selection_matches(&Selection, &Surface));

    /* Disabled beats everything. */
    Selection.unSurfaceClass = ED_SURFACE_SELECTION_ANY_CLASS;
    Selection.bEnabled = false;
    assert(!ed_surface_selection_matches(&Selection, &Surface));
}

int main(void)
{
    test_exact_fixed_uvs_and_identity();
    test_low_resolution_fixed_uvs();
    test_reverse_material_and_generated_back_face();
    test_paired_mapping_in_both_directions();
    test_export_mapping_uses_material_transform();
    test_generic_identity_layout_and_skip();
    test_separate_texture_sets_keep_their_own_tile_identity();
    test_pair_falls_back_when_the_atlas_has_no_successor();
    test_back_material_matches_the_draw_time_substitution();
    test_non_textured_surfaces_carry_their_material_kind();
    test_invalid_identity_is_refused();
    test_normal_agrees_with_the_renderer_front_face_rule();
    test_twisted_quads_get_per_vertex_normals();
    test_pinched_corners_fall_back_to_the_quad_normal();
    test_degenerate_quads_publish_zero_normals();
    test_world_axes_and_scale_pass_through_unchanged();
    test_selection_uses_only_canonical_identity();
    test_chunk_range_selection_covers_every_class();
    test_full_track_chunk_traversal_is_complete_and_camera_free();
    test_full_scenery_traversal_is_complete_and_camera_free();
    test_unresolvable_texture_is_distinguishable_from_other_refusals();
    test_wireframe_edges_trace_the_surface_front_face();
    test_wireframe_refuses_degenerate_edges();
    puts("editor surface emission tests passed");
    return 0;
}
