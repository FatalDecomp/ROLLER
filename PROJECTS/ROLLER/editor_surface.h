#ifndef ROLLER_EDITOR_SURFACE_H
#define ROLLER_EDITOR_SURFACE_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

#define ED_SURFACE_VERTEX_COUNT 4u
#define ED_MATERIAL_ID_NONE ROLLER_ED_INVALID_MATERIAL_ID

typedef enum
{
    ROLLER_ED_SURFACE_CLASS_LEFT_WALL = 0
} eRollerEdSurfaceClass;

typedef enum
{
    ROLLER_ED_TOPOLOGY_QUAD = 0
} eRollerEdTopology;

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

typedef struct
{
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
    tEdTextureAtlas Atlas;
} tEdMaterialTable;

typedef struct
{
    tEdSurfaceVertex aVertices[ED_SURFACE_VERTEX_COUNT];
    uint32_t uiVertexCount;
    uint32_t uiFrontMaterialId;
    uint32_t uiBackMaterialId;
    uint32_t uiChunkId;
    uint32_t uiRenderFlags;
    float fSubdivideThreshold;
    uint16_t unSurfaceClass;
    uint16_t unContentClass;
    uint16_t unFlags;
    uint8_t byTopology;
    uint8_t byReserved;
} tEdSurfaceEmission;

typedef void (*tEdEmitSurfaceFn)(const tEdSurfaceEmission *pSurface,
                                 void *pUserData);

typedef struct
{
    uint32_t uiChunkId;
    uint32_t uiRenderFlags;
    uint32_t uiBackSurfaceFlags;
    uint32_t uiTextureSet;
    float fSubdivideThreshold;
    bool bPairTextureEnabled;
    bool bHighWall;
} tEdLeftWallSurfaceInfo;

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

const tEdMaterial *ed_material_table_get(const tEdMaterialTable *pTable,
                                         uint32_t uiMaterialId);

void ed_material_resolve_uv(const tEdMaterial *pMaterial,
                            const float afMaterialUV[2],
                            float afAtlasUV[2]);

bool ed_surface_selection_matches(const tEdSurfaceSelection *pSelection,
                                  const tEdSurfaceEmission *pSurface);

uint32_t ed_surface_selection_render_flags(
    const tEdSurfaceSelection *pSelection,
    const tEdSurfaceEmission *pSurface);

bool ed_emit_left_wall_surface(const float afWorldVertices[ED_SURFACE_VERTEX_COUNT][3],
                               const tEdLeftWallSurfaceInfo *pInfo,
                               tEdMaterialTable *pMaterials,
                               tEdEmitSurfaceFn pfnEmit,
                               void *pUserData);

#endif
