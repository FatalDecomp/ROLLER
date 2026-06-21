#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "scene_render_types.h"
#include "debug_overlay.h"
#include <stdbool.h>

#define TEXTURE_BANK_TRACK    0
#define TEXTURE_BANK_BUILDING 17
#define TEXTURE_BANK_CARGEN   18

typedef struct SceneRenderer SceneRenderer;

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

void scene_render_set_use_gpu(SceneRenderer *renderer, bool use_gpu);
void scene_render_set_split_screen(SceneRenderer *renderer, bool split);
void scene_render_set_debug_overlay(SceneRenderer *renderer, DebugOverlay *overlay);

#endif
