#include "png_diff.h"

//-------------------------------------------------------------------------------------------------

static tColor s_diffPalette[256];
static int s_bDiffPaletteInit = 0;

//-------------------------------------------------------------------------------------------------

static void InitDiffPalette(void)
{
  if (s_bDiffPaletteInit) return;
  // Indices 0..254: linear grayscale gradient at 6-bit precision (the game's
  // palette layout). Index 254 maps to maximum brightness so we never collide
  // with the red entry.
  for (int i = 0; i < ROLLER_DIFF_RED_INDEX; ++i) {
    uint8 byVal = (uint8)((i * 63) / (ROLLER_DIFF_RED_INDEX - 1));
    s_diffPalette[i].byR = byVal;
    s_diffPalette[i].byG = byVal;
    s_diffPalette[i].byB = byVal;
  }
  // Index 255: bright red so mismatching pixels are unmistakable in any viewer.
  s_diffPalette[ROLLER_DIFF_RED_INDEX].byR = 63;
  s_diffPalette[ROLLER_DIFF_RED_INDEX].byG = 0;
  s_diffPalette[ROLLER_DIFF_RED_INDEX].byB = 0;
  s_bDiffPaletteInit = 1;
}

//-------------------------------------------------------------------------------------------------

const tColor *RollerGetDiffPalette(void)
{
  InitDiffPalette();
  return s_diffPalette;
}

//-------------------------------------------------------------------------------------------------

int RollerComputePngDiff(const uint8 *pExpected,
                         const uint8 *pActual,
                         const tColor *pExpectedPalette,
                         int iWidth,
                         int iHeight,
                         uint8 *pDiffOut)
{
  if (!pExpected || !pActual || !pExpectedPalette || !pDiffOut ||
      iWidth <= 0 || iHeight <= 0)
    return 0;

  InitDiffPalette();

  int iDifferCount = 0;
  int iTotal = iWidth * iHeight;
  for (int i = 0; i < iTotal; ++i) {
    if (pExpected[i] != pActual[i]) {
      pDiffOut[i] = (uint8)ROLLER_DIFF_RED_INDEX;
      iDifferCount++;
    } else {
      tColor c = pExpectedPalette[pExpected[i]];
      // 6-bit (0..63) brightness, scaled into [0..ROLLER_DIFF_RED_INDEX - 1].
      int iBright = ((int)c.byR + (int)c.byG + (int)c.byB) / 3;
      if (iBright > 63) iBright = 63;
      pDiffOut[i] = (uint8)((iBright * (ROLLER_DIFF_RED_INDEX - 1)) / 63);
    }
  }
  return iDifferCount;
}
