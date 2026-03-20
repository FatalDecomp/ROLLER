#include "game_render_software.h"
#include "3d.h"
#include "func2.h"
#include "func3.h"
#include "view.h"
#include "horizon.h"
#include "car.h"
#include "polytex.h"
#include "roller.h"
#include "sound.h"

#include <stdlib.h>
#include <math.h>

#define GAME_RENDER_MAX_BLOCK_SLOTS 32

struct GameRendererSoftware {
    int fadeInPending;
    tBlockHeader *blocks[GAME_RENDER_MAX_BLOCK_SLOTS];
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

GameRendererSoftware *game_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window) {
    (void)device;
    (void)window;
    GameRendererSoftware *sw = calloc(1, sizeof(GameRendererSoftware));
    return sw;
}

void game_render_sw_destroy(GameRendererSoftware *sw) {
    free(sw);
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void game_render_sw_begin_frame(GameRendererSoftware *sw) {
    (void)sw;
}

void game_render_sw_end_frame(GameRendererSoftware *sw) {
    if (sw->fadeInPending) {
        sw->fadeInPending = 0;
        palette_brightness = 0;
        for (int i = 0; i < 256; i++) {
            pal_addr[i].byR = 0;
            pal_addr[i].byB = 0;
            pal_addr[i].byG = 0;
        }
        fade_palette(32);
        return;
    }
    g_bPaletteSet = true;
    UpdateSDLWindow();
}

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

void game_render_sw_set_viewport(GameRendererSoftware *sw,
                                 int x, int y, int w, int h) {
    (void)sw;
    (void)w;
    (void)h;
    // Set screen_pointer to the viewport origin within scrbuf.
    // Existing rendering code writes relative to screen_pointer.
    screen_pointer = scrbuf + (y * winw) + x;
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void game_render_sw_set_camera(GameRendererSoftware *sw,
                               int viewMode, int carIdx, int chaseCamIdx) {
    (void)sw;
    calculateview(viewMode, carIdx, chaseCamIdx);
}

// ---------------------------------------------------------------------------
// Asset loading — all no-ops for software (data lives in globals)
// ---------------------------------------------------------------------------

int game_render_sw_load_track_textures(GameRendererSoftware *sw,
                                       uint8 *texture_vga, int gfx_size) {
    (void)sw; (void)texture_vga; (void)gfx_size;
    return 0;
}

void game_render_sw_free_track_textures(GameRendererSoftware *sw) {
    (void)sw;
}

void game_render_sw_load_car_mesh(GameRendererSoftware *sw, int carIdx,
                                  const tColor *palette) {
    (void)sw; (void)carIdx; (void)palette;
}

void game_render_sw_free_car_mesh(GameRendererSoftware *sw, int carIdx) {
    (void)sw; (void)carIdx;
}

int game_render_sw_load_horizon(GameRendererSoftware *sw,
                                uint8 *horizon_data) {
    (void)sw; (void)horizon_data;
    return 0;
}

void game_render_sw_free_horizon(GameRendererSoftware *sw) {
    (void)sw;
}

int game_render_sw_load_blocks(GameRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette) {
    (void)palette;
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS)
        sw->blocks[slot] = blocks;
    return 0;
}

void game_render_sw_free_blocks(GameRendererSoftware *sw, int slot) {
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS)
        sw->blocks[slot] = NULL;
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

void game_render_sw_quad(GameRendererSoftware *sw, tPolyParams *poly,
                         const uint8 *texture_data, int tex_idx,
                         int gfx_size, const uint8 *palette_remap) {
    (void)sw;
    (void)palette_remap;
    if (texture_data) {
        POLYTEX((uint8 *)texture_data, screen_pointer, poly, tex_idx, gfx_size);
    } else {
        POLYFLAT(screen_pointer, poly);
    }
}

void game_render_sw_draw_car(GameRendererSoftware *sw, int carIdx,
                             int yaw, int pitch, int roll,
                             float worldX, float worldY, float worldZ,
                             int animFrame, const uint8 *color_remap) {
    (void)sw;
    (void)yaw; (void)pitch; (void)roll;
    (void)animFrame; (void)color_remap;
    // Compute distance from camera to car for LOD.
    // DisplayCar reads car state from global Car[] array.
    extern float viewx, viewy, viewz;
    float dx = worldX - viewx;
    float dy = worldY - viewy;
    float dz = worldZ - viewz;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    DisplayCar(carIdx, screen_pointer, dist);
}

void game_render_sw_draw_horizon(GameRendererSoftware *sw) {
    (void)sw;
    DrawHorizon(screen_pointer);
}

void game_render_sw_sprite(GameRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette) {
    (void)palette;
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS && sw->blocks[slot])
        display_block(scrbuf, sw->blocks[slot], blockIdx, x, y,
                      transparentColorIndex);
}

void game_render_sw_print_block(GameRendererSoftware *sw, int slot,
                                int blockIdx, uint8 *pDest) {
    if (slot >= 0 && slot < GAME_RENDER_MAX_BLOCK_SLOTS && sw->blocks[slot])
        print_block(pDest, sw->blocks[slot], blockIdx);
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

void game_render_sw_set_palette(GameRendererSoftware *sw,
                                const tColor *palette) {
    (void)sw;
    for (int i = 0; i < 256; i++)
        pal_addr[i] = palette[i];
}

// ---------------------------------------------------------------------------
// Fade
// ---------------------------------------------------------------------------

void game_render_sw_begin_fade(GameRendererSoftware *sw, int direction,
                               int durationFrames) {
    (void)durationFrames;
    if (direction) {
        sw->fadeInPending = 1;
    } else {
        fade_palette(0);
    }
}

int game_render_sw_fade_active(GameRendererSoftware *sw) {
    (void)sw;
    return 0; // blocking fade completes immediately
}

void game_render_sw_fade_wait(GameRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx) {
    (void)sw;
    (void)redraw_fn;
    (void)ctx;
}
