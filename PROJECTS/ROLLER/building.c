#include "building.h"
#include "loadtrak.h"
#include "drawtrk3.h"
#include "transfrm.h"
#include "plans.h"
#include "graphics.h"
#include "polytex.h"
#include <float.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
//-------------------------------------------------------------------------------------------------

uint8 BuildingSub[24] =                 //000A745C
{
  1u, 1u, 1u, 1u, 20u, 1u, 1u, 20u, 1u, 1u, 1u, 1u,
  1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u, 0u
};
tBuildingZOrderEntry BuildingZOrder[32];//0018EEC0
int BuildingSect[MAX_TRACK_CHUNKS];     //0018F040
tVec3 BuildingView[32];                 //0018F810
float BuildingAngles[768];              //0018F990
int BuildingBase[256][4];               //00190590
tVec3 BuildingBox[256][8];              //00191590
tBuildingCoord BuildingCoords[32];      //00197590
float BuildingBaseX[256];               //00197710
float BuildingBaseY[256];               //00197B10
float BuildingBaseZ[256];               //00197F10
float BuildingX[256];                   //00198310
float BuildingY[256];                   //00198710
float BuildingZ[256];                   //00198B10
tVisibleBuilding VisibleBuildings[256]; //00198F10
int16 advert_list[256];                 //00199710
tPolyParams BuildingPol;                //00199910
int NumBuildings;                       //0019993C
int NumVisibleBuildings;                //00199940

