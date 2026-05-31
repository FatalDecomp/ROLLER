#include "polyf.h"
#include "3d.h"
#include "polytex.h"
#include "drawtrk3.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

#define SHADE_PALETTE_STRIDE 256
#define SHADE_PALETTE_LEVELS 5

//000706B0
void twpoly(tPoint *vertices, int16 nColor)
{
  int16 nEdge1Dx;
  int16 nEdge2Dx;
  int16 nEdge2Dy;
  int16 nEdge3Dy;
  double dIntersectionX2;
  double dIntersectionY2;
  int iNumVerts;
  tPoint *vertices2;
  int16 nColor2;
  double dIntersectionX;
  double dIntersectionY;
  int iCrossProductResult2;
  double dIntersectionY3;
  int16 nColor3;
  double dIntersectionY4;
  tPoint pTempPoint2;
  tPoint pTempPoint5;
  tPoint pTempPoint3;
  tPoint pTempPoint4;
  tPoint pTempPoint;
  int iCrossProductResult2_2;
  float fCrossProduct2;
  float fCrossProduct1;
  float fIntersectionParam2;
  double dIntersectionParam;
  int iCrossProductResult1;
  int16 nEdge1Dy;
  int16 nSumEdge23Dy;
  int16 nSumEdge23Dx;
  int16 SumEdge23Dy2;
  int16 nSumEdge23Dx2;
  int16 nEdge3Dx;

  nEdge1Dx = (int16)(vertices[1].x) - (int16)(vertices->x);
  nEdge2Dx = (int16)(vertices[2].x) - (int16)(vertices->x);
  nEdge3Dx = (int16)(vertices[3].x) - (int16)(vertices[2].x);
  nSumEdge23Dx = nEdge2Dx + nEdge3Dx;
  nSumEdge23Dx2 = nEdge1Dx - nEdge2Dx;
  nEdge1Dy = (int16)(vertices[1].y) - (int16)(vertices->y);
  nEdge2Dy = (int16)(vertices[2].y) - (int16)(vertices->y);
  nEdge3Dy = (int16)(vertices[3].y) - (int16)(vertices[2].y);
  nSumEdge23Dy = nEdge2Dy + nEdge3Dy;
  SumEdge23Dy2 = nEdge1Dy - nEdge2Dy;
  iCrossProductResult1 = 0;
  iCrossProductResult2_2 = 0;
  fCrossProduct1 = (float)(nEdge3Dy * nEdge1Dx - nEdge1Dy * nEdge3Dx);

  // Check if quadrilateral is concave by testing edge intersections
  if (fabsf(fCrossProduct1) > FLT_EPSILON) {
    iCrossProductResult1 = nEdge1Dy * nEdge2Dx - nEdge2Dy * nEdge1Dx;
    dIntersectionParam = (double)iCrossProductResult1 / (double)fCrossProduct1;

    // Valid intersection point found 0 < t < 1
    if (dIntersectionParam > 0.0 && dIntersectionParam < 1.0) {
      pTempPoint = *vertices;
      if (iCrossProductResult1 <= 0) {
        // Case 1: Split into triangles P2-P3-P* and P0-P*-P3
        *vertices = vertices[2];                // move P2 to P0 position

        // Calculate intersection point P*
        dIntersectionX = (double)nEdge3Dx * dIntersectionParam + (double)vertices[2].x;
        //CHP
        vertices[2].x = (int)dIntersectionX;
        dIntersectionY = dIntersectionParam * (double)nEdge3Dy + (double)vertices[2].y;
        //CHP
        vertices[2].y = (int)dIntersectionY;

        // Render first triangle (P2, P3, P*)
        poly(vertices, 3, nColor);

        // Prepare second triangle (P0, P*, P3)
        *vertices = vertices[3];                // Original P3
        vertices[1] = pTempPoint;               // Original P0
        poly(vertices, 3, nColor);
        return;
      }
      // Case 2: Split into triangles P1-P2-P* and P0-P*-P3
      *vertices = vertices[1];                  // Move P1 to P0 position
      vertices[1] = vertices[2];                // Move P2 to P1 position

      // Calculate intersection point P*
      dIntersectionX2 = (double)nEdge3Dx * dIntersectionParam + (double)vertices[2].x;
      //CHP
      vertices[2].x = (int)dIntersectionX2;
      dIntersectionY2 = dIntersectionParam * (double)nEdge3Dy + (double)vertices[2].y;
      //CHP
      vertices[2].y = (int)dIntersectionY2;

      // Render first triangle (P1, P2, P*)
      poly(vertices, 3, nColor);

      // Prepare second triangle (P0, P*, P3)
      *vertices = vertices[3];                  // Original P3
      iNumVerts = 3;
      vertices2 = vertices;
      nColor2 = nColor;
      vertices[1] = pTempPoint;                 // Original P0
      goto LABEL_18;
    }
  }

  // Secondary concave test using different edge combination
  fCrossProduct2 = (float)(SumEdge23Dy2 * nSumEdge23Dx - nSumEdge23Dy * nSumEdge23Dx2);

  if (fabsf(fCrossProduct2) <= FLT_EPSILON
    || (iCrossProductResult2 = nEdge2Dx * nSumEdge23Dy - nEdge2Dy * nSumEdge23Dx,
        iCrossProductResult2_2 = iCrossProductResult2,
        fIntersectionParam2 = (float)((double)iCrossProductResult2 / (double)fCrossProduct2),
        fIntersectionParam2 <= 0.0)             // Valid intersection point found (0 < t < 1)
    || fIntersectionParam2 >= 1.0) {

    // Convex quadrilateral handling
    if (iCrossProductResult1 < 0 || iCrossProductResult2_2 > 0) {
      // Swap P1 and P3 to fix winding order
      pTempPoint2 = vertices[1];
      vertices[1] = vertices[3];
      vertices[3] = pTempPoint2;
    }
    nColor2 = nColor;
    vertices2 = vertices;
    iNumVerts = 4;
  LABEL_18:
    // Render as convex quadrilateral
    poly(vertices2, iNumVerts, nColor2);
    return;
  }

  if (iCrossProductResult2 <= 0) {
    // Case 3: Swap vertices and split
    pTempPoint3 = *vertices;
    *vertices = vertices[1];                    // Swap P0 and P1
    vertices[1] = pTempPoint3;
    pTempPoint4 = vertices[2];

    // Calculate intersection point P*
    vertices[2].x = (int)((double)nSumEdge23Dx2 * fIntersectionParam2 + (double)pTempPoint4.x);
    dIntersectionY4 = fIntersectionParam2 * (double)SumEdge23Dy2 + (double)pTempPoint4.y;
    //CHP
    vertices[2].y = (int)dIntersectionY4;
    nColor3 = nColor;

    // Render first triangle
    poly(vertices, 3, nColor);

    // Prepare second triangle
    vertices[1] = vertices[3];
    *vertices = pTempPoint4;
  } else {
    // Case 4: Standard split at intersection
    pTempPoint5 = vertices[2];

    // Calculate intersection point P*
    vertices[2].x = (int)((double)nSumEdge23Dx2 * fIntersectionParam2 + (double)pTempPoint5.x);
    dIntersectionY3 = fIntersectionParam2 * (double)SumEdge23Dy2 + (double)vertices[2].y;
    //CHP
    vertices[2].y = (int)dIntersectionY3;
    nColor3 = nColor;

    // Render first triangle
    poly(vertices, 3, nColor);

    // Prepare second triangle
    *vertices = vertices[3];                    // Original P3
    vertices[1] = pTempPoint5;                  // Original P2
  }
  poly(vertices, 3, nColor3);
}

//-------------------------------------------------------------------------------------------------
//00070A40
void POLYFLAT(uint8 *pScrBuf, tPolyParams *polyParams)
{
  uint32 uiSurfaceType; // ebx
  int iX0; // edx
  int iY0; // ecx
  int iY1; // ebp
  int iDx; // edx
  int iDy; // ecx
  int iX0_1; // edx
  int iY0_1; // ecx
  int iX1_1; // edx
  int iY1_1; // ecx
  int iX1; // [esp+0h] [ebp-18h]

  uiSurfaceType = polyParams->iSurfaceType;
  if ((polyParams->iSurfaceType & SURFACE_FLAG_SKIP_RENDER) == 0) {
    if ((uiSurfaceType & SURFACE_FLAG_FLIP_BACKFACE) != 0) {
      iX0 = polyParams->vertices[0].x;
      iY0 = polyParams->vertices[0].y;
      iX1 = iX0 - polyParams->vertices[1].x;
      iY1 = iY0 - polyParams->vertices[1].y;

      // Determine comparison points (p2 or p3)
      if (polyParams->vertices[1].x == polyParams->vertices[2].x
        && polyParams->vertices[1].y == polyParams->vertices[2].y) {
        iDx = iX0 - polyParams->vertices[3].x;
        iDy = iY0 - polyParams->vertices[3].y;
      } else {
        iDx = iX0 - polyParams->vertices[2].x;
        iDy = iY0 - polyParams->vertices[2].y;
      }

      // Backface detection using cross product
      if (iX1 * iDy > iY1 * iDx) {
        // Swap vertices to flip polygon orientation
        iX0_1 = polyParams->vertices[0].x;
        // Swap P0 and P3
        polyParams->vertices[0].x = polyParams->vertices[3].x;
        polyParams->vertices[3].x = iX0_1;
        iY0_1 = polyParams->vertices[0].y;
        polyParams->vertices[0].y = polyParams->vertices[3].y;
        polyParams->vertices[3].y = iY0_1;
        // Swap P1 and P2
        iX1_1 = polyParams->vertices[1].x;
        polyParams->vertices[1].x = polyParams->vertices[2].x;
        polyParams->vertices[2].x = iX1_1;
        iY1_1 = polyParams->vertices[1].y;
        polyParams->vertices[1].y = polyParams->vertices[2].y;
        polyParams->vertices[2].y = iY1_1;
      }
    }
    scrptr = pScrBuf;
    ++num_pols;
    if ((uiSurfaceType & SURFACE_FLAG_TRANSPARENT) != 0)      // shadow polygon
    {
      shadow_poly(polyParams->vertices, polyParams->uiNumVerts, (uint8)uiSurfaceType);
    } else if ((uiSurfaceType & SURFACE_FLAG_CONCAVE) != 0) {
      twpoly(polyParams->vertices, (uint8)uiSurfaceType);// potentially concave quad
    } else                                        // solid polygon
    {
      poly(polyParams->vertices, polyParams->uiNumVerts, (uint8)uiSurfaceType);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00070B40
void poly(tPoint *vertices, int iNumVerts, int16 nColor)
{
  // Find polygon bounds and top vertex
  int iMinX = vertices[0].x;
  int iMaxX = vertices[0].x;
  int iMinY = vertices[0].y;
  int iMaxY = vertices[0].y;
  int iMinYIdx = 0;

  for (int i = 1; i < iNumVerts; i++) {
    int x = vertices[i].x;
    int y = vertices[i].y;

    if (x < iMinX) iMinX = x;
    if (x > iMaxX) iMaxX = x;

    if (y < iMinY) {
      iMinY = y;
      iMinYIdx = i;
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

  // Vertex indices for edge walking
  int iLeftVertexIdx = iMinYIdx;
  int iRightVertexIdx = iMinYIdx;

  // Current scanline Y and edge heights
  int iScanlineY = vertices[iMinYIdx].y;
  int iLeftRemain = 0;
  int iRightRemain = 0;

  // Initialize starting positions
  if (iScanlineY >= 0) {
    // Normal case: polygon starts on or below screen top
    fLeftEdgeX = (float)vertices[iMinYIdx].x;
    fRightEdgeX = fLeftEdgeX;

    // Find first non-horizontal left edge
    do {
      int iPrevLeftVertexIdx = iLeftVertexIdx;
      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iScanlineY == iMaxY)
        return; // Degenerate polygon

      iLeftRemain = vertices[iLeftVertexIdx].y - iScanlineY;

      if (iLeftRemain > 0) {
        // Calculate left edge parameters
        float fDeltaX = (float)(vertices[iLeftVertexIdx].x - (int)fLeftEdgeX);
        fLeftEdgeStep = fDeltaX / (float)iLeftRemain;

        // Adjust for negative slopes (prestep)
        if (fLeftEdgeStep < 0.0f) {
          fLeftEdgeX += fLeftEdgeStep;
        }
        break;
      }

      // Update position for horizontal edge
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
    } while (true);

    // Find first non-horizontal right edge
    do {
      int iPrevRightVertexIdx = iRightVertexIdx;
      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iScanlineY == iMaxY)
        return; // Degenerate polygon

      iRightRemain = vertices[iRightVertexIdx].y - iScanlineY;

      if (iRightRemain > 0) {
        // Calculate right edge parameters
        float fDeltaX = (float)(vertices[iRightVertexIdx].x - (int)fRightEdgeX);
        fRightEdgeStep = fDeltaX / (float)iRightRemain;

        // Adjust for positive slopes (prestep)
        if (fRightEdgeStep > 0.0f) {
          fRightEdgeX += fRightEdgeStep;
        }
        break;
      }

      // Update position for horizontal edge
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
    } while (true);
  } else {
    // Special case: polygon starts above screen top
    // Need to clip and find first visible edges

    // Walk left edge until it enters screen
    while (vertices[iLeftVertexIdx].y < 0) {
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped left edge
    int iPrevLeftIdx = (iLeftVertexIdx - iLeftDir + iNumVerts) % iNumVerts;
    int iStartY = vertices[iPrevLeftIdx].y;
    int iEndY = vertices[iLeftVertexIdx].y;
    iLeftRemain = iEndY - 0; // Clip to y=0

    if (iLeftRemain > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftIdx].x);
      fLeftEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fLeftEdgeX = (float)vertices[iPrevLeftIdx].x + fLeftEdgeStep * (float)(0 - iStartY);

      if (fLeftEdgeStep < 0.0f) {
        fLeftEdgeX += fLeftEdgeStep;
      }
    }

    // Similar logic for right edge
    while (vertices[iRightVertexIdx].y < 0) {
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped right edge
    int iPrevRightIdx = (iRightVertexIdx - iRightDir + iNumVerts) % iNumVerts;
    iStartY = vertices[iPrevRightIdx].y;
    iEndY = vertices[iRightVertexIdx].y;
    iRightRemain = iEndY - 0; // Clip to y=0

    if (iRightRemain > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightIdx].x);
      fRightEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fRightEdgeX = (float)vertices[iPrevRightIdx].x + fRightEdgeStep * (float)(0 - iStartY);

      if (fRightEdgeStep > 0.0f) {
        fRightEdgeX += fRightEdgeStep;
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
        // Fill the span with solid color
        uint8 *pDest = &scrptr[iScanlineY * winw + iStartX];
        int iSpanWidth = iEndX - iStartX;
        memset(pDest, nColor, iSpanWidth);
      }
    }

    // Advance to next scanline
    iScanlineY++;
    fLeftEdgeX += fLeftEdgeStep;
    fRightEdgeX += fRightEdgeStep;

    // Check if we need to advance to next edge
    iLeftRemain--;
    if (iLeftRemain <= 0) {
      // Find next left edge
      do {
        int iPrevLeftVertexIdx = iLeftVertexIdx;
        iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

        if (iPrevLeftVertexIdx == iRightVertexIdx)
          return; // Polygon complete

        iLeftRemain = vertices[iLeftVertexIdx].y - iScanlineY;

        if (iLeftRemain > 0) {
          // Setup new left edge
          fLeftEdgeX = (float)vertices[iPrevLeftVertexIdx].x;

          float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftVertexIdx].x);
          fLeftEdgeStep = fDeltaX / (float)iLeftRemain;

          if (fLeftEdgeStep < 0.0f) {
            fLeftEdgeX += fLeftEdgeStep;
          }
          break;
        }

        // Update position for horizontal edge
        fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      } while (true);
    }

    iRightRemain--;
    if (iRightRemain <= 0) {
      // Find next right edge
      do {
        int iPrevRightVertexIdx = iRightVertexIdx;
        iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

        if (iLeftVertexIdx == iPrevRightVertexIdx)
          return; // Polygon complete

        iRightRemain = vertices[iRightVertexIdx].y - iScanlineY;

        if (iRightRemain > 0) {
          // Setup new right edge
          fRightEdgeX = (float)vertices[iPrevRightVertexIdx].x;

          float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightVertexIdx].x);
          fRightEdgeStep = fDeltaX / (float)iRightRemain;

          if (fRightEdgeStep > 0.0f) {
            fRightEdgeX += fRightEdgeStep;
          }
          break;
        }

        // Update position for horizontal edge
        fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      } while (true);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00071D70
