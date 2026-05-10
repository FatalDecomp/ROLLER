#ifndef _ROLLER_PNG_WRITER_H
#define _ROLLER_PNG_WRITER_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

// Writes an 8-bit indexed PNG with the supplied 256-entry palette in the PLTE
// chunk. The palette uses the game's 6-bit colour values (0..63), which are
// scaled to 8-bit. Returns 0 on success, non-zero on error.
int RollerWriteIndexedPng(const char *szPath, const uint8 *pIndexedBuf,
                          const tColor *pPalette, int iWidth, int iHeight);

//-------------------------------------------------------------------------------------------------
#endif
