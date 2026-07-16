#include "present_sdlrenderer.h"
#include "roller.h"

//-------------------------------------------------------------------------------------------------

#define ROLLER_PRESENT_TEXTURE_WIDTH 640
#define ROLLER_PRESENT_TEXTURE_HEIGHT 400
#define ROLLER_PRESENT_ASPECT ((float)ROLLER_PRESENT_TEXTURE_WIDTH / \
                               (float)ROLLER_PRESENT_TEXTURE_HEIGHT)

//-------------------------------------------------------------------------------------------------

static SDL_Renderer *s_pRenderer = NULL;
static SDL_Texture *s_pTexture = NULL;

//-------------------------------------------------------------------------------------------------

static void ConvertIndexedToRGBA(const uint8 *pIndexed, const tColor *pPalette,
                                 uint8 *pRGBA, int iWidth, int iHeight)
{
  if (!pIndexed || !pPalette || !pRGBA)
    return;

  for (int i = 0; i < iWidth * iHeight; ++i) {
    const tColor *pColor = &pPalette[pIndexed[i]];
    pRGBA[i * 4 + 0] = (pColor->byR * 255) / 63;
    pRGBA[i * 4 + 1] = (pColor->byG * 255) / 63;
    pRGBA[i * 4 + 2] = (pColor->byB * 255) / 63;
    pRGBA[i * 4 + 3] = 255;
  }
}

//-------------------------------------------------------------------------------------------------

bool ROLLERPresentSDLRendererInit(SDL_Window *pWindow, bool bVsync)
{
  if (!pWindow)
    return false;

  s_pRenderer = SDL_CreateRenderer(pWindow, NULL);
  if (!s_pRenderer)
    return false;

  SDL_Log("SDL_Renderer driver: %s", SDL_GetRendererName(s_pRenderer));

  s_pTexture = SDL_CreateTexture(s_pRenderer, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 ROLLER_PRESENT_TEXTURE_WIDTH,
                                 ROLLER_PRESENT_TEXTURE_HEIGHT);
  if (!s_pTexture) {
    SDL_DestroyRenderer(s_pRenderer);
    s_pRenderer = NULL;
    return false;
  }

  if (!SDL_SetTextureScaleMode(s_pTexture, SDL_SCALEMODE_NEAREST))
    SDL_Log("SDL_SetTextureScaleMode failed: %s", SDL_GetError());

  ROLLERPresentSDLRendererSetVSync(bVsync);
  ROLLERPresentSDLRendererClear();
  return true;
}

//-------------------------------------------------------------------------------------------------

void ROLLERPresentSDLRendererShutdown(void)
{
  SDL_DestroyTexture(s_pTexture);
  s_pTexture = NULL;
  SDL_DestroyRenderer(s_pRenderer);
  s_pRenderer = NULL;
}

//-------------------------------------------------------------------------------------------------

void ROLLERPresentSDLRendererSetVSync(bool bVsync)
{
  if (s_pRenderer && !SDL_SetRenderVSync(s_pRenderer, bVsync ? 1 : 0))
    SDL_Log("SDL_SetRenderVSync failed: %s", SDL_GetError());
}

//-------------------------------------------------------------------------------------------------

void ROLLERPresentSDLRendererClear(void)
{
  if (!s_pRenderer)
    return;

  SDL_SetRenderDrawColor(s_pRenderer, 0, 0, 0, 255);
  SDL_RenderClear(s_pRenderer);
  SDL_RenderPresent(s_pRenderer);
}

//-------------------------------------------------------------------------------------------------

void ROLLERPresentSDLRendererFrame(const uint8 *pIndexed, const tColor *pPalette,
                                   int iWidth, int iHeight)
{
  SDL_Rect rSrcLockRect;
  SDL_FRect rSrcRect;
  SDL_FRect rDstRect;
  SDL_GPUViewport rViewport = { 0 };
  void *pPixels = NULL;
  int iPitch = 0;
  int iOutputW = 0;
  int iOutputH = 0;

  if (!s_pRenderer || !s_pTexture || !pIndexed || !pPalette)
    return;
  if (iWidth <= 0 || iHeight <= 0 ||
      iWidth > ROLLER_PRESENT_TEXTURE_WIDTH ||
      iHeight > ROLLER_PRESENT_TEXTURE_HEIGHT) {
    SDL_Log("SDL_Renderer present dimensions are invalid: %dx%d", iWidth, iHeight);
    return;
  }

  rSrcLockRect = (SDL_Rect){ 0, 0, iWidth, iHeight };
  if (!SDL_LockTexture(s_pTexture, &rSrcLockRect, &pPixels, &iPitch)) {
    SDL_Log("SDL_LockTexture failed: %s", SDL_GetError());
    return;
  }

  for (int iRow = 0; iRow < iHeight; ++iRow) {
    ConvertIndexedToRGBA(pIndexed + iRow * iWidth, pPalette,
                         (uint8 *)pPixels + iRow * iPitch, iWidth, 1);
  }
  SDL_UnlockTexture(s_pTexture);

  if (!SDL_GetCurrentRenderOutputSize(s_pRenderer, &iOutputW, &iOutputH) ||
      iOutputW <= 0 || iOutputH <= 0)
    return;

  ROLLERGetPresentViewport((Uint32)iOutputW, (Uint32)iOutputH,
                           ROLLER_PRESENT_ASPECT, &rViewport);
  rSrcRect = (SDL_FRect){ 0.0f, 0.0f, (float)iWidth, (float)iHeight };
  rDstRect = (SDL_FRect){ rViewport.x, rViewport.y, rViewport.w, rViewport.h };

  SDL_SetRenderDrawColor(s_pRenderer, 0, 0, 0, 255);
  SDL_RenderClear(s_pRenderer);
  if (!SDL_RenderTexture(s_pRenderer, s_pTexture, &rSrcRect, &rDstRect))
    SDL_Log("SDL_RenderTexture failed: %s", SDL_GetError());
  SDL_RenderPresent(s_pRenderer);
}

//-------------------------------------------------------------------------------------------------
