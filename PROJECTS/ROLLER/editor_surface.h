#ifndef ROLLER_EDITOR_SURFACE_H
#define ROLLER_EDITOR_SURFACE_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

#define ED_SURFACE_VERTEX_COUNT 4u
#define ED_MATERIAL_ID_NONE ROLLER_ED_INVALID_MATERIAL_ID

/*
 * Canonical geometry conventions (E4A-S4). The authoritative prose lives in
 * docs/adr/0003-canonical-geometry-conventions.md; this is the summary the
 * emitter implements and tests/editor_surface_test.c asserts.
 *
 * Coordinate system: legacy ROLLER world space, unchanged. Axes satisfy the
 *   algebraic right-hand rule (X cross Y == Z) and world +Z is up. A heading
 *   of zero looks along world +X, and increasing heading rotates toward +Y.
 *   View space is left-handed (x right, y up, z into the screen); the emitter
 *   never enters view space.
 * Scale: none. World positions pass through verbatim from TrakPt/GroundPt in
 *   legacy track units. Unit conversion belongs to exporters, not here.
 * Winding: the producer's vertex order v0..v3 is preserved exactly. Its
 *   right-hand-rule normal is the front face -- the same side the renderer's
 *   facing test calls front-facing, and the side uiFrontMaterialId describes.
 * Normals: generated here, because the render vertex carries position and UV
 *   only. fNormal on the emission is the whole-quad (Newell) normal; each
 *   vertex also carries its own adjacent-edge normal so twisted quads stay
 *   correct. All are unit length, or exactly zero for degenerate geometry.
 * UV origin: top-left. v0/v1 sit on V=0 and v2/v3 on V=1; U runs left to
 *   right across the tile, or across both tiles of a pair.
 */
#define ED_SURFACE_WORLD_UP_AXIS 2u

/* eRollerEdSurfaceClass moved to editor_api.h in E3A-S2: the values are
 * published through tEdPrimitive.unSurfaceClass and index the overlay class
 * masks, so they belong to the public ABI rather than to the emitter. */

typedef enum
{
    ROLLER_ED_TOPOLOGY_QUAD = 0
} eRollerEdTopology;

typedef enum
{
    ROLLER_ED_RENDER_UV_TILE = 0,
    ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL = 1,
    ROLLER_ED_RENDER_UV_PAIR_VERTICAL = 2
} eRollerEdRenderUVLayout;

enum
{
    ROLLER_ED_SURFACE_FLAG_ALPHA = 1u << 0,
    ROLLER_ED_SURFACE_FLAG_TWO_SIDED = 1u << 1,
    ROLLER_ED_SURFACE_FLAG_TEXTURED = 1u << 2,
    ROLLER_ED_SURFACE_FLAG_PAIRED_TEXTURE = 1u << 3,
    ROLLER_ED_SURFACE_FLAG_HIGH_VARIANT = 1u << 4
};

typedef struct
{
    float fPosition[3];
    float fNormal[3];
    int32_t iRenderU16_16;
    int32_t iRenderV16_16;
    float fMaterialUV[2];
} tEdSurfaceVertex;

/* Main track, building/sign, and cargen are separate legacy texture banks with
 * independent tile counts, so a canonical stream that mixes them needs one
 * atlas description per set rather than a single global one. */
#define ED_MATERIAL_MAX_TEXTURE_SETS 4u

typedef struct
{
    uint32_t uiTextureSet;
    uint32_t uiWidth;
    uint32_t uiHeight;
    uint32_t uiTileSize;
    uint32_t uiTileCount;
} tEdTextureAtlas;

typedef struct
{
    tEdMaterial *pMaterials;
    uint32_t uiCapacity;
    uint32_t uiCount;
    tEdTextureAtlas aAtlases[ED_MATERIAL_MAX_TEXTURE_SETS];
    uint32_t uiAtlasCount;
    /* Every registered atlas shares this: the legacy tile size follows the
     * global gfx_size, not the bank. */
    uint32_t uiTileSize;
} tEdMaterialTable;

typedef struct
{
    tEdSurfaceVertex aVertices[ED_SURFACE_VERTEX_COUNT];
    /* Whole-quad front-face normal; unit length, or zero when degenerate. */
    float fNormal[3];
    uint32_t uiVertexCount;
    uint32_t uiFrontMaterialId;
    uint32_t uiBackMaterialId;
    uint32_t uiChunkId;
    uint32_t uiRenderFlags;
    int32_t iRenderSubdivideType;
    float fSubdivideThreshold;
    uint16_t unSurfaceClass;
    uint16_t unContentClass;
    uint16_t unFlags;
    uint8_t byTopology;
    uint8_t byReserved;
} tEdSurfaceEmission;

typedef void (*tEdEmitSurfaceFn)(const tEdSurfaceEmission *pSurface,
                                 void *pUserData);

/* A canonical traversal visitor: it receives an index and returns false to
 * abort the whole traversal. tEdVisitChunkFn is the track-chunk spelling of
 * the same signature. */
typedef bool (*tEdVisitIndexFn)(uint32_t uiIndex, void *pUserData);
typedef tEdVisitIndexFn tEdVisitChunkFn;

