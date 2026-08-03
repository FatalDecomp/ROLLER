#include "editor_surface.h"

#include "types.h"

#include <string.h>

static bool ed_material_equal(const tEdMaterial *pLeft,
                              const tEdMaterial *pRight)
{
    return memcmp(pLeft, pRight, sizeof(*pLeft)) == 0;
}

static bool ed_material_table_intern(tEdMaterialTable *pTable,
                                     const tEdMaterial *pMaterial,
                                     uint32_t *puiMaterialId)
{
    for (uint32_t i = 0; i < pTable->uiCount; i++) {
        if (ed_material_equal(&pTable->pMaterials[i], pMaterial)) {
            *puiMaterialId = i;
            return true;
        }
    }

    if (pTable->uiCount >= pTable->uiCapacity)
        return false;

    *puiMaterialId = pTable->uiCount;
    pTable->pMaterials[pTable->uiCount++] = *pMaterial;
    return true;
}

static bool ed_build_material(const tEdMaterialTable *pTable,
                              uint32_t uiSurfaceFlags,
                              uint32_t uiTextureSet,
                              bool bPairTexture,
                              tEdMaterial *pMaterial)
{
    uint32_t uiTileIndex = uiSurfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
    memset(pMaterial, 0, sizeof(*pMaterial));
    pMaterial->uiTextureSet = uiTextureSet;
    pMaterial->uiTileIndex = uiTileIndex;
    if (uiSurfaceFlags & (SURFACE_FLAG_TRANSPARENT | SURFACE_FLAG_PARTIAL_TRANS))
        pMaterial->uiFlags |= ROLLER_ED_MATERIAL_FLAG_ALPHA_BLEND;
    if (uiSurfaceFlags & SURFACE_FLAG_PARTIAL_TRANS)
        pMaterial->uiFlags |= ROLLER_ED_MATERIAL_FLAG_PARTIAL_ALPHA;

    if (uiSurfaceFlags & SURFACE_FLAG_APPLY_TEXTURE) {
        uint32_t uiTilesPerRow = pTable->Atlas.uiWidth
                               / pTable->Atlas.uiTileSize;
        if (uiTilesPerRow == 0 || uiTileIndex >= pTable->Atlas.uiTileCount)
            return false;

        pMaterial->uiKind = bPairTexture
            ? ROLLER_ED_MATERIAL_TEXTURED_PAIR
            : ROLLER_ED_MATERIAL_TEXTURED_TILE;
        pMaterial->fAtlasScale[0] =
            (float)(pTable->Atlas.uiTileSize * (bPairTexture ? 2u : 1u))
            / (float)pTable->Atlas.uiWidth;
        pMaterial->fAtlasScale[1] =
            (float)pTable->Atlas.uiTileSize / (float)pTable->Atlas.uiHeight;
        pMaterial->fAtlasBias[0] =
            (float)((uiTileIndex % uiTilesPerRow) * pTable->Atlas.uiTileSize)
            / (float)pTable->Atlas.uiWidth;
        pMaterial->fAtlasBias[1] =
            (float)((uiTileIndex / uiTilesPerRow) * pTable->Atlas.uiTileSize)
            / (float)pTable->Atlas.uiHeight;
    } else if (uiSurfaceFlags & SURFACE_FLAG_TRANSPARENT) {
        pMaterial->uiKind = ROLLER_ED_MATERIAL_SCREEN_DARKEN;
        pMaterial->uiDarkenLevel = uiTileIndex;
    } else {
        pMaterial->uiKind = ROLLER_ED_MATERIAL_FLAT_PALETTE_COLOR;
        pMaterial->uiPaletteColour = uiTileIndex;
    }
    return true;
}

bool ed_material_table_init(tEdMaterialTable *pTable,
                            tEdMaterial *pStorage,
                            uint32_t uiCapacity,
                            tEdTextureAtlas Atlas)
{
    if (!pTable || !pStorage || uiCapacity == 0
            || Atlas.uiWidth == 0 || Atlas.uiHeight == 0
            || Atlas.uiTileSize == 0
            || Atlas.uiWidth % Atlas.uiTileSize != 0
            || Atlas.uiHeight % Atlas.uiTileSize != 0)
        return false;

    pTable->pMaterials = pStorage;
    pTable->uiCapacity = uiCapacity;
    pTable->uiCount = 0;
    pTable->Atlas = Atlas;
    return true;
}

const tEdMaterial *ed_material_table_get(const tEdMaterialTable *pTable,
                                         uint32_t uiMaterialId)
{
    if (!pTable || uiMaterialId >= pTable->uiCount)
        return NULL;
    return &pTable->pMaterials[uiMaterialId];
}

void ed_material_resolve_uv(const tEdMaterial *pMaterial,
                            const float afMaterialUV[2],
                            float afAtlasUV[2])
{
    if (!pMaterial || !afMaterialUV || !afAtlasUV)
        return;
    afAtlasUV[0] = afMaterialUV[0] * pMaterial->fAtlasScale[0]
                 + pMaterial->fAtlasBias[0];
    afAtlasUV[1] = afMaterialUV[1] * pMaterial->fAtlasScale[1]
                 + pMaterial->fAtlasBias[1];
}

