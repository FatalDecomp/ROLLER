#ifndef SCENE_RENDER_GPU_H
#define SCENE_RENDER_GPU_H

#include "scene_render_types.h"
#include "debug_overlay.h"
#include "sound.h"    /* tColor, pal_addr */

/* GPU projection near/far planes — must match the perspective matrix in scene_render_gpu.c */
#define SCENE_GPU_NEAR 10.0f
#define SCENE_GPU_FAR  20000000.0f

typedef struct SceneRendererGPU SceneRendererGPU;
struct SceneRenderer;
struct CRTFilter;

/* Extract the GPU sub-renderer from an opaque SceneRenderer.
 * Returns NULL if the GPU renderer was not created (e.g. no GPU device). */
SceneRendererGPU *scene_render_get_gpu(struct SceneRenderer *renderer);

SceneRendererGPU *scene_render_gpu_create(SDL_GPUDevice *device, SDL_Window *window);
void              scene_render_gpu_destroy(SceneRendererGPU *r);

/* Generic one-off GPU upload helpers -- not tied to any SceneRendererGPU
 * instance (just take the device directly), shared by scene_render_gpu.c,
 * game_render_hardware.c, and menu_render_gpu.c. Each acquires and submits
 * its own dedicated command buffer synchronously, so they're for textures/
 * buffers created once at init or on demand, NOT per-frame streaming data. */
SDL_GPUTexture *scene_render_gpu_upload_rgba(SDL_GPUDevice *dev, const uint8 *rgba,
                                              int w, int h, bool generateMipmaps);
SDL_GPUBuffer  *scene_render_gpu_upload_buffer(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage,
                                                const void *data, Uint32 size);

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
                                                 const uint8 *pixelData,
                                                 int width, int height,
                                                 int tex_idx,
                                                 int texHalfRes);
void               scene_render_gpu_free_texture(SceneRendererGPU *r,
                                                 SceneTextureHandle handle);
SceneTextureHandle scene_render_gpu_get_texture_handle(const SceneRendererGPU *r,
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
                                  const float (*skyPoly)[2], int n_verts);

/* filter: 0=nearest, 1=bilinear, 2=anisotropic */
void scene_render_gpu_set_texture_filter(SceneRendererGPU *r, int filter);

/* enabled: true = trilinear (LINEAR between mip levels); textures always carry a full mip chain */
void scene_render_gpu_set_trilinear(SceneRendererGPU *r, bool enabled);

/* disabled: clamp sampler max_lod to 0 — debug flag to isolate mipmap-related GPU bugs */
void scene_render_gpu_set_disable_mipmaps(SceneRendererGPU *r, bool disabled);

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

/* enabled: true = emulate SW rasterizer's extra-dark one-pixel track-darken quad border */
void scene_render_gpu_set_emulate_software_track_darken_border(SceneRendererGPU *r, bool enabled);

/* enabled: true = wireframe (line) fill mode, false = solid */
void scene_render_gpu_set_wireframe(SceneRendererGPU *r, bool enabled);

/* mode: 0=default(none), 1=none, 2=back-face cull, 3=front-face cull (debug only) */
void scene_render_gpu_set_cull_mode(SceneRendererGPU *r, int mode);

/* level: 0=off, 1=2x, 2=4x, 3=8x */
void scene_render_gpu_set_msaa(SceneRendererGPU *r, int level);

void scene_render_gpu_set_hud_buffer(SceneRendererGPU *r,
                                     uint8 *buf, int w, int h);
void scene_render_gpu_set_split_screen(SceneRendererGPU *r, bool split);

void texture_uv_map_reset(void);
void texture_uv_map_dump(int texId, bool splitScreen);

void scene_render_gpu_set_debug_overlay(SceneRendererGPU *r, DebugOverlay *overlay);
void scene_render_gpu_set_crt_filter(SceneRendererGPU *r, struct CRTFilter *filter);

void scene_render_gpu_build_vp(const SceneRendererGPU *r, float vp[16]);
int  scene_render_gpu_get_render_chunk(const SceneRendererGPU *r);

/* Project an already-camera-space point (fvx,fvy,fvz -- the same R·(world-cam)
 * coordinates the SW vk1-9 transform produces) to NDC, using the SAME FOV/
 * viewport math as the real 3D scene (build_mvp), rather than a SW-pixel-space
 * (scrX/scrY) intermediate -- see the definition for why that intermediate
 * breaks in "Render Scale (native)" mode. Returns false (leaves outNdcX/Y
 * untouched) if fvz <= 0 (point behind camera). */
bool scene_render_gpu_project_to_ndc(const SceneRendererGPU *r,
                                     double fvx, double fvy, double fvz,
                                     float *outNdcX, float *outNdcY);


/* Queue a flat-colour screen-space quad for the particle pass.
 * ndcX[4], ndcY[4]: NDC coordinates of the four corners (v0=top-right, v1=top-left,
 * v2=bottom-left, v3=bottom-right; same winding as tPolyParams SW quads).
 * cr/cg/cb/ca: linear RGBA colour [0..1].
 * Uses the current particleNdcZ set by scene_render_gpu_set_particle_ndcz. */
