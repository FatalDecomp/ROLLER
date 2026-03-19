#ifndef MENU_RENDER_SOFTWARE_H
#define MENU_RENDER_SOFTWARE_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"

typedef struct MenuRendererSoftware MenuRendererSoftware;

// Lifecycle
MenuRendererSoftware *menu_render_sw_create(SDL_GPUDevice *device, SDL_Window *window);
void menu_render_sw_destroy(MenuRendererSoftware *sw);

// Asset conversion
int menu_render_sw_load_blocks(MenuRendererSoftware *sw, int slot,
                               tBlockHeader *blocks, const tColor *palette);
void menu_render_sw_free_blocks(MenuRendererSoftware *sw, int slot);

// Frame lifecycle
void menu_render_sw_begin_frame(MenuRendererSoftware *sw);
void menu_render_sw_end_frame(MenuRendererSoftware *sw);

// Draw calls (between begin_frame / end_frame)
void menu_render_sw_clear(MenuRendererSoftware *sw, uint8 colorIndex,
                          const tColor *palette);
void menu_render_sw_background(MenuRendererSoftware *sw, int slot);
void menu_render_sw_sprite(MenuRendererSoftware *sw, int slot, int blockIdx,
                           int x, int y, int transparentColorIndex,
                           const tColor *palette);

// Fade system
void menu_render_sw_begin_fade(MenuRendererSoftware *sw, int direction,
                               int durationFrames);
int menu_render_sw_fade_active(MenuRendererSoftware *sw);
void menu_render_sw_fade_wait(MenuRendererSoftware *sw,
                              void (*redraw_fn)(void *ctx), void *ctx);

// Text rendering
void menu_render_sw_text(MenuRendererSoftware *sw, int fontSlot,
                         const char *text, const char *mappingTable,
                         int *charVOffsets, int x, int y,
                         uint8 colorReplace, int alignment,
                         const tColor *palette);
void menu_render_sw_scaled_text(MenuRendererSoftware *sw, int fontSlot,
                                const char *text, const char *mappingTable,
                                int *charVOffsets, int x, int y,
                                uint8 colorReplace, unsigned int alignment,
                                int clipLeft, int clipRight,
                                const tColor *palette);

// 3D mesh previews
void menu_render_sw_load_car_mesh(MenuRendererSoftware *sw, int carIdx,
                                  const tColor *palette);
void menu_render_sw_free_car_mesh(MenuRendererSoftware *sw);
void menu_render_sw_draw_car_preview(MenuRendererSoftware *sw, float angle,
                                     float distance, int carYaw,
                                     int destX, int destY,
                                     int destW, int destH);

void menu_render_sw_load_track_mesh(MenuRendererSoftware *sw,
                                    const tColor *palette);
void menu_render_sw_free_track_mesh(MenuRendererSoftware *sw);
void menu_render_sw_draw_track_preview(MenuRendererSoftware *sw, float cameraZ,
                                       int elevation, int yaw,
                                       int destX, int destY,
                                       int destW, int destH);

#endif