//-------------------------------------------------------------------------------------------------
//000691B0
void InitBuildings()
{
  int iBuildingIdx_1; // edi
  int iBuildingIdx_2; // esi
  unsigned int iBuildingType; // eax
  tBuildingPlan *pBuildingPlan; // ecx
  tPolygon *pPols; // ebx
  int iNextSect; // edx
  tData *pTrackData; // eax
  tData *pNextTrackData; // edx
  int iHalfHeight; // eax
  int byNumPols; // edx
  float *p_fX; // edx
  int i; // ebx
  double dTempY; // st7
  float *pYCoord; // edx
  double dTempZ; // st7
  float *pZCoord; // edx
  //unsigned int uiBuildingBoxOffset; // edx
  double dDeltaY; // [esp+0h] [ebp-118h]
  double dDeltaX; // [esp+18h] [ebp-100h]
  double dCosPitch; // [esp+28h] [ebp-F0h]
  double dCosYaw; // [esp+38h] [ebp-E0h]
  double dCosRoll; // [esp+40h] [ebp-D8h]
  double dSinYaw; // [esp+48h] [ebp-D0h]
  double dSinRoll; // [esp+50h] [ebp-C8h]
  double dSinPitch; // [esp+58h] [ebp-C0h]
  double dDistance; // [esp+60h] [ebp-B8h]
  float fZ; // [esp+70h] [ebp-A8h]
  float fY; // [esp+80h] [ebp-98h]
  float fX; // [esp+84h] [ebp-94h]
  float fHalfWidth; // [esp+8Ch] [ebp-8Ch]
  float fNegTrackY; // [esp+90h] [ebp-88h]
  float fNegTrackX; // [esp+94h] [ebp-84h]
  float fNegTrackZ; // [esp+98h] [ebp-80h]
  int iBuildingBoxStride; // [esp+9Ch] [ebp-7Ch]
  float fRollAngle; // [esp+A0h] [ebp-78h]
  float fPitchAngle; // [esp+A4h] [ebp-74h]
  int iBuildingSect; // [esp+A8h] [ebp-70h]
  float fYawAngle; // [esp+ACh] [ebp-6Ch]
  int iAngleIdx; // [esp+B0h] [ebp-68h]
  int iBuildingIdx; // [esp+B4h] [ebp-64h]
  float fMatrix20; // [esp+B8h] [ebp-60h]
  float fMatrix21; // [esp+BCh] [ebp-5Ch]
  float fMatrix12; // [esp+C0h] [ebp-58h]
  float fMatrix10; // [esp+C4h] [ebp-54h]
  float fMatrix00; // [esp+C8h] [ebp-50h]
  float fMatrix02; // [esp+CCh] [ebp-4Ch]
  float fMatrix22; // [esp+D0h] [ebp-48h]
  float fMatrix11; // [esp+D4h] [ebp-44h]
  float fMatrix01; // [esp+D8h] [ebp-40h]
  float fBuildingY; // [esp+DCh] [ebp-3Ch]
  float fBuildingX; // [esp+E0h] [ebp-38h]
  float fBuildingZ; // [esp+E4h] [ebp-34h]
  float fMinY; // [esp+E8h] [ebp-30h]
  float fMaxZ; // [esp+ECh] [ebp-2Ch]
  float fMinX; // [esp+F0h] [ebp-28h]
  float fMaxY; // [esp+F4h] [ebp-24h]
  float fMinZ; // [esp+F8h] [ebp-20h]
  float fMaxX; // [esp+FCh] [ebp-1Ch]

  memset(BuildingSect, 255, sizeof(BuildingSect));// Initialize building sector map to -1 (no buildings)
  iBuildingIdx = 0;
  if (NumBuildings > 0) {
    iAngleIdx = 0;
    iBuildingIdx_1 = 0;
    iBuildingIdx_2 = 0;
    iBuildingBoxStride = 96;
    do {
      iBuildingType = BuildingBase[iBuildingIdx_1][0];
      if (iBuildingType <= 0x10)              // Check if building type is valid (<=16)
      {
        pBuildingPlan = &BuildingPlans[iBuildingType];
        pPols = pBuildingPlan->pPols;
        iBuildingSect = BuildingBase[iBuildingIdx_1][1];
        iNextSect = iBuildingSect + 1;          // Get next track segment, wrap around if needed
        fHalfWidth = (float)(32 * BuildingBase[iBuildingIdx_1][2] / 2);// Calculate half width of building from BuildingBase[2]
        if (iBuildingSect + 1 >= TRAK_LEN)
          iNextSect -= TRAK_LEN;
        pTrackData = &localdata[iBuildingSect];
        fNegTrackX = -pTrackData->pointAy[3].fX;
        fNegTrackY = -pTrackData->pointAy[3].fY;
        fNegTrackZ = -pTrackData->pointAy[3].fZ;
        pNextTrackData = &localdata[iNextSect];
        dDeltaY = pTrackData->pointAy[3].fY - pNextTrackData->pointAy[3].fY;// Calculate track direction vector between current and next segment
        dDeltaX = pTrackData->pointAy[3].fX - pNextTrackData->pointAy[3].fX;
        dDistance = sqrt(dDeltaX * dDeltaX + dDeltaY * dDeltaY);// Calculate distance and normalize direction vector
        //if ((HIDWORD(dDeltaX) & 0x7FFFFFFF) != 0 || LODWORD(dDeltaX))
        if (dDeltaX != 0)
          dDeltaX = (pTrackData->pointAy[3].fX - pNextTrackData->pointAy[3].fX) / dDistance;
        //if ((HIDWORD(dDeltaY) & 0x7FFFFFFF) != 0 || LODWORD(dDeltaY))
        if (dDeltaY != 0)
          dDeltaY = dDeltaY / dDistance;
        iHalfHeight = 32 * BuildingBase[iBuildingIdx_1][3] / 2;
        BuildingX[iBuildingIdx_2] = fNegTrackX - (float)dDeltaY * fHalfWidth;// Calculate building position offset from track centerline
        BuildingY[iBuildingIdx_2] = fHalfWidth * (float)dDeltaX + fNegTrackY;
        BuildingSect[iBuildingSect] = iBuildingIdx;
        byNumPols = 0;
        BuildingZ[iBuildingIdx_2] = (float)iHalfHeight + fNegTrackZ;
        while (byNumPols < pBuildingPlan->byNumPols) {                                       // Skip polygons with special texture flag (0x200)
          if ((pPols->uiTex & 0x200) != 0)
            byNumPols = pBuildingPlan->byNumPols;
          ++pPols;
          ++byNumPols;
        }
        fMinZ = 1073741800.0f;                   // Initialize min/max values for bounding box calculation
        fMinY = 1073741800.0f;
        fMinX = 1073741800.0f;
        p_fX = &pBuildingPlan->pCoords->fX;
        fMaxZ = -1073741800.0f;
        fMaxY = -1073741800.0f;
        fMaxX = -1073741800.0f;
        for (i = 0; i < pBuildingPlan->byNumCoords; ++i)// Find min/max coordinates for building bounding box
        {
          if (*p_fX < (double)fMinX)
            fMinX = *p_fX;
          if (*p_fX > (double)fMaxX)
            fMaxX = *p_fX;
          dTempY = p_fX[1];
          pYCoord = p_fX + 1;
          if (dTempY < fMinY)
            fMinY = *pYCoord;
          if (*pYCoord > (double)fMaxY)
            fMaxY = *pYCoord;
          dTempZ = pYCoord[1];
          pZCoord = pYCoord + 1;
          if (dTempZ < fMinZ)
            fMinZ = *pZCoord;
          if (*pZCoord > (double)fMaxZ)
            fMaxZ = *pZCoord;
          p_fX = pZCoord + 1;
        }
        if ((cheat_mode & CHEAT_MODE_DOUBLE_TRACK) != 0)       // Scale building 2x if cheat mode 0x1000 is enabled
        {
          fMinX = fMinX * 2.0f;
          fMinY = fMinY * 2.0f;
          fMinZ = fMinZ * 2.0f;
          fMaxX = fMaxX * 2.0f;
          fMaxY = fMaxY * 2.0f;
          fMaxZ = fMaxZ * 2.0f;
        }
        fYawAngle = BuildingAngles[iAngleIdx] * 0.0174532925199f;// Convert building rotation angles from degrees to radians
        fPitchAngle = BuildingAngles[iAngleIdx + 1] * 0.0174532925199f;
        fRollAngle = BuildingAngles[iAngleIdx + 2] * 0.0174532925199f;
        fBuildingX = BuildingX[iBuildingIdx_2];
        fBuildingY = BuildingY[iBuildingIdx_2];
        fBuildingZ = BuildingZ[iBuildingIdx_2];
        dCosYaw = cos(fYawAngle);               // Calculate sine and cosine values for 3D rotation matrix
        dCosPitch = cos(fPitchAngle);
        dSinYaw = sin(fYawAngle);
        dSinPitch = sin(fPitchAngle);
        dSinRoll = sin(fRollAngle);
        dCosRoll = cos(fRollAngle);
        //uiBuildingBoxOffset = 96 * iBuildingIdx;
        BuildingBox[iBuildingIdx][0].fX = fMinX;// Set up 8 corners of building bounding box (cube vertices)
        BuildingBox[iBuildingIdx][0].fY = fMinY;
        BuildingBox[iBuildingIdx][0].fZ = fMinZ;
        BuildingBox[iBuildingIdx][1].fX = fMaxX;
        BuildingBox[iBuildingIdx][1].fY = fMinY;
        BuildingBox[iBuildingIdx][1].fZ = fMinZ;
        BuildingBox[iBuildingIdx][2].fX = fMaxX;
        BuildingBox[iBuildingIdx][2].fY = fMaxY;
        BuildingBox[iBuildingIdx][2].fZ = fMinZ;
        BuildingBox[iBuildingIdx][3].fX = fMinX;
        BuildingBox[iBuildingIdx][3].fY = fMaxY;
        BuildingBox[iBuildingIdx][3].fZ = fMinZ;
        BuildingBox[iBuildingIdx][4].fX = fMinX;
        BuildingBox[iBuildingIdx][4].fY = fMinY;
        BuildingBox[iBuildingIdx][4].fZ = fMaxZ;
        BuildingBox[iBuildingIdx][5].fX = fMaxX;
        BuildingBox[iBuildingIdx][5].fY = fMinY;
        BuildingBox[iBuildingIdx][5].fZ = fMaxZ;
        BuildingBox[iBuildingIdx][6].fX = fMaxX;
        BuildingBox[iBuildingIdx][6].fY = fMaxY;
        BuildingBox[iBuildingIdx][6].fZ = fMaxZ;
        BuildingBox[iBuildingIdx][7].fX = fMinX;
        BuildingBox[iBuildingIdx][7].fY = fMaxY;
        BuildingBox[iBuildingIdx][7].fZ = fMaxZ;
        int iPoint = 0;
        do {
          fX = BuildingBox[iBuildingIdx][iPoint].fX;// Apply 3D rotation matrix and translate each bounding box corner
          fY = BuildingBox[iBuildingIdx][iPoint].fY;
          fZ = BuildingBox[iBuildingIdx][iPoint].fZ;
          fMatrix00 = (float)(dCosYaw * dCosPitch);
          fMatrix02 = (float)(-dCosYaw * dSinPitch * dCosRoll - dSinYaw * dSinRoll);
          fMatrix01 = (float)(dCosYaw * dSinPitch * dSinRoll - dSinYaw * dCosRoll);
          BuildingBox[iBuildingIdx][iPoint].fX = fX * fMatrix00 + fY * fMatrix01 + fZ * fMatrix02 + fBuildingX;
          fMatrix12 = (float)(-dSinYaw * dSinPitch * dCosRoll + dCosYaw * dSinRoll);
          fMatrix10 = (float)(dSinYaw * dCosPitch);
          fMatrix11 = (float)(dSinYaw * dSinPitch * dSinRoll + dCosYaw * dCosRoll);
          BuildingBox[iBuildingIdx][iPoint].fY = fX * fMatrix10 + fY * fMatrix11 + fZ * fMatrix12 + fBuildingY;
          //uiBuildingBoxOffset += sizeof(tVec3);
          fMatrix20 = (float)(dSinPitch);
          fMatrix22 = (float)(dCosRoll * dCosPitch);
          fMatrix21 = (float)(-dSinRoll * dCosPitch);
          BuildingBox[iBuildingIdx][iPoint].fZ = fX * fMatrix20 + fY * fMatrix21 + fZ * fMatrix22 + fBuildingZ;
          ++iPoint;
          //*(float *)&BuildingBase[255][uiBuildingBoxOffset / 4 + 3] = fX * fMatrix20 + fY * fMatrix21 + fZ * fMatrix22 + fBuildingZ;// reference to BuildingBox
        } while (iPoint < 8); //while (uiBuildingBoxOffset != iBuildingBoxStride);// 
        BuildingBaseX[iBuildingIdx_2] = BuildingX[iBuildingIdx_2];// Store final building position in base arrays
        BuildingBaseY[iBuildingIdx_2] = BuildingY[iBuildingIdx_2];
        BuildingBaseZ[iBuildingIdx_2] = BuildingZ[iBuildingIdx_2];
      }
      ++iBuildingIdx_1;
      ++iBuildingIdx_2;
      iAngleIdx += 3;
      iBuildingBoxStride += 96;
      ++iBuildingIdx;
    } while (iBuildingIdx < NumBuildings);
  }
}

