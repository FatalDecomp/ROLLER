#include "editor_surface.h"

#include "types.h"

#include <math.h>
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

/* The renderer builds a pair texture for every tile that has a successor
 * (scene_render_gpu.c creates pairTextures[n] for n + 1 < numTiles) and
 * silently falls back to the plain tile when that pair is missing. */
bool ed_atlas_pair_available(const tEdTextureAtlas *pAtlas,
                             uint32_t uiTileIndex)
{
    return pAtlas && uiTileIndex + 1u < pAtlas->uiTileCount;
}

/* A pair whose left tile sits in the last atlas column takes its right half
 * from the following row, so no single scale/bias rectangle describes it. */
bool ed_atlas_pair_wraps_row(const tEdTextureAtlas *pAtlas,
                             uint32_t uiTileIndex)
{
    uint32_t uiTilesPerRow;

    if (!pAtlas || pAtlas->uiTileSize == 0)
        return false;
    uiTilesPerRow = pAtlas->uiWidth / pAtlas->uiTileSize;
    if (uiTilesPerRow == 0)
        return false;
    return (uiTileIndex % uiTilesPerRow) + 1u >= uiTilesPerRow;
}

static bool ed_build_material(const tEdTextureAtlas *pAtlas,
                              uint32_t uiSurfaceFlags,
                              uint32_t uiTextureSet,
                              bool bPairTexture,
                              tEdMaterial *pMaterial)
{
    uint32_t uiTileIndex = uiSurfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
    memset(pMaterial, 0, sizeof(*pMaterial));
    pMaterial->uiTextureSet = uiTextureSet;
    if (uiSurfaceFlags & (SURFACE_FLAG_TRANSPARENT | SURFACE_FLAG_PARTIAL_TRANS))
        pMaterial->uiFlags |= ROLLER_ED_MATERIAL_FLAG_ALPHA_BLEND;
    if (uiSurfaceFlags & SURFACE_FLAG_PARTIAL_TRANS)
        pMaterial->uiFlags |= ROLLER_ED_MATERIAL_FLAG_PARTIAL_ALPHA;

    if (uiSurfaceFlags & SURFACE_FLAG_APPLY_TEXTURE) {
        uint32_t uiTilesPerRow;

        if (!pAtlas || pAtlas->uiTileSize == 0)
            return false;
        uiTilesPerRow = pAtlas->uiWidth / pAtlas->uiTileSize;
        if (uiTilesPerRow == 0 || uiTileIndex >= pAtlas->uiTileCount)
            return false;
        /* A caller must not ask for a pair the renderer would not build. */
        if (bPairTexture && !ed_atlas_pair_available(pAtlas, uiTileIndex))
            return false;

        pMaterial->uiTileIndex = uiTileIndex;
        pMaterial->uiKind = bPairTexture
            ? ROLLER_ED_MATERIAL_TEXTURED_PAIR
            : ROLLER_ED_MATERIAL_TEXTURED_TILE;
        if (bPairTexture && ed_atlas_pair_wraps_row(pAtlas, uiTileIndex))
            pMaterial->uiFlags |=
                ROLLER_ED_MATERIAL_FLAG_PAIR_WRAPS_ATLAS_ROW;
        pMaterial->fAtlasScale[0] =
            (float)(pAtlas->uiTileSize
                    * (bPairTexture ? ROLLER_ED_PAIR_TEXTURE_TILE_SPAN : 1u))
            / (float)pAtlas->uiWidth;
        pMaterial->fAtlasScale[1] =
            (float)pAtlas->uiTileSize / (float)pAtlas->uiHeight;
        pMaterial->fAtlasBias[0] =
            (float)((uiTileIndex % uiTilesPerRow) * pAtlas->uiTileSize)
            / (float)pAtlas->uiWidth;
        pMaterial->fAtlasBias[1] =
            (float)((uiTileIndex / uiTilesPerRow) * pAtlas->uiTileSize)
            / (float)pAtlas->uiHeight;
    } else if (uiSurfaceFlags & SURFACE_FLAG_TRANSPARENT) {
        /* No tile identity: the renderer darkens whatever is already in the
         * framebuffer by the level the surface index selects. */
        pMaterial->uiKind = ROLLER_ED_MATERIAL_SCREEN_DARKEN;
        pMaterial->uiDarkenLevel = uiTileIndex;
    } else {
        pMaterial->uiKind = ROLLER_ED_MATERIAL_FLAT_PALETTE_COLOR;
        pMaterial->uiPaletteColour = uiTileIndex;
    }
    return true;
}

