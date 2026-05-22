#ifndef _ROLLER_MOVING_H
#define _ROLLER_MOVING_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iLocalX;
  int iLocalY;
  int iLocalZ;
} tStuntLocalPoint;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tStuntLocalPoint localPtAy[6];
  tVec3 refPoint;
  float fTrackHalfLength;
} tStuntGeometry;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iGeometryIdx;
  int iChunkCount;
  int iNumTicks;
  int iTickStartIdx2;
  int iTimingGroup;
  int iHeight;
  int iTimeBulging;
  int iTimeFlat;
  int iRampSideLength;
  int iFlags;
  int iTickStartIdx;
  int iTimingGroup2;
  int iRunningTimer;
  tVec3 startPos;
  tVec3 endPos;
  int iAngle;
  tStuntGeometry *chunkDataAy;
} tStuntData;

//-------------------------------------------------------------------------------------------------

extern int totalramps;
extern tStuntData *ramp[50];
extern int replaytype;

//-------------------------------------------------------------------------------------------------

tStuntData *initramp(
        int iGeometryIdx,
        int iChunkCount,
        int iNumTicks,
        int iTickStartIdx,
        int iTimingGroup,
        int iHeight,
        int iTimeBulging,
        int iTimeFlat,
        int iRampSideLength,
        int iFlags);
void reinitramp(tStuntData *pStunt);
void updateramp(tStuntData *pStunt);
void updatestunts();
void freestunts(uint8 **pTrackData, int *pBuf);

//-------------------------------------------------------------------------------------------------
#endif