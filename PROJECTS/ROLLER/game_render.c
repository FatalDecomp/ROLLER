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
                            const GameRenderCamera *camera) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_camera(renderer->sw, camera);
}

// Projection

void game_render_set_projection(GameRenderer *renderer,
                                const GameRenderProjection *proj) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_set_projection(renderer->sw, proj);
}

// Asset loading

TextureHandle game_render_load_texture(GameRenderer *renderer,
                                       uint8 *pixelData,
                                       int width, int height,
                                       int tex_idx, int gfx_size) {
    return game_render_sw_load_texture(renderer->sw, pixelData,
                                       width, height, tex_idx, gfx_size);
}

void game_render_free_texture(GameRenderer *renderer,
                              TextureHandle handle) {
    game_render_sw_free_texture(renderer->sw, handle);
}

TextureHandle game_render_get_texture_handle(GameRenderer *renderer,
                                             int tex_idx) {
    return game_render_sw_get_texture_handle(renderer->sw, tex_idx);
}

TextureHandle game_render_load_blocks(GameRenderer *renderer, int slot,
                                      tBlockHeader *blocks,
                                      const tColor *palette) {
    return game_render_sw_load_blocks(renderer->sw, slot, blocks, palette);
}

void game_render_free_blocks(GameRenderer *renderer, int slot) {
    game_render_sw_free_blocks(renderer->sw, slot);
}

// Draw calls

void game_render_quad_screen(GameRenderer *renderer, tPolyParams *poly,
                      TextureHandle handle,
                      const uint8 *palette_remap) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_quad_screen(renderer->sw, poly, handle, palette_remap);
}

void game_render_quad_world(GameRenderer *renderer,
                            const GameRenderVertex *verts,
                            TextureHandle handle,
                            int surfaceFlags,
                            float subThreshold) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_quad_world(renderer->sw, verts, handle, surfaceFlags,
                                  subThreshold);
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

void game_render_print_block(GameRenderer *renderer, int slot, int blockIdx,
                             uint8 *pDest) {
    if (renderer->mode == GAME_RENDER_SOFTWARE)
        game_render_sw_print_block(renderer->sw, slot, blockIdx, pDest);
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