static bool ed_atlas_valid(const tEdTextureAtlas *pAtlas)
{
    return pAtlas->uiWidth != 0 && pAtlas->uiHeight != 0
        && pAtlas->uiTileSize != 0
        && pAtlas->uiWidth % pAtlas->uiTileSize == 0
        && pAtlas->uiHeight % pAtlas->uiTileSize == 0;
}

bool ed_material_table_init(tEdMaterialTable *pTable,
                            tEdMaterial *pStorage,
                            uint32_t uiCapacity,
                            tEdTextureAtlas Atlas)
{
    if (!pTable || !pStorage || uiCapacity == 0 || !ed_atlas_valid(&Atlas))
        return false;

    pTable->pMaterials = pStorage;
    pTable->uiCapacity = uiCapacity;
    pTable->uiCount = 0;
    pTable->uiAtlasCount = 0;
    pTable->uiTileSize = Atlas.uiTileSize;
    return ed_material_table_set_atlas(pTable, Atlas);
}

bool ed_material_table_set_atlas(tEdMaterialTable *pTable,
                                 tEdTextureAtlas Atlas)
{
    if (!pTable || !ed_atlas_valid(&Atlas)
            || Atlas.uiTileSize != pTable->uiTileSize)
        return false;

    for (uint32_t i = 0; i < pTable->uiAtlasCount; i++) {
        if (pTable->aAtlases[i].uiTextureSet == Atlas.uiTextureSet) {
            pTable->aAtlases[i] = Atlas;
            return true;
        }
    }

    if (pTable->uiAtlasCount >= ED_MATERIAL_MAX_TEXTURE_SETS)
        return false;
    pTable->aAtlases[pTable->uiAtlasCount++] = Atlas;
    return true;
}

const tEdTextureAtlas *ed_material_table_atlas(const tEdMaterialTable *pTable,
                                               uint32_t uiTextureSet)
{
    if (!pTable)
        return NULL;
    for (uint32_t i = 0; i < pTable->uiAtlasCount; i++) {
        if (pTable->aAtlases[i].uiTextureSet == uiTextureSet)
            return &pTable->aAtlases[i];
    }
    return NULL;
}

bool ed_surface_identity_valid(const tEdSurfaceInfo *pInfo)
{
    return pInfo
        && (pInfo->uiChunkId == ROLLER_ED_INVALID_CHUNK_ID
            || pInfo->uiChunkId < (uint32_t)MAX_TRACK_CHUNKS)
        && pInfo->unSurfaceClass <= ROLLER_ED_SURFACE_CLASS_TOWER
        && pInfo->unContentClass <= ROLLER_ED_CONTENT_RUNTIME_SCENERY
        && pInfo->byTopology == ROLLER_ED_TOPOLOGY_QUAD
        && pInfo->byRenderUVLayout <= ROLLER_ED_RENDER_UV_PAIR_VERTICAL;
}

/*
 * Whether the table can describe this surface's texture at all. A textured
 * surface whose tile is not in its bank is refused by ed_emit_surface, and the
 * render path already lets that refusal drop the quad -- DrawBuilding ignores
 * the return of drawtrk3_emit_surface_to_renderer, so the surface is simply
 * not on screen. Retail TRACK5's advert list names tile 45 of a 45-tile
 * building bank, so this is real data, not a hypothetical.
 *
 * A producer uses this to skip such a surface deliberately instead of aborting
 * a whole traversal, while leaving every other ed_emit_surface refusal -- a
 * full material table, invalid identity -- fatal, which is what E4A-S5's
 * probe/fill agreement depends on.
 */
