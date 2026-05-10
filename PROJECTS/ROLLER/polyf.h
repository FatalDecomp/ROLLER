#ifndef _ROLLER_POLYF_H
#define _ROLLER_POLYF_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "frontend.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  int iSurfaceType;
  uint32 uiNumVerts;
  tPoint vertices[4];
} tPolyParams;

//-------------------------------------------------------------------------------------------------

void twpoly(tPoint *vertices, int16 nColor);
void POLYFLAT(uint8 *pScrBuf, tPolyParams *polyParams);
void poly(tPoint *vertices, int iNumVerts, int16 nColor);
void shadow_poly(tPoint *vertices, int iNumVerts, int iPaletteIndex);

//-------------------------------------------------------------------------------------------------
#endif
