#ifndef MENU_RENDER_H
#define MENU_RENDER_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"

typedef enum {
    MENU_RENDER_GPU,
    MENU_RENDER_SOFTWARE
} MenuRenderMode;

typedef struct MenuRenderer MenuRenderer;

// Lifecycle
MenuRenderer *menu_render_create(SDL_GPUDevice *device, SDL_Window *window);
void menu_render_destroy(MenuRenderer *renderer);

// Mode switching
void menu_render_set_mode(MenuRenderer *renderer, MenuRenderMode mode);
MenuRenderMode menu_render_get_mode(MenuRenderer *renderer);

// Asset conversion
int menu_render_load_blocks(MenuRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette);
void menu_render_free_blocks(MenuRenderer *renderer, int slot);

// Frame lifecycle
void menu_render_begin_frame(MenuRenderer *renderer);
void menu_render_end_frame(MenuRenderer *renderer);

// Draw calls (between begin_frame / end_frame)
void menu_render_clear(MenuRenderer *renderer, uint8 colorIndex,
                       const tColor *palette);
void menu_render_background(MenuRenderer *renderer, int slot);
void menu_render_sprite(MenuRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette);

// Fade system
void menu_render_begin_fade(MenuRenderer *renderer, int direction, int durationFrames);
int menu_render_fade_active(MenuRenderer *renderer);
void menu_render_fade_wait(MenuRenderer *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx);

// Text rendering
void menu_render_text(MenuRenderer *renderer, int fontSlot, const char *text,
                      const char *mappingTable, int *charVOffsets,
                      int x, int y, uint8 colorReplace, int alignment,
                      const tColor *palette);
void menu_render_scaled_text(MenuRenderer *renderer, int fontSlot,
                             const char *text, const char *mappingTable,
                             int *charVOffsets, int x, int y,
                             uint8 colorReplace, unsigned int alignment,
                             int clipLeft, int clipRight,
                             const tColor *palette);

// 3D mesh previews
void menu_render_load_car_mesh(MenuRenderer *renderer, int carIdx, const tColor *palette);
void menu_render_free_car_mesh(MenuRenderer *renderer);
void menu_render_draw_car_preview(MenuRenderer *renderer, float angle, float distance,
                                  int carYaw,
                                  int destX, int destY, int destW, int destH);

void menu_render_load_track_mesh(MenuRenderer *renderer, const tColor *palette);
void menu_render_free_track_mesh(MenuRenderer *renderer);
void menu_render_draw_track_preview(MenuRenderer *renderer, float cameraZ,
                                    int elevation, int yaw,
                                    int destX, int destY, int destW, int destH);

#endif
