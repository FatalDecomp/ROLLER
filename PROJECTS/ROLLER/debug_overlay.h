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

/* Outline around the last surface picked via click-to-pick (g_bPickTextures).
 * nx/ny are the quad's 4 corners in normalised viewport space [0,1]; valid[i]
 * is false for corners that were behind the camera (that edge is skipped).
 * Persists (redrawn every frame) until the next pick call, active=false, or
 * a fresh pick with active=false on a miss clears it. */
void debug_overlay_pick_outline_set(bool active, const float nx[4], const float ny[4], const bool valid[4]);

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