bool ed_surface_selection_matches(const tEdSurfaceSelection *pSelection,
                                  const tEdSurfaceEmission *pSurface)
{
    uint32_t uiFirstChunkId;
    uint32_t uiLastChunkId;

    if (!pSelection || !pSurface || !pSelection->bEnabled
            || pSurface->unSurfaceClass != pSelection->unSurfaceClass)
        return false;

    uiFirstChunkId = pSelection->uiFirstChunkId;
    uiLastChunkId = pSelection->uiLastChunkId;
    if (uiFirstChunkId > uiLastChunkId) {
        uint32_t uiTemp = uiFirstChunkId;
        uiFirstChunkId = uiLastChunkId;
        uiLastChunkId = uiTemp;
    }
    return pSurface->uiChunkId >= uiFirstChunkId
        && pSurface->uiChunkId <= uiLastChunkId;
}

uint32_t ed_surface_selection_render_flags(
    const tEdSurfaceSelection *pSelection,
    const tEdSurfaceEmission *pSurface)
{
    const uint32_t uiMaterialFlags =
        SURFACE_FLAG_APPLY_TEXTURE
        | SURFACE_FLAG_TRANSPARENT
        | SURFACE_FLAG_PARTIAL_TRANS;

    if (!ed_surface_selection_matches(pSelection, pSurface))
        return pSurface ? pSurface->uiRenderFlags : 0u;
    return (pSurface->uiRenderFlags
            & ~(uiMaterialFlags | SURFACE_MASK_TEXTURE_INDEX))
        | pSelection->byHighlightColour;
}

bool ed_surface_compute_render_uvs(
    uint8_t byRenderUVLayout,
    bool bHalfResolution,
    int32_t aiRenderU16_16[ED_SURFACE_VERTEX_COUNT],
    int32_t aiRenderV16_16[ED_SURFACE_VERTEX_COUNT])
{
    static const int32_t aiUCorner[ED_SURFACE_VERTEX_COUNT] = { 1, 0, 0, 1 };
    static const int32_t aiVCorner[ED_SURFACE_VERTEX_COUNT] = { 0, 0, 1, 1 };
    uint32_t uiTileSize;
    uint32_t uiRenderWidth;
    uint32_t uiRenderHeight;
    int32_t iMaxU;
    int32_t iMaxV;

    if (!aiRenderU16_16 || !aiRenderV16_16
            || byRenderUVLayout > ROLLER_ED_RENDER_UV_PAIR_VERTICAL)
        return false;

    uiTileSize = bHalfResolution ? 32u : 64u;
    uiRenderWidth = uiTileSize
                  * (byRenderUVLayout == ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL
                     ? 2u : 1u);
    uiRenderHeight = uiTileSize
                   * (byRenderUVLayout == ROLLER_ED_RENDER_UV_PAIR_VERTICAL
                      ? 2u : 1u);
    iMaxU = (int32_t)(uiRenderWidth << 16) - 0x1000;
    iMaxV = (int32_t)(uiRenderHeight << 16) - 0x1000;

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        aiRenderU16_16[i] = aiUCorner[i] ? iMaxU : 0;
        aiRenderV16_16[i] = aiVCorner[i] ? iMaxV : 0;
    }
    return true;
}

bool ed_traverse_full_track_chunks(uint32_t uiLoadedChunkCount,
                                   tEdVisitChunkFn pfnVisit,
                                   void *pUserData)
{
    if (!pfnVisit)
        return false;

    for (uint32_t uiChunkId = 0; uiChunkId < uiLoadedChunkCount; uiChunkId++) {
        if (!pfnVisit(uiChunkId, pUserData))
            return false;
    }
    return true;
}

