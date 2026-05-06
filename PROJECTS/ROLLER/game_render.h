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
                            int viewMode, int carIdx, int chaseCamIdx);

// Unified texture loading
// tex_idx selects the texture bank (0=track, 17=building, 18=cargen,
// 2..16=cars). gfx_size determines layout: 0=64x64, 1=32x32.
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
void game_render_quad(GameRenderer *renderer,
                      tPolyParams *poly,
                      TextureHandle handle,
                      const uint8 *palette_remap);

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
