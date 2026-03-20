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

// Asset loading — track textures
int game_render_load_track_textures(GameRenderer *renderer,
                                    uint8 *texture_vga, int gfx_size);
void game_render_free_track_textures(GameRenderer *renderer);

// Asset loading — car meshes
void game_render_load_car_mesh(GameRenderer *renderer, int carIdx,
                               const tColor *palette);
void game_render_free_car_mesh(GameRenderer *renderer, int carIdx);

// Asset loading — horizon
int game_render_load_horizon(GameRenderer *renderer, uint8 *horizon_data);
void game_render_free_horizon(GameRenderer *renderer);

// Asset loading — sprite blocks (HUD)
int game_render_load_blocks(GameRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette);
void game_render_free_blocks(GameRenderer *renderer, int slot);

// Draw — polygon (track, buildings, particles, clouds)
// tex_idx selects the texture bank (0=track, 17=building, 18=cargen, etc.)
// For POLYFLAT calls, pass texture_data=NULL, tex_idx=0, gfx_size=0.
void game_render_quad(GameRenderer *renderer,
                      tPolyParams *poly,
                      const uint8 *texture_data,
                      int tex_idx,
                      int gfx_size,
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

// Palette
void game_render_set_palette(GameRenderer *renderer, const tColor *palette);

// Fade
void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames);
int game_render_fade_active(GameRenderer *renderer);
void game_render_fade_wait(GameRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx);

#endif