bool ed_surface_material_resolvable(const tEdMaterialTable *pTable,
                                    const tEdSurfaceInfo *pInfo)
{
    const tEdTextureAtlas *pAtlas;

    if (!pTable || !pInfo)
        return false;
    if (!(pInfo->uiRenderFlags & SURFACE_FLAG_APPLY_TEXTURE))
        return true;
    pAtlas = ed_material_table_atlas(pTable, pInfo->uiTextureSet);
    return pAtlas
        && (pInfo->uiRenderFlags & SURFACE_MASK_TEXTURE_INDEX)
            < pAtlas->uiTileCount;
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

    if (!pSelection || !pSurface || !pSelection->bEnabled)
        return false;
    if (pSelection->unSurfaceClass != ED_SURFACE_SELECTION_ANY_CLASS
            && pSurface->unSurfaceClass != pSelection->unSurfaceClass)
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

static void ed_cross(const float afLeft[3],
                     const float afRight[3],
                     float afOut[3])
{
    afOut[0] = afLeft[1] * afRight[2] - afLeft[2] * afRight[1];
    afOut[1] = afLeft[2] * afRight[0] - afLeft[0] * afRight[2];
    afOut[2] = afLeft[0] * afRight[1] - afLeft[1] * afRight[0];
}

/*
 * Both magnitudes below are twice an area: a corner's cross product measures
 * its own triangle, Newell's measures the whole quad. On a healthy quad the
 * ratio sits near one half. A pinched corner -- TRACK3 chunk 124's right
 * shoulder collapses to a sub-unit edge at coordinates near 90000 -- drops to
 * roughly 0.0015, and the sliver triangle it does describe is near
 * perpendicular to the quad, so it is worthless as a shading normal. This
 * threshold sits an order of magnitude either side of both cases.
 */
#define ED_NORMAL_DEGENERATE_RATIO (1.0 / 64.0)

static double ed_length3(const float afVector[3])
{
    return sqrt((double)afVector[0] * afVector[0]
              + (double)afVector[1] * afVector[1]
              + (double)afVector[2] * afVector[2]);
}

/* Returns false and zeroes the vector when the input is degenerate, so a
 * collapsed quad publishes an obviously absent normal rather than a NaN. */
static bool ed_normalize(float afVector[3])
{
    double dLength = ed_length3(afVector);

    if (!(dLength > 0.0)) {
        afVector[0] = 0.0f;
        afVector[1] = 0.0f;
        afVector[2] = 0.0f;
        return false;
    }
    afVector[0] = (float)((double)afVector[0] / dLength);
    afVector[1] = (float)((double)afVector[1] / dLength);
    afVector[2] = (float)((double)afVector[2] / dLength);
    return true;
}

/*
 * The surface normal uses Newell's method over all four corners, so a twisted
 * quad is not reduced to a normal taken from three of them. Each vertex also
 * gets its own adjacent-edge normal, which equals the surface normal on a flat
 * quad and follows the local slope on a corkscrew. Both follow the right-hand
 * rule for the producer's v0..v3 order, which is the renderer's front face.
 */
bool ed_surface_compute_normals(
    const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
    float afSurfaceNormal[3],
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3])
{
    float afNewell[3] = { 0.0f, 0.0f, 0.0f };
    double dNewellLength;
    bool bSurfaceValid;

    if (!afWorldVertices || !afSurfaceNormal)
        return false;

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        const float *pfCurrent = afWorldVertices[i];
        const float *pfNext =
            afWorldVertices[(i + 1u) % ED_SURFACE_VERTEX_COUNT];
        afNewell[0] += (pfCurrent[1] - pfNext[1]) * (pfCurrent[2] + pfNext[2]);
        afNewell[1] += (pfCurrent[2] - pfNext[2]) * (pfCurrent[0] + pfNext[0]);
        afNewell[2] += (pfCurrent[0] - pfNext[0]) * (pfCurrent[1] + pfNext[1]);
    }
    dNewellLength = ed_length3(afNewell);
    memcpy(afSurfaceNormal, afNewell, sizeof(afNewell));
    bSurfaceValid = ed_normalize(afSurfaceNormal);

    if (afVertexNormals) {
        for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
            const float *pfCurrent = afWorldVertices[i];
            const float *pfNext =
                afWorldVertices[(i + 1u) % ED_SURFACE_VERTEX_COUNT];
            const float *pfPrev =
                afWorldVertices[(i + ED_SURFACE_VERTEX_COUNT - 1u)
                                % ED_SURFACE_VERTEX_COUNT];
            float afToNext[3];
            float afToPrev[3];

            for (uint32_t iAxis = 0; iAxis < 3; iAxis++) {
                afToNext[iAxis] = pfNext[iAxis] - pfCurrent[iAxis];
                afToPrev[iAxis] = pfPrev[iAxis] - pfCurrent[iAxis];
            }
            ed_cross(afToNext, afToPrev, afVertexNormals[i]);
            /* A collinear, duplicated, or pinched corner has no local normal
             * of its own; the whole-quad normal is the only meaningful
             * answer. Judged against the quad's own size, because absolute
             * magnitudes here scale with the track's world coordinates. */
            if (ed_length3(afVertexNormals[i])
                    <= dNewellLength * ED_NORMAL_DEGENERATE_RATIO
                    || !ed_normalize(afVertexNormals[i]))
                memcpy(afVertexNormals[i], afSurfaceNormal,
                       sizeof(afVertexNormals[i]));
        }
    }
    return bSurfaceValid;
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

