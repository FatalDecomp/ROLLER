#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"
#include "scene_render.h"

typedef enum {
    GAME_RENDER_GPU,
    GAME_RENDER_SOFTWARE
} GameRenderMode;

typedef SceneTextureHandle TextureHandle;
#define TEXTURE_HANDLE_INVALID SCENE_TEXTURE_HANDLE_INVALID

typedef SceneRenderVertex GameRenderVertex;
typedef SceneRenderCamera GameRenderCamera;
typedef SceneRenderProjection GameRenderProjection;

typedef struct GameRenderCarPose {
    tVec3 position;
    int yaw;
    int pitch;
    int roll;
} GameRenderCarPose;

typedef struct GameRenderCarOptions {
    int anim_frame;
    const uint8 *color_remap;
} GameRenderCarOptions;

#define GAME_RENDER_SUBDIVIDE_TYPE_AUTO SCENE_RENDER_SUBDIVIDE_TYPE_AUTO
#define GAME_RENDER_SUBDIVIDE_TYPE_CLOUD SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD
#define GAME_RENDER_SUBDIVIDE_TYPE_BUILDING SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING
#define GAME_RENDER_SUBDIVIDE_TYPE_SIGN SCENE_RENDER_SUBDIVIDE_TYPE_SIGN

typedef struct GameRenderer GameRenderer;

// Lifecycle
GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window);
void game_render_destroy(GameRenderer *renderer);
void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode);
GameRenderMode game_render_get_mode(GameRenderer *renderer);
void game_render_set_split_screen(GameRenderer *renderer, bool split);
bool game_render_is_split_screen(GameRenderer *renderer);
void game_render_set_debug_overlay(GameRenderer *renderer, DebugOverlay *overlay);

// Frame lifecycle
void game_render_begin_frame(GameRenderer *renderer);
void game_render_end_frame(GameRenderer *renderer);

// Mirror pass: in GPU mode, temporarily routes scene calls through the SW
// renderer so the mirror buffer gets a proper SW-rendered backward view.
void game_render_begin_mirror_pass(GameRenderer *renderer);
void game_render_end_mirror_pass(GameRenderer *renderer);

// Viewport
void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h);
void game_render_set_target(GameRenderer *renderer, uint8 *buffer,
                            int stride, int width, int height);

// Camera
void game_render_set_camera(GameRenderer *renderer,
                            const GameRenderCamera *camera);

// Projection
void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj);

// Unified texture loading
// tex_idx: use TEXTURE_BANK_* constants.
// gfx_size determines layout: 0=64x64, 1=32x32.
// Returned handles are GameRenderer-level texture-bank tokens. The renderer
// translates them to the active backend's internal slot at draw time.
TextureHandle game_render_load_texture(GameRenderer *renderer,
                                       uint8 *pixelData,
                                       int width, int height,
                                       int tex_idx, int gfx_size);
void game_render_free_texture(GameRenderer *renderer,
                              TextureHandle handle);

// Look up the GameRenderer-level handle registered for a texture bank index.
TextureHandle game_render_get_texture_handle(GameRenderer *renderer,
                                             int tex_idx);

// Asset loading — sprite blocks (HUD)
TextureHandle game_render_load_blocks(GameRenderer *renderer, int slot,
                                      tBlockHeader *blocks,
                                      const tColor *palette);
void game_render_free_blocks(GameRenderer *renderer, int slot);

// Draw — polygon (track, buildings, particles, clouds)
// Pass TEXTURE_HANDLE_INVALID for flat (untextured) polygons.
void game_render_quad_screen(GameRenderer *renderer,
                      tPolyParams *poly,
                      TextureHandle handle,
                      const uint8 *palette_remap);

