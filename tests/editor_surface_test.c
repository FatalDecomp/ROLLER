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

static void capture_emission(const tEdSurfaceEmission *pSurface,
                             void *pUserData)
{
    tEmissionCapture *pCapture = pUserData;
    pCapture->Surface = *pSurface;
    pCapture->iCalls++;
}

static void assert_float_near(float fActual, float fExpected)
{
    assert(fabsf(fActual - fExpected) < 0.000001f);
}

static tEdLeftWallSurfaceInfo make_info(uint32_t uiFlags)
{
    tEdLeftWallSurfaceInfo Info = {
        .uiChunkId = 417u,
        .uiRenderFlags = uiFlags,
        .uiBackSurfaceFlags = ED_MATERIAL_ID_NONE,
        .uiTextureSet = 0u,
        .fSubdivideThreshold = 1234.5f,
        .bPairTextureEnabled =
            (uiFlags & SURFACE_FLAG_TEXTURE_PAIR) != 0,
        .bHighWall = true
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
    tEdTextureAtlas Atlas = { 256u, 128u, 64u, 8u };
    tEdLeftWallSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE
        | SURFACE_FLAG_TEXTURE_PAIR
        | SURFACE_FLAG_FLIP_BACKFACE
        | 2u);

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_left_wall_surface(
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
    tEdTextureAtlas Atlas = { 256u, 32u, 32u, 8u };
    tEdLeftWallSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR);

    assert(ed_material_table_init(&Table, aMaterials, 1u, Atlas));
    assert(ed_emit_left_wall_surface(
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
    tEdTextureAtlas Atlas = { 256u, 128u, 64u, 8u };
    tEdLeftWallSurfaceInfo Info = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK | 1u);
    Info.uiBackSurfaceFlags = 6u;
    Info.bPairTextureEnabled = false;

    assert(ed_material_table_init(&Table, aMaterials, 2u, Atlas));
    assert(ed_emit_left_wall_surface(
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
    tEdTextureAtlas Atlas = { 256u, 128u, 64u, 8u };
    tEdLeftWallSurfaceInfo ForwardInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR);
    tEdLeftWallSurfaceInfo ReverseInfo = make_info(
        SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR
        | SURFACE_FLAG_FLIP_HORIZ);

    assert(ed_material_table_init(
        &ForwardTable, aForwardMaterials, 1u, Atlas));
    assert(ed_material_table_init(
        &ReverseTable, aReverseMaterials, 1u, Atlas));
    assert(ed_emit_left_wall_surface(
        afWorld, &ForwardInfo, &ForwardTable, capture_emission, &Forward));
    assert(ed_emit_left_wall_surface(
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

int main(void)
{
    test_exact_fixed_uvs_and_identity();
    test_low_resolution_fixed_uvs();
    test_reverse_material_and_generated_back_face();
    test_paired_mapping_in_both_directions();
    test_export_mapping_uses_material_transform();
    puts("editor surface emission tests passed");
    return 0;
}
