#include "horizon.h"
#include "3d.h"
#include "graphics.h"
#include "polyf.h"
#include "polytex.h"
#include "transfrm.h"
#include "drawtrk3.h"
#include "roller.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <assert.h>
//-------------------------------------------------------------------------------------------------

tCloudData cloud[40]; //00199950
int test_y1;          //0019A3F4
char upside_down;     //0019A410
char ground_left;     //0019A411

//-------------------------------------------------------------------------------------------------
//0006AB90
void DrawHorizon(uint8 *pScrBuf)
{
  uint8 *pScrPtr; // ecx
  uint8 byFillColor; // dl
  int iWorldTiltMasked; // eax
  double dViewDistTan; // st7
  double dHorizonX; // st6
  double dHorizonY; // st7
  int iGroundHeight1; // esi
  int iGroundHeight2; // edi
  int iSkyHeight1; // eax
  int iSkyHeight2; // eax
  int iGroundHeight3; // eax
  //int iLoopEnd; // edi
  //int iLoopCounter; // esi
  //double dSlope; // st7
  //double dPrevScanValue; // st6
  int iRowCounter; // edi
  int iScanIdx; // ebp
  double dScanValue; // st7
  double dNegGroundWidth; // st7
  double dInvScale; // st6
  signed int iGroundWidthCopy; // esi
  int iSkyWidth; // esi
  uint8 *pScrPtrRight; // ecx
  int iRowCounter2; // edi
  int iScanIdx2; // esi
  double dScanValue2; // st7
  int iGroundWidthTemp; // ebp
  double dExcessWidth; // st7
  double dInvScale2; // st6
  double dModfResult1; // [esp+14h] [ebp-70h] BYREF
  double dModfResult2; // [esp+1Ch] [ebp-68h] BYREF
  double fSinElevation; // [esp+24h] [ebp-60h]
  float fTempCalc; // [esp+2Ch] [ebp-58h]
  int iYBase; // [esp+38h] [ebp-4Ch]
  signed int iGroundWidth; // [esp+3Ch] [ebp-48h]
  int iHorizonX; // [esp+40h] [ebp-44h]
  uint8 *pScrBuf_1; // [esp+44h] [ebp-40h]
  int iHorizonY; // [esp+48h] [ebp-3Ch]
  float fSlope; // [esp+4Ch] [ebp-38h]
  float fScale; // [esp+50h] [ebp-34h]
  float fCosTilt; // [esp+54h] [ebp-30h]
  float fNegSinTilt; // [esp+58h] [ebp-2Ch]
  float fScanValue; // [esp+5Ch] [ebp-28h]
  signed int iGroundWidth2; // [esp+60h] [ebp-24h]
  uint8 bySkyColor; // [esp+64h] [ebp-20h]
  uint8 byGroundColor; // [esp+68h] [ebp-1Ch]

  pScrBuf_1 = pScrBuf;
  fSinElevation = tsin[worldelev & 0x3FFF];     // Calculate sine of world elevation (masked to 14 bits for lookup table)
  pScrPtr = pScrBuf;
  bySkyColor = 0x91;
  byGroundColor = HorizonColour[front_sec];

  // 0x10 = TEX_OFF_HORIZON
  if (fSinElevation > 0.7 || fSinElevation < -0.7 || (textures_off & TEX_OFF_HORIZON) != 0)// Check for simple fill cases: steep angle or horizon textures disabled
  {
    if (tsin[worldelev & 0x3FFF] <= 0.7)
      byFillColor = byGroundColor;
    else
      byFillColor = bySkyColor;
    if ((textures_off & TEX_OFF_HORIZON) != 0)
      byFillColor = bySkyColor;
    memset(pScrBuf, byFillColor, winh * winw);  // Simple case: fill entire screen with single color
  } else {
    iWorldTiltMasked = worldtilt & 0x3FFF;      // Complex horizon rendering: calculate tilt trigonometry
    fNegSinTilt = -tsin[iWorldTiltMasked];
    fCosTilt = tcos[iWorldTiltMasked];
    upside_down = tcos[worldelev & 0x3FFF] < 0.0 != fCosTilt < 0.0;// Determine if world is upside-down (elevation vs tilt sign comparison)
    ground_left = fNegSinTilt < 0.0;            // Determine which side has ground based on tilt direction
    if (tcos[worldelev & 0x3FFF] < 0.0)
      ground_left = fNegSinTilt >= 0.0;
    dViewDistTan = (double)VIEWDIST * ptan[worldelev & 0x3FFF];// Calculate horizon line position using perspective projection
    dHorizonX = dViewDistTan * fNegSinTilt + (double)xbase;
    //_CHP();
    iHorizonX = (int)dHorizonX;
    iYBase = 199 - ybase;
    dHorizonY = (double)(199 - ybase) + dViewDistTan * fCosTilt;
    //_CHP();
    iHorizonY = (int)dHorizonY;
    //if ((LODWORD(fNegSinTilt) & 0x7FFFFFFF) != 0)// Calculate horizon slope (rise/run) with divide-by-zero protection
    if (fabs(fNegSinTilt) > FLT_EPSILON)// Calculate horizon slope (rise/run) with divide-by-zero protection
      fSlope = fCosTilt / fNegSinTilt;
    else
      fSlope = 41.0;
    if (fSlope <= 40.0 && fSlope >= -40.0)    // Check if slope is reasonable for scanline rendering (not too steep)
    {
      fScale = (float)((double)scr_size * 0.015625);     // Scanline rendering case: calculate per-row horizon intersection
      if (tcos[worldelev & 0x3FFF] < 0.0)     // Handle upside-down case by flipping tilt values
      {
        fCosTilt = -fCosTilt;
        fNegSinTilt = -fNegSinTilt;
        //HIBYTE(fCosTilt) ^= 0x80u;
        //HIBYTE(fNegSinTilt) ^= 0x80u;
      }
      fScanValue = (float)(((double)iHorizonY * fSlope + (double)iHorizonX) * fScale);// Pre-calculate horizon intersection points for each scanline
      if (winh > 0) {
        // Pre-calculate horizon intersection points for each scanline
        for (int iScanlineIdx = 0; iScanlineIdx < winh; iScanlineIdx++)
        {
          hor_scan[iScanlineIdx] = fScanValue;
          fScanValue -= fSlope;  // Move to next scanline position
        }
        //iLoopEnd = 4 * winh;
        //iLoopCounter = 0;
        //dSlope = fSlope;
        //do {
        //  dPrevScanValue = fScanValue - dSlope;
        //  iLoopCounter += 4;
        //  *(float *)((char *)&GroundPt[499].pointAy[5].fZ + iLoopCounter) = fScanValue;// reference to hor_scan
        //  fScanValue = (float)dPrevScanValue;
        //} while (iLoopCounter < iLoopEnd);
      }
      if (ground_left)                        // Branch: ground on left side of screen
      {
        iRowCounter = 0;
        if (winh > 0) {
          iScanIdx = 0;
          do {
            dScanValue = hor_scan[iScanIdx];    // Get horizon intersection point for current scanline from pre-calculated array
            //_CHP();
            iGroundWidth = (int)dScanValue;
            if ((int)dScanValue <= winw)      // Check if horizon intersection is within screen bounds
            {                                   // Horizon intersection is on-screen: render ground portion
              if ((int)dScanValue >= 0) {
                iGroundWidthCopy = iGroundWidth;
                memset(pScrPtr, byGroundColor, iGroundWidth);// Fill ground pixels from left edge to horizon intersection
                pScrPtr += iGroundWidthCopy;
              } else {
                dNegGroundWidth = (double)iGroundWidth;// Horizon intersection is off-screen left: calculate texture coordinates
                dInvScale = 1.0 / fScale;
                fTempCalc = (float)(-fCosTilt * dNegGroundWidth * dInvScale);
                modf(dNegGroundWidth * fNegSinTilt * dInvScale, &dModfResult1);// modf() splits floating point into integer and fractional parts
                modf(fTempCalc, &dModfResult2);
                iGroundWidth = 0;
              }
              iSkyWidth = winw - iGroundWidth;
              if (winw - iGroundWidth >= 0) {
                memset(pScrPtr, bySkyColor, winw - iGroundWidth);// Fill sky pixels from horizon intersection to right edge
                pScrPtr += iSkyWidth;
              }
            } else {
              memset(pScrPtr, byGroundColor, winw);
              pScrPtr += winw;
            }
            ++iRowCounter;
            ++iScanIdx;
          } while (iRowCounter < winh);
        }
      } else {
        pScrPtrRight = &pScrPtr[winw - 1];      // Branch: ground on right side of screen (render right-to-left)
        iRowCounter2 = 0;
        if (winh > 0) {
          iScanIdx2 = 0;
          do {
            dScanValue2 = hor_scan[iScanIdx2];
            //_CHP();
            iGroundWidth2 = (int)dScanValue2;
            iGroundWidthTemp = (int)dScanValue2;
            if ((int)dScanValue2 >= 0)        // Right-side rendering: check horizon intersection bounds
            {
              if (iGroundWidthTemp <= winw) {
                pScrPtrRight -= winw - iGroundWidthTemp;
                memset(pScrPtrRight + 1, byGroundColor, winw - iGroundWidthTemp);// Fill ground pixels from intersection to right edge (right-to-left)
              } else {
                iYBase = iGroundWidthTemp - (winw - 1);// Horizon extends beyond screen right: calculate excess width
                dExcessWidth = (double)iYBase;
                dInvScale2 = 1.0 / fScale;
                fTempCalc = (float)(-fCosTilt * dExcessWidth * dInvScale2);
                modf(dExcessWidth * fNegSinTilt * dInvScale2, &dModfResult1);
                modf(fTempCalc, &dModfResult2);
                iGroundWidth2 = winw;
              }
              if (iGroundWidth2 >= 0) {
                pScrPtrRight -= iGroundWidth2;
                memset(pScrPtrRight + 1, bySkyColor, iGroundWidth2);// Fill sky pixels from left edge to intersection (right-to-left)
              }
              pScrPtrRight += 2 * winw;
            } else {
              memset(&pScrPtrRight[-winw + 1], byGroundColor, winw);
              pScrPtrRight += winw;
            }
            ++iRowCounter2;
            ++iScanIdx2;
          } while (iRowCounter2 < winh);
        }
      }
    } else {                                           // Steep slope case: fill screen in large blocks rather than scanlines
      if (upside_down) {
        iGroundHeight1 = (iHorizonY * scr_size + 32) / 64;
        //iGroundHeight1 = (iHorizonY * scr_size + 32 - (__CFSHL__((iHorizonY * scr_size + 32) >> 31, 6) + ((iHorizonY * scr_size + 32) >> 31 << 6))) >> 6;
        if (iGroundHeight1 > winh)
          iGroundHeight1 = winh;
        if (iGroundHeight1 > 0) {
          memset(pScrPtr, byGroundColor, iGroundHeight1 * winw);
          pScrPtr += iGroundHeight1 * winw;
        }
      } else {
        iGroundHeight1 = (iHorizonY * scr_size) / 64;
        //iGroundHeight1 = (iHorizonY * scr_size - (__CFSHL__((iHorizonY * scr_size) >> 31, 6) + ((iHorizonY * scr_size) >> 31 << 6))) >> 6;
        iGroundHeight2 = iGroundHeight1;
        if (iGroundHeight1 > winh)
          iGroundHeight1 = winh;
        if (iGroundHeight2 > winh)
          iGroundHeight2 = winh;
        if (iGroundHeight2 > 0) {
          memset(pScrPtr, bySkyColor, iGroundHeight2 * winw);
          pScrPtr += winw * iGroundHeight2;
        }
      }
      if (upside_down) {
        iSkyHeight1 = (iHorizonY * scr_size + 32) / 64;
        //iSkyHeight1 = (iHorizonY * scr_size + 32 - (__CFSHL__((iHorizonY * scr_size + 32) >> 31, 6) + ((iHorizonY * scr_size + 32) >> 31 << 6))) >> 6;
        if (iSkyHeight1 > winh)
          iSkyHeight1 = winh;
        iSkyHeight2 = winh - iSkyHeight1;
        if (iSkyHeight2 > winh)
          iSkyHeight2 = winh;
        if (iSkyHeight2 > 0)
          memset(pScrPtr, bySkyColor, iSkyHeight2 * winw);
      } else {
        iGroundHeight3 = winh - iGroundHeight1;
        if (winh - iGroundHeight1 > winh)
          iGroundHeight3 = winh;
        if (iGroundHeight3 > 0)
          memset(pScrPtr, byGroundColor, iGroundHeight3 * winw);
      }
    }
  }
  // 0x8 = TEX_OFF_CLOUDS
  if ((textures_off & TEX_OFF_CLOUDS) == 0)                // Render clouds on top of horizon if cloud textures enabled
    displayclouds(pScrBuf_1);
}