void shadow_poly(tPoint *vertices, int iNumVerts, int iPaletteIndex)
{
  if (iPaletteIndex < 0 || iPaletteIndex >= SHADE_PALETTE_LEVELS)
    return;

  // Find polygon bounds and top vertex
  int iMinX = vertices[0].x;
  int iMaxX = vertices[0].x;
  int iMinY = vertices[0].y;
  int iMaxY = vertices[0].y;
  int iMinYIdx = 0;

  for (int i = 1; i < iNumVerts; i++) {
    int x = vertices[i].x;
    int y = vertices[i].y;

    if (x < iMinX) iMinX = x;
    if (x > iMaxX) iMaxX = x;

    if (y < iMinY) {
      iMinY = y;
      iMinYIdx = i;
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

  // Get shade palette for this palette index
  uint8 *pShadePalette = &shade_palette[SHADE_PALETTE_STRIDE * iPaletteIndex];

  // Edge walking variables (floating-point)
  float fLeftEdgeX = 0.0f;          // Left edge X position
  float fRightEdgeX = 0.0f;         // Right edge X position  
  float fLeftEdgeStep = 0.0f;       // Left edge X step per scanline
  float fRightEdgeStep = 0.0f;      // Right edge X step per scanline

  // Vertex indices for edge walking
  int iLeftVertexIdx = iMinYIdx;
  int iRightVertexIdx = iMinYIdx;

  // Current scanline Y and edge heights
  int iCurrScanline = vertices[iMinYIdx].y;
  int iLeftRemain = 0;
  int iRightRemain = 0;

  // Initialize starting positions
  if (iCurrScanline >= 0) {
    // Normal case: polygon starts on or below screen top
    fLeftEdgeX = (float)vertices[iMinYIdx].x;
    fRightEdgeX = fLeftEdgeX;

    // Find first non-horizontal left edge
    do {
      int iPrevLeftVertexIdx = iLeftVertexIdx;
      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iCurrScanline == iMaxY)
        return; // Degenerate polygon

      iLeftRemain = vertices[iLeftVertexIdx].y - iCurrScanline;

      if (iLeftRemain > 0) {
        // Calculate left edge parameters
        float fDeltaX = (float)(vertices[iLeftVertexIdx].x - (int)fLeftEdgeX);
        fLeftEdgeStep = fDeltaX / (float)iLeftRemain;

        // Adjust for negative slopes (prestep)
        if (fLeftEdgeStep < 0.0f) {
          fLeftEdgeX += fLeftEdgeStep;
        }
        break;
      }

      // Update position for horizontal edge
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
    } while (true);

    // Find first non-horizontal right edge
    do {
      int iPrevRightVertexIdx = iRightVertexIdx;
      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

      if (iLeftVertexIdx == iRightVertexIdx && iCurrScanline == iMaxY)
        return; // Degenerate polygon

      iRightRemain = vertices[iRightVertexIdx].y - iCurrScanline;

      if (iRightRemain > 0) {
        // Calculate right edge parameters
        float fDeltaX = (float)(vertices[iRightVertexIdx].x - (int)fRightEdgeX);
        fRightEdgeStep = fDeltaX / (float)iRightRemain;

        // Adjust for positive slopes (prestep)
        if (fRightEdgeStep > 0.0f) {
          fRightEdgeX += fRightEdgeStep;
        }
        break;
      }

      // Update position for horizontal edge
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
    } while (true);
  } else {
    // Special case: polygon starts above screen top
    // Need to clip and find first visible edges

    // Walk left edge until it enters screen
    while (vertices[iLeftVertexIdx].y < 0) {
      fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped left edge
    int iPrevLeftIdx = (iLeftVertexIdx - iLeftDir + iNumVerts) % iNumVerts;
    int iStartY = vertices[iPrevLeftIdx].y;
    int iEndY = vertices[iLeftVertexIdx].y;
    iLeftRemain = iEndY - 0; // Clip to y=0

    if (iLeftRemain > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftIdx].x);
      fLeftEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fLeftEdgeX = (float)vertices[iPrevLeftIdx].x + fLeftEdgeStep * (float)(0 - iStartY);

      if (fLeftEdgeStep < 0.0f) {
        fLeftEdgeX += fLeftEdgeStep;
      }
    }

    // Similar logic for right edge
    while (vertices[iRightVertexIdx].y < 0) {
      fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;
    }

    // Calculate clipped right edge
    int iPrevRightIdx = (iRightVertexIdx - iRightDir + iNumVerts) % iNumVerts;
    iStartY = vertices[iPrevRightIdx].y;
    iEndY = vertices[iRightVertexIdx].y;
    iRightRemain = iEndY - 0; // Clip to y=0

    if (iRightRemain > 0) {
      int iTotalHeight = iEndY - iStartY;
      float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightIdx].x);
      fRightEdgeStep = fDeltaX / (float)iTotalHeight;

      // Adjust for clipping
      fRightEdgeX = (float)vertices[iPrevRightIdx].x + fRightEdgeStep * (float)(0 - iStartY);

      if (fRightEdgeStep > 0.0f) {
        fRightEdgeX += fRightEdgeStep;
      }
    }

    iCurrScanline = 0;
  }

  // Main rasterization loop
  while (iCurrScanline < winh) {
    // Check if we have a valid span to render
    int iLeftX = (int)fLeftEdgeX;
    int iRightX = (int)fRightEdgeX;

    if (iLeftX < iRightX && iRightX > 0 && iLeftX < winw) {
      // Clip span to screen bounds
      int iStartX = iLeftX < 0 ? 0 : iLeftX;
      int iEndX = iRightX > winw ? winw : iRightX;

      if (iStartX < iEndX) {
        // Render the span with shading palette
        uint8 *pDest = &scrptr[iCurrScanline * winw + iStartX];
        for (int x = iStartX; x < iEndX; x++) {
          // Apply shading palette to existing pixel
          *pDest = pShadePalette[*pDest];
          pDest++;
        }
      }
    }

    // Advance to next scanline
    iCurrScanline++;
    fLeftEdgeX += fLeftEdgeStep;
    fRightEdgeX += fRightEdgeStep;

    // Check if we need to advance to next edge
    iLeftRemain--;
    if (iLeftRemain <= 0) {
      // Find next left edge
      do {
        int iPrevLeftVertexIdx = iLeftVertexIdx;
        iLeftVertexIdx = (iLeftVertexIdx + iLeftDir + iNumVerts) % iNumVerts;

        if (iPrevLeftVertexIdx == iRightVertexIdx)
          return; // Polygon complete

        iLeftRemain = vertices[iLeftVertexIdx].y - iCurrScanline;

        if (iLeftRemain > 0) {
          // Setup new left edge
          fLeftEdgeX = (float)vertices[iPrevLeftVertexIdx].x;

          float fDeltaX = (float)(vertices[iLeftVertexIdx].x - vertices[iPrevLeftVertexIdx].x);
          fLeftEdgeStep = fDeltaX / (float)iLeftRemain;

          if (fLeftEdgeStep < 0.0f) {
            fLeftEdgeX += fLeftEdgeStep;
          }
          break;
        }

        // Update position for horizontal edge
        fLeftEdgeX = (float)vertices[iLeftVertexIdx].x;
      } while (true);
    }

    iRightRemain--;
    if (iRightRemain <= 0) {
      // Find next right edge
      do {
        int iPrevRightVertexIdx = iRightVertexIdx;
        iRightVertexIdx = (iRightVertexIdx + iRightDir + iNumVerts) % iNumVerts;

        if (iLeftVertexIdx == iPrevRightVertexIdx)
          return; // Polygon complete

        iRightRemain = vertices[iRightVertexIdx].y - iCurrScanline;

        if (iRightRemain > 0) {
          // Setup new right edge
          fRightEdgeX = (float)vertices[iPrevRightVertexIdx].x;

          float fDeltaX = (float)(vertices[iRightVertexIdx].x - vertices[iPrevRightVertexIdx].x);
          fRightEdgeStep = fDeltaX / (float)iRightRemain;

          if (fRightEdgeStep > 0.0f) {
            fRightEdgeX += fRightEdgeStep;
          }
          break;
        }

        // Update position for horizontal edge
        fRightEdgeX = (float)vertices[iRightVertexIdx].x;
      } while (true);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00070B40
/*void poly(tPoint *vertices, int iVertexCount, int16 nColor)
{
  int iOldWinW; // ebp
  int iMinYIdx; // edi
  int iCurrX; // ecx
  int iCurrY; // ebx
  tPoint *pTopVertex; // eax
  int iScanlineY; // ecx
  int iY; // ebx
  int16 nX; // ax
  int y; // eax
  int v13; // ecx
  int16 nX_1; // ax
  int v15; // eax
  int v16; // ecx
  int v17; // ebx
  int v18; // edx
  int iLeftEdgeRemaining; // edx
  int16 v20; // ebx^2
  tPoint *pNextLeftPt; // eax
  int v22; // edx
  tPoint *pVertex; // eax
  int v24; // ebx
  int v25; // edx
  int v26; // edx
  int v27; // ebx
  bool bZeroFlag; // zf
  int16 nv29; // ax
  tPoint *pVertex_1; // eax
  int v31; // ebx
  int v32; // ebx
  int v33; // eax
  int16 nv34; // bx
  tPoint *pVertex_2; // eax
  int16 nv36; // ebx^2
  int v37; // edx
  int16 nv38; // ax
  tPoint *pVertex_7; // eax
  int v40; // edx
  int v41; // edx
  tPoint *pVertex_8; // eax
  int v43; // edx
  int iPixelCount; // ebx
  int iColor; // edx
  uint8 *pDest_1; // eax
  int v47; // edx
  int v48; // eax
  int16 nv49; // ebx^2
  int v50; // edx
  int16 nv51; // ax
  tPoint *pVertex_3; // eax
  int v53; // edx
  int v54; // edx
  int v55; // edx
  tPoint *pVertex_4; // eax
  int v57; // edx
  int v58; // eax
  uint8 *pScrBuf; // ecx
  uint8 *pDest; // eax
  int v61; // edx
  int v62; // eax
  int16 nv63; // ebx^2
  int v64; // edx
  int16 nv65; // ax
  tPoint *pVertex_5; // eax
  int v67; // edx
  int v68; // edx
  int v69; // edx
  tPoint *pVertex_6; // eax
  int v71; // edx
  int16 nv72; // ebx^2
  int v73; // edx
  int16 nv74; // ax
  tPoint *pVertex_9; // eax
  int v76; // edx
  int v77; // edx
  tPoint *pVertex_10; // eax
  int v79; // edx
  int v80; // [esp+0h] [ebp-68h]
  int v81; // [esp+0h] [ebp-68h]
  int v82; // [esp+4h] [ebp-64h]
  int v83; // [esp+4h] [ebp-64h]
  int v84; // [esp+4h] [ebp-64h]
  int v85; // [esp+4h] [ebp-64h]
  int16 nLeftXStep; // [esp+Ah] [ebp-5Eh]
  int16 nRightXStep; // [esp+Eh] [ebp-5Ah]
  int16 nv88; // [esp+16h] [ebp-52h]
  int16 nv89; // [esp+1Ah] [ebp-4Eh]
  int iMaxX; // [esp+1Ch] [ebp-4Ch]
  int iMaxY; // [esp+20h] [ebp-48h]
  int iMinY; // [esp+24h] [ebp-44h]
  int iMinX; // [esp+28h] [ebp-40h]
  int v94; // [esp+2Ch] [ebp-3Ch]
  int v95; // [esp+30h] [ebp-38h]
  int v96; // [esp+30h] [ebp-38h]
  int v97; // [esp+30h] [ebp-38h]
  int v98; // [esp+30h] [ebp-38h]
  int v99; // [esp+30h] [ebp-38h]
  int v100; // [esp+30h] [ebp-38h]
  int v101; // [esp+30h] [ebp-38h]
  int v102; // [esp+30h] [ebp-38h]
  int v103; // [esp+34h] [ebp-34h]
  int v104; // [esp+34h] [ebp-34h]
  int v105; // [esp+34h] [ebp-34h]
  int v106; // [esp+34h] [ebp-34h]
  int v107; // [esp+34h] [ebp-34h]
  int v108; // [esp+34h] [ebp-34h]
  int v109; // [esp+34h] [ebp-34h]
  int v110; // [esp+34h] [ebp-34h]
  int iLeftX; // [esp+38h] [ebp-30h]
  int iLeftXFixed; // [esp+38h] [ebp-30h]
  int v113; // [esp+3Ch] [ebp-2Ch]
  int v114; // [esp+3Ch] [ebp-2Ch]
  int iMinYIdx2; // [esp+40h] [ebp-28h]
  int i; // [esp+44h] [ebp-24h]
  int iLeftRemaining_1; // [esp+4Ch] [ebp-1Ch]
  int iLeftRemaining; // [esp+50h] [ebp-18h]

  iOldWinW = winw;
  iMinYIdx = 0;

  // Find the bounding box and top vertex
  iMinX = vertices->x;
  iMaxX = vertices->x;
  i = 1;
  iMinY = vertices->y;
  iMaxY = iMinY;
  // Loop through all vertices to find min/max X/Y
  while ((int16)i < (int16)iVertexCount) {
    iCurrX = vertices[(int16)i].x;
    iCurrY = vertices[(int16)i].y;
    // Update X bounds
    if (iCurrX <= iMaxX) {
      if (iCurrX < iMinX)
        iMinX = vertices[(int16)i].x;
    } else {
      iMaxX = vertices[(int16)i].x;
    }
    // Update Y bounds and find top vertex
    if (iCurrY <= iMaxY) {
      if (iCurrY < iMinY) {
        iMinYIdx = i;
        iMinY = vertices[(int16)i].y;
      }
    } else {
      iMaxY = vertices[(int16)i].y;
    }
    ++i;
  }

  // Check if polygon is outside visible area
  if (iMaxX < 0 || iMaxY < 0 || winw <= iMinX || iMinY >= winh)
    goto CLEANUP_AND_RETURN;

  // Edge tracking variables
  pTopVertex = &vertices[(int16)iMinYIdx];
  iScanlineY = pTopVertex->y;                   // Start at top vertex Y
  iMinYIdx2 = iMinYIdx;                         // Save top vertex index

  // Case 1: starting at or below top of screen
  if (iScanlineY >= 0) {
    // Initialize left edge with top vertex X (16.16 fixed point format)
    SET_HIWORD(iLeftXFixed, pTopVertex->x);
    SET_HIWORD(v114, pTopVertex->x);
    SET_LOWORD(iLeftXFixed, -1);                   // Initialize fraction to -1 for correct rounding
    SET_LOWORD(v114, 0);                           // Initialize fraction to 0
    SET_LOWORD(v105, 0);                           // Clear loword
    SET_LOWORD(v97, 0);                            // Clear loword

    // Initialize left edge
    while (1) {
      SET_HIWORD(iLeftEdgeRemaining, GET_HIWORD(iVertexCount));// Unused hiword
      ++iMinYIdx;                               // Move to next vertex (ccw)
      v20 = GET_HIWORD(v114);                       // Save current leftX
      // Wrap vertex idx if needed
      if ((int16)iMinYIdx == (int16)iVertexCount)
        iMinYIdx ^= iVertexCount;
      pNextLeftPt = &vertices[(int16)iMinYIdx];
      // Calculate scanlines until next verted
      SET_LOWORD(iLeftEdgeRemaining, GET_LOWORD(pNextLeftPt->y) - iScanlineY);
      iLeftRemaining = iLeftEdgeRemaining;
      if ((int16)iLeftEdgeRemaining)          // if edge has visible length
        break;
      // Skip horizontal edges
      if ((int16)iMinYIdx == (int16)iMinYIdx2)
        goto FINAL_SCANLINE_CHECK;              // Back to start vertex
      SET_HIWORD(v114, pNextLeftPt->x);            // Update current X
    }
    // Calculate left edge slope (dx/dy in fixed point)
    SET_HIWORD(v97, GET_LOWORD(pNextLeftPt->x) - GET_HIWORD(v114));// dx
    if (v97 >= 0) {
      INC_HIWORD(v97);                            // Adjust for positive slope
      v96 = v97 / ((int16)iLeftEdgeRemaining + 1);// dx/dy
    } else {
      DEC_HIWORD(v97);                            // Adjust for negative slope
      v81 = (int16)iLeftEdgeRemaining + 1;
      v114 += v97 / v81;                        // update X with quotient
      v96 = v97 / v81;                          // dx/dy
      INC_HIWORD(v114);                           // Adjust for rounding
    }

    // Initialize right edge
    while (1) {
      v22 = --iMinYIdx2;                        // Move to previous vertex clockwise
      if ((int16)iMinYIdx2 == -1)           // Wrap vertex idx
        iMinYIdx2 = iVertexCount - 1;
      pVertex = &vertices[(int16)iMinYIdx2];
      // Calculate scanlines until next vertex
      SET_LOWORD(v22, GET_LOWORD(pVertex->y) - iScanlineY);
      iLeftRemaining_1 = v22;
      if ((int16)v22)                         // If edge has visible length
        break;
      // Skip horizontal edges
      if ((int16)iMinYIdx == (int16)iMinYIdx2) {
        SET_HIWORD(v114, v20);                     // Restore saved left X
        goto FINAL_SCANLINE_CHECK;
      }
      SET_HIWORD(iLeftXFixed, pVertex->x);         // Update current X
    }
    // Calculate right edge slope (dx/dy in fixed point)
    SET_HIWORD(v105, GET_LOWORD(pVertex->x) - GET_HIWORD(iLeftXFixed));// dx
    if (v105 <= 0) {
      DEC_HIWORD(v105);                           // Adjust for negative slope
      v104 = v105 / ((int16)v22 + 1);         // dx/dy
      INC_HIWORD(iLeftXFixed);                    // Adjust for rounding
    } else {
      INC_HIWORD(v105);                           // Adjust for positive slope
      v24 = (int16)v22 + 1;
      v25 = v105 / v24 + iLeftXFixed;           // Udpate X with quotient
      v104 = v105 / v24;                        // dx/dy
      iLeftXFixed = v25;                        // Store updated X
    }
  } else {
    // Case 2: starting above top of screen

    // Initialize left edge above screen
    iY = pTopVertex->y;
    v94 = iY;                                   // Save starting Y
    SET_LOWORD(v103, 0);                           // Clear loword
    SET_LOWORD(v95, 0);                            // Clear loword
    SET_LOWORD(v113, 0);                           // Clear loword
    SET_LOWORD(iLeftX, -1);                        // Initialize fraction for right edge

    // Find first vertex at or below screen top
    do {
      nX = vertices[(int16)iMinYIdx++].x;
      SET_HIWORD(v113, nX);                        // Store current X
      if ((int16)iMinYIdx == (int16)iVertexCount)
        iMinYIdx ^= iVertexCount;
      y = vertices[(int16)iMinYIdx].y;
      v13 = v94;
      v94 = y;                                  // Update current Y
    } while (y < 0);                            // Until Y >= 0

    // Handle case where we land exactly on screen top
    if (!y) {
      do {
        nX_1 = vertices[(int16)iMinYIdx++].x;
        SET_HIWORD(v113, nX_1);
        if ((int16)iMinYIdx == (int16)iVertexCount)
          iMinYIdx ^= iVertexCount;
        v15 = vertices[(int16)iMinYIdx].y;
        v13 = v94;
        v94 = v15;
      } while (!v15);                           // Skip horizontal edges
      if (v15 < 0)
        goto CLEANUP_AND_RETURN;                // All vertices above screen
    }
    // Calculate left edge slope (dx/dy)
    SET_HIWORD(v95, GET_LOWORD(vertices[(int16)iMinYIdx].x) - GET_HIWORD(v113));
    iLeftRemaining = v94;
    if (v95 >= 0) {
      INC_HIWORD(v95);
      v96 = v95 / ((int16)v94 - v13 + 1);     // dx/dy
    } else {
      DEC_HIWORD(v95);
      v80 = (int16)v94 - v13 + 1;
      v113 += v95 / v80;                        // Update X with quotient
      v96 = v95 / v80;                          // dx/dy
      INC_HIWORD(v113);                           // Adjust for rounding
    }
    // Extrapolate left edge to Y=0
    v114 = v113 - v96 * v13;

    // Initialize right edge above screen
    do {
      SET_HIWORD(iLeftX, vertices[(int16)iMinYIdx2--].x);
      if ((int16)iMinYIdx2 == -1)
        iMinYIdx2 = iVertexCount - 1;
      v16 = iY;
      iY = vertices[(int16)iMinYIdx2].y;
    } while (iY < 0);                           // Until Y >= 0

    // Handle case where we land exactly on screen top
    if (!iY) {
      do {
        SET_HIWORD(iLeftX, vertices[(int16)iMinYIdx2--].x);
        if ((int16)iMinYIdx2 == -1)
          iMinYIdx2 = iVertexCount - 1;
        v16 = iY;
        iY = vertices[(int16)iMinYIdx2].y;
      } while (!iY);                            // Skip horizontal edges
      if (iY < 0)
        goto CLEANUP_AND_RETURN;                // All vertices above screen
    }
    // Calculate right edge slope (dx/dy)
    SET_HIWORD(v103, GET_LOWORD(vertices[(int16)iMinYIdx2].x) - GET_HIWORD(iLeftX));
    iLeftRemaining_1 = iY;
    if (v103 <= 0) {
      DEC_HIWORD(v103);
      v104 = v103 / ((int16)iY - v16 + 1);    // dx/dy
      INC_HIWORD(iLeftX);                         // Adjust for rounding
    } else {
      INC_HIWORD(v103);
      v17 = (int16)iY - v16 + 1;
      v18 = v103 / v17 + iLeftX;                // Update X with quotient
      v104 = v103 / v17;                        // dx/dy
      iLeftX = v18;                             // Store updated X
    }
    // Extrapolate right edge to Y=0
    iLeftXFixed = iLeftX - v104 * v16;
    iScanlineY = 0;                             // Start at top of screen
  }

  // Main scanline loop - skip lines until we enter visible area
  while (GET_SHIWORD(v114) >= winw || GET_SHIWORD(iLeftXFixed) <= 0) {
    v26 = v96 + v114;                           // Advance left edge
    ++iScanlineY;                               // Move to next scanline
    iLeftXFixed += v104;                        // Advance right edge
    SET_HIWORD(v27, GET_HIWORD(iLeftRemaining));       // Unused
    v114 += v96;                                // Commit left edge advance
    SET_LOWORD(v27, iLeftRemaining - 1);           // Decrement edge length
    bZeroFlag = (int16)iLeftRemaining == 1;
    iLeftRemaining = v27;                       // Update remaining scanlines

    // Handle left edge completion
    if (bZeroFlag) {
      while ((int16)iMinYIdx != (int16)iMinYIdx2) {
        nv29 = vertices[(int16)iMinYIdx++].x; // Next vertex X
        SET_HIWORD(v114, nv29);                    // Set new X
        if ((int16)iMinYIdx == (int16)iVertexCount)
          iMinYIdx ^= iVertexCount;             // Wrap index
        pVertex_1 = &vertices[(int16)iMinYIdx];
        SET_LOWORD(v26, pVertex_1->y);             // Next vertex Y
        v26 -= iScanlineY;                      // Remaining scanlines
        iLeftRemaining = v26;
        if ((int16)v26 > 0)                 // valid edge segment
        {
          SET_HIWORD(v98, GET_LOWORD(pVertex_1->x) - GET_HIWORD(v114));// dx
          SET_LOWORD(v98, 0);                      // clear fraction
          SET_LOWORD(v114, 0);                     // clear fraction
          if (v98 >= 0) {
            INC_HIWORD(v98);                      // Adjust positive slope
            v32 = (int16)v26 + 1;
            v26 = v98 % v32;                    // remainder (unused)
            v96 = v98 / v32;                    // dx/dy
          } else {
            DEC_HIWORD(v98);                      // Adjust negative slope
            v31 = (int16)v26 + 1;
            v26 = v98 % v31;                    // Remainder (unused)
            v114 += v98 / v31;                  // Update X
            v96 = v98 / v31;                    // dx/dy
            INC_HIWORD(v114);                     // Adjust for rounding
          }
          goto UPDATE_RIGHT_EDGE;               // Process right edge next
        }
        if ((v26 & 0x8000u) != 0)             // Negative = done
          goto CLEANUP_AND_RETURN;
      }
      goto CLEANUP_AND_RETURN;                  // No more vertices
    }
  UPDATE_RIGHT_EDGE:
    SET_HIWORD(v33, GET_HIWORD(iLeftRemaining_1));     // Unused
    SET_LOWORD(v33, iLeftRemaining_1 - 1);         // Decrement edge length
    bZeroFlag = (int16)iLeftRemaining_1 == 1;
    iLeftRemaining_1 = v33;                     // Update remaining scanlines

    // Handle right edge completion
    if (bZeroFlag) {
      while (1) {
        nv34 = iMinYIdx2;                       // Current index
        if ((int16)iMinYIdx == (int16)iMinYIdx2)
          goto CLEANUP_AND_RETURN;              // Back to start vertex
        SET_HIWORD(iLeftXFixed, vertices[(int16)iMinYIdx2--].x);// Prev vertex
        if ((int16)(nv34 - 1) == -1)
          iMinYIdx2 = iVertexCount - 1;         // Wrap index
        pVertex_2 = &vertices[(int16)iMinYIdx2];
        SET_LOWORD(v26, pVertex_2->y);             // Next vertex Y
        v26 -= iScanlineY;                      // Remaining scanlines
        iLeftRemaining_1 = v26;
        if ((int16)v26 > 0)                 // Valid edge segment
        {
          SET_HIWORD(v106, GET_LOWORD(pVertex_2->x) - GET_HIWORD(iLeftXFixed));// dx
          SET_LOWORD(v106, 0);                     // clear fraction
          SET_LOWORD(iLeftXFixed, -1);             // reset fraction
          if (v106 <= 0) {
            DEC_HIWORD(v106);                     // Adjust for negative slope
            v104 = v106 / ((int16)v26 + 1);   // dx/dy
            INC_HIWORD(iLeftXFixed);              // Adjust for rounding
          } else {
            INC_HIWORD(v106);                     // Adjust for positive slope
            v104 = v106 / ((int16)v26 + 1);   // dx/dy
            iLeftXFixed += v104;                // Udpate X
          }
          break;
        }
        if ((v26 & 0x8000u) != 0)             // Negative = done
          goto CLEANUP_AND_RETURN;
      }
    }
    if (iScanlineY >= winh)                   // Past bottom of screen
      goto CLEANUP_AND_RETURN;
  }                                             // 
                                                // 
  while (1) {
    // Main drawing loop

    // Handle clipping
    if (GET_SHIWORD(v114) < iOldWinW)             // left edge visible
    {
      if (GET_SHIWORD(iLeftXFixed) <= iOldWinW)   // right edge visible
        goto DRAW_SCANLINE_SEGMENT;
      goto HANDLE_RIGHT_CLIPPING;
    }
    if (GET_SHIWORD(v114) >= (int)iOldWinW)       // left edge off right
      goto CLEANUP_AND_RETURN;
    if (GET_SHIWORD(iLeftXFixed) > iOldWinW)      // right edge off right
    {
    HANDLE_RIGHT_CLIPPING_2:
      if (GET_SHIWORD(iLeftXFixed) > 0)
        goto DRAW_RIGHT_CLIPPED;
      goto CLEANUP_AND_RETURN;
    }
    while (1) {
      // Draw visible scanline segment
    DRAW_SCANLINE_SEGMENT_2:
      if (GET_SHIWORD(iLeftXFixed) > 0)           // right edge visible
      {
        winw = iOldWinW;
        // Fill entire scanline segment
        memset(&scrptr[iOldWinW * iScanlineY], nColor, GET_SHIWORD(iLeftXFixed));
        iOldWinW = winw;
      } else if (iLeftXFixed < 0)               // Right edge off left
      {
        goto CLEANUP_AND_RETURN;
      }
      // Advance to next scanline
      ++iScanlineY;
      v47 = v96 + v114;                         // advance left edge
      v114 = v47;                               // commit advance
      SET_HIWORD(v48, GET_HIWORD(iLeftRemaining));     // unused
      iLeftXFixed += v104;                      // advance right edge
      nv49 = GET_HIWORD(v47);                       // save new left x
      SET_LOWORD(v48, iLeftRemaining - 1);         // decrement edge length
      bZeroFlag = (int16)iLeftRemaining == 1;
      iLeftRemaining = v48;                     // update remaining

      // Handle left edge completion
      if (bZeroFlag) {
        if ((int16)iMinYIdx == (int16)iMinYIdx2)// back to start
        {
          SET_HIWORD(v114, GET_HIWORD(v47));           // restore X
        FINAL_SCANLINE_CHECK_2:
          if (iScanlineY >= winh)             // past bottom
            goto CLEANUP_AND_RETURN;
          if (GET_SHIWORD(v114) < iOldWinW)       // left visible
          {
            if (GET_SHIWORD(v114) >= (int)iOldWinW)// left off right
              goto CLEANUP_AND_RETURN;
            if (GET_SHIWORD(iLeftXFixed) <= iOldWinW)// right visible
              goto DRAW_FINAL_SCANLINE_2;
            goto HANDLE_RIGHT_CLIPPING_FINAL;
          }
          if (GET_SHIWORD(iLeftXFixed) <= iOldWinW)// right visible
          {
          DRAW_LEFT_CLIPPED:
            if (GET_SHIWORD(iLeftXFixed) > 0)     // right edge on screen
            {
              v58 = iOldWinW * iScanlineY;      // screen offset
              iColor = nColor;
              pScrBuf = scrptr;
              iPixelCount = GET_SHIWORD(iLeftXFixed);
              goto FILL_SCANLINE;
            }
            goto CLEANUP_AND_RETURN;            // nothing to draw
          }
        LABEL_253:
          if (GET_SHIWORD(iLeftXFixed) <= 0)      // Right edge beyond screen right
            goto CLEANUP_AND_RETURN;
          goto FILL_FULL_WIDTH;
        }
        // Continue processing left edge after completion
        while (1) {
          SET_HIWORD(v50, GET_HIWORD(iVertexCount));   // unused
          nv51 = vertices[(int16)iMinYIdx++].x;// next vertex X
          SET_HIWORD(v114, nv51);                  // update left edge x
          // wrap vertex index
          if ((int16)iMinYIdx == (int16)iVertexCount)
            iMinYIdx ^= iVertexCount;
          pVertex_3 = &vertices[(int16)iMinYIdx];
          SET_LOWORD(v50, pVertex_3->y);           // next vertex y
          v53 = v50 - iScanlineY;               // scanlines in new edge
          iLeftRemaining = v53;                 // store new edge length
          // Check if edge has positive length
          if ((int16)v53 > 0)
            break;
          // Skip negative/zero length edges
          if ((v53 & 0x8000u) != 0)           // negative length
            goto CLEANUP_AND_RETURN;
          // Check if back to start vertex
          if ((int16)iMinYIdx == (int16)iMinYIdx2) {
            SET_HIWORD(v114, nv49);                // restore left X
            goto FINAL_SCANLINE_CHECK_2;
          }
        }
        // Calculate new left edge slope
        SET_HIWORD(v100, GET_LOWORD(pVertex_3->x) - GET_HIWORD(v114));// dx
        SET_LOWORD(v100, 0);                       // clear fraction
        SET_LOWORD(v114, 0);                       // clear fraction
        if (v100 >= 0) {
          INC_HIWORD(v100);                       // Adjust for positive slope
          v96 = v100 / ((int16)v53 + 1);      // dx/dy
        } else {
          DEC_HIWORD(v100);                       // Adjust for negative slope
          v83 = (int16)v53 + 1;
          v114 += v100 / v83;                   // update X
          v96 = v100 / v83;                     // dx/dy
          INC_HIWORD(v114);                       // adjust for rounding
        }
      }
      // Update right edge tracking
      SET_HIWORD(v54, GET_HIWORD(iLeftRemaining_1));   // unused
      nRightXStep = GET_HIWORD(iLeftXFixed);        // save current right x
      SET_LOWORD(v54, iLeftRemaining_1 - 1);       // decrement edge length
      bZeroFlag = (int16)iLeftRemaining_1 == 1;
      iLeftRemaining_1 = v54;                   // update remaining scanlines

      // Handle right edge completion
      if (bZeroFlag) {
        while (1) {
          v55 = iMinYIdx2;                      // current index
          // Check if back to start vertex
          if ((int16)iMinYIdx == (int16)iMinYIdx2) {
            SET_HIWORD(v114, nv49);                // restore left X
            SET_HIWORD(iLeftXFixed, nRightXStep);  // restore right X
            goto FINAL_SCANLINE_CHECK_2;
          }
          // Move to previous vertex
          SET_HIWORD(iLeftXFixed, vertices[(int16)iMinYIdx2--].x);
          // Wrap index
          if ((int16)(v55 - 1) == -1)
            iMinYIdx2 = iVertexCount - 1;
          pVertex_4 = &vertices[(int16)iMinYIdx2];
          SET_LOWORD(v55, pVertex_4->y);           // next vertex y
          v57 = v55 - iScanlineY;               // scanlines in new edge
          iLeftRemaining_1 = v57;               // store new edge length
          // Check if edge has positive length
          if ((int16)v57 > 0)
            break;
          // skip negative/zero length edges
          if ((v57 & 0x8000u) != 0)           // negative length
            goto CLEANUP_AND_RETURN;
        }
        // Calcualte new right edge slope
        SET_HIWORD(v108, GET_LOWORD(pVertex_4->x) - GET_HIWORD(iLeftXFixed));// dx
        SET_LOWORD(v108, 0);                       // clear fraction
        SET_LOWORD(iLeftXFixed, -1);               // reset fraction
        if (v108 <= 0) {
          DEC_HIWORD(v108);                       // adjust for negative slope
          v104 = v108 / ((int16)v57 + 1);     // dx/dy
          INC_HIWORD(iLeftXFixed);                // adjust for rounding
        } else {
          INC_HIWORD(v108);                       // adjust for positive slope
          v104 = v108 / ((int16)v57 + 1);     // dx/dy
          iLeftXFixed += v104;                  // update X
        }
      }
      // Check if below screen bottom
      if (iScanlineY >= winh) {
        winw = iOldWinW;
        return;
      }
      // Check if left edge visible
      if (GET_SHIWORD(v114) < iOldWinW)
        break;                                  // enter drawing phase
      // handle right edge beyond screen right
      if (GET_SHIWORD(iLeftXFixed) > iOldWinW)
        goto HANDLE_RIGHT_CLIPPING_2;
    }
    if (GET_SHIWORD(v114) >= (int)iOldWinW)       // left edge beyond right
      goto CLEANUP_AND_RETURN;
    if (GET_SHIWORD(iLeftXFixed) <= iOldWinW)     // right edge visible
      goto DRAW_SCANLINE_SEGMENT;
  HANDLE_RIGHT_CLIPPING:
    if (GET_SHIWORD(iLeftXFixed) <= 0)            // right edge not visible
      goto CLEANUP_AND_RETURN;
  FILL_RIGHT_CLIPPED:

      // Draw from left edge to screen right
    winw = iOldWinW;
    pDest = &scrptr[GET_SHIWORD(v114) + iOldWinW * iScanlineY++];
    memset(pDest, nColor, iOldWinW - GET_SHIWORD(v114));
    iOldWinW = winw;
    v61 = v96 + v114;                           // advance left edge
    v114 = v61;
    SET_HIWORD(v62, GET_HIWORD(iLeftRemaining));       // unused
    iLeftXFixed += v104;                        // advance right edge
    nv63 = GET_HIWORD(v61);                         // save new left x
    SET_LOWORD(v62, iLeftRemaining - 1);           // decrement edge length
    bZeroFlag = (int16)iLeftRemaining == 1;
    iLeftRemaining = v62;                       // update remaining

    // Handle left edge completion
    if (bZeroFlag) {
      if ((int16)iMinYIdx == (int16)iMinYIdx2)// back to start
      {
        SET_HIWORD(v114, GET_HIWORD(v61));             // restore left x
      SCANLINE_END_CHECK:
        if (iScanlineY >= winh)               // past bottom
          goto CLEANUP_AND_RETURN;
        if (GET_SHIWORD(v114) < winw)// left visible
        {
          if (GET_SHIWORD(iLeftXFixed) > winw)// right clipped
            goto HANDLE_RIGHT_CLIPPING_FINAL_2;
        DRAW_FINAL_SCANLINE:
          if (GET_SHIWORD(iLeftXFixed) <= 0)      // right not visible
            goto CLEANUP_AND_RETURN;
          goto DRAW_FINAL_SCANLINE_2;
        }
        if (GET_SHIWORD(v114) >= winw)
          goto CLEANUP_AND_RETURN;
      HANDLE_LEFT_CLIPPING:
              // Handle left edge beyond right but right visible
        if (GET_SHIWORD(iLeftXFixed) <= iOldWinW) {
          if (GET_SHIWORD(iLeftXFixed) <= 0)      // right not visible
            goto CLEANUP_AND_RETURN;            // draw from 0 to right edge
          goto DRAW_LEFT_CLIPPED;
        }
      FILL_FULL_WIDTH:
              // Fill entire screen width
        v58 = iOldWinW * iScanlineY;            // screen offset
        iColor = nColor;
        pScrBuf = scrptr;
        iPixelCount = iOldWinW;                 // full width
        goto FILL_SCANLINE;
      }
      // Find next valid left edge
      while (1) {
        SET_HIWORD(v64, GET_HIWORD(iVertexCount));     // unused
        nv65 = vertices[(int16)iMinYIdx++].x; // next vertex x
        SET_HIWORD(v114, nv65);                    // update left x
        // wrap index
        if ((int16)iMinYIdx == (int16)iVertexCount)
          iMinYIdx ^= iVertexCount;
        pVertex_5 = &vertices[(int16)iMinYIdx];
        SET_LOWORD(v64, pVertex_5->y);             // next vertex y
        v67 = v64 - iScanlineY;                 // scanlines in edge
        iLeftRemaining = v67;                   // store edge length
        // check for valid edge
        if ((int16)v67 > 0)
          break;
        // skip negative/zero length edges
        if ((v67 & 0x8000u) != 0)             // negative length
          goto CLEANUP_AND_RETURN;
        // check if back to start
        if ((int16)iMinYIdx == (int16)iMinYIdx2) {
          SET_HIWORD(v114, nv63);                  // restore left x
          goto SCANLINE_END_CHECK;
        }
      }
      // calculate new left edge slope
      SET_HIWORD(v101, GET_LOWORD(pVertex_5->x) - GET_HIWORD(v114));// dx
      SET_LOWORD(v101, 0);                         // clear fraction
      SET_LOWORD(v114, 0);                         // clear fraction
      if (v101 >= 0) {
        INC_HIWORD(v101);                         // adjust for positive slope
        v96 = v101 / ((int16)v67 + 1);        // dx/dy
      } else {
        DEC_HIWORD(v101);                         // adjust for negative slope
        v84 = (int16)v67 + 1;
        v114 += v101 / v84;                     // update x
        v96 = v101 / v84;                       // dx/dy
        INC_HIWORD(v114);                         // adjust for rounding
      }
    }
    // Update right edge tracking
    SET_HIWORD(v68, GET_HIWORD(iLeftRemaining_1));     // unused
    nv89 = GET_HIWORD(iLeftXFixed);                 // save right x
    SET_LOWORD(v68, iLeftRemaining_1 - 1);         // decrement edge length
    bZeroFlag = (int16)iLeftRemaining_1 == 1;
    iLeftRemaining_1 = v68;                     // udpate remaining

    // handle right edge completion
    if (!bZeroFlag)
      goto CONTINUE_SCANLINE;                   // continue normally

    // find next valid right edge
    while (1) {
      v69 = iMinYIdx2;                          // current index
      // check if back to start
      if ((int16)iMinYIdx == (int16)iMinYIdx2) {
        SET_HIWORD(v114, nv63);                    // restore left x
        SET_HIWORD(iLeftXFixed, nv89);             // restore right x
        goto SCANLINE_END_CHECK;
      }
      // move to previous vertex
      SET_HIWORD(iLeftXFixed, vertices[(int16)iMinYIdx2--].x);
      // wrap index
      if ((int16)(v69 - 1) == -1)
        iMinYIdx2 = iVertexCount - 1;
      pVertex_6 = &vertices[(int16)iMinYIdx2];
      SET_LOWORD(v69, pVertex_6->y);               // next vertex y
      v71 = v69 - iScanlineY;                   // scanlines in edge
      iLeftRemaining_1 = v71;                   // store edge length
      // check for valid edge
      if ((int16)v71 > 0)
        break;
      // skip negative/zero length edges
      if ((v71 & 0x8000u) != 0)               // negative length
        goto CLEANUP_AND_RETURN;
    }
    // calculate new right edge slope
    SET_HIWORD(v109, GET_LOWORD(pVertex_6->x) - GET_HIWORD(iLeftXFixed));// dx
    SET_LOWORD(v109, 0);                           // clear fraction
    SET_LOWORD(iLeftXFixed, -1);                   // reset fraction
    if (v109 <= 0) {
      DEC_HIWORD(v109);                           // adjust negative slope
      v104 = v109 / ((int16)v71 + 1);         // dx/dy
      INC_HIWORD(iLeftXFixed);                    // adjust for rounding
    } else {
      INC_HIWORD(v109);                           // adjust positive slope
      v104 = v109 / ((int16)v71 + 1);         // dx/dy
      iLeftXFixed += v104;                      // update x
    }
  CONTINUE_SCANLINE:
      // check if below screen bottom
    if (iScanlineY >= winh)
      return;
    // check if left edge beyond right
    if (GET_SHIWORD(v114) >= winw)
      break;
    // check if right edge beyond right
    if (GET_SHIWORD(iLeftXFixed) > winw)
      goto FILL_RIGHT_CLIPPED;                  // fill from left to screen right
  DRAW_SCANLINE:                                  // draw between left and right edges
    if (GET_SHIWORD(iLeftXFixed) <= 0)            // right edge not visible
      goto CLEANUP_AND_RETURN;
  DRAW_SCANLINE_SEGMENT:
      // calculate span between edges
    if ((int16)(GET_HIWORD(iLeftXFixed) - GET_HIWORD(v114)) > 0) {
      winw = iOldWinW;
      // fill between edges
      memset(&scrptr[GET_SHIWORD(v114) + iOldWinW * iScanlineY], nColor, (int16)(GET_HIWORD(iLeftXFixed) - GET_HIWORD(v114)));
      iOldWinW = winw;
    }
    // Handle inverted edges (shouldn't happen)
    else if ((int16)(GET_HIWORD(iLeftXFixed) - GET_HIWORD(v114)) < 0) {
      goto CLEANUP_AND_RETURN;
    }
    // Advance edges
    v114 += v96;                                // advance left edge
    iLeftXFixed += v104;                        // advance right edge
    nv36 = GET_HIWORD(v114);                        // save new left x
    SET_HIWORD(v37, GET_HIWORD(iLeftRemaining));       // unused
    ++iScanlineY;                               // next scanline
    SET_LOWORD(v37, iLeftRemaining - 1);           // decrement edge length
    bZeroFlag = (int16)iLeftRemaining == 1;
    iLeftRemaining = v37;                       // update remaining

    // handle left edge completion
    if (bZeroFlag) {
      if ((int16)iMinYIdx != (int16)iMinYIdx2) {
        // find next valid left edge
        while (1) {
          nv38 = vertices[(int16)iMinYIdx++].x;// next vertex x
          SET_HIWORD(v114, nv38);                  // update left x
          if ((int16)iMinYIdx == (int16)iVertexCount)
            iMinYIdx ^= iVertexCount;
          pVertex_7 = &vertices[(int16)iMinYIdx];
          SET_LOWORD(v37, pVertex_7->y);           // next vertex y
          v37 -= iScanlineY;                    // scanlines in edge
          iLeftRemaining = v37;                 // store edge length
          // check for valid edge
          if ((int16)v37 > 0)
            break;
          // skip negative/zero length edges
          if ((v37 & 0x8000u) != 0)           // negative length
            goto CLEANUP_AND_RETURN;
          // check if back to start
          if ((int16)iMinYIdx == (int16)iMinYIdx2) {
            SET_HIWORD(v114, nv36);                // restore left x
            goto FINAL_SCANLINE_CHECK;
          }
        }
        // calculate new left edge slope
        SET_HIWORD(v99, GET_LOWORD(pVertex_7->x) - GET_HIWORD(v114));// dx
        SET_LOWORD(v99, 0);                        // clear fraction
        SET_LOWORD(v114, 0);                       // clear fraction
        if (v99 >= 0) {
          INC_HIWORD(v99);                        // adjust positive slope
          v96 = v99 / ((int16)v37 + 1);       // dx/dy
        } else {
          DEC_HIWORD(v99);                        // adjust negative slope
          v82 = (int16)v37 + 1;
          v114 += v99 / v82;                    // update x
          v96 = v99 / v82;                      // dx/dy
          INC_HIWORD(v114);                       // adjust for rounding
        }
        goto UPDATE_RIGHT_EDGE_STEP;            // process right edge
      }
    FINAL_SCANLINE_CHECK:
      if (iScanlineY >= winh)                 // past bottom
        goto CLEANUP_AND_RETURN;
      if (GET_SHIWORD(v114) < iOldWinW)           // left visible
      {
        if (GET_SHIWORD(iLeftXFixed) <= iOldWinW) // right visible
        {
          // draw between edges if valid span
        DRAW_FINAL_SCANLINE_2:
          if ((int16)(GET_HIWORD(iLeftXFixed) - GET_HIWORD(v114)) > 0) {
            iPixelCount = (int16)(GET_HIWORD(iLeftXFixed) - GET_HIWORD(v114));
            iColor = nColor;
            pDest_1 = &scrptr[GET_SHIWORD(v114) + iOldWinW * iScanlineY];
            goto FILL_SCANLINE_FINAL;
          }
          goto CLEANUP_AND_RETURN;
        }
      HANDLE_RIGHT_CLIPPING_FINAL:
              // handle right edge beyond right
        if (GET_SHIWORD(iLeftXFixed) > 0)
          goto HANDLE_RIGHT_CLIPPING_FINAL_2;
        goto CLEANUP_AND_RETURN;
      }
      // left edge beyond right
      if (GET_SHIWORD(v114) >= (int)iOldWinW)
        goto CLEANUP_AND_RETURN;
      // right edge clipped
      if (GET_SHIWORD(iLeftXFixed) <= iOldWinW)
        goto DRAW_LEFT_CLIPPED;
      goto LABEL_253;
    }
  UPDATE_RIGHT_EDGE_STEP:
      // udpate right edge tracking
    SET_HIWORD(v40, GET_HIWORD(iLeftRemaining_1));     // unused
    nLeftXStep = GET_HIWORD(iLeftXFixed);           // save right x
    SET_LOWORD(v40, iLeftRemaining_1 - 1);         // decrement edge length
    bZeroFlag = (int16)iLeftRemaining_1 == 1;
    iLeftRemaining_1 = v40;                     // update remaining

    // handle right edge completion
    if (bZeroFlag) {
      while (1) {
        v41 = iMinYIdx2;                        // current index
        // check if back to start
        if ((int16)iMinYIdx == (int16)iMinYIdx2) {
          SET_HIWORD(v114, nv36);                  // restore left x
          SET_HIWORD(iLeftXFixed, nLeftXStep);     // restore right x
          goto FINAL_SCANLINE_CHECK;
        }
        // move to prev vertex
        SET_HIWORD(iLeftXFixed, vertices[(int16)iMinYIdx2--].x);
        // wrap index
        if ((int16)(v41 - 1) == -1)
          iMinYIdx2 = iVertexCount - 1;
        pVertex_8 = &vertices[(int16)iMinYIdx2];
        SET_LOWORD(v41, pVertex_8->y);             // next vertex y
        v43 = v41 - iScanlineY;                 // scanlines in edge
        iLeftRemaining_1 = v43;                 // store edge length
        // check for valid edge
        if ((int16)v43 > 0)
          break;
        // skip negative/zero length edges
        if ((v43 & 0x8000u) != 0)             // negative length
          goto CLEANUP_AND_RETURN;
      }
      // calculate new right edge slope
      SET_HIWORD(v107, GET_LOWORD(pVertex_8->x) - GET_HIWORD(iLeftXFixed));// dx
      SET_LOWORD(v107, 0);                         // clear fraction
      SET_LOWORD(iLeftXFixed, -1);                 // reset fraction
      if (v107 <= 0) {
        DEC_HIWORD(v107);                         // adjust negative slope
        v104 = v107 / ((int16)v43 + 1);       // dx/dy
        INC_HIWORD(iLeftXFixed);                  // adjust for rounding
      } else {
        INC_HIWORD(v107);                         // adjust positive slope
        v104 = v107 / ((int16)v43 + 1);       // dx/dy
        iLeftXFixed += v104;                    // update x
      }
    }
    // check if below screen bottom
    if (iScanlineY >= winh) {
      winw = iOldWinW;
      return;
    }
  }
  // exit if left edge beyond right
  if (GET_SHIWORD(v114) >= winw)
    goto CLEANUP_AND_RETURN;
  // handle right edge beyond right
  if (GET_SHIWORD(iLeftXFixed) <= winw) {
  LABEL_135:
    if (GET_SHIWORD(iLeftXFixed) > 0)             // right edge visible
      goto DRAW_SCANLINE_SEGMENT_2;
    goto CLEANUP_AND_RETURN;
  }
  while (1) {
    // fill entire scanline width
  DRAW_RIGHT_CLIPPED:
    winw = iOldWinW;
    memset(&scrptr[iOldWinW * iScanlineY++], nColor, iOldWinW);
    v114 += v96;                                // advance left edge
    iLeftXFixed += v104;                        // advance right edge
    nv72 = GET_HIWORD(v114);                        // save new left x
    SET_HIWORD(v73, GET_HIWORD(iLeftRemaining));       // unused
    iOldWinW = winw;
    SET_LOWORD(v73, iLeftRemaining - 1);           // decrement edge length
    bZeroFlag = (int16)iLeftRemaining == 1;
    iLeftRemaining = v73;                       // update remaining

    // handle left edge completion
    if (!bZeroFlag)
      goto UPDATE_LEFT_EDGE;                    // continue normally

    // back to start
    if ((int16)iMinYIdx == (int16)iMinYIdx2)
      break;

    // find next valid left edge
    while (1) {
      nv74 = vertices[(int16)iMinYIdx++].x;   // next vertex x
      SET_HIWORD(v114, nv74);                      // update left x
      // wrap index
      if ((int16)iMinYIdx == (int16)iVertexCount)
        iMinYIdx ^= iVertexCount;
      pVertex_9 = &vertices[(int16)iMinYIdx];
      SET_LOWORD(v73, pVertex_9->y);               // next vertex y
      v73 -= iScanlineY;                        // scanlines in edge
      iLeftRemaining = v73;                     // store edge length
      // check for valid edge
      if ((int16)v73 > 0)
        break;
      // skip negative/zero length edges
      if ((v73 & 0x8000u) != 0)               // negative length
        goto CLEANUP_AND_RETURN;
      // check if back at start
      if ((int16)iMinYIdx == (int16)iMinYIdx2) {
        SET_HIWORD(v114, nv72);                    // restore left x
        goto SCANLINE_END_CHECK_2;
      }
    }
    // calculate new left edge slope
    SET_HIWORD(v102, GET_LOWORD(pVertex_9->x) - GET_HIWORD(v114));// dx
    SET_LOWORD(v102, 0);                           // clear fraction
    SET_LOWORD(v114, 0);                           // clear fraction
    if (v102 >= 0) {
      INC_HIWORD(v102);                           // adjust positive slope
      v96 = v102 / ((int16)v73 + 1);          // dx/dy
    } else {
      DEC_HIWORD(v102);                           // adjust negative slope
      v85 = (int16)v73 + 1;
      v114 += v102 / v85;                       // update x
      v96 = v102 / v85;                         // dx/dy
      INC_HIWORD(v114);                           // adjust for rounding
    }
  UPDATE_LEFT_EDGE:
      // Update right edge tracking
    SET_HIWORD(v76, GET_HIWORD(iLeftRemaining_1));     // unused
    nv88 = GET_HIWORD(iLeftXFixed);                 // save right x
    SET_LOWORD(v76, iLeftRemaining_1 - 1);         // decrement edge length
    bZeroFlag = (int16)iLeftRemaining_1 == 1;
    iLeftRemaining_1 = v76;                     // update remaining

    // handle right edge completion
    if (bZeroFlag) {
      while (1) {
        v77 = iMinYIdx2;                        // current index
        // check if back to start
        if ((int16)iMinYIdx == (int16)iMinYIdx2) {
          SET_HIWORD(v114, nv72);                  // restore left x
          SET_HIWORD(iLeftXFixed, nv88);           // restore right x
          goto SCANLINE_END_CHECK_2;
        }
        // move to prev vertex
        SET_HIWORD(iLeftXFixed, vertices[(int16)iMinYIdx2--].x);
        // wrap index
        if ((int16)(v77 - 1) == -1)
          iMinYIdx2 = iVertexCount - 1;
        pVertex_10 = &vertices[(int16)iMinYIdx2];
        SET_LOWORD(v77, pVertex_10->y);            // next vertex y
        v79 = v77 - iScanlineY;                 // scanlines in edge
        iLeftRemaining_1 = v79;                 // store edge length
        // check for valid edge
        if ((int16)v79 > 0)
          break;
        // skip negative/zero length edges
        if ((v79 & 0x8000u) != 0)             // negative length
          goto CLEANUP_AND_RETURN;
      }
      // calculate new right edge slope
      SET_HIWORD(v110, GET_LOWORD(pVertex_10->x) - GET_HIWORD(iLeftXFixed));// dx
      SET_LOWORD(v110, 0);                         // clear fraction
      SET_LOWORD(iLeftXFixed, -1);                 // reset fraction
      if (v110 <= 0) {
        DEC_HIWORD(v110);                         // adjust negative slope
        v104 = v110 / ((int16)v79 + 1);       // dx/dy
        INC_HIWORD(iLeftXFixed);                  // adjust for rounding
      } else {
        INC_HIWORD(v110);                         // adjust positive slope
        v104 = v110 / ((int16)v79 + 1);       // dx/dy
        iLeftXFixed += v104;                    // update x
      }
    }
    // check if below screen bottom
    if (iScanlineY >= winh)
      return;
    // check visibility and draw
    if (GET_SHIWORD(v114) < winw) {
      if (GET_SHIWORD(v114) >= winw)              // left beyond right
        goto CLEANUP_AND_RETURN;
      if (GET_SHIWORD(iLeftXFixed) <= winw)// right visible
        goto DRAW_SCANLINE;
      goto FILL_RIGHT_CLIPPED;
    }
    // handle left edge clipped
    if (GET_SHIWORD(iLeftXFixed) <= winw)
      goto LABEL_135;                           // draw from 0 to right edge
  }
SCANLINE_END_CHECK_2:
  // final visibility checks
  if (iScanlineY >= winh)                     // past bottom
    goto CLEANUP_AND_RETURN;
  if (GET_SHIWORD(v114) >= winw)    // left beyond right
    goto HANDLE_LEFT_CLIPPING;
  if (GET_SHIWORD(v114) >= winw)                  // left beyond right
    goto CLEANUP_AND_RETURN;
  if (GET_SHIWORD(iLeftXFixed) <= winw)// right visible
    goto DRAW_FINAL_SCANLINE;
HANDLE_RIGHT_CLIPPING_FINAL_2:
  // fill from left edge to screen right

  //pScrBuf = (uint8 *)(iOldWinW * iScanlineY);
  //iPixelCount = iOldWinW - GET_SHIWORD(v114);
  //v58 = (int)&scrptr[GET_SHIWORD(v114)];
  //iColor = nColor;

  //weird pointer math fixed
  pScrBuf = scrptr + GET_SHIWORD(v114);
  iPixelCount = iOldWinW - GET_SHIWORD(v114);
  v58 = iOldWinW * iScanlineY;
  iColor = nColor;

FILL_SCANLINE:
  pDest_1 = &pScrBuf[v58];
FILL_SCANLINE_FINAL:
  winw = iOldWinW;
  memset(pDest_1, iColor, iPixelCount);
  iOldWinW = winw;
CLEANUP_AND_RETURN:
  winw = iOldWinW;
}*/

//-------------------------------------------------------------------------------------------------
//00071D70
/*void shadow_poly(tPoint *vertices, int iNumVertices, int iPaletteIndex)
{
  int iOldWinW; // ebp
  int iMinX; // edi
  int iMinYIdx; // esi
  int iVertX; // ebx
  int iY; // eax
  tPoint *pMinYVert; // eax
  int iCurrScanline; // ebx
  int iBackwardIdx; // edi
  int16 nTempX; // ax
  int iTempY_1; // eax
  int iTempY_2; // ebx
  int16 nTempX_1; // ax
  int iCalculatedStep_1; // eax
  int iCalculatedStep; // eax
  int iStepOffset; // edx
  int16 nTempX_2; // ax
  int iTempY_3; // eax
  int iTempY_4; // ebx
  int16 nTempX_3; // ax
  int iRightCalcStep; // eax
  int iRightCalculatedStep; // eax
  int iRightStepOffset; // edx
  tPoint *pVertex; // edx
  int iStep_3; // eax
  int iStep; // eax
  int iRemainder_1; // edx
  tPoint *pVertex_1; // eax
  int iStep_1; // eax
  int iRightEdgeUpdate; // edx
  int iTempVal; // eax
  bool bZeroFlag; // zf
  int iVertexX; // edx
  int16 nTempX_4; // ax
  tPoint *pVertex_10; // eax
  int iNewStep; // eax
  int iTempVal_7; // eax
  int16 nTempX_5; // ax
  tPoint *pVertex_2; // eax
  int iStep_4; // eax
  int iStepOffset_1; // edx
  int iVertexIdx_2; // edx
  int16 nTempX_13; // ax
  tPoint *pVertex_9; // eax
  int iNewStep_4; // eax
  int iTempVal_6; // eax
  int16 nTempX_14; // ax
  tPoint *pVertex_8; // eax
  int iStep_7; // eax
  int iPixelIdx_2; // edx
  int iStep_2; // edx
  int nTempX_10; // edx
  int16 nTempX_11; // ax
  tPoint *pVertex_6; // eax
  int iNewStep_3; // eax
  int iTempVal_5; // eax
  int16 nTempX_12; // ax
  tPoint *pVertex_7; // eax
  int iStep_8; // eax
  int iPixelIdx_1; // edx
  int iRightEdgeTemp_1; // edx
  int iTempVal_3; // eax
  int iTempY_5; // edx
  int16 nTempX_8; // ax
  tPoint *pVertex_4; // eax
  int iNewStep_2; // eax
  int iTempVal_4; // eax
  int16 nTempX_9; // ax
  tPoint *pVertex_5; // eax
  int iStep_6; // eax
  int iRightEdgeTemp; // edx
  int iTempVal_1; // eax
  int iVertexIdx_1; // edx
  int16 nTempX_6; // ax
  tPoint *pVertex_11; // eax
  int iNewStep_1; // eax
  int iTempVal_2; // eax
  int16 nTempX_7; // ax
  tPoint *pVertex_3; // eax
  int iStep_5; // eax
  int iMaxY; // [esp+4h] [ebp-6Ch]
  int iMaxX; // [esp+8h] [ebp-68h]
  int iPixelIdx_3; // [esp+10h] [ebp-60h]
  int iPixelIdx; // [esp+14h] [ebp-5Ch]
  char *pShadePalette; // [esp+1Ch] [ebp-54h]
  int iMinY; // [esp+20h] [ebp-50h]
  uint8 *pDest4; // [esp+24h] [ebp-4Ch]
  uint8 *pDest3; // [esp+28h] [ebp-48h]
  uint8 *pDest2; // [esp+2Ch] [ebp-44h]
  uint8 *pDest; // [esp+30h] [ebp-40h]
  int iTempY; // [esp+34h] [ebp-3Ch]
  int iTempY2; // [esp+38h] [ebp-38h]
  int iRemainder; // [esp+40h] [ebp-30h]
  int iRightEdgeDx; // [esp+40h] [ebp-30h]
  int iLeftEdgeDx; // [esp+40h] [ebp-30h]
  int iTempEdgeDx; // [esp+40h] [ebp-30h]
  int iTempEdgeDx2; // [esp+40h] [ebp-30h]
  int iTempEdgeDx3; // [esp+40h] [ebp-30h]
  int iTempEdgeDx4; // [esp+40h] [ebp-30h]
  int iTempEdgeDx5; // [esp+40h] [ebp-30h]
  int iLeftEdgeDxTemp; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDx_1; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp2; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp3; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp4; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp5; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp6; // [esp+44h] [ebp-2Ch]
  int iLeftEdgeDxTemp7; // [esp+44h] [ebp-2Ch]
  int iRightEdgeXTemp; // [esp+48h] [ebp-28h]
  int iRightEdgeX; // [esp+48h] [ebp-28h]
  int iLeftEdgeXTemp; // [esp+4Ch] [ebp-24h]
  int iLeftEdgeX; // [esp+4Ch] [ebp-24h]
  int iVertexIdx; // [esp+50h] [ebp-20h]
  int iRightRemain; // [esp+54h] [ebp-1Ch]
  int iLeftRemain; // [esp+58h] [ebp-18h]
  int16 nSegmentWidth; // [esp+5Ch] [ebp-14h]

  iOldWinW = winw;
  iMinX = vertices->x;
  iMinYIdx = 0;
  iMaxX = vertices->x;
  pShadePalette = &shade_palette[256 * iPaletteIndex];
  iVertexIdx = 1;
  iMinY = vertices->y;
  iMaxY = iMinY;

  // Find bouding box and min Y vertex
  while ((int16)iVertexIdx < iNumVertices) {
    iVertX = vertices[(int16)iVertexIdx].x;
    iY = vertices[(int16)iVertexIdx].y;

    // Update X boundaries
    if (iVertX <= iMaxX) {
      if (iVertX < iMinX)
        iMinX = vertices[(int16)iVertexIdx].x;
    } else {
      iMaxX = vertices[(int16)iVertexIdx].x;
    }

    // Update Y boundaries and get min Y vertex index
    if (iY <= iMaxY) {
      if (iY < iMinY) {
        iMinYIdx = iVertexIdx;
        iMinY = vertices[(int16)iVertexIdx].y;
      }
    } else {
      iMaxY = vertices[(int16)iVertexIdx].y;
    }
    ++iVertexIdx;
  }

  // Check if polygon is completely off-screen
  if (iMaxX < 0 || iMaxY < 0 || iMinX >= winw || iMinY >= winh)
    goto RESTORE_AND_RETURN;

  pMinYVert = &vertices[(int16)iMinYIdx];
  iCurrScanline = pMinYVert->y;
  iBackwardIdx = iMinYIdx;

  if (iCurrScanline >= 0) {
    // Initialize edges starting from min Y vertex (16.16 fixed point)
    SET_HIWORD(iRightEdgeX, pMinYVert->x);
    SET_HIWORD(iLeftEdgeX, pMinYVert->x);
    SET_LOWORD(iLeftEdgeDx, 0);
    SET_LOWORD(iRightEdgeX, 0);
    SET_LOWORD(iLeftEdgeDxTemp2, 0);
    SET_LOWORD(iLeftEdgeX, 0);

    while (1) {
      // Process left edge
      if ((int16)++iMinYIdx == iNumVertices)
        iMinYIdx = 0;
      iStep_3 = 8 * (int16)iMinYIdx;
      pVertex = (tPoint *)((char *)vertices + iStep_3);

      int16 *yArray = (int16 *)&vertices->y;
      int16 yValue = yArray[iStep_3 / 2];
      SET_LOWORD(iStep_3, yValue - iCurrScanline);

      iLeftRemain = iStep_3;
      if ((int16)iStep_3)
        break;                                  // found non-horizontal edge
      if ((int16)iMinYIdx == (int16)iBackwardIdx)
        goto RESTORE_AND_RETURN;
      SET_HIWORD(iLeftEdgeX, pVertex->x);
    }

    // Calculate left edge step
    SET_HIWORD(iLeftEdgeDxTemp2, GET_LOWORD(pVertex->x) - GET_HIWORD(iLeftEdgeX));
    iStep = iLeftEdgeDxTemp2 / (int16)iStep_3;
    iRemainder_1 = iLeftEdgeDxTemp2 % (int16)iLeftRemain;
    iLeftEdgeDx_1 = iStep;
    if (iStep < 0)
      iLeftEdgeX += iStep;                      // adjust for negative slope
                                                // 
    while (1) {
      // Process right edge
      if ((int16)--iBackwardIdx == -1)
        iBackwardIdx = iNumVertices - 1;
      pVertex_1 = &vertices[(int16)iBackwardIdx];
      SET_LOWORD(iRemainder_1, GET_LOWORD(pVertex_1->y) - iCurrScanline);
      iRightRemain = iRemainder_1;
      if ((int16)iRemainder_1)                // found non-horizontal edge
        break;
      if ((int16)iMinYIdx == (int16)iBackwardIdx)
        goto RESTORE_AND_RETURN;
      SET_HIWORD(iRightEdgeX, pVertex_1->x);
    }

    // Calculate right edge step
    SET_HIWORD(iLeftEdgeDx, GET_LOWORD(pVertex_1->x) - GET_HIWORD(iRightEdgeX));
    iStep_1 = iLeftEdgeDx / (int16)iRemainder_1;
    iRightEdgeDx = iStep_1;
    if (iStep_1 > 0)
      iRightEdgeX += iStep_1;
  } else {
    // handle starting above screen
    iTempY = pMinYVert->y;
    iTempY2 = iTempY;
    SET_LOWORD(iRightEdgeXTemp, 0);
    SET_LOWORD(iRemainder, 0);
    SET_LOWORD(iLeftEdgeXTemp, 0);
    SET_LOWORD(iLeftEdgeDxTemp, 0);

    // Find first visible left vertex
    do {
      nTempX = vertices[(int16)iMinYIdx++].x;
      SET_HIWORD(iLeftEdgeXTemp, nTempX);
      if ((int16)iMinYIdx == iNumVertices)
        iMinYIdx = 0;
      iTempY_1 = vertices[(int16)iMinYIdx].y;
      iTempY_2 = iTempY2;
      iTempY2 = iTempY_1;
    } while (iTempY_1 < 0);

    // Handle zero-length segments
    if (!iTempY_1) {
      do {
        nTempX_1 = vertices[(int16)iMinYIdx++].x;
        SET_HIWORD(iLeftEdgeXTemp, nTempX_1);
        if ((int16)iMinYIdx == iNumVertices)
          iMinYIdx = 0;
        iCalculatedStep_1 = vertices[(int16)iMinYIdx].y;
        iTempY_2 = iTempY2;
        iTempY2 = iCalculatedStep_1;
      } while (!iCalculatedStep_1);
      if (iCalculatedStep_1 < 0)
        goto RESTORE_AND_RETURN;
    }

    // Calculate left edge parameters for clipped start
    SET_HIWORD(iLeftEdgeDxTemp, GET_LOWORD(vertices[(int16)iMinYIdx].x) - GET_HIWORD(iLeftEdgeXTemp));
    iLeftRemain = iTempY2;
    iCalculatedStep = iLeftEdgeDxTemp / ((int16)iTempY2 - iTempY_2);
    iStepOffset = (int)((long long)iLeftEdgeXTemp - (long long)iCalculatedStep * iTempY_2);
    iLeftEdgeDx_1 = iCalculatedStep;
    iLeftEdgeX = iStepOffset;
    if (iCalculatedStep < 0)
      iLeftEdgeX = iCalculatedStep + iStepOffset;

    // Find first visible right vertex
    do {
      nTempX_2 = vertices[(int16)iBackwardIdx--].x;
      SET_HIWORD(iRightEdgeXTemp, nTempX_2);
      if ((int16)iBackwardIdx == -1)
        iBackwardIdx = iNumVertices - 1;
      iTempY_3 = vertices[(int16)iBackwardIdx].y;
      iTempY_4 = iTempY;
      iTempY = iTempY_3;
    } while (iTempY_3 < 0);

    // Handle zero-length segments
    if (!iTempY_3) {
      do {
        nTempX_3 = vertices[(int16)iBackwardIdx--].x;
        SET_HIWORD(iRightEdgeXTemp, nTempX_3);
        if ((int16)iBackwardIdx == -1)
          iBackwardIdx = iNumVertices - 1;
        iRightCalcStep = vertices[(int16)iBackwardIdx].y;
        iTempY_4 = iTempY;
        iTempY = iRightCalcStep;
      } while (!iRightCalcStep);
      if (iRightCalcStep < 0)
        goto RESTORE_AND_RETURN;
    }

    // Calculate right edge parameters for clipped start
    SET_HIWORD(iRemainder, GET_LOWORD(vertices[(int16)iBackwardIdx].x) - GET_HIWORD(iRightEdgeXTemp));
    iRightRemain = iTempY;
    iRightCalculatedStep = iRemainder / ((int16)iTempY - iTempY_4);
    iRightStepOffset = (int)((long long)iRightEdgeXTemp - (long long)iRightCalculatedStep * iTempY_4);
    iRightEdgeDx = iRightCalculatedStep;
    iRightEdgeX = iRightStepOffset;
    if (iRightCalculatedStep > 0)
      iRightEdgeX = iRightCalculatedStep + iRightStepOffset;
    iCurrScanline = 0;                          // Start rendering at top of screen
  }

  // Edge processing and clipping
  while (((iLeftEdgeX >> 16) & 0xFFFF) >= winw || iRightEdgeX < 0 || iLeftEdgeX > iRightEdgeX) {
    // Advance edges
    iLeftEdgeX += iLeftEdgeDx_1;
    ++iCurrScanline;
    iRightEdgeUpdate = iRightEdgeDx + iRightEdgeX;
    SET_HIWORD(iTempVal, GET_HIWORD(iLeftRemain));
    iRightEdgeX += iRightEdgeDx;
    SET_LOWORD(iTempVal, iLeftRemain - 1);
    bZeroFlag = (int16)iLeftRemain == 1;
    iLeftRemain = iTempVal;

    // Process left edge segment completion
    if (bZeroFlag) {
      while ((int16)iMinYIdx != (int16)iBackwardIdx) {
        SET_HIWORD(iVertexX, GET_HIWORD(iNumVertices));
        nTempX_4 = vertices[(int16)iMinYIdx++].x;
        SET_HIWORD(iLeftEdgeX, nTempX_4);
        if ((int16)iMinYIdx == iNumVertices)
          iMinYIdx = 0;
        pVertex_10 = &vertices[(int16)iMinYIdx];
        SET_LOWORD(iVertexX, pVertex_10->y);
        iLeftRemain = iVertexX - iCurrScanline;
        if ((int16)(iVertexX - iCurrScanline) > 0) {
          SET_HIWORD(iLeftEdgeDxTemp3, GET_LOWORD(pVertex_10->x) - GET_HIWORD(iLeftEdgeX));
          SET_LOWORD(iLeftEdgeX, 0);
          SET_LOWORD(iLeftEdgeDxTemp3, 0);
          iNewStep = iLeftEdgeDxTemp3 / (int16)iLeftRemain;
          iRightEdgeUpdate = iLeftEdgeDxTemp3 % (int16)iLeftRemain;
          iLeftEdgeDx_1 = iNewStep;
          if (iNewStep < 0)
            iLeftEdgeX += iNewStep;
          goto EDGE_ADVANCE_1;
        }
      }
      goto RESTORE_AND_RETURN;
    }
  EDGE_ADVANCE_1:
    SET_HIWORD(iTempVal_7, GET_HIWORD(iRightRemain));
    SET_LOWORD(iTempVal_7, iRightRemain - 1);
    bZeroFlag = (int16)iRightRemain == 1;
    iRightRemain = iTempVal_7;

    // Process right edge segment completion
    if (bZeroFlag) {
      while ((int16)iMinYIdx != (int16)iBackwardIdx) {
        nTempX_5 = vertices[(int16)iBackwardIdx--].x;
        SET_HIWORD(iRightEdgeX, nTempX_5);
        if ((int16)iBackwardIdx == -1)
          iBackwardIdx = iNumVertices - 1;
        pVertex_2 = &vertices[(int16)iBackwardIdx];
        SET_LOWORD(iRightEdgeUpdate, pVertex_2->y);
        iRightEdgeUpdate -= iCurrScanline;
        iRightRemain = iRightEdgeUpdate;
        if ((int16)iRightEdgeUpdate > 0) {
          SET_HIWORD(iTempEdgeDx, GET_LOWORD(pVertex_2->x) - GET_HIWORD(iRightEdgeX));
          SET_LOWORD(iRightEdgeX, 0);
          SET_LOWORD(iTempEdgeDx, 0);
          iStep_4 = iTempEdgeDx / (int16)iRightEdgeUpdate;
          iRightEdgeDx = iStep_4;
          if (iStep_4 > 0)
            iRightEdgeX += iStep_4;
          goto EDGE_ADVANCE_2;
        }
      }
      goto RESTORE_AND_RETURN;
    }
  EDGE_ADVANCE_2:
      // Screen boundary check
    if (iCurrScanline >= winh)
      goto RESTORE_AND_RETURN;
  }
  // Check right edge against screen
  while (1) {
    if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW) {
      if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW)    // Left-clipped scanline
        goto RESTORE_AND_RETURN;
      if (GET_SHIWORD(iRightEdgeX) < iOldWinW)
        goto RENDER_LEFT_VISIBLE_SEGMENT;
    FULL_WIDTH_RENDER:
      if (iRightEdgeX < 0)
        goto RESTORE_AND_RETURN;
      while (1) {
        // Fully on-screen scanline
      FULL_WIDTH_LOOP:
        pDest = &scrptr[iOldWinW * iCurrScanline];
        for (iPixelIdx = 0; iOldWinW > iPixelIdx; ++iPixelIdx) {
          *pDest = pShadePalette[*pDest];
          ++pDest;
        }

        // Advance to next scanline
        iLeftEdgeX += iLeftEdgeDx_1;
        ++iCurrScanline;
        iRightEdgeTemp = iRightEdgeDx + iRightEdgeX;
        SET_HIWORD(iTempVal_1, GET_HIWORD(iLeftRemain));
        iRightEdgeX += iRightEdgeDx;
        SET_LOWORD(iTempVal_1, iLeftRemain - 1);
        bZeroFlag = (int16)iLeftRemain == 1;
        iLeftRemain = iTempVal_1;

        // Left edge segment completion
        if (bZeroFlag) {
          while ((int16)iMinYIdx != (int16)iBackwardIdx) {
            SET_HIWORD(iVertexIdx_1, GET_HIWORD(iNumVertices));
            nTempX_6 = vertices[(int16)iMinYIdx++].x;
            SET_HIWORD(iLeftEdgeX, nTempX_6);
            if ((int16)iMinYIdx == iNumVertices)
              iMinYIdx = 0;
            pVertex_11 = &vertices[(int16)iMinYIdx];
            SET_LOWORD(iVertexIdx_1, pVertex_11->y);
            iLeftRemain = iVertexIdx_1 - iCurrScanline;
            if ((int16)(iVertexIdx_1 - iCurrScanline) > 0) {
              SET_HIWORD(iLeftEdgeDxTemp7, GET_LOWORD(pVertex_11->x) - GET_HIWORD(iLeftEdgeX));
              SET_LOWORD(iLeftEdgeX, 0);
              SET_LOWORD(iLeftEdgeDxTemp7, 0);
              iNewStep_1 = iLeftEdgeDxTemp7 / (int16)iLeftRemain;
              iRightEdgeTemp = iLeftEdgeDxTemp7 % (int16)iLeftRemain;
              iLeftEdgeDx_1 = iNewStep_1;
              if (iNewStep_1 < 0)
                iLeftEdgeX += iNewStep_1;
              goto LEFT_EDGE_COMPLETE;
            }
          }
          goto RESTORE_AND_RETURN;
        }
      LEFT_EDGE_COMPLETE:
        SET_HIWORD(iTempVal_2, GET_HIWORD(iRightRemain));
        SET_LOWORD(iTempVal_2, iRightRemain - 1);
        bZeroFlag = (int16)iRightRemain == 1;
        iRightRemain = iTempVal_2;

        // Process right edge segment
        if (bZeroFlag) {
          while ((int16)iMinYIdx != (int16)iBackwardIdx) {
            nTempX_7 = vertices[(int16)iBackwardIdx--].x;
            SET_HIWORD(iRightEdgeX, nTempX_7);
            if ((int16)iBackwardIdx == -1)
              iBackwardIdx = iNumVertices - 1;
            pVertex_3 = &vertices[(int16)iBackwardIdx];
            SET_LOWORD(iRightEdgeTemp, pVertex_3->y);
            iRightEdgeTemp -= iCurrScanline;
            iRightRemain = iRightEdgeTemp;
            if ((int16)iRightEdgeTemp > 0) {
              SET_HIWORD(iTempEdgeDx5, GET_LOWORD(pVertex_3->x) - GET_HIWORD(iRightEdgeX));
              SET_LOWORD(iRightEdgeX, 0);
              SET_LOWORD(iTempEdgeDx5, 0);
              iStep_5 = iTempEdgeDx5 / (int16)iRightEdgeTemp;
              iRightEdgeDx = iStep_5;
              if (iStep_5 > 0)
                iRightEdgeX += iStep_5;
              goto SCANLINE_CHECK;
            }
          }
          goto RESTORE_AND_RETURN;
        }
      SCANLINE_CHECK:
              // Exit if at bottom of screen
        if (iCurrScanline >= winh)
          goto RESTORE_AND_RETURN;
        // Check if left edge is on-screen
        if (GET_SHIWORD(iLeftEdgeX) < iOldWinW)
          break;
        // Check if right edge is off-screen right
        if (GET_SHIWORD(iRightEdgeX) < iOldWinW)
          goto RENDER_RIGHT_VISIBLE;
      }
      // Verify left edge position
      if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW)
        goto RESTORE_AND_RETURN;
      // Verify right edge position
      if (GET_SHIWORD(iRightEdgeX) >= iOldWinW)
        goto RIGHT_CLIPPED_RENDER;
    RENDER_LEFT_VISIBLE:
      if (iRightEdgeX < 0)
        goto RESTORE_AND_RETURN;
    } else {

      // Handle right-clipped scanlines
      while (GET_SHIWORD(iRightEdgeX) >= iOldWinW) {
        if (iRightEdgeX < 0)
          goto RESTORE_AND_RETURN;
        while (1) {
          // Right clipped scanline processing
        RIGHT_CLIPPED_RENDER:
          pDest3 = &scrptr[GET_SHIWORD(iLeftEdgeX) + iOldWinW * iCurrScanline];
          for (iPixelIdx_1 = 0; iOldWinW - GET_SHIWORD(iLeftEdgeX) > iPixelIdx_1; ++iPixelIdx_1) {
            *pDest3 = pShadePalette[*pDest3];
            ++pDest3;
          }
          // Advance to next scanline
          iLeftEdgeX += iLeftEdgeDx_1;
          ++iCurrScanline;
          iRightEdgeTemp_1 = iRightEdgeDx + iRightEdgeX;
          SET_HIWORD(iTempVal_3, GET_HIWORD(iLeftRemain));
          iRightEdgeX += iRightEdgeDx;
          SET_LOWORD(iTempVal_3, iLeftRemain - 1);
          bZeroFlag = (int16)iLeftRemain == 1;
          iLeftRemain = iTempVal_3;

          // Process left edge segment completion
          if (bZeroFlag) {
            while ((int16)iMinYIdx != (int16)iBackwardIdx) {
              SET_HIWORD(iTempY_5, GET_HIWORD(iNumVertices));
              nTempX_8 = vertices[(int16)iMinYIdx++].x;
              SET_HIWORD(iLeftEdgeX, nTempX_8);
              if ((int16)iMinYIdx == iNumVertices)
                iMinYIdx = 0;
              pVertex_4 = &vertices[(int16)iMinYIdx];
              SET_LOWORD(iTempY_5, pVertex_4->y);
              iLeftRemain = iTempY_5 - iCurrScanline;
              if ((int16)(iTempY_5 - iCurrScanline) > 0) {
                SET_HIWORD(iLeftEdgeDxTemp6, GET_LOWORD(pVertex_4->x) - GET_HIWORD(iLeftEdgeX));
                SET_LOWORD(iLeftEdgeX, 0);
                SET_LOWORD(iLeftEdgeDxTemp6, 0);
                iNewStep_2 = iLeftEdgeDxTemp6 / (int16)iLeftRemain;
                iRightEdgeTemp_1 = iLeftEdgeDxTemp6 % (int16)iLeftRemain;
                iLeftEdgeDx_1 = iNewStep_2;
                if (iNewStep_2 < 0)
                  iLeftEdgeX += iNewStep_2;
                goto LEFT_EDGE_COMPLETE_2;
              }
            }
            goto RESTORE_AND_RETURN;
          }
        LEFT_EDGE_COMPLETE_2:
          SET_HIWORD(iTempVal_4, GET_HIWORD(iRightRemain));
          SET_LOWORD(iTempVal_4, iRightRemain - 1);
          bZeroFlag = (int16)iRightRemain == 1;
          iRightRemain = iTempVal_4;

          // Process right edge segment completion
          if (bZeroFlag) {
            while ((int16)iMinYIdx != (int16)iBackwardIdx) {
              nTempX_9 = vertices[(int16)iBackwardIdx--].x;
              SET_HIWORD(iRightEdgeX, nTempX_9);
              if ((int16)iBackwardIdx == -1)
                iBackwardIdx = iNumVertices - 1;
              pVertex_5 = &vertices[(int16)iBackwardIdx];
              SET_LOWORD(iRightEdgeTemp_1, pVertex_5->y);
              iRightEdgeTemp_1 -= iCurrScanline;
              iRightRemain = iRightEdgeTemp_1;
              if ((int16)iRightEdgeTemp_1 > 0) {
                SET_HIWORD(iTempEdgeDx4, GET_LOWORD(pVertex_5->x) - GET_HIWORD(iRightEdgeX));
                SET_LOWORD(iRightEdgeX, 0);
                SET_LOWORD(iTempEdgeDx4, 0);
                iStep_6 = iTempEdgeDx4 / (int16)iRightEdgeTemp_1;
                iRightEdgeDx = iStep_6;
                if (iStep_6 > 0)
                  iRightEdgeX += iStep_6;
                goto SCANLINE_CHECK_2;
              }
            }
            goto RESTORE_AND_RETURN;
          }
        SCANLINE_CHECK_2:
                  // Exit if at bottom of screen
          if (iCurrScanline >= winh) {
            winw = iOldWinW;
            return;
          }
          // Check if left edge is off-screen right
          if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW)
            break;
          // Check if right edge is on-screen
          if (GET_SHIWORD(iRightEdgeX) < iOldWinW)
            goto RENDER_LEFT_VISIBLE;
        }

        // Handle partially visible scanlines
        if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW)
          goto RESTORE_AND_RETURN;
        if (GET_SHIWORD(iRightEdgeX) >= iOldWinW)
          goto FULL_WIDTH_LOOP;
      RENDER_RIGHT_VISIBLE:
        if (iRightEdgeX < 0)
          goto RESTORE_AND_RETURN;
        while (1) {
          // Left-visible scanline processing
        RENDER_LEFT_VISIBLE_SEGMENT:
          pDest2 = &scrptr[iOldWinW * iCurrScanline];
          for (iPixelIdx_2 = 0; GET_SHIWORD(iRightEdgeX) + 1 > iPixelIdx_2; ++iPixelIdx_2) {
            *pDest2 = pShadePalette[*pDest2];
            ++pDest2;
          }

          // Advance to next scanline
          iLeftEdgeX += iLeftEdgeDx_1;
          iRightEdgeX += iRightEdgeDx;
          SET_HIWORD(iStep_2, GET_HIWORD(iLeftRemain));
          ++iCurrScanline;
          SET_LOWORD(iStep_2, iLeftRemain - 1);
          bZeroFlag = (int16)iLeftRemain == 1;
          iLeftRemain = iStep_2;

          // Process left edge segment completion
          if (bZeroFlag) {
            while ((int16)iMinYIdx != (int16)iBackwardIdx) {
              SET_HIWORD(nTempX_10, GET_HIWORD(iNumVertices));
              nTempX_11 = vertices[(int16)iMinYIdx++].x;
              SET_HIWORD(iLeftEdgeX, nTempX_11);
              if ((int16)iMinYIdx == iNumVertices)
                iMinYIdx = 0;
              pVertex_6 = &vertices[(int16)iMinYIdx];
              SET_LOWORD(nTempX_10, pVertex_6->y);
              iLeftRemain = nTempX_10 - iCurrScanline;
              if ((int16)(nTempX_10 - iCurrScanline) > 0) {
                SET_HIWORD(iLeftEdgeDxTemp5, GET_LOWORD(pVertex_6->x) - GET_HIWORD(iLeftEdgeX));
                SET_LOWORD(iLeftEdgeX, 0);
                SET_LOWORD(iLeftEdgeDxTemp5, 0);
                iNewStep_3 = iLeftEdgeDxTemp5 / (int16)iLeftRemain;
                iStep_2 = iLeftEdgeDxTemp5 % (int16)iLeftRemain;
                iLeftEdgeDx_1 = iNewStep_3;
                if (iNewStep_3 < 0)
                  iLeftEdgeX += iNewStep_3;
                goto LEFT_EDGE_COMPLETE_3;
              }
            }
            goto RESTORE_AND_RETURN;
          }
        LEFT_EDGE_COMPLETE_3:
          SET_HIWORD(iTempVal_5, GET_HIWORD(iRightRemain));
          SET_LOWORD(iTempVal_5, iRightRemain - 1);
          bZeroFlag = (int16)iRightRemain == 1;
          iRightRemain = iTempVal_5;

          // Process right edge segment completion
          if (bZeroFlag) {
            while ((int16)iMinYIdx != (int16)iBackwardIdx) {
              nTempX_12 = vertices[(int16)iBackwardIdx--].x;
              SET_HIWORD(iRightEdgeX, nTempX_12);
              if ((int16)iBackwardIdx == -1)
                iBackwardIdx = iNumVertices - 1;
              pVertex_7 = &vertices[(int16)iBackwardIdx];
              SET_LOWORD(iStep_2, pVertex_7->y);
              iStep_2 -= iCurrScanline;
              iRightRemain = iStep_2;
              if ((int16)iStep_2 > 0) {
                SET_HIWORD(iTempEdgeDx3, GET_LOWORD(pVertex_7->x) - GET_HIWORD(iRightEdgeX));
                SET_LOWORD(iRightEdgeX, 0);
                SET_LOWORD(iTempEdgeDx3, 0);
                iStep_8 = iTempEdgeDx3 / (int16)iStep_2;
                iRightEdgeDx = iStep_8;
                if (iStep_8 > 0)
                  iRightEdgeX += iStep_8;
                goto SCANLINE_CHECK_3;
              }
            }
            goto RESTORE_AND_RETURN;
          }
        SCANLINE_CHECK_3:
                  // Exit if at bottom of screen
          if (iCurrScanline >= winh) {
            winw = iOldWinW;
            return;
          }
          // Check if left edge is on-screen
          if (GET_SHIWORD(iLeftEdgeX) < iOldWinW)
            break;
          // Check if right edge is off-screen right
          if (GET_SHIWORD(iRightEdgeX) >= iOldWinW)
            goto FULL_WIDTH_RENDER;
        }
        // Verify left edge position
        if (GET_SHIWORD(iLeftEdgeX) >= iOldWinW)
          goto RESTORE_AND_RETURN;
      }
    }

    // Calculate visible segment width
    nSegmentWidth = (int16)(GET_HIWORD(iRightEdgeX) - GET_HIWORD(iLeftEdgeX) + 1);
    if (nSegmentWidth < 0)                    // Negative width check
      goto RESTORE_AND_RETURN;

    // Process visible scanline segment
    pDest4 = &scrptr[GET_SHIWORD(iLeftEdgeX) + iOldWinW * iCurrScanline];
    for (iPixelIdx_3 = 0; nSegmentWidth > iPixelIdx_3; ++iPixelIdx_3) {
      *pDest4 = pShadePalette[*pDest4];         // Apply palette
      ++pDest4;
    }

    // Advance to next scanline
    iLeftEdgeX += iLeftEdgeDx_1;
    iRightEdgeX += iRightEdgeDx;
    SET_HIWORD(iStepOffset_1, GET_HIWORD(iLeftRemain));
    ++iCurrScanline;
    SET_LOWORD(iStepOffset_1, iLeftRemain - 1);
    bZeroFlag = (int16)iLeftRemain == 1;
    iLeftRemain = iStepOffset_1;

    // Process left edge segment completion
    if (bZeroFlag)
      break;
  PROCESS_RIGHT_EDGE:
      // Process right edge segment
    SET_HIWORD(iTempVal_6, GET_HIWORD(iRightRemain));
    SET_LOWORD(iTempVal_6, iRightRemain - 1);
    bZeroFlag = (int16)iRightRemain == 1;
    iRightRemain = iTempVal_6;

    // Process right edge segment completion
    if (bZeroFlag) {
      while ((int16)iMinYIdx != (int16)iBackwardIdx) {
        nTempX_14 = vertices[(int16)iBackwardIdx--].x;
        SET_HIWORD(iRightEdgeX, nTempX_14);
        if ((int16)iBackwardIdx == -1)
          iBackwardIdx = iNumVertices - 1;
        pVertex_8 = &vertices[(int16)iBackwardIdx];
        SET_LOWORD(iStepOffset_1, pVertex_8->y);
        iStepOffset_1 -= iCurrScanline;
        iRightRemain = iStepOffset_1;
        if ((int16)iStepOffset_1 > 0) {
          SET_HIWORD(iTempEdgeDx2, GET_LOWORD(pVertex_8->x) - GET_HIWORD(iRightEdgeX));
          SET_LOWORD(iRightEdgeX, 0);
          SET_LOWORD(iTempEdgeDx2, 0);
          iStep_7 = iTempEdgeDx2 / (int16)iStepOffset_1;
          iRightEdgeDx = iStep_7;
          if (iStep_7 > 0)
            iRightEdgeX += iStep_7;
          goto NEXT_SCANLINE;
        }
      }
      goto RESTORE_AND_RETURN;
    }
  NEXT_SCANLINE:
      // Exit if at bottom of screen
    if (iCurrScanline >= winh) {
      winw = iOldWinW;
      return;
    }
  }

  // Process left edge segment completion
  while ((int16)iMinYIdx != (int16)iBackwardIdx) {
    SET_HIWORD(iVertexIdx_2, GET_HIWORD(iNumVertices));
    nTempX_13 = vertices[(int16)iMinYIdx++].x;
    SET_HIWORD(iLeftEdgeX, nTempX_13);
    if ((int16)iMinYIdx == iNumVertices)
      iMinYIdx = 0;
    pVertex_9 = &vertices[(int16)iMinYIdx];
    SET_LOWORD(iVertexIdx_2, pVertex_9->y);
    iLeftRemain = iVertexIdx_2 - iCurrScanline;
    if ((int16)(iVertexIdx_2 - iCurrScanline) > 0) {
      SET_HIWORD(iLeftEdgeDxTemp4, GET_LOWORD(pVertex_9->x) - GET_HIWORD(iLeftEdgeX));
      SET_LOWORD(iLeftEdgeX, 0);
      SET_LOWORD(iLeftEdgeDxTemp4, 0);
      iNewStep_4 = iLeftEdgeDxTemp4 / (int16)iLeftRemain;
      iStepOffset_1 = iLeftEdgeDxTemp4 % (int16)iLeftRemain;
      iLeftEdgeDx_1 = iNewStep_4;
      if (iNewStep_4 < 0)
        iLeftEdgeX += iNewStep_4;
      goto PROCESS_RIGHT_EDGE;
    }
  }
RESTORE_AND_RETURN:
  winw = iOldWinW;
}*/

//-------------------------------------------------------------------------------------------------
