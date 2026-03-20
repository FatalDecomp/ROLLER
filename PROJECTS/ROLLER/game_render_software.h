#ifndef GAME_RENDER_SOFTWARE_H
#define GAME_RENDER_SOFTWARE_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"
#include "polyf.h"

typedef struct GameRendererSoftware GameRendererSoftware;

// Lifecycle
GameRendererSoftware *game_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window);
void game_render_sw_destroy(GameRendererSoftware *sw);

// Frame lifecycle
void game_render_sw_begin_frame(GameRendererSoftware *sw);
void game_render_sw_end_frame(GameRendererSoftware *sw);

// Viewport
void game_render_sw_set_viewport(GameRendererSoftware *sw,
                                 int x, int y, int w, int h);

// Camera
void game_render_sw_set_camera(GameRendererSoftware *sw,
                               int viewMode, int carIdx, int chaseCamIdx);

// Asset loading
int game_render_sw_load_track_textures(GameRendererSoftware *sw,
                                       uint8 *texture_vga, int gfx_size);
void game_render_sw_free_track_textures(GameRendererSoftware *sw);
void game_render_sw_load_car_mesh(GameRendererSoftware *sw, int carIdx,
                                  const tColor *palette);
void game_render_sw_free_car_mesh(GameRendererSoftware *sw, int carIdx);
int game_render_sw_load_horizon(GameRendererSoftware *sw,
                                uint8 *horizon_data);
void game_render_sw_free_horizon(GameRendererSoftware *sw);
int game_render_sw_load_blocks(GameRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette);
void game_render_sw_free_blocks(GameRendererSoftware *sw, int slot);

// Draw calls
void game_render_sw_quad(GameRendererSoftware *sw, tPolyParams *poly,
                         const uint8 *texture_data, int tex_idx,
                         int gfx_size, const uint8 *palette_remap);
void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             int yaw, int pitch, int roll,
                             float worldX, float worldY, float worldZ,
                             int animFrame, const uint8 *color_remap);
void game_render_sw_draw_horizon(GameRendererSoftware *sw);
void game_render_sw_sprite(GameRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette);

// Palette
void game_render_sw_set_palette(GameRendererSoftware *sw,
                                const tColor *palette);

// Fade
void game_render_sw_begin_fade(GameRendererSoftware *sw, int direction,
                               int durationFrames);
int game_render_sw_fade_active(GameRendererSoftware *sw);
void game_render_sw_fade_wait(GameRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx);

#endif
