#ifndef _ROLLER_PRESENT_SDLRENDERER_H
#define _ROLLER_PRESENT_SDLRENDERER_H
//-------------------------------------------------------------------------------------------------

#include "types.h"
#include <SDL3/SDL.h>

//-------------------------------------------------------------------------------------------------

struct DebugOverlay;

bool ROLLERPresentSDLRendererInit(SDL_Window *pWindow, bool bVsync);
void ROLLERPresentSDLRendererShutdown(void);
void ROLLERPresentSDLRendererSetVSync(bool bVsync);
void ROLLERPresentSDLRendererClear(void);
SDL_Renderer *ROLLERPresentSDLRendererGetRenderer(void);
void ROLLERPresentSDLRendererOverlayOnly(struct DebugOverlay *pOverlay);
void ROLLERPresentSDLRendererFrame(const uint8 *pIndexed, const tColor *pPalette,
                                   int iWidth, int iHeight,
                                   struct DebugOverlay *pOverlay);

//-------------------------------------------------------------------------------------------------
#endif