/*
 * E3A-S2 wireframe. The renderer has no line primitive -- world-space quads
 * are the only geometry it accepts (game_render.h) -- so an edge is drawn as a
 * thin ribbon lying in the surface's own plane, nudged toward the front face
 * so it wins the depth test against the surface it outlines.
 *
 * The ribbon width is a fraction of the quad's longest edge rather than an
 * absolute distance, because legacy track units span four orders of magnitude
 * between a sign panel and an outer wall; one constant width would be
 * invisible on one and a slab on the other. It is therefore thicker in world
 * space on a big quad, which is what keeps it a roughly constant thickness on
 * screen for the surface it belongs to.
 */
#define ED_WIREFRAME_WIDTH_RATIO 0.012f
/* Enough to clear coplanar depth without visibly floating off the surface. */
#define ED_WIREFRAME_DEPTH_BIAS_RATIO 0.5f

static void ed_subtract3(const float afLeft[3],
                         const float afRight[3],
                         float afOut[3])
{
    afOut[0] = afLeft[0] - afRight[0];
    afOut[1] = afLeft[1] - afRight[1];
    afOut[2] = afLeft[2] - afRight[2];
}

bool ed_surface_wireframe_edge_quad_points(
    const float (*afPoints)[3],
    uint32_t uiPointCount,
    const float afNormal[3],
    uint32_t uiEdge,
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3])
{
    const float *pfStart;
    const float *pfEnd;
    float afDirection[3];
    float afSideways[3];
    float afWidth[3];
    float afBias[3];
    float fWidth;
    double dLongestEdge = 0.0;

    if (!afPoints || !afNormal || !afEdgeQuad || uiPointCount < 3u
            || uiEdge >= uiPointCount)
        return false;

    for (uint32_t i = 0; i < uiPointCount; i++) {
        float afEdge[3];
        double dLength;

        ed_subtract3(afPoints[(i + 1u) % uiPointCount], afPoints[i], afEdge);
        dLength = ed_length3(afEdge);
        if (dLength > dLongestEdge)
            dLongestEdge = dLength;
    }
    if (!(dLongestEdge > 0.0))
        return false;

    pfStart = afPoints[uiEdge];
    pfEnd = afPoints[(uiEdge + 1u) % uiPointCount];
    ed_subtract3(pfEnd, pfStart, afDirection);
    if (!ed_normalize(afDirection))
        return false;

    /* In-plane perpendicular: normal x direction. A pinched corner can leave
     * an edge parallel to the quad normal, which has no ribbon to draw. */
    ed_cross(afNormal, afDirection, afSideways);
    if (!ed_normalize(afSideways))
        return false;

    fWidth = (float)(dLongestEdge * ED_WIREFRAME_WIDTH_RATIO);
    for (uint32_t i = 0; i < 3u; i++) {
        afWidth[i] = afSideways[i] * fWidth;
        afBias[i] = afNormal[i] * fWidth * ED_WIREFRAME_DEPTH_BIAS_RATIO;
    }

    /*
     * v0..v3 wound so the right-hand-rule normal matches the surface normal --
     * the same front face E4A-S4 recorded, so the renderer's facing test keeps
     * the ribbon visible from exactly the side the surface is visible from.
     */
    for (uint32_t i = 0; i < 3u; i++) {
        afEdgeQuad[0][i] = pfStart[i] - afWidth[i] + afBias[i];
        afEdgeQuad[1][i] = pfEnd[i] - afWidth[i] + afBias[i];
        afEdgeQuad[2][i] = pfEnd[i] + afWidth[i] + afBias[i];
        afEdgeQuad[3][i] = pfStart[i] + afWidth[i] + afBias[i];
    }
    return true;
}

