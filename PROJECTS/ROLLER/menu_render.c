#include "menu_render.h"
#include "menu_render_gpu.h"
#include "menu_render_software.h"
#include "3d.h"
#include "sound.h"

#include <stdlib.h>

#define MENU_RENDER_MAX_SLOTS 16

struct MenuRenderer {
    MenuRenderMode mode;
    MenuRenderMode pendingMode;
    MenuRendererGPU *gpu;
    MenuRendererSoftware *sw;
    SDL_GPUDevice *device;
    SDL_Window *window;
    tBlockHeader *cachedBlocks[MENU_RENDER_MAX_SLOTS];
    const tColor *cachedPalettes[MENU_RENDER_MAX_SLOTS];
    int gpuBlockLoaded[MENU_RENDER_MAX_SLOTS];
    int gpuBlockDirty[MENU_RENDER_MAX_SLOTS];
};

static int menu_render_wants_gpu_assets(MenuRenderer *renderer) {
    return renderer && renderer->gpu &&
           (renderer->mode == MENU_RENDER_GPU ||
            renderer->pendingMode == MENU_RENDER_GPU);
}

static int menu_render_upload_gpu_slot(MenuRenderer *renderer, int slot) {
    if (!renderer || !renderer->gpu || slot < 0 || slot >= MENU_RENDER_MAX_SLOTS)
        return 0;
    if (!renderer->cachedBlocks[slot] || !renderer->cachedPalettes[slot])
        return 0;

    int uploaded = menu_render_gpu_load_blocks(renderer->gpu, slot,
        renderer->cachedBlocks[slot], renderer->cachedPalettes[slot]);
    renderer->gpuBlockLoaded[slot] = uploaded != 0;
    renderer->gpuBlockDirty[slot] = 0;
    return uploaded;
}

static void menu_render_upload_dirty_gpu_blocks(MenuRenderer *renderer) {
    if (!renderer || !renderer->gpu)
        return;
    for (int i = 0; i < MENU_RENDER_MAX_SLOTS; i++) {
        if (renderer->gpuBlockDirty[i])
            menu_render_upload_gpu_slot(renderer, i);
    }
}

MenuRenderer *menu_render_create(SDL_GPUDevice *device, SDL_Window *window) {
    MenuRenderer *r = calloc(1, sizeof(MenuRenderer));
    r->device = device;
    r->window = window;
    r->sw = menu_render_sw_create(device, window);
    if (device && window) {
        r->gpu = menu_render_gpu_create(device, window);
    }
    r->mode = MENU_RENDER_SOFTWARE;
    r->pendingMode = MENU_RENDER_SOFTWARE;
    return r;
}

void menu_render_destroy(MenuRenderer *renderer) {
    if (!renderer) return;
    if (renderer->gpu)
        menu_render_gpu_destroy(renderer->gpu);
    menu_render_sw_destroy(renderer->sw);
    free(renderer);
}

void menu_render_set_mode(MenuRenderer *renderer, MenuRenderMode mode) {
    if (!renderer) return;
    if (mode == MENU_RENDER_GPU && !renderer->gpu)
        mode = MENU_RENDER_SOFTWARE;
    if (mode == MENU_RENDER_GPU && renderer->pendingMode != MENU_RENDER_GPU) {
        for (int i = 0; i < MENU_RENDER_MAX_SLOTS; i++) {
            if (renderer->cachedBlocks[i])
                renderer->gpuBlockDirty[i] = 1;
        }
    }
    renderer->pendingMode = mode;
}

MenuRenderMode menu_render_get_mode(MenuRenderer *renderer) {
    if (!renderer) return MENU_RENDER_SOFTWARE;
    return renderer->mode;
}

MenuRenderMode menu_render_get_pending_mode(MenuRenderer *renderer) {
    if (!renderer) return MENU_RENDER_SOFTWARE;
    return renderer->pendingMode;
}

int menu_render_load_blocks(MenuRenderer *renderer, int slot,
                            tBlockHeader *blocks, const tColor *palette) {
    if (!renderer) return 1;
    if (slot >= 0 && slot < MENU_RENDER_MAX_SLOTS) {
        renderer->cachedBlocks[slot] = blocks;
        renderer->cachedPalettes[slot] = palette;
        renderer->gpuBlockDirty[slot] = 1;
    }
    menu_render_sw_load_blocks(renderer->sw, slot, blocks, palette);
    if (menu_render_wants_gpu_assets(renderer) &&
        slot >= 0 && slot < MENU_RENDER_MAX_SLOTS)
        return menu_render_upload_gpu_slot(renderer, slot);
    return 0;
}

void menu_render_begin_frame(MenuRenderer *renderer) {
    if (!renderer) return;
    if (renderer->pendingMode == MENU_RENDER_GPU && !renderer->gpu)
        renderer->pendingMode = MENU_RENDER_SOFTWARE;

    if (renderer->pendingMode == MENU_RENDER_GPU)
        menu_render_upload_dirty_gpu_blocks(renderer);

    if (renderer->pendingMode != renderer->mode) {
        if (renderer->pendingMode == MENU_RENDER_SOFTWARE) {
            // GPU fade doesn't update pal_addr; restore from base palette
            for (int i = 0; i < 256; i++)
                pal_addr[i] = palette[i];
        }
        renderer->mode = renderer->pendingMode;
    }

    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_begin_frame(renderer->gpu);
    else
        menu_render_sw_begin_frame(renderer->sw);
}

