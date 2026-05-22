#include "polytex.h"
#include "graphics.h"
#include "3d.h"
#include "drawtrk3.h"
#include "roller.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

typedef uint32 _DWORD;
//-------------------------------------------------------------------------------------------------

fixed16_16 startsx[4] = { 0x3FF000, 0x0, 0x0, 0x3FF000 }; //000A7474 0x3FF000 = 64.0 in 16.16 fixed point
fixed16_16 startsy[4] = { 0x0, 0x0, 0x3FF000, 0x3FF000 }; //000A7484 0x3FF000 = 64.0 in 16.16 fixed point
//fixed16_16 nTexU = 0;     //000A7498
//fixed16_16 nTexV = 0;     //000A749C
//fixed16_16 nTexDuDx = 0;  //000A74A0
//fixed16_16 nTexDvDx = 0;  //000A74A4
int texture_back[4608];   //0019A420
uint8 *scrptr;            //0019EC20
uint8 *scrptr1;           //0019EC24
uint8 *mapsel[4884];      //0019EC28 changed to uint8* by ROLLER, original code these are 16-bit DOS selectors, [19][257]

//-------------------------------------------------------------------------------------------------
//0006BBF0
void remove_mapsels()
{
  // Clear all mapsel arrays
  memset(mapsel, 0, sizeof(mapsel));
  // Clear texture counts
  memset(num_textures, 0, sizeof(num_textures));

  //int iCarIdx; // ebp
  //int iMapselOffset; // ecx
  //int iCarDataOffset; // edi
  //int iTextureIdx; // esi
  //int iNextCarDataOffset; // esi
  //int iNextMapselOffset; // eax
  //int iTrackTexIdx; // esi
  //int iMapselOffset_1; // ecx
  //int iBldTexIdx; // esi
  //int iBuildingMapselIdx; // ecx
  //int iCarGenTexIdx; // esi
  //int iCarGenMapselIdx; // ecx
  //union REGS regs; // [esp+0h] [ebp-3Ch] BYREF
  //int iCarIdxCounter; // [esp+1Ch] [ebp-20h]
  //int iCarMapselOffset; // [esp+20h] [ebp-1Ch]
  //
  //iCarIdxCounter = 1;
  //
  //// Process car textures
  //if (LoadCarTextures > 1) {
  //  iCarIdx = 1;
  //  iCarMapselOffset = 514;                     // starting offset: 514 = 257*2 (skip types 0 and 1)
  //  do {
  //    iMapselOffset = iCarMapselOffset;
  //    iCarDataOffset = iCarIdx * 4;             // 4 bytes per car entry
  //
  //    // Process all textures for this car
  //    for (iTextureIdx = 0; iTextureIdx < bld_remap[iCarIdx + 255]; iMapselOffset += 2)// reference into num_textures
  //    {
  //      // Check if selector is allocated
  //      if (*(int16 *)((char *)mapsel[0] + iMapselOffset)) {
  //        // Free the selector
  //        regs.w.bx = *(int16 *)((char *)mapsel[0] + iMapselOffset);
  //        regs.w.ax = 1;                        // DPMI function: free LDT descriptor
  //        int386(0x31, &regs, &regs);           // DPMI services
  //        // Clear the selector entry
  //        *(int16 *)((char *)mapsel[0] + iMapselOffset) = 0;
  //      }
  //      ++iTextureIdx;
  //    }
  //
  //    iNextCarDataOffset = iCarMapselOffset;
  //    iNextMapselOffset = LoadCarTextures;
  //    ++iCarIdx;
  //
  //    // Clear tex count for this car
  //    *(int *)((char *)&bld_remap[255] + iCarDataOffset) = 0;// reference into num_textures
  //
  //    // Move to next car's mapsel area
  //    iCarMapselOffset = iNextCarDataOffset + 514;
  //    ++iCarIdxCounter;
  //  } while (iCarIdxCounter < iNextMapselOffset);
  //}
  //
  //// Process track textures
  //iTrackTexIdx = 0;
  //if (num_textures[19] > 0) {
  //  iMapselOffset_1 = 0;
  //  do {
  //    // Check if selector is allocated
  //    if (mapsel[0][iMapselOffset_1]) {
  //      // Free the selector
  //      regs.w.bx = mapsel[0][iMapselOffset_1];
  //      regs.w.ax = 1;
  //      int386(0x31, &regs, &regs);
  //
  //      // Clear the selector entry
  //      mapsel[0][iMapselOffset_1] = 0;
  //    }
  //    ++iTrackTexIdx;
  //    ++iMapselOffset_1;
  //  } while (iTrackTexIdx < num_textures[19]);
  //}
  //iBldTexIdx = 0;
  //num_textures[19] = 0;                         // clear track tex count
  //
  //// Process bld textures
  //if (num_textures[17] > 0) {
  //  iBuildingMapselIdx = 0;
  //  do {
  //    // Check if selector is allocated
  //    if (mapsel[17][iBuildingMapselIdx]) {
  //      // free the selector
  //      regs.w.bx = mapsel[17][iBuildingMapselIdx];
  //      regs.w.ax = 1;
  //      int386(0x31, &regs, &regs);
  //
  //      // Clear the selector entry
  //      mapsel[17][iBuildingMapselIdx] = 0;
  //    }
  //    ++iBldTexIdx;
  //    ++iBuildingMapselIdx;
  //  } while (iBldTexIdx < num_textures[17]);
  //}
  //iCarGenTexIdx = 0;
  //num_textures[17] = 0;                         // clear building tex count
  //
  //// process car gen textures
  //if (num_textures[18] > 0) {
  //  iCarGenMapselIdx = 0;
  //  do {
  //    if (mapsel[18][iCarGenMapselIdx]) {
  //      // Free the selector
  //      regs.w.bx = mapsel[18][iCarGenMapselIdx];
  //      regs.w.ax = 1;
  //      int386(0x31, &regs, &regs);
  //
  //      // Clear the selector entry
  //      mapsel[18][iCarGenMapselIdx] = 0;
  //    }
  //    ++iCarGenTexIdx;
  //    ++iCarGenMapselIdx;
  //  } while (iCarGenTexIdx < num_textures[18]);
  //}
  //
  //// Clear car gen tex count
  //num_textures[18] = 0;
}

