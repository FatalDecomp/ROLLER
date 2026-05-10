#ifndef _ROLLER_POLYTEX_H
#define _ROLLER_POLYTEX_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "polyf.h"
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

extern fixed16_16 startsx[4];
extern fixed16_16 startsy[4];
extern int texture_back[4608];
extern uint8 *scrptr;
extern uint8 *scrptr1;
extern uint8 *mapsel[4884];

//-------------------------------------------------------------------------------------------------

void remove_mapsels();
void setmapsel(uint8 *pBase, int iIndex, int iMode, int iCount);
void twpolym(tPoint *vertices, uint8 *pTex);
void POLYTEX(uint8 *pTexture, uint8 *pScrBuf, tPolyParams *pPolyParams,
             int iTexIdx, int iGfxSize);
void polym(tPoint *vertices, int iNumVerts, uint8 *pTex);
void polyt(tPoint *vertices, int iNumVerts, uint8 *pTex);

//-------------------------------------------------------------------------------------------------
#endif
