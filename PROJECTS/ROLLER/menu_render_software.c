#include "menu_render_software.h"
#include "3d.h"
#include "frontend.h"
#include "roller.h"
#include "sound.h"
#include <stdlib.h>
#include <string.h>

struct MenuRendererSoftware {
    int loadedCarIdx; // stored by _sw_load_car_mesh for DrawCar()
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MenuRendererSoftware *menu_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window) {
    (void)device;
    (void)window;
    MenuRendererSoftware *sw = calloc(1, sizeof(MenuRendererSoftware));
    return sw;
}

void menu_render_sw_destroy(MenuRendererSoftware *sw) {
    free(sw);
}

// ---------------------------------------------------------------------------
// Asset conversion (no-ops -- front_vga already loaded by existing code)
// ---------------------------------------------------------------------------

int menu_render_sw_load_blocks(MenuRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette) {
    (void)sw;
    (void)slot;
    (void)blocks;
    (void)palette;
    return 0;
}

void menu_render_sw_free_blocks(MenuRendererSoftware *sw, int slot) {
    (void)sw;
    (void)slot;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void menu_render_sw_begin_frame(MenuRendererSoftware *sw) {
    (void)sw;
    // No-op: scrbuf is always available
}

void menu_render_sw_end_frame(MenuRendererSoftware *sw) {
    (void)sw;
    g_bPaletteSet = true;
    UpdateSDLWindow();
}

// ---------------------------------------------------------------------------
// Draw calls
// ---------------------------------------------------------------------------

void menu_render_sw_clear(MenuRendererSoftware *sw, uint8 colorIndex,
                          const tColor *palette) {
    (void)sw;
    (void)palette;
    memset(scrbuf, colorIndex, winw * winh);
}

void menu_render_sw_background(MenuRendererSoftware *sw, int slot) {
    (void)sw;
    display_picture(scrbuf, front_vga[slot]);
}

void menu_render_sw_sprite(MenuRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette) {
    (void)sw;
    (void)palette;
    display_block(scrbuf, front_vga[slot], blockIdx, x, y,
                  transparentColorIndex);
}

// ---------------------------------------------------------------------------
// Fade system
// ---------------------------------------------------------------------------

void menu_render_sw_begin_fade(MenuRendererSoftware *sw, int direction,
                               int durationFrames) {
    (void)sw;
    (void)durationFrames;
    fade_palette(direction);
}

int menu_render_sw_fade_active(MenuRendererSoftware *sw) {
    (void)sw;
    return 0; // blocking fade completes immediately
}

void menu_render_sw_fade_wait(MenuRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx) {
    (void)sw;
    (void)redraw_fn;
    (void)ctx;
    // No-op: fade already done in begin_fade
}

// ---------------------------------------------------------------------------
// Text rendering
// ---------------------------------------------------------------------------

void menu_render_sw_text(MenuRendererSoftware *sw, int fontSlot,
                         const char *text, const char *mappingTable,
                         int *charVOffsets, int x, int y,
                         uint8 colorReplace, int alignment,
                         const tColor *palette) {
    (void)sw;
    (void)palette;
    front_text(front_vga[fontSlot], text, (const uint8 *)mappingTable,
               charVOffsets, x, y, colorReplace, alignment);
}

void menu_render_sw_scaled_text(MenuRendererSoftware *sw, int fontSlot,
                                const char *text, const char *mappingTable,
                                int *charVOffsets, int x, int y,
                                uint8 colorReplace, unsigned int alignment,
                                int clipLeft, int clipRight,
                                const tColor *palette) {
    (void)sw;
    (void)palette;
    scale_text(front_vga[fontSlot], (char *)text, mappingTable, charVOffsets,
               x, y, (char)colorReplace, alignment, clipLeft, clipRight);
}

// ---------------------------------------------------------------------------
// 3D mesh previews -- car
// ---------------------------------------------------------------------------

void menu_render_sw_load_car_mesh(MenuRendererSoftware *sw, int carIdx,
                                  const tColor *palette) {
    (void)palette;
    sw->loadedCarIdx = carIdx;
}

void menu_render_sw_free_car_mesh(MenuRendererSoftware *sw) {
    (void)sw;
    // No-op
}

void menu_render_sw_draw_car_preview(MenuRendererSoftware *sw, float angle,
                                     float distance, int carYaw,
                                     int destX, int destY,
                                     int destW, int destH) {
    (void)carYaw;
    (void)destW;
    (void)destH;
    xbase = destX;
    ybase = destY;
    DrawCar(scrbuf, sw->loadedCarIdx, distance, (int)angle, 0);
}

// ---------------------------------------------------------------------------
// 3D mesh previews -- track
// ---------------------------------------------------------------------------

void menu_render_sw_load_track_mesh(MenuRendererSoftware *sw,
                                    const tColor *palette) {
    (void)sw;
    (void)palette;
    // No-op
}

void menu_render_sw_free_track_mesh(MenuRendererSoftware *sw) {
    (void)sw;
    // No-op
}

void menu_render_sw_draw_track_preview(MenuRendererSoftware *sw, float cameraZ,
                                       int elevation, int yaw,
                                       int destX, int destY,
                                       int destW, int destH) {
    (void)sw;
    (void)destW;
    (void)destH;
    xbase = destX;
    ybase = destY;
    show_3dmap(cameraZ, elevation, yaw);
}