//-------------------------------------------------------------------------------------------------
//0006BDB0
void setmapsel(uint8_t *pBase, int iIndex, int iMode, int iCount)
{
// Calculate base offset in mapsel array for this texture index
// Original used 257 * iIndex, but since we changed from int16 to uint8*, 
// and POLYTEX uses 514 * iTexIdx, we need to adjust
  int iBaseMapselIndex = 257 * iIndex;

  // Set up pointers for each texture
  for (int i = 0; i < iCount; i++) {
    uint8_t *pTexturePointer;

    if (iMode == 1) {
        // Mode 1: 8-segment grouping (32x32 textures)
        // Original: iCoarseOffset = (i >> 3 << 13) + iBaseAdjusted;
        //          iFineOffset = 32 * (i & 7);
      int iCoarseOffset = (i >> 3) << 13;  // (i / 8) * 8192
      int iFineOffset = 32 * (i & 7);      // 32 * (i % 8)
      pTexturePointer = pBase + iCoarseOffset + iFineOffset;
    } else {
        // Mode 0: 4-segment grouping (64x64 textures) - interleaved layout
        // Original: iCoarseOffset = (i & 3) << 6;
        //          iFineOffset = iBaseAdjusted + (i >> 2 << 14);
      int iCoarseOffset = (i & 3) << 6;    // (i % 4) * 64
      int iFineOffset = (i >> 2) << 14;    // (i / 4) * 16384
      pTexturePointer = pBase + iFineOffset + iCoarseOffset;
    }

// Store pointer in mapsel array
    mapsel[iBaseMapselIndex + i] = pTexturePointer;
  }
}

