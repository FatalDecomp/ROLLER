/*
 * Browser builds present through SDL_Renderer and do not create the SDL_GPU
 * device, pipelines, or command buffers required by debug_overlay.c. Keep the
 * public API available as inert operations until the optional SDL_Renderer
 * Nuklear backend is implemented.
 */
#include "debug_overlay.h"

void debug_overlay_surface_labels_reset(void)
{
}

void debug_overlay_surface_label_push(float nx, float ny, const char *text)
{
  (void)nx;
  (void)ny;
  (void)text;
}

void debug_overlay_pick_outline_set(bool active, const float nx[4],
                                    const float ny[4], const bool valid[4])
{
  (void)active;
  (void)nx;
  (void)ny;
  (void)valid;
}

DebugOverlay *debug_overlay_create(SDL_GPUDevice *pDevice, SDL_Window *pWindow)
{
  (void)pDevice;
  (void)pWindow;
  return NULL;
}

void debug_overlay_destroy(DebugOverlay *pOverlay)
{
  (void)pOverlay;
}

void debug_overlay_set_visible(DebugOverlay *pOverlay, bool bVisible)
{
  (void)pOverlay;
  (void)bVisible;
}

bool debug_overlay_visible(DebugOverlay *pOverlay)
{
  (void)pOverlay;
  return false;
}

void debug_overlay_toggle(DebugOverlay *pOverlay)
{
  (void)pOverlay;
}

bool debug_overlay_handle_event(DebugOverlay *pOverlay, SDL_Event *pEvent)
{
  (void)pOverlay;
  (void)pEvent;
  return false;
}

void debug_overlay_render(DebugOverlay *pOverlay,
                          SDL_GPUCommandBuffer *pCmdBuf,
                          SDL_GPUTexture *pSwapchainTex,
                          Uint32 uiSwapchainW, Uint32 uiSwapchainH)
{
  (void)pOverlay;
  (void)pCmdBuf;
  (void)pSwapchainTex;
  (void)uiSwapchainW;
  (void)uiSwapchainH;
}