// Draw — world-space quad (GPU-ready interface)
// verts must point to exactly 4 GameRenderVertex entries.
// surfaceFlags carries the same bitfield as tPolyParams.iSurfaceType.
// subThreshold is the legacy `subdivides[i] * subscale` depth threshold;
// when >0 and the polygon's nearest projected Z meets/exceeds it, the
// renderer skips subdivide and rasterizes via POLYTEX/POLYFLAT directly
// (matches the legacy subdivide-vs-game_render_quad_screen branch). Pass 0.0f
// to always subdivide.
void game_render_quad_world(GameRenderer *renderer,
                            const GameRenderVertex *verts,
                            TextureHandle handle,
                            int surfaceFlags,
                            float subThreshold);
void game_render_quad_world_subdivide_type(GameRenderer *renderer,
                                           const GameRenderVertex *verts,
                                           TextureHandle handle,
                                           int surfaceFlags,
                                           int subdivideType,
                                           float subThreshold);

// Draw — car mesh
void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          const GameRenderCarPose *pose,
                          const GameRenderCarOptions *options);

// Draw — sky/horizon background
void game_render_draw_sky(GameRenderer *renderer,
                          const GameRenderCamera *camera,
                          const GameRenderProjection *projection);

// Draw — HUD sprites
void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette);

// Draw — HUD print_block (scaled sprite blit to dest pointer)
void game_render_print_block(GameRenderer *renderer, int slot, int blockIdx,
                             uint8 *pDest);

// Palette
void game_render_set_palette(GameRenderer *renderer, const tColor *palette);

// Fade
void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames);
int game_render_fade_active(GameRenderer *renderer);

// Texture filtering (GPU mode only): 0=nearest, 1=bilinear, 2=anisotropic
void game_render_set_texture_filter(GameRenderer *renderer, int filter);
// Trilinear filtering (GPU mode only): true = blend linearly between mip levels
void game_render_set_trilinear(GameRenderer *renderer, bool enabled);
// Anisotropy level (GPU mode only): 0=2x, 1=4x, 2=8x, 3=16x
void game_render_set_anisotropy_level(GameRenderer *renderer, int level);
// LOD bias (GPU mode only): negative=sharper, positive=blurrier
void game_render_set_lod_bias(GameRenderer *renderer, float bias);
// Render scale (GPU mode only): 1.0=native, 1.5=2.25x pixels, 2.0=4x pixels (SSAA)
void game_render_set_render_scale(GameRenderer *renderer, float scale);
// Fog density (GPU mode only): 0.0=off; exponential-squared coefficient
void game_render_set_fog_density(GameRenderer *renderer, float density);
// Fog color (GPU mode only): linear RGB [0..1] each channel
void game_render_set_fog_color(GameRenderer *renderer, float fr, float fg, float fb);
// Gamma correction (GPU mode only): 1.0=neutral, <1=brighter, >1=darker
void game_render_set_gamma(GameRenderer *renderer, float gamma);
// Anti-aliasing (GPU mode only): 0=off, 1=2x, 2=4x, 3=8x
void game_render_set_antialiasing(GameRenderer *renderer, int level);
// V-sync: true=vsync on, false=immediate present
void game_render_set_vsync(GameRenderer *renderer, bool enabled);
// Fog start distance (GPU mode only): view-space depth before which fog is suppressed
void game_render_set_fog_start(GameRenderer *renderer, float start);
// Saturation (GPU mode only): 0=greyscale, 1=neutral, >1=boosted
void game_render_set_saturation(GameRenderer *renderer, float saturation);
// Contrast (GPU mode only): 0=flat grey, 1=neutral, >1=high contrast
void game_render_set_contrast(GameRenderer *renderer, float contrast);
// Vignette strength (GPU mode only): 0=off, higher=darker edges
void game_render_set_vignette(GameRenderer *renderer, float strength);
// FOV multiplier (GPU mode only): 1.0=native camera, <1=zoom in, >1=zoom out
void game_render_set_fov_multiplier(GameRenderer *renderer, float mult);
// Wireframe (GPU mode only): true=line fill, false=solid
void game_render_set_wireframe(GameRenderer *renderer, bool enabled);
// Brightness (GPU mode only): additive offset; 0.0=neutral, positive=brighter, negative=darker
void game_render_set_brightness(GameRenderer *renderer, float brightness);

#endif
