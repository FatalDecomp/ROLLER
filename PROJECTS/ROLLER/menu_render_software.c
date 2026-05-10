#include "menu_render_software.h"
#include "3d.h"
#include "frontend.h"
#include "roller.h"
#include "sound.h"
#include "carplans.h"
#include "car.h"
#include "graphics.h"
#include "scene_render.h"
#include <stdlib.h>
#include <string.h>

struct MenuRendererSoftware {
    SceneRenderer *scene;
    int loadedCarIdx; // stored by _sw_load_car_mesh for DrawCar()
    int fadeInPending; // deferred fade-in (so content is drawn before the fade)
};

// Legacy software preview origin: old code rendered at scrbuf + 34640,
// i.e. row 54, column 80 in the 640-wide frontend buffer. Keep the placement
// explicit at the scene seam instead of smuggling it as a destination pointer.
#define MENU_SW_CAR_PREVIEW_X 80
#define MENU_SW_CAR_PREVIEW_Y 54

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MenuRendererSoftware *menu_render_sw_create(SDL_GPUDevice *device,
                                            SDL_Window *window) {
    MenuRendererSoftware *sw = calloc(1, sizeof(MenuRendererSoftware));
    if (!sw)
        return NULL;
    sw->loadedCarIdx = -1;
    sw->scene = scene_render_create(device, window);
    return sw;
}

void menu_render_sw_destroy(MenuRendererSoftware *sw) {
    if (!sw)
        return;
    scene_render_destroy(sw->scene);
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
    if (sw->fadeInPending) {
        sw->fadeInPending = 0;
        // Content has been drawn to scrbuf; fade from black to full brightness.
        // palette_brightness may have been set to 32 by GPU init code, so
        // reset to 0 to ensure the fade actually animates.
        palette_brightness = 0;
        for (int i = 0; i < 256; i++) {
            pal_addr[i].byR = 0;
            pal_addr[i].byB = 0;
            pal_addr[i].byG = 0;
        }
        fade_palette(32); // blocking; calls UpdateSDLWindow each step
        return;
    }
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
    (void)durationFrames;
    if (direction) {
        // Fade-in: defer to end_frame so scrbuf has the new content drawn first
        sw->fadeInPending = 1;
    } else {
        fade_palette(0);
    }
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
    if (!sw->scene || carIdx < 0 || carIdx > CAR_DESIGN_DEATH)
        return;
    int texIdx = car_texmap[carIdx];
    if (texIdx > 0 && cartex_vga[texIdx - 1]) {
        scene_render_load_texture(sw->scene, cartex_vga[texIdx - 1],
                                  256, 0, texIdx, gfx_size);
    }
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
    (void)destX;
    (void)destY;
    if (sw->loadedCarIdx < 0 || sw->loadedCarIdx > CAR_DESIGN_DEATH) return;
    if (!CarDesigns[sw->loadedCarIdx].pCoords) return;
    scene_render_set_target(sw->scene, scrbuf, winw, winw, winh);
    scene_render_set_viewport(sw->scene, MENU_SW_CAR_PREVIEW_X,
                              MENU_SW_CAR_PREVIEW_Y, destW, destH);
    DrawCar(sw->scene, sw->loadedCarIdx, distance, (int)angle, 0);
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
    (void)destX;
    (void)destY;
    (void)destW;
    (void)destH;
    // Original code: show_3dmap uses xbase/ybase globals internally (set by init_screen)
    show_3dmap(cameraZ, elevation, yaw);
}
