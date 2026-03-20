#include "game_render.h"
#include "game_render_software.h"

#include <stdlib.h>

struct GameRenderer {
    GameRenderMode mode;
    GameRendererSoftware *sw;
    SDL_GPUDevice *device;
    SDL_Window *window;
};

GameRenderer *game_render_create(SDL_GPUDevice *device, SDL_Window *window) {
    GameRenderer *r = calloc(1, sizeof(GameRenderer));
    r->device = device;
    r->window = window;
    r->sw = game_render_sw_create(device, window);
    r->mode = GAME_RENDER_SOFTWARE;
    return r;
}

void game_render_destroy(GameRenderer *renderer) {
    game_render_sw_destroy(renderer->sw);
    free(renderer);
}

void game_render_set_mode(GameRenderer *renderer, GameRenderMode mode) {
    (void)renderer;
    (void)mode;
    // Only software mode available currently
}

GameRenderMode game_render_get_mode(GameRenderer *renderer) {
    return renderer->mode;
}

// Frame lifecycle

void game_render_begin_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_frame(renderer->sw);
}

void game_render_end_frame(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_end_frame(renderer->sw);
}

// Viewport

void game_render_set_viewport(GameRenderer *renderer,
                              int x, int y, int w, int h) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_viewport(renderer->sw, x, y, w, h);
}

// Camera

void game_render_set_camera(GameRenderer *renderer,
                            int viewMode, int carIdx, int chaseCamIdx) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_camera(renderer->sw, viewMode, carIdx, chaseCamIdx);
}

// Asset loading

int game_render_load_track_textures(GameRenderer *renderer,
                                    uint8 *texture_vga, int gfx_size) {
    return game_render_sw_load_track_textures(renderer->sw,
                                              texture_vga, gfx_size);
}

void game_render_free_track_textures(GameRenderer *renderer) {
    game_render_sw_free_track_textures(renderer->sw);
}

void game_render_load_car_mesh(GameRenderer *renderer, int carIdx,
                               const tColor *palette) {
    game_render_sw_load_car_mesh(renderer->sw, carIdx, palette);
}

void game_render_free_car_mesh(GameRenderer *renderer, int carIdx) {
    game_render_sw_free_car_mesh(renderer->sw, carIdx);
}

int game_render_load_horizon(GameRenderer *renderer, uint8 *horizon_data) {
    return game_render_sw_load_horizon(renderer->sw, horizon_data);
}

void game_render_free_horizon(GameRenderer *renderer) {
    game_render_sw_free_horizon(renderer->sw);
}

int game_render_load_blocks(GameRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette) {
    return game_render_sw_load_blocks(renderer->sw, slot, blocks, palette);
}

void game_render_free_blocks(GameRenderer *renderer, int slot) {
    game_render_sw_free_blocks(renderer->sw, slot);
}

// Draw calls

void game_render_quad(GameRenderer *renderer, tPolyParams *poly,
                      const uint8 *texture_data, int tex_idx,
                      int gfx_size, const uint8 *palette_remap) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_quad(renderer->sw, poly, texture_data, tex_idx,
                            gfx_size, palette_remap);
}

void game_render_draw_car(GameRenderer *renderer, int carIdx,
                          int yaw, int pitch, int roll,
                          float worldX, float worldY, float worldZ,
                          int animFrame, const uint8 *color_remap) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_car(renderer->sw, carIdx, yaw, pitch, roll,
                                worldX, worldY, worldZ, animFrame,
                                color_remap);
}

void game_render_draw_horizon(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_draw_horizon(renderer->sw);
}

void game_render_sprite(GameRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_sprite(renderer->sw, slot, blockIdx, x, y,
                              transparentColorIndex, palette);
}

// Palette

void game_render_set_palette(GameRenderer *renderer, const tColor *palette) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_palette(renderer->sw, palette);
}

// Fade

void game_render_begin_fade(GameRenderer *renderer, int direction,
                            int durationFrames) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_begin_fade(renderer->sw, direction, durationFrames);
}

int game_render_fade_active(GameRenderer *renderer) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        return game_render_sw_fade_active(renderer->sw);
    return 0;
}

void game_render_fade_wait(GameRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_fade_wait(renderer->sw, redraw_fn, ctx);
}
