#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "scene_render_types.h"
#include "debug_overlay.h"
#include <stdbool.h>

#define TEXTURE_BANK_TRACK    0
#define TEXTURE_BANK_BUILDING 17
#define TEXTURE_BANK_CARGEN   18

typedef struct SceneRenderer SceneRenderer;
typedef struct SceneRendererGPU SceneRendererGPU;

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

void scene_render_gpu_begin_frame_via_scene(SceneRenderer *renderer);
void scene_render_gpu_end_frame_via_scene(SceneRenderer *renderer);
void scene_render_gpu_cancel_frame_via_scene(SceneRenderer *renderer);
void scene_render_activate_gpu(SceneRenderer *renderer);
void scene_render_gpu_set_sky_color_via_scene(SceneRenderer *renderer,
                                               float r, float g, float b);
void scene_render_gpu_set_hud_buffer_via_scene(SceneRenderer *renderer,
                                                uint8 *buf, int w, int h);
/* filter: 0=nearest, 1=bilinear, 2=anisotropic */
void scene_render_gpu_set_texture_filter_via_scene(SceneRenderer *renderer,
                                                    int filter);
/* vsync: deferred to next begin_frame */
void scene_render_gpu_set_vsync_via_scene(SceneRenderer *renderer, bool enabled);
/* enabled: true = trilinear mipmap blending */
void scene_render_gpu_set_trilinear_via_scene(SceneRenderer *renderer, bool enabled);
/* level: 0=2x, 1=4x, 2=8x, 3=16x */
void scene_render_gpu_set_anisotropy_level_via_scene(SceneRenderer *renderer, int level);
/* bias: mip LOD offset */
void scene_render_gpu_set_lod_bias_via_scene(SceneRenderer *renderer, float bias);
/* scale: render resolution multiplier; 1.0=native, 2.0=4x pixels */
void scene_render_gpu_set_render_scale_via_scene(SceneRenderer *renderer, float scale);
/* density: exponential-squared fog coefficient; 0.0 = off */
void scene_render_gpu_set_fog_density_via_scene(SceneRenderer *renderer, float density);
/* fog colour: linear RGB [0..1] each channel */
void scene_render_gpu_set_fog_color_via_scene(SceneRenderer *renderer, float fr, float fg, float fb);
/* gamma: 1.0 = neutral */
void scene_render_gpu_set_gamma_via_scene(SceneRenderer *renderer, float gamma);
/* fog start: view-space depth before which fog is suppressed */
void scene_render_gpu_set_fog_start_via_scene(SceneRenderer *renderer, float start);
/* saturation: 0=greyscale, 1=neutral */
void scene_render_gpu_set_saturation_via_scene(SceneRenderer *renderer, float saturation);
/* contrast: 0=flat, 1=neutral */
void scene_render_gpu_set_contrast_via_scene(SceneRenderer *renderer, float contrast);
/* vignette: 0=off */
void scene_render_gpu_set_vignette_via_scene(SceneRenderer *renderer, float strength);
/* brightness: additive offset; 0.0=neutral */
void scene_render_gpu_set_brightness_via_scene(SceneRenderer *renderer, float brightness);
/* fov multiplier: 1.0 = native camera FOV */
void scene_render_gpu_set_fov_multiplier_via_scene(SceneRenderer *renderer, float mult);
/* wireframe: true = line fill */
void scene_render_gpu_set_wireframe_via_scene(SceneRenderer *renderer, bool enabled);
/* level: 0=off, 1=2x, 2=4x, 3=8x */
void scene_render_gpu_set_msaa_via_scene(SceneRenderer *renderer, int level);
SceneRendererGPU *scene_render_get_gpu(SceneRenderer *renderer);

#endif