bool ed_surface_wireframe_edge_quad(
    const tEdSurfaceEmission *pSurface,
    uint32_t uiEdge,
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3])
{
    float afPoints[ED_SURFACE_VERTEX_COUNT][3];

    if (!pSurface || pSurface->uiVertexCount != ED_SURFACE_VERTEX_COUNT)
        return false;
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        for (uint32_t j = 0; j < 3u; j++)
            afPoints[i][j] = pSurface->aVertices[i].fPosition[j];
    }
    return ed_surface_wireframe_edge_quad_points(
        (const float (*)[3])afPoints, ED_SURFACE_VERTEX_COUNT,
        pSurface->fNormal, uiEdge, afEdgeQuad);
}

/* The canonical traversals are both "visit 0..N-1 in index order, stop on the
 * first refusal". Keeping the walk in one place is what makes the order a
 * property of the emitter rather than of each producer. */
static bool ed_traverse_indices(uint32_t uiCount,
                                tEdVisitIndexFn pfnVisit,
                                void *pUserData)
{
    if (!pfnVisit)
        return false;

    for (uint32_t uiIndex = 0; uiIndex < uiCount; uiIndex++) {
        if (!pfnVisit(uiIndex, pUserData))
            return false;
    }
    return true;
}

bool ed_traverse_full_track_chunks(uint32_t uiLoadedChunkCount,
                                   tEdVisitChunkFn pfnVisit,
                                   void *pUserData)
{
    return ed_traverse_indices(uiLoadedChunkCount, pfnVisit, pUserData);
}

bool ed_traverse_full_scenery_objects(uint32_t uiPlacedObjectCount,
                                      tEdVisitIndexFn pfnVisit,
                                      void *pUserData)
{
    return ed_traverse_indices(uiPlacedObjectCount, pfnVisit, pUserData);
}