//-------------------------------------------------------------------------------------------------
//00069960
void CalcVisibleBuildings()
{
  int iTrackLen; // ebp
  tVisibleBuilding *pVisibleBuilding; // edi
  int iTrackSectIdx; // ecx
  int iBuildingCounter; // edx
  int iWrappedSectIdx; // edx
  int iBuildingIdx; // esi
  unsigned int uiBuildingType; // edx
  //int iPointOffset; // edx
  double dPointDepth; // st7
  //int iPointOffset2; // edx
  double dPointDepth2; // st7
  int iCurrentCount; // eax
  //float fTempDepth; // [esp+18h] [ebp-24h]
  //float fTempDepth2; // [esp+1Ch] [ebp-20h]
  float fBuildingDepth; // [esp+20h] [ebp-1Ch]

  iTrackLen = TRAK_LEN;                         // Store track length for later restoration
  pVisibleBuilding = VisibleBuildings;          // Get pointer to visible buildings array
  iTrackSectIdx = TrackSize;
  VisibleBuildings[0].iBuildingIdx = -1;        // Initialize visible buildings array with terminator
  NumVisibleBuildings = 0;                      // Reset visible building count
  for (iBuildingCounter = 0; iBuildingCounter < NumBuildings; ++iBuildingCounter)// Count through all buildings (purpose unclear - possibly validation)
    ;
  if ((textures_off & 0x200) == 0)            // Check if building textures are enabled (bit 9 of textures_off)
  {
    while (1) {
      if (iTrackSectIdx < 0)
        goto FUNCTION_EXIT;                     // Start of main track section loop - iterate backwards through track
      if (iTrackSectIdx <= first_size || iTrackSectIdx >= gap_size)// Skip sections in track gap (between first_size and gap_size)
      {
        iWrappedSectIdx = iTrackSectIdx + start_sect;// Calculate wrapped track section index with start_sect offset
        if (iTrackSectIdx + start_sect < 0)   // Handle negative wraparound
          iWrappedSectIdx += iTrackLen;
        if (iWrappedSectIdx >= iTrackLen)     // Handle positive wraparound
          iWrappedSectIdx -= iTrackLen;
        iBuildingIdx = BuildingSect[iWrappedSectIdx];// Get building index for this track section
        if (iBuildingIdx != -1)
          break;                                // Skip if no building in this section (-1)
      }
    NEXT_TRACK_SECTION:
      --iTrackSectIdx;                          // Continue to next track section
    }
    uiBuildingType = BuildingBase[iBuildingIdx][0];// Get building type from BuildingBase array
    if (uiBuildingType < 4)                   // Check building type - types 0,1 use max depth, others use min depth
    {
      if (uiBuildingType > 1)
        goto CALC_MIN_DEPTH;
    } else if (uiBuildingType > 7 && uiBuildingType != 14)// Types 8+ except 14 use min depth calculation
    {
    CALC_MIN_DEPTH:
      // Calculate min depth
      fBuildingDepth = FLT_MAX;  // Initialize to very large value
      for (int iPointIdx = 0; iPointIdx < 8; iPointIdx++)
      {
        dPointDepth2 = (BuildingBox[iBuildingIdx][iPointIdx].fX - viewx) * vk3
                     + (BuildingBox[iBuildingIdx][iPointIdx].fY - viewy) * vk6  
                     + (BuildingBox[iBuildingIdx][iPointIdx].fZ - viewz) * vk9;
         
        if (dPointDepth2 < fBuildingDepth)
        {
          fBuildingDepth = (float)dPointDepth2;
        }
      }
      //iPointOffset2 = 96 * iBuildingIdx + 12;   // Calculate min depth for other building types - start with first point
      //fBuildingDepth = (BuildingBox[iBuildingIdx][0].fX - viewx) * vk3 + (BuildingBox[iBuildingIdx][0].fY - viewy) * vk6 + (BuildingBox[iBuildingIdx][0].fZ - viewz) * vk9;// Transform first bounding box point to view space depth
      //do {
      //  dPointDepth2 = (*(float *)((char *)&BuildingBox[0][0].fX + iPointOffset2) - viewx) * vk3
      //    + (*(float *)((char *)&BuildingBox[0][0].fY + iPointOffset2) - viewy) * vk6
      //    + (*(float *)((char *)&BuildingBox[0][0].fZ + iPointOffset2) - viewz) * vk9;// Transform current point to view space depth
      //  if (dPointDepth2 < fBuildingDepth)    // Keep minimum depth value
      //  {
      //    fTempDepth = dPointDepth2;
      //    fBuildingDepth = fTempDepth;
      //  }
      //  iPointOffset2 += 12;
      //} while (iPointOffset2 != 96 * iBuildingIdx + 96);// Loop through all 8 bounding box points
    ADD_BUILDING_TO_LIST:
      ++pVisibleBuilding;                       // Add building to visible list - advance pointer
      pVisibleBuilding[-1].fDepth = fBuildingDepth;// Store depth value in array (float)
      iCurrentCount = NumVisibleBuildings;
      pVisibleBuilding[-1].iBuildingIdx = iBuildingIdx;// Store building index in array
      pVisibleBuilding->iBuildingIdx = -1;      // Add array terminator (-1)
      NumVisibleBuildings = iCurrentCount + 1;  // Increment visible building count
      goto NEXT_TRACK_SECTION;
    }

    fBuildingDepth = -FLT_MAX;  // Initialize to very small value
    for (int iPointIdx = 0; iPointIdx < 8; iPointIdx++)
    {
      dPointDepth = (BuildingBox[iBuildingIdx][iPointIdx].fX - viewx) * vk3
                  + (BuildingBox[iBuildingIdx][iPointIdx].fY - viewy) * vk6  
                  + (BuildingBox[iBuildingIdx][iPointIdx].fZ - viewz) * vk9;
        
      if (dPointDepth > fBuildingDepth)
      {
        fBuildingDepth = (float)dPointDepth;
      }
    }
    //iPointOffset = 96 * iBuildingIdx + 12;      // Calculate max depth for building types 0,1 - start with first point
    //fBuildingDepth = (BuildingBox[iBuildingIdx][0].fX - viewx) * vk3 + (BuildingBox[iBuildingIdx][0].fY - viewy) * vk6 + (BuildingBox[iBuildingIdx][0].fZ - viewz) * vk9;// Transform first bounding box point to view space using view matrix
    //do {
    //  dPointDepth = (*(float *)((char *)&BuildingBox[0][0].fX + iPointOffset) - viewx) * vk3
    //    + (*(float *)((char *)&BuildingBox[0][0].fY + iPointOffset) - viewy) * vk6
    //    + (*(float *)((char *)&BuildingBox[0][0].fZ + iPointOffset) - viewz) * vk9;// Transform current point to view space depth
    //  if (dPointDepth > fBuildingDepth)       // Keep maximum depth value
    //  {
    //    fTempDepth2 = dPointDepth;
    //    fBuildingDepth = fTempDepth2;
    //  }
    //  iPointOffset += sizeof(tVec3);
    //} while (iPointOffset != 96 * iBuildingIdx + 96);// Loop through all 8 bounding box points (96 bytes total, 12 per point)
    goto ADD_BUILDING_TO_LIST;
  }
FUNCTION_EXIT:
  TRAK_LEN = iTrackLen;                         // Restore original TRAK_LEN value
}