void menu_render_end_frame(MenuRenderer *renderer) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_end_frame(renderer->gpu);
    else
        menu_render_sw_end_frame(renderer->sw);
}

void menu_render_set_layer(MenuRenderer *renderer, MenuDrawLayer layer) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_set_layer(renderer->gpu, layer);
}

void menu_render_background(MenuRenderer *renderer, int slot) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_background(renderer->gpu, slot);
    else
        menu_render_sw_background(renderer->sw, slot);
}

void menu_render_sprite(MenuRenderer *renderer, int slot, int blockIdx,
                        int x, int y, int transparentColorIndex,
                        const tColor *palette) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_sprite(renderer->gpu, slot, blockIdx, x, y,
                               transparentColorIndex, palette);
    else
        menu_render_sw_sprite(renderer->sw, slot, blockIdx, x, y,
                              transparentColorIndex, palette);
}

void menu_render_begin_fade(MenuRenderer *renderer, int direction,
                            int durationFrames) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu) {
        if (direction)
            fade_audio_restore();
        menu_render_gpu_begin_fade(renderer->gpu, direction, durationFrames);
    } else {
        menu_render_sw_begin_fade(renderer->sw, direction, durationFrames);
    }
}

int menu_render_fade_active(MenuRenderer *renderer) {
    if (!renderer) return 0;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        return menu_render_gpu_fade_active(renderer->gpu);
    else
        return menu_render_sw_fade_active(renderer->sw);
}

void menu_render_text(MenuRenderer *renderer, int fontSlot, const char *text,
                      const char *mappingTable, int *charVOffsets,
                      int x, int y, uint8 colorReplace, int alignment,
                      const tColor *palette) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_text(renderer->gpu, fontSlot, text, mappingTable,
                             charVOffsets, x, y, colorReplace, alignment,
                             palette);
    else
        menu_render_sw_text(renderer->sw, fontSlot, text, mappingTable,
                            charVOffsets, x, y, colorReplace, alignment,
                            palette);
}

void menu_render_scaled_text(MenuRenderer *renderer, int fontSlot,
                             const char *text, const char *mappingTable,
                             int *charVOffsets, int x, int y,
                             uint8 colorReplace, unsigned int alignment,
                             int clipLeft, int clipRight,
                             const tColor *palette) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_scaled_text(renderer->gpu, fontSlot, text, mappingTable,
                                    charVOffsets, x, y, colorReplace, alignment,
                                    clipLeft, clipRight, palette);
    else
        menu_render_sw_scaled_text(renderer->sw, fontSlot, text, mappingTable,
                                   charVOffsets, x, y, colorReplace, alignment,
                                   clipLeft, clipRight, palette);
}

void menu_render_load_car_mesh(MenuRenderer *renderer, int carIdx,
                               const tColor *palette) {
    if (!renderer) return;
    if (menu_render_wants_gpu_assets(renderer))
        menu_render_gpu_load_car_mesh(renderer->gpu, carIdx, palette);
    menu_render_sw_load_car_mesh(renderer->sw, carIdx, palette);
}

void menu_render_free_car_mesh(MenuRenderer *renderer) {
    if (!renderer) return;
    if (renderer->gpu)
        menu_render_gpu_free_car_mesh(renderer->gpu);
    menu_render_sw_free_car_mesh(renderer->sw);
}

void menu_render_draw_car_preview(MenuRenderer *renderer, float angle,
                                  float distance, int carYaw,
                                  int destX, int destY, int destW, int destH) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_draw_car_preview(renderer->gpu, angle, distance, carYaw,
                                         destX, destY, destW, destH);
    else
        menu_render_sw_draw_car_preview(renderer->sw, angle, distance, carYaw,
                                        destX, destY, destW, destH);
}

void menu_render_load_track_mesh(MenuRenderer *renderer, const tColor *palette) {
    if (!renderer) return;
    if (menu_render_wants_gpu_assets(renderer))
        menu_render_gpu_load_track_mesh(renderer->gpu, palette);
    menu_render_sw_load_track_mesh(renderer->sw, palette);
}

void menu_render_draw_track_preview(MenuRenderer *renderer, float cameraZ,
                                    int elevation, int yaw,
                                    int destX, int destY, int destW, int destH) {
    if (!renderer) return;
    if (renderer->mode == MENU_RENDER_GPU && renderer->gpu)
        menu_render_gpu_draw_track_preview(renderer->gpu, cameraZ, elevation, yaw,
                                           destX, destY, destW, destH);
    else
        menu_render_sw_draw_track_preview(renderer->sw, cameraZ, elevation, yaw,
                                          destX, destY, destW, destH);
}
