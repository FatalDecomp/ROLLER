#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"

/* Texture bank indices passed to scene_render_load_texture / get_texture_handle.
 * These identify which legacy asset category a texture belongs to. */
#define TEXTURE_BANK_TRACK    0
#define TEXTURE_BANK_BUILDING 17
#define TEXTURE_BANK_CARGEN   18

typedef struct SceneRenderer SceneRenderer;
typedef int SceneTextureHandle;
#define SCENE_TEXTURE_HANDLE_INVALID 0

typedef struct {
    float x, y, z;  // world-space position
    float u, v;     // texture coordinates
} SceneRenderVertex;

#define SCENE_RENDER_SUBDIVIDE_TYPE_AUTO     (-2147483647 - 1)
#define SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD    (-2147483647)
#define SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING 666

typedef struct {
    float viewX, viewY, viewZ;
    float cosYaw, sinYaw;
    float fovScale;
} SceneRenderCamera;

// Column-major 3×3 view matrix + screen-space projection state.
// view[col][row] maps to GLSL mat3 for direct GPU upload.
typedef struct {
    float view[3][3];
    int   screenScale;   // 6-bit fixed-point scale (was scr_size)
    int   centerX;       // projection origin X (was xbase)
    int   centerY;       // projection origin Y (was ybase)
    int   texHalfRes;    // 0=64×64, 1=32×32 (was gfx_size)
} SceneRenderProjection;

typedef struct {
    int subdivideType;
    float subThreshold;
} SceneRenderLegacyQuadOptions;

SceneRenderer *scene_render_create(SDL_GPUDevice *device, SDL_Window *window);
void scene_render_destroy(SceneRenderer *renderer);

void scene_render_set_target(SceneRenderer *renderer, uint8 *buffer,
                             int stride, int width, int height);
void scene_render_set_viewport(SceneRenderer *renderer,
                               int x, int y, int w, int h);
void scene_render_set_camera(SceneRenderer *renderer,
                             const SceneRenderCamera *camera);
void scene_render_set_projection(SceneRenderer *renderer,
                                 const SceneRenderProjection *projection);

SceneTextureHandle scene_render_load_texture(SceneRenderer *renderer,
                                             uint8 *pixelData,
                                             int width, int height,
                                             int tex_idx,
                                             int texHalfRes);
void scene_render_free_texture(SceneRenderer *renderer,
                               SceneTextureHandle handle);
SceneTextureHandle scene_render_get_texture_handle(SceneRenderer *renderer,
                                                   int tex_idx);

void scene_render_quad_world_legacy(SceneRenderer *renderer,
                                    const SceneRenderVertex verts[4],
                                    SceneTextureHandle texture,
                                    int surfaceFlags,
                                    SceneRenderLegacyQuadOptions options);

#endif
