#ifndef SCENE_RENDER_SOFTWARE_H
#define SCENE_RENDER_SOFTWARE_H

#include <SDL3/SDL.h>
#include "scene_render.h"

typedef struct SceneRendererSoftware SceneRendererSoftware;

SceneRendererSoftware *scene_render_sw_create(SDL_GPUDevice *device,
                                              SDL_Window *window);
void scene_render_sw_destroy(SceneRendererSoftware *sw);

void scene_render_sw_set_target(SceneRendererSoftware *sw, uint8 *buffer,
                                int stride, int width, int height);
void scene_render_sw_set_viewport(SceneRendererSoftware *sw, int x, int y,
                                  int w, int h);
void scene_render_sw_set_camera(SceneRendererSoftware *sw,
                                const SceneRenderCamera *camera);
void scene_render_sw_set_projection(SceneRendererSoftware *sw,
                                    const SceneRenderProjection *proj);

SceneTextureHandle scene_render_sw_load_texture(SceneRendererSoftware *sw,
                                                uint8 *pixelData, int width,
                                                int height, int tex_idx,
                                                int texHalfRes);
void scene_render_sw_free_texture(SceneRendererSoftware *sw,
                                  SceneTextureHandle handle);
SceneTextureHandle scene_render_sw_get_texture_handle(SceneRendererSoftware *sw,
                                                      int tex_idx);

void scene_render_sw_quad_world_legacy(SceneRendererSoftware *sw,
                                       const SceneRenderVertex *verts,
                                       SceneTextureHandle handle,
                                       int surfaceFlags,
                                       SceneRenderLegacyQuadOptions options);

#endif
