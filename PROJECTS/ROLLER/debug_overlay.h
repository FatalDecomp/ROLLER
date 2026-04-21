#ifndef _ROLLER_DEBUG_OVERLAY_H
#define _ROLLER_DEBUG_OVERLAY_H

#include <SDL3/SDL.h>

typedef struct DebugOverlay DebugOverlay;

DebugOverlay *debug_overlay_create(SDL_GPUDevice *pDevice, SDL_Window *pWindow);
void          debug_overlay_destroy(DebugOverlay *pOverlay);

bool debug_overlay_is_visible(DebugOverlay *pOverlay);
void debug_overlay_toggle(DebugOverlay *pOverlay);
void debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent);

void debug_overlay_render(DebugOverlay *pOverlay,
                          SDL_GPUCommandBuffer *pCmdBuf,
                          SDL_GPUTexture *pSwapchainTex,
                          Uint32 uiSwapchainW, Uint32 uiSwapchainH);

#endif
