#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"
#include "scene_render.h"
#include "crt_filter.h"

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

/* GPU-only routing flag set by building.c on real advert-sign quads.
 * Uses bit 20 (SURFACE_FLAG_BOUNCE_20 in physics) which building polygon data
 * never occupies (building uiTex is 16-bit) and is not checked by the GPU
 * renderer for any visual purpose.  SW renderer ignores it entirely. */
#define SURFACE_FLAG_GPU_IS_SIGN 0x00100000

typedef struct GameRenderer GameRenderer;

// Lifecycle
GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window);
void game_render_destroy(GameRenderer *renderer);
void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode);
GameRenderMode game_render_get_mode(const GameRenderer *renderer);
void game_render_set_force_gpu_load(GameRenderer *renderer, bool force);
void game_render_set_particle_depth(GameRenderer *renderer, float ndcZ);
void game_render_set_split_screen(GameRenderer *renderer, bool split);
bool game_render_is_split_screen(const GameRenderer *renderer);
void game_render_set_debug_overlay(GameRenderer *renderer, DebugOverlay *overlay);
void game_render_set_crt_filter(GameRenderer *renderer, CRTFilter *filter);

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

// Set the clip-space depth (NDC Z in [0,1]) for the next game_render_quad_screen
// call so GPU particles are depth-tested against scene geometry.
// Must be called before each particle quad.  Ignored in SW mode.
void game_render_set_particle_depth(GameRenderer *renderer, float ndcZ);

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

#endif