bool ed_emit_surface(const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
                     const tEdSurfaceInfo *pInfo,
                     tEdMaterialTable *pMaterials,
                     tEdEmitSurfaceFn pfnEmit,
                     void *pUserData)
{
    tEdSurfaceEmission Surface;
    tEdMaterial FrontMaterial;
    uint32_t uiTileSize;
    uint32_t uiRenderWidth;
    uint32_t uiRenderHeight;
    int32_t aiRenderU16_16[ED_SURFACE_VERTEX_COUNT];
    int32_t aiRenderV16_16[ED_SURFACE_VERTEX_COUNT];

    if (!afWorldVertices || !pInfo || !pMaterials || !pfnEmit)
        return false;
    if (pInfo->uiRenderFlags & SURFACE_FLAG_SKIP_RENDER)
        return true;

    uiTileSize = pMaterials->Atlas.uiTileSize;
    uiRenderWidth = uiTileSize
                  * (pInfo->byRenderUVLayout
                     == ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL ? 2u : 1u);
    uiRenderHeight = uiTileSize
                   * (pInfo->byRenderUVLayout
                      == ROLLER_ED_RENDER_UV_PAIR_VERTICAL ? 2u : 1u);
    if (uiRenderWidth > (uint32_t)(INT32_MAX >> 16)
            || uiRenderHeight > (uint32_t)(INT32_MAX >> 16)
            || !ed_surface_compute_render_uvs(
                pInfo->byRenderUVLayout, uiTileSize == 32u,
                aiRenderU16_16, aiRenderV16_16))
        return false;

    memset(&Surface, 0, sizeof(Surface));
    Surface.uiVertexCount = ED_SURFACE_VERTEX_COUNT;
    Surface.uiBackMaterialId = ED_MATERIAL_ID_NONE;
    Surface.uiChunkId = pInfo->uiChunkId;
    Surface.uiRenderFlags = pInfo->uiRenderFlags;
    Surface.iRenderSubdivideType = pInfo->iRenderSubdivideType;
    Surface.fSubdivideThreshold = pInfo->fSubdivideThreshold;
    Surface.unSurfaceClass = pInfo->unSurfaceClass;
    Surface.unContentClass = pInfo->unContentClass;
    Surface.unFlags = pInfo->unFlags;
    Surface.byTopology = pInfo->byTopology;

    if (pInfo->uiRenderFlags
            & (SURFACE_FLAG_TRANSPARENT | SURFACE_FLAG_PARTIAL_TRANS))
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_ALPHA;
    if (pInfo->uiRenderFlags & SURFACE_FLAG_FLIP_BACKFACE)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_TWO_SIDED;
    if (pInfo->uiRenderFlags & SURFACE_FLAG_APPLY_TEXTURE)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_TEXTURED;
    if (pInfo->bPairTextureEnabled)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE;
    if (pInfo->bHighVariant)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_HIGH_VARIANT;

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        memcpy(Surface.aVertices[i].fPosition, afWorldVertices[i],
               sizeof(Surface.aVertices[i].fPosition));
        Surface.aVertices[i].iRenderU16_16 = aiRenderU16_16[i];
        Surface.aVertices[i].iRenderV16_16 = aiRenderV16_16[i];
        Surface.aVertices[i].fMaterialUV[0] =
            (float)Surface.aVertices[i].iRenderU16_16
            / (float)(uiRenderWidth << 16);
        Surface.aVertices[i].fMaterialUV[1] =
            (float)Surface.aVertices[i].iRenderV16_16
            / (float)(uiRenderHeight << 16);
    }

    if (pInfo->uiRenderFlags & SURFACE_FLAG_FLIP_HORIZ) {
        float fTemp = Surface.aVertices[0].fMaterialUV[0];
        Surface.aVertices[0].fMaterialUV[0] =
            Surface.aVertices[1].fMaterialUV[0];
        Surface.aVertices[1].fMaterialUV[0] = fTemp;
        fTemp = Surface.aVertices[3].fMaterialUV[0];
        Surface.aVertices[3].fMaterialUV[0] =
            Surface.aVertices[2].fMaterialUV[0];
        Surface.aVertices[2].fMaterialUV[0] = fTemp;
    }
    if (pInfo->uiRenderFlags & SURFACE_FLAG_FLIP_VERT) {
        float fTemp = Surface.aVertices[0].fMaterialUV[1];
        Surface.aVertices[0].fMaterialUV[1] =
            Surface.aVertices[2].fMaterialUV[1];
        Surface.aVertices[2].fMaterialUV[1] = fTemp;
        fTemp = Surface.aVertices[1].fMaterialUV[1];
        Surface.aVertices[1].fMaterialUV[1] =
            Surface.aVertices[3].fMaterialUV[1];
        Surface.aVertices[3].fMaterialUV[1] = fTemp;
    }

    if (!ed_build_material(pMaterials, pInfo->uiRenderFlags,
                           pInfo->uiTextureSet,
                           pInfo->bPairTextureEnabled, &FrontMaterial)
            || !ed_material_table_intern(pMaterials, &FrontMaterial,
                                         &Surface.uiFrontMaterialId))
        return false;

    if ((pInfo->uiRenderFlags & (SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK))
            == (SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK)
            && pInfo->uiBackSurfaceFlags != ED_MATERIAL_ID_NONE) {
        uint32_t uiBackTile =
            pInfo->uiBackSurfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
        uint32_t uiFrontTile =
            pInfo->uiRenderFlags & SURFACE_MASK_TEXTURE_INDEX;
        if (uiBackTile != uiFrontTile
                && uiBackTile < pMaterials->Atlas.uiTileCount) {
            tEdMaterial BackMaterial;
            uint32_t uiBackFlags =
                (pInfo->uiRenderFlags & SURFACE_MASK_FLAGS) | uiBackTile;
            if (!ed_build_material(pMaterials, uiBackFlags,
                                   pInfo->uiTextureSet,
                                   pInfo->bPairTextureEnabled, &BackMaterial)
                    || !ed_material_table_intern(pMaterials, &BackMaterial,
                                                 &Surface.uiBackMaterialId))
                return false;
        }
    }

    pfnEmit(&Surface, pUserData);
    return true;
}