//-------------------------------------------------------------------------------------------------
//0006B1A0
void initclouds()
{                                               // Loop through all 40 cloud slots
  int iCloudIdx; // edi
  //int iRandVal1; // eax
  //uint32 uiAngle1Calc; // eax
  int iAngle1; // ebp
  //int iRandVal2; // eax
  int iAngle2; // esi
  double dRadiusRotComp1; // st6
  double dRadiusRotComp2; // st5
  double dBaseCalcX; // st4
  double dBaseCalcY; // st6
  int iValidPlacement; // ecx
  double dMatrix12; // st6
  double dMatrix20Temp; // st3
  double dMatrix21Temp; // st2
  double dMatrix22Temp; // rt1
  int iRandColorBase; // eax
  int iColorIndex; // eax
  int iCheckIdx; // edx
  double fDeltaX; // st7
  double fDeltaY; // st6
  double fXYDistSq; // st7
  double fDeltaZ; // st6
  float fCos1Cos2; // [esp+8h] [ebp-80h]
  float fTransX2; // [esp+Ch] [ebp-7Ch]
  float fSin1Cos2; // [esp+10h] [ebp-78h]
  float fTransX1; // [esp+14h] [ebp-74h]
  float fTransY2; // [esp+18h] [ebp-70h]
  float fTransZ1; // [esp+1Ch] [ebp-6Ch]
  float fTransY1; // [esp+20h] [ebp-68h]
  float fMatrix11; // [esp+24h] [ebp-64h]
  float fMatrix02; // [esp+28h] [ebp-60h]
  float fMatrix10; // [esp+2Ch] [ebp-5Ch]
  float fMatrix00; // [esp+30h] [ebp-58h]
  float fMatrix01; // [esp+34h] [ebp-54h]
  float fRadiusComp; // [esp+38h] [ebp-50h]
  float fSin2; // [esp+3Ch] [ebp-4Ch]
  float fBaseY; // [esp+40h] [ebp-48h]
  float fBaseX; // [esp+44h] [ebp-44h]
  float fRotComp2; // [esp+48h] [ebp-40h]
  float fRotComp1; // [esp+4Ch] [ebp-3Ch]
  float fBaseZ; // [esp+50h] [ebp-38h]
  float fCos2; // [esp+54h] [ebp-34h]
  float fNegSin1; // [esp+58h] [ebp-30h]
  float fCos1; // [esp+5Ch] [ebp-2Ch]
  float fNegRadius; // [esp+60h] [ebp-28h]
  float fRadius; // [esp+64h] [ebp-24h]
  float fMinDistance; // [esp+68h] [ebp-20h]
  float fDistance; // [esp+6Ch] [ebp-1Ch]

  for (iCloudIdx = 0; iCloudIdx < 40; ++iCloudIdx) {
    fRadius = 1800000.0;                        // Start with maximum radius for placement attempt
    do {
      //iRandVal1 = rand();                       // Generate random angle1 (theta) for spherical coordinates
      //uiAngle1Calc = (uint32)3400 * (uint32)GetHighOrderRand(iRandVal1, iRandVal1);
      ////iAngle1Calc = 3400 * (((iRandVal1 * iRandVal1) & 0x7FFF) >> 15);
      ////iAngle1Calc = 3400 * ((iRandVal1 * iRandVal1 - (__CFSHL__((iRandVal1 * iRandVal1) >> 31, 15) + ((iRandVal1 * iRandVal1) >> 31 << 15))) >> 15);
      //iAngle1 = ((uiAngle1Calc & 0x8000) >> 15) + 520;  // Will be either 520 or 521
      ////iAngle1 = ((iAngle1Calc - (__CFSHL__(iAngle1Calc >> 31, 15) + (iAngle1Calc >> 31 << 15))) >> 15) + 520;
      //iRandVal2 = rand();                       // Generate random angle2 (phi) for spherical coordinates
      //
      ////TODO look at this
      //iAngle2 = GetHighOrderRand(2, iRandVal2);  // Will be 0 or 1
      ////iAngle2 = ((iRandVal2 << 14) - (__CFSHL__(iRandVal2 << 14 >> 31, 15) + (iRandVal2 << 14 >> 31 << 15))) >> 15;
      
      iAngle1 = (rand() & 0x0FFF) + 0x200;
      iAngle2 = rand() & 0x3FFF;
      
      fCos1Cos2 = tcos[iAngle2] * tcos[iAngle1];// Calculate rotation matrix elements using trigonometric tables
      fSin1Cos2 = tsin[iAngle2] * tcos[iAngle1];
      fNegSin1 = -tsin[iAngle2];
      fSin2 = tsin[iAngle1];
      fCos1 = tcos[iAngle2];
      fRotComp1 = -fCos1 * fSin2;               // Calculate rotation matrix components: -cos(phi)*sin(theta) and -sin(phi)*sin(theta)
      fRotComp2 = fNegSin1 * fSin2;
      fNegRadius = -fRadius;                    // Negative radius for translation calculations
      fBaseX = 10000000.0f * fCos1Cos2;          // Calculate 3x4 transformation matrix for cloud positioning
      fTransX1 = fNegRadius * fNegSin1;
      fMatrix00 = fBaseX + fTransX1;
      dRadiusRotComp1 = fRadius * fRotComp1;
      cloud[iCloudIdx].matrix[0][0] = fMatrix00 + (float)dRadiusRotComp1;
      fBaseY = 10000000.0f * fSin1Cos2;
      fTransY1 = fNegRadius * fCos1;
      fMatrix01 = fBaseY + fTransY1;
      dRadiusRotComp2 = fRadius * fRotComp2;
      cloud[iCloudIdx].matrix[0][1] = fMatrix01 + (float)dRadiusRotComp2;
      fBaseZ = 10000000.0f * fSin2;
      fTransZ1 = 0.0f * fNegRadius;
      fMatrix02 = fBaseZ + fTransZ1;
      fCos2 = tcos[iAngle1];
      fRadiusComp = fRadius * fCos2;
      cloud[iCloudIdx].matrix[0][2] = fMatrix02 + fRadiusComp;
      fTransX2 = fRadius * fNegSin1;
      dBaseCalcX = fBaseX + fTransX2;
      fMatrix10 = (float)dBaseCalcX;
      cloud[iCloudIdx].matrix[1][0] = (float)(dRadiusRotComp1 + dBaseCalcX);
      fTransY2 = fRadius * fCos1;
      dBaseCalcY = fBaseY + fTransY2;
      fMatrix11 = (float)dBaseCalcY;
      iValidPlacement = -1;
      cloud[iCloudIdx].matrix[1][1] = (float)(dRadiusRotComp2 + dBaseCalcY);
      dMatrix12 = 0.0 * fRadius + fBaseZ;
      cloud[iCloudIdx].matrix[1][2] = (float)dMatrix12 + fRadiusComp;
      dMatrix20Temp = fNegRadius * fRotComp1;
      cloud[iCloudIdx].matrix[2][0] = fMatrix10 + (float)dMatrix20Temp;
      dMatrix21Temp = fNegRadius * fRotComp2;
      cloud[iCloudIdx].matrix[2][1] = fMatrix11 + (float)dMatrix21Temp;
      dMatrix22Temp = fNegRadius * fCos2;
      cloud[iCloudIdx].matrix[2][2] = (float)(dMatrix12 + dMatrix22Temp);
      cloud[iCloudIdx].matrix[3][0] = (float)dMatrix20Temp + fMatrix00;
      cloud[iCloudIdx].matrix[3][1] = (float)dMatrix21Temp + fMatrix01;
      cloud[iCloudIdx].matrix[3][2] = (float)dMatrix22Temp + fMatrix02;
      cloud[iCloudIdx].world.fX = fRotComp1 * 0.0f + 0.0f * fNegSin1 + fBaseX;// World X coordinate: final cloud position after transformation
      cloud[iCloudIdx].world.fY = 0.0f * fCos1 + fBaseY + 0.0f * fRotComp2;// World Y coordinate: final cloud position after transformation
      cloud[iCloudIdx].world.fZ = fBaseZ + 0.0f * fCos2;// World Z coordinate: final cloud position after transformation
      iRandColorBase = rand();                  // Generate random cloud color/texture index
      iColorIndex = GetHighOrderRand(5, iRandColorBase);
      //iColorIndex = (5 * iRandColorBase - (__CFSHL__((5 * iRandColorBase) >> 31, 15) + ((5 * iRandColorBase) >> 31 << 15))) >> 15;
      cloud[iCloudIdx].iSurfaceType = iColorIndex;
      cloud[iCloudIdx].iSurfaceType = iColorIndex + 0x508;
      if (ROLLERrand() < 0x4000)                    // Randomly modify color properties (50% chance each)
        cloud[iCloudIdx].iSurfaceType += 0x1000;
      if (ROLLERrand() < 0x4000)
        SET_HIWORD(cloud[iCloudIdx].iSurfaceType, 4 + GET_HIWORD(cloud[iCloudIdx].iSurfaceType));
        //HIWORD(cloud[iCloudIdx].iSurfaceType) += 4;
      cloud[iCloudIdx].fRadius = fRadius;       // Store cloud radius
      if (iCloudIdx > 0) {                                         // Check collision with previously placed clouds
        for (iCheckIdx = 0; iCheckIdx < iCloudIdx; ++iCheckIdx) {
          if (iValidPlacement) {
            fDeltaX = cloud[iCheckIdx].world.fX - cloud[iCloudIdx].world.fX;
            fDeltaY = cloud[iCheckIdx].world.fY - cloud[iCloudIdx].world.fY;
            fXYDistSq = fDeltaX * fDeltaX + fDeltaY * fDeltaY;
            fDeltaZ = cloud[iCheckIdx].world.fZ - cloud[iCloudIdx].world.fZ;
            fDistance = (float)sqrt(fXYDistSq + fDeltaZ * fDeltaZ);// Calculate 3D distance between cloud centers
          }
          fMinDistance = fRadius + cloud[iCloudIdx].fRadius;
          //TODO
          if (fDistance < fMinDistance * 1.3f) // Check if clouds overlap (distance < combined radii * 1.3)
            iValidPlacement = 0;
        }
      }
      if (!iValidPlacement)
        fRadius = fRadius + -2000.0f;            // Reduce radius and retry if collision detected
      if (fRadius < 1000000.0f)                // Minimum radius constraint
        fRadius = 1000000.0f;
    } while (!iValidPlacement);
  }
}