typedef struct
{
    uint32_t uiChunkId;
    uint32_t uiRenderFlags;
    uint32_t uiBackSurfaceFlags;
    uint32_t uiTextureSet;
    float fSubdivideThreshold;
    bool bPairTextureEnabled;
    bool bHighVariant;
    uint16_t unSurfaceClass;
    uint16_t unContentClass;
    uint16_t unFlags;
    uint8_t byTopology;
    uint8_t byRenderUVLayout;
    int32_t iRenderSubdivideType;
} tEdSurfaceInfo;

/* A chunk-range selection covers every class in the range, which is what the
 * editor's From/To chunk selection means. F-S4b's original single-class form
 * is still available for a caller that wants one class. */
#define ED_SURFACE_SELECTION_ANY_CLASS 0xFFFFu

typedef struct
{
    uint32_t uiFirstChunkId;
    uint32_t uiLastChunkId;
    /* A specific class, or ED_SURFACE_SELECTION_ANY_CLASS. */
    uint16_t unSurfaceClass;
    uint8_t byHighlightColour;
    bool bEnabled;
} tEdSurfaceSelection;

bool ed_material_table_init(tEdMaterialTable *pTable,
                            tEdMaterial *pStorage,
                            uint32_t uiCapacity,
                            tEdTextureAtlas Atlas);

bool ed_material_table_set_atlas(tEdMaterialTable *pTable,
                                 tEdTextureAtlas Atlas);

const tEdTextureAtlas *ed_material_table_atlas(const tEdMaterialTable *pTable,
                                               uint32_t uiTextureSet);

const tEdMaterial *ed_material_table_get(const tEdMaterialTable *pTable,
                                         uint32_t uiMaterialId);

bool ed_surface_identity_valid(const tEdSurfaceInfo *pInfo);

/*
 * True when the table can build a material for this surface. False only for a
 * textured surface whose tile is not in its bank -- which ed_emit_surface
 * refuses and the render path silently drops, so a producer that skips it
 * matches what is on screen. Every other ed_emit_surface refusal stays fatal.
 */
bool ed_surface_material_resolvable(const tEdMaterialTable *pTable,
                                    const tEdSurfaceInfo *pInfo);

bool ed_atlas_pair_available(const tEdTextureAtlas *pAtlas,
                             uint32_t uiTileIndex);

bool ed_atlas_pair_wraps_row(const tEdTextureAtlas *pAtlas,
                             uint32_t uiTileIndex);

void ed_material_resolve_uv(const tEdMaterial *pMaterial,
                            const float afMaterialUV[2],
                            float afAtlasUV[2]);

bool ed_surface_selection_matches(const tEdSurfaceSelection *pSelection,
                                  const tEdSurfaceEmission *pSurface);

uint32_t ed_surface_selection_render_flags(
    const tEdSurfaceSelection *pSelection,
    const tEdSurfaceEmission *pSurface);

bool ed_surface_compute_normals(
    const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
    float afSurfaceNormal[3],
    float afVertexNormals[ED_SURFACE_VERTEX_COUNT][3]);

bool ed_surface_compute_render_uvs(
    uint8_t byRenderUVLayout,
    bool bHalfResolution,
    int32_t aiRenderU16_16[ED_SURFACE_VERTEX_COUNT],
    int32_t aiRenderV16_16[ED_SURFACE_VERTEX_COUNT]);

/*
 * Builds one edge of a surface as a thin front-facing ribbon in the surface's
 * own plane, biased toward the front face so it wins the depth test. uiEdge
 * selects the edge from vertex uiEdge to vertex (uiEdge + 1) % 4. Returns
 * false for a degenerate edge, which is drawn as nothing rather than as a
 * NaN.
 */
bool ed_surface_wireframe_edge_quad(
    const tEdSurfaceEmission *pSurface,
    uint32_t uiEdge,
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3]);

/*
 * The same ribbon over a bare polygon rather than an emitted surface, so
 * E3A-S7's reference-mesh triangles use the identical width ratio, depth bias,
 * and winding rule the surface wireframe uses. uiPointCount is 3 or 4.
 */
bool ed_surface_wireframe_edge_quad_points(
    const float (*afPoints)[3],
    uint32_t uiPointCount,
    const float afNormal[3],
    uint32_t uiEdge,
    float afEdgeQuad[ED_SURFACE_VERTEX_COUNT][3]);

bool ed_traverse_full_track_chunks(uint32_t uiLoadedChunkCount,
                                   tEdVisitChunkFn pfnVisit,
                                   void *pUserData);

/*
 * The scenery counterpart (E4A-S6): visits every placed object index in order,
 * reading no visible set, camera, or render queue. drawtrk3_emit_full_scenery
 * drives it, exactly as drawtrk3_emit_full_track drives the chunk walk.
 */
bool ed_traverse_full_scenery_objects(uint32_t uiPlacedObjectCount,
                                      tEdVisitIndexFn pfnVisit,
                                      void *pUserData);

bool ed_emit_surface(const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
                     const tEdSurfaceInfo *pInfo,
                     tEdMaterialTable *pMaterials,
                     tEdEmitSurfaceFn pfnEmit,
                     void *pUserData);

#endif