//-------------------------------------------------------------------------------------------------
//0006BF50
void twpolym(tPoint *vertices, uint8 *pTex)
{
  int16 nEdge01X; // bx
  int16 nEdge01Y; // si
  int16 nEdge02Y; // di
  int iOgStartsY2_1; // ebx
  double dIntersectionX1_1; // st6
  double dIntersectionY1_1; // st6
  tPoint *pCurrVert; // edi
  double dIntersectionX1; // st6
  double dIntersectionY1; // st6
  int iCrossProduct2_1; // ebp
  double dIntersectionY1_3; // st6
  tPoint *pNextVert; // edi
  double dIntersectionY1_2; // st6
  int iSwappedStartsX; // eax
  int iSwappedStartsY; // ebp
  int iSwappedStartsX_1; // esi
  tPoint pSavedVert; // [esp+0h] [ebp-9Ch]
  tPoint pVert1Copy; // [esp+8h] [ebp-94h]
  tPoint pIntersectionVert_1; // [esp+10h] [ebp-8Ch]
  tPoint pIntersectionVert; // [esp+10h] [ebp-8Ch]
  int iDeltaTexY23; // [esp+1Ch] [ebp-80h]
  int iDeltaTexX23; // [esp+20h] [ebp-7Ch]
  float fCrossProduct2; // [esp+24h] [ebp-78h]
  float fCrossProduct1; // [esp+28h] [ebp-74h]
  int iOgStartsX1_1; // [esp+2Ch] [ebp-70h]
  int iOgStartsY1_1; // [esp+30h] [ebp-6Ch]
  int iOgStartsX0_1; // [esp+34h] [ebp-68h]
  int iOgStartsX1; // [esp+38h] [ebp-64h]
  int iOgStartsY0_1; // [esp+3Ch] [ebp-60h]
  int iOgStartsY1; // [esp+40h] [ebp-5Ch]
  int iOgStartsY0; // [esp+44h] [ebp-58h]
  int iOgStartsX0; // [esp+48h] [ebp-54h]
  int iCrossProduct2; // [esp+4Ch] [ebp-50h]
  int iCrossProduct1; // [esp+50h] [ebp-4Ch]
  int iOgStartsY2; // [esp+54h] [ebp-48h]
  int iOgStartsX2; // [esp+58h] [ebp-44h]
  int iOgStartsY2_2; // [esp+5Ch] [ebp-40h]
  int iOgStartsX2_1; // [esp+60h] [ebp-3Ch]
  float fIntersection; // [esp+64h] [ebp-38h]
  float fIntersection_1; // [esp+68h] [ebp-34h]
  int16 nEdge23X; // [esp+6Ch] [ebp-30h]
  int16 nEdge23Y; // [esp+70h] [ebp-2Ch]
  int16 nEdge02X; // [esp+74h] [ebp-28h]
  int16 nDiagonal13X; // [esp+78h] [ebp-24h]
  int16 nDiagonal03Y; // [esp+7Ch] [ebp-20h]
  int16 nDiagonal13Y; // [esp+80h] [ebp-1Ch]
  int16 nDiagonal03X; // [esp+84h] [ebp-18h]

  // Calcualte edge vectors for intersection testing
  nEdge01X = GET_LOWORD(vertices[1].x) - GET_LOWORD(vertices->x);
  nEdge02X = GET_LOWORD(vertices[2].x) - GET_LOWORD(vertices->x);
  nEdge23X = GET_LOWORD(vertices[3].x) - GET_LOWORD(vertices[2].x);
  nDiagonal03X = nEdge02X + nEdge23X;
  nDiagonal13X = nEdge01X - nEdge02X;

  nEdge01Y = GET_LOWORD(vertices[1].y) - GET_LOWORD(vertices->y);
  nEdge02Y = GET_LOWORD(vertices[2].y) - GET_LOWORD(vertices->y);
  iCrossProduct1 = 0;
  nEdge23Y = GET_LOWORD(vertices[3].y) - GET_LOWORD(vertices[2].y);
  iCrossProduct2 = 0;
  nDiagonal03Y = nEdge02Y + nEdge23Y;
  nDiagonal13Y = nEdge01Y - nEdge02Y;

  // Test for intersection between edgse 0-1 and 2-3
  fCrossProduct1 = (float)(nEdge23Y * nEdge01X - nEdge01Y * nEdge23X);

  if (fabs(fCrossProduct1) != 0.0// non-parallel edges
    && (iCrossProduct1 = nEdge02X * nEdge01Y - nEdge02Y * nEdge01X,
        fIntersection = (float)((double)iCrossProduct1 / (double)fCrossProduct1),
        fIntersection > 0.0)
    && fIntersection < 1.0)                    // valid intersection exists
  {
    // Save original texcoords
    iOgStartsX0 = startsx[0];
    iOgStartsY0 = startsy[0];
    iOgStartsX1 = startsx[1];
    iOgStartsY1 = startsy[1];
    iOgStartsX2 = startsx[2];
    iOgStartsY2 = startsy[2];
    iOgStartsY2_1 = startsy[2];

    pVert1Copy = *vertices;
    iDeltaTexY23 = startsy[3] - startsy[2];
    iDeltaTexX23 = startsx[3] - startsx[2];

    // Intersection from one side
    if (iCrossProduct1 <= 0) {
      // Reorder verts: start from vert 2
      *vertices = vertices[2];

      // Calcualte intersection point on edge 2-3
      dIntersectionX1 = (double)nEdge23X * fIntersection + (double)vertices[2].x;
      //_CHP();
      vertices[2].x = (int)dIntersectionX1;
      dIntersectionY1 = (double)nEdge23Y * fIntersection + (double)vertices[2].y;
      //_CHP();
      vertices[2].y = (int)dIntersectionY1;

      // Interpolate tex coords at intersection
      //_CHP();
      startsx[2] = (int)((double)iDeltaTexX23 * fIntersection + (double)iOgStartsX2);
      startsy[0] = iOgStartsY2_1;
      startsx[1] = iOgStartsX1;
      //_CHP();
      startsx[0] = iOgStartsX2;
      startsy[1] = iOgStartsY1;
      startsy[2] = (int)(fIntersection * (double)iDeltaTexY23 + (double)iOgStartsY2);

      // Render first tri
      polym(vertices, 3, pTex);

      startsx[1] = startsx[3];
      *vertices = vertices[3];
      startsx[0] = iOgStartsX0;
      startsy[1] = startsy[3];
      pCurrVert = vertices + 1;
      startsy[0] = iOgStartsY0;
    } else                                        // intersection from other side
    {
      // Reorder verts: start from vert 1
      *vertices = vertices[1];
      vertices[1] = vertices[2];

      // Calculate intersection point
      dIntersectionX1_1 = (double)nEdge23X * fIntersection + (double)vertices[2].x;
      //_CHP();
      vertices[2].x = (int)dIntersectionX1_1;
      dIntersectionY1_1 = (double)nEdge23Y * fIntersection + (double)vertices[2].y;

      // Interpolate tex coords
      //_CHP();
      vertices[2].y = (int)dIntersectionY1_1;
      //_CHP();
      startsx[2] = (int)((double)iDeltaTexX23 * fIntersection + (double)iOgStartsX2);
      startsy[1] = iOgStartsY2_1;
      startsx[0] = iOgStartsX1;
      //_CHP();
      startsx[1] = iOgStartsX2;
      startsy[0] = iOgStartsY1;
      startsy[2] = (int)(fIntersection * (double)iDeltaTexY23 + (double)iOgStartsY2);

      // Render first tri
      polym(vertices, 3, pTex);

      startsx[0] = startsx[3];
      *vertices = vertices[3];
      startsx[1] = iOgStartsX0;
      startsy[0] = startsy[3];
      pCurrVert = vertices + 1;
      startsy[1] = iOgStartsY0;
    }

    // Render second tri
    *pCurrVert = pVert1Copy;
    polym(vertices, 3, pTex);

    // Restore texcoords
    startsx[0] = iOgStartsX0;
    startsy[0] = iOgStartsY0;
    startsx[1] = iOgStartsX1;
    startsy[1] = iOgStartsY1;
    startsx[2] = iOgStartsX2;
    startsy[2] = iOgStartsY2;
  } else {
    // Test for intersection between edges 1-3 and 0-2
    fCrossProduct2 = (float)(nDiagonal13Y * nDiagonal03X - nDiagonal03Y * nDiagonal13X);
    if (fabs(fCrossProduct2) != 0.0// non-parallel
      && (iCrossProduct2_1 = nEdge02X * nDiagonal03Y - nDiagonal03X * nEdge02Y,
          iCrossProduct2 = iCrossProduct2_1,
          fIntersection_1 = (float)((double)iCrossProduct2_1 / (double)fCrossProduct2),
          fIntersection_1 > 0.0)
      && fIntersection_1 < 1.0)                // valid intersection
    {
      // Save original tex coords
      iOgStartsX0_1 = startsx[0];
      iOgStartsY0_1 = startsy[0];
      iOgStartsX1_1 = startsx[1];
      iOgStartsY1_1 = startsy[1];
      iOgStartsX2_1 = startsx[2];
      iOgStartsY2_2 = startsy[2];

      // Intersection from one side
      if (iCrossProduct2_1 <= 0) {
        // Swap verts 0 and 1
        pIntersectionVert = *vertices;
        *vertices = vertices[1];
        vertices[1] = pIntersectionVert;

        // Calculate intersection point
        pIntersectionVert_1 = vertices[2];
        //_CHP();
        vertices[2].x = (int)((double)nDiagonal13X * fIntersection_1 + (double)pIntersectionVert_1.x);
        dIntersectionY1_2 = (double)nDiagonal13Y * fIntersection_1 + (double)vertices[2].y;
        //_CHP();

        // Setup tex coords
        startsx[1] = iOgStartsX0_1;
        startsx[0] = iOgStartsX1_1;
        vertices[2].y = (int)dIntersectionY1_2;
        startsy[1] = iOgStartsY0_1;
        startsy[0] = iOgStartsY1_1;

        // Interpolate tex coords at intersection
        //_CHP();
        startsx[2] = (int)((double)(iOgStartsX0_1 - iOgStartsX2_1) * fIntersection_1 + (double)iOgStartsX2_1);
        //_CHP();
        startsy[2] = (int)(fIntersection_1 * (double)(iOgStartsY0_1 - iOgStartsY2_2) + (double)iOgStartsY2_2);

        // Render first tri
        polym(vertices, 3, pTex);

        startsx[1] = startsx[3];
        vertices[1] = vertices[3];
        startsx[0] = iOgStartsX2_1;
        startsy[1] = startsy[3];
        pNextVert = vertices;
        startsy[0] = iOgStartsY2_2;
      } else                                      // intersection from other side
      {
        pIntersectionVert_1 = vertices[2];
        //_CHP();
        vertices[2].x = (int)((double)nDiagonal13X * fIntersection_1 + (double)pIntersectionVert_1.x);
        dIntersectionY1_3 = (double)nDiagonal13Y * fIntersection_1 + (double)vertices[2].y;
        //_CHP();
        vertices[2].y = (int)dIntersectionY1_3;

        // Interpolate tex coords
        //_CHP();
        startsx[2] = (int)((double)(iOgStartsX1_1 - iOgStartsX2_1) * fIntersection_1 + (double)iOgStartsX2_1);
        //_CHP();
        startsy[2] = (int)(fIntersection_1 * (double)(iOgStartsY1_1 - iOgStartsY2_2) + (double)iOgStartsY2_2);

        // Render first tri
        polym(vertices, 3, pTex);

        startsx[0] = startsx[3];
        *vertices = vertices[3];
        startsx[1] = iOgStartsX2_1;
        startsy[0] = startsy[3];
        pNextVert = vertices + 1;
        startsy[1] = iOgStartsY2_2;
      }
      *pNextVert = pIntersectionVert_1;
      polym(vertices, 3, pTex);
      startsx[0] = iOgStartsX0_1;
      startsy[0] = iOgStartsY0_1;
      startsx[1] = iOgStartsX1_1;
      startsy[1] = iOgStartsY1_1;
      startsx[2] = iOgStartsX2_1;
      startsy[2] = iOgStartsY2_2;
    } else if (iCrossProduct1 >= 0 && iCrossProduct2 <= 0)// convex quad
    {
      // Render as single quad
      polym(vertices, 4, pTex);
    } else                                        // concave - need to swap diagonal
    {
      // Sawp verts 1 and 3 to change triangulation
      pSavedVert = vertices[1];
      iSwappedStartsX = startsx[3];
      vertices[1] = vertices[3];
      iSwappedStartsY = startsy[1];
      vertices[3] = pSavedVert;
      iSwappedStartsX_1 = startsx[1];
      startsx[1] = iSwappedStartsX;
      startsx[3] = iSwappedStartsX_1;
      startsy[1] = startsy[3];
      startsy[3] = iSwappedStartsY;

      // Render swapped quad
      polym(vertices, 4, pTex);

      // Restore og vert order
      startsx[3] = startsx[1];
      startsx[1] = iSwappedStartsX_1;
      startsy[3] = startsy[1];
      startsy[1] = iSwappedStartsY;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0006C620
void POLYTEX(uint8 *pTexture, uint8 *pScrBuf, tPolyParams *pPolyParams, int iTexIdx, int iGfxSize)
{
  uint32 uiSurfaceType; // eax
  int iVert0X; // edx
  int iVert0Y; // ecx
  int iDeltaX02; // edx
  int iDeltaY02; // ecx
  int iTempX; // edx
  int iTempY; // ecx
  int iTempX_1; // edx
  int iTempY_1; // ecx
  int iTempStartsX0; // edx
  int iTempStartsX1; // edx
  int iTempStartsY0; // edx
  int iTempStartsY1; // edx
  int iTexRowOffset; // ecx
  int iTexColOffset; // eax
  tPoint *vertices; // edx
  //int iMapselOffset; // ebx
  int iTempStartsX2; // eax
  int iTempStartsX3; // eax
  int iTempStartsY2; // eax
  int iTempStartsY3; // eax
  int iDeltaY01; // [esp+4h] [ebp-18h]
  char iShouldFlipVert; // [esp+8h] [ebp-14h]
  char iShouldFlipHoriz; // [esp+Ch] [ebp-10h]

  if (!pTexture)
    return; //added by ROLLER

  // Check if tex idx is valid for given tex type
  uiSurfaceType = pPolyParams->iSurfaceType;
  if ((uint8)pPolyParams->iSurfaceType >= NoOfTextures && (uiSurfaceType & SURFACE_FLAG_SKIP_RENDER) == 0 && !iTexIdx
    || (uint8)uiSurfaceType >= BldTextures && (uiSurfaceType & SURFACE_FLAG_SKIP_RENDER) == 0 && iTexIdx == 17)
  {
    // Invalid texture, render as flat colored pol instead
    pPolyParams->iSurfaceType = (uint8)pPolyParams->iSurfaceType;
    POLYFLAT(pScrBuf, pPolyParams);
    uiSurfaceType = SURFACE_FLAG_SKIP_RENDER;
  }

  // Textured rendering
  if ((uiSurfaceType & SURFACE_FLAG_SKIP_RENDER) == 0)
  {
    scrptr = pScrBuf;
    scrptr1 = &pScrBuf[winw];
    if ((uiSurfaceType & SURFACE_FLAG_FLIP_HORIZ) != 0)
      iShouldFlipHoriz = -1;
    else
      iShouldFlipHoriz = 0;
    if ((uiSurfaceType & SURFACE_FLAG_FLIP_VERT) != 0)
      iShouldFlipVert = -1;
    else
      iShouldFlipVert = 0;

    // Handle backface culling and vertex reordering
    //SURFACE_FLAG_BACK added by ROLLER, TODO: why didn't the IDA decomp check for this
    //and how does it work?
    if ((uiSurfaceType & (SURFACE_FLAG_FLIP_BACKFACE | SURFACE_FLAG_BACK)) != 0)
    {
      // Get first vertex coords for cross product calculation
      iVert0X = pPolyParams->vertices[0].x;
      iVert0Y = pPolyParams->vertices[0].y;
      iDeltaY01 = iVert0Y - pPolyParams->vertices[1].y;

      // Find a non-degenerate edge for cross product
      if (pPolyParams->vertices[1].x == pPolyParams->vertices[2].x
        && pPolyParams->vertices[1].y == pPolyParams->vertices[2].y) {
        // Vertex 1 and 2 are the same, use vertex 3
        iDeltaX02 = iVert0X - pPolyParams->vertices[3].x;
        iDeltaY02 = iVert0Y - pPolyParams->vertices[3].y;
      } else {
        // Use vertex 2
        iDeltaX02 = iVert0X - pPolyParams->vertices[2].x;
        iDeltaY02 = iVert0Y - pPolyParams->vertices[2].y;
      }

      // Cross product test for backface detection
      if ((pPolyParams->vertices[0].x - pPolyParams->vertices[1].x) * iDeltaY02 > iDeltaY01 * iDeltaX02) {
        // Backface detected, swap vertex pairs to flip winding order
        // Swap verts 0 and 1
        iTempX = pPolyParams->vertices[0].x;
        pPolyParams->vertices[0].x = pPolyParams->vertices[1].x;
        pPolyParams->vertices[1].x = iTempX;
        iTempY = pPolyParams->vertices[0].y;
        pPolyParams->vertices[0].y = pPolyParams->vertices[1].y;
        pPolyParams->vertices[1].y = iTempY;

        // Swap verts 2 and 3
        iTempX_1 = pPolyParams->vertices[2].x;
        pPolyParams->vertices[2].x = pPolyParams->vertices[3].x;
        pPolyParams->vertices[3].x = iTempX_1;
        iTempY_1 = pPolyParams->vertices[2].y;
        pPolyParams->vertices[2].y = pPolyParams->vertices[3].y;
        pPolyParams->vertices[3].y = iTempY_1;

        // Flip horiz tex coord flag
        iShouldFlipHoriz = iShouldFlipHoriz == 0;

        // Handle back texture substitution
        if ((uiSurfaceType & SURFACE_FLAG_BACK) != 0)
        {
          uiSurfaceType = texture_back[256 * iTexIdx + (uint8)uiSurfaceType];
          uiSurfaceType |= SURFACE_FLAG_FLIP_BACKFACE;
        }
      }
    }

    // Apply horiz tex coord flipping
    if (iShouldFlipHoriz) {
      iTempStartsX0 = startsx[1];
      startsx[1] = startsx[0];
      startsx[0] = iTempStartsX0;
      iTempStartsX1 = startsx[3];
      startsx[3] = startsx[2];
      startsx[2] = iTempStartsX1;
    }

    // Apply vertical tex coord flipping
    if (iShouldFlipVert) {
      iTempStartsY0 = startsy[2];
      startsy[2] = startsy[0];
      startsy[0] = iTempStartsY0;
      iTempStartsY1 = startsy[3];
      startsy[3] = startsy[1];
      startsy[1] = iTempStartsY1;
    }

    // increase pol count sta
    ++num_pols;

    // Choose rendering method
    if ((uiSurfaceType & SURFACE_FLAG_PARTIAL_TRANS) != 0)
    {
      if (iGfxSize) {
        // 32x32 textures, 8 per row
        iTexRowOffset = (int)(int8)uiSurfaceType >> 3 << 13;// row * 8192
        iTexColOffset = 32 * (uiSurfaceType & 7);// col * 32
      } else {
        // 64x64 textures, 4 per row
        iTexRowOffset = (int)(uint8)uiSurfaceType >> 2 << 14;// row * 16384
        iTexColOffset = (uiSurfaceType & 3) << 6;// col * 64
      }
      // Render transparent textured polygon
      polyt(pPolyParams->vertices, 4, &pTexture[iTexRowOffset + iTexColOffset]);
    } else {
      // Opaque rendering
      vertices = pPolyParams->vertices;
       // iMapselOffset = 514 * iTexIdx + 2 * (uint8)uiSurfaceType; //offset assumes array of int16s
      if ((uiSurfaceType & SURFACE_FLAG_CONCAVE) != 0)
        // Render concave pol (tri)
        twpolym(vertices, mapsel[257 * iTexIdx + (uint8)uiSurfaceType]);
      else
        // Render convex pol (quad)
        polym(vertices, 4, mapsel[257 * iTexIdx + (uint8)uiSurfaceType]);
    }

    // Restore original tex coords after rendering
    if (iShouldFlipHoriz) {
      iTempStartsX2 = startsx[1];
      startsx[1] = startsx[0];
      startsx[0] = iTempStartsX2;
      iTempStartsX3 = startsx[3];
      startsx[3] = startsx[2];
      startsx[2] = iTempStartsX3;
    }
    if (iShouldFlipVert) {
      iTempStartsY2 = startsy[2];
      startsy[2] = startsy[0];
      startsy[0] = iTempStartsY2;
      iTempStartsY3 = startsy[3];
      startsy[3] = startsy[1];
      startsy[1] = iTempStartsY3;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0006C8D0
void polym(tPoint *vertices, int iNumVerts, uint8 *pTex)
{
  polyt(vertices, iNumVerts, pTex);
}

//-------------------------------------------------------------------------------------------------
//0006F240
void polyt(tPoint *vertices, int iNumVerts, uint8_t *pTex)
{
  // Find polygon bounds and top vertex
  int iMinX = vertices[0].x;
  int iMaxX = vertices[0].x;
  int iMinY = vertices[0].y;
  int iMaxY = vertices[0].y;
  int iTopVertexIdx = 0;

  for (int i = 1; i < iNumVerts; i++) {
    int x = vertices[i].x;
    int y = vertices[i].y;

    if (x < iMinX) iMinX = x;
    if (x > iMaxX) iMaxX = x;

    if (y < iMinY) {
      iMinY = y;
      iTopVertexIdx = i;
    }
    if (y > iMaxY) iMaxY = y;
  }

  // Early exit if polygon is completely outside screen
  if (iMaxX < 0 || iMaxY < 0 || iMinX >= winw || iMinY >= winh)
    return;

  // Detect winding order using signed area calculation
  // Positive area = counter-clockwise, negative = clockwise
  int iSignedArea = 0;
  for (int i = 0; i < iNumVerts; i++) {
    int j = (i + 1) % iNumVerts;
    iSignedArea += (vertices[j].x - vertices[i].x) * (vertices[j].y + vertices[i].y);
  }

  // Determine edge walking directions based on winding order
  int iLeftDir, iRightDir;
  if (iSignedArea > 0) {
    // Counter-clockwise: left edge goes forward, right goes backward
    iLeftDir = 1;
    iRightDir = -1;
  } else {
    // Clockwise: left edge goes backward, right goes forward
    iLeftDir = -1;
    iRightDir = 1;
  }

  // Edge walking variables (floating-point)
  float fLeftEdgeX = 0.0f;          // Left edge X position
  float fRightEdgeX = 0.0f;         // Right edge X position  
  float fLeftEdgeStep = 0.0f;       // Left edge X step per scanline
  float fRightEdgeStep = 0.0f;      // Right edge X step per scanline

  // Texture coordinate interpolation variables
  float fLeftTexX = 0.0f, fLeftTexY = 0.0f;      // Current texture coords on left edge
  float fRightTexX = 0.0f, fRightTexY = 0.0f;    // Current texture coords on right edge
  float fLeftTexXStep = 0.0f, fLeftTexYStep = 0.0f;    // Texture coord steps for left edge
  float fRightTexXStep = 0.0f, fRightTexYStep = 0.0f;  // Texture coord steps for right edge

  // Vertex indices for edge walking
  int iLeftVertexIdx = iTopVertexIdx;
  int iRightVertexIdx = iTopVertexIdx;

  // Current scanline Y and edge heights
  int iScanlineY = vertices[iTopVertexIdx].y;
  int iLeftEdgeHeight = 0;
  int iRightEdgeHeight = 0;

  // Initialize starting positions
  if (iScanlineY >= 0) {
    // Normal case: polygon starts on or below screen top
    fLeftEdgeX = (float)vertices[iTopVertexIdx].x;
    fRightEdgeX = fLeftEdgeX;

    fLeftTexX = FROM_FIXED(startsx[iTopVertexIdx]);
    fLeftTexY = FROM_FIXED(startsy[iTopVertexIdx]);
    fRightTexX = fLeftTexX;
    fRightTexY = fLeftTexY;

    // Find first non-horizontal left edge
    do {
      int iPrevLeftVertexIdx = iLeftVertexIdx;
      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iScanlineY == iMaxY)
        return; // Degenerate polygon

      iLeftEdgeHeight = vertices[iLeftVertexIdx].y - iScanlineY;

      if (iLeftEdgeHeight > 0) {
        // Calculate left edge parameters
        float fDeltaX = (float)(vertices[iLeftVertexIdx].x - (int)fLeftEdgeX);
        fLeftEdgeStep = fDeltaX / (float)iLeftEdgeHeight;

        // Calculate texture coordinate steps
        fLeftTexXStep = (FROM_FIXED(startsx[iLeftVertexIdx]) - fLeftTexX) / (float)iLeftEdgeHeight;
        fLeftTexYStep = (FROM_FIXED(startsy[iLeftVertexIdx]) - fLeftTexY) / (float)iLeftEdgeHeight;

        // Adjust for negative slopes (prestep)
        if (fLeftEdgeStep < 0.0f) {
          fLeftEdgeX += fLeftEdgeStep;
          fLeftTexX += fLeftTexXStep;
          fLeftTexY += fLeftTexYStep;
        }
        break;
      }

      // Update position for horizontal edge
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      fLeftTexX = FROM_FIXED(startsx[iLeftVertexIdx]);
      fLeftTexY = FROM_FIXED(startsy[iLeftVertexIdx]);
    } while (true);

    // Find first non-horizontal right edge
    do {
      int iPrevRightVertexIdx = iRightVertexIdx;
      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iScanlineY == iMaxY)
        return; // Degenerate polygon

      iRightEdgeHeight = vertices[iRightVertexIdx].y - iScanlineY;

      if (iRightEdgeHeight > 0) {
        // Calculate right edge parameters
        float fDeltaX = (float)(vertices[iRightVertexIdx].x - (int)fRightEdgeX);
        fRightEdgeStep = fDeltaX / (float)iRightEdgeHeight;

        // Calculate texture coordinate steps
        fRightTexXStep = (FROM_FIXED(startsx[iRightVertexIdx]) - fRightTexX) / (float)iRightEdgeHeight;
        fRightTexYStep = (FROM_FIXED(startsy[iRightVertexIdx]) - fRightTexY) / (float)iRightEdgeHeight;

        // Adjust for positive slopes (prestep)
        if (fRightEdgeStep > 0.0f) {
          fRightEdgeX += fRightEdgeStep;
          fRightTexX += fRightTexXStep;
          fRightTexY += fRightTexYStep;
        }
        break;
      }

      // Update position for horizontal edge
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      fRightTexX = FROM_FIXED(startsx[iRightVertexIdx]);
      fRightTexY = FROM_FIXED(startsy[iRightVertexIdx]);
    } while (true);
  } else {
    // Special case: polygon starts above screen top
    // Need to clip and find first visible edges

    // Walk left edge until it enters screen
    while (vertices[iLeftVertexIdx].y < 0) {
      int iPrevY = vertices[iLeftVertexIdx].y;
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      fLeftTexX = FROM_FIXED(startsx[iLeftVertexIdx]);
      fLeftTexY = FROM_FIXED(startsy[iLeftVertexIdx]);

      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped left edge
    int iPrevLeftIdx = (iLeftVertexIdx - iLeftDir + iNumVerts) % iNumVerts;
    int iStartY = vertices[iPrevLeftIdx].y;
    int iEndY = vertices[iLeftVertexIdx].y;
    iLeftEdgeHeight = iEndY - 0; // Clip to y=0

    if (iLeftEdgeHeight > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftIdx].x);
      fLeftEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fLeftEdgeX = (float)vertices[iPrevLeftIdx].x + fLeftEdgeStep * (float)(0 - iStartY);

      // Calculate texture steps and adjust for clipping
      float fTexXStart = FROM_FIXED(startsx[iPrevLeftIdx]);
      float fTexYStart = FROM_FIXED(startsy[iPrevLeftIdx]);
      fLeftTexXStep = (FROM_FIXED(startsx[iLeftVertexIdx]) - fTexXStart) / (float)iTotalHeight;
      fLeftTexYStep = (FROM_FIXED(startsy[iLeftVertexIdx]) - fTexYStart) / (float)iTotalHeight;
      fLeftTexX = fTexXStart + fLeftTexXStep * (float)(0 - iStartY);
      fLeftTexY = fTexYStart + fLeftTexYStep * (float)(0 - iStartY);

      if (fLeftEdgeStep < 0.0f) {
        fLeftEdgeX += fLeftEdgeStep;
        fLeftTexX += fLeftTexXStep;
        fLeftTexY += fLeftTexYStep;
      }
    }

    // Similar logic for right edge
    while (vertices[iRightVertexIdx].y < 0) {
      int iPrevY = vertices[iRightVertexIdx].y;
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      fRightTexX = FROM_FIXED(startsx[iRightVertexIdx]);
      fRightTexY = FROM_FIXED(startsy[iRightVertexIdx]);

      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped right edge
    int iPrevRightIdx = (iRightVertexIdx - iRightDir + iNumVerts) % iNumVerts;
    iStartY = vertices[iPrevRightIdx].y;
    iEndY = vertices[iRightVertexIdx].y;
    iRightEdgeHeight = iEndY - 0; // Clip to y=0

    if (iRightEdgeHeight > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightIdx].x);
      fRightEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fRightEdgeX = (float)vertices[iPrevRightIdx].x + fRightEdgeStep * (float)(0 - iStartY);

      // Calculate texture steps and adjust for clipping
      float fTexXStart = FROM_FIXED(startsx[iPrevRightIdx]);
      float fTexYStart = FROM_FIXED(startsy[iPrevRightIdx]);
      fRightTexXStep = (FROM_FIXED(startsx[iRightVertexIdx]) - fTexXStart) / (float)iTotalHeight;
      fRightTexYStep = (FROM_FIXED(startsy[iRightVertexIdx]) - fTexYStart) / (float)iTotalHeight;
      fRightTexX = fTexXStart + fRightTexXStep * (float)(0 - iStartY);
      fRightTexY = fTexYStart + fRightTexYStep * (float)(0 - iStartY);

      if (fRightEdgeStep > 0.0f) {
        fRightEdgeX += fRightEdgeStep;
        fRightTexX += fRightTexXStep;
        fRightTexY += fRightTexYStep;
      }
    }

    iScanlineY = 0;
  }

  // Main rasterization loop
  while (iScanlineY < winh) {
    // Check if we have a valid span to render
    int iLeftX = (int)fLeftEdgeX;
    int iRightX = (int)fRightEdgeX;

    if (iLeftX < iRightX && iRightX > 0 && iLeftX < winw) {
      // Clip span to screen bounds
      int iStartX = iLeftX < 0 ? 0 : iLeftX;
      int iEndX = iRightX > winw ? winw : iRightX;

      if (iStartX < iEndX) {
        // Calculate texture coordinate interpolants for this span
        int iSpanWidth = iRightX - iLeftX;
        float fTexXStep = (fRightTexX - fLeftTexX) / (float)iSpanWidth;
        float fTexYStep = (fRightTexY - fLeftTexY) / (float)iSpanWidth;

        // Adjust texture coordinates for clipped start
        float fTexX = fLeftTexX + (float)(iStartX - iLeftX) * fTexXStep;
        float fTexY = fLeftTexY + (float)(iStartX - iLeftX) * fTexYStep;

        // Render the span
        uint8_t *pDest = &scrptr[iScanlineY * winw + iStartX];
        for (int x = iStartX; x < iEndX; x++) {
          // Extract texture coordinates (convert float to int for 64x64 texture)
          int iU = (int)fTexX;
          int iV = (int)fTexY;

          // Sample texture
          uint8_t texel = pTex[iV * 256 + iU];

          // Write pixel if not transparent (0 = transparent)
          if (texel != 0) {
            *pDest = texel;
          }

          pDest++;
          fTexX += fTexXStep;
          fTexY += fTexYStep;
        }
      }
    }

    // Advance to next scanline
    iScanlineY++;
    fLeftEdgeX += fLeftEdgeStep;
    fRightEdgeX += fRightEdgeStep;
    fLeftTexX += fLeftTexXStep;
    fLeftTexY += fLeftTexYStep;
    fRightTexX += fRightTexXStep;
    fRightTexY += fRightTexYStep;

    // Check if we need to advance to next edge
    iLeftEdgeHeight--;
    if (iLeftEdgeHeight <= 0) {
      // Find next left edge
      do {
        int iPrevLeftVertexIdx = iLeftVertexIdx;
        iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

        if (iPrevLeftVertexIdx == iRightVertexIdx)
          return; // Polygon complete

        iLeftEdgeHeight = vertices[iLeftVertexIdx].y - iScanlineY;

        if (iLeftEdgeHeight > 0) {
          // Setup new left edge
          fLeftEdgeX = (float)vertices[iPrevLeftVertexIdx].x;

          float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftVertexIdx].x);
          fLeftEdgeStep = fDeltaX / (float)iLeftEdgeHeight;

          // Setup texture interpolation
          fLeftTexX = FROM_FIXED(startsx[iPrevLeftVertexIdx]);
          fLeftTexY = FROM_FIXED(startsy[iPrevLeftVertexIdx]);
          fLeftTexXStep = (FROM_FIXED(startsx[iLeftVertexIdx]) - fLeftTexX) / (float)iLeftEdgeHeight;
          fLeftTexYStep = (FROM_FIXED(startsy[iLeftVertexIdx]) - fLeftTexY) / (float)iLeftEdgeHeight;

          if (fLeftEdgeStep < 0.0f) {
            fLeftEdgeX += fLeftEdgeStep;
            fLeftTexX += fLeftTexXStep;
            fLeftTexY += fLeftTexYStep;
          }
          break;
        }
      } while (true);
    }

    iRightEdgeHeight--;
    if (iRightEdgeHeight <= 0) {
      // Find next right edge
      do {
        int iPrevRightVertexIdx = iRightVertexIdx;
        iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

        if (iLeftVertexIdx == iPrevRightVertexIdx)
          return; // Polygon complete

        iRightEdgeHeight = vertices[iRightVertexIdx].y - iScanlineY;

        if (iRightEdgeHeight > 0) {
          // Setup new right edge
          fRightEdgeX = (float)vertices[iPrevRightVertexIdx].x;

          float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightVertexIdx].x);
          fRightEdgeStep = fDeltaX / (float)iRightEdgeHeight;

          // Setup texture interpolation
          fRightTexX = FROM_FIXED(startsx[iPrevRightVertexIdx]);
          fRightTexY = FROM_FIXED(startsy[iPrevRightVertexIdx]);
          fRightTexXStep = (FROM_FIXED(startsx[iRightVertexIdx]) - fRightTexX) / (float)iRightEdgeHeight;
          fRightTexYStep = (FROM_FIXED(startsy[iRightVertexIdx]) - fRightTexY) / (float)iRightEdgeHeight;

          if (fRightEdgeStep > 0.0f) {
            fRightEdgeX += fRightEdgeStep;
            fRightTexX += fRightTexXStep;
            fRightTexY += fRightTexYStep;
          }
          break;
        }
      } while (true);
    }
  }
}

//-------------------------------------------------------------------------------------------------
