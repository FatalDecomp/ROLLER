#include "png_writer.h"
#include "lodepng.h"
#include <stdlib.h>

//-------------------------------------------------------------------------------------------------

int RollerWriteIndexedPng(const char *szPath,
                          const uint8 *pIndexedBuf,
                          const tColor *pPalette,
                          int iWidth,
                          int iHeight)
{
  if (!szPath || !pIndexedBuf || !pPalette || iWidth <= 0 || iHeight <= 0)
    return 1;

  LodePNGState state;
  lodepng_state_init(&state);
  state.info_raw.colortype = LCT_PALETTE;
  state.info_raw.bitdepth = 8;
  state.info_png.color.colortype = LCT_PALETTE;
  state.info_png.color.bitdepth = 8;
  state.encoder.auto_convert = 0;

  for (int i = 0; i < 256; ++i) {
    unsigned char r = (unsigned char)((pPalette[i].byR * 255) / 63);
    unsigned char g = (unsigned char)((pPalette[i].byG * 255) / 63);
    unsigned char b = (unsigned char)((pPalette[i].byB * 255) / 63);
    unsigned uErr = lodepng_palette_add(&state.info_png.color, r, g, b, 255);
    if (!uErr)
      uErr = lodepng_palette_add(&state.info_raw, r, g, b, 255);
    if (uErr) {
      lodepng_state_cleanup(&state);
      return (int)uErr;
    }
  }

  unsigned char *pOut = NULL;
  size_t uiOutSize = 0;
  unsigned uErr = lodepng_encode(&pOut, &uiOutSize, pIndexedBuf,
                                 (unsigned)iWidth, (unsigned)iHeight, &state);
  if (!uErr)
    uErr = lodepng_save_file(pOut, uiOutSize, szPath);
  free(pOut);
  lodepng_state_cleanup(&state);
  return (int)uErr;
}
