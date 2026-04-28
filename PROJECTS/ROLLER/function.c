#include "function.h"
#include "loadtrak.h"
#include "control.h"
#include "sound.h"
#include "func2.h"
#include "view.h"
#include "roller.h"
#include "network.h"
#include "graphics.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
//-------------------------------------------------------------------------------------------------

int firework_colours[16] =  //000A4580
{
  231, 207, 171, 255, 243, 219, 195, 183,
  255, 159, 183, 143, 231, 231, 231, 231
};
float mapsize[25] =       //000A45C0
{
  6000.0, 6000.0, 3500.0, 4500.0, 6000.0,
  3000.0, 4000.0, 3000.0, 4000.0, 6000.0,
  6000.0, 6000.0, 6000.0, 6000.0, 6000.0,
  6000.0, 6000.0, 6000.0, 6000.0, 6000.0,
  6000.0, 6000.0, 6000.0, 6000.0, 6000.0
};
int mapsect[25] =         //000A4624
{
  10, 10, 10, 10, 10,
  10, 10, 10, 10, 10,
  10, 10, 10, 10, 10,
  10, 10, 10, 10, 10,
  10, 10, 10, 10, 10
};
int invulnerable[16];     //00149EB0

//-------------------------------------------------------------------------------------------------
//00036C00
void finish_race()
{
  int iAiCarCount; // edi
  int iLap; // esi
  int iCarOrderIdx; // edx
  int iCurrentCarId; // eax
  int iFinishedCarIndex; // edx
  int ii; // eax
  int iNearCarId; // ecx
  int iFinishingCarId; // eax
  int iCarId; // ebx
  int iTotalLaps; // ebp
  int iChunk2; // ecx
  int iLap_1; // eax
  int iChunk2_1; // ebx
  int iRandomOffset; // eax
  int iTrackLength; // ebp
  int iChunkDistance; // ebx
  int iDistanceCounter; // edx
  int iLapThreshold; // eax
  int iChampAiIndex; // edx
  int j; // ecx
  int k; // eax
  int iChampionshipCarId; // eax
  int iChampPlayerIndex; // edx
  int iPlayerPlaceIndex; // ebx
  int m; // eax
  int iAiPlacementIndex; // ebx
  int iRandomValue; // eax
  int iRandomCarIndex; // eax
  int iRandomPlayerIndex; // edx
  int iPlayerPlacementIndex; // ebx
  int n; // eax
  int iCarIndex; // ebp
  int iCarArrayIndex; // esi
  int iTargetPosition; // eax
  int i; // ebx
  int iTargetCarId; // edx
  int iCurrentPosition; // ebx
  int iPositionIndex; // edx
  int iCar; // ecx
  int iCarBytesTotal; // ebx
  int iCarByteOffset; // edx
  int iCarInitIndex; // eax
  int iLapTimeIndex; // ebx
  int iLapTimeCarIndex; // ecx
  int iLapTimeCarId; // edx
  double dRunningLapTime; // st7
  int iRaceTimeCarId; // edx
  int iAiCarBytes; // edi
  int iByteOffset; // ebx
  char byFinishingLap; // cl
  int iLapAssignCarId; // edx
  double dTimeDiff; // st7
  double dRemainingTime; // st7
  double dLapThreshold; // st6
  int iRacePosition; // [esp+8h] [ebp-3Ch]
  float fTotalRaceTime; // [esp+10h] [ebp-34h]
  float fWinnerTotalTime; // [esp+14h] [ebp-30h]
  int iMaxRacerIndex; // [esp+18h] [ebp-2Ch]
  int iCarCounter; // [esp+1Ch] [ebp-28h]
  int iFinishingCarId_1; // [esp+1Ch] [ebp-28h]
  int iAiOrderIndex; // [esp+1Ch] [ebp-28h]
  int iPlayerOrderIndex; // [esp+1Ch] [ebp-28h]
  int iRandomOrderCounter; // [esp+1Ch] [ebp-28h]
  int iRandomPlayerCounter; // [esp+1Ch] [ebp-28h]
  int iFinishCarIdx; // [esp+1Ch] [ebp-28h]
  int iCarInitCounter; // [esp+1Ch] [ebp-28h]
  int iCarCountera; // [esp+1Ch] [ebp-28h]
  float fReferenceLapTime; // [esp+20h] [ebp-24h]
  float fRandomLapVariation; // [esp+24h] [ebp-20h]
  float fTimeDifference; // [esp+28h] [ebp-1Ch]

  if (player_type == 1)
    ROLLERsrand(random_seed);
  iAiCarCount = 0;                              // Count AI cars (non-human controlled) that are still alive
  iCarCounter = 0;
  iLap = Car[carorder[0]].byLap;
  if (racers > 0) {
    iCarOrderIdx = 0;
    do {
      iCurrentCarId = carorder[iCarOrderIdx];
      if (!human_control[iCurrentCarId] && (char)Car[iCurrentCarId].byLives > 0)
        ++iAiCarCount;
      ++iCarOrderIdx;
      ++iCarCounter;
    } while (iCarOrderIdx < racers);
  }
  if (iLap <= NoOfLaps)                       // Check if race is still in progress (current lap <= total laps)
  {
    iLapThreshold = NoOfLaps - 2;               // Handle near-end-of-race logic (last 2 laps)
    if (NoOfLaps - 2 < 1)
      iLapThreshold = 1;
    if (iLap >= iLapThreshold) {
      iFinishCarIdx = 0;                        // Handle human players finishing race in final laps
      if (racers > 0) {
        iCarIndex = 0;
        iCarArrayIndex = 0;
        iMaxRacerIndex = racers - 1;
        do {                                       // Find human player and update finishing order
          if (human_control[iCarIndex] && !finished_car[iCarIndex]) {
            iRacePosition = Car[iCarArrayIndex].byRacePosition;
            iTargetPosition = iMaxRacerIndex;
            for (i = iMaxRacerIndex; ; --i) {
              iTargetCarId = carorder[i];
              if (!finished_car[iTargetCarId])
                break;
              --iTargetPosition;
            }
            finished_car[iTargetCarId] = -1;
            iCurrentPosition = iRacePosition;
            if (iTargetPosition > iRacePosition) {
              iPositionIndex = iRacePosition;   // Shift car positions in finishing order array
              do {
                iCar = carorder[iPositionIndex + 1];
                carorder[iPositionIndex] = iCar;
                Car[iCar].byRacePosition = iCurrentPosition++;
                ++iPositionIndex;
              } while (iCurrentPosition < iTargetPosition);
            }
            carorder[iTargetPosition] = iFinishCarIdx;
            Car[iCarArrayIndex].byLives = -1;
            Car[iCarArrayIndex].byRacePosition = iFinishCarIdx;
          }
          ++iCarIndex;
          ++iCarArrayIndex;
          ++iFinishCarIdx;
        } while (iFinishCarIdx < racers);
      }
    } else if (game_type == 1 && Race > 0)      // Championship mode: Order AI cars by championship standings
    {
      iChampAiIndex = 0;
      iAiOrderIndex = 0;
      if (iAiCarCount > 0) {
        for (j = 0; j < iAiCarCount; ++j) {
          for (k = iChampAiIndex; ; ++k) {
            ++iChampAiIndex;
            if (!human_control[champorder[k]])
              break;
          }
          iChampionshipCarId = champorder[k];
          carorder[j] = iChampionshipCarId;
          finished_car[iChampionshipCarId] = -1;
          ++iAiOrderIndex;
        }
      }
      iChampPlayerIndex = 0;
      iPlayerOrderIndex = 0;
      if (players > 0) {
        iPlayerPlaceIndex = iAiCarCount;
        do {
          for (m = iChampPlayerIndex; !human_control[m]; ++m)
            ++iChampPlayerIndex;
          if (!finished_car[m]) {
            Car[iChampPlayerIndex].byLives = -1;
            carorder[iPlayerPlaceIndex] = iChampPlayerIndex;
            finished_car[m] = -1;
          }
          ++iChampPlayerIndex;
          ++iPlayerPlaceIndex;
          ++iPlayerOrderIndex;
        } while (iPlayerOrderIndex < players);
      }
    } else {
      iRandomOrderCounter = 0;                  // Random placement mode: Randomly order AI cars for finish
      if (iAiCarCount > 0) {
        iAiPlacementIndex = 0;
        while (1) {
          iRandomValue = ROLLERrandRaw();
          iRandomCarIndex = GetHighOrderRand(numcars, iRandomValue);
          //iRandomCarIndex = (iRandomValue * numcars - (__CFSHL__((iRandomValue * numcars) >> 31, 15) + ((iRandomValue * numcars) >> 31 << 15))) >> 15;
          if (iRandomCarIndex == numcars)
            iRandomCarIndex = numcars - 1;
          if (!human_control[iRandomCarIndex] && !finished_car[iRandomCarIndex]) {
            carorder[iAiPlacementIndex++] = iRandomCarIndex;
            finished_car[iRandomCarIndex] = -1;
            ++iRandomOrderCounter;
            if (iAiPlacementIndex >= iAiCarCount)
              break;
          }
        }
      }
      iRandomPlayerIndex = 0;
      iRandomPlayerCounter = 0;
      if (players > 0) {
        iPlayerPlacementIndex = iAiCarCount;
        do {
          for (n = iRandomPlayerIndex; !human_control[n]; ++n)
            ++iRandomPlayerIndex;
          if (!finished_car[n]) {
            Car[iRandomPlayerIndex].byLives = -1;
            carorder[iPlayerPlacementIndex] = iRandomPlayerIndex;
            finished_car[n] = -1;
          }
          ++iRandomPlayerIndex;
          ++iPlayerPlacementIndex;
          ++iRandomPlayerCounter;
        } while (iRandomPlayerCounter < players);
      }
    }
    iCarInitCounter = 0;                        // Initialize AI car race times and best lap times
    if (numcars > 0) {
      iCarBytesTotal = 4 * numcars;
      iCarByteOffset = 0;
      iCarInitIndex = 0;
      do {
        if (!human_control[iCarByteOffset / 4u]) {
          Car[iCarInitIndex].fTotalRaceTime = 0.0;
          Car[iCarInitIndex].fBestLapTime = 9.9999998e17f;
        }
        iCarByteOffset += 4;
        ++iCarInitIndex;
        ++iCarInitCounter;
      } while (iCarByteOffset < iCarBytesTotal);
    }
    for (iCarCountera = 0; iCarCountera < NoOfLaps; ++iCarCountera)// Generate lap times for each AI car for all laps
    {
      iLapTimeIndex = 0;
      if (iAiCarCount > 0) {
        iLapTimeCarIndex = 0;
        do {
          iLapTimeCarId = carorder[iLapTimeCarIndex];
          fRandomLapVariation = (float)((double)ROLLERrand() * 0.000030517578125 + 0.02);// Add random variation (0.02-0.05s) to base lap time
          if (iLapTimeIndex)
            dRunningLapTime = Car[carorder[iLapTimeCarIndex - 1]].fRunningLapTime;
            //dRunningLapTime = Car[nearcall[3][iLapTimeCarIndex + 3]].fRunningLapTime;
          else
            dRunningLapTime = RecordLaps[game_track];
          Car[iLapTimeCarId].fRunningLapTime = (float)dRunningLapTime + fRandomLapVariation;
          iRaceTimeCarId = iLapTimeCarId;
          Car[iRaceTimeCarId].fTotalRaceTime = Car[iRaceTimeCarId].fRunningLapTime + Car[iRaceTimeCarId].fTotalRaceTime;
          if (Car[iRaceTimeCarId].fBestLapTime > (double)Car[iRaceTimeCarId].fRunningLapTime)
            Car[iRaceTimeCarId].fBestLapTime = Car[iRaceTimeCarId].fRunningLapTime;
          ++iLapTimeIndex;
          ++iLapTimeCarIndex;
        } while (iLapTimeIndex < iAiCarCount);
      }
    }
    fTotalRaceTime = Car[carorder[0]].fTotalRaceTime;// Calculate final race positions based on total race times
    fReferenceLapTime = RecordLaps[game_track];
    if (fReferenceLapTime < 10.0)
      fReferenceLapTime = 10.0;
    if (iAiCarCount > 0) {
      iAiCarBytes = 4 * iAiCarCount;
      iByteOffset = 0;
      byFinishingLap = NoOfLaps + 1;
      do {
        iLapAssignCarId = carorder[iByteOffset / 4u];
        dTimeDiff = Car[iLapAssignCarId].fTotalRaceTime - fTotalRaceTime;
        Car[iLapAssignCarId].byLap = byFinishingLap;
        if (dTimeDiff > fReferenceLapTime)    // Calculate how many laps behind based on time difference
        {
          fTimeDifference = (float)dTimeDiff;
          dRemainingTime = fTimeDifference;
          dLapThreshold = fReferenceLapTime;
          do {
            dRemainingTime = dRemainingTime - dLapThreshold;
            --Car[iLapAssignCarId].byLap;
          } while (dRemainingTime > dLapThreshold);
        }
        iByteOffset += 4;
      } while (iByteOffset < iAiCarBytes);
    }
  } else {                                             // Race in progress: Process cars finishing current lap
    while (finishers < racers) {
      iFinishedCarIndex = 0;
      for (ii = 0; finished_car[carorder[ii]]; ++ii)
        ++iFinishedCarIndex;
      iNearCarId = carorder[iFinishedCarIndex];
      //iNearCarId = nearcall[3][iFinishedCarIndex + 3];
      fWinnerTotalTime = Car[iNearCarId].fTotalRaceTime;
      iFinishingCarId = carorder[iFinishedCarIndex];
      iCarId = iFinishingCarId;
      iTotalLaps = NoOfLaps;
      iChunk2 = Car[iNearCarId].nReferenceChunk;
      finished_car[iFinishingCarId] = -1;
      iFinishingCarId_1 = iFinishingCarId;
      iLap_1 = Car[iFinishingCarId].byLap;
      ++finishers;
      if (iLap_1 == iTotalLaps)               // Check if car completed all laps
      {
        Car[iCarId].byLap = NoOfLaps + 1;
        iChunk2_1 = Car[iCarId].nReferenceChunk;
        if (iChunk2 < iChunk2_1) {
          iTrackLength = TRAK_LEN;
          iRandomOffset = 12 * ROLLERrand() / 0x8000;
          iChunk2 += iTrackLength;
        } else {
          iRandomOffset = 12 * ROLLERrand() / 0x8000;
        }
        iChunkDistance = iChunk2 - iChunk2_1 + iRandomOffset;
        iDistanceCounter = 0;
        for (Car[iFinishingCarId_1].fTotalRaceTime = fWinnerTotalTime + 0.02f;
              iDistanceCounter < iChunkDistance;
              Car[iFinishingCarId_1].fTotalRaceTime = (float)((double)ROLLERrand() * 0.1 * 0.000030517578125 + 0.1 + Car[iFinishingCarId_1].fTotalRaceTime))// Adjust finishing time based on track position
        {
          ++iDistanceCounter;
        }
      } else if (iLap_1 < iTotalLaps) {
        ++Car[iCarId].byLap;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00037250
double getbankz(float fInput, int iChunkIdx, tData *pData)
{
  tData *pUseData; // edx
  tGroundPt *pGroundPt; // ebx
  double dCalcVal; // st7
  int iSelectedPointIdx; // esi
  int iCurrPointIdx; // ecx
  tVec3 *pPoint1; // ebx
  tGroundPt *pGroundPt2; // ecx
  float fPrevCalcVal; // [esp+0h] [ebp-20h]
  float fCurrCalcVal; // [esp+4h] [ebp-1Ch]

  // Default to localdata if pData is NULL
  pUseData = pData;
  if (!pData)
    pUseData = &localdata[iChunkIdx];

  // Get ground points for this chunk
  pGroundPt = &GroundPt[iChunkIdx];

  // Calculate first dot product
  dCalcVal = (pGroundPt->pointAy[0].fY + pUseData->pointAy[3].fY) * pUseData->pointAy[1].fY
    + (pGroundPt->pointAy[0].fX + pUseData->pointAy[3].fX) * pUseData->pointAy[0].fY
    + (pGroundPt->pointAy[0].fZ + pUseData->pointAy[3].fZ) * pUseData->pointAy[2].fY;

  iSelectedPointIdx = 4;                        // default to point 4 if no point found
  iCurrPointIdx = 1;                            // start checking from point 1
  pPoint1 = &pGroundPt->pointAy[1];             // point to second ground point
  fPrevCalcVal = (float)dCalcVal;

  // Loop through ground points to find appropriate interpolation segment
  while (1) {
    fCurrCalcVal = (pPoint1->fY + pUseData->pointAy[3].fY) * pUseData->pointAy[1].fY
      + (pPoint1->fX + pUseData->pointAy[3].fX) * pUseData->pointAy[0].fY
      + (pPoint1->fZ + pUseData->pointAy[3].fZ) * pUseData->pointAy[2].fY;

// If input is greater than current dot product, found segment
    if (fInput > (double)fCurrCalcVal)
      break;

    // Move to next point
    ++pPoint1;
    ++iCurrPointIdx;
    fPrevCalcVal = fCurrCalcVal;
    if (iCurrPointIdx >= 6)
      goto INTERPOLATION_CALC;
  }

  // Use the point before the one that exceeded input value
  iSelectedPointIdx = iCurrPointIdx - 1;
INTERPOLATION_CALC:
  // Calculate interpolated Z value using weighted average of two points
  // First term: contribution from point at selectedPointIndex
  // Second term: contribution from point at selectedPointIndex + 1
  pGroundPt2 = &GroundPt[iChunkIdx];
  return ((pGroundPt2->pointAy[iSelectedPointIdx].fY + pUseData->pointAy[3].fY) * pUseData->pointAy[1].fZ
        + (pGroundPt2->pointAy[iSelectedPointIdx].fX + pUseData->pointAy[3].fX) * pUseData->pointAy[0].fZ
        + (pGroundPt2->pointAy[iSelectedPointIdx].fZ + pUseData->pointAy[3].fZ) * pUseData->pointAy[2].fZ)
    * (fInput - fCurrCalcVal)                // weight for first point
    / (fPrevCalcVal - fCurrCalcVal)          // total range
    + ((pGroundPt2->pointAy[iSelectedPointIdx + 1].fY + pUseData->pointAy[3].fY) * pUseData->pointAy[1].fZ
     + (pGroundPt2->pointAy[iSelectedPointIdx + 1].fX + pUseData->pointAy[3].fX) * pUseData->pointAy[0].fZ
     + (pGroundPt2->pointAy[iSelectedPointIdx + 1].fZ + pUseData->pointAy[3].fZ) * pUseData->pointAy[2].fZ)
    * (fInput - fPrevCalcVal)                // weight for second point
    / (fCurrCalcVal - fPrevCalcVal);         // total range
}

//-------------------------------------------------------------------------------------------------
//000373A0
void calculate_aheadbehindtime(int iCarIdx, float *pfAheadTime, float *pfBehindTime)
{
  int iCurrentCarId; // eax
  int iCurrChunk; // esi
  int iAheadCarId; // edi
  int nCurrChunk; // esi
  int iAheadCarRef; // edi
  float fFinalSpeed; // eax
  double dTimeCalc; // st7
  int iRacePosition; // eax
  int iBehindCarId; // esi
  int iLastValidChunk; // eax
  int iCurrentCarRef; // ecx
  int iBehindCarRef; // esi
  float fBehindSpeed; // eax
  double dBehindTimeCalc; // st7
  float fAheadTimeResult; // [esp+0h] [ebp-44h]
  float fBehindTimeResult; // [esp+0h] [ebp-44h]
  float fTrackDistance; // [esp+4h] [ebp-40h]
  float fBehindTrackDistance; // [esp+8h] [ebp-3Ch]
  float fAheadSpeed; // [esp+Ch] [ebp-38h]
  float fBehindSpeedFinal; // [esp+10h] [ebp-34h]
  float fAheadDistance; // [esp+1Ch] [ebp-28h]
  float fAheadCarPosition; // [esp+20h] [ebp-24h]
  float fBehindDistance; // [esp+24h] [ebp-20h]
  float fBehindCarPosition; // [esp+28h] [ebp-1Ch]
  float fCurrentCarPosition; // [esp+2Ch] [ebp-18h]

  iCurrentCarId = iCarIdx;
  if ((char)Car[iCurrentCarId].byLives > 0)   // Check if current car is alive, otherwise return -1 for both times
  {
    iCurrChunk = Car[iCurrentCarId].nCurrChunk; // Calculate current car's absolute position on track (chunk + lap distance)
    if (iCurrChunk == -1)
      iCurrChunk = Car[iCurrentCarId].iLastValidChunk;
    fCurrentCarPosition = (float)(averagesectionlen * iCurrChunk + totaltrackdistance * (char)Car[iCarIdx].byLapNumber);
    if (Car[iCarIdx].nCurrChunk != -1)
      fCurrentCarPosition = fCurrentCarPosition + Car[iCarIdx].pos.fX;
    if (Car[iCarIdx].byRacePosition)          // Calculate time to car ahead (if not in first position)
    {
      //TODO recheck this offset to ensure it is correct
      iAheadCarId = carorder[Car[iCarIdx].byRacePosition - 1];// Get car ahead from race position order
      if ((char)Car[iAheadCarId].byLives > 0) {
        nCurrChunk = Car[iAheadCarId].nCurrChunk;
        if (nCurrChunk == -1)
          nCurrChunk = Car[iAheadCarId].iLastValidChunk;
        fAheadCarPosition = (float)(averagesectionlen * nCurrChunk + totaltrackdistance * (char)Car[iAheadCarId].byLapNumber);// Calculate ahead car's absolute position on track
        if (Car[iAheadCarId].nCurrChunk != -1)
          fAheadCarPosition = fAheadCarPosition + Car[iAheadCarId].pos.fX;
        iAheadCarRef = iAheadCarId;
        fAheadDistance = (float)fabs(fCurrentCarPosition - fAheadCarPosition);// Calculate absolute distance between cars
        if (Car[iCarIdx].fFinalSpeed <= (double)Car[iAheadCarRef].fFinalSpeed)// Use higher speed of the two cars for time calculation
          fFinalSpeed = Car[iAheadCarRef].fFinalSpeed;
        else
          fFinalSpeed = Car[iCarIdx].fFinalSpeed;
        fAheadSpeed = fFinalSpeed;
        if (fFinalSpeed < 100.0)
          fAheadSpeed = 100.0;
        fTrackDistance = (float)totaltrackdistance;
        if (fAheadDistance >= (double)fTrackDistance)// Convert distance to time: if >1 lap apart use large penalty, else distance/speed
          dTimeCalc = fAheadDistance * 10000000.0 / fTrackDistance;
        else
          dTimeCalc = fAheadDistance / fAheadSpeed * 0.027777778;
        fAheadTimeResult = (float)dTimeCalc;
        *pfAheadTime = fAheadTimeResult;
      } else {
        *pfAheadTime = -1.0;
      }
    } else {
      *pfAheadTime = -1.0;
    }
    iRacePosition = Car[iCarIdx].byRacePosition;// Calculate time to car behind (if not in last position)
    if (iRacePosition == numcars - 1) {
      *pfBehindTime = -1.0;
    } else {
      iBehindCarId = carorder[iRacePosition + 1];// Get car behind from race position order
      if ((char)Car[iBehindCarId].byLives > 0) {
        iLastValidChunk = Car[iBehindCarId].nCurrChunk;
        if (iLastValidChunk == -1)
          iLastValidChunk = Car[iBehindCarId].iLastValidChunk;
        fBehindCarPosition = (float)(averagesectionlen * iLastValidChunk + totaltrackdistance * (char)Car[iBehindCarId].byLapNumber);// Calculate behind car's absolute position and distance/time gap
        if (Car[iBehindCarId].nCurrChunk != -1)
          fBehindCarPosition = fBehindCarPosition + Car[iBehindCarId].pos.fX;
        iCurrentCarRef = iCarIdx;
        iBehindCarRef = iBehindCarId;
        fBehindDistance = (float)fabs(fCurrentCarPosition - fBehindCarPosition);
        if (Car[iCurrentCarRef].fFinalSpeed <= (double)Car[iBehindCarRef].fFinalSpeed)
          fBehindSpeed = Car[iBehindCarRef].fFinalSpeed;
        else
          fBehindSpeed = Car[iCurrentCarRef].fFinalSpeed;
        fBehindSpeedFinal = fBehindSpeed;
        if (fBehindSpeed < 100.0)
          fBehindSpeedFinal = 100.0;
        fBehindTrackDistance = (float)totaltrackdistance;
        if (fBehindDistance >= (double)fBehindTrackDistance)
          dBehindTimeCalc = fBehindDistance * 10000000.0 / fBehindTrackDistance;
        else
          dBehindTimeCalc = fBehindDistance / fBehindSpeedFinal * 0.027777778;
        fBehindTimeResult = (float)dBehindTimeCalc;
        *pfBehindTime = fBehindTimeResult;
      } else {
        *pfBehindTime = -1.0;
      }
    }
  } else {
    *pfAheadTime = -1.0;
    *pfBehindTime = -1.0;
  }
}

//-------------------------------------------------------------------------------------------------
//000376B0
void initnearcars()
{
  int iAICarsRemaining; // edi
  int iCarIndex; // ebx
  int iCurrentCarIdx; // ebx
  int iCarsPerGroup; // esi
  //int iGroupCarCount; // ecx
  //int iNearCallOffset; // edx
  //int iTempCarIdx; // eax
  int iGroupSize; // [esp+0h] [ebp-20h]
  //int iGroupOffset; // [esp+4h] [ebp-1Ch]

  memset(nearcall, 255, sizeof(nearcall));      // Initialize nearcall array with -1 (no car assigned)
  iAICarsRemaining = 0;                         // Count total number of AI-controlled cars
  if (numcars > 0) {
    iCarIndex = 0;
    do {                                           // Skip non-competitors and human-controlled cars
      if (!non_competitors[iCarIndex] && !human_control[iCarIndex])
        ++iAICarsRemaining;
      ++iCarIndex;
    } while (iCarIndex < numcars);
  }

  iCurrentCarIdx = 0;
  iGroupSize = 4;                                    // Start with group size 4, distribute AI cars across 4 groups
  for (int iGroup = 0; iGroup < 4; iGroup++)            // Loop through 4 nearcall groups: [0],[1],[2],[3]
  {
    iCarsPerGroup = iAICarsRemaining / iGroupSize; // Calculate how many cars to assign to current group
    for (int iSlot = 0; iSlot < iCarsPerGroup; iSlot++)
    {
      // Find next AI car (skip non-competitors and human players)
      while (non_competitors[iCurrentCarIdx] || human_control[iCurrentCarIdx])
      {
        iCurrentCarIdx++;
      }
      // Assign AI car index to current nearcall group slot
      nearcall[iGroup][iSlot] = iCurrentCarIdx;
      iCurrentCarIdx++;
    }
    iAICarsRemaining -= iCarsPerGroup;             // Subtract assigned cars from remaining count
    iGroupSize--;                                   // Decrease group size for uneven distribution
  }
  //iCurrentCarIdx = 0;
  //iGroupSize = 4;                               // Start with group size 4, distribute AI cars across 4 groups
  //for (iGroupOffset = 0; iGroupOffset != 64; iGroupOffset += 16)// Loop through 4 nearcall groups: [0],[1],[2],[3]
  //{
  //  iCarsPerGroup = iAICarsRemaining / iGroupSize;// Calculate how many cars to assign to current group
  //  iGroupCarCount = 0;
  //  if (iAICarsRemaining / iGroupSize > 0) {
  //    iNearCallOffset = iGroupOffset;
  //    do {                                         // Find next AI car (skip non-competitors and human players)
  //      for (iTempCarIdx = iCurrentCarIdx; non_competitors[iTempCarIdx] || human_control[iTempCarIdx]; ++iTempCarIdx)
  //        ++iCurrentCarIdx;
  //      ++iGroupCarCount;
  //      *(int *)((char *)nearcall[0] + iNearCallOffset) = iCurrentCarIdx;// Assign AI car index to current nearcall group slot
  //      iNearCallOffset += 4;
  //      ++iCurrentCarIdx;
  //    } while (iGroupCarCount < iCarsPerGroup);
  //  }
  //  iAICarsRemaining -= iCarsPerGroup;          // Subtract assigned cars from remaining count
  //  --iGroupSize;                               // Decrease group size for uneven distribution
  //}
  nearcarcheck = 0;                             // Reset near car check counter
}

//-------------------------------------------------------------------------------------------------
//00037780
void initpits()
{
  int iChunkIdx_1; // eax
  int iPitStopCount; // ecx
  int iStopsIdx; // ebx
  int iChunkIdx; // edx


  // Start from last track chunk and work backwards
  iChunkIdx_1 = TRAK_LEN - 1;
  iPitStopCount = 0;

  // Process all track segments
  if (TRAK_LEN - 1 >= 0) {
    iStopsIdx = 0;
    iChunkIdx = iChunkIdx_1;
    do {
      if ((TrakColour[iChunkIdx][TRAK_COLOUR_CENTER] & SURFACE_FLAG_PIT_BOX) != 0)
      {
        // store pit stop position
        stops[iStopsIdx] = iChunkIdx_1; // reference into stops

        ++iStopsIdx;
        ++iPitStopCount;
      }
      --iChunkIdx_1;
      --iChunkIdx;
    } while (iChunkIdx_1 >= 0);
  }
  numstops = iPitStopCount;
}

//-------------------------------------------------------------------------------------------------
//000377D0
void dodamage(tCar *pCar, float fDamage)
{
  tCarEngine *pCarEngine; // edi
  int iDriverIdx; // eax
  tCarSpray *pCarSpray; // ebx
  int iSprayIdx; // ecx
  int iRandValue1; // eax
  tCarSpray *pPlayerSpray; // ebx
  int i; // ecx
  int iRandValue2; // eax
  uint8 byStatusFlags; // dh
  int iPlayerType; // ebx
  int iDeadDriverIdx; // edi
  int iGameTrack; // eax
  int iCurrentDriverIdx; // ecx
  int iRacePosition; // ebx
  int j; // edx
  int iCurrentPos; // edx
  int iPosIdx; // eax
  int iCarOrderValue; // ebx
  int iFinishers; // edi
  int iHumanControl; // ebp
  int iCheckDriverIdx; // edx
  int iPlayer2DriverIdx; // ebp
  int iPlayerTypeCheck; // eax
  uint8 byCarDesignIdx; // bl
  int iFinalDriverIdx; // edi
  float fHealth; // [esp+0h] [ebp-20h]
  int iDamageIntensity; // [esp+4h] [ebp-1Ch]
  int iRacePosCount; // [esp+4h] [ebp-1Ch]

  pCarEngine = &CarEngines.engines[pCar->byCarDesignIdx];// Get car engine data based on car design
  fHealth = pCar->fHealth;
  iDriverIdx = pCar->iDriverIdx;
  if (!invulnerable[iDriverIdx] && !finished_car[iDriverIdx])// Check if car is vulnerable and not finished
  {                                             // Check if car is not already destroyed (status flag 4)
    if ((pCar->byStatusFlags & 4) == 0) {
      pCarSpray = CarSpray[pCar->iDriverIdx];
      if (fDamage > 0.55)                     // Handle visual spray effects for significant damage (> 0.55)
      {
        //_CHP();
        iDamageIntensity = (int)(fDamage * 16.0);
        pCar->byDamageToggle = 1 - pCar->byDamageToggle;
        iSprayIdx = 0;
        pCar->byDamageIntensity = iDamageIntensity;
        do {
          if (pCarSpray->iLifeTime > 0) {
            ROLLERrandRaw();
          } else {
            iRandValue1 = ROLLERrandRaw();
            if (GetHighOrderRand(12, iRandValue1) < iDamageIntensity)
              pCarSpray->iTimer = -1;
          }
          ++iSprayIdx;
          ++pCarSpray;
        } while (iSprayIdx < 32);               // Loop through all 32 spray effects for this car
        if (player_type == 2) {
          pPlayerSpray = 0;
          if (player1_car == pCar->iDriverIdx)
            pPlayerSpray = CarSpray[16];
          if (player2_car == pCar->iDriverIdx)
            pPlayerSpray = CarSpray[17];
          if (pPlayerSpray) {
            for (i = 0; i < 32; ++i) {
              if (pPlayerSpray->iLifeTime <= 0) {
                iRandValue2 = ROLLERrandRaw();
                if (GetHighOrderRand(12, iRandValue2) < iDamageIntensity)
                  pPlayerSpray->iTimer = -1;
              }
              ++pPlayerSpray;
            }
          }
        }
      }
    }
    pCar->fHealth = pCar->fHealth - fDamage * pCarEngine->fDurability;// Apply damage to car health (modified by engine durability)
    if (pCar->fHealth < 1.0)                  // Handle car destruction when health drops below 1.0
    {
      byStatusFlags = pCar->byStatusFlags;
      pCar->fHealth = 0.0;
      if ((byStatusFlags & 4) == 0) {
        --pCar->byLives;
        iPlayerType = player_type;
        pCar->nDeathTimer = 18;
        // CHEAT_MODE_CLONES
        if (iPlayerType != 2 && (cheat_mode & CHEAT_MODE_CLONES) == 0) {
          iDeadDriverIdx = pCar->iDriverIdx;
          if (player1_car != iDeadDriverIdx && pCar->byCarDesignIdx == Car[player1_car].byCarDesignIdx && !human_control[iDeadDriverIdx] && pCar->byCarDesignIdx <= 7u) {
            // SOUND_SAMPLE_ARIEL
            speechsample(pCar->byCarDesignIdx + SOUND_SAMPLE_ARIEL, 20000, 18, player1_car + 17152);
            // SOUND_SAMPLE_TGONE
            speechsample(SOUND_SAMPLE_TGONE, 20000, 0, player1_car);
          }
        }
        iGameTrack = game_track;
        pCar->nChangeMateCooldown = 1080;
        ++RecordKills[iGameTrack];
        iCurrentDriverIdx = pCar->iDriverIdx;
        if (iCurrentDriverIdx == ViewType[0]) {
          if (iCurrentDriverIdx == player1_car)
            DeathView[0] = SelectedView[0];
          if (SelectedView[0] != 1 && SelectedView[0] != 3) {
            SelectedView[0] = 1;
            select_view(0);
            initcarview(ViewType[0], 0);
          }
        }
        if (iCurrentDriverIdx == ViewType[1]) {
          if (iCurrentDriverIdx == player2_car)
            DeathView[1] = SelectedView[1];
          if (SelectedView[1] != 1 && SelectedView[1] != 3) {
            SelectedView[1] = 1;
            select_view(1);
            initcarview(ViewType[1], 1);
          }
        }
        if (!pCar->byLives && !finished_car[iCurrentDriverIdx]) {
          iRacePosition = pCar->byRacePosition; // Update race positions when car is eliminated
          ++Destroyed;
          iRacePosCount = racers - 1;
          for (j = racers - 1; finished_car[carorder[j]]; --j)
            --iRacePosCount;
          iCurrentPos = iRacePosition;
          finished_car[iCurrentDriverIdx] = -1;
          if (iRacePosition < iRacePosCount) {
            iPosIdx = iRacePosition;
            do {
              iCarOrderValue = carorder[iPosIdx + 1];
              carorder[iPosIdx] = iCarOrderValue;
              Car[iCarOrderValue].byRacePosition = iCurrentPos++;
              ++iPosIdx;
            } while (iCurrentPos < iRacePosCount);
          }
          carorder[iRacePosCount] = iCurrentDriverIdx;
          iFinishers = finishers;
          pCar->byRacePosition = iRacePosCount;
          iHumanControl = human_control[iCurrentDriverIdx];
          finishers = iFinishers + 1;
          if (iHumanControl)
            ++human_finishers;
          if ((iCurrentDriverIdx == player1_car || iCurrentDriverIdx == player2_car) && (char)pCar->byLives > 0)
            // SOUND_SAMPLE_RUBBISH
            speechsample(SOUND_SAMPLE_RUBBISH, 0x8000, 18, iCurrentDriverIdx);
        }
        iCheckDriverIdx = pCar->iDriverIdx;
        if (player1_car == iCheckDriverIdx || player2_car == iCheckDriverIdx) {
          // SOUND_SAMPLE_FATALITY
          speechsample(SOUND_SAMPLE_FATALITY, 0x8000, 18, pCar->iDriverIdx);// Play death sound effects for human players
          if (!pCar->byLives) {
            // SOUND_SAMPLE_0LEFT
            speechsample(SOUND_SAMPLE_0LEFT, 0x8000, 18, pCar->iDriverIdx);
            // SOUND_SAMPLE_RACEOVER
            speechsample(SOUND_SAMPLE_RACEOVER, 0x8000, 18, pCar->iDriverIdx);
          }
        }
        if (pCar->byDamageSourceTimer)
          ++Car[pCar->byAttacker].byKills;      // Track kills for the attacking car
        if (pCar->byDamageSourceTimer && pCar->byAttacker == player1_car) {
          speechsample(SOUND_SAMPLE_GOTONE, 0x8000, 18, player1_car);
          Victim = pCar->iDriverIdx;
        } else if (player1_car != pCar->iDriverIdx) {
          if ((char)pCar->byLives <= 0)
            sprintf(buffer, "%s %s", &language_buffer[1984], driver_names[pCar->iDriverIdx]);
          else
            sprintf(buffer, "%s %s", &language_buffer[1920], driver_names[pCar->iDriverIdx]);
          small_zoom(buffer);
          if (pCar->byDamageSourceTimer) {
            sprintf(buffer, "%s %s", &language_buffer[2176], driver_names[pCar->byAttacker]);
            subzoom(buffer);
          }
        }
        if (player_type == 2) {
          if (pCar->byDamageSourceTimer && pCar->byAttacker == player2_car) {
            // SOUND_SAMPLE_GOTONE
            speechsample(SOUND_SAMPLE_GOTONE, 0x8000, 18, player2_car);
            Victim = pCar->iDriverIdx;
          } else {
            iPlayer2DriverIdx = pCar->iDriverIdx;
            if (player2_car != iPlayer2DriverIdx) {
              if ((char)pCar->byLives <= 0)
                sprintf(buffer, "%s %s", &language_buffer[1984], driver_names[iPlayer2DriverIdx]);
              else
                sprintf(buffer, "%s %s", &language_buffer[1920], driver_names[iPlayer2DriverIdx]);
              small_zoom(buffer);
              if (pCar->byDamageSourceTimer) {
                sprintf(buffer, "%s %s", &language_buffer[2176], driver_names[pCar->byAttacker]);
                subzoom(buffer);
              }
            }
          }
        }
      }
      pCar->byStatusFlags |= 4u;
    }
    if (pCar->fHealth < 30.0 && fHealth >= 30.0)// Handle transition from healthy to critical health (30.0 threshold)
    {
      iPlayerTypeCheck = player_type;
      pCar->nChangeMateCooldown = 1080;
      // CHEAT_MODE_CLONES
      if (iPlayerTypeCheck != 2 && (cheat_mode & CHEAT_MODE_CLONES) == 0 && player1_car != pCar->iDriverIdx) {
        byCarDesignIdx = pCar->byCarDesignIdx;
        if (byCarDesignIdx == Car[player1_car].byCarDesignIdx && !human_control[pCar->iDriverIdx] && byCarDesignIdx <= 7u) {
          // SOUND_SAMPLE_ARIEL
          speechsample(byCarDesignIdx + SOUND_SAMPLE_ARIEL, 20000, 18, player1_car + 17408);
          // SOUND_SAMPLE_TDAMAGE
          speechsample(SOUND_SAMPLE_TDAMAGE, 20000, 0, player1_car);
        }
      }
    }
  }
  iFinalDriverIdx = pCar->iDriverIdx;
  if ((player1_car == iFinalDriverIdx || player2_car == iFinalDriverIdx) && pCar->fHealth < 30.0 && fHealth >= 30.0 && pCar->byCarDesignIdx <= 7u)
    // SOUND_SAMPLE_BLOWTIME
    speechsample(SOUND_SAMPLE_BLOWTIME, 20000, 18, pCar->iDriverIdx);// Play critical health warning sound for human players
}

//-------------------------------------------------------------------------------------------------
//00037F00
void doviewtend(tCar *pCar, int iFrameDelta, int iViewIdx)
{
  int iSelectedView; // edx
  unsigned int iControlType; // eax
  double dChasePullCrash; // st7
  double dTempPullCrash; // st7
  int iViewIndex; // ebx
  double dTempPullCrash2; // st7
  double dChasePullNormal; // st7
  double dTempPullNormal; // st7
  int iViewIndex2; // ebx
  double dTempPullNormal2; // st7
  double dChasePullDefault; // st7
  double dTempPullDefault; // st7
  int iViewIndex3; // ebx
  double dTempPullDefault2; // st7
  double dHighSpeedCrashPull; // st7
  double dTempHighSpeedCrash; // st7
  int iViewIndex4; // ebx
  double dTempHighSpeedCrash2; // st7
  double dHighSpeedNormalPull; // st7
  double dTempHighSpeedNormal; // st7
  int iViewIndex5; // ebx
  double dTempHighSpeedNormal2; // st7
  int iHighSpeedPullIncrement; // [esp+4h] [ebp-50h]
  int iTargetPullNormal; // [esp+8h] [ebp-4Ch]
  int iTargetPullHighSpeed; // [esp+Ch] [ebp-48h]
  int iTargetPullCrash; // [esp+10h] [ebp-44h]
  int iTargetPullDefault; // [esp+14h] [ebp-40h]
  int iTargetPullHighSpeedCrash; // [esp+18h] [ebp-3Ch]
  float fTargetPullHighSpeed; // [esp+1Ch] [ebp-38h]
  float fTargetPullNormal; // [esp+20h] [ebp-34h]
  float fTargetPullHighSpeedCrash; // [esp+24h] [ebp-30h]
  float fTargetPullDefault; // [esp+28h] [ebp-2Ch]
  int iPullSpeed; // [esp+2Ch] [ebp-28h]
  float fTargetPullDefaultFloat; // [esp+30h] [ebp-24h]
  float fTargetPullCrashFloat; // [esp+34h] [ebp-20h]
  float fTargetPullNormalFloat; // [esp+38h] [ebp-1Ch]
  float fTargetPullCrashLimit; // [esp+3Ch] [ebp-18h]
  float fTargetPullHighSpeedLimit; // [esp+40h] [ebp-14h]
  float fTargetPullHighSpeedCrashLimit; // [esp+44h] [ebp-10h]

  iSelectedView = SelectedView[iViewIdx];       // Get the selected view type for this view index (a3: 0=player1 view, 1=player2 view)
  if (iSelectedView == 1 || iSelectedView == 3)// Only process chase views (view types 1 and 3)
  {
    iControlType = pCar->iControlType;          // Get car control type to determine pull behavior
    iPullSpeed = 40 * iFrameDelta;              // Calculate pull speed increment (40 units per frame) * frame delta
    if (iControlType < 2) {                                           // Control type 0: Autopilot/Default car behavior
      if (!iControlType) {
        dChasePullDefault = viewdata[chaseview[iViewIdx]].fChasePullDefault;// Get default chase pull distance from view data
        //_CHP();
        iTargetPullDefault = (int)dChasePullDefault;
        fTargetPullDefault = (float)dChasePullDefault;
        if (PULLZ[iViewIdx] > (double)fTargetPullDefault) {
          dTempPullDefault = PULLZ[iViewIdx] - (double)iPullSpeed;
          PULLZ[iViewIdx] = (float)dTempPullDefault;
          if (dTempPullDefault < fTargetPullDefault)
            PULLZ[iViewIdx] = fTargetPullDefault;
        }
        fTargetPullDefaultFloat = (float)iTargetPullDefault;
        iViewIndex3 = iViewIdx;
        if (PULLZ[iViewIndex3] < (double)fTargetPullDefaultFloat) {
          dTempPullDefault2 = (double)(40 * iFrameDelta) + PULLZ[iViewIndex3];
          PULLZ[iViewIndex3] = (float)dTempPullDefault2;
          if (dTempPullDefault2 > fTargetPullDefaultFloat)
            PULLZ[iViewIndex3] = fTargetPullDefaultFloat;
        }
      }
    } else if (iControlType <= 3) {                                           // Check if car is stunned/crashed to use crash pull values
      if (pCar->iStunned) {
        dChasePullCrash = viewdata[chaseview[iViewIdx]].fChasePullCrash;// Use crash pull distance for stunned cars
        //_CHP();
        iTargetPullCrash = (int)dChasePullCrash;
        fTargetPullCrashFloat = (float)dChasePullCrash;
        if (PULLZ[iViewIdx] > (double)fTargetPullCrashFloat) {
          dTempPullCrash = PULLZ[iViewIdx] - (double)iPullSpeed;
          PULLZ[iViewIdx] = (float)dTempPullCrash;
          if (dTempPullCrash < fTargetPullCrashFloat)
            PULLZ[iViewIdx] = fTargetPullCrashFloat;
        }
        fTargetPullCrashLimit = (float)iTargetPullCrash;
        iViewIndex = iViewIdx;
        if (PULLZ[iViewIndex] < (double)fTargetPullCrashLimit) {
          dTempPullCrash2 = (double)(40 * iFrameDelta) + PULLZ[iViewIndex];
          PULLZ[iViewIndex] = (float)dTempPullCrash2;
          if (dTempPullCrash2 > fTargetPullCrashLimit)
            PULLZ[iViewIndex] = fTargetPullCrashLimit;
        }
      } else {
        dChasePullNormal = viewdata[chaseview[iViewIdx]].fChasePullNormal;// Use normal pull distance for active cars
        //_CHP();
        iTargetPullNormal = (int)dChasePullNormal;
        fTargetPullNormal = (float)dChasePullNormal;
        if (PULLZ[iViewIdx] > (double)fTargetPullNormal) {
          dTempPullNormal = PULLZ[iViewIdx] - (double)iPullSpeed;
          PULLZ[iViewIdx] = (float)dTempPullNormal;
          if (dTempPullNormal < fTargetPullNormal)
            PULLZ[iViewIdx] = fTargetPullNormal;
        }
        fTargetPullNormalFloat = (float)iTargetPullNormal;
        iViewIndex2 = iViewIdx;
        if (PULLZ[iViewIndex2] < (double)fTargetPullNormalFloat) {
          dTempPullNormal2 = (double)(40 * iFrameDelta) + PULLZ[iViewIndex2];
          PULLZ[iViewIndex2] = (float)dTempPullNormal2;
          if (dTempPullNormal2 > fTargetPullNormalFloat)
            PULLZ[iViewIndex2] = fTargetPullNormalFloat;
        }
      }
    } else if (iControlType == 1002)            // Control type 1002: High-speed mode with modified pull distances
    {
      iHighSpeedPullIncrement = 160 * iFrameDelta;// Use faster pull speed for high-speed mode (160 units per frame)
      if (pCar->iStunned) {
        dHighSpeedCrashPull = viewdata[chaseview[iViewIdx]].fChasePullCrash + -400.0;// High-speed crash: use crash pull distance minus 400 units
        //_CHP();
        iTargetPullHighSpeedCrash = (int)dHighSpeedCrashPull;
        fTargetPullHighSpeedCrash = (float)dHighSpeedCrashPull;
        if (PULLZ[iViewIdx] > (double)fTargetPullHighSpeedCrash) {
          dTempHighSpeedCrash = PULLZ[iViewIdx] - (double)iHighSpeedPullIncrement;
          PULLZ[iViewIdx] = (float)dTempHighSpeedCrash;
          if (dTempHighSpeedCrash < fTargetPullHighSpeedCrash)
            PULLZ[iViewIdx] = fTargetPullHighSpeedCrash;
        }
        fTargetPullHighSpeedCrashLimit = (float)iTargetPullHighSpeedCrash;
        iViewIndex4 = iViewIdx;
        if (PULLZ[iViewIndex4] < (double)fTargetPullHighSpeedCrashLimit) {
          dTempHighSpeedCrash2 = (double)(40 * iFrameDelta) + PULLZ[iViewIndex4];
          PULLZ[iViewIndex4] = (float)dTempHighSpeedCrash2;
          if (dTempHighSpeedCrash2 > fTargetPullHighSpeedCrashLimit)
            PULLZ[iViewIndex4] = fTargetPullHighSpeedCrashLimit;
        }
      } else {
        dHighSpeedNormalPull = viewdata[chaseview[iViewIdx]].fChasePullNormal + 400.0;// High-speed normal: use normal pull distance plus 400 units
        //_CHP();
        iTargetPullHighSpeed = (int)dHighSpeedNormalPull;
        fTargetPullHighSpeed = (float)dHighSpeedNormalPull;
        if (PULLZ[iViewIdx] > (double)fTargetPullHighSpeed) {
          dTempHighSpeedNormal = PULLZ[iViewIdx] - (double)iHighSpeedPullIncrement;
          PULLZ[iViewIdx] = (float)dTempHighSpeedNormal;
          if (dTempHighSpeedNormal < fTargetPullHighSpeed)
            PULLZ[iViewIdx] = fTargetPullHighSpeed;
        }
        fTargetPullHighSpeedLimit = (float)iTargetPullHighSpeed;
        iViewIndex5 = iViewIdx;
        if (PULLZ[iViewIndex5] < (double)fTargetPullHighSpeedLimit) {
          dTempHighSpeedNormal2 = (double)(40 * iFrameDelta) + PULLZ[iViewIndex5];
          PULLZ[iViewIndex5] = (float)dTempHighSpeedNormal2;
          if (dTempHighSpeedNormal2 > fTargetPullHighSpeedLimit)
            PULLZ[iViewIndex5] = fTargetPullHighSpeedLimit;
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00038330
void changemateto(int iCarIndex, int iNewStrategy)
{
  int iMateCarIndex; // esi
  int iRandomValue; // eax
  int iSpeechParam; // ecx
  int iRandomSpeech; // eax
  int iCooldownCarIndex; // esi

  if ((iCarIndex & 1) != 0)
    iMateCarIndex = iCarIndex - 1;
  else
    iMateCarIndex = iCarIndex + 1;
  if ((char)Car[iMateCarIndex].byLives > 0 && !human_control[iMateCarIndex])// Check if mate car is alive and AI-controlled
  {                                             // Check if mate already has the requested strategy
    if (iNewStrategy == Car[iMateCarIndex].iSelectedStrategy) {                                           // Play acknowledgment speech when mate already has requested strategy
      // CHEAT_MODE_CLONES
      if (player_type != 2 && iCarIndex == player1_car && (cheat_mode & CHEAT_MODE_CLONES) == 0)
        speechsample(iNewStrategy + 57, 20000, 18, iCarIndex);
    } else {
      iRandomValue = ROLLERrandRaw();                    // Random chance (1/4) or cooldown check - mate may refuse strategy change
      if (GetHighOrderRand(4, iRandomValue) == 3 || Car[iMateCarIndex].nChangeMateCooldown) {
        // CHEAT_MODE_CLONES
        if (player_type == 2 || iCarIndex != player1_car || (cheat_mode & CHEAT_MODE_CLONES) != 0) {
          ROLLERrandRaw();
        } else {
          iSpeechParam = player1_car + ((Car[iMateCarIndex].iSelectedStrategy + 61) << 8);// Play refusal speech sequence when mate rejects strategy change
          iRandomSpeech = ROLLERrandRaw();
          speechsample(GetHighOrderRand(2, iRandomSpeech) + 65, 20000, 18, iSpeechParam);
          speechsample(Car[iMateCarIndex].iSelectedStrategy + 61, 20000, 0, player1_car);
        }
        iCooldownCarIndex = iMateCarIndex;
        if (!Car[iCooldownCarIndex].nChangeMateCooldown)
          Car[iCooldownCarIndex].nChangeMateCooldown = 1080;// Set strategy change cooldown (1080 ticks)
      } else {
        Car[iMateCarIndex].iSelectedStrategy = iNewStrategy;// Accept strategy change and play confirmation speech
        // CHEAT_MODE_CLONES
        if (player_type != 2 && iCarIndex == player1_car && (cheat_mode & CHEAT_MODE_CLONES) == 0)
          speechsample(iNewStrategy + 57, 20000, 18, iCarIndex);
        Car[iMateCarIndex].nChangeMateCooldown = 1080;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000384C0
// valid when car movement is not blocked by i.e. fence separating track from run-off area
// top-down view, driving up (= start is down, end is up):
/******************************************************************
 *      region=2       f      region=1     f       region=0       *
 *                     e                   e                      *
 *  left-run-off-area  n       track       n  right-run-off-area  *
 *                     c                   c                      *
 *                     e                   e                      *
 ***************  -halfWidth     0     halfWidth  ****************/
bool linevalid(int iChunkIdx, float fStartCoord, float fEndCoord)
{
  tData *pData; // ecx
  tTrackInfo *pTrackInfo; // edx
  unsigned int uiStartRegion; // esi
  unsigned int uiEndRegion; // ebx
  bool iValidResult; // ecx
  int iRightSurfaceType; // edi
  bool iRightBarierPresent; // ebp
  int iLeftSurfaceType; // edi
  bool iLeftBarierPresent; // edi
  unsigned int iNotRendered; // eax

  pData = &localdata[iChunkIdx];
  pTrackInfo = &TrackInfo[iChunkIdx];

  // Classify start coordinate relative to track boundaries
  if (fStartCoord <= (double)pData->fTrackHalfWidth) {
    if (-pData->fTrackHalfWidth <= fStartCoord)
      uiStartRegion = 1;                        // inside track boundaries
    else
      uiStartRegion = 2;                        // outside track (negative side)
  } else {
    uiStartRegion = 0;                          // outside track (positive side)
  }
  if (fEndCoord <= (double)pData->fTrackHalfWidth) {
    if (-pData->fTrackHalfWidth <= fEndCoord)
      uiEndRegion = 1;                          // inside track boundaries
    else
      uiEndRegion = 2;                          // outside track (negative side)
  } else {
    uiEndRegion = 0;                            // outside track (positive side)
  }

  // If both points are in the same region, line doesn't cross track boundaries
  iValidResult = true;
  if (uiStartRegion == uiEndRegion)
    return iValidResult;

  // Check surface types that block visibility
  iRightSurfaceType = pTrackInfo->iLeftSurfaceType;
  if (iRightSurfaceType == 5 || iRightSurfaceType == 6 || iRightSurfaceType == 9)
    iRightBarierPresent = true;                   // surface blocks visibility
  else
    iRightBarierPresent = false;

  iLeftSurfaceType = pTrackInfo->iRightSurfaceType;
  if (iLeftSurfaceType == 5 || iLeftSurfaceType == 6 || iLeftSurfaceType == 9)
    iLeftBarierPresent = true;                  // surface blocks visibility
  else
    iLeftBarierPresent = false;

  // Check if invisible
  iNotRendered = abs(TrakColour[iChunkIdx][TRAK_COLOUR_CENTER]) & SURFACE_FLAG_SKIP_RENDER;

  // Determine line validity based on region crossing and surface blocking
  if (!uiStartRegion)                         // start outside positive side
  {
    if (!uiEndRegion)                         // both outside positive (shouldn't happen)
      return iValidResult;
    if (uiEndRegion > 1)                      // end outside negative side
    {
      // Line crosses entire track, blocked if any barriers present and track rendered
      if (!iRightBarierPresent && !iLeftBarierPresent && !iNotRendered)
        return iValidResult;                  // valid if no barriers and rendered
      return 0;
    }
    goto CHECK_LEFT_SURFACE;                    // Start outside positive, end inside track
  }

  if (uiStartRegion <= 1)                     // start inside track
  {
    if (uiEndRegion) {
      if (uiEndRegion != 2 || !iLeftBarierPresent)
        return iValidResult;                  // valid if crossing to unblocked left surface
      return false;
    }
  CHECK_LEFT_SURFACE:
      // start on track, end outside positive - check right surface
    if (iRightBarierPresent)
      return false;                                 // valid if right surface missing
    return iValidResult;
  }

  // Start=2 = outside negative side
  if (!uiEndRegion)                           // end outside positive
  {
    // Line crosses entire track, blocked if no barriers and not rendered
    if (!iRightBarierPresent && !iLeftBarierPresent && !iNotRendered)
      return iValidResult;                    // valid if no barriers and rendered
    return false;
  }

  if (uiEndRegion != 1)                       // end not inside track
    return iValidResult;

  // Start outside negative, end inside - check left surface
  if (iLeftBarierPresent)
    return false;                                   // valid if left surface missing
  return iValidResult;
}

//-------------------------------------------------------------------------------------------------
//00038640
void analysefalloff(tCar *pCar)
{
  int iCurrChunk; // ecx
  double dCarY; // st7
  tData *pData; // esi
  tTrackInfo *pInfo; // edi
  unsigned int iLaneType; // ecx
  //int64 v7; // rax
  //int64 v8; // rax
  double dWallPosition; // st7
  int iWallFlag; // edx
  int nActualYaw; // ebp
  double dSinVelocity; // st7
  double dRollMomentum; // st7
  //int64 v14; // rax
  //int64 v15; // rax
  double dWallPosition2; // st7
  int iWallFlag2; // edx
  int iYawTemp; // edx
  double dSinVelocity2; // st7
  double dRollMomentum2; // st7
  int iPrevChunk; // eax
  //int64 v22; // rax
  int iYawTemp2; // edx
  double dCosVelocity; // st7
  double dRollMomentum3; // st7
  //int64 v26; // rax
  int iYawTemp3; // edx
  double dCosVelocity2; // st7
  double dRollMomentum4; // st7
  float fCosVelocity; // [esp+0h] [ebp-34h]
  float fSinVelocity; // [esp+0h] [ebp-34h]
  float fSinVelocity2; // [esp+0h] [ebp-34h]
  float fSinVelocityTemp; // [esp+4h] [ebp-30h]
  float fCosVelocityTemp; // [esp+8h] [ebp-2Ch]
  float fCosVelocityTemp2; // [esp+Ch] [ebp-28h]
  float fSinVelocityTemp2; // [esp+10h] [ebp-24h]
  float fCosVelocityTemp3; // [esp+14h] [ebp-20h]
  float fTrackHalfWidth; // [esp+18h] [ebp-1Ch]
  float fWallPosition; // [esp+18h] [ebp-1Ch]

  iCurrChunk = pCar->nCurrChunk;
  dCarY = pCar->pos.fY;
  pData = &localdata[iCurrChunk];
  pCar->iRollMomentum = 0;
  pInfo = &TrackInfo[iCurrChunk];
  if (dCarY <= pData->fTrackHalfWidth)        // Determine which lane/area the car is in: 0=left shoulder, 1=on track, 2=right shoulder
  {
    if (-pData->fTrackHalfWidth <= pCar->pos.fY)
      iLaneType = 1;
    else
      iLaneType = 2;
  } else {
    iLaneType = 0;
  }
  if (!iLaneType)                             // Handle left shoulder collision - set wall position and check for wall collision
  {
    dWallPosition = pData->fTrackHalfWidth + pInfo->fLShoulderWidth;
    iWallFlag = -1;
    goto LABEL_11;
  }
  if (iLaneType > 1) {
    //v8 = TrakColour[pCar->nCurrChunk][1];
    dWallPosition = -pData->fTrackHalfWidth;
    iWallFlag = TrakColour[pCar->nCurrChunk][TRAK_COLOUR_CENTER] & SURFACE_FLAG_SKIP_RENDER;// ((HIDWORD(v8) ^ v8) - HIDWORD(v8)) & 0x20000;
  LABEL_11:
    fTrackHalfWidth = (float)dWallPosition;
    goto LABEL_12;
  }
  //v7 = TrakColour[pCar->nCurrChunk][0];
  iWallFlag = TrakColour[pCar->nCurrChunk][TRAK_COLOUR_LEFT_LANE] & SURFACE_FLAG_SKIP_RENDER; //((HIDWORD(v7) ^ v7) - HIDWORD(v7)) & 0x20000;
  fTrackHalfWidth = pData->fTrackHalfWidth;
LABEL_12:
  if (iWallFlag && fTrackHalfWidth < pCar->fCarHalfWidth * 0.5 + pCar->pos.fY)// Check if car is hitting the wall - if wall exists and car position exceeds wall boundary
  {
    nActualYaw = pCar->nActualYaw;
    dSinVelocity = pCar->fFinalSpeed * tsin[nActualYaw];// Calculate lateral velocity component (sin component) for wall collision handling
    if (dSinVelocity < 30.0) {
      fSinVelocityTemp2 = (float)dSinVelocity;
      if (fSinVelocityTemp2 >= 0.0) {
        fCosVelocityTemp3 = pCar->fFinalSpeed * tcos[nActualYaw];
        pCar->nActualYaw = getangle(fCosVelocityTemp3, 40.0);// Adjust car yaw and speed when bouncing off left wall - redirect momentum
        pCar->fFinalSpeed = (float)sqrt(fCosVelocityTemp3 * fCosVelocityTemp3 + 1600.0);
      }
    }
    dRollMomentum = tcos[pCar->nYaw] * 200.0 + (double)pCar->iRollMomentum;// Apply roll momentum based on car orientation - banking effect on slopes
    //_CHP();
    pCar->iRollMomentum = (int)dRollMomentum;
  }
  if (iLaneType) {
    if (iLaneType <= 1) {
      //v15 = TrakColour[pCar->nCurrChunk][2];
      dWallPosition2 = -pData->fTrackHalfWidth;
      iWallFlag2 = TrakColour[pCar->nCurrChunk][TRAK_COLOUR_RIGHT_LANE] & SURFACE_FLAG_SKIP_RENDER;//((HIDWORD(v15) ^ v15) - HIDWORD(v15)) & 0x20000;
    } else {
      dWallPosition2 = -pData->fTrackHalfWidth - pInfo->fRShoulderWidth;
      iWallFlag2 = -1;
    }
    fWallPosition = (float)dWallPosition2;
  } else {
    //v14 = TrakColour[pCar->nCurrChunk][1];
    iWallFlag2 = TrakColour[pCar->nCurrChunk][TRAK_COLOUR_CENTER] & SURFACE_FLAG_SKIP_RENDER; //((HIDWORD(v14) ^ v14) - HIDWORD(v14)) & 0x20000;
    fWallPosition = pData->fTrackHalfWidth;
  }
  if (iWallFlag2 && fWallPosition > pCar->pos.fY - pCar->fCarHalfWidth * 0.5)// Check for right wall collision - mirror of left wall logic
  {
    iYawTemp = pCar->nActualYaw;
    dSinVelocity2 = pCar->fFinalSpeed * tsin[iYawTemp];
    if (dSinVelocity2 > -30.0) {
      fSinVelocityTemp = (float)dSinVelocity2;
      if (fSinVelocityTemp <= 0.0) {
        fCosVelocity = pCar->fFinalSpeed * tcos[iYawTemp];
        pCar->nActualYaw = getangle(fCosVelocity, -40.0);// Adjust car yaw and speed when bouncing off right wall
        pCar->fFinalSpeed = (float)sqrt(fCosVelocity * fCosVelocity + 1600.0);
      }
    }
    dRollMomentum2 = (double)pCar->iRollMomentum - tcos[pCar->nYaw] * 200.0;
    //_CHP();
    pCar->iRollMomentum = (int)dRollMomentum2;
  }
  iPrevChunk = pCar->nCurrChunk - 1;            // Check previous track chunk for front wall collision
  if (iPrevChunk < 0)
    iPrevChunk = TRAK_LEN - 1;
  //v22 = TrakColour[iPrevChunk][iLaneType];
  //if ((((HIDWORD(v22) ^ (unsigned int)v22) - HIDWORD(v22)) & 0x20000) != 0 && -pData->fTrackHalfLength > pCar->pos.fX - pCar->fCarWidthBankingProjection * 0.5) {
  if ((TrakColour[iPrevChunk][iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0 && -pData->fTrackHalfLength > pCar->pos.fX - pCar->fCarWidthBankingProjection * 0.5) {
    iYawTemp2 = pCar->nActualYaw;
    dCosVelocity = pCar->fFinalSpeed * tcos[iYawTemp2];
    if (dCosVelocity > -30.0) {
      fCosVelocityTemp2 = (float)dCosVelocity;
      if (fCosVelocityTemp2 <= 0.0) {
        fSinVelocity = pCar->fFinalSpeed * tsin[iYawTemp2];
        pCar->nActualYaw = getangle(-40.0, fSinVelocity);// Handle front wall collision - adjust yaw by redirecting forward momentum
        pCar->fFinalSpeed = (float)sqrt(fSinVelocity * fSinVelocity + 1600.0);
      }
    }
    dRollMomentum3 = tsin[pCar->nYaw] * 200.0 + (double)pCar->iRollMomentum;
    //_CHP();
    pCar->iRollMomentum = (int)dRollMomentum3;
  }
  //v26 = TrakColour[pCar->nCurrChunk + 1][iLaneType];// Check next track chunk for rear wall collision
  //if ((((HIDWORD(v26) ^ (unsigned int)v26) - HIDWORD(v26)) & 0x20000) != 0 && pData->fTrackHalfLength < pCar->fCarWidthBankingProjection * 0.5 + pCar->pos.fX) {
  if ((TrakColour[pCar->nCurrChunk + 1][iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0 && pData->fTrackHalfLength < pCar->fCarWidthBankingProjection * 0.5 + pCar->pos.fX) {
    iYawTemp3 = pCar->nActualYaw;
    dCosVelocity2 = pCar->fFinalSpeed * tcos[iYawTemp3];
    if (dCosVelocity2 < 30.0) {
      fCosVelocityTemp = (float)dCosVelocity2;
      if (fCosVelocityTemp >= 0.0) {
        fSinVelocity2 = pCar->fFinalSpeed * tsin[iYawTemp3];
        pCar->nActualYaw = getangle(40.0, fSinVelocity2);// Handle rear wall collision - adjust yaw by redirecting backward momentum
        pCar->fFinalSpeed = (float)sqrt(fSinVelocity2 * fSinVelocity2 + 1600.0);
      }
    }
    dRollMomentum4 = (double)pCar->iRollMomentum - tsin[pCar->nYaw] * 200.0;
    //_CHP();
    pCar->iRollMomentum = (int)dRollMomentum4;
  }
}

//-------------------------------------------------------------------------------------------------
//00038A80
void showmap(uint8 *pScrPtr, int iCarIdx)
{
  int iChunk2; // edx
  int iNextChunk; // eax
  tData *pCurrChunkData; // edx
  tData *pNextChunkData; // ebx
  tData *pCarChunkData; // eax
  double dInvScale; // st5
  double dRelX; // st7
  double dRelY; // st6
  double dRotatedY_1; // st7
  int iCalcResult; // eax
  int iScreenX2; // eax
  int iScreenY; // ebx
  int iMapSect; // edi
  tData *pSectionData; // eax
  double dSectionInvScale; // st5
  double dSectionRelX; // st7
  double dSectionRelY; // st6
  //int iAngleIndex; // eax
  double dSectionRotatedY; // st7
  int iSectionCalc; // eax
  int iNextScreenX; // edx
  int iCurrentScreenX; // edx
  int iMapSectLoop; // edi
  int iCurrentScreenY; // ebx
  tData *pLoopSectionData; // eax
  double dLoopRelX; // st7
  double dLoopRelY; // st6
  //int iLoopAngleIndex; // eax
  double dLoopRotatedY; // st7
  int iLoopCalc; // eax
  int iLastSection; // eax
  int iWinWidth; // esi
  int iMarkerY; // edx
  int iCurrentX; // eax
  int i; // ebx
  tCar *pCarArray; // eax
  tCar *pCurrentCar; // edi
  int iCurrChunk; // edx
  float *pChunkTransform; // edx
  double dCarInvScale; // st5
  double dTransformedX; // st7
  double dTransformedY; // st6
  double dRotatedX; // st5
  double dRotatedY; // st7
  int iScreenSize; // ecx
  int iCarScreenCalc; // edx
  int iDriverIdx; // edx
  int iColorIndex; // edx
  uint8 *pCarPixel; // ebx
  int iCarColor; // edx
  uint8 *pPlayerPixel; // eax
  float fDeltaX; // [esp+0h] [ebp-9Ch]
  float fDeltaY; // [esp+4h] [ebp-98h]
  int iViewAngle; // [esp+1Ch] [ebp-80h]
  int iX1; // [esp+20h] [ebp-7Ch]
  int iOrigScreenY; // [esp+24h] [ebp-78h]
  int iPlayerScreenY; // [esp+34h] [ebp-68h]
  int iPlayerScreenX; // [esp+3Ch] [ebp-60h]
  int iPlayerColor; // [esp+44h] [ebp-58h]
  float fInvScale; // [esp+48h] [ebp-54h]
  float fMapScale; // [esp+4Ch] [ebp-50h]
  float fX; // [esp+50h] [ebp-4Ch]
  float fY; // [esp+54h] [ebp-48h]
  int iPixelLoop; // [esp+58h] [ebp-44h]
  int iCarLoop; // [esp+5Ch] [ebp-40h]
  int iSectionScreenXTemp; // [esp+6Ch] [ebp-30h]
  int iSectionScreenY; // [esp+70h] [ebp-2Ch]
  int iSectionScreenX; // [esp+74h] [ebp-28h]
  int iY1; // [esp+78h] [ebp-24h]
  tData *pData; // [esp+7Ch] [ebp-20h]
  int iCarScreenY; // [esp+80h] [ebp-1Ch]
  int iCarScreenX; // [esp+84h] [ebp-18h]

  fMapScale = cur_mapsize;                      // Set base map scale
  if ((cheat_mode & CHEAT_MODE_DOUBLE_TRACK) != 0)             // Double scale if cheat mode bit 0x1000 is set
    fMapScale = cur_mapsize * 2.0f;
  iChunk2 = Car[iCarIdx].nReferenceChunk;               // Calculate view angle based on car direction
  iNextChunk = iChunk2 + 1;
  if (iChunk2 + 1 == TRAK_LEN)
    iNextChunk ^= TRAK_LEN;
  pCurrChunkData = &localdata[iChunk2];
  pNextChunkData = &localdata[iNextChunk];
  fDeltaY = pCurrChunkData->pointAy[3].fY - pNextChunkData->pointAy[3].fY;
  fDeltaX = pCurrChunkData->pointAy[3].fX - pNextChunkData->pointAy[3].fX;
  iViewAngle = (4096 - (uint16)getangle(fDeltaX, fDeltaY)) & 0x3FFF;
  pCarChunkData = &localdata[Car[iCarIdx].nReferenceChunk];
  dInvScale = 1.0 / fMapScale;                  // Setup coordinate transformation for map display
  dRelX = (pCarChunkData->pointAy[3].fX - localdata[0].pointAy[3].fX) * dInvScale;
  dRelY = (pCarChunkData->pointAy[3].fY - localdata[0].pointAy[3].fY) * dInvScale;
  pData = pCarChunkData;
  dRotatedX = dRelX * tcos[iViewAngle] - dRelY * tsin[iViewAngle]; //line skipped decompiler artifact
  //_CHP();
  dRotatedY_1 = dRelX * tsin[iViewAngle] + dRelY * tcos[iViewAngle];
  //_CHP();
  iCalcResult = scr_size * ((int)dRotatedX + 38); //line skipped decompiler artifact
  iScreenX2 = iCalcResult >> 6;
  iX1 = iScreenX2;
  iScreenY = (scr_size * (160 - (int)dRotatedY_1)) >> 6;
  iMapSect = cur_mapsect;

  iOrigScreenY = iScreenY;
  while (1) {
    iNextScreenX = iScreenX2 + 1;               // Draw track centerline segments
    if (iMapSect >= TRAK_LEN)
      break;
    pSectionData = &localdata[iMapSect];
    dSectionInvScale = 1.0 / fMapScale;
    dSectionRelX = (pData->pointAy[3].fX - pSectionData->pointAy[3].fX) * dSectionInvScale;
    dSectionRelY = (pData->pointAy[3].fY - pSectionData->pointAy[3].fY) * dSectionInvScale;
    dRotatedX = dSectionRelX * tcos[iViewAngle] - dSectionRelY * tsin[iViewAngle]; //line skipped decompiler artifact
    //_CHP();
    dSectionRotatedY = dSectionRelX * tsin[iViewAngle] + dSectionRelY * tcos[iViewAngle];
    //_CHP();
    iSectionCalc = scr_size * ((int)dRotatedX + 38); //line skipped decompiler artifact
    iSectionScreenXTemp = iSectionCalc >> 6;
    iSectionScreenY = (scr_size * (160 - (int)dSectionRotatedY)) >> 6;
    compout(pScrPtr, iNextScreenX, iScreenY, (iSectionCalc >> 6) + 1, iSectionScreenY, 0x70u);
    iScreenX2 = iSectionScreenXTemp;
    iScreenY = iSectionScreenY;
    iMapSect += cur_mapsect;
  }
  compout(pScrPtr, iNextScreenX, iScreenY, iX1 + 1, iOrigScreenY, 0x70u);
  iCurrentScreenX = iX1;
  iMapSectLoop = cur_mapsect;
  iCurrentScreenY = iOrigScreenY;
  if (cur_mapsect < TRAK_LEN)                 // Draw track edges with different colors
  {
    fInvScale = 1.0f / fMapScale;
    do {
      pLoopSectionData = &localdata[iMapSectLoop];
      dLoopRelX = (pData->pointAy[3].fX - pLoopSectionData->pointAy[3].fX) * fInvScale;
      dLoopRelY = (pData->pointAy[3].fY - pLoopSectionData->pointAy[3].fY) * fInvScale;
      dRotatedX = dLoopRelX * tcos[iViewAngle] - dLoopRelY * tsin[iViewAngle]; //line skipped decompiler artifact
      //_CHP();
      dLoopRotatedY = dLoopRelX * tsin[iViewAngle] + dLoopRelY * tcos[iViewAngle];
      //_CHP();
      iLoopCalc = scr_size * ((int)dRotatedX + 38); //line skipped decompiler artifact
      iSectionScreenX = iLoopCalc >> 6;
      iY1 = (scr_size * (160 - (int)dLoopRotatedY)) >> 6;
      if ((TrakColour[iMapSectLoop][0] & SURFACE_FLAG_PIT_ZONE) != 0 || (TrakColour[iMapSectLoop][1] & SURFACE_FLAG_PIT_ZONE) != 0 || (TrakColour[iMapSectLoop][2] & SURFACE_FLAG_PIT_ZONE) != 0)
        compout(pScrPtr, iCurrentScreenX, iCurrentScreenY, iSectionScreenX, iY1, 0xCDu);
      else
        compout(pScrPtr, iCurrentScreenX, iCurrentScreenY, iSectionScreenX, iY1, 0x83u);
      iCurrentScreenX = iSectionScreenX;
      iMapSectLoop += cur_mapsect;
      iCurrentScreenY = iY1;
    } while (iMapSectLoop < TRAK_LEN);
  }
  iLastSection = TRAK_LEN - 1;
  if ((TrakColour[iLastSection][0] & SURFACE_FLAG_PIT_ZONE) != 0 || (TrakColour[iLastSection][1] & SURFACE_FLAG_PIT_ZONE) != 0 || (TrakColour[iLastSection][2] & SURFACE_FLAG_PIT_ZONE) != 0)
    compout(pScrPtr, iCurrentScreenX, iCurrentScreenY, iX1, iOrigScreenY, 0xCDu);
  else
    compout(pScrPtr, iCurrentScreenX, iCurrentScreenY, iX1, iOrigScreenY, 0x83u);
  iPixelLoop = 0;                               // Draw 3x3 pixel marker at player position
  iWinWidth = winw;
  iMarkerY = iOrigScreenY - 1;
  do {
    iCurrentX = iX1 - 1;
    for (i = 0; i < 3; ++i) {
      if (iCurrentX >= 0 && iCurrentX < iWinWidth && iMarkerY >= 0 && iMarkerY < winh)
        pScrPtr[iCurrentX + iWinWidth * iMarkerY] = -117;
      ++iCurrentX;
    }
    ++iMarkerY;
    ++iPixelLoop;
  } while (iPixelLoop < 3);
  iCarLoop = 0;
  if (numcars > 0)                            // Draw all cars on the minimap
  {
    pCarArray = Car;
    do {
      pCurrentCar = pCarArray;
      if ((char)pCarArray->byLives > 0) {
        iCurrChunk = pCarArray->nCurrChunk;     // Calculate car position on track
        if (iCurrChunk == -1) {
          fX = pCarArray->pos.fX;
          fY = pCarArray->pos.fY;
        } else {
          pChunkTransform = (float *)&localdata[iCurrChunk];
          fX = *pChunkTransform * pCarArray->pos.fX - pChunkTransform[9];
          fY = pChunkTransform[3] * pCarArray->pos.fX - pChunkTransform[10];
        }
        dCarInvScale = 1.0 / fMapScale;         // Transform car position to screen coordinates
        dTransformedX = (fX + pData->pointAy[3].fX) * dCarInvScale;
        dTransformedY = (fY + pData->pointAy[3].fY) * dCarInvScale;
        dRotatedX = dTransformedX * tcos[iViewAngle] - dTransformedY * tsin[iViewAngle];
        //_CHP();
        dRotatedY = dTransformedX * tsin[iViewAngle] + dTransformedY * tcos[iViewAngle];
        iScreenSize = scr_size;
        iCarScreenCalc = scr_size * ((int)dRotatedX + 38);
        //_CHP();
        iCarScreenX = iCarScreenCalc >> 6;
        iCarScreenY = (iScreenSize * (160 - (int)dRotatedY)) >> 6;
        iDriverIdx = pCurrentCar->iDriverIdx;
        if (iDriverIdx == iCarIdx)            // Special handling for player car (larger marker)
        {
          //iColorIndex = iCarIdx ^ iDriverIdx;
          //LOBYTE(iColorIndex) = pCurrentCar->byCarDesignIdx;
          iColorIndex = pCurrentCar->byCarDesignIdx;
          iPlayerColor = team_col[iColorIndex];
          iPlayerScreenX = iCarScreenX;
          iPlayerScreenY = (iScreenSize * (160 - (int)dRotatedY)) >> 6;
        } else if (iCarScreenX > 0 && iWinWidth - 1 > iCarScreenX && iCarScreenY > 0 && winh - 1 > iCarScreenY)// Draw car marker (cross pattern)
        {
          pCarPixel = &pScrPtr[iCarScreenX + iWinWidth * iCarScreenY];
          iCarColor = team_col[pCurrentCar->byCarDesignIdx];
          if (pCurrentCar->byRacePosition || (frames & 8) != 0) {
            *(pCarPixel - 1) = iCarColor;
            *pCarPixel = iCarColor;
            pCarPixel[1] = iCarColor;
            pCarPixel[-iWinWidth] = iCarColor;
            pCarPixel[iWinWidth] = iCarColor;
          }
        }
      }
      ++pCarArray;
      ++iCarLoop;
    } while (iCarLoop < numcars);
  }
  if ((char)Car[iCarIdx].byLives > 0 && (Car[iCarIdx].byRacePosition || (frames & 8) != 0)) {
    pPlayerPixel = &pScrPtr[iPlayerScreenX + iWinWidth * iPlayerScreenY];
    *(pPlayerPixel - 1) = iPlayerColor;
    *(pPlayerPixel - 2) = iPlayerColor;
    *pPlayerPixel = iPlayerColor;
    pPlayerPixel[1] = iPlayerColor;
    pPlayerPixel[2] = iPlayerColor;
    pPlayerPixel[-iWinWidth] = iPlayerColor;
    pPlayerPixel[-2 * iWinWidth] = iPlayerColor;
    pPlayerPixel[iWinWidth] = iPlayerColor;
    pPlayerPixel[2 * iWinWidth] = iPlayerColor;
  }
  winw = iWinWidth;
}

//-------------------------------------------------------------------------------------------------
//000390D0
void firework_display()
{
  int i; // ebp
  tCarSpray *pSprayData; // ebx
  uint8 byType; // al
  int iRandTimer; // eax
  int iTimerCount; // esi
  int iWidthDiv3; // edx
  int iRandX; // eax
  int iRandVelX; // eax
  int iRandLifetime; // eax
  int iLifetimeCount; // eax
  int iRandColor; // eax
  int iParticleCount; // ecx
  int iFireworkColor; // esi
  int iRandAngle; // eax
  int iAngleIndex; // edx
  double dRandVelocity; // st7
  int iRandParticleLife; // eax
  double dVelocityY; // st6
  double dVelocityX; // st7
  int iActiveParticles; // edx
  int j; // eax
  int iLifeTime; // ecx
  double dParticleVelY; // st6
  double dParticleVelX; // st7
  tCarSpray *pCleanupSpray; // eax
  float fOrigVelY; // [esp+10h] [ebp-28h]
  float fY; // [esp+14h] [ebp-24h]
  float fX; // [esp+18h] [ebp-20h]
  float fOrigVelX; // [esp+1Ch] [ebp-1Ch]

  for (updates = 0; readptr != writeptr; readptr = ((uint16)readptr + 1) & 0x1FF) {                                             // Only advance animation if not paused
    if (!paused)
      ++updates;
    dozoomstuff(0);
    if (readptr >= 0) {                                           // Process all 18 firework spray slots
      for (i = 0; i != 18; ++i) {
        pSprayData = CarSpray[i];
        byType = CarSpray[i][0].iType;
        if (byType) {                                       // Handle ascending rocket (type 1)
          if (byType <= 1u) {
            iLifetimeCount = pSprayData->iLifeTime - 1;
            pSprayData->iLifeTime = iLifetimeCount;
            if (iLifetimeCount > 0) {
              memcpy(&CarSpray[i][4], &CarSpray[i][3], sizeof(CarSpray[i][4]));
              memcpy(&CarSpray[i][3], &CarSpray[i][2], sizeof(CarSpray[i][3]));
              memcpy(&CarSpray[i][2], &CarSpray[i][1], sizeof(CarSpray[i][2]));
              memcpy(&CarSpray[i][1], CarSpray[i], sizeof(CarSpray[i][1]));
              dVelocityY = pSprayData->velocity.fY + 0.1;// Apply gravity and update rocket position
              pSprayData->velocity.fY = (float)dVelocityY;
              pSprayData->position.fY = (float)dVelocityY + pSprayData->position.fY;
              dVelocityX = pSprayData->velocity.fX + pSprayData->position.fX;
              pSprayData->iColor = 0x8F;
              pSprayData->position.fX = (float)dVelocityX;
            } else {
              sfxsample(SOUND_SAMPLE_EXPLO, 0x8000);             // Rocket reached peak - create explosion particles
              fX = pSprayData->position.fX;
              fY = pSprayData->position.fY;
              fOrigVelX = pSprayData->velocity.fX;
              fOrigVelY = pSprayData->velocity.fY;
              iRandColor = ROLLERrandRaw();
              iParticleCount = 0;
              iFireworkColor = firework_colours[GetHighOrderRand(12, iRandColor)];
              do {
                pSprayData->iType = 2;
                //LOBYTE(pSprayData->iType) = 2;

                pSprayData->iColor = iFireworkColor;
                pSprayData->position.fX = fX;
                pSprayData->position.fY = fY;
                iRandAngle = ROLLERrandRaw();

                iAngleIndex = GetHighOrderRand(32768, iRandAngle);
                //iAngleIndex = ((iRandAngle << 14) - (__CFSHL__(iRandAngle << 14 >> 31, 15) + (iRandAngle << 14 >> 31 << 15))) >> 15;
                
                dRandVelocity = (double)ROLLERrandRaw() * 2.0 * 0.000030517578125;
                pSprayData->velocity.fX = (float)dRandVelocity * tcos[iAngleIndex] + fOrigVelX;
                pSprayData->velocity.fY = (float)dRandVelocity * tsin[iAngleIndex] + fOrigVelY;
                iRandParticleLife = ROLLERrandRaw();
                ++pSprayData;
                ++iParticleCount;
                pSprayData[-1].iLifeTime = GetHighOrderRand(36, iRandParticleLife) + 36;
              } while (iParticleCount < 32);
            }
          } else if (byType == 2)                // Handle explosion particles (type 2)
          {
            iActiveParticles = 0;
            for (j = 0; j < 32; ++j) {
              iLifeTime = pSprayData->iLifeTime;
              if (iLifeTime > 0) {
                dParticleVelY = pSprayData->velocity.fY + 0.1;// Apply gravity to particle and update position
                pSprayData->velocity.fY = (float)dParticleVelY;
                pSprayData->position.fY = (float)dParticleVelY + pSprayData->position.fY;
                dParticleVelX = pSprayData->velocity.fX + pSprayData->position.fX;
                pSprayData->iLifeTime = iLifeTime - 1;
                ++iActiveParticles;
                pSprayData->position.fX = (float)dParticleVelX;
              }
              ++pSprayData;
            }
            if (!iActiveParticles) {
              pCleanupSpray = CarSpray[i];      // All particles expired - reset spray slot
              do {
                pCleanupSpray += 4;

                pCleanupSpray[-4].iType = 0;
                //LOBYTE(pCleanupSpray[-4].iType) = 0;

                pCleanupSpray[-4].iTimer = 0;
                pCleanupSpray[-4].position.fZ = 0.0;
                pCleanupSpray[-4].iColor = 0x8F;

                pCleanupSpray[-3].iType = 0;
                //LOBYTE(pCleanupSpray[-3].iType) = 0;

                pCleanupSpray[-3].iTimer = 0;
                pCleanupSpray[-3].position.fZ = 0.0;
                pCleanupSpray[-3].iColor = 0x8F;

                pCleanupSpray[-2].iType = 0;
                //LOBYTE(pCleanupSpray[-2].iType) = 0;

                pCleanupSpray[-2].iTimer = 0;
                pCleanupSpray[-2].position.fZ = 0.0;
                pCleanupSpray[-2].iColor = 0x8F;

                pCleanupSpray[-1].iType = 0;
                //LOBYTE(pCleanupSpray[-1].iType) = 0;

                pCleanupSpray[-1].iTimer = 0;
                pCleanupSpray[-1].position.fZ = 0.0;
                iActiveParticles += 4;
                pCleanupSpray[-1].iColor = 0x8F;
              } while (iActiveParticles < 32);
            }
          }
        //} else if ((LODWORD(pSprayData->position.fZ) & 0x7FFFFFFF) != 0) {
        } else if (fabs(pSprayData->position.fZ) != 0) {
          iTimerCount = pSprayData->iTimer - 1;
          pSprayData->iTimer = iTimerCount;
          if (iTimerCount < 0) {
            iWidthDiv3 = winw / 3;              // Launch new rocket with random position and velocity
            iRandX = ROLLERrandRaw();
            pSprayData->position.fX = (float)(2 * winw / 3 - GetHighOrderRand(iWidthDiv3, iRandX));
            pSprayData->position.fY = (float)winh;
            iRandVelX = ROLLERrandRaw();
            pSprayData->velocity.fX = (float)(2 - GetHighOrderRand(4, iRandVelX));
            pSprayData->velocity.fY = (float)(-4.8 - (double)ROLLERrandRaw() * 1.2 * 0.000030517578125);
            iRandLifetime = ROLLERrandRaw();
            pSprayData->iColor = 0x8F;

            pSprayData->iType = 1;
            //LOBYTE(pSprayData->iType) = 1;

            pSprayData->iLifeTime = GetHighOrderRand(48, iRandLifetime) + 18;
          }
        } else {
          pSprayData->position.fZ = -1.0;       // Initialize new firework with random launch delay
          iRandTimer = ROLLERrandRaw();
          pSprayData->iTimer = GetHighOrderRand(36, iRandTimer);
        }
      }
    }
    analysespeechsamples();
  }
}

//-------------------------------------------------------------------------------------------------
