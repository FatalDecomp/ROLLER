#include "png_writer.h"
#include <SDL3/SDL.h>
#if !defined(IS_ANDROID)
#include <SDL3_image/SDL_image.h>
#endif

//-------------------------------------------------------------------------------------------------

int RollerWriteIndexedPng(const char *szPath,
                          const uint8 *pIndexedBuf,
                          const tColor *pPalette,
                          int iWidth,
                          int iHeight)
{
#if defined(IS_ANDROID)
  (void)szPath;
  (void)pIndexedBuf;
  (void)pPalette;
  (void)iWidth;
  (void)iHeight;
  return 1;
#else
  if (!szPath || !pIndexedBuf || !pPalette || iWidth <= 0 || iHeight <= 0)
    return 1;

  SDL_Surface *surface = SDL_CreateSurface(iWidth, iHeight, SDL_PIXELFORMAT_INDEX8);
  if (!surface)
    return 1;

  SDL_Palette *pal = SDL_CreateSurfacePalette(surface);
  if (!pal) {
    SDL_DestroySurface(surface);
    return 1;
  }

  SDL_Color colors[256];
  for (int i = 0; i < 256; ++i) {
    colors[i].r = (Uint8)((pPalette[i].byR * 255) / 63);
    colors[i].g = (Uint8)((pPalette[i].byG * 255) / 63);
    colors[i].b = (Uint8)((pPalette[i].byB * 255) / 63);
    colors[i].a = 255;
  }
  SDL_SetPaletteColors(pal, colors, 0, 256);

  // Copy indexed pixel data row by row, respecting surface pitch.
  const Uint8 *src = pIndexedBuf;
  Uint8 *dst = (Uint8 *)surface->pixels;
  for (int y = 0; y < iHeight; ++y) {
    SDL_memcpy(dst, src, (size_t)iWidth);
    src += iWidth;
    dst += surface->pitch;
  }

  bool bOk = IMG_SavePNG(surface, szPath);
  SDL_DestroySurface(surface);
  return bOk ? 0 : 1;
#endif
}

//-------------------------------------------------------------------------------------------------

int RollerWriteRgbaPng(const char *szPath,
                       const uint8 *pRgbaBuf,
                       int iWidth,
                       int iHeight)
{
#if defined(IS_ANDROID)
  (void)szPath;
  (void)pRgbaBuf;
  (void)iWidth;
  (void)iHeight;
  return 1;
#else
  if (!szPath || !pRgbaBuf || iWidth <= 0 || iHeight <= 0)
    return 1;

  SDL_Surface *surface = SDL_CreateSurface(iWidth, iHeight, SDL_PIXELFORMAT_RGBA32);
  if (!surface)
    return 1;

  const Uint8 *src = pRgbaBuf;
  Uint8 *dst = (Uint8 *)surface->pixels;
  for (int y = 0; y < iHeight; ++y) {
    SDL_memcpy(dst, src, (size_t)iWidth * 4);
    src += (size_t)iWidth * 4;
    dst += surface->pitch;
  }

  bool bOk = IMG_SavePNG(surface, szPath);
  SDL_DestroySurface(surface);
  return bOk ? 0 : 1;
#endif
}

//-------------------------------------------------------------------------------------------------
