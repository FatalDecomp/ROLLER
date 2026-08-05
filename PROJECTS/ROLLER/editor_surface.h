#ifndef ROLLER_EDITOR_SURFACE_H
#define ROLLER_EDITOR_SURFACE_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

#define ED_SURFACE_VERTEX_COUNT 4u
#define ED_MATERIAL_ID_NONE ROLLER_ED_INVALID_MATERIAL_ID

typedef enum
{
    ROLLER_ED_SURFACE_CLASS_CENTER = 0,
    ROLLER_ED_SURFACE_CLASS_LEFT_SHOULDER,
    ROLLER_ED_SURFACE_CLASS_RIGHT_SHOULDER,
    ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
    ROLLER_ED_SURFACE_CLASS_RIGHT_WALL,
    ROLLER_ED_SURFACE_CLASS_ROOF,
    ROLLER_ED_SURFACE_CLASS_OUTER_WALL_FLOOR,
    ROLLER_ED_SURFACE_CLASS_LEFT_LOWER_OUTER_WALL,
    ROLLER_ED_SURFACE_CLASS_RIGHT_LOWER_OUTER_WALL,
    ROLLER_ED_SURFACE_CLASS_LEFT_UPPER_OUTER_WALL,
    ROLLER_ED_SURFACE_CLASS_RIGHT_UPPER_OUTER_WALL,
    ROLLER_ED_SURFACE_CLASS_SIGN,
    ROLLER_ED_SURFACE_CLASS_BUILDING,
    ROLLER_ED_SURFACE_CLASS_TOWER
} eRollerEdSurfaceClass;

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

typedef bool (*tEdVisitChunkFn)(uint32_t uiChunkId, void *pUserData);

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

typedef struct
{
    uint32_t uiFirstChunkId;
    uint32_t uiLastChunkId;
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

bool ed_surface_compute_render_uvs(
    uint8_t byRenderUVLayout,
    bool bHalfResolution,
    int32_t aiRenderU16_16[ED_SURFACE_VERTEX_COUNT],
    int32_t aiRenderV16_16[ED_SURFACE_VERTEX_COUNT]);

bool ed_traverse_full_track_chunks(uint32_t uiLoadedChunkCount,
                                   tEdVisitChunkFn pfnVisit,
                                   void *pUserData);

bool ed_emit_surface(const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
                     const tEdSurfaceInfo *pInfo,
                     tEdMaterialTable *pMaterials,
                     tEdEmitSurfaceFn pfnEmit,
                     void *pUserData);

#endif
