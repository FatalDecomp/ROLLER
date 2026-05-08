#ifndef GAME_RENDER_H
#define GAME_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"

typedef enum {
    GAME_RENDER_GPU,
    GAME_RENDER_SOFTWARE
} GameRenderMode;

typedef int TextureHandle;
#define TEXTURE_HANDLE_INVALID 0

/* Texture bank indices passed to game_render_load_texture / get_texture_handle.
 * These identify which asset category a texture belongs to. */
#define TEXTURE_BANK_TRACK    0
#define TEXTURE_BANK_BUILDING 17
#define TEXTURE_BANK_CARGEN   18

typedef struct {
    float x, y, z;  // world-space position
    float u, v;     // texture coordinates
} GameRenderVertex;

typedef struct {
    float viewX, viewY, viewZ;
    float cosYaw, sinYaw;
    float fovScale;
} GameRenderCamera;

// Column-major 3×3 view matrix + screen-space projection state.
// view[col][row] maps to GLSL mat3 for direct GPU upload.
typedef struct {
    float view[3][3];
    int   screenScale;   // 6-bit fixed-point scale (was scr_size)
    int   centerX;       // projection origin X (was xbase)
    int   centerY;       // projection origin Y (was ybase)
    int   texHalfRes;    // 0=64×64, 1=32×32 (was gfx_size)
} GameRenderProjection;

typedef struct GameRenderer GameRenderer;

// Lifecycle
GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window);
void game_render_destroy(GameRenderer *renderer);
void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode);
GameRenderMode game_render_get_mode(GameRenderer *renderer);

// Frame lifecycle
void game_render_begin_frame(GameRenderer *renderer);
void game_render_end_frame(GameRenderer *renderer);

// Viewport
void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h);

// Camera
void game_render_set_camera(GameRenderer *renderer,
                            const GameRenderCamera *camera);

// Projection
void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj);

// Unified texture loading
// tex_idx: use TEXTURE_BANK_* constants.
// gfx_size determines layout: 0=64x64, 1=32x32.
TextureHandle game_render_load_texture(GameRenderer *renderer,
                                       uint8 *pixelData,
                                       int width, int height,
                                       int tex_idx, int gfx_size);
void game_render_free_texture(GameRenderer *renderer,
                              TextureHandle handle);

// Look up the handle registered for a given texture bank index
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

// Draw — car mesh
void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          int yaw, int pitch, int roll,
                          float worldX, float worldY, float worldZ,
                          int animFrame, const uint8 *color_remap);

// Draw — horizon
void game_render_draw_horizon(GameRenderer *renderer);

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
void game_render_fade_wait(GameRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx);

#endif