bool ed_emit_surface(const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
                     const tEdSurfaceInfo *pInfo,
                     tEdMaterialTable *pMaterials,
                     tEdEmitSurfaceFn pfnEmit,
                     void *pUserData)
{
    tEdSurfaceEmission Surface;
    tEdMaterial FrontMaterial;
    const tEdTextureAtlas *pAtlas;
    uint32_t uiTileIndex;
    uint32_t uiTileSize;
    uint32_t uiRenderWidth;
    uint32_t uiRenderHeight;
    uint8_t byRenderUVLayout;
    bool bTextured;
    bool bPairTexture;
    int32_t aiRenderU16_16[ED_SURFACE_VERTEX_COUNT];
    int32_t aiRenderV16_16[ED_SURFACE_VERTEX_COUNT];
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3];

    if (!afWorldVertices || !pInfo || !pMaterials || !pfnEmit
            || !ed_surface_identity_valid(pInfo))
        return false;
    if (pInfo->uiRenderFlags & SURFACE_FLAG_SKIP_RENDER)
        return true;

    /* Tile identity is resolved against the surface's own texture set, so a
     * stream that mixes main-track and building/sign surfaces still gets the
     * right tile count and atlas transform for each. */
    pAtlas = ed_material_table_atlas(pMaterials, pInfo->uiTextureSet);
    uiTileIndex = pInfo->uiRenderFlags & SURFACE_MASK_TEXTURE_INDEX;
    bTextured = (pInfo->uiRenderFlags & SURFACE_FLAG_APPLY_TEXTURE) != 0;
    if (bTextured && (!pAtlas || uiTileIndex >= pAtlas->uiTileCount))
        return false;

    /* Match the draw-time fallback: a requested pair that the atlas cannot
     * supply degrades to the plain tile for both the material and the render
     * UV span, so the two never disagree. */
    bPairTexture = pInfo->bPairTextureEnabled && bTextured
                && ed_atlas_pair_available(pAtlas, uiTileIndex);
    byRenderUVLayout = pInfo->byRenderUVLayout;
    if (byRenderUVLayout == ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL
            && !bPairTexture)
        byRenderUVLayout = ROLLER_ED_RENDER_UV_TILE;

    uiTileSize = pMaterials->uiTileSize;
    uiRenderWidth = uiTileSize
                  * (byRenderUVLayout
                     == ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL ? 2u : 1u);
    uiRenderHeight = uiTileSize
                   * (byRenderUVLayout
                      == ROLLER_ED_RENDER_UV_PAIR_VERTICAL ? 2u : 1u);
    if (uiRenderWidth > (uint32_t)(INT32_MAX >> 16)
            || uiRenderHeight > (uint32_t)(INT32_MAX >> 16)
            || !ed_surface_compute_render_uvs(
                byRenderUVLayout, uiTileSize == 32u,
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
    /* Both legacy flags mean the renderer will not cull this surface by
     * facing: FLIP_BACKFACE draws the reverse side, and CONCAVE bypasses the
     * facing test outright (drawtrk3.c:3001 and its three siblings). The
     * outer-wall sections that carry CONCAVE are not consistently wound in
     * the source data, which is exactly why the renderer stopped trusting
     * their winding -- so an exporter must treat them as two-sided rather
     * than reading a front face off the normal. */
    if (pInfo->uiRenderFlags
            & (SURFACE_FLAG_FLIP_BACKFACE | SURFACE_FLAG_CONCAVE))
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_TWO_SIDED;
    if (pInfo->uiRenderFlags & SURFACE_FLAG_APPLY_TEXTURE)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_TEXTURED;
    if (bPairTexture)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE;
    if (pInfo->bHighVariant)
        Surface.unFlags |= ROLLER_ED_SURFACE_FLAG_HIGH_VARIANT;

    /* Positions pass through unscaled and in the producer's winding, so the
     * generated normals describe the same front face the renderer draws. */
    ed_surface_compute_normals(
        afWorldVertices, Surface.fNormal, afVertexNormals);

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        memcpy(Surface.aVertices[i].fPosition, afWorldVertices[i],
               sizeof(Surface.aVertices[i].fPosition));
        memcpy(Surface.aVertices[i].fNormal, afVertexNormals[i],
               sizeof(Surface.aVertices[i].fNormal));
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

    if (!ed_build_material(pAtlas, pInfo->uiRenderFlags,
                           pInfo->uiTextureSet,
                           bPairTexture, &FrontMaterial)
            || !ed_material_table_intern(pMaterials, &FrontMaterial,
                                         &Surface.uiFrontMaterialId))
        return false;

    /* The draw-time rule (polytex.c:578, scene_render_gpu.c:5521) substitutes
     * texture_back[]'s tile index on a back-facing textured surface, keeping
     * the surface's other flags and ignoring an out-of-range substitute. An
     * identical or absent substitute leaves uiBackMaterialId as the
     * "no alternate reverse material" sentinel. */
    if (bTextured && (pInfo->uiRenderFlags & SURFACE_FLAG_BACK)
            && pInfo->uiBackSurfaceFlags != ED_MATERIAL_ID_NONE) {
        uint32_t uiBackTile =
            pInfo->uiBackSurfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
        if (uiBackTile != uiTileIndex && uiBackTile < pAtlas->uiTileCount) {
            tEdMaterial BackMaterial;
            uint32_t uiBackFlags =
                (pInfo->uiRenderFlags & SURFACE_MASK_FLAGS) | uiBackTile;
            /* Pair availability is a property of the substituted tile. */
            bool bBackPair = pInfo->bPairTextureEnabled
                          && ed_atlas_pair_available(pAtlas, uiBackTile);
            if (!ed_build_material(pAtlas, uiBackFlags,
                                   pInfo->uiTextureSet,
                                   bBackPair, &BackMaterial)
                    || !ed_material_table_intern(pMaterials, &BackMaterial,
                                                 &Surface.uiBackMaterialId))
                return false;
        }
    }

    pfnEmit(&Surface, pUserData);
    return true;
}