void scene_render_gpu_screen_quad_flat(SceneRendererGPU *r,
                                       const float ndcX[4], const float ndcY[4],
                                       float cr, float cg, float cb, float ca);

/* Set the per-particle NDC depth used by subsequent screen_quad_* calls.
 * 0.0 = near plane (always passes depth test). Reset to 0 each begin_frame. */
void scene_render_gpu_set_particle_ndcz(SceneRendererGPU *r, float ndcZ);

/* Set per-vertex NDC depth for the NEXT screen_quad_* call only (v0..v3 order).
 * Consumed and cleared after a single call. Use for elongated trails where head
 * and tail are at different camera depths to avoid Z-fighting through walls. */
void scene_render_gpu_set_particle_ndcz_pervertex(SceneRendererGPU *r, const float ndcZ[4]);

/* Return the GPU texture for tile_idx within slot tex_idx, or NULL if out of range. */
SDL_GPUTexture *scene_render_gpu_get_tile_texture(SceneRendererGPU *r, int tex_idx, int tile_idx);

/* Cached flat-colour texture for a palette index (built once per index, reused
 * after). Use via screen_quad_textured (NOT screen_quad_flat) for any UI
 * element that must draw correctly in 2-player mode: flat-particle quads draw
 * as a whole batch BEFORE textured-particle quads in the same render pass
 * regardless of queue order, and each player's composited view is itself an
 * opaque textured quad -- a flat quad queued earlier gets painted over. */
SDL_GPUTexture *scene_render_gpu_get_flat_color_texture(SceneRendererGPU *r, int colorIdx);

/* Return the particle-variant tile texture (palette index 0 = opaque white) for tile_idx.
 * Only cargen (tex_idx 18) has a particle variant; other slots fall back to the normal tile. */
SDL_GPUTexture *scene_render_gpu_get_particle_tile_texture(SceneRendererGPU *r, int tex_idx, int tile_idx);

/* Accumulate a textured particle quad for this frame (depth-tested via particleNdcZ).
 * Returns true if accepted, false if the frame's texture slot is already taken by a
 * different texture — caller should fall through to SW in that case. */
bool scene_render_gpu_screen_quad_textured(SceneRendererGPU *r,
                                           const float ndcX[4], const float ndcY[4],
                                           SDL_GPUTexture *tex,
                                           float cr, float cg, float cb, float ca);

/* Full-screen translucent black quad for the in-race pause-menu darken
 * effect. Deliberately routed through the textured-particle path rather
 * than screen_quad_flat -- see the definition for why (2-player composite
 * ordering). */
bool scene_render_gpu_screen_quad_darken(SceneRendererGPU *r, float alpha);

/* Queue a car mesh draw into the current frame (called by game_render_hardware.c). */
void scene_render_gpu_queue_car_draw(SceneRendererGPU *r,
                                      SDL_GPUBuffer    *vertBuf,
                                      SDL_GPUBuffer    *idxBuf,
                                      SDL_GPUTexture   *texture,
                                      int               firstIndex,
                                      int               idxCount,
                                      const float       mvp[16]);

/* Queue a car shadow draw — uses carShadowPipeline (LESS_OR_EQUAL, no depth write, biased). */
void scene_render_gpu_queue_car_shadow_draw(SceneRendererGPU *r,
                                             SDL_GPUBuffer    *vertBuf,
                                             SDL_GPUBuffer    *idxBuf,
                                             SDL_GPUTexture   *texture,
                                             int               firstIndex,
                                             int               idxCount,
                                             const float       mvp[16]);

/* Call before a secondary view's own draw_road() runs -- see the struct-field
 * comment in scene_render_gpu.c for why the timing matters (must be before
 * that view's scene, including real smoke/particles, gets queued). */
void scene_render_gpu_secondary_view_will_queue(SceneRendererGPU *r);

/* Render the queued scene draws (produced by calling the normal camera/
 * projection/draw_car/quad_world/etc. API with a secondary camera already
 * set) into a small dedicated offscreen colour target instead of the main
 * swapchain, then reset the shared per-frame draw-command/vertex state so
 * the NEXT queued scene (e.g. the main view, or another secondary view)
 * starts clean. Used by the rearview/side mirror (slot 0) and 2-player
 * split screen (slot 0 = player 1, slot 1 = player 2).
 * slot: which persistent offscreen texture to render into (0..
 * SCENE_GPU_MAX_SECONDARY_VIEWS-1) -- each slot keeps its own texture so
 * multiple secondary views can be composited later in the same frame
 * without one flush overwriting another's not-yet-consumed texture.
 * texW/texH: desired render-target pixel size. Returns the resulting
 * texture (owned by the renderer, valid until the next call with this slot
 * or destroy), or NULL on failure.
 * Must be called after scene_render_gpu_secondary_view_will_queue() and
 * that view's draw_road() have both already run. */
SDL_GPUTexture *scene_render_gpu_flush_secondary_view(SceneRendererGPU *r, int slot, int texW, int texH);

#endif /* SCENE_RENDER_GPU_H */
