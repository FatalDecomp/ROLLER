#ifndef SCENE_RENDER_GPU_H
#define SCENE_RENDER_GPU_H

#include "scene_render_types.h"
#include "debug_overlay.h"
#include "sound.h"    /* tColor, pal_addr */

typedef struct SceneRendererGPU SceneRendererGPU;
struct SceneRenderer;
struct CRTFilter;

/* Extract the GPU sub-renderer from an opaque SceneRenderer.
 * Returns NULL if the GPU renderer was not created (e.g. no GPU device). */
SceneRendererGPU *scene_render_get_gpu(struct SceneRenderer *renderer);

SceneRendererGPU *scene_render_gpu_create(SDL_GPUDevice *device, SDL_Window *window);
void              scene_render_gpu_destroy(SceneRendererGPU *r);

void scene_render_gpu_begin_frame(SceneRendererGPU *r);
void scene_render_gpu_end_frame(SceneRendererGPU *r);
void scene_render_gpu_cancel_frame(SceneRendererGPU *r);
void scene_render_gpu_discard_queued(SceneRendererGPU *r);

void scene_render_gpu_set_viewport(SceneRendererGPU *r,
                                   int x, int y, int w, int h);
void scene_render_gpu_set_camera(SceneRendererGPU *r,
                                 const SceneRenderCamera *cam);
void scene_render_gpu_set_projection(SceneRendererGPU *r,
                                     const SceneRenderProjection *proj);

SceneTextureHandle scene_render_gpu_load_texture(SceneRendererGPU *r,
                                                 uint8 *pixelData,
                                                 int width, int height,
                                                 int tex_idx,
                                                 int texHalfRes);
void               scene_render_gpu_free_texture(SceneRendererGPU *r,
                                                 SceneTextureHandle handle);
SceneTextureHandle scene_render_gpu_get_texture_handle(SceneRendererGPU *r,
                                                       int tex_idx);

void scene_render_gpu_quad_world_legacy(SceneRendererGPU *r,
                                        const SceneRenderVertex verts[4],
                                        SceneTextureHandle texture,
                                        int surfaceFlags,
                                        SceneRenderLegacyQuadOptions options);

void scene_render_gpu_set_sky_color(SceneRendererGPU *r,
                                    float red, float green, float blue);

/* colorIdx:  palette index for ground clear colour (-1 = all sky).
   anyGround: true when at least one screen corner is on the ground side.
   poly:      NDC sky-region polygon vertices (CCW, 3-5 verts) from S-H clip.
   n_verts:   number of polygon vertices (0 = no sky quad). */
void scene_render_gpu_set_horizon(SceneRendererGPU *r, int colorIdx, bool anyGround,
                                  const float (*poly)[2], int n_verts);

/* filter: 0=nearest, 1=bilinear, 2=anisotropic */
void scene_render_gpu_set_texture_filter(SceneRendererGPU *r, int filter);

/* enabled: true = trilinear (LINEAR between mip levels); textures always carry a full mip chain */
void scene_render_gpu_set_trilinear(SceneRendererGPU *r, bool enabled);

/* vsync: deferred to next begin_frame to avoid mid-frame swapchain conflict */
void scene_render_gpu_set_vsync(SceneRendererGPU *r, bool enabled);

/* level: 0=2x, 1=4x, 2=8x, 3=16x — only applied when texture filter is anisotropic */
void scene_render_gpu_set_anisotropy_level(SceneRendererGPU *r, int level);

/* bias: mip LOD offset; negative = sharper (lower mip), positive = blurrier (higher mip) */
void scene_render_gpu_set_lod_bias(SceneRendererGPU *r, float bias);

/* scale: internal render resolution multiplier; 1.0=native, 2.0=4x pixels (SSAA) */
void scene_render_gpu_set_render_scale(SceneRendererGPU *r, float scale);

/* density: exponential-squared fog coefficient; 0.0 = off */
void scene_render_gpu_set_fog_density(SceneRendererGPU *r, float density);

/* fog colour: linear RGB [0..1] each channel */
void scene_render_gpu_set_fog_color(SceneRendererGPU *r, float fr, float fg, float fb);

/* gamma: output gamma exponent; 1.0 = neutral, <1 = brighter, >1 = darker */
void scene_render_gpu_set_gamma(SceneRendererGPU *r, float gamma);

/* start: view-space depth at which fog begins; 0.0 = fog from camera */
void scene_render_gpu_set_fog_start(SceneRendererGPU *r, float start);

/* saturation: 0.0 = greyscale, 1.0 = neutral, >1 = boosted */
void scene_render_gpu_set_saturation(SceneRendererGPU *r, float saturation);

/* contrast: 0.0 = flat grey, 1.0 = neutral, >1 = high contrast */
void scene_render_gpu_set_contrast(SceneRendererGPU *r, float contrast);

/* strength: 0.0 = off, higher = darker edges */
void scene_render_gpu_set_vignette(SceneRendererGPU *r, float strength);

/* brightness: additive offset; 0.0 = neutral, positive = brighter, negative = darker */
void scene_render_gpu_set_brightness(SceneRendererGPU *r, float brightness);

/* mult: FOV multiplier on top of game camera; 1.0 = native, <1 = zoom in, >1 = zoom out */
void scene_render_gpu_set_fov_multiplier(SceneRendererGPU *r, float mult);

/* enabled: true = wireframe (line) fill mode, false = solid */
void scene_render_gpu_set_wireframe(SceneRendererGPU *r, bool enabled);

/* mode: 0=default(none), 1=none, 2=back-face cull, 3=front-face cull (debug only) */
void scene_render_gpu_set_cull_mode(SceneRendererGPU *r, int mode);

/* level: 0=off, 1=2x, 2=4x, 3=8x */
void scene_render_gpu_set_msaa(SceneRendererGPU *r, int level);

void scene_render_gpu_set_hud_buffer(SceneRendererGPU *r,
                                     uint8 *buf, int w, int h);
void scene_render_gpu_set_split_screen(SceneRendererGPU *r, bool split);

void scene_render_gpu_set_debug_overlay(SceneRendererGPU *r, DebugOverlay *overlay);
void scene_render_gpu_set_crt_filter(SceneRendererGPU *r, struct CRTFilter *filter);

void scene_render_gpu_build_vp(const SceneRendererGPU *r, float vp[16]);
int  scene_render_gpu_get_render_chunk(const SceneRendererGPU *r);

/* Queue a car mesh draw into the current frame (called by game_render_hardware.c). */
void scene_render_gpu_queue_car_draw(SceneRendererGPU *r,
                                      SDL_GPUBuffer    *vertBuf,
                                      SDL_GPUBuffer    *idxBuf,
                                      SDL_GPUTexture   *texture,
                                      int               firstIndex,
                                      int               idxCount,
                                      const float       mvp[16]);

#endif /* SCENE_RENDER_GPU_H */
