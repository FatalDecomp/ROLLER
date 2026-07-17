#ifndef _ROLLER_NUKLEAR_SDL_RENDERER_H
#define _ROLLER_NUKLEAR_SDL_RENDERER_H

#include <SDL3/SDL.h>

struct nk_context;
struct nk_font_atlas;

typedef struct NuklearSDLRenderer NuklearSDLRenderer;

/* SDL3 adaptation of Nuklear's demo/sdl_renderer backend. The caller owns
 * the Nuklear context and font atlas so ROLLER can keep one shared overlay
 * implementation across the native SDL_GPU and browser SDL_Renderer paths. */
NuklearSDLRenderer *nuklear_sdl_renderer_create(SDL_Renderer *pRenderer);
bool nuklear_sdl_renderer_finish_font_atlas(NuklearSDLRenderer *pBackend,
                                            struct nk_font_atlas *pAtlas,
                                            const void *pPixels,
                                            int iWidth, int iHeight);
bool nuklear_sdl_renderer_render(NuklearSDLRenderer *pBackend,
                                 struct nk_context *pContext,
                                 int iLogicalWidth, int iLogicalHeight,
                                 int iOutputWidth, int iOutputHeight);
void nuklear_sdl_renderer_destroy(NuklearSDLRenderer *pBackend);

#endif
