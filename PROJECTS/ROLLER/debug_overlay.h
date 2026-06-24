#ifndef _ROLLER_DEBUG_OVERLAY_H
#define _ROLLER_DEBUG_OVERLAY_H

#include <SDL3/SDL.h>

typedef struct DebugOverlay DebugOverlay;

/* Per-quad labels drawn on screen when surface debug viz is enabled. */
#define SURFACE_LABEL_MAX 512
typedef struct {
    float nx, ny;   /* normalised viewport position [0,1] */
    char  text[128];
} SurfaceDebugLabel;

void debug_overlay_surface_labels_reset(void);
void debug_overlay_surface_label_push(float nx, float ny, const char *text);

DebugOverlay *debug_overlay_create(SDL_GPUDevice *pDevice, SDL_Window *pWindow);
void          debug_overlay_destroy(DebugOverlay *pOverlay);
void debug_overlay_set_visible(DebugOverlay *pOverlay, bool bVisible);
bool debug_overlay_visible(DebugOverlay *pOverlay);
void debug_overlay_toggle(DebugOverlay *pOverlay);
bool debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent);

void debug_overlay_render(DebugOverlay *pOverlay,
                          SDL_GPUCommandBuffer *pCmdBuf,
                          SDL_GPUTexture *pSwapchainTex,
                          Uint32 uiSwapchainW, Uint32 uiSwapchainH);

#endif