//-------------------------------------------------------------------------------------------------
//00069C10
void DrawBuilding(int iBuildingIdx, uint8 *pScrPtr)
{
  tBuildingCoord *pScreenPt; // esi
  tVec3 *pBuildingView; // ecx
  tPolygon *pPols; // ebx
  tVec3 *pCoords; // edi
  int iClipped; // ebp
  float *p_fZ; // eax
  double dTransformedX; // st5
  double dTempX; // rt1
  double dTransformedY; // st5
  double dTempY; // rt2
  double dTransformedZ; // st5
  double dViewX; // st4
  double dViewY; // st4
  double dViewZ; // st7
  int iScreenY; // edx
  int *p_iUnk3; // esi
  int iPolygonLoop; // edi
  int iZOrderIdx; // edx
  float fMinZ1; // eax
  float fMinZ2; // eax
  float fPolygonZ; // eax
  float fZ; // eax
  float fMaxZ; // eax
  int iZOrderLoop; // edx
  int iPolygonLink; // esi
  int k; // ebx
  int iPolygonIndex; // ebx
  tPolygon *pPolygon; // ebp
  int iVert2; // eax
  float fY; // edx
  int iVert1; // eax
  float fYCoord1; // edx
  int iVert0; // eax
  int iVert3; // eax
  uint8 *pEndVerts; // edi
  int iProjectedSum; // ebx
  tPoint *pVerticesRev; // edx
  uint8 *pVerts_1; // ecx
  tBuildingCoord *pScreenCoordRev; // eax
  tPoint *pVertices; // edx
  uint8 *pVerts; // ecx
  tBuildingCoord *pScreenCoord; // eax
  int *p_y; // edx
  int iFinalVert0; // ebx
  int iFinalVert1; // esi
  int iFinalVert2; // ecx
  int iFinalVert3; // edx
  float fClosestZ1; // eax
  float fClosestZ2; // eax
  float fClosestZ; // eax
  int iZOrderOffset; // ebp
  double dCosYaw; // [esp+48h] [ebp-190h]
  double dCosRoll; // [esp+58h] [ebp-180h]
  double dSinYaw; // [esp+70h] [ebp-168h]
  double dSinPitch; // [esp+78h] [ebp-160h]
  double dCosPitch; // [esp+80h] [ebp-158h]
  double dSinRoll; // [esp+88h] [ebp-150h]
  float fCoordY; // [esp+90h] [ebp-148h]
  float fBuildingZ; // [esp+94h] [ebp-144h]
  int iViewY; // [esp+98h] [ebp-140h]
  float fBuildingX; // [esp+9Ch] [ebp-13Ch]
  float fX; // [esp+A0h] [ebp-138h]
  float fCoordZ; // [esp+A4h] [ebp-134h]
  float fBuildingY; // [esp+A8h] [ebp-130h]
  int iViewX; // [esp+ACh] [ebp-12Ch]
  float fVert1X; // [esp+B0h] [ebp-128h]
  float fVert1Y; // [esp+B0h] [ebp-128h]
  float fVert2Z; // [esp+B4h] [ebp-124h]
  float fDeltaZ1; // [esp+B4h] [ebp-124h]
  int iViewZ; // [esp+B8h] [ebp-120h]
  float fVert2X; // [esp+BCh] [ebp-11Ch]
  float fVert2Y; // [esp+C0h] [ebp-118h]
  float fDeltaY1; // [esp+C0h] [ebp-118h]
  float v75; // [esp+C4h] [ebp-114h]
  float fDeltaX2; // [esp+C4h] [ebp-114h]
  float fVert0Y; // [esp+C8h] [ebp-110h]
  float fMatrix01; // [esp+CCh] [ebp-10Ch]
  float fMatrix20; // [esp+D0h] [ebp-108h]
  float fMatrix10; // [esp+D4h] [ebp-104h]
  float fMatrix00; // [esp+D8h] [ebp-100h]
  float fZDepth; // [esp+E0h] [ebp-F8h]
  float fNormalDot; // [esp+F8h] [ebp-E0h]
  float fRollAngle; // [esp+104h] [ebp-D4h]
  float fRollRad; // [esp+104h] [ebp-D4h]
  int iBestZOrderIdx; // [esp+108h] [ebp-D0h]
  float fPitchAngle; // [esp+10Ch] [ebp-CCh]
  float fPitchRad; // [esp+10Ch] [ebp-CCh]
  float fDeltaY2; // [esp+118h] [ebp-C0h]
  float fMatrix12; // [esp+11Ch] [ebp-BCh]
  float fMatrix02; // [esp+120h] [ebp-B8h]
  float fVert0Z; // [esp+124h] [ebp-B4h]
  float fMatrix11; // [esp+128h] [ebp-B0h]
  float fVert1Z; // [esp+13Ch] [ebp-9Ch]
  float fDeltaZ2; // [esp+13Ch] [ebp-9Ch]
  float fTempZ; // [esp+140h] [ebp-98h]
  float fTempZ2; // [esp+154h] [ebp-84h]
  float fTempClosestZ; // [esp+170h] [ebp-68h]
  int iCurrentZOrderIdx; // [esp+184h] [ebp-54h]
  float *p_fY; // [esp+188h] [ebp-50h]
  int j; // [esp+190h] [ebp-48h]
  int iFoundPolygonLink; // [esp+194h] [ebp-44h]
  unsigned int uiBuildingType; // [esp+198h] [ebp-40h]
  int uiTex; // [esp+1A0h] [ebp-38h]
  float fYawAngle; // [esp+1A4h] [ebp-34h]
  float fYawRad; // [esp+1A4h] [ebp-34h]
  float v109; // [esp+1ACh] [ebp-2Ch]
  int i; // [esp+1B0h] [ebp-28h]
  float fMatrix21; // [esp+1B4h] [ebp-24h]
  uint8 byNumPols; // [esp+1BCh] [ebp-1Ch]
  uint8 byNumCoords; // [esp+1C0h] [ebp-18h]
  tVec3 worldCoords[32];
  float sortDepths[32];
  int viewIntX[32];
  int viewIntY[32];
  int viewIntZ[32];
  int screenX[32];
  int screenY[32];
  int screenClipped[32];

  if (iBuildingIdx < 0)
    return; //added by ROLLER

  set_starts(0);                                // Initialize rendering system
  uiBuildingType = BuildingBase[iBuildingIdx][0];// Get building plan data (polygons, coordinates, etc.)
  byNumPols = BuildingPlans[uiBuildingType].byNumPols;
  pPols = BuildingPlans[uiBuildingType].pPols;
  byNumCoords = BuildingPlans[uiBuildingType].byNumCoords;
  pCoords = BuildingPlans[uiBuildingType].pCoords;
  if (uiBuildingType >= 9 && (uiBuildingType <= 0xA || uiBuildingType == 15))// Special rotation for buildings 9, 10, and 15 based on world direction
  {
    fPitchAngle = BuildingAngles[3 * iBuildingIdx + 1];
    fRollAngle = BuildingAngles[3 * iBuildingIdx + 2];
    fYawAngle = (float)(360 * worlddirn / 0x3FFF);
  } else {
    fYawAngle = BuildingAngles[3 * iBuildingIdx];
    fPitchAngle = BuildingAngles[3 * iBuildingIdx + 1];
    fRollAngle = BuildingAngles[3 * iBuildingIdx + 2];
  }
  fYawRad = fYawAngle * 0.0174532925199f;        // Convert rotation angles from degrees to radians
  fPitchRad = fPitchAngle * 0.0174532925199f;
  fRollRad = 0.0174532925199f * fRollAngle;
  fBuildingX = BuildingX[iBuildingIdx];
  fBuildingZ = BuildingZ[iBuildingIdx];
  dCosYaw = cos(fYawRad);                       // Calculate sine and cosine for 3D rotation matrix
  dCosPitch = cos(fPitchRad);
  dSinYaw = sin(fYawRad);
  dSinPitch = sin(fPitchRad);
  dSinRoll = sin(fRollRad);
  dCosRoll = cos(fRollRad);
  fBuildingY = BuildingY[iBuildingIdx];
  for (i = 0; byNumCoords > i; ++i) {
    p_fY = &pCoords->fY;
    p_fZ = &pCoords->fZ;
    // CHEAT_MODE_DOUBLE_TRACK
    if ((cheat_mode & 0x1000) != 0)           // Scale coordinates 2x if cheat mode enabled
    {
      fX = pCoords->fX * 2.0f;
      fCoordY = *p_fY * 2.0f;
      ++pCoords;
      v109 = 2.0f * *p_fZ;
    } else {
      fX = pCoords->fX;
      ++pCoords;
      v109 = *p_fZ;
      fCoordY = *p_fY;
    }
    fMatrix01 = (float)(dCosYaw * dSinPitch * dSinRoll - dSinYaw * dCosRoll);// Build 3x3 rotation matrix elements from yaw/pitch/roll
    fMatrix00 = (float)(dCosYaw * dCosPitch);
    fMatrix02 = (float)(-dCosYaw * dSinPitch * dCosRoll - dSinYaw * dSinRoll);
    dTransformedX = fX * fMatrix00 + fCoordY * fMatrix01 + v109 * fMatrix02 + fBuildingX;// Apply 3D rotation and translation to building coordinates
    dTempX = dTransformedX;
    fMatrix10 = (float)(dSinYaw * dCosPitch);
    fMatrix12 = (float)(-dSinYaw * dSinPitch * dCosRoll + dCosYaw * dSinRoll);
    fMatrix11 = (float)(dSinYaw * dSinPitch * dSinRoll + dCosYaw * dCosRoll);
    dTransformedY = fX * fMatrix10 + fCoordY * fMatrix11 + v109 * fMatrix12 + fBuildingY;
    dTempY = dTransformedY;
    fVert2X = (float)(dCosPitch * dCosRoll);
    fMatrix20 = (float)dSinPitch;
    fMatrix21 = (float)(-dSinRoll * dCosPitch);
    dTransformedZ = fX * fMatrix20 + fCoordY * fMatrix21 + v109 * fVert2X + fBuildingZ;
    // Store raw (un-floored, un-translated) world coords for game_render_quad_world.
    // sw_quad_world performs floor(world − viewer) and the view-matrix multiply
    // itself, matching legacy precision.
    worldCoords[i].fX = (float)dTempX;
    worldCoords[i].fY = (float)dTempY;
    worldCoords[i].fZ = (float)dTransformedZ;
    // Bit-exact mirror of legacy view-space transform: subtract viewer in
    // double, floor, matrix-multiply, integer-truncate.
    {
      double dx = floor(dTempX - viewx);
      double dy = floor(dTempY - viewy);
      double dz = floor(dTransformedZ - viewz);
      viewIntX[i] = (int)(dx * vk1 + dy * vk4 + dz * vk7);
      viewIntY[i] = (int)(dx * vk2 + dy * vk5 + dz * vk8);
      viewIntZ[i] = (int)(dx * vk3 + dy * vk6 + dz * vk9);
      sortDepths[i] = (float)viewIntZ[i];
      // Project to screen-space using legacy integer math for culling and
      // depth sorting; building polygons are submitted through world-space
      // scene rendering below.
      int iClipped = 0;
      int iVz = viewIntZ[i];
      if (iVz < 80) {
        iVz = 80;
        iClipped = 1;
      }
      int xp = viewIntX[i] * VIEWDIST / iVz + xbase;
      int yp = viewIntY[i] * VIEWDIST / iVz + ybase;
      screenX[i] = (scr_size * xp) >> 6;
      screenY[i] = (scr_size * (199 - yp)) >> 6;
      screenClipped[i] = iClipped;
    }
  }
  iPolygonLoop = 0;
  iZOrderIdx = 0;
  while (iPolygonLoop < byNumPols) {
    BuildingZOrder[iZOrderIdx].iPolygonIndex = iPolygonLoop;// Build Z-order sorting list for polygons
    BuildingZOrder[iZOrderIdx].iPolygonLink = pPols->nNextPolIdx;
    if ((pPols->uiTex & 0x8000) == 0)         // Calculate polygon Z depth for sorting (front-to-back vs back-to-front)
    {                                           // Find maximum Z (farthest) for back-to-front rendering order
      if (sortDepths[pPols->verts[2]] <= (double)sortDepths[pPols->verts[3]])
        fZ = sortDepths[pPols->verts[3]];
      else
        fZ = sortDepths[pPols->verts[2]];
      fTempZ2 = fZ;
      if (sortDepths[pPols->verts[0]] <= (double)sortDepths[pPols->verts[1]])
        fMaxZ = sortDepths[pPols->verts[1]];
      else
        fMaxZ = sortDepths[pPols->verts[0]];
      if (fMaxZ <= (double)fTempZ2) {
        if (sortDepths[pPols->verts[2]] <= (double)sortDepths[pPols->verts[3]])
          fPolygonZ = sortDepths[pPols->verts[3]];
        else
          fPolygonZ = sortDepths[pPols->verts[2]];
      } else if (sortDepths[pPols->verts[0]] <= (double)sortDepths[pPols->verts[1]]) {
        fPolygonZ = sortDepths[pPols->verts[1]];
      } else {
        fPolygonZ = sortDepths[pPols->verts[0]];
      }
    } else {                                           // Find minimum Z (closest) for front-to-back rendering order
      if (sortDepths[pPols->verts[2]] >= (double)sortDepths[pPols->verts[3]])
        fMinZ1 = sortDepths[pPols->verts[3]];
      else
        fMinZ1 = sortDepths[pPols->verts[2]];
      fTempZ = fMinZ1;
      if (sortDepths[pPols->verts[0]] >= (double)sortDepths[pPols->verts[1]])
        fMinZ2 = sortDepths[pPols->verts[1]];
      else
        fMinZ2 = sortDepths[pPols->verts[0]];
      if (fMinZ2 >= (double)fTempZ) {
        if (sortDepths[pPols->verts[2]] >= (double)sortDepths[pPols->verts[3]])
          fPolygonZ = sortDepths[pPols->verts[3]];
        else
          fPolygonZ = sortDepths[pPols->verts[2]];
      } else if (sortDepths[pPols->verts[0]] >= (double)sortDepths[pPols->verts[1]]) {
        fPolygonZ = sortDepths[pPols->verts[1]];
      } else {
        fPolygonZ = sortDepths[pPols->verts[0]];
      }
    }
    BuildingZOrder[iZOrderIdx].fZDepth = fPolygonZ;
    ++pPols;
    ++iZOrderIdx;
    ++iPolygonLoop;
  }
  qsort(BuildingZOrder, byNumPols, 0xCu, bldZcmp);// Sort polygons by Z-depth using qsort
  for (j = 0; byNumPols > j; ++j) {
    iZOrderLoop = 0;
    iFoundPolygonLink = -1;
    iPolygonLink = -1;
    fZDepth = -32768.0;
    for (k = 0; k < byNumPols; ++k)           // Find deepest polygon in each linked group
    {
      if (BuildingZOrder[iZOrderLoop].iPolygonLink != iPolygonLink) {
        iPolygonLink = BuildingZOrder[iZOrderLoop].iPolygonLink;
        if (fZDepth < (double)BuildingZOrder[iZOrderLoop].fZDepth) {
          iBestZOrderIdx = k;
          iFoundPolygonLink = BuildingZOrder[iZOrderLoop].iPolygonLink;
          fZDepth = BuildingZOrder[iZOrderLoop].fZDepth;
        }
      }
      ++iZOrderLoop;
    }
    if (iFoundPolygonLink < 0) {
      j = byNumPols;
    } else {
      iCurrentZOrderIdx = iBestZOrderIdx;
      do {
        BuildingZOrder[iCurrentZOrderIdx].fZDepth = -1.0;
        iPolygonIndex = BuildingZOrder[iCurrentZOrderIdx].iPolygonIndex;
        BuildingZOrder[iCurrentZOrderIdx].iPolygonLink = -1;
        pPolygon = &BuildingPlans[uiBuildingType].pPols[iPolygonIndex];
        uiTex = pPolygon->uiTex;
        // Backface culling — render only if polygon faces camera, or if the
        // 0x2000 flag forces it visible from both sides. Mirrors the legacy
        // (d1 × d2) · V0 test using int-truncated view-space coords.
        {
          int iV0 = pPolygon->verts[0];
          int iV1 = pPolygon->verts[1];
          int iV2 = pPolygon->verts[2];
          int iV3 = pPolygon->verts[3];
          float fX0 = (float)viewIntX[iV0];
          float fY0 = (float)viewIntY[iV0];
          float fZ0 = (float)viewIntZ[iV0];
          float fDx02 = (float)viewIntX[iV2] - fX0;
          float fDy02 = (float)viewIntY[iV2] - fY0;
          float fDz02 = (float)viewIntZ[iV2] - fZ0;
          float fDx13 = (float)viewIntX[iV1] - (float)viewIntX[iV3];
          float fDy13 = (float)viewIntY[iV1] - (float)viewIntY[iV3];
          float fDz13 = (float)viewIntZ[iV1] - (float)viewIntZ[iV3];
          double fNormalDot = (fDy02 * fDz13 - fDz02 * fDy13) * fX0
                            + (fDz02 * fDx13 - fDx02 * fDz13) * fY0
                            + (fDx02 * fDy13 - fDy02 * fDx13) * fZ0;
          int isBackFace = (fNormalDot >= 0.0);
          if (isBackFace && (uiTex & 0x2000) == 0)
            goto skip_polygon;

          // Sum per-vertex clip flags; skip if entire poly is behind near plane.
          int iProjectedSum = screenClipped[iV0] + screenClipped[iV1]
                            + screenClipped[iV2] + screenClipped[iV3];
          if (iProjectedSum >= 4)
            goto skip_polygon;

          // Handle special textures (advertisements, building remapping)
          if ((uiTex & 0x200) != 0)
            uiTex = advert_list[iBuildingIdx];
          if ((textures_off & 0x80) != 0 && (uiTex & 0x100) != 0)
            uiTex = (uiTex & 0xFFFFFE00) + bld_remap[(uint8)uiTex];

          // Vertex order: forward for front-facing, reversed for the
          // back-facing 0x2000 case so texture mapping stays correct.
          int iOrder[4];
          if (isBackFace) {
            iOrder[0] = iV3; iOrder[1] = iV2; iOrder[2] = iV1; iOrder[3] = iV0;
          } else {
            iOrder[0] = iV0; iOrder[1] = iV1; iOrder[2] = iV2; iOrder[3] = iV3;
          }

          // Closest-Z determines whether the legacy path subdivided.
          int iZ0 = viewIntZ[iV0];
          int iZ1 = viewIntZ[iV1];
          int iZ2 = viewIntZ[iV2];
          int iZ3 = viewIntZ[iV3];
          float fClosestZ = (float)((iZ0 < iZ1) ? iZ0 : iZ1);
          if ((float)iZ2 < fClosestZ) fClosestZ = (float)iZ2;
          if ((float)iZ3 < fClosestZ) fClosestZ = (float)iZ3;

          GameRenderVertex verts[4];
          for (int vi = 0; vi < 4; vi++) {
            tVec3 *wc = &worldCoords[iOrder[vi]];
            verts[vi].x = wc->fX;
            verts[vi].y = wc->fY;
            verts[vi].z = wc->fZ;
            verts[vi].u = 0.0f;
            verts[vi].v = 0.0f;
          }
          TextureHandle th = ((uiTex & 0x100) != 0)
            ? game_render_get_texture_handle(g_pGameRenderer, TEXTURE_BANK_BUILDING)
            : TEXTURE_HANDLE_INVALID;
          game_render_quad_world(g_pGameRenderer, verts, th, (int)uiTex,
                                 (float)BuildingSub[uiBuildingType] * subscale);
        }
        skip_polygon:;
        iZOrderOffset = iCurrentZOrderIdx * 12 + 12;
        ++iCurrentZOrderIdx;
        ++iBestZOrderIdx;
      } while (byNumPols > iBestZOrderIdx && iFoundPolygonLink == *(int *)((char *)&BuildingZOrder[0].iPolygonLink + iZOrderOffset));
    }
  }
  init_animate_ads();                           // Initialize animated advertisements
}

//-------------------------------------------------------------------------------------------------
//0006AB30
void init_animate_ads()
{
  ;
}

//-------------------------------------------------------------------------------------------------
//0006AB40
int bldZcmp(const void *pBuilding1, const void *pBuilding2)
{
  int iPolygonLink1; // ecx
  int iPolygonLink2; // ebx
  float fZDepth2; // [esp+0h] [ebp-10h]
  float fZDepth1; // [esp+4h] [ebp-Ch]

  const tBuildingZOrderEntry *pEntry1 = (const tBuildingZOrderEntry *)pBuilding1;
  const tBuildingZOrderEntry *pEntry2 = (const tBuildingZOrderEntry *)pBuilding2;

  iPolygonLink1 = pEntry1->iPolygonLink;
  fZDepth1 = pEntry1->fZDepth;
  iPolygonLink2 = pEntry2->iPolygonLink;
  fZDepth2 = pEntry2->fZDepth;
  if (iPolygonLink1 < iPolygonLink2)
    return -1;
  if (iPolygonLink1 == iPolygonLink2) {
    if (fZDepth1 == fZDepth2)
      return 0;
    if (fZDepth1 >= (double)fZDepth2)
      return -1;
  }
  return 1;
}

//-------------------------------------------------------------------------------------------------