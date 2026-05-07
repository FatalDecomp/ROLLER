#ifndef _ROLLER_PNG_DIFF_H
#define _ROLLER_PNG_DIFF_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

// Index of the "differing pixel" colour in the diff palette returned by
// RollerGetDiffPalette(). Indices [0..ROLLER_DIFF_RED_INDEX-1] are reserved
// for the desaturated-expected gradient.
#define ROLLER_DIFF_RED_INDEX 255

//-------------------------------------------------------------------------------------------------

// Pure function. For every pixel where pExpected[i] != pActual[i], writes
// ROLLER_DIFF_RED_INDEX into pDiffOut[i]. For matching pixels, writes a
// desaturated grayscale index derived from the brightness of the expected
// pixel under pExpectedPalette. The output buffer must be decoded with the
// palette returned by RollerGetDiffPalette() to render correctly.
//
// All buffers must be at least iWidth * iHeight bytes. Returns the number of
// differing pixels.
int RollerComputePngDiff(const uint8 *pExpected,
                         const uint8 *pActual,
                         const tColor *pExpectedPalette,
                         int iWidth,
                         int iHeight,
                         uint8 *pDiffOut);

// Returns a pointer to a static 256-entry palette: indices [0..254] form a
// linear grayscale gradient, index 255 is solid red. Safe to call repeatedly.
const tColor *RollerGetDiffPalette(void);

//-------------------------------------------------------------------------------------------------
#endif
