#ifndef MENU_RENDER_GPU_H
#define MENU_RENDER_GPU_H

#include <SDL3/SDL.h>
#include "types.h"
#include "func3.h"

typedef struct MenuRendererGPU MenuRendererGPU;

typedef struct {
    SDL_GPUTexture *texture;
    int width;
    int height;
} MenuTexture;

// Lifecycle
MenuRendererGPU *menu_render_gpu_create(SDL_GPUDevice *device, SDL_Window *window);
void menu_render_gpu_destroy(MenuRendererGPU *renderer);

// Asset conversion
int menu_render_gpu_load_blocks(MenuRendererGPU *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette);
void menu_render_gpu_free_blocks(MenuRendererGPU *renderer, int slot);

// Frame lifecycle
void menu_render_gpu_begin_frame(MenuRendererGPU *renderer);
void menu_render_gpu_end_frame(MenuRendererGPU *renderer);

// Draw calls (between begin_frame / end_frame)
void menu_render_gpu_clear(MenuRendererGPU *renderer, uint8 colorIndex,
                       const tColor *palette);
void menu_render_gpu_background(MenuRendererGPU *renderer, int slot);
void menu_render_gpu_sprite(MenuRendererGPU *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette);

// Fade system
void menu_render_gpu_begin_fade(MenuRendererGPU *renderer, int direction, int durationFrames);
int menu_render_gpu_fade_active(MenuRendererGPU *renderer);
void menu_render_gpu_fade_wait(MenuRendererGPU *renderer,
                           void (*redraw_fn)(void *ctx), void *ctx);

// Text rendering
void menu_render_gpu_text(MenuRendererGPU *renderer, int fontSlot, const char *text,
                      const char *mappingTable, int *charVOffsets,
                      int x, int y, uint8 colorReplace, int alignment,
                      const tColor *palette);
void menu_render_gpu_scaled_text(MenuRendererGPU *renderer, int fontSlot,
                             const char *text, const char *mappingTable,
                             int *charVOffsets, int x, int y,
                             uint8 colorReplace, unsigned int alignment,
                             int clipLeft, int clipRight,
                             const tColor *palette);

// 3D mesh previews
void menu_render_gpu_load_car_mesh(MenuRendererGPU *renderer, int carIdx, const tColor *palette);
void menu_render_gpu_free_car_mesh(MenuRendererGPU *renderer);
void menu_render_gpu_draw_car_preview(MenuRendererGPU *renderer, float angle, float distance,
                                  int carYaw,
                                  int destX, int destY, int destW, int destH);

void menu_render_gpu_load_track_mesh(MenuRendererGPU *renderer, const tColor *palette);
void menu_render_gpu_free_track_mesh(MenuRendererGPU *renderer);
void menu_render_gpu_draw_track_preview(MenuRendererGPU *renderer, float cameraZ,
                                    int elevation, int yaw,
                                    int destX, int destY, int destW, int destH);

#endif
