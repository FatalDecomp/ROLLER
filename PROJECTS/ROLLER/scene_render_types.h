#ifndef SCENE_RENDER_TYPES_H
#define SCENE_RENDER_TYPES_H

#include <SDL3/SDL.h>
#include "types.h"

typedef int SceneTextureHandle;
#define SCENE_TEXTURE_HANDLE_INVALID 0

typedef struct {
    float x, y, z;
    float u, v;
} SceneRenderVertex;

typedef struct {
    float viewX, viewY, viewZ;
    float cosYaw, sinYaw;
    float fovScale;
    int   renderChunkIdx;  /* track chunk used in calculatetransform; -1 if off-track */
} SceneRenderCamera;

typedef struct {
    float view[3][3];
    int   screenScale;
    int   centerX;
    int   centerY;
    int   texHalfRes;
} SceneRenderProjection;

typedef struct {
    int   subdivideType;
    float subThreshold;
} SceneRenderLegacyQuadOptions;

#define SCENE_RENDER_SUBDIVIDE_TYPE_AUTO     (-2147483647 - 1)
#define SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD    (-2147483647)
#define SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING 666
#define SCENE_RENDER_SUBDIVIDE_TYPE_SIGN     667

#endif /* SCENE_RENDER_TYPES_H */