//-------------------------------------------------------------------------------------------------
//0006B530
void displayclouds(uint8 *pScrBuf)
{
  int iCloudIdx; // esi
  double dCorner0X; // st7
  double dCorner0Y; // st6
  double dCorner0Z; // st5
  double dCorner0ViewZ; // st7
  int iBehindCamera; // edx
  double dViewDist; // st7
  double dInvZ0; // st6
  double dProjX0; // st5
  double dProjY0; // st7
  int iScrSizeTemp; // ecx
  //int iXpTemp; // eax
  double dScreenX0; // st7
  double dScreenY0; // st7
  double dCorner1X; // st7
  double dCorner1Y; // st6
  double dCorner1Z; // st5
  double dCorner1ViewZ; // st7
  double dViewDist1; // st7
  double dInvZ1; // st6
  double dProjX1; // st5
  double dProjY1; // st7
  int iScrSize1; // ebx
  //int iXpTemp1; // eax
  double dScreenX1; // st7
  double dScreenY1; // st7
  double dCorner2X; // st7
  double dCorner2Y; // st6
  double dCorner2Z; // st5
  double dCorner2ViewZ; // st7
  double dViewDist2; // st7
  double dInvZ2; // st6
  double dProjX2; // st5
  double dProjY2; // st7
  int iScrSize2; // ebx
  //int iXpTemp2; // eax
  double dScreenX2; // st7
  double dScreenY2; // st7
  double dCorner3X; // st7
  double dCorner3Y; // st6
  double dCorner3Z; // st5
  double dCorner3ViewZ; // st7
  double dViewDist3; // st7
  double dInvZ3; // st6
  double dProjX3; // st5
  double dProjY3; // st7
  int iScrSize3; // ebx
  //int iXpTemp3; // eax
  double dScreenX3; // st7
  double dScreenY3; // st7
  tPolyParams poly; // [esp+0h] [ebp-A8h] BYREF
  int iYCalcTemp; // [esp+38h] [ebp-70h]
  float fViewX0; // [esp+3Ch] [ebp-6Ch]
  float fViewY0; // [esp+40h] [ebp-68h]
  float fViewY1; // [esp+44h] [ebp-64h]
  float fViewX3; // [esp+48h] [ebp-60h]
  float fViewX2; // [esp+4Ch] [ebp-5Ch]
  float fViewY3; // [esp+50h] [ebp-58h]
  float fViewY2; // [esp+54h] [ebp-54h]
  float fViewX1; // [esp+58h] [ebp-50h]
  float fScreenX0; // [esp+5Ch] [ebp-4Ch]
  float fViewZ0; // [esp+60h] [ebp-48h]
  float fScreenY0; // [esp+64h] [ebp-44h]
  float fScreenX2; // [esp+68h] [ebp-40h]
  float fScreenX1; // [esp+6Ch] [ebp-3Ch]
  float fViewZ3; // [esp+70h] [ebp-38h]
  float fViewZ2; // [esp+74h] [ebp-34h]
  float fViewZ1; // [esp+78h] [ebp-30h]
  float fScreenX3; // [esp+7Ch] [ebp-2Ch]
  float fScreenY1; // [esp+80h] [ebp-28h]
  float fScreenY3; // [esp+84h] [ebp-24h]
  float fScreenY2; // [esp+88h] [ebp-20h]
  //int iScreenXTemp; // [esp+8Ch] [ebp-1Ch]

  set_starts(0);                                // Initialize polygon renderer
  if ((textures_off & 8) == 0)                // Skip cloud rendering if textures disabled (bit 3 of textures_off)
  {                                             // Loop through all 40 cloud objects
    for (iCloudIdx = 0; iCloudIdx != 40; ++iCloudIdx) {
      dCorner0X = cloud[iCloudIdx].matrix[0][0] - viewx;// Transform cloud corner 0 from world space to camera space
      dCorner0Y = cloud[iCloudIdx].matrix[0][1] - viewy;
      dCorner0Z = cloud[iCloudIdx].matrix[0][2] * 0.5 - viewz;
      fViewX0 = (float)(dCorner0X * vk1 + dCorner0Y * vk4 + dCorner0Z * vk7);// Apply view matrix transformation to get view coordinates
      fViewY0 = (float)(dCorner0X * vk2 + dCorner0Y * vk5 + dCorner0Z * vk8);
      dCorner0ViewZ = dCorner0X * vk3 + dCorner0Y * vk6 + dCorner0Z * vk9;
      fViewZ0 = (float)dCorner0ViewZ;
      iBehindCamera = 0;                        // Check for near clipping (minimum Z distance = 80.0)
      if (dCorner0ViewZ < 80.0) {
        iBehindCamera = 1;
        fViewZ0 = 80.0;
      }
      dViewDist = (double)VIEWDIST;             // Project 3D coordinates to 2D screen coordinates using perspective division
      dInvZ0 = 1.0 / fViewZ0;
      dProjX0 = dViewDist * fViewX0 * dInvZ0 + (double)xbase;
      //_CHP();
      xp = (int)dProjX0;
      dProjY0 = dInvZ0 * (dViewDist * fViewY0) + (double)ybase;
      iScrSizeTemp = scr_size;
      //_CHP();
      yp = (int)dProjY0;
      fScreenX0 = (float)((long long)iScrSizeTemp * xp >> 6);        // Convert to screen space and check clipping bounds (-5000 to +5000)
      //long long cast added by ROLLER to avoid integer overflow
      iYCalcTemp = (int)((long long)iScrSizeTemp * (199 - (int)dProjY0)) >> 6;
      fScreenY0 = (float)iYCalcTemp;
      if (iBehindCamera || fScreenX0 >= -5000.0 && fScreenX0 <= 5000.0 && fScreenY0 >= -5000.0 && fScreenY0 <= 5000.0) {
        dScreenX0 = fScreenX0;                  // Store vertex 0 coordinates in polygon structure
        //_CHP();
        poly.vertices[0].x = (int)dScreenX0;
        dScreenY0 = fScreenY0;
        //_CHP();
        poly.vertices[0].y = (int)dScreenY0;
      } else {
        iBehindCamera = 1;
      }
      if (!iBehindCamera) {
        dCorner1X = cloud[iCloudIdx].matrix[1][0] - viewx;// Transform cloud corner 1 (repeat process for second vertex)
        dCorner1Y = cloud[iCloudIdx].matrix[1][1] - viewy;
        dCorner1Z = cloud[iCloudIdx].matrix[1][2] * 0.5 - viewz;
        fViewX1 = (float)(dCorner1X * vk1 + dCorner1Y * vk4 + dCorner1Z * vk7);
        fViewY1 = (float)(dCorner1X * vk2 + dCorner1Y * vk5 + dCorner1Z * vk8);
        dCorner1ViewZ = dCorner1X * vk3 + dCorner1Y * vk6 + dCorner1Z * vk9;
        fViewZ1 = (float)dCorner1ViewZ;
        if (dCorner1ViewZ < 80.0) {
          iBehindCamera = 1;
          fViewZ1 = 80.0;
        }
        dViewDist1 = (double)VIEWDIST;
        dInvZ1 = 1.0 / fViewZ1;
        dProjX1 = dViewDist1 * fViewX1 * dInvZ1 + (double)xbase;
        //_CHP();
        xp = (int)dProjX1;
        dProjY1 = dInvZ1 * (dViewDist1 * fViewY1) + (double)ybase;
        iScrSize1 = scr_size;
        //_CHP();
        yp = (int)dProjY1;
        //iScreenXTemp = xp >> 6;
        fScreenX1 = (float)((long long)iScrSize1 * xp >> 6);
        iYCalcTemp = ((long long)iScrSize1 * (199 - (int)dProjY1)) >> 6;
        fScreenY1 = (float)iYCalcTemp;
        if (iBehindCamera || fScreenX1 >= -5000.0 && fScreenX1 <= 5000.0 && fScreenY1 >= -5000.0 && fScreenY1 <= 5000.0) {
          dScreenX1 = fScreenX1;                // Store vertex 1 coordinates in polygon structure
          //_CHP();
          poly.vertices[1].x = (int)dScreenX1;
          dScreenY1 = fScreenY1;
          //_CHP();
          poly.vertices[1].y = (int)dScreenY1;
        } else {
          iBehindCamera = 1;
        }
        if (!iBehindCamera) {
          dCorner2X = cloud[iCloudIdx].matrix[2][0] - viewx;// Transform cloud corner 2 (third vertex of quad)
          dCorner2Y = cloud[iCloudIdx].matrix[2][1] - viewy;
          dCorner2Z = cloud[iCloudIdx].matrix[2][2] * 0.5 - viewz;
          fViewX2 = (float)(dCorner2X * vk1 + dCorner2Y * vk4 + dCorner2Z * vk7);
          fViewY2 = (float)(dCorner2X * vk2 + dCorner2Y * vk5 + dCorner2Z * vk8);
          dCorner2ViewZ = dCorner2X * vk3 + dCorner2Y * vk6 + dCorner2Z * vk9;
          fViewZ2 = (float)dCorner2ViewZ;
          if (dCorner2ViewZ < 80.0) {
            iBehindCamera = 1;
            fViewZ2 = 80.0;
          }
          dViewDist2 = (double)VIEWDIST;
          dInvZ2 = 1.0 / fViewZ2;
          dProjX2 = dViewDist2 * fViewX2 * dInvZ2 + (double)xbase;
          //_CHP();
          xp = (int)dProjX2;
          dProjY2 = dInvZ2 * (dViewDist2 * fViewY2) + (double)ybase;
          iScrSize2 = scr_size;
          //_CHP();
          yp = (int)dProjY2;
          fScreenX2 = (float)((long long)iScrSize2 * xp >> 6);
          iYCalcTemp = ((long long)iScrSize2 * (199 - (int)dProjY2)) >> 6;
          fScreenY2 = (float)iYCalcTemp;
          if (iBehindCamera || fScreenX2 >= -5000.0 && fScreenX2 <= 5000.0 && fScreenY2 >= -5000.0 && fScreenY2 <= 5000.0) {
            dScreenX2 = fScreenX2;              // Store vertex 2 coordinates in polygon structure
            //_CHP();
            poly.vertices[2].x = (int)dScreenX2;
            dScreenY2 = fScreenY2;
            //_CHP();
            poly.vertices[2].y = (int)dScreenY2;
          } else {
            iBehindCamera = 1;
          }
          if (!iBehindCamera) {
            dCorner3X = cloud[iCloudIdx].matrix[3][0] - viewx;// Transform cloud corner 3 (final vertex of quad)
            dCorner3Y = cloud[iCloudIdx].matrix[3][1] - viewy;
            dCorner3Z = cloud[iCloudIdx].matrix[3][2] * 0.5 - viewz;
            fViewX3 = (float)(dCorner3X * vk1 + dCorner3Y * vk4 + dCorner3Z * vk7);
            fViewY3 = (float)(dCorner3X * vk2 + dCorner3Y * vk5 + dCorner3Z * vk8);
            dCorner3ViewZ = dCorner3X * vk3 + dCorner3Y * vk6 + dCorner3Z * vk9;
            fViewZ3 = (float)dCorner3ViewZ;
            if (dCorner3ViewZ < 80.0) {
              iBehindCamera = 1;
              fViewZ3 = 80.0;
            }
            dViewDist3 = (double)VIEWDIST;
            dInvZ3 = 1.0 / fViewZ3;
            dProjX3 = dViewDist3 * fViewX3 * dInvZ3 + (double)xbase;
            //_CHP();
            xp = (int)dProjX3;
            dProjY3 = dInvZ3 * (dViewDist3 * fViewY3) + (double)ybase;
            iScrSize3 = scr_size;
            //_CHP();
            yp = (int)dProjY3;
            fScreenX3 = (float)((long long)iScrSize3 * xp >> 6);
            iYCalcTemp = ((long long)iScrSize3 * (199 - (int)dProjY3)) >> 6;
            fScreenY3 = (float)iYCalcTemp;
            if (iBehindCamera || fScreenX3 >= -5000.0 && fScreenX3 <= 5000.0 && fScreenY3 >= -5000.0 && fScreenY3 <= 5000.0) {
              dScreenX3 = fScreenX3;            // Store vertex 3 coordinates in polygon structure
              //_CHP();
              poly.vertices[3].x = (int)dScreenX3;
              dScreenY3 = fScreenY3;
              //_CHP();
              poly.vertices[3].y = (int)dScreenY3;
            } else {
              iBehindCamera = 1;
            }
            if (!iBehindCamera) {
              poly.iSurfaceType = cloud[iCloudIdx].iSurfaceType;// Set polygon surface type and vertex count (quad = 4 vertices)
              poly.uiNumVerts = -4;
              POLYTEX(cargen_vga, pScrBuf, &poly, 18, gfx_size);// Render textured polygon to screen buffer
            }
          }
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------