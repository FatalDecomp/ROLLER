#include "moving.h"
#include "3d.h"
#include "control.h"
#include <string.h>
#include <math.h>
//-------------------------------------------------------------------------------------------------

int totalramps = 0;   // 000A7508
tStuntData *ramp[50]; // 001A1A80
int replaytype;       // 001A1B48

//-------------------------------------------------------------------------------------------------
// 00073E90
tStuntData *initramp(int iGeometryIdx, int iChunkCount, int iNumTicks,
                     int iTickStartIdx, int iTimingGroup, int iHeight,
                     int iTimeBulging, int iTimeFlat, int iRampSideLength,
                     int iFlags) {
  tStuntData *pBuffer; // eax
  tStuntData *pStunt;  // [esp+0h] [ebp-10h] BYREF

  pBuffer = (tStuntData *)getbuffer(sizeof(tStuntData));
  pStunt = pBuffer;
  if (pBuffer) {
    memset(pBuffer, 0, sizeof(tStuntData));
    pStunt->chunkDataAy = (tStuntGeometry *)getbuffer(0xB0 * iChunkCount);
    if (pStunt->chunkDataAy) {
      memset(pStunt->chunkDataAy, 0, 0xB0 * iChunkCount);
      pStunt->iNumTicks = iNumTicks;
      pStunt->iTickStartIdx = iTickStartIdx;
      pStunt->iTickStartIdx2 = iTickStartIdx;
      pStunt->iGeometryIdx = iGeometryIdx;
      pStunt->iChunkCount = iChunkCount;
      pStunt->iTimingGroup2 = iTimingGroup;
      pStunt->iTimingGroup = iTimingGroup;
      pStunt->iTimeBulging = iTimeBulging;
      pStunt->iTimeFlat = iTimeFlat;
      pStunt->iRunningTimer = 0;
      pStunt->iRampSideLength = iRampSideLength;
      pStunt->iHeight = iHeight;
      pStunt->iFlags = iFlags;
      reinitramp(pStunt);
      return pStunt;
    } else {
      fre((void **)&pStunt);
      pStunt = 0;
      return 0;
    }
  }
  return pBuffer;
}

//-------------------------------------------------------------------------------------------------
// 00073F80
void reinitramp(tStuntData *pStunt) {
  int iGeometryIdx;              // ebx
  int iStartTrackIdx;            // eax
  int iFlags;                    // edi
  int iAngle;                    // eax
  tStuntGeometry *pChunkData;    // ebp
  int iPointOffset;              // esi
  tStuntLocalPoint *pDataPoint;  // ebx
  double dRelX;                  // st7
  double dRelY;                  // st6
  double dRelZ;                  // st5
  double dTempZ;                 // st4
  double dRelX2;                 // st7
  double dRelY2;                 // st6
  double dRelZ2;                 // st5
  double dTempZ2;                // st4
  double dTrackHalfLength_1;     // st7
  int iPointOffset2;             // ebx
  tStuntLocalPoint *pDataPoint2; // esi
  double dRelX3;                 // st7
  double dRelY3;                 // st6
  double dRelZ3;                 // st5
  double dRelX4;                 // st7
  double dRelY4;                 // st6
  double dRelZ4;                 // st5
  double dTrackHalfLength;       // st7
  float fDeltaX;                 // [esp+0h] [ebp-88h]
  float fDeltaY;                 // [esp+4h] [ebp-84h]
  int iStartIdx;                 // [esp+24h] [ebp-64h]
  int iChunkCount;               // [esp+28h] [ebp-60h]
  int j;                         // [esp+30h] [ebp-58h]
  int iPointOffsetBase;          // [esp+34h] [ebp-54h]
  int iPointOffsetCur;           // [esp+38h] [ebp-50h]
  tData *pCurrentData;           // [esp+3Ch] [ebp-4Ch]
  tData *pNextData;              // [esp+3Ch] [ebp-4Ch]
  float fCos;                    // [esp+54h] [ebp-34h]
  float fNegSin;                 // [esp+58h] [ebp-30h]
  float fSin;                    // [esp+60h] [ebp-28h]
  int i;                         // [esp+64h] [ebp-24h]
  int k;                         // [esp+68h] [ebp-20h]

  pStunt->iTickStartIdx =
      pStunt->iTickStartIdx2; // Initialize stunt timing and geometry data
  pStunt->iTimingGroup2 = pStunt->iTimingGroup;
  iGeometryIdx = pStunt->iGeometryIdx;
  iChunkCount = pStunt->iChunkCount;
  iStartTrackIdx = pStunt->iGeometryIdx - iChunkCount;
  pStunt->iRunningTimer = 0;
  pStunt->startPos.fX =
      (TrakPt[iStartTrackIdx].pointAy[2].fX +
       TrakPt[iStartTrackIdx].pointAy[3].fX) *
      0.5f; // Calculate start position as midpoint between track edge points
  pStunt->startPos.fY = (TrakPt[iStartTrackIdx].pointAy[2].fY +
                         TrakPt[iStartTrackIdx].pointAy[3].fY) *
                        0.5f;
  pStunt->startPos.fZ = (TrakPt[iStartTrackIdx].pointAy[2].fZ +
                         TrakPt[iStartTrackIdx].pointAy[3].fZ) *
                        0.5f;
  pStunt->endPos.fX =
      (TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[2].fX +
       TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[3].fX) *
      0.5f; // Calculate end position as midpoint between track edge points
  pStunt->endPos.fY = (TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[2].fY +
                       TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[3].fY) *
                      0.5f;
  pStunt->endPos.fZ = (TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[2].fZ +
                       TrakPt[iGeometryIdx + 1 + iChunkCount].pointAy[3].fZ) *
                      0.5f;
  iFlags = pStunt->iFlags;
  fDeltaY = (TrakPt[iGeometryIdx].pointAy[2].fY +
             TrakPt[iGeometryIdx].pointAy[3].fY) *
                0.5f -
            pStunt->startPos.fY;
  fDeltaX = (TrakPt[iGeometryIdx].pointAy[2].fX +
             TrakPt[iGeometryIdx].pointAy[3].fX) *
                0.5f -
            pStunt->startPos.fX;
  iAngle =
      getangle(fDeltaX, fDeltaY); // Calculate ramp angle from delta coordinates
  pStunt->iAngle = iAngle;
  iStartIdx = iGeometryIdx - iChunkCount + 1;
  fSin = tsin[iAngle]; // Get sine and cosine values from lookup tables
  fCos = tcos[iAngle];
  pChunkData = pStunt->chunkDataAy;
  pCurrentData = &localdata[iStartIdx - 1];
  fNegSin = -fSin;
  if (iChunkCount > 0) {
    iPointOffsetCur =
        72 * iStartIdx; // Process first chunk of track geometry data
    do {
      iPointOffset = iPointOffsetCur;
      pDataPoint = (tStuntLocalPoint *)pChunkData;
      for (i = 0; i < 6; ++i) {
        if (((1 << i) & iFlags) != 0) {
          dRelX = *(float *)((char *)&TrakPt[0].pointAy[0].fX + iPointOffset) -
                  pStunt->startPos.fX; // Transform track point coordinates
                                       // relative to ramp start position
          dRelY = *(float *)((char *)&TrakPt[0].pointAy[0].fY + iPointOffset) -
                  pStunt->startPos.fY;
          dRelZ = *(float *)((char *)&TrakPt[0].pointAy[0].fZ + iPointOffset) -
                  pStunt->startPos.fZ;
          dTempZ = 0.0 * dRelZ;
          //_CHP();
          //_CHP();
          //_CHP();
          pDataPoint->iLocalX =
              (int)(fCos * dRelX + fSin * dRelY +
                    dTempZ); // Apply rotation matrix to transform coordinates
          pDataPoint->iLocalY = (int)(dTempZ + fNegSin * dRelX + fCos * dRelY);
          pDataPoint->iLocalZ = (int)(dRelX * 0.0 + dRelY * 0.0 + dRelZ);
        }
        ++pDataPoint;
        iPointOffset += sizeof(tStuntLocalPoint);
      }
      dRelX2 = -pCurrentData->pointAy[3].fX -
               pStunt->startPos
                   .fX; // Process special data point with negative coordinates
      dRelY2 = -pCurrentData->pointAy[3].fY - pStunt->startPos.fY;
      dRelZ2 = -pCurrentData->pointAy[3].fZ - pStunt->startPos.fZ;
      dTempZ2 = 0.0 * dRelZ2;
      pChunkData->refPoint.fX =
          (float)(fCos * dRelX2 + fSin * dRelY2 + dTempZ2);
      pChunkData->refPoint.fY =
          (float)(dTempZ2 + fNegSin * dRelX2 + fCos * dRelY2);
      pChunkData->refPoint.fZ = (float)(dRelX2 * 0.0 + dRelY2 * 0.0 + dRelZ2);
      dTrackHalfLength_1 = pCurrentData->fTrackHalfLength;
      ++pChunkData;
      ++pCurrentData;
      iPointOffsetCur += sizeof(tGroundPt);
      pChunkData[-1].fTrackHalfLength = (float)dTrackHalfLength_1;
    } while (iPointOffsetCur < 72 * iChunkCount + 72 * iStartIdx);
  }
  pNextData = pCurrentData + 1;
  iPointOffsetBase = 72 * iStartIdx + 72 * iChunkCount;
  for (j = 144 * iChunkCount + 72 * iStartIdx; iPointOffsetBase < j;
       pChunkData[-1].fTrackHalfLength =
           (float)dTrackHalfLength) // Process second chunk of track geometry
                                    // data relative to ramp end
  {
    iPointOffset2 = iPointOffsetBase;
    pDataPoint2 = (tStuntLocalPoint *)pChunkData;
    for (k = 0; k < 6; ++k) {
      if (((1 << k) & iFlags) != 0) {
        dRelX3 = *(float *)((char *)&TrakPt[0].pointAy[0].fX + iPointOffset2) -
                 pStunt->endPos.fX; // Transform track point coordinates
                                    // relative to ramp end position
        dRelY3 = *(float *)((char *)&TrakPt[0].pointAy[0].fY + iPointOffset2) -
                 pStunt->endPos.fY;
        dRelZ3 = *(float *)((char *)&TrakPt[0].pointAy[0].fZ + iPointOffset2) -
                 pStunt->endPos.fZ;
        //_CHP();
        //_CHP();
        //_CHP();
        pDataPoint2->iLocalX =
            (int)(fCos * dRelX3 + fSin * dRelY3 + 0.0 * dRelZ3);
        pDataPoint2->iLocalY =
            (int)(fNegSin * dRelX3 + fCos * dRelY3 + 0.0 * dRelZ3);
        pDataPoint2->iLocalZ =
            (int)(dRelX3 * 0.0 + dRelY3 * 0.0 + dRelZ3 * 1.0);
      }
      ++pDataPoint2;
      iPointOffset2 += sizeof(tStuntLocalPoint);
    }
    dRelX4 = -pNextData->pointAy[3].fX - pStunt->endPos.fX;
    dRelY4 = -pNextData->pointAy[3].fY - pStunt->endPos.fY;
    dRelZ4 = -pNextData->pointAy[3].fZ - pStunt->endPos.fZ;
    pChunkData->refPoint.fX =
        (float)(fCos * dRelX4 + fSin * dRelY4 + 0.0 * dRelZ4);
    pChunkData->refPoint.fY =
        (float)(fNegSin * dRelX4 + fCos * dRelY4 + 0.0 * dRelZ4);
    pChunkData->refPoint.fZ =
        (float)(dRelX4 * 0.0 + dRelY4 * 0.0 + dRelZ4 * 1.0);
    dTrackHalfLength = pNextData->fTrackHalfLength;
    ++pChunkData;
    ++pNextData;
    iPointOffsetBase += sizeof(tGroundPt);
  }
  updateramp(pStunt); // Update ramp display data after geometry processing
}

//-------------------------------------------------------------------------------------------------
// 00074440
void updateramp(tStuntData *pStunt) {
  int iTickStartIdx;                   // edx
  int iTimer;                          // ecx
  int iHeightOffset;                   // ebx
  int iAngle;                          // edx
  double dNegSin;                      // st7
  int iFlags;                          // esi
  int iStartIdx;                       // edx
  tData *pData;                        // edi
  int iFlagBit;                        // ecx
  int iPointOffset;                    // ebx
  tStuntLocalPoint *pLocalPoint;       // edx
  double dScaledX;                     // st7
  double iLocalY;                      // st6
  double iLocalZ;                      // st5
  double dInterpolation;               // st6
  tData *pNextData;                    // edi
  int iFlagBit2;                       // ecx
  int iPointOffset2;                   // ebx
  tStuntLocalPoint *pCoords;           // edx
  double dScaledX2;                    // st7
  double dLocalY2;                     // st6
  double dLocalZ2;                     // st5
  double dInterpolation2;              // st6
  double dTrackHalfLength;             // st6
  int iGeometryIdx;                    // ebx
  double dMidX;                        // st7
  double dMidY;                        // st6
  double dMidZ;                        // st5
  int iNextGeomIdx;                    // eax
  double dNextMidX;                    // st4
  double dNextMidY;                    // st3
  double dNextMidZ;                    // st2
  tData *pCurrentData;                 // edx
  float fHeightCos;                    // [esp+14h] [ebp-A0h]
  float fHeightSin;                    // [esp+18h] [ebp-9Ch]
  float fAngleCos;                     // [esp+24h] [ebp-90h]
  float fAngleSin;                     // [esp+28h] [ebp-8Ch]
  float fZ;                            // [esp+2Ch] [ebp-88h]
  float fRefZ2;                        // [esp+2Ch] [ebp-88h]
  float fY;                            // [esp+30h] [ebp-84h]
  float fRefY2;                        // [esp+30h] [ebp-84h]
  int iHeightValue;                    // [esp+38h] [ebp-7Ch]
  float fGravityX;                     // [esp+3Ch] [ebp-78h]
  float fGravityX2;                    // [esp+40h] [ebp-74h]
  float fGravityZ;                     // [esp+44h] [ebp-70h]
  float fGravityZ2;                    // [esp+48h] [ebp-6Ch]
  float fGravityY;                     // [esp+4Ch] [ebp-68h]
  int *pEndLocalPoints;                // [esp+50h] [ebp-64h]
  int iCurrentIdx;                     // [esp+54h] [ebp-60h]
  int iPointOffsetBase;                // [esp+58h] [ebp-5Ch]
  tStuntLocalPoint *pEndLocalPoints_1; // [esp+5Ch] [ebp-58h]
  int iPointOffsetCur;                 // [esp+60h] [ebp-54h]
  int iGeomIdx;                        // [esp+64h] [ebp-50h]
  tStuntGeometry *pStuntGeometry;      // [esp+68h] [ebp-4Ch]
  float fNegAngleSin;                  // [esp+78h] [ebp-3Ch]
  float fRotYX;                        // [esp+7Ch] [ebp-38h]
  float fRotXX;                        // [esp+80h] [ebp-34h]
  float fRotXZ;                        // [esp+8Ch] [ebp-28h]
  float fRotYZ;                        // [esp+90h] [ebp-24h]
  float fRampScale;                    // [esp+94h] [ebp-20h]
  float fInterpolation;                // [esp+94h] [ebp-20h]
  float fRotZX;                        // [esp+98h] [ebp-1Ch]

  iTickStartIdx = pStunt->iTickStartIdx; // Get current tick position for ramp
                                         // animation timing
  if (replaytype != 2) // Skip timing updates during replay mode
  {
    iTimer = pStunt->iRunningTimer;
    if (iTimer) // Handle ramp animation state machine for timing control
    {
      pStunt->iRunningTimer = iTimer - 1;
    } else if (pStunt->iTimingGroup2 ==
               1) { // Check if reached end of ramp animation sequence
      if (iTickStartIdx == pStunt->iNumTicks - 1) {
        pStunt->iTimingGroup2 = -1;
        pStunt->iRunningTimer = pStunt->iTimeBulging;
      } else {
        ++iTickStartIdx;
      }
    } else if (iTickStartIdx) {
      --iTickStartIdx;
    } else {
      pStunt->iTimingGroup2 = 1;
      pStunt->iRunningTimer = pStunt->iTimeFlat;
    }
  }
  if (iTickStartIdx != pStunt->iTickStartIdx ||
      replaytype ==
          2) // Update geometry only when timing changed or in replay mode
  {
    iHeightOffset =
        iTickStartIdx *
        pStunt
            ->iHeight; // Calculate height offset based on current tick position
    pStunt->iTickStartIdx = iTickStartIdx;
    iAngle = pStunt->iAngle;
    iHeightValue = iHeightOffset;
    fAngleSin = tsin[iAngle]; // Get trigonometric values from lookup tables for
                              // rotation matrix
    fAngleCos = tcos[iAngle];
    fHeightCos = tcos[iHeightOffset];
    fRotXX = fHeightCos * fAngleCos; // Build 3D rotation matrix components for
                                     // coordinate transformation
    fRotYX = fHeightCos * fAngleSin;
    fNegAngleSin = -fAngleSin;
    fHeightSin = tsin[iHeightOffset];
    dNegSin = -fHeightSin;
    fRotXZ = (float)dNegSin * fAngleCos;
    fRotYZ = (float)dNegSin * fAngleSin;
    fRampScale = (float)((double)pStunt->iRampSideLength *
                         0.0009765625); // Calculate ramp scaling factor and
                                        // interpolation value
    iFlags = pStunt->iFlags;
    fRotZX = fHeightSin;
    pStuntGeometry = pStunt->chunkDataAy;

    if (!pStuntGeometry) // added by ROLLER
      return;

    iStartIdx = pStunt->iGeometryIdx - pStunt->iChunkCount + 1;
    iCurrentIdx = iStartIdx;
    pData = &localdata[iStartIdx - 1];
    fInterpolation =
        (float)((double)pStunt->iTickStartIdx * (fRampScale + -1.0) /
                    (double)pStunt->iNumTicks +
                1.0); // Calculate animation interpolation factor based on
                      // timing state
    if (iStartIdx <= pStunt->iGeometryIdx) // Process first half of ramp
                                           // geometry (start to middle)
    {
      pEndLocalPoints = (int *)&pStuntGeometry->refPoint;
      iPointOffsetCur = 72 * iStartIdx;
      do {
        iFlagBit = 1;
        iPointOffset = iPointOffsetCur;
        pLocalPoint = (tStuntLocalPoint *)pStuntGeometry;
        do { // Transform local ramp coordinates to world coordinates for each
             // track point
          if ((iFlagBit & iFlags) != 0) {
            dScaledX = (double)pLocalPoint->iLocalX * fInterpolation;
            iLocalY = (double)pLocalPoint->iLocalY;
            iLocalZ = (double)pLocalPoint->iLocalZ;
            *(float *)((char *)&TrakPt[0].pointAy[0].fX + iPointOffset) =
                (float)(fRotXX * dScaledX + fNegAngleSin * iLocalY +
                        fRotXZ * iLocalZ +
                        pStunt->startPos
                            .fX); // Apply 3D rotation matrix and translation to
                                  // transform coordinates
            *(float *)((char *)&TrakPt[0].pointAy[0].fY + iPointOffset) =
                (float)(fRotYX * dScaledX + fAngleCos * iLocalY +
                        fRotYZ * iLocalZ + pStunt->startPos.fY);
            *(float *)((char *)&TrakPt[0].pointAy[0].fZ + iPointOffset) =
                (float)(dScaledX * fHeightSin + iLocalY * 0.0 +
                        iLocalZ * fHeightCos + pStunt->startPos.fZ);
          }
          iPointOffset += 12;
          ++pLocalPoint;
          iFlagBit *= 2;
        } while (pLocalPoint != (tStuntLocalPoint *)pEndLocalPoints);
        dInterpolation = pStuntGeometry->refPoint.fX *
                         fInterpolation; // Transform reference point with
                                         // negative coordinates
        fY = pStuntGeometry->refPoint.fY;
        fZ = pStuntGeometry->refPoint.fZ;
        pData->pointAy[3].fX =
            (float)(-fRotXX * dInterpolation - fNegAngleSin * fY - fRotXZ * fZ -
                    pStunt->startPos.fX);
        pData->pointAy[3].fY =
            (float)(-fRotYX * dInterpolation - fAngleCos * fY - fRotYZ * fZ -
                    pStunt->startPos.fY);
        pData->pointAy[3].fZ = (float)(dInterpolation * -fHeightSin - 0.0 * fY -
                                       fHeightCos * fZ - pStunt->startPos.fZ);
        pData->fTrackHalfLength =
            fInterpolation * pStuntGeometry->fTrackHalfLength;
        ++pData;
        pData[-1].pointAy[2].fY = 0.0;
        pData[-1].pointAy[0].fX =
            fRotXX; // Set up rotation matrix components for track geometry
        pData[-1].pointAy[0].fY = fNegAngleSin;
        pData[-1].pointAy[0].fZ = fRotXZ;
        pData[-1].pointAy[1].fX = fRotYX;
        pData[-1].pointAy[1].fY = fAngleCos;
        pData[-1].pointAy[1].fZ = fRotYZ;
        pData[-1].pointAy[2].fX = fHeightSin;
        iPointOffsetCur += 72;
        pData[-1].pointAy[2].fZ = fHeightCos;
        ++iCurrentIdx;
        fGravityX = fHeightSin * -3.0f;
        pData[-1].gravity.fX =
            fGravityX; // Set gravity vector components for physics simulation
        ++pStuntGeometry;
        fGravityZ = fHeightCos * -3.0f;
        pData[-1].gravity.fZ = fGravityZ;
        pData[-1].gravity.fY = 0.0;
        pEndLocalPoints += 22;
      } while (iCurrentIdx <= pStunt->iGeometryIdx);
    }
    fRotZX = -fHeightSin;
    // HIBYTE(fRotZX) = HIBYTE(fHeightSin) ^ 0x80;
    fRotXZ = -fRotXZ;
    // HIBYTE(fRotXZ) ^= 0x80u;
    iGeomIdx = pStunt->iGeometryIdx + 1;
    fRotYZ = -fRotYZ;
    // HIBYTE(fRotYZ) ^= 0x80u;
    pEndLocalPoints_1 = (tStuntLocalPoint *)&pStuntGeometry->refPoint;
    pNextData = pData + 1;
    iPointOffsetBase =
        72 * iGeomIdx; // Process second half of ramp geometry (middle to end)
    while (pStunt->iChunkCount + pStunt->iGeometryIdx >= iGeomIdx) {
      iFlagBit2 = 1;
      iPointOffset2 = iPointOffsetBase;
      pCoords = (tStuntLocalPoint *)pStuntGeometry;
      do { // Transform coordinates relative to ramp end position
        if ((iFlagBit2 & iFlags) != 0) {
          dScaledX2 = (double)pCoords->iLocalX * fInterpolation;
          dLocalY2 = (double)pCoords->iLocalY;
          dLocalZ2 = (double)pCoords->iLocalZ;
          *(float *)((char *)&TrakPt[0].pointAy[0].fX + iPointOffset2) =
              (float)(fRotXX * dScaledX2 + fNegAngleSin * dLocalY2 +
                      fRotXZ * dLocalZ2 +
                      pStunt->endPos.fX); // Apply transformation matrix
                                          // relative to end position
          *(float *)((char *)&TrakPt[0].pointAy[0].fY + iPointOffset2) =
              (float)(fRotYX * dScaledX2 + fAngleCos * dLocalY2 +
                      fRotYZ * dLocalZ2 + pStunt->endPos.fY);
          *(float *)((char *)&TrakPt[0].pointAy[0].fZ + iPointOffset2) =
              (float)(dScaledX2 * fRotZX + dLocalY2 * 0.0 +
                      dLocalZ2 * fHeightCos + pStunt->endPos.fZ);
        }
        iPointOffset2 += 12;
        ++pCoords;
        iFlagBit2 *= 2;
      } while (pCoords != pEndLocalPoints_1);
      dInterpolation2 = pStuntGeometry->refPoint.fX * fInterpolation;
      fRefY2 = pStuntGeometry->refPoint.fY;
      fRefZ2 = pStuntGeometry->refPoint.fZ;
      pNextData->pointAy[3].fX =
          (float)(-fRotXX * dInterpolation2 - fNegAngleSin * fRefY2 -
                  fRotXZ * fRefZ2 - pStunt->endPos.fX);
      pNextData->pointAy[3].fY =
          (float)(-fRotYX * dInterpolation2 - fAngleCos * fRefY2 -
                  fRotYZ * fRefZ2 - pStunt->endPos.fY);
      pNextData->pointAy[3].fZ =
          (float)(dInterpolation2 * -fRotZX - 0.0 * fRefY2 -
                  fHeightCos * fRefZ2 - pStunt->endPos.fZ);
      ++pNextData;
      dTrackHalfLength = pStuntGeometry->fTrackHalfLength;
      pNextData[-1].pointAy[0].fX = fRotXX;
      pNextData[-1].pointAy[0].fY = fNegAngleSin;
      pNextData[-1].pointAy[0].fZ = fRotXZ;
      pNextData[-1].pointAy[1].fX = fRotYX;
      pNextData[-1].pointAy[1].fY = fAngleCos;
      pNextData[-1].pointAy[1].fZ = fRotYZ;
      pNextData[-1].pointAy[2].fX = fRotZX;
      pNextData[-1].pointAy[2].fY = 0.0;
      pNextData[-1].pointAy[2].fZ = fHeightCos;
      fGravityX2 = fRotZX * -3.0f;
      pNextData[-1].gravity.fX = fGravityX2;
      iPointOffsetBase += 72;
      fGravityY = 0.0 * -3.0f;
      pNextData[-1].gravity.fY = fGravityY;
      ++iGeomIdx;
      fGravityZ2 = fHeightCos * -3.0f;
      pNextData[-1].gravity.fZ = fGravityZ2;
      ++pStuntGeometry;
      pNextData[-1].fTrackHalfLength = fInterpolation * (float)dTrackHalfLength;
      pEndLocalPoints_1 = (tStuntLocalPoint *)((char *)pEndLocalPoints_1 + 88);
    }
    // TODO is this correct?
    localdata[pStunt->iGeometryIdx - pStunt->iChunkCount].iPitch = iHeightValue;
    localdata[pStunt->iGeometryIdx].iPitch = -iHeightValue;
    // HorizonColour[32 * (pStunt->iGeometryIdx - pStunt->iChunkCount) + 482] =
    // iHeightValue; HorizonColour[32 * pStunt->iGeometryIdx + 482] =
    // -iHeightValue;
    localdata[pStunt->iGeometryIdx].iPitch =
        -iHeightValue; // Set pitch values for track geometry
    localdata[pStunt->iChunkCount + pStunt->iGeometryIdx].iPitch = iHeightValue;
    iGeometryIdx = pStunt->iGeometryIdx;
    dMidX = (TrakPt[pStunt->iGeometryIdx].pointAy[2].fX +
             TrakPt[pStunt->iGeometryIdx].pointAy[3].fX) *
            0.5; // Calculate ramp center point for final geometry setup
    dMidY = (TrakPt[pStunt->iGeometryIdx].pointAy[2].fY +
             TrakPt[pStunt->iGeometryIdx].pointAy[3].fY) *
            0.5;
    dMidZ = (TrakPt[pStunt->iGeometryIdx].pointAy[2].fZ +
             TrakPt[pStunt->iGeometryIdx].pointAy[3].fZ) *
            0.5;
    iNextGeomIdx = pStunt->iGeometryIdx + 1;
    dNextMidX = (TrakPt[iNextGeomIdx].pointAy[2].fX +
                 TrakPt[iNextGeomIdx].pointAy[3].fX) *
                0.5;
    dNextMidY = (TrakPt[iNextGeomIdx].pointAy[2].fY +
                 TrakPt[iNextGeomIdx].pointAy[3].fY) *
                0.5;
    dNextMidZ = (TrakPt[iNextGeomIdx].pointAy[2].fZ +
                 TrakPt[iNextGeomIdx].pointAy[3].fZ) *
                0.5;
    pCurrentData = &localdata[iGeometryIdx];
    pCurrentData->pointAy[3].fX = (float)(-(dMidX + dNextMidX) * 0.5);
    pCurrentData->pointAy[3].fY = (float)(-(dMidY + dNextMidY) * 0.5);
    pCurrentData->pointAy[3].fZ = (float)(-(dMidZ + dNextMidZ) * 0.5);
    pCurrentData->fTrackHalfLength =
        (float)sqrt((dNextMidX - dMidX) * (dNextMidX - dMidX) +
                    (dNextMidY - dMidY) * (dNextMidY - dMidY) +
                    (dNextMidZ - dMidZ) * (dNextMidZ - dMidZ)) *
        0.5f; // Calculate distance between ramp endpoints for track half-length
  }
}

//-------------------------------------------------------------------------------------------------
// 00074BA0
void updatestunts() {
  tStuntData **pStuntItr; // edx
  tStuntData *pCurrStunt; // ecx

  pStuntItr = ramp;
  if (ramp[0]) {
    do {
      updateramp(*pStuntItr);
      pCurrStunt = pStuntItr[1];
      ++pStuntItr;
    } while (pCurrStunt);
  }
}

//-------------------------------------------------------------------------------------------------
// 00074BD0
void reinitstunts() {
  tStuntData **pStuntItr; // edx
  tStuntData *pCurrStunt; // ecx

  pStuntItr = ramp;
  if (ramp[0]) {
    do {
      reinitramp(*pStuntItr);
      pCurrStunt = pStuntItr[1];
      ++pStuntItr;
    } while (pCurrStunt);
  }
}

//-------------------------------------------------------------------------------------------------
// 00074C00
void freeramp(void **pRampData) {
  void *pRampToFree; // [esp+0h] [ebp-4h] BYREF

  pRampToFree = pRampData;
  if (pRampData) {
    if (pRampData[20])
      fre(pRampData + 20);
    fre(&pRampToFree);
  }
}

//-------------------------------------------------------------------------------------------------
// 00074C30
void freestunts(uint8 **pTrackData, int *pBuf) {
  tStuntData **ppRampArray; // edx
  tStuntData *pRampData;    // eax
  tStuntData *pNextRamp;    // ecx
  void *pRampToFree;        // [esp+0h] [ebp-10h] BYREF
  int *pBufSaved;           // [esp+4h] [ebp-Ch]

  pBufSaved = pBuf;
  ppRampArray = ramp; // Start at the beginning of the global ramp pointer array
  if (ramp[0])        // Check if there are any ramps to free
  {
    do {
      pRampData = *ppRampArray; // Get pointer to current ramp structure
      pRampToFree = pRampData;
      if (pRampData) { // Check if ramp has allocated data at offset 0x50 (80
                       // bytes)
        if (pRampData->chunkDataAy)
          fre((void **)&pRampData
                  ->chunkDataAy); // Free the allocated data at offset 0x50
        fre(&pRampToFree);        // Free the ramp structure itself
      }
      pNextRamp = ppRampArray[1]; // Get next ramp pointer for loop condition
      ++ppRampArray;              // Move to next ramp pointer in array
    } while (pNextRamp); // Continue until we find a NULL pointer
  }
}

//-------------------------------------------------------------------------------------------------
