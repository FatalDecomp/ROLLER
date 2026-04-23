#include "types.h"
#include "car.h"
#include "engines.h"
#include "control.h"
#include "3d.h"
#include "graphics.h"
#include "loadtrak.h"
#include "transfrm.h"
#include "drawtrk3.h"
#include "moving.h"
#include "func2.h"
#include "function.h"
#include "polytex.h"
#include "roller.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

const int car_c_reference_1[4] = { 0, 1, 1, 2 };

//-------------------------------------------------------------------------------------------------

int numcars = 16;               //000A6114
int team_col[16] =              //000A6118
{
  255, 207, 231, 171, 219, 195, 143, 243, 159, 159, 159, 159, 159, 112, 112, 112
};
char default_names[16][9] = {   //000A6158
  "SAL",
  "HAL",
  "SLAVE",
  "ZEN",
  "ASH",
  "BISHOP",
  "VOYAGER",
  "NOMAD",
  "BOB",
  "VINCENT",
  "EDDIE",
  "MARVIN",
  "KRYTEN",
  "HOLLY",
  "ROBBY",
  "GORT"
};
int16 ViewType[2] = { 0, 0 };   //000A620C
tVec3 carworld[4];              //0017C9E0
tVec3 carpoint[4];              //0017CA10
tVec3 carlocal[4];              //0017CA40
float roadheight[4];            //0017CA70
tCarPt CarPt[128];              //0017CA80
tCarZOrderEntry CarZOrder[MAX_TRACK_CHUNKS]; //0017DA80
tCarDrawOrder car_draw_order[16]; //0017F1F0
int car_texmap[16];             //0017F2B0
tCarBox CarBox;                 //0017F2F0
tCar Car[16];                   //0017F8F0
int car_persps[128];            //00180C30
tCarPt SmokePt[2][64];          //00180E30
int car_texs_loaded[16];        //00181E30
tCarSpray CarSpray[18][32];     //00181E70
char driver_names[16][9];       //00188170
tStoreEngine StoreEngines[14];  //00188200
tSLight SLight[2][3];           //00188350
int finished_car[16];           //00188470
int grid[16];                   //001884B0
tPolyParams CarPol;             //001884F0
int cars_drawn;                 //00188528
float CarBaseX;                 //0018851C
float CarBaseY;                 //00188520
int LoadCarTextures;            //0018852C
float CarDiag;                  //00188524

//-------------------------------------------------------------------------------------------------
//00052270
void InitCarStructs()
{
  int carDesignIndex; // ebx
  int v1; // esi
  int carDesignIndex2; // ecx
  int iNumGears; // edi
  float *pSpds; // ebx
  int j; // ecx
  double v6; // st7
  int iCurrGear; // ebx
  int iChgIdx; // ecx
  int carDesignIndex3; // esi
  int iRevsOffset; // ebp
  int iNextGear; // edx
  float fChg; // [esp+0h] [ebp-20h]
  float fNextChg; // [esp+0h] [ebp-20h]
  int16 i; // [esp+4h] [ebp-1Ch]

  for (i = 0; i < 14; ++i) {
    carDesignIndex = (int)i;
    v1 = 0;
    carDesignIndex2 = CAR_DESIGN_AUTO;
    iNumGears = CarEngines.engines[carDesignIndex].iNumGears;
    pSpds = CarEngines.engines[carDesignIndex].pSpds;
    while (v1 < i) {
      if (pSpds == CarEngines.engines[carDesignIndex2].pSpds)
        iNumGears = -1;
      carDesignIndex2 = ((int)carDesignIndex2 + 1);
      ++v1;
    }
    for (j = 0; j < iNumGears; *(pSpds - 1) = (float)v6) {
      v6 = *pSpds++ * 2.2;
      ++j;
    }
    iCurrGear = 0;
    if (iNumGears > 0) {
      iChgIdx = 0;
      do {
        carDesignIndex3 = (int)i;                    // why is this defined a second time
        iRevsOffset = 12 * i;
        fChg = (float)CarEngines.engines[carDesignIndex3].pChgs[iChgIdx];
        eng_chg_revs[iChgIdx + iRevsOffset] = (float)calc_revs(CarEngines.engines[carDesignIndex3].pRevs, iCurrGear, fChg);
        fNextChg = (float)CarEngines.engines[carDesignIndex3].pChgs[iChgIdx + 1];
        iChgIdx += 2;
        iNextGear = iCurrGear++;
        eng_chg_revs[iChgIdx + iRevsOffset - 1] = (float)calc_revs(
                                                          CarEngines.engines[carDesignIndex3].pRevs,
                                                          iNextGear,
                                                          fNextChg);
      } while (iCurrGear < iNumGears);
    }
  }
  for (int iCarIdx = 0; iCarIdx < 14; iCarIdx++) {
    for (int i = 0; i < CarEngines.engines[iCarIdx].iNumGears; i++) {
      StoreEngines[iCarIdx].speeds[i] = CarEngines.engines[iCarIdx].pSpds[i];
    }
  }
  CalcCarSizes();
}

//-------------------------------------------------------------------------------------------------
//000523A0
void CalcCarSizes()
{
  int16 nCarDesignsIdx; // cx
  int16 nCoordsIdx; // bx
  tVec3 *pCoords; // edx
  double fY; // st7
  float *p_fY; // edx
  double dZ; // st7
  float *p_fZ; // edx
  int iCarBoxIdx; // eax
  tVec3 *pAutoCoords; // edx
  int16 nAutoCoordsIdx; // bx
  float fZLow; // [esp+0h] [ebp-28h]
  float fZHigh; // [esp+4h] [ebp-24h]
  float fYHigh; // [esp+8h] [ebp-20h]
  float fXHigh; // [esp+Ch] [ebp-1Ch]
  float fYLow; // [esp+10h] [ebp-18h]
  float fXLow; // [esp+14h] [ebp-14h]

  for (nCarDesignsIdx = 0; nCarDesignsIdx < 14; ++nCarDesignsIdx) {
    fZLow = 1073741800.0f;
    fYLow = 1073741800.0f;
    fXLow = 1073741800.0f;
    fZHigh = -1073741800.0f;
    fYHigh = -1073741800.0f;
    fXHigh = -1073741800.0f;
    nCoordsIdx = 0;
    // Go through everything listed in CarDesigns and get the max and min values to generate a hit box for each car
    pCoords = CarDesigns[nCarDesignsIdx].pCoords;
    while (nCoordsIdx < CarDesigns[nCarDesignsIdx].byNumCoords) {
      if (pCoords->fX < (double)fXLow)
        fXLow = pCoords->fX;
      if (pCoords->fX > (double)fXHigh)
        fXHigh = pCoords->fX;
      fY = pCoords->fY;
      p_fY = &pCoords->fY;
      if (fY < fYLow)
        fYLow = *p_fY;
      if (*p_fY > (double)fYHigh)
        fYHigh = *p_fY;
      dZ = p_fY[1];                             // why is it like this
      p_fZ = p_fY + 1;
      if (dZ < fZLow)
        fZLow = *p_fZ;
      if (*p_fZ > (double)fZHigh)
        fZHigh = *p_fZ;
      pCoords = (tVec3 *)(p_fZ + 1);            // funny way to get to the next set of coords
      ++nCoordsIdx;
    }
    // Multiply by tinycar value if using tinycar cheat
    if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0) {
      fXLow = fXLow * 0.25f;
      fYLow = fYLow * 0.25f;
      fXHigh = fXHigh * 0.25f;
      fYHigh = fYHigh * 0.25f;
    }
    iCarBoxIdx = nCarDesignsIdx;                // 24 for 8 3-float points defining a hitbox
    CarBox.hitboxAy[iCarBoxIdx][0].fX = fXLow;
    CarBox.hitboxAy[iCarBoxIdx][0].fY = fYLow;
    CarBox.hitboxAy[iCarBoxIdx][0].fZ = fZLow;
    CarBox.hitboxAy[iCarBoxIdx][1].fX = fXHigh;
    CarBox.hitboxAy[iCarBoxIdx][1].fY = fYLow;
    CarBox.hitboxAy[iCarBoxIdx][1].fZ = fZLow;
    CarBox.hitboxAy[iCarBoxIdx][2].fX = fXHigh;
    CarBox.hitboxAy[iCarBoxIdx][2].fY = fYHigh;
    CarBox.hitboxAy[iCarBoxIdx][2].fZ = fZLow;
    CarBox.hitboxAy[iCarBoxIdx][3].fX = fXLow;
    CarBox.hitboxAy[iCarBoxIdx][3].fY = fYHigh;
    CarBox.hitboxAy[iCarBoxIdx][3].fZ = fZLow;
    CarBox.hitboxAy[iCarBoxIdx][4].fX = fXLow;
    CarBox.hitboxAy[iCarBoxIdx][4].fY = fYLow;
    CarBox.hitboxAy[iCarBoxIdx][4].fZ = fZHigh;
    CarBox.hitboxAy[iCarBoxIdx][5].fX = fXHigh;
    CarBox.hitboxAy[iCarBoxIdx][5].fY = fYLow;
    CarBox.hitboxAy[iCarBoxIdx][5].fZ = fZHigh;
    CarBox.hitboxAy[iCarBoxIdx][6].fX = fXHigh;
    CarBox.hitboxAy[iCarBoxIdx][6].fY = fYHigh;
    CarBox.hitboxAy[iCarBoxIdx][6].fZ = fZHigh;
    CarBox.hitboxAy[iCarBoxIdx][7].fX = fXLow;
    CarBox.hitboxAy[iCarBoxIdx][7].fY = fYHigh;
    CarBox.hitboxAy[iCarBoxIdx][7].fZ = fZHigh;
  }
  pAutoCoords = CarDesigns[0].pCoords;          // ptr to xauto_coords
  CarBaseX = 0.0;
  CarBaseY = 0.0;
  for (nAutoCoordsIdx = 0; nAutoCoordsIdx < CarDesigns[0].byNumCoords; ++nAutoCoordsIdx) {
    if (pAutoCoords->fX > (double)CarBaseX)
      CarBaseX = pAutoCoords->fX;
    if (pAutoCoords->fY > (double)CarBaseY)
      CarBaseY = pAutoCoords->fY;
    ++pAutoCoords;
  }
  if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0) {
    CarBaseX = CarBaseX * 0.25f;
    CarBaseY = CarBaseY * 0.25f;
  }
  CarDiag = (float)sqrt(CarBaseX * CarBaseX + CarBaseY * CarBaseY);
}

//-------------------------------------------------------------------------------------------------
//00052650
void InitCars()
{
  int16 i; // si
  int16 nNumGears; // di
  int16 j; // bx
  int16 k; // bx
  int iCurrCar; // eax
  uint8 byCarDesign; // dl
  int iCarTexIdxSetUnloaded; // eax
  int iCurrCarTexIdxSetUnloaded; // edx
  int16 nCarTexIdx; // di
  eCarType carType; // eax
  eCarType carTexsLoadedIndex; // esi
  int iCarTexLoaded; // edx
  int iCurrCarTex; // ecx
  int16 m; // [esp+0h] [ebp-1Ch]

  for (i = 0; i < 14; ++i) {
    nNumGears = CarEngines.engines[i].iNumGears;
    for (j = 0; j < nNumGears; ++j) {
      if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
        CarEngines.engines[i].pSpds[j] = StoreEngines[i].speeds[j] * 1.03f;
      else
        CarEngines.engines[i].pSpds[j] = StoreEngines[i].speeds[j];
    }
  }
  for (k = 0; k < numcars; ++k) {
    iCurrCar = k;
    byCarDesign = Drivers_Car[k];
    Car[iCurrCar].byCarDesignIdx = byCarDesign;
  }

  // set all of car_texs_loaded to -1
  iCarTexIdxSetUnloaded = 1;
  car_texs_loaded[0] = 0;
  do {
    iCurrCarTexIdxSetUnloaded = (int16)iCarTexIdxSetUnloaded++;
    car_texs_loaded[iCurrCarTexIdxSetUnloaded] = -1;
  } while ((int16)iCarTexIdxSetUnloaded < 16);

  // load all car textures
  nCarTexIdx = 1;
  for (m = 0; m < numcars; ++m) {
    if (!non_competitors[m]) {
      carType = CarDesigns[Car[m].byCarDesignIdx].carType;
      carTexsLoadedIndex = carType;
      iCarTexLoaded = car_texs_loaded[carType];
      if (iCarTexLoaded == -1) {
        iCurrCarTex = nCarTexIdx;
        LoadCarTexture(carType, (uint8)nCarTexIdx++);
        car_texmap[m] = iCurrCarTex;
        car_texs_loaded[carTexsLoadedIndex] = iCurrCarTex;
      } else {
        car_texmap[m] = iCarTexLoaded;
      }
    }
  }
  LoadCarTextures = nCarTexIdx;
  CalcCarSizes();
}

//-------------------------------------------------------------------------------------------------
//000527C0
void placecars()
{
  unsigned int uiStartingLane; // ecx
  int iDriverIdx; // edi
  int iCarIdx; // eax
  uint8 byDriversCarDesign; // dl
  char byCarIdx; // dl
  int iCarIdx2; // eax
  int iChampMode; // esi
  int16 nStartChunk; // dx
  int iCarIdx3; // ebx
  int iRandomVal; // eax
  unsigned int uiOrderOffset; // eax
  int iCarIdx4; // esi
  tVec3 *pTrackData; // eax
  double dZCmp; // st7
  int iViewCarIdx; // ebx
  tCarEngine *pEngine; // eax
  char byGearAyMax; // dl
  double dPower; // st7
  int nCurrChunk; // edx
  tData *pTrackLocalData; // ebp
  double dTrackYPos; // st7
  int iPlayers; // edi
  int iMaxOffset; // ebp
  int iPlayerIdx; // esi
  int iPlayerCarIdx; // eax
  unsigned int iSLightOffset; // edx
  double dZ; // st7
  int nPlayerChunk; // ebx
  float *pLightTrackData; // ebx
  double dTransform1; // st7
  double dTransform2; // st7
  double dTransform3; // st6
  double dTransform4; // st7
  double dTransform5; // st7
  double dTransform6; // st6
  double dLightSpeedZ; // st7
  int surfaceTypeAy[4]; // [esp+0h] [ebp-50h]
  float fX; // [esp+10h] [ebp-40h]
  tTrackInfo *pTrackChunk; // [esp+14h] [ebp-3Ch]
  float fTempZ; // [esp+18h] [ebp-38h]
  float fY; // [esp+1Ch] [ebp-34h]
  float fLightPosX; // [esp+20h] [ebp-30h]
  float fLightPosZ; // [esp+24h] [ebp-2Ch]
  unsigned int uiGridOffset; // [esp+28h] [ebp-28h]
  int iCarCounter; // [esp+2Ch] [ebp-24h]
  int iGridOffsetDWords; // [esp+30h] [ebp-20h]
  int iGridOffsetWords; // [esp+34h] [ebp-1Ch]

  surfaceTypeAy[0] = car_c_reference_1[0];       // Copy car reference array (car type lookup table)
  surfaceTypeAy[1] = car_c_reference_1[1];
  surfaceTypeAy[2] = car_c_reference_1[2];
  surfaceTypeAy[3] = car_c_reference_1[3];
  memset(Car, 0, sizeof(tCar) * numcars);       // Initialize all car data structures to zero
  uiStartingLane = 1;
  memset(CarSpray, 0, 1408 * (numcars + 2));    // Initialize spray effect data for all cars + 2 extra
  iCarCounter = 0;                              // Start car placement loop
  if (numcars > 0) {
    iGridOffsetDWords = 4 * iCarCounter;
    uiGridOffset = 0;
    iGridOffsetWords = 2 * iCarCounter;
    do {
      iDriverIdx = grid[uiGridOffset / 4];      // Get driver index from grid position
      iCarIdx = iDriverIdx;
      byDriversCarDesign = Drivers_Car[iDriverIdx];// Get car design for this driver
      Car[iCarIdx].iDriverIdx = iDriverIdx;     // Set driver index for this car
      finished_car[iDriverIdx] = 0;             // Mark driver as not finished
      Car[iCarIdx].byCarDesignIdx = byDriversCarDesign;
      byCarIdx = iCarCounter;
      Car[iCarIdx].pos.fX = 0.0;
      if ((byCarIdx & 1) != 0)                // Alternate Y position based on car index (staggered grid)
        Car[iDriverIdx].pos.fY = 500.0f;
      else
        Car[iDriverIdx].pos.fY = -500.0f;
      iCarIdx2 = iDriverIdx;
      Car[iCarIdx2].pos.fZ = 0.0;
      Car[iCarIdx2].fBestLapTime = 9999.9902f;   // Initialize best lap time to maximum value
      Car[iCarIdx2].nRoll = 0;                  // Initialize car orientation angles
      Car[iCarIdx2].nYaw = 0;
      Car[iCarIdx2].fFinalSpeed = 0.0f;
      Car[iCarIdx2].byGearAyMax = -1;
      iChampMode = champ_mode;                  // Set starting track chunk based on championship mode
      Car[iCarIdx2].nPitch = 0;
      if (iChampMode)
        nStartChunk = TRAK_LEN - 56 - iGridOffsetWords;// start further back (56 chunks + offset)
      else
        nStartChunk = TRAK_LEN - 2 - iGridOffsetDWords;// start close to end (2 chunks + offset)
      Car[iDriverIdx].nCurrChunk = nStartChunk; // Set current track chunk for car
      iCarIdx3 = iDriverIdx;
      Car[iCarIdx3].iLastValidChunk = Car[iDriverIdx].nCurrChunk;// Initialize track chunk tracking variables
      Car[iCarIdx3].nReferenceChunk = Car[iDriverIdx].nCurrChunk;
      Car[iCarIdx3].iTrackedCarIdx = -1;
      Car[iCarIdx3].byLap = 0;
      Car[iCarIdx3].byLappedStatus = 0;
      Car[iCarIdx3].byLapNumber = 0;
      Car[iCarIdx3].fDurability = CarEngines.engines[Car[iDriverIdx].byCarDesignIdx].fDurability;
      Car[iCarIdx3].iControlType = 3;
      Car[iCarIdx3].iSelectedStrategy = 0;
      Car[iCarIdx3].iBobMode = 0;
      Car[iCarIdx3].iAITargetLine = -1;
      iRandomVal = rand();
      Car[iDriverIdx].iAITargetCar = -1;
      Car[iDriverIdx].fHealth = 100.0f;
      Car[iDriverIdx].nTargetChunk = -1;
      Car[iDriverIdx].nLastCommentaryChunk = -1;
      Car[iDriverIdx].iAICurrentLine = GetHighOrderRand(4, iRandomVal);// iRandomVal >> 13;
      //Car[iDriverIdx].iUnk37 = (iRandomVal % 8192) / 8192;
      //Car[iDriverIdx].iUnk37 = (iRandomVal - (__CFSHL__(iRandomVal >> 31, 13) + (iRandomVal >> 31 << 13))) >> 13;
      Car[iDriverIdx].iStunned = 0;
      Car[iDriverIdx].iPitchDynamicOffset = 0;
      Car[iDriverIdx].iRollDynamicOffset = 0;
      Car[iDriverIdx].iDamageState = 0;
      Car[iDriverIdx].byRacePosition = iCarCounter;// Set race position and other race parameters
      Car[iDriverIdx].nChangeMateCooldown = 1080;
      Car[iDriverIdx].byThrottlePressed = 0;
      uiOrderOffset = uiGridOffset;
      Car[iDriverIdx].byLives = 3;
      *(int *)((char *)carorder + uiOrderOffset) = iDriverIdx;// Add car to race order array
      Car[iDriverIdx].byAccelerating = 0;              // Initialize various car flags and counters
      Car[iDriverIdx].byEngineStartTimer = 0;
      Car[iDriverIdx].byAIThrottleControl = 0;
      Car[iDriverIdx].byPitLaneActiveFlag = 0;
      Car[iDriverIdx].bySfxCooldown = 0;
      Car[iDriverIdx].byCollisionTimer = 0;
      Car[iDriverIdx].byDamageToggle = 0;
      Car[iDriverIdx].byLastDamageToggle = 0;
      Car[iDriverIdx].byCheatAmmo = 8;          // Set cheat ammo count
      Car[iDriverIdx].nReverseWarnCooldown = 0;
      iCarIdx4 = non_competitors[iDriverIdx];   // Check if this is a non-competitor car
      Car[iDriverIdx].byCheatCooldown = 0;
      if (iCarIdx4)                           // Handle non-competitor cars (spectators/disabled)
      {
        Car[iDriverIdx].byLives = -1;           // Non-competitors have unlimited lives (-1)
        finished_car[iDriverIdx] = -1;          // Mark as finished (disabled)
        pTrackData = localdata[Car[iDriverIdx].nCurrChunk].pointAy;// Get track data for positioning
        Car[iDriverIdx].iControlType = 0;       // Set control type to inactive (0)
        Car[iDriverIdx].pos.fX = 0.0f * pTrackData->fY + 0.0f * pTrackData->fX + 0.0f * pTrackData->fZ - pTrackData[3].fX;// Transform and set position using track transformation matrix
        Car[iDriverIdx].pos.fY = 0.0f * pTrackData[1].fX + 0.0f * pTrackData[1].fY + 0.0f * pTrackData[1].fZ - pTrackData[3].fY;
        dZCmp = 0.0 * pTrackData[2].fX + 0.0 * pTrackData[2].fY + 0.0 * pTrackData[2].fZ - pTrackData[3].fZ + 1000.0;// Calculate Z position with offset
        Car[iDriverIdx].fFinalSpeed = 0.0;
        //LOWORD(pTrackData) = Car[iDriverIdx].nCurrChunk;
        Car[iDriverIdx].fSpeedOverflow = 0;
        Car[iDriverIdx].nReferenceChunk = Car[iDriverIdx].nCurrChunk;// (__int16)pTrackData;
        Car[iDriverIdx].nCurrChunk = -1;        // Set chunk to -1 (disabled car)
        Car[iDriverIdx].pos.fZ = (float)dZCmp;
      }
      if (champ_mode && iDriverIdx == ViewType[0])// Special handling for player car in championship mode
      {
        iViewCarIdx = ViewType[0];
        pEngine = &CarEngines.engines[Car[iViewCarIdx].byCarDesignIdx];// Get engine data for player car
        Car[iViewCarIdx].fRPMRatio = 1.0;
        byGearAyMax = (uint8)(pEngine->iNumGears) - 1;// Set maximum gear for this car
        Car[iViewCarIdx].byGearAyMax = byGearAyMax;
        Car[iViewCarIdx].fFinalSpeed = pEngine->pSpds[byGearAyMax];// Set max speed from engine data
        Car[iViewCarIdx].fBaseSpeed = Car[iViewCarIdx].fFinalSpeed;
        dPower = calc_pow(Car[iViewCarIdx].byCarDesignIdx, (char)Car[iViewCarIdx].byGearAyMax, Car[iViewCarIdx].fRPMRatio);// Calculate power for this car configuration
        Car[iViewCarIdx].fSpeedOverflow = 0;
        Car[iViewCarIdx].byThrottlePressed = -1;
        Car[iViewCarIdx].byEngineStartTimer = 36;
        Car[iViewCarIdx].fPower = (float)dPower;
      }
      nCurrChunk = Car[iDriverIdx].nCurrChunk;  // Position cars on track based on available lanes
      if (nCurrChunk >= 0 && nCurrChunk < TRAK_LEN) { //added by ROLLER
        pTrackLocalData = &localdata[nCurrChunk]; // Get track data for current chunk
        pTrackChunk = &TrackInfo[nCurrChunk];     // Get track info for surface types]
        while ((TrakColour[Car[iDriverIdx].nCurrChunk][surfaceTypeAy[uiStartingLane]] & SURFACE_FLAG_NO_SPAWN) != 0)
        //while ((TrakColour[Car[iDriverIdx].nCurrChunk][*(int *)((char *)surfaceTypeAy + Car[iDriverIdx].nCurrChunk)] & 0x8000000) != 0)// Find valid lane (avoid surfaces with special flags)
        {
          if (++uiStartingLane == 4)
            uiStartingLane = 0;
        }
        if (uiStartingLane <= 3) {                                         // Calculate Y position based on selected lane
          switch (uiStartingLane) {
            case 0u:
              dTrackYPos = pTrackChunk->fLShoulderWidth * 0.5 + pTrackLocalData->fTrackHalfWidth;// Lane 0: left shoulder position
              break;
            case 1u:
              dTrackYPos = pTrackLocalData->fTrackHalfWidth * 0.5;// Lane 1: left side of track
              break;
            case 2u:
              dTrackYPos = -pTrackLocalData->fTrackHalfWidth * 0.5;// Lane 2: right side of track
              break;
            case 3u:
              dTrackYPos = -pTrackLocalData->fTrackHalfWidth - pTrackChunk->fRShoulderWidth * 0.5;// Lane 3: right shoulder position
              break;
          }
          Car[iDriverIdx].pos.fY = (float)dTrackYPos;    // Set calculated Y position for car
        }
      }
      ++uiStartingLane;
      putflat(&Car[iDriverIdx]);                // Position car flat on track surface
      if (uiStartingLane == 4)
        uiStartingLane = 0;
      iGridOffsetDWords += 4;                   // Advance to next car
      iGridOffsetWords += 2;
      uiGridOffset += 4;
      ++iCarCounter;
    } while (iCarCounter < numcars);
  }
  iPlayers = 0;
  if (local_players > 0) {
    iMaxOffset = 144;
    iPlayerIdx = 0;
    do {
      iPlayerCarIdx = ViewType[iPlayerIdx];
      iSLightOffset = 144 * iPlayers;
      fLightPosX = Car[iPlayerCarIdx].pos.fX + 2000.0f;
      SLight[0][iSLightOffset / 0x30].targetPos.fY = Car[iPlayerCarIdx].pos.fY;
      fLightPosZ = Car[iPlayerCarIdx].pos.fZ + 500.0f;
      SLight[0][iSLightOffset / 0x30].targetPos.fX = fLightPosX;
      SLight[0][iSLightOffset / 0x30].currentPos.fX = SLight[iPlayers][0].targetPos.fX + 10000.0f;
      SLight[0][iSLightOffset / 0x30].currentPos.fY = SLight[iPlayers][0].targetPos.fY;
      SLight[0][iSLightOffset / 0x30].targetPos.fZ = fLightPosZ;
      SLight[0][iSLightOffset / 0x30].currentPos.fZ = SLight[iPlayers][0].targetPos.fZ + 5000.0f;
      SLight[0][iSLightOffset / 0x30 + 1].targetPos.fY = Car[iPlayerCarIdx].pos.fY + -500.0f;
      SLight[0][iSLightOffset / 0x30].speed.fX = 100.0f;
      SLight[0][iSLightOffset / 0x30].speed.fY = 100.0f;
      SLight[0][iSLightOffset / 0x30].speed.fZ = 40.0f;
      SLight[0][iSLightOffset / 0x30 + 1].targetPos.fX = fLightPosX;
      SLight[0][iSLightOffset / 0x30 + 1].currentPos.fX = SLight[iPlayers][1].targetPos.fX + 10000.0f;
      SLight[0][iSLightOffset / 0x30 + 1].currentPos.fY = SLight[iPlayers][1].targetPos.fY + -1000.0f;
      SLight[0][iSLightOffset / 0x30 + 1].targetPos.fZ = fLightPosZ;
      SLight[0][iSLightOffset / 0x30 + 1].currentPos.fZ = SLight[iPlayers][1].targetPos.fZ + 5000.0f;
      SLight[0][iSLightOffset / 0x30 + 2].targetPos.fY = Car[iPlayerCarIdx].pos.fY + 500.0f;
      SLight[0][iSLightOffset / 0x30 + 2].targetPos.fX = fLightPosX;
      SLight[0][iSLightOffset / 0x30 + 2].currentPos.fX = SLight[iPlayers][2].targetPos.fX + 10000.0f;
      SLight[0][iSLightOffset / 0x30 + 2].currentPos.fY = SLight[iPlayers][2].targetPos.fY + 1000.0f;
      SLight[0][iSLightOffset / 0x30].uiRotation = 0;
      SLight[0][iSLightOffset / 0x30 + 1].uiRotation = 0;
      SLight[0][iSLightOffset / 0x30 + 2].targetPos.fZ = fLightPosZ;
      dZ = SLight[iPlayers][2].targetPos.fZ;
      SLight[0][iSLightOffset / 0x30 + 1].speed.fX = 100.0f;
      SLight[0][iSLightOffset / 0x30 + 1].speed.fY = 100.0f;
      SLight[0][iSLightOffset / 0x30 + 1].speed.fZ = 40.0f;
      SLight[0][iSLightOffset / 0x30 + 2].speed.fX = 100.0f;
      SLight[0][iSLightOffset / 0x30 + 2].speed.fY = 100.0f;
      SLight[0][iSLightOffset / 0x30 + 2].uiRotation = 0;
      nPlayerChunk = Car[iPlayerCarIdx].nCurrChunk;
      SLight[0][iSLightOffset / 0x30 + 2].speed.fZ = 40.0f;
      pLightTrackData = (float *)&localdata[nPlayerChunk];
      SLight[0][iSLightOffset / 0x30 + 2].currentPos.fZ = (float)dZ + 5000.0f;
      do {
        dTransform1 = pLightTrackData[1];
        fX = SLight[0][iSLightOffset / 0x30].currentPos.fX;
        fY = SLight[0][iSLightOffset / 0x30].currentPos.fY;
        dTransform2 = dTransform1 * fY + *pLightTrackData * fX;
        dTransform3 = pLightTrackData[2];
        fTempZ = SLight[0][iSLightOffset / 0x30].currentPos.fZ;
        SLight[0][iSLightOffset / 0x30].currentPos.fX = (float)(dTransform2 + dTransform3 * fTempZ - pLightTrackData[9]);
        SLight[0][iSLightOffset / 0x30].currentPos.fY = pLightTrackData[3] * fX + pLightTrackData[4] * fY + pLightTrackData[5] * fTempZ - pLightTrackData[10];
        SLight[0][iSLightOffset / 0x30].currentPos.fZ = pLightTrackData[6] * fX + pLightTrackData[7] * fY + pLightTrackData[8] * fTempZ - pLightTrackData[11];
        dTransform4 = pLightTrackData[1];
        fX = SLight[0][iSLightOffset / 0x30].targetPos.fX;
        fY = SLight[0][iSLightOffset / 0x30].targetPos.fY;
        dTransform5 = dTransform4 * fY + *pLightTrackData * fX;
        dTransform6 = pLightTrackData[2];
        fTempZ = SLight[0][iSLightOffset / 0x30].targetPos.fZ;
        SLight[0][iSLightOffset / 0x30].targetPos.fX = (float)(dTransform5 + dTransform6 * fTempZ - pLightTrackData[9]);
        SLight[0][iSLightOffset / 0x30].targetPos.fY = pLightTrackData[3] * fX + pLightTrackData[4] * fY + pLightTrackData[5] * fTempZ - pLightTrackData[10];
        SLight[0][iSLightOffset / 0x30].targetPos.fZ = pLightTrackData[6] * fX + pLightTrackData[7] * fY + pLightTrackData[8] * fTempZ - pLightTrackData[11];
        SLight[0][iSLightOffset / 0x30].speed.fX = (float)(fabs(SLight[0][iSLightOffset / 0x30].targetPos.fX - SLight[0][iSLightOffset / 0x30].currentPos.fX) * 0.01388888888888889);
        SLight[0][iSLightOffset / 0x30].speed.fY = (float)(fabs(SLight[0][iSLightOffset / 0x30].targetPos.fY - SLight[0][iSLightOffset / 0x30].currentPos.fY) * 0.01388888888888889);
        dLightSpeedZ = fabs(SLight[0][iSLightOffset / 0x30].targetPos.fZ - SLight[0][iSLightOffset / 0x30].currentPos.fZ) * 0.02777777777777778;
        SLight[0][iSLightOffset / 0x30].speed.fZ = (float)dLightSpeedZ;
        iSLightOffset += 48;
        //StoreEngines[iSLightOffset / 0x18 + 13].speeds[2] = (float)dLightSpeedZ;
      } while (iSLightOffset != iMaxOffset);
      iMaxOffset += 144;
      ++iPlayers;
      ++iPlayerIdx;
    } while (iPlayers < local_players);
  }
}

//-------------------------------------------------------------------------------------------------
//00052FE0
void DrawCars(int iCarIdx, int iViewMode)
{
  int iCarDrawIdx; // ebp
  int iCurrChunk_1; // esi
  int iCurrChunk; // edi
  int iVisibilityFlag; // edx
  int iYaw; // ebx
  int iPitch; // eax
  int iPointIdx; // edx
  tVec3 *pointAy; // ebx
  tVec3 *pHitboxPt; // ecx
  double dMinZ; // st7
  int iNumCars; // edi
  float fHitboxZ; // [esp+0h] [ebp-74h]
  float fHitboxY; // [esp+4h] [ebp-70h]
  float fHitboxX; // [esp+8h] [ebp-6Ch]
  float fWorldZ; // [esp+Ch] [ebp-68h]
  float fWorldY; // [esp+10h] [ebp-64h]
  int iRoll; // [esp+14h] [ebp-60h]
  float fWorldX; // [esp+18h] [ebp-5Ch]
  int iCarIterator; // [esp+24h] [ebp-50h]
  tCar *pCar; // [esp+28h] [ebp-4Ch]
  float fTransformM12; // [esp+2Ch] [ebp-48h]
  float fX; // [esp+30h] [ebp-44h]
  float fTransformM00; // [esp+34h] [ebp-40h]
  float fTransformM02; // [esp+38h] [ebp-3Ch]
  float fTransformM11; // [esp+3Ch] [ebp-38h]
  float fTransformM01; // [esp+40h] [ebp-34h]
  float fTransformM21; // [esp+44h] [ebp-30h]
  float fTransformM10; // [esp+48h] [ebp-2Ch]
  float fTransformM22; // [esp+4Ch] [ebp-28h]
  float fZ; // [esp+50h] [ebp-24h]
  float fY; // [esp+54h] [ebp-20h]
  float fTransformM20; // [esp+58h] [ebp-1Ch]
  float fClosestZ; // [esp+5Ch] [ebp-18h]

  iCarIterator = 0;                             // Initialize car iterator counter
  if (numcars > 0) {
    pCar = Car;
    iCarDrawIdx = 0;
    do {
      fX = pCar->pos.fX;                        // Get car position and current track chunk
      iCurrChunk_1 = pCar->nCurrChunk;
      fY = pCar->pos.fY;
      iCurrChunk = iCurrChunk_1;
      fZ = pCar->pos.fZ;
      if (iCurrChunk_1 == -1)                 // Use last valid chunk if current chunk is invalid (-1)
        iCurrChunk = pCar->iLastValidChunk;
      if (backwards)                          // Visibility culling - check if car is in visible track sections
      {
        if (front_sec <= back_sec) {
          if (iCurrChunk <= front_sec || iCurrChunk >= back_sec) {
            iVisibilityFlag = -1;
            goto LABEL_32;
          }
        } else if (iCurrChunk <= front_sec && iCurrChunk >= back_sec) {
          iVisibilityFlag = -1;
        LABEL_32:
          if (!iVisibilityFlag || mid_sec < 0 || next_front < 0)
            goto LABEL_42;
          if (mid_sec <= next_front) {
            if (iCurrChunk >= mid_sec && iCurrChunk <= next_front)
              goto LABEL_42;
          } else if (iCurrChunk >= mid_sec || iCurrChunk <= next_front) {
            goto LABEL_42;
          }
          goto LABEL_41;
        }
        iVisibilityFlag = 0;
        goto LABEL_32;
      }
      if (front_sec >= back_sec) {
        if (iCurrChunk > back_sec && iCurrChunk < front_sec) {
        LABEL_13:
          iVisibilityFlag = 0;
          goto LABEL_14;
        }
        iVisibilityFlag = -1;
      } else {
        if (iCurrChunk < front_sec || iCurrChunk > back_sec)
          goto LABEL_13;
        iVisibilityFlag = -1;
      }
    LABEL_14:
      if (!iVisibilityFlag || mid_sec < 0 || next_front < 0)
        goto LABEL_42;
      if (mid_sec >= next_front) {
        if (iCurrChunk <= mid_sec && iCurrChunk >= next_front)
          goto LABEL_42;
      } else if (iCurrChunk <= mid_sec || iCurrChunk >= next_front) {
        goto LABEL_42;
      }
    LABEL_41:
      iVisibilityFlag = 0;
    LABEL_42:
      if (iVisibilityFlag && (pCar->byLives & 0x80u) == 0) {
        iRoll = (pCar->iRollDynamicOffset + pCar->nRoll) & 0x3FFF;// Calculate car rotation matrix from yaw, pitch, roll
        iYaw = pCar->nYaw;
        iPitch = ((uint16)pCar->iPitchDynamicOffset + pCar->nPitch) & 0x3FFF;
        fTransformM00 = tcos[iYaw] * tcos[iPitch];// Build 3x3 rotation matrix for car orientation
        fTransformM10 = tsin[iYaw] * tcos[iPitch];
        fTransformM20 = tsin[iPitch];
        fTransformM01 = tcos[iYaw] * fTransformM20 * tsin[iRoll] - tsin[iYaw] * tcos[iRoll];
        fTransformM11 = tsin[iYaw] * fTransformM20 * tsin[iRoll] + tcos[iYaw] * tcos[iRoll];
        fTransformM12 = -tsin[iRoll] * tcos[iPitch];
        fTransformM02 = -tcos[iYaw] * fTransformM20 * tcos[iRoll] - tsin[iYaw] * tsin[iRoll];
        fTransformM21 = -tsin[iYaw] * fTransformM20 * tcos[iRoll] + tcos[iYaw] * tsin[iRoll];
        fTransformM22 = tcos[iPitch] * tcos[iRoll];
        iPointIdx = 0;
        fClosestZ = 1073676300.0f;               // Initialize closest Z distance for depth sorting
        
        if (iCurrChunk_1 >= 0 && iCurrChunk_1 < TRAK_LEN) //check added by ROLLER
          pointAy = localdata[iCurrChunk_1].pointAy;
        else
          pointAy = NULL;

        pHitboxPt = CarBox.hitboxAy[pCar->byCarDesignIdx];// Get car hitbox points and chunk transform matrix
        do {
          fHitboxX = pHitboxPt->fX;             // Transform car hitbox points loop (8 points)
          fHitboxY = pHitboxPt->fY;
          fHitboxZ = pHitboxPt->fZ;
          CarPt[iPointIdx].world.fX = pHitboxPt->fX * fTransformM00 + fHitboxY * fTransformM01 + fHitboxZ * fTransformM02 + fX;// Apply car rotation matrix to hitbox point
          CarPt[iPointIdx].world.fY = fHitboxX * fTransformM10 + fHitboxY * fTransformM11 + fHitboxZ * fTransformM21 + fY;
          ++pHitboxPt;
          CarPt[iPointIdx].world.fZ = fHitboxX * fTransformM20 + fHitboxY * fTransformM12 + fHitboxZ * fTransformM22 + fZ;
          if (iCurrChunk_1 != -1 && pointAy)             // Transform from car space to track chunk space if valid chunk
          {
            fWorldX = CarPt[iPointIdx].world.fX;
            fWorldY = CarPt[iPointIdx].world.fY;
            fWorldZ = CarPt[iPointIdx].world.fZ;
            CarPt[iPointIdx].world.fX = pointAy->fY * fWorldY + pointAy->fX * fWorldX + pointAy->fZ * fWorldZ - pointAy[3].fX;
            CarPt[iPointIdx].world.fY = pointAy[1].fX * fWorldX + pointAy[1].fY * fWorldY + pointAy[1].fZ * fWorldZ - pointAy[3].fY;
            CarPt[iPointIdx].world.fZ = pointAy[2].fX * fWorldX + pointAy[2].fY * fWorldY + pointAy[2].fZ * fWorldZ - pointAy[3].fZ;
          }
          k1 = CarPt[iPointIdx].world.fX - viewx;// Transform to camera view space for rendering
          k2 = CarPt[iPointIdx].world.fY - viewy;
          k3 = CarPt[iPointIdx].world.fZ - viewz;
          dMinZ = k1 * vk3 + k2 * vk6 + k3 * vk9;
          CarPt[iPointIdx].view.fZ = (float)dMinZ;
          if (dMinZ < fClosestZ)              // Track closest Z value for depth sorting
            fClosestZ = CarPt[iPointIdx].view.fZ;
          ++iPointIdx;
        } while (iPointIdx != 8);
        car_draw_order[iCarDrawIdx].fMinZDepth = fClosestZ;// Store car in draw order array for Z-sorting
        car_draw_order[iCarDrawIdx].iChunkIdx = iCurrChunk;
        if (iCarIterator == iCarIdx && (!iViewMode || iViewMode == -1))// Hide player car in certain view modes
          car_draw_order[iCarDrawIdx].iChunkIdx = -2;
      } else {
        car_draw_order[iCarDrawIdx].iChunkIdx = -2;// Mark invisible cars with special values
        car_draw_order[iCarDrawIdx].fMinZDepth = -1.0f;
      }
      iNumCars = numcars;

      car_draw_order[iCarDrawIdx].iCarIdx = iCarIterator;
      iCarDrawIdx++;
      //CarZOrder[++iCarDrawIdx + 499].iPolygonIndex = iCarIterator;// references car_draw_order
      ++pCar;
      ++iCarIterator;
    } while (iCarIterator < iNumCars);
  }
}

//-------------------------------------------------------------------------------------------------
//000534A0
void DisplayCar(int iCarIndex, uint8 *pScreenBuffer, float fDistanceToCar)
{
  int iMotionX; // ebx
  int iMotionY; // esi
  int iMotionZ; // edi
  int16 nRoll; // cx
  int iYawAngle; // edx
  int iPitch; // ebp
  double dCosYaw; // st7
  int iRoll; // ecx
  double dRotMat22; // st7
  float fZ; // eax
  int iChunk; // edi
  tData *pTrackData; // esi
  uint8 byCarDesignIdx; // dh
  double dY; // st7
  float *p_fX; // edx
  unsigned int uiCarWorldOffset; // eax
  double dTransformMat22; // st7
  double dTransformMat02; // st6
  double dTransformMat12; // st5
  double dTransformMat11; // st4
  double dTransformMat01; // st3
  double dTransformMat21; // st2
  int i; // eax
  double dLocalZ; // st7
  float *pCarPosPtr; // edx
  int iCornerIndex; // eax
  double dTransformMat22_2; // st7
  double dTransformMat02_2; // st6
  double dTransformMat12_2; // st5
  double dTransformMat11_2; // st4
  double dTransformMat01_2; // st3
  double dTransformMat21_2; // st2
  int nCurrChunk; // eax
  tData *pCurrentTrackData; // eax
  double dRelativeX; // st7
  double dRelativeY; // st5
  double dRelativeZ; // st4
  int iLeftSurfaceType; // ecx
  int iRightSurfaceType; // ecx
  int iLaneType; // ebx
  int iGroundedCorners; // ecx
  //int64 ullTrackColor; // rax
  int iGroundColorType; // edx
  int iCornerIdx; // ebx
  int iHeightIdx; // ebp
  double dGroundHeight; // st7
  int iCornerLoopIdx; // edi
  int j; // edx
  int m; // eax
  double dCoordZ; // st7
  int n; // edx
  double dBankHeight; // st7
  int k; // edx
  double dBankHeight2; // st7
  int iUndergroundCheck; // ebp
  int iUndergroundIdx; // edx
  double dUndergroundHeight; // st7
  int iShadowOffset; // eax
  double dWorldToLocalZ; // st7
  int iScreenSize; // ecx
  int ii; // edx
  double dViewDeltaX; // st7
  double dViewDeltaY; // st6
  double dViewDeltaZ; // st5
  double dViewSpaceZ; // st7
  double dViewDistance; // st7
  double dInverseZ; // st6
  double dScreenX; // st5
  double dScreenY; // st7
  //int iScreenXInt; // eax
  double dShadowX; // st7
  double dShadowCorner0X; // st7
  double dX; // st7
  double dShadowCorner1Y; // st7
  double dShadowCorner2X; // st7
  double dShadowCorner2Y; // st7
  double dShadowCorner3X; // st7
  double dShadowCorner3Y; // st7
  tVec3 *pCoords; // ebx
  tPolygon *pPols; // ecx
  unsigned int iCoordOffset; // esi
  int iCoordLoopSize; // edi
  tData *pChunkData; // edx
  float fModelCoordX; // eax
  float *pCoordPtr; // ebx
  float fModelCoordY; // eax
  float fModelCoordZ; // eax
  double dTransformedX; // st7
  double dTransformedY; // st5
  double dTransformedZ; // st6
  int iCurrentChunk; // ebp
  double dChunkTransformY; // st7
  double dChunkTransformX; // st7
  double dChunkMatrixZ; // st6
  double dChunkTransformZ; // st7
  double dViewRelativeX; // st7
  double dViewRelativeY; // st6
  double dViewRelativeZ; // st5
  double dCameraSpaceZ; // st7
  double dProjViewDistance; // st7
  double dProjInverseZ; // st6
  double dProjScreenX; // st5
  double dProjScreenY; // st7
  //int iScreenXResult; // eax
  signed int iVisiblePolygons; // esi
  int iPolygonLoopIdx; // edi
  //uint8 *pCurrentPolygon; // edx
  int jj; // eax
  //tCarPt *pCarVertex; // ebx
  double dCrossProduct1X; // st7
  double dCrossProduct1Z; // st6
  double dCrossProduct2X; // st5
  double dCrossProduct2Y; // st4
  double dCrossProduct2Z; // st3
  //char byTextureFlags; // dh
  int iPolygonResult; // eax
  float fZDepthMin1; // eax
  float fZDepthMin2; // eax
  float fZDepthFinal; // eax
  float fZDepthMax1; // eax
  float fZDepthMax2; // eax
  tPolygon *pPolygonBase; // eax
  int iPolygonIndex; // edi
  int nNextPolIdx; // ecx
  int iDepthSortIdx; // edi
  int iLinkedPolygonIdx; // ebx
  signed int kk; // eax
  int iZOrderCompareIdx; // edx
  int iSpriteIdx; // ebp
  float *pTrackChunkPtr; // ebx
  tCarSpray *pSmokeSpray; // ecx
  int iSmokeIndex; // edx
  signed int iSmokeZOrderIdx; // edi
  int iSmokeChunkIdx; // eax
  double dSmokeChunkTransY; // st7
  double dSmokeChunkTransX; // st7
  double dSmokeChunkMatZ; // st6
  double dSmokeChunkTransZ; // st7
  double dSmokeViewDeltaX; // st7
  double dSmokeViewDeltaY; // st6
  double dSmokeViewDeltaZ; // st5
  double dSmokeCameraZ; // st7
  double dSmokeProjViewDist; // st7
  double dSmokeProjInverseZ; // st6
  double dSmokeProjScreenX; // st5
  double dSmokeProjScreenY; // st7
  //int iSmokeScreenXResult; // eax
  int iSmokeChunkIdx2; // eax
  double dSmoke2ChunkTransY; // st7
  double dSmoke2ChunkTransX; // st7
  double dSmoke2ChunkMatZ; // st6
  double dSmoke2ChunkTransZ; // st7
  double dSmoke2ViewDeltaX; // st7
  double dSmoke2ViewDeltaY; // st6
  double dSmoke2ViewDeltaZ; // st5
  double dSmoke2CameraZ; // st7
  double dSmoke2ProjViewDist; // st7
  double dSmoke2ProjInverseZ; // st6
  double dSmoke2ProjScreenX; // st5
  double dSmoke2ProjScreenY; // st7
  //int iSmoke2ScreenXResult; // eax
  unsigned int uiRenderLoopOffset; // edi
  int iSpritePolygonIdx; // ebx
  int iSpriteIndex; // edx
  tCarSpray *pCurrentSprite; // eax
  double fSpriteSize; // st7
  int iScreenX; // ecx
  int iScreenY; // esi
  int x; // ebx
  int y; // edx
  //tCarSpray *pSpriteDataPtr; // eax
  int uiSpriteSurface; // eax
  double dSpriteProjection; // st7
  //tCarSpray *pSpriteDataPtr2; // eax
  int iSpriteSize; // esi
  int iSpriteCenterX; // ebx
  int iSpriteCenterY; // edx
  int iPolygonIdx; // esi
  tPolygon *pRenderPolygon; // edx
  int uiTextureSurface; // ecx
  int mm; // eax
  //tCarPt *pPolygonVertex; // ebx
  double dPolygonDepth23; // st7
  tCarPt *pMinDepthVertex23; // eax
  tCarPt *pMinDepthVertex01; // eax
  tCarPt *pFinalMinVertex01; // eax
  float fFinalMinDepth; // eax
  tCarPt *pFinalMinVertex23; // eax
  bool bCloseToCamera; // ebx
  int byTextureIndex; // eax
  tAnimation *pAnimation; // ecx
  int iAnimationFrame; // eax
  double dNameTagDeltaX; // st7
  double dNameTagDeltaY; // st6
  double dNameTagDeltaZ; // st5
  double dNameTagCameraZ; // st7
  double dNameTagViewDist; // st7
  double dNameTagInverseZ; // st6
  double dNameTagScreenX; // st5
  double dNameTagScreenY; // st7
  int iNameTagScreenSize; // ebx
  //int iNameTagXResult; // eax
  int iNameWidth; // ebx
  int iNameCharIdx; // eax
  int iNameHalfWidth; // eax
  int iNameDisplayX; // ecx
  int iNameDisplayY; // esi
  int iPrevScrSize; // edi
  int iTeamColIdx; // eax
  tData tempData; // [esp+0h] [ebp-298h] BYREF
  tCarPt *polygonVertices[4]; // [esp+80h] [ebp-218h]
  float fLocalCarPosY; // [esp+90h] [ebp-208h]
  int iCarIndexCopy; // [esp+94h] [ebp-204h]
  int iOnTrackFlag; // [esp+98h] [ebp-200h]
  tCarSpray *pCarSprayArray; // [esp+9Ch] [ebp-1FCh]
  uint8 *pScrBuf; // [esp+A0h] [ebp-1F8h]
  int iShadowRenderFlag; // [esp+A4h] [ebp-1F4h]
  uint32 uiColorTo; // [esp+A8h] [ebp-1F0h]
  uint32 uiColorFrom; // [esp+ACh] [ebp-1ECh]
  float fLocalCarPosX; // [esp+B0h] [ebp-1E8h]
  float fCarPointZ; // [esp+B4h] [ebp-1E4h]
  tTrackInfo *pTrackInfo; // [esp+B8h] [ebp-1E0h]
  int iBank; // [esp+BCh] [ebp-1DCh] BYREF
  int iSpriteScreenSize; // [esp+C0h] [ebp-1D8h]
  float fPolygonVertex1Y; // [esp+C4h] [ebp-1D4h]
  float fGroundHeightTemp; // [esp+CCh] [ebp-1CCh]
  float *pChunkDataPtr = NULL; // [esp+D4h] [ebp-1C4h]
  float fCrossProductY; // [esp+D8h] [ebp-1C0h]
  float fClampedZ; // [esp+DCh] [ebp-1BCh]
  int iTextureDisabled; // [esp+E0h] [ebp-1B8h]
  float fShadowViewX; // [esp+E4h] [ebp-1B4h]
  int carDesignIndex; // [esp+E8h] [ebp-1B0h]
  float fCarPointY; // [esp+F0h] [ebp-1A8h]
  float fTrackRelativeZ; // [esp+F4h] [ebp-1A4h]
  float fTrackRelativeY; // [esp+F8h] [ebp-1A0h]
  float fLocalCarPosZ; // [esp+FCh] [ebp-19Ch]
  int iElevation; // [esp+100h] [ebp-198h] BYREF
  int iAzimuth; // [esp+104h] [ebp-194h] BYREF
  int iBackfaceFlag; // [esp+108h] [ebp-190h]
  int iChunkOffset; // [esp+10Ch] [ebp-18Ch]
  float fZDepthCloseMin1; // [esp+110h] [ebp-188h]
  float fZDepthCloseMin2; // [esp+114h] [ebp-184h]
  float fZDepthCloseFinal; // [esp+118h] [ebp-180h]
  float fZDepthCloseFar1; // [esp+11Ch] [ebp-17Ch]
  float fZDepthCloseFar2; // [esp+120h] [ebp-178h]
  float fZDepthFarMax1; // [esp+124h] [ebp-174h]
  float fZDepthFarMax2; // [esp+128h] [ebp-170h]
  float fZDepthFarFinal; // [esp+12Ch] [ebp-16Ch]
  float fZDepthFarMin1; // [esp+130h] [ebp-168h]
  float fZDepthFarMin2; // [esp+134h] [ebp-164h]
  tPolygon *pPolygonArray; // [esp+138h] [ebp-160h]
  uint32 uiCarDesignStride; // [esp+13Ch] [ebp-15Ch]
  tPolygon *pLinkedPolygons; // [esp+140h] [ebp-158h]
  float fMinDepthVertex23; // [esp+144h] [ebp-154h]
  float fMinDepthVertex01; // [esp+148h] [ebp-150h]
  float fPolygonMinDepth; // [esp+14Ch] [ebp-14Ch]
  float fFinalVertex01Depth; // [esp+150h] [ebp-148h]
  float fFinalVertex23Depth; // [esp+154h] [ebp-144h]
  uint32 dwCarDesignOffset; // [esp+158h] [ebp-140h]
  int iAnimationOffset; // [esp+15Ch] [ebp-13Ch]
  int iSubdivisionParam; // [esp+160h] [ebp-138h]
  unsigned int uiTextureMapOffset; // [esp+164h] [ebp-134h]
  unsigned int uiZOrderLoopOffset; // [esp+168h] [ebp-130h]
  int iRenderLoopLimit; // [esp+16Ch] [ebp-12Ch]
  int iTunnelFlag; // [esp+170h] [ebp-128h]
  float fGroundHeightLoop; // [esp+174h] [ebp-124h]
  float fBankHeightLoop; // [esp+178h] [ebp-120h]
  float fBankHeightLoop2; // [esp+17Ch] [ebp-11Ch]
  float fNameTagCameraZCopy; // [esp+180h] [ebp-118h]
  float fSmoke2CameraZCopy; // [esp+184h] [ebp-114h]
  float fSmokeCameraZCopy; // [esp+188h] [ebp-110h]
  float fModelCameraZCopy; // [esp+18Ch] [ebp-10Ch]
  float fNameTagClampedZ; // [esp+190h] [ebp-108h]
  float fSmoke2ClampedZ; // [esp+194h] [ebp-104h]
  float fSmokeClampedZ; // [esp+198h] [ebp-100h]
  float fModelClampedZ; // [esp+19Ch] [ebp-FCh]
  float fNameTagViewY; // [esp+1A0h] [ebp-F8h]
  float fSmoke2ViewY; // [esp+1A4h] [ebp-F4h]
  float fSmokeViewY; // [esp+1A8h] [ebp-F0h]
  float fModelViewY; // [esp+1ACh] [ebp-ECh]
  float fNameTagViewX; // [esp+1B0h] [ebp-E8h]
  float fSmoke2ViewX; // [esp+1B4h] [ebp-E4h]
  float fSmokeViewX; // [esp+1B8h] [ebp-E0h]
  float fModelViewX; // [esp+1BCh] [ebp-DCh]
  float fPosZ; // [esp+1C0h] [ebp-D8h]
  float fPosY; // [esp+1C4h] [ebp-D4h]
  float fPosX; // [esp+1C8h] [ebp-D0h]
  float fNameTagWorldZ; // [esp+1CCh] [ebp-CCh]
  float fSmokeDepthCheck; // [esp+1D0h] [ebp-C8h]
  int iTemp; // [esp+1D4h] [ebp-C4h]
  float fNameTagWorldY; // [esp+1D8h] [ebp-C0h]
  float fSmokeWorldY; // [esp+1DCh] [ebp-BCh]
  float fNameTagWorldX; // [esp+1E0h] [ebp-B8h]
  signed int iZOrderIndex; // [esp+1E4h] [ebp-B4h]
  int iPolygonCount; // [esp+1E8h] [ebp-B0h]
  float fBackfaceDotProduct; // [esp+1ECh] [ebp-ACh]
  float fWarpDistortion; // [esp+1F0h] [ebp-A8h]
  float fShadowViewY; // [esp+1F4h] [ebp-A4h]
  float fModelCoordZLocal; // [esp+200h] [ebp-98h]
  float fModelCoordYLocal; // [esp+204h] [ebp-94h]
  float fModelCoordXLocal; // [esp+208h] [ebp-90h]
  float fModelWorldZ; // [esp+20Ch] [ebp-8Ch]
  float fModelWorldY; // [esp+210h] [ebp-88h]
  float fModelWorldX; // [esp+214h] [ebp-84h]
  float fCarPosZ; // [esp+218h] [ebp-80h]
  float fCarPosY; // [esp+21Ch] [ebp-7Ch]
  float fCarPosX; // [esp+220h] [ebp-78h]
  int iCurrChunk; // [esp+224h] [ebp-74h]
  int iNumCoords; // [esp+228h] [ebp-70h]
  float fRotMat22Copy; // [esp+22Ch] [ebp-6Ch]
  float fRotMat11; // [esp+230h] [ebp-68h]
  float fRotMat20; // [esp+234h] [ebp-64h]
  float fRotMat21; // [esp+238h] [ebp-60h]
  float fRotMat12; // [esp+23Ch] [ebp-5Ch]
  float fRotMat10; // [esp+240h] [ebp-58h]
  float fRotMat02; // [esp+244h] [ebp-54h]
  float fRotMat01; // [esp+248h] [ebp-50h]
  float fRotMat00; // [esp+24Ch] [ebp-4Ch]
  int iYaw; // [esp+250h] [ebp-48h]
  tCar *pCar; // [esp+254h] [ebp-44h]
  float fPolygonVertex1Z; // [esp+264h] [ebp-34h]
  float fPolygonVertex1X; // [esp+268h] [ebp-30h]
  tAnimation *pAnms; // [esp+278h] [ebp-20h]
  tCarDesign *pCarDesign; // [esp+27Ch] [ebp-1Ch]
  float fSmokeWorldX; // [esp+280h] [ebp-18h]

  iCarIndexCopy = iCarIndex;                    // Store car index parameter
  pScrBuf = pScreenBuffer;
  set_starts(0);
  pCar = &Car[iCarIndex];                       // Get pointer to car data structure
  carDesignIndex = pCar->byCarDesignIdx;        // Get car design index from car data
  iNumCoords = CarDesigns[carDesignIndex].byNumCoords;
  pCarDesign = &CarDesigns[carDesignIndex];
  if (pCar->nCurrChunk == -1 || paused) {
    (iMotionX) = 0;
    (iMotionY) = 0;
    (iMotionZ) = 0;
    //LOWORD(iMotionX) = 0;
    //LOWORD(iMotionY) = 0;
    //LOWORD(iMotionZ) = 0;
  } else {
    iMotionX = pCar->iRollMotion;
    iMotionY = pCar->iPitchMotion;
    iMotionZ = pCar->iYawMotion;
  }
  nRoll = pCar->iRollCameraOffset + iMotionX + pCar->nRoll + pCar->iRollDynamicOffset;
  iYawAngle = ((int16)iMotionZ + pCar->nYaw) & 0x3FFF;
  iPitch = ((uint16)pCar->iPitchCameraOffset + (int16)iMotionY + (uint16)pCar->iPitchDynamicOffset + pCar->nPitch) & 0x3FFF;

  fRotMat00 = tcos[iYawAngle] * tcos[iPitch];   // Calculate 3x3 rotation matrix from car orientation angles
  fRotMat01 = tsin[iYawAngle] * tcos[iPitch];
  dCosYaw = tcos[iYawAngle];
  iYaw = iYawAngle;
  fRotMat02 = tsin[iPitch];
  iRoll = nRoll & 0x3FFF;
  fRotMat10 = (float)dCosYaw * fRotMat02 * tsin[iRoll] - tsin[iYawAngle] * tcos[iRoll];
  fRotMat12 = tsin[iYawAngle] * fRotMat02 * tsin[iRoll] + tcos[iYawAngle] * tcos[iRoll];
  fRotMat21 = -tsin[iRoll] * tcos[iPitch];
  fRotMat20 = -tcos[iYawAngle] * fRotMat02 * tcos[iRoll] - tsin[iYawAngle] * tsin[iRoll];
  fRotMat11 = -tsin[iYawAngle] * fRotMat02 * tcos[iRoll] + tcos[iYawAngle] * tsin[iRoll];
  dRotMat22 = tcos[iPitch] * tcos[iRoll];
  fCarPosX = pCar->pos.fX;
  fCarPosY = pCar->pos.fY;
  fZ = pCar->pos.fZ;
  fRotMat22Copy = (float)dRotMat22;
  fCarPosZ = fZ;
  iCurrChunk = pCar->nCurrChunk;
  uiColorFrom = car_flat_remap[carDesignIndex].uiColorFrom;
  uiColorTo = car_flat_remap[carDesignIndex].uiColorTo;
  pAnms = CarDesigns[carDesignIndex].pAnms;
  if (fDistanceToCar >= 8000.0 && VisibleCars >= 4 || (textures_off & 0x100) != 0 || fDistanceToCar <= 0.0 || (pCar->byStatusFlags & 2) != 0)
    goto LABEL_117;                             // Early exit if car is too far, invisible, or textures disabled
  iChunk = pCar->nCurrChunk;
  if (iChunk == -1)
    iChunk = pCar->iLastValidChunk;
  pTrackData = &localdata[iChunk];
  if (pCar->nCurrChunk == -1)
    getlocalangles(iYaw, iPitch, iRoll, iChunk, &iAzimuth, &iElevation, &iBank);
  else
    iBank = iRoll;
  byCarDesignIdx = pCar->byCarDesignIdx;
  fCarPointZ = CarBox.hitboxAy[0][6].fZ - CarBox.hitboxAy[0][3].fZ;
  if (byCarDesignIdx > 7u)
    fCarPointY = CarBaseY;
  else
    fCarPointY = CarBaseY + -50.0f;
  if (pCar->nCurrChunk == -1)                 // Setup car bounding box corners based on banking angle
  {
    if (iBank > 4096) {
      if (iBank <= 0x2000) {
        carpoint[0].fX = CarBaseX;
        carpoint[0].fY = -fCarPointY;
        carpoint[1].fX = -CarBaseX;
        carpoint[1].fY = carpoint[0].fY;
        carpoint[2].fX = carpoint[1].fX;
        carpoint[0].fZ = fCarPointZ;
        carpoint[1].fZ = fCarPointZ;
        carpoint[2].fZ = 0.0;
        carpoint[3].fZ = 0.0;
        carpoint[2].fY = fCarPointY;
        carpoint[3].fY = fCarPointY;
        carpoint[3].fX = CarBaseX;
        goto LABEL_28;
      }
      if (iBank <= 12288) {
        carpoint[0].fX = CarBaseX;
        carpoint[0].fY = -fCarPointY;
        carpoint[1].fX = -CarBaseX;
        carpoint[1].fY = carpoint[0].fY;
        carpoint[0].fZ = 0.0;
        carpoint[1].fZ = 0.0;
        carpoint[2].fX = carpoint[1].fX;
        carpoint[2].fY = fCarPointY;
        carpoint[2].fZ = fCarPointZ;
        carpoint[3].fY = fCarPointY;
        carpoint[3].fZ = fCarPointZ;
        carpoint[3].fX = CarBaseX;
        goto LABEL_28;
      }
      carpoint[0].fX = CarBaseX;
      carpoint[1].fX = -CarBaseX;
      carpoint[2].fX = carpoint[1].fX;
      carpoint[2].fY = -fCarPointY;
      carpoint[3].fX = CarBaseX;
      dY = carpoint[2].fY;
      carpoint[0].fY = fCarPointY;
      carpoint[0].fZ = 0.0;
      carpoint[1].fY = fCarPointY;
      carpoint[1].fZ = 0.0;
      carpoint[2].fZ = fCarPointZ;
      carpoint[3].fZ = fCarPointZ;
    } else {
      carpoint[0].fX = CarBaseX;
      carpoint[1].fX = -CarBaseX;
      carpoint[2].fX = carpoint[1].fX;
      carpoint[2].fY = -fCarPointY;
      carpoint[3].fX = CarBaseX;
      carpoint[0].fY = fCarPointY;
      dY = carpoint[2].fY;
      carpoint[0].fZ = fCarPointZ;
      carpoint[2].fZ = 0.0;
      carpoint[1].fY = fCarPointY;
      carpoint[3].fZ = 0.0;
      carpoint[1].fZ = fCarPointZ;
    }
  } else {
    carpoint[0].fX = CarBaseX;
    carpoint[1].fX = -CarBaseX;
    carpoint[2].fX = carpoint[1].fX;
    carpoint[2].fY = -fCarPointY;
    carpoint[3].fX = CarBaseX;
    dY = carpoint[2].fY;
    carpoint[0].fY = fCarPointY;
    carpoint[0].fZ = 0.0;
    carpoint[1].fY = fCarPointY;
    carpoint[1].fZ = 0.0;
    carpoint[2].fZ = 0.0;
    carpoint[3].fZ = 0.0;
  }
  carpoint[3].fY = (float)dY;
LABEL_28:
  if (pCar->nCurrChunk == -1) {
    p_fX = &pCar->pos.fX;
    uiCarWorldOffset = 0;
    dTransformMat22 = fRotMat22Copy;
    dTransformMat02 = fRotMat02;
    dTransformMat12 = fRotMat21;
    dTransformMat11 = fRotMat11;
    dTransformMat01 = fRotMat01;
    do {
      carworld[uiCarWorldOffset / 3].fX = (float)(carpoint[uiCarWorldOffset / 3].fY * fRotMat10
        + carpoint[uiCarWorldOffset / 3].fX * fRotMat00
        + carpoint[uiCarWorldOffset / 3].fZ * fRotMat20
        + *p_fX);// Transform car corner points from local to world coordinates
      carworld[uiCarWorldOffset / 3].fY = (float)(carpoint[uiCarWorldOffset / 3].fY * fRotMat12
        + carpoint[uiCarWorldOffset / 3].fX * dTransformMat01
        + carpoint[uiCarWorldOffset / 3].fZ * dTransformMat11
        + p_fX[1]);
      dTransformMat21 = carpoint[uiCarWorldOffset / 3].fY * dTransformMat12
        + carpoint[uiCarWorldOffset / 3].fX * dTransformMat02
        + carpoint[uiCarWorldOffset / 3].fZ * dTransformMat22
        + p_fX[2];
      carworld[uiCarWorldOffset / 3].fZ = (float)dTransformMat21;
      uiCarWorldOffset += 3;
      //wConsoleNode_variable_1[uiCarWorldOffset] = dTransformMat21;// offset into carworld
    } while (uiCarWorldOffset != 12);
    for (i = 0; i != 4; ++i)// carpoint[i + 3].fZ = dLocalZ)// offset into carlocal
    {
      carlocal[i].fX = (carworld[i].fY + pTrackData->pointAy[3].fY) * pTrackData->pointAy[1].fX
        + (carworld[i].fX + pTrackData->pointAy[3].fX) * pTrackData->pointAy[0].fX
        + (carworld[i].fZ + pTrackData->pointAy[3].fZ) * pTrackData->pointAy[2].fX;
      carlocal[i].fY = (carworld[i].fY + pTrackData->pointAy[3].fY) * pTrackData->pointAy[1].fY
        + (carworld[i].fX + pTrackData->pointAy[3].fX) * pTrackData->pointAy[0].fY
        + (carworld[i].fZ + pTrackData->pointAy[3].fZ) * pTrackData->pointAy[2].fY;
      dLocalZ = (carworld[i].fY + pTrackData->pointAy[3].fY) * pTrackData->pointAy[1].fZ
        + (carworld[i].fX + pTrackData->pointAy[3].fX) * pTrackData->pointAy[0].fZ
        + (carworld[i].fZ + pTrackData->pointAy[3].fZ) * pTrackData->pointAy[2].fZ;
      carlocal[i].fZ = (float)dLocalZ;
      //++i;                                      // replace carpoint assignment with this in for loop
    }
  } else {
    pCarPosPtr = &pCar->pos.fX;
    iCornerIndex = 0;
    dTransformMat22_2 = fRotMat22Copy;
    dTransformMat02_2 = fRotMat02;
    dTransformMat12_2 = fRotMat21;
    dTransformMat11_2 = fRotMat11;
    dTransformMat01_2 = fRotMat01;
    do {
      carlocal[iCornerIndex].fX = fRotMat10 * carpoint[iCornerIndex].fY + fRotMat00 * carpoint[iCornerIndex].fX + fRotMat20 * carpoint[iCornerIndex].fZ + *pCarPosPtr;
      carlocal[iCornerIndex].fY = (float)(fRotMat12 * carpoint[iCornerIndex].fY
        + dTransformMat01_2 * carpoint[iCornerIndex].fX
        + dTransformMat11_2 * carpoint[iCornerIndex].fZ
        + pCarPosPtr[1]);
      dTransformMat21_2 = dTransformMat12_2 * carpoint[iCornerIndex].fY
        + dTransformMat02_2 * carpoint[iCornerIndex].fX
        + dTransformMat22_2 * carpoint[iCornerIndex].fZ
        + pCarPosPtr[2];
      carlocal[iCornerIndex].fZ = (float)dTransformMat21_2;
      ++iCornerIndex;
      //carpoint[++iCornerIndex + 3].fZ = dTransformMat21_2;// offset into carlocal
    } while (iCornerIndex != 4);
  }
  nCurrChunk = pCar->nCurrChunk;
  pTrackInfo = &TrackInfo[iChunk];
  if (nCurrChunk == -1) {
    fLocalCarPosX = fCarPosX;
    fLocalCarPosY = fCarPosY;
    fLocalCarPosZ = fCarPosZ;
  } else {
    pCurrentTrackData = &localdata[nCurrChunk];
    fLocalCarPosX = pCurrentTrackData->pointAy[0].fY * fCarPosY
      + pCurrentTrackData->pointAy[0].fX * fCarPosX
      + pCurrentTrackData->pointAy[0].fZ * fCarPosZ
      - pCurrentTrackData->pointAy[3].fX;
    fLocalCarPosY = pCurrentTrackData->pointAy[1].fX * fCarPosX
      + pCurrentTrackData->pointAy[1].fY * fCarPosY
      + pCurrentTrackData->pointAy[1].fZ * fCarPosZ
      - pCurrentTrackData->pointAy[3].fY;
    fLocalCarPosZ = fCarPosY * pCurrentTrackData->pointAy[2].fY
      + fCarPosX * pCurrentTrackData->pointAy[2].fX
      + fCarPosZ * pCurrentTrackData->pointAy[2].fZ
      - pCurrentTrackData->pointAy[3].fZ;
  }
  dRelativeX = fLocalCarPosX + pTrackData->pointAy[3].fX;
  dRelativeY = fLocalCarPosY + pTrackData->pointAy[3].fY;
  dRelativeZ = fLocalCarPosZ + pTrackData->pointAy[3].fZ;
  fTrackRelativeY = (float)(pTrackData->pointAy[0].fY * dRelativeX + pTrackData->pointAy[1].fY * dRelativeY + pTrackData->pointAy[2].fY * dRelativeZ);
  fTrackRelativeZ = (float)(dRelativeX * pTrackData->pointAy[0].fZ + dRelativeY * pTrackData->pointAy[1].fZ + dRelativeZ * pTrackData->pointAy[2].fZ);
  iTunnelFlag = 0;
  if (fTrackRelativeY <= 0.0) {
    if (-pTrackData->fTrackHalfWidth < fTrackRelativeY) {
      iRightSurfaceType = pTrackInfo->iRightSurfaceType;
      if ((iRightSurfaceType == 5 || iRightSurfaceType == 6 || iRightSurfaceType == 9) && fTrackRelativeZ < (double)pTrackInfo->fRoofHeight)
        goto LABEL_51;
    }
  } else if (fTrackRelativeY < (double)pTrackData->fTrackHalfWidth) {
    iLeftSurfaceType = pTrackInfo->iLeftSurfaceType;
    if ((iLeftSurfaceType == 5 || iLeftSurfaceType == 6 || iLeftSurfaceType == 9) && fTrackRelativeZ < (double)pTrackInfo->fRoofHeight)
      LABEL_51:
    iTunnelFlag = -1;
  }
  iLaneType = pCar->iLaneType;
  iShadowRenderFlag = -1;
  iGroundedCorners = 0;
  //ullTrackColor = TrakColour[iChunk][iLaneType];
  // SURFACE_FLAG_SKIP_RENDER
  if ((TrakColour[iChunk][iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0 && GroundColour[iChunk][2] < 0)
  //if ((((HIDWORD(ullTrackColor) ^ (unsigned int)ullTrackColor) - HIDWORD(ullTrackColor)) & 0x20000) != 0 && GroundColour[iChunk][2] < 0)
    iShadowRenderFlag = 0;
  iGroundColorType = GroundColour[iChunk][2];
  if (iGroundColorType == -2) {
    iUndergroundCheck = iTunnelFlag;
    iUndergroundIdx = 0;
    while (1) {
      if (pTrackData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth < carlocal[iUndergroundIdx].fY
        || -pTrackData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth > carlocal[iUndergroundIdx].fY) {
        if (!iUndergroundCheck) {
          dUndergroundHeight = getbankz(carlocal[iUndergroundIdx].fY, iChunk, 0);
          goto LABEL_101;
        }
      } else if (!iTunnelFlag) {
        dUndergroundHeight = getgroundz(carlocal[iUndergroundIdx].fX, carlocal[iUndergroundIdx].fY, iChunk);
        goto LABEL_101;
      }
      dUndergroundHeight = getroadz(carlocal[iUndergroundIdx].fX, carlocal[iUndergroundIdx].fY, iChunk);
    LABEL_101:
      fGroundHeightTemp = (float)dUndergroundHeight;
      if (fGroundHeightTemp >= (double)carlocal[iUndergroundIdx].fZ)
        ++iGroundedCorners;
      else
        carlocal[iUndergroundIdx].fZ = fGroundHeightTemp;
      if (++iUndergroundIdx == 4)
        goto LABEL_105;
    }
  }
  iOnTrackFlag = 0;                             // Calculate ground/road height at car corner positions
  if (iGroundColorType == -1)
    iOnTrackFlag = -1;
  iCornerIdx = 0;
  iHeightIdx = 0;
  iChunkOffset = 24 * iChunk;
  do {
    if (iTunnelFlag)
      dGroundHeight = getroadz(carlocal[iCornerIdx].fX, carlocal[iCornerIdx].fY, iChunk);
    else
      dGroundHeight = getgroundz(carlocal[iCornerIdx].fX, carlocal[iCornerIdx].fY, iChunk);
    roadheight[iHeightIdx] = (float)dGroundHeight;
    if (pTrackData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth >= carlocal[iCornerIdx].fY
      && -pTrackData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth <= carlocal[iCornerIdx].fY) {
      //iTemp = 4 * pCar->iLaneType;
      // SURFACE_FLAG_SKIP_RENDER
      if ((TrakColour[iChunk][pCar->iLaneType] & SURFACE_FLAG_SKIP_RENDER) == 0 && roadheight[iHeightIdx] + -400.0 <= carlocal[iCornerIdx].fZ)
        iOnTrackFlag = -1;
    }
    ++iHeightIdx;
    ++iCornerIdx;
  } while (iHeightIdx != 4);
  if (iOnTrackFlag) {
    iCornerLoopIdx = 0;
    for (j = 0; j != 4; ++j) {
      fGroundHeightLoop = roadheight[j];
      if (fGroundHeightLoop >= (double)carlocal[iCornerLoopIdx].fZ)
        ++iGroundedCorners;
      else
        carlocal[iCornerLoopIdx].fZ = fGroundHeightLoop;
      ++iCornerLoopIdx;
    }
  } else if (GroundColour[iChunk][2] < 2 || pCar->iControlType == 3) {
    for (k = 0; k != 4; ++k) {
      if (iTunnelFlag)
        dBankHeight2 = getroadz(carlocal[k].fX, carlocal[k].fY, iChunk);
      else
        dBankHeight2 = getbankz(carlocal[k].fY, iChunk, 0);
      fBankHeightLoop2 = (float)dBankHeight2;
      if (fBankHeightLoop2 >= (double)carlocal[k].fZ)
        ++iGroundedCorners;
      else
        carlocal[k].fZ = fBankHeightLoop2;
    }
  } else {
    pTrackData = &tempData;
    calculateseparatedcoordinatesystem(iChunk, &tempData);
    for (m = 0; m != 4; ++m)//carpoint[m + 3].fZ = fCoordZ)// offset into carlocal
    {
      carlocal[m].fX = (carworld[m].fY + tempData.pointAy[3].fY) * tempData.pointAy[1].fX
        + (carworld[m].fX + tempData.pointAy[3].fX) * tempData.pointAy[0].fX
        + (carworld[m].fZ + tempData.pointAy[3].fZ) * tempData.pointAy[2].fX;
      carlocal[m].fY = (carworld[m].fY + tempData.pointAy[3].fY) * tempData.pointAy[1].fY
        + (carworld[m].fX + tempData.pointAy[3].fX) * tempData.pointAy[0].fY
        + (carworld[m].fZ + tempData.pointAy[3].fZ) * tempData.pointAy[2].fY;
      dCoordZ = (carworld[m].fY + tempData.pointAy[3].fY) * tempData.pointAy[1].fZ
        + (carworld[m].fX + tempData.pointAy[3].fX) * tempData.pointAy[0].fZ
        + (carworld[m].fZ + tempData.pointAy[3].fZ) * tempData.pointAy[2].fZ;
      carlocal[m].fZ = (float)dCoordZ;
      //++m;                                      // replace carpoint assignment with this in for loop
    }
    for (n = 0; n != 4; ++n) {
      dBankHeight = getbankz(carlocal[n].fY, iChunk, &tempData);
      fBankHeightLoop = (float)dBankHeight;
      if (dBankHeight < carlocal[n].fZ)
        carlocal[n].fZ = fBankHeightLoop;
    }
  }
LABEL_105:
  if (iShadowRenderFlag && pCar->iControlType != 3 && iGroundedCorners >= 2)
    iShadowRenderFlag = 0;
  if (iShadowRenderFlag) {
    for (iShadowOffset = 0; iShadowOffset != 48; iShadowOffset += 0xC)// wConsoleNode_variable_1[iShadowOffset / 4u] = dWorldToLocalZ)// offset into carworld
    {
      carworld[iShadowOffset / 0xCu].fX = pTrackData->pointAy[0].fY * carlocal[iShadowOffset / 0xCu].fY
        + pTrackData->pointAy[0].fX * carlocal[iShadowOffset / 0xCu].fX
        + pTrackData->pointAy[0].fZ * carlocal[iShadowOffset / 0xCu].fZ
        - pTrackData->pointAy[3].fX;
      carworld[iShadowOffset / 0xCu].fY = pTrackData->pointAy[1].fY * carlocal[iShadowOffset / 0xCu].fY
        + pTrackData->pointAy[1].fX * carlocal[iShadowOffset / 0xCu].fX
        + pTrackData->pointAy[1].fZ * carlocal[iShadowOffset / 0xCu].fZ
        - pTrackData->pointAy[3].fY;
      dWorldToLocalZ = pTrackData->pointAy[2].fY * carlocal[iShadowOffset / 0xCu].fY
        + pTrackData->pointAy[2].fX * carlocal[iShadowOffset / 0xCu].fX
        + pTrackData->pointAy[2].fZ * carlocal[iShadowOffset / 0xCu].fZ
        - pTrackData->pointAy[3].fZ;
      carworld[iShadowOffset / 0xC].fZ = (float)dWorldToLocalZ;
      //iShadowOffset += 0xC;                     // replace consolenode assign with this in for loop
    }
    iScreenSize = scr_size;
    for (ii = 0; ii != 4; ++ii)//carworld[ii + 3].fY = (float)iTemp)// offset into carpoint
    {
      dViewDeltaX = carworld[ii].fX - viewx;    // Render car shadow as flat polygon on ground
      dViewDeltaY = carworld[ii].fY - viewy;
      dViewDeltaZ = carworld[ii].fZ - viewz;
      fShadowViewX = (float)(dViewDeltaX * vk1 + dViewDeltaY * vk4 + dViewDeltaZ * vk7);
      fShadowViewY = (float)(dViewDeltaX * vk2 + dViewDeltaY * vk5 + dViewDeltaZ * vk8);
      dViewSpaceZ = dViewDeltaX * vk3 + dViewDeltaY * vk6 + dViewDeltaZ * vk9;
      fClampedZ = (float)dViewSpaceZ;
      if (dViewSpaceZ < 80.0)
        fClampedZ = 80.0;
      dViewDistance = (double)VIEWDIST;
      dInverseZ = 1.0 / fClampedZ;
      dScreenX = dViewDistance * fShadowViewX * dInverseZ + (double)xbase;
      //_CHP();
      if (!isnan(dScreenX)) { //check added by ROLLER
        xp = (int)dScreenX;
        dScreenY = dInverseZ * (dViewDistance * fShadowViewY) + (double)ybase;
        //_CHP();
        yp = (int)dScreenY;
        carpoint[ii].fX = (float)(xp * iScreenSize >> 6);
        //carpoint[ii].fX = (float)(iScreenXInt >> 6);
        iTemp = (iScreenSize * (199 - (int)dScreenY)) >> 6;
        carpoint[ii].fY = (float)iTemp;
      }
      //++ii;                                     // replace reference to carworld in for loop with this
    }
    dShadowX = carpoint[0].fX;
    //_CHP();
    CarPol.vertices[0].x = (int)dShadowX;
    dShadowCorner0X = carpoint[0].fY;
    //_CHP();
    CarPol.vertices[0].y = (int)dShadowCorner0X;
    dX = carpoint[1].fX;
    //_CHP();
    CarPol.vertices[1].x = (int)dX;
    dShadowCorner1Y = carpoint[1].fY;
    //_CHP();
    CarPol.vertices[1].y = (int)dShadowCorner1Y;
    dShadowCorner2X = carpoint[2].fX;
    //_CHP();
    CarPol.vertices[2].x = (int)dShadowCorner2X;
    dShadowCorner2Y = carpoint[2].fY;
    //_CHP();
    CarPol.vertices[2].y = (int)dShadowCorner2Y;
    dShadowCorner3X = carpoint[3].fX;
    //_CHP();
    CarPol.vertices[3].x = (int)dShadowCorner3X;
    dShadowCorner3Y = carpoint[3].fY;
    //_CHP();
    CarPol.uiNumVerts = 4;
    CarPol.iSurfaceType = 0x202002;
    CarPol.vertices[3].y = (int)dShadowCorner3Y;
    POLYFLAT(pScrBuf, &CarPol);
  }
LABEL_117:
  pCoords = CarDesigns[carDesignIndex].pCoords;
  pPols = CarDesigns[carDesignIndex].pPols;
  if ((Car[iCarIndexCopy].byStatusFlags & 2) != 0) {
    pCarDesign = NULL;
    iNumCoords = 0;
  }
  fWarpDistortion = tsin[((uint16)warp_angle + ((uint16)iCarIndexCopy << 11)) & 0x3FFF] * 0.5f;
  if (iNumCoords > 0)                         // Process 3D car model coordinates and transform to screen space
  {
    iCoordOffset = 0;
    iCoordLoopSize = 32 * iNumCoords;
    //iCurrChunk bounds check added by ROLLER
    if (iCurrChunk >= 0 && iCurrChunk < TRAK_LEN)
      pChunkData = &localdata[iCurrChunk];
    else
      pChunkData = NULL;

    do {
      fModelCoordX = pCoords->fX;
      pCoordPtr = &pCoords->fY;
      fModelCoordXLocal = fModelCoordX;
      fModelCoordY = *pCoordPtr++;              // increment to Z pos
      fModelCoordYLocal = fModelCoordY;
      fModelCoordZ = *pCoordPtr;
      pCoords = (tVec3 *)(pCoordPtr + 1);       // increment to next coord
      fModelCoordZLocal = fModelCoordZ;
      // CHEAT_MODE_TINY_CARS
      if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0)         // Apply CHEAT_MODE_TINY_CARS scaling if enabled
      {
        fModelCoordXLocal = fModelCoordXLocal * 0.25f;
        fModelCoordYLocal = 0.25f * fModelCoordYLocal;
      }
      // CHEAT_MODE_WARP
      if ((cheat_mode & CHEAT_MODE_WARP) != 0)        // Apply CHEAT_MODE_WARP distortion effect if enabled
      {
        fModelCoordXLocal = fModelCoordXLocal * fWarpDistortion + fModelCoordXLocal;
        fModelCoordYLocal = fModelCoordYLocal * fWarpDistortion + fModelCoordYLocal;
        fModelCoordZLocal = fWarpDistortion * fModelCoordZLocal + fModelCoordZLocal;
      }
      dTransformedX = fModelCoordXLocal;
      dTransformedY = fModelCoordYLocal;
      dTransformedZ = fModelCoordZLocal;
      CarPt[iCoordOffset / 0x20].world.fX = fModelCoordXLocal * fRotMat00 + fModelCoordYLocal * fRotMat10 + fModelCoordZLocal * fRotMat20 + fCarPosX;
      CarPt[iCoordOffset / 0x20].world.fY = (float)(dTransformedX * fRotMat01 + dTransformedY * fRotMat12 + dTransformedZ * fRotMat11 + fCarPosY);
      iCurrentChunk = iCurrChunk;
      CarPt[iCoordOffset / 0x20].world.fZ = (float)(dTransformedX * fRotMat02 + dTransformedY * fRotMat21 + dTransformedZ * fRotMat22Copy + fCarPosZ);
      if (iCurrentChunk != -1) {
        dChunkTransformY = pChunkData->pointAy[0].fY;
        fModelWorldX = CarPt[iCoordOffset / 0x20].world.fX;
        fModelWorldY = CarPt[iCoordOffset / 0x20].world.fY;
        dChunkTransformX = dChunkTransformY * fModelWorldY + pChunkData->pointAy[0].fX * fModelWorldX;
        dChunkMatrixZ = pChunkData->pointAy[0].fZ;
        fModelWorldZ = CarPt[iCoordOffset / 0x20].world.fZ;
        CarPt[iCoordOffset / 0x20].world.fX = (float)(dChunkTransformX + dChunkMatrixZ * fModelWorldZ - pChunkData->pointAy[3].fX);
        CarPt[iCoordOffset / 0x20].world.fY = pChunkData->pointAy[1].fX * fModelWorldX
          + pChunkData->pointAy[1].fY * fModelWorldY
          + pChunkData->pointAy[1].fZ * fModelWorldZ
          - pChunkData->pointAy[3].fY;
        dChunkTransformZ = pChunkData->pointAy[2].fX * fModelWorldX
          + pChunkData->pointAy[2].fY * fModelWorldY
          + pChunkData->pointAy[2].fZ * fModelWorldZ
          - pChunkData->pointAy[3].fZ;
        pChunkDataPtr = (float *)pChunkData;
        CarPt[iCoordOffset / 0x20].world.fZ = (float)dChunkTransformZ;
      }
      dViewRelativeX = CarPt[iCoordOffset / 0x20].world.fX - viewx;
      dViewRelativeY = CarPt[iCoordOffset / 0x20].world.fY - viewy;
      dViewRelativeZ = CarPt[iCoordOffset / 0x20].world.fZ - viewz;
      fModelViewX = (float)(dViewRelativeX * vk1 + dViewRelativeY * vk4 + dViewRelativeZ * vk7);
      fModelViewY = (float)(dViewRelativeX * vk2 + dViewRelativeY * vk5 + dViewRelativeZ * vk8);
      dCameraSpaceZ = dViewRelativeX * vk3 + dViewRelativeY * vk6 + dViewRelativeZ * vk9;
      fModelClampedZ = (float)dCameraSpaceZ;
      fModelCameraZCopy = fModelClampedZ;
      if (dCameraSpaceZ < 80.0)
        fModelClampedZ = 80.0;
      dProjViewDistance = (double)VIEWDIST;
      dProjInverseZ = 1.0 / fModelClampedZ;
      dProjScreenX = dProjViewDistance * fModelViewX * dProjInverseZ + (double)xbase;
      //_CHP();
      xp = (int)dProjScreenX;
      dProjScreenY = dProjInverseZ * (dProjViewDistance * fModelViewY) + (double)ybase;
      //_CHP();
      yp = (int)dProjScreenY;
      CarPt[iCoordOffset / 0x20].screen.x = xp * scr_size >> 6;
      //CarPt[iCoordOffset / 0x20].screen.x = iScreenXResult >> 6;
      CarPt[iCoordOffset / 0x20].screen.y = (scr_size * (199 - yp)) >> 6;
      CarPt[iCoordOffset / 0x20].view.fX = fModelViewX;
      CarPt[iCoordOffset / 0x20].view.fY = fModelViewY;
      CarPt[iCoordOffset / 0x20].view.fZ = fModelCameraZCopy;
      iCoordOffset += 32;

      // All these are offsets into CarPt
      //*(_DWORD *)((char *)&carlocal[3].fX + iCoordOffset) = (scr_size * (199 - yp)) >> 6;
      //roadheight[iCoordOffset / 4 + 1] = fModelViewX;
      //roadheight[iCoordOffset / 4 + 2] = fModelViewY;
      //roadheight[iCoordOffset / 4 + 3] = fModelCameraZCopy;
    } while ((int)iCoordOffset < iCoordLoopSize);
  }
  iVisiblePolygons = 0;
  iPolygonCount = 0;
  if (pCarDesign && (int)pCarDesign->byNumPols > 0) {
    iPolygonLoopIdx = 0;
    do {
      for (jj = 0; jj < 4; ++jj) {
        polygonVertices[jj] = &CarPt[pPols->verts[jj]];
      }
      //pCurrentPolygon = (uint8 *)pPols;         // ->verts[0]
      //for (jj = 0; jj != 16; *(_DWORD *)((char *)&tempData.fAIMaxSpeed + jj) = pCarVertex)// reference into polygonVertices
      //{
      //  jj += 4;
      //  pCarVertex = &CarPt[*pCurrentPolygon++];
      //}

      dCrossProduct1X = polygonVertices[2]->view.fX - polygonVertices[0]->view.fX;// Test polygon visibility using cross product (backface culling)
      fCrossProductY = polygonVertices[2]->view.fY - polygonVertices[0]->view.fY;
      dCrossProduct1Z = polygonVertices[2]->view.fZ - polygonVertices[0]->view.fZ;
      fPolygonVertex1X = polygonVertices[1]->view.fX;
      fPolygonVertex1Y = polygonVertices[1]->view.fY;
      fPolygonVertex1Z = polygonVertices[1]->view.fZ;
      dCrossProduct2X = fPolygonVertex1X - polygonVertices[3]->view.fX;
      dCrossProduct2Y = fPolygonVertex1Y - polygonVertices[3]->view.fY;
      dCrossProduct2Z = fPolygonVertex1Z - polygonVertices[3]->view.fZ;
      //byTextureFlags = BYTE1(pPols->uiTex);
      fBackfaceDotProduct = (float)((fCrossProductY * dCrossProduct2Z - dCrossProduct1Z * dCrossProduct2Y) * fPolygonVertex1X
        + (dCrossProduct1Z * dCrossProduct2X - dCrossProduct2Z * dCrossProduct1X) * fPolygonVertex1Y
        + (dCrossProduct2Y * dCrossProduct1X - dCrossProduct2X * fCrossProductY) * fPolygonVertex1Z);
// SURFACE_FLAG_FLIP_BACKFACE
      if ((pPols->uiTex & SURFACE_FLAG_FLIP_BACKFACE) != 0 || fBackfaceDotProduct <= 0.0) {
      //if ((byTextureFlags & 0x20) != 0 || fBackfaceDotProduct <= 0.0) {
        // SURFACE_FLAG_BACK
        if (fBackfaceDotProduct > 0.0 && (pPols->uiTex & SURFACE_FLAG_BACK) != 0)
          iPolygonResult = -iPolygonCount - 1;
        else
          iPolygonResult = iPolygonCount;
        CarZOrder[iPolygonLoopIdx].iPolygonIndex = iPolygonResult;
        CarZOrder[iPolygonLoopIdx].iPolygonLink = pPols->nNextPolIdx;
        // CAR_FLAG_ANMS_LIVERY
        if ((pPols->uiTex & CAR_FLAG_ANMS_LIVERY) == 0) {
          if (CarPt[pPols->verts[2]].view.fZ <= (double)CarPt[pPols->verts[3]].view.fZ)
            fZDepthMax1 = CarPt[pPols->verts[3]].view.fZ;
          else
            fZDepthMax1 = CarPt[pPols->verts[2]].view.fZ;
          fZDepthFarMax1 = fZDepthMax1;
          if (CarPt[pPols->verts[0]].view.fZ <= (double)CarPt[pPols->verts[1]].view.fZ)
            fZDepthMax2 = CarPt[pPols->verts[1]].view.fZ;
          else
            fZDepthMax2 = CarPt[pPols->verts[0]].view.fZ;
          fZDepthFarMax2 = fZDepthMax2;
          if (fZDepthMax2 <= (double)fZDepthFarMax1) {
            if (CarPt[pPols->verts[2]].view.fZ <= (double)CarPt[pPols->verts[3]].view.fZ)
              fZDepthFinal = CarPt[pPols->verts[3]].view.fZ;
            else
              fZDepthFinal = CarPt[pPols->verts[2]].view.fZ;
            fZDepthFarMin2 = fZDepthFinal;
          } else {
            if (CarPt[pPols->verts[0]].view.fZ <= (double)CarPt[pPols->verts[1]].view.fZ)
              fZDepthFinal = CarPt[pPols->verts[1]].view.fZ;
            else
              fZDepthFinal = CarPt[pPols->verts[0]].view.fZ;
            fZDepthFarMin1 = fZDepthFinal;
          }
          fZDepthFarFinal = fZDepthFinal;
        } else {
          if (CarPt[pPols->verts[2]].view.fZ >= (double)CarPt[pPols->verts[3]].view.fZ)
            fZDepthMin1 = CarPt[pPols->verts[3]].view.fZ;
          else
            fZDepthMin1 = CarPt[pPols->verts[2]].view.fZ;
          fZDepthCloseMin1 = fZDepthMin1;
          if (CarPt[pPols->verts[0]].view.fZ >= (double)CarPt[pPols->verts[1]].view.fZ)
            fZDepthMin2 = CarPt[pPols->verts[1]].view.fZ;
          else
            fZDepthMin2 = CarPt[pPols->verts[0]].view.fZ;
          fZDepthCloseMin2 = fZDepthMin2;
          if (fZDepthMin2 >= (double)fZDepthCloseMin1) {
            if (CarPt[pPols->verts[2]].view.fZ >= (double)CarPt[pPols->verts[3]].view.fZ)
              fZDepthFinal = CarPt[pPols->verts[3]].view.fZ;
            else
              fZDepthFinal = CarPt[pPols->verts[2]].view.fZ;
            fZDepthCloseFar2 = fZDepthFinal;
          } else {
            if (CarPt[pPols->verts[0]].view.fZ >= (double)CarPt[pPols->verts[1]].view.fZ)
              fZDepthFinal = CarPt[pPols->verts[1]].view.fZ;
            else
              fZDepthFinal = CarPt[pPols->verts[0]].view.fZ;
            fZDepthCloseFar1 = fZDepthFinal;
          }
          fZDepthCloseFinal = fZDepthFinal;
        }
        CarZOrder[iPolygonLoopIdx++].fZDepth = fZDepthFinal;
        ++iVisiblePolygons;
      }
      ++pPols;
      ++iPolygonCount;
    } while (iPolygonCount < (int)pCarDesign->byNumPols);
  }
  if (iVisiblePolygons > 0) {
    iZOrderIndex = 0;
    uiCarDesignStride = 28 * carDesignIndex;
    pPolygonBase = CarDesigns[carDesignIndex].pPols;
    uiZOrderLoopOffset = 0;
    pPolygonArray = pPolygonBase;
    do {
      iPolygonIndex = CarZOrder[uiZOrderLoopOffset / 0xC].iPolygonIndex;
      if (iPolygonIndex < 0)
        iPolygonIndex = -1 - iPolygonIndex;
      nNextPolIdx = pPolygonArray[iPolygonIndex].nNextPolIdx;
      iDepthSortIdx = iZOrderIndex;
      if (nNextPolIdx >= 0) {
        pLinkedPolygons = CarDesigns[uiCarDesignStride / 0x1C].pPols;
        do {
          iLinkedPolygonIdx = -1;
          for (kk = 0; kk < iVisiblePolygons; ++kk) {
            iZOrderCompareIdx = CarZOrder[kk].iPolygonIndex;
            if (iZOrderCompareIdx < 0)
              iZOrderCompareIdx = -1 - iZOrderCompareIdx;
            if (nNextPolIdx == iZOrderCompareIdx) {
              iLinkedPolygonIdx = kk;
              kk = iVisiblePolygons;
            }
          }
          if (iLinkedPolygonIdx > 0 && iDepthSortIdx > 0)//depth sort check added by ROLLER
            CarZOrder[iLinkedPolygonIdx].fZDepth = CarZOrder[iDepthSortIdx].fZDepth + -1.0f;
          nNextPolIdx = pLinkedPolygons[nNextPolIdx].nNextPolIdx;
          iDepthSortIdx = iLinkedPolygonIdx;
        } while (nNextPolIdx >= 0);
      }
      uiZOrderLoopOffset += 12;
      ++iZOrderIndex;
    } while (iVisiblePolygons > iZOrderIndex);
  }
  if (iVisiblePolygons > 0 || (Car[iCarIndexCopy].byStatusFlags & 2) != 0) {
    iSpriteIdx = 0;

    if (iCurrChunk >= 0 && iCurrChunk < TRAK_LEN)// bounds check added by ROLLER
      pTrackChunkPtr = (float *)&localdata[iCurrChunk];
    else
      pTrackChunkPtr = NULL;

    pCarSprayArray = CarSpray[iCarIndexCopy];
    pSmokeSpray = pCarSprayArray;
    iSmokeIndex = 0;
    iSmokeZOrderIdx = iVisiblePolygons;
    do {                                           // Process and render car smoke/spray particle effects
      if (pSmokeSpray->iLifeTime > 0) {
        fPosX = pSmokeSpray->position.fX;
        fPosY = pSmokeSpray->position.fY;
        fPosZ = pSmokeSpray->position.fZ;
        SmokePt[0][iSmokeIndex].world.fX = fPosX * fRotMat00 + fPosY * fRotMat10 + fPosZ * fRotMat20 + fCarPosX;
        SmokePt[0][iSmokeIndex].world.fY = fPosX * fRotMat01 + fPosY * fRotMat12 + fPosZ * fRotMat11 + fCarPosY;
        iSmokeChunkIdx = iCurrChunk;
        SmokePt[0][iSmokeIndex].world.fZ = fPosX * fRotMat02 + fPosY * fRotMat21 + fPosZ * fRotMat22Copy + fCarPosZ;
        if (iSmokeChunkIdx != -1 && pTrackChunkPtr) {
          dSmokeChunkTransY = pTrackChunkPtr[1];
          fSmokeWorldX = SmokePt[0][iSmokeIndex].world.fX;
          fSmokeWorldY = SmokePt[0][iSmokeIndex].world.fY;
          dSmokeChunkTransX = dSmokeChunkTransY * fSmokeWorldY + *pTrackChunkPtr * fSmokeWorldX;
          dSmokeChunkMatZ = pTrackChunkPtr[2];
          //iTemp = LODWORD(SmokePt[0][iSmokeIndex].world.fZ);
          float fSmokeWorldZ = SmokePt[0][iSmokeIndex].world.fZ;
          SmokePt[0][iSmokeIndex].world.fX = (float)(dSmokeChunkTransX + dSmokeChunkMatZ * fSmokeWorldZ - pTrackChunkPtr[9]);
          SmokePt[0][iSmokeIndex].world.fY = pTrackChunkPtr[3] * fSmokeWorldX + pTrackChunkPtr[4] * fSmokeWorldY + pTrackChunkPtr[5] * fSmokeWorldZ - pTrackChunkPtr[10];
          dSmokeChunkTransZ = pTrackChunkPtr[6] * fSmokeWorldX + pTrackChunkPtr[7] * fSmokeWorldY + pTrackChunkPtr[8] * fSmokeWorldZ - pTrackChunkPtr[11];
          pChunkDataPtr = pTrackChunkPtr;
          SmokePt[0][iSmokeIndex].world.fZ = (float)dSmokeChunkTransZ;
        }
        dSmokeViewDeltaX = SmokePt[0][iSmokeIndex].world.fX - viewx;
        dSmokeViewDeltaY = SmokePt[0][iSmokeIndex].world.fY - viewy;
        dSmokeViewDeltaZ = SmokePt[0][iSmokeIndex].world.fZ - viewz;
        fSmokeViewX = (float)(dSmokeViewDeltaX * vk1 + dSmokeViewDeltaY * vk4 + dSmokeViewDeltaZ * vk7);
        fSmokeViewY = (float)(dSmokeViewDeltaX * vk2 + dSmokeViewDeltaY * vk5 + dSmokeViewDeltaZ * vk8);
        dSmokeCameraZ = dSmokeViewDeltaX * vk3 + dSmokeViewDeltaY * vk6 + dSmokeViewDeltaZ * vk9;
        fSmokeClampedZ = (float)dSmokeCameraZ;
        fSmokeCameraZCopy = fSmokeClampedZ;
        if (dSmokeCameraZ < 80.0)
          fSmokeClampedZ = 80.0;
        dSmokeProjViewDist = (double)VIEWDIST;
        dSmokeProjInverseZ = 1.0 / fSmokeClampedZ;
        dSmokeProjScreenX = dSmokeProjViewDist * fSmokeViewX * dSmokeProjInverseZ + (double)xbase;
        //_CHP();
        xp = (int)dSmokeProjScreenX;
        dSmokeProjScreenY = dSmokeProjInverseZ * (dSmokeProjViewDist * fSmokeViewY) + (double)ybase;
        //_CHP();
        yp = (int)dSmokeProjScreenY;
        SmokePt[0][iSmokeIndex].screen.x = xp * scr_size >> 6;
        //SmokePt[0][iSmokeIndex].screen.x = iSmokeScreenXResult >> 6;
        SmokePt[0][iSmokeIndex].screen.y = (scr_size * (199 - yp)) >> 6;
        SmokePt[0][iSmokeIndex].view.fX = fSmokeViewX;
        SmokePt[0][iSmokeIndex].view.fY = fSmokeViewY;
        SmokePt[0][iSmokeIndex].view.fZ = fSmokeCameraZCopy;
        if ((uint8)(pSmokeSpray->iType) == 1) {
          fPosX = fPosX + pSmokeSpray->velocity.fX;
          fPosY = fPosY + pSmokeSpray->velocity.fY;
          fPosZ = fPosZ + pSmokeSpray->velocity.fZ;
          SmokePt[1][iSmokeIndex].world.fX = fPosX * fRotMat00 + fPosY * fRotMat10 + fPosZ * fRotMat20 + fCarPosX;
          SmokePt[1][iSmokeIndex].world.fY = fPosX * fRotMat01 + fPosY * fRotMat12 + fPosZ * fRotMat11 + fCarPosY;
          iSmokeChunkIdx2 = iCurrChunk;
          SmokePt[1][iSmokeIndex].world.fZ = fPosX * fRotMat02 + fPosY * fRotMat21 + fPosZ * fRotMat22Copy + fCarPosZ;
          if (iSmokeChunkIdx2 != -1 && pTrackChunkPtr) {
            dSmoke2ChunkTransY = pTrackChunkPtr[1];
            fSmokeWorldX = SmokePt[1][iSmokeIndex].world.fX;
            fSmokeWorldY = SmokePt[1][iSmokeIndex].world.fY;
            dSmoke2ChunkTransX = dSmoke2ChunkTransY * fSmokeWorldY + *pTrackChunkPtr * fSmokeWorldX;
            dSmoke2ChunkMatZ = pTrackChunkPtr[2];
            //iTemp = LODWORD(SmokePt[1][iSmokeIndex].world.fZ);
            float fSmokeZ = (SmokePt[1][iSmokeIndex].world.fZ);
            SmokePt[1][iSmokeIndex].world.fX = (float)(dSmoke2ChunkTransX + dSmoke2ChunkMatZ * fSmokeZ - pTrackChunkPtr[9]);
            SmokePt[1][iSmokeIndex].world.fY = pTrackChunkPtr[3] * fSmokeWorldX + pTrackChunkPtr[4] * fSmokeWorldY + pTrackChunkPtr[5] * fSmokeZ - pTrackChunkPtr[10];
            dSmoke2ChunkTransZ = pTrackChunkPtr[6] * fSmokeWorldX + pTrackChunkPtr[7] * fSmokeWorldY + pTrackChunkPtr[8] * fSmokeZ - pTrackChunkPtr[11];
            pChunkDataPtr = pTrackChunkPtr;
            SmokePt[1][iSmokeIndex].world.fZ = (float)dSmoke2ChunkTransZ;
          }
          dSmoke2ViewDeltaX = SmokePt[1][iSmokeIndex].world.fX - viewx;
          dSmoke2ViewDeltaY = SmokePt[1][iSmokeIndex].world.fY - viewy;
          dSmoke2ViewDeltaZ = SmokePt[1][iSmokeIndex].world.fZ - viewz;
          fSmoke2ViewX = (float)(dSmoke2ViewDeltaX * vk1 + dSmoke2ViewDeltaY * vk4 + dSmoke2ViewDeltaZ * vk7);
          fSmoke2ViewY = (float)(dSmoke2ViewDeltaX * vk2 + dSmoke2ViewDeltaY * vk5 + dSmoke2ViewDeltaZ * vk8);
          dSmoke2CameraZ = dSmoke2ViewDeltaX * vk3 + dSmoke2ViewDeltaY * vk6 + dSmoke2ViewDeltaZ * vk9;
          fSmoke2ClampedZ = (float)dSmoke2CameraZ;
          fSmoke2CameraZCopy = fSmoke2ClampedZ;
          if (dSmoke2CameraZ < 80.0)
            fSmoke2ClampedZ = 80.0;
          dSmoke2ProjViewDist = (double)VIEWDIST;
          dSmoke2ProjInverseZ = 1.0 / fSmoke2ClampedZ;
          dSmoke2ProjScreenX = dSmoke2ProjViewDist * fSmoke2ViewX * dSmoke2ProjInverseZ + (double)xbase;
          //_CHP();
          xp = (int)dSmoke2ProjScreenX;
          dSmoke2ProjScreenY = dSmoke2ProjInverseZ * (dSmoke2ProjViewDist * fSmoke2ViewY) + (double)ybase;
          //_CHP();
          yp = (int)dSmoke2ProjScreenY;
          SmokePt[1][iSmokeIndex].screen.x = xp * scr_size >> 6;
          //SmokePt[1][iSmokeIndex].screen.x = iSmoke2ScreenXResult >> 6;
          SmokePt[1][iSmokeIndex].screen.y = (scr_size * (199 - yp)) >> 6;
          SmokePt[1][iSmokeIndex].view.fX = fSmoke2ViewX;
          SmokePt[1][iSmokeIndex].view.fY = fSmoke2ViewY;
          SmokePt[1][iSmokeIndex].view.fZ = fSmoke2CameraZCopy;
        }
        CarZOrder[iSmokeZOrderIdx].iPolygonIndex = iSpriteIdx;
        CarZOrder[iSmokeZOrderIdx].iPolygonLink = -2;
        if ((uint8)(pSmokeSpray->iType) == 2)
          CarZOrder[iSmokeZOrderIdx].fZDepth = SmokePt[0][iSmokeIndex].view.fZ + -32768.0f;
        else
          CarZOrder[iSmokeZOrderIdx].fZDepth = SmokePt[0][iSmokeIndex].view.fZ;
        ++iSmokeZOrderIdx;
        ++iVisiblePolygons;
      }
      ++iSmokeIndex;
      ++iSpriteIdx;
      ++pSmokeSpray;
    } while (iSpriteIdx < 32);
    qsort(CarZOrder, iVisiblePolygons, 0xCu, carZcmp);// Sort polygons by Z-depth for proper rendering order
    if (CarZOrder[0].fZDepth <= 16000.0 || VisibleCars < 5)
      iTextureDisabled = -1;
    else
      iTextureDisabled = 0;
    // TEX_OFF_CAR_TEXTURES
    if ((textures_off & 0x40) != 0)
      iTextureDisabled = 0;
    if (iVisiblePolygons > 0) {
      iSubdivisionParam = iCarIndexCopy + 3;
      uiTextureMapOffset = 4 * iCarIndexCopy;
      dwCarDesignOffset = 28 * carDesignIndex;  // 28 is sizeof(tCarDesign)
      iAnimationOffset = 4 * (iCarIndexCopy & 1);
      uiRenderLoopOffset = 0;
      iRenderLoopLimit = 12 * iVisiblePolygons;
      do {                                         // Main polygon rendering loop - draw sorted polygons
        if (CarZOrder[uiRenderLoopOffset / 0xC].iPolygonLink >= -1) {
          CarZOrder[uiRenderLoopOffset / 0xC].fZDepth = -1.0;
          iPolygonIdx = CarZOrder[uiRenderLoopOffset / 0xC].iPolygonIndex;
          CarZOrder[uiRenderLoopOffset / 0xC].iPolygonLink = -1;
          if (iPolygonIdx >= 0) {
            iBackfaceFlag = 0;
          } else {
            iBackfaceFlag = -1;
            iPolygonIdx = -1 - iPolygonIdx;
          }
          pRenderPolygon = &CarDesigns[dwCarDesignOffset / 0x1C].pPols[iPolygonIdx];
          uiTextureSurface = pRenderPolygon->uiTex;

          for (mm = 0; mm < 4; ++mm) {
            polygonVertices[mm] = &CarPt[pRenderPolygon->verts[mm]];
          }
          //for (mm = 0; mm != 16; *(_DWORD *)((char *)&tempData.fAIMaxSpeed + mm) = pPolygonVertex)// offset into polygonVertices
          //{
          //  mm += 4;
          //  pPolygonVertex = &CarPt[pRenderPolygon->verts[0]];
          //  pRenderPolygon = (tPolygon *)((char *)pRenderPolygon + 1);
          //}

          dPolygonDepth23 = polygonVertices[2]->view.fZ;
          CarPol.uiNumVerts = 4;
          if (dPolygonDepth23 >= polygonVertices[3]->view.fZ)
            pMinDepthVertex23 = polygonVertices[3];
          else
            pMinDepthVertex23 = polygonVertices[2];
          fMinDepthVertex23 = pMinDepthVertex23->view.fZ;
          if (polygonVertices[0]->view.fZ >= (double)polygonVertices[1]->view.fZ)
            pMinDepthVertex01 = polygonVertices[1];
          else
            pMinDepthVertex01 = polygonVertices[0];
          fMinDepthVertex01 = pMinDepthVertex01->view.fZ;
          if (fMinDepthVertex01 >= (double)fMinDepthVertex23) {
            if (polygonVertices[2]->view.fZ >= (double)polygonVertices[3]->view.fZ)
              pFinalMinVertex23 = polygonVertices[3];
            else
              pFinalMinVertex23 = polygonVertices[2];
            fFinalVertex23Depth = pFinalMinVertex23->view.fZ;
            fFinalMinDepth = fFinalVertex23Depth;
          } else {
            if (polygonVertices[0]->view.fZ >= (double)polygonVertices[1]->view.fZ)
              pFinalMinVertex01 = polygonVertices[1];
            else
              pFinalMinVertex01 = polygonVertices[0];
            fFinalVertex01Depth = pFinalMinVertex01->view.fZ;
            fFinalMinDepth = fFinalVertex01Depth;
          }
          fPolygonMinDepth = fFinalMinDepth;
          CarPol.vertices[0] = polygonVertices[0]->screen;
          CarPol.vertices[1] = polygonVertices[1]->screen;
          CarPol.vertices[2] = polygonVertices[2]->screen;
          CarPol.vertices[3] = polygonVertices[3]->screen;
          bCloseToCamera = fFinalMinDepth < 1.0;
          byTextureIndex = (uint8)uiTextureSurface;
          if (iBackfaceFlag) {
            uiTextureSurface = CarDesigns[dwCarDesignOffset / 0x1C].pBacks[iPolygonIdx];
            uiTextureSurface |= SURFACE_FLAG_FLIP_BACKFACE;
            //BYTE1(uiTextureSurface) |= 0x20u;
          } else if ((uiTextureSurface & CAR_FLAG_ANMS_LOOKUP) != 0) {
            pAnimation = &pAnms[(uint8)uiTextureSurface];
            if (byTextureIndex >= 4)
              uiTextureSurface = pAnimation->framesAy[iAnimationOffset / 4u];
            else
              uiTextureSurface = pAnimation->framesAy[(char)pCar->byWheelAnimationFrame];
          }
          // SURFACE_FLAG_BACK
          if ((uiTextureSurface & SURFACE_FLAG_BACK) != 0)
            uiTextureSurface ^= SURFACE_FLAG_BACK;
            //BYTE1(uiTextureSurface) ^= 8u;
          // SURFACE_FLAG_APPLY_TEXTURE
          if ((uiTextureSurface & SURFACE_FLAG_APPLY_TEXTURE) != 0 && !iTextureDisabled)// Apply texture mapping if SURFACE_FLAG_APPLY_TEXTURE set and not disabled
          {
            iAnimationFrame = (uint8)uiTextureSurface;
            SET_LOWORD(uiTextureSurface, uiTextureSurface & 0xFE00);
            uiTextureSurface += car_remap[256 * car_texmap[uiTextureMapOffset / 4] - 256 + iAnimationFrame];
          }
          // TEX_OFF_ADVANCED_CARS SURFACE_FLAG_APPLY_TEXTURE
          if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0 && (uiTextureSurface & SURFACE_FLAG_APPLY_TEXTURE) == 0 && (uint8)uiTextureSurface == uiColorFrom)
            uiTextureSurface = uiColorTo;
          CarPol.iSurfaceType = uiTextureSurface;
          if (bCloseToCamera) {
            subdivide(
              pScrBuf,
              &CarPol,
              polygonVertices[0]->view.fX,
              polygonVertices[0]->view.fY,
              polygonVertices[0]->view.fZ,
              polygonVertices[1]->view.fX,
              polygonVertices[1]->view.fY,
              polygonVertices[1]->view.fZ,
              polygonVertices[2]->view.fX,
              polygonVertices[2]->view.fY,
              polygonVertices[2]->view.fZ,
              polygonVertices[3]->view.fX,
              polygonVertices[3]->view.fY,
              polygonVertices[3]->view.fZ,
              iSubdivisionParam,
              gfx_size);                        // Subdivide polygon if too close to camera for better quality
          } else {
            // SURFACE_FLAG_APPLY_TEXTURE
            if ((uiTextureSurface & SURFACE_FLAG_APPLY_TEXTURE) != 0 && iTextureDisabled) {
              POLYTEX(cartex_vga[car_texmap[uiTextureMapOffset / 4] - 1], pScrBuf, &CarPol, car_texmap[uiTextureMapOffset / 4], gfx_size);
            } else {
              goto LABEL_267;  // No texture - render flat polygon
            }
          }
        } else {
          CarZOrder[uiRenderLoopOffset / 0xC].iPolygonLink = -1;
          iSpritePolygonIdx = CarZOrder[uiRenderLoopOffset / 0xC].iPolygonIndex;
          CarZOrder[uiRenderLoopOffset / 0xC].fZDepth = -1.0;
          iSpriteIndex = iSpritePolygonIdx;
          fSmokeDepthCheck = SmokePt[0][iSpritePolygonIdx].view.fZ;
          if (fSmokeDepthCheck > 80.0) {
            pCurrentSprite = &pCarSprayArray[iSpritePolygonIdx];
            if ((uint8)(pCurrentSprite->iType) == 1) {
              fSpriteSize = (double)VIEWDIST * pCurrentSprite->fSize / fSmokeDepthCheck;
              //_CHP();
              iTemp = (int)fSpriteSize;
              iScreenX = SmokePt[0][iSpritePolygonIdx].screen.x;
              iScreenY = SmokePt[0][iSpritePolygonIdx].screen.y;
              x = SmokePt[1][iSpritePolygonIdx].screen.x;
              CarPol.vertices[2].y = iScreenY;
              y = SmokePt[1][iSpriteIndex].screen.y;
              CarPol.vertices[3].y = iScreenY;
              CarPol.vertices[0].y = y;
              CarPol.vertices[1].y = y;
              uiSpriteSurface = pCurrentSprite->iColor;
              //uiSpriteSurface = pSpriteDataPtr->iColor;
              iTemp = (scr_size * (int)fSpriteSize) >> 6;
              uiSpriteSurface |= SURFACE_FLAG_FLIP_BACKFACE;
              //BYTE1(uiSpriteSurface) |= 0x20u;
              CarPol.vertices[0].x = x + iTemp;
              CarPol.iSurfaceType = uiSpriteSurface;
              CarPol.vertices[2].x = iScreenX - iTemp;
              CarPol.vertices[1].x = x - iTemp;
              CarPol.uiNumVerts = 4;
              CarPol.vertices[3].x = iScreenX + iTemp;
              // SURFACE_FLAG_APPLY_TEXTURE
              if ((uiSpriteSurface & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_267;
            } else {
              dSpriteProjection = (double)VIEWDIST * pCurrentSprite->fSize / fSmokeDepthCheck;
              //_CHP();
              iSpriteScreenSize = (int)dSpriteProjection;
              iSpriteSize = (int)dSpriteProjection;
              if ((int)dSpriteProjection <= 0 || iSpriteSize >= 100)
                goto LABEL_268;
              CarPol.iSurfaceType = pCurrentSprite->iColor;
              //CarPol.iSurfaceType = pSpriteDataPtr2->iColor;
              iSpriteScreenSize = (scr_size * iSpriteSize) >> 6;
              CarPol.uiNumVerts = 4;
              iSpriteCenterX = SmokePt[0][iSpritePolygonIdx].screen.x;
              iSpriteCenterY = SmokePt[0][iSpriteIndex].screen.y;
              CarPol.vertices[0].x = iSpriteCenterX + iSpriteScreenSize;
              CarPol.vertices[1].x = iSpriteCenterX - iSpriteScreenSize;
              CarPol.vertices[2].x = iSpriteCenterX - iSpriteScreenSize;
              CarPol.vertices[0].y = iSpriteCenterY - iSpriteScreenSize;
              CarPol.vertices[1].y = iSpriteCenterY - iSpriteScreenSize;
              CarPol.vertices[2].y = iSpriteScreenSize + iSpriteCenterY;
              CarPol.vertices[3].y = iSpriteScreenSize + iSpriteCenterY;
              CarPol.vertices[3].x = iSpriteCenterX + iSpriteScreenSize;
              // SURFACE_FLAG_APPLY_TEXTURE
              if ((CarPol.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_267:
                POLYFLAT(pScrBuf, &CarPol);
                goto LABEL_268;
              }
            }
            POLYTEX(cargen_vga, pScrBuf, &CarPol, 18, gfx_size);
          }
        }
      LABEL_268:
        uiRenderLoopOffset += 12;
      } while ((int)uiRenderLoopOffset < iRenderLoopLimit);
    }
  }
  if (NamesLeft < 5
    && NamesLeft >= -2
    && (names_on == 1 || names_on == 2 && human_control[pCar->iDriverIdx])
    && replaytype != 2
    && (pCar->byStatusFlags & 2) == 0
    && !winner_mode)                           // Render driver name above car if enabled and visible
  {
    float fHitboxZ = CarBox.hitboxAy[carDesignIndex][4].fZ;
    //iTemp = LODWORD(CarBox.hitboxAy[carDesignIndex][4].fZ);
    fNameTagWorldX = fHitboxZ * fRotMat20 + fCarPosX;
    fNameTagWorldY = fHitboxZ * fRotMat11 + fCarPosY;
    fNameTagWorldZ = fHitboxZ * fRotMat22Copy + fCarPosZ;
    if (iCurrChunk == -1) {
      CarPt[0].world.fX = fNameTagWorldX;
      CarPt[0].world.fY = fNameTagWorldY;
      CarPt[0].world.fZ = fNameTagWorldZ;
    } else {
      if (pChunkDataPtr) { //added by ROLLER
        CarPt[0].world.fX = pChunkDataPtr[1] * fNameTagWorldY + *pChunkDataPtr * fNameTagWorldX + pChunkDataPtr[2] * fNameTagWorldZ - pChunkDataPtr[9];
        CarPt[0].world.fY = pChunkDataPtr[3] * fNameTagWorldX + pChunkDataPtr[4] * fNameTagWorldY + pChunkDataPtr[5] * fNameTagWorldZ - pChunkDataPtr[10];
        CarPt[0].world.fZ = fNameTagWorldY * pChunkDataPtr[7] + fNameTagWorldX * pChunkDataPtr[6] + fNameTagWorldZ * pChunkDataPtr[8] - pChunkDataPtr[11];
      }
    }
    dNameTagDeltaX = CarPt[0].world.fX - viewx;
    dNameTagDeltaY = CarPt[0].world.fY - viewy;
    dNameTagDeltaZ = CarPt[0].world.fZ - viewz;
    fNameTagViewX = (float)(dNameTagDeltaX * vk1 + dNameTagDeltaY * vk4 + dNameTagDeltaZ * vk7);
    fNameTagViewY = (float)(dNameTagDeltaX * vk2 + dNameTagDeltaY * vk5 + dNameTagDeltaZ * vk8);
    dNameTagCameraZ = dNameTagDeltaX * vk3 + dNameTagDeltaY * vk6 + dNameTagDeltaZ * vk9;
    fNameTagClampedZ = (float)dNameTagCameraZ;
    fNameTagCameraZCopy = fNameTagClampedZ;
    if (dNameTagCameraZ < 80.0)
      fNameTagClampedZ = 80.0;
    dNameTagViewDist = (double)VIEWDIST;
    dNameTagInverseZ = 1.0 / fNameTagClampedZ;
    dNameTagScreenX = dNameTagViewDist * fNameTagViewX * dNameTagInverseZ + (double)xbase;
    //_CHP();
    xp = (int)dNameTagScreenX;
    dNameTagScreenY = dNameTagInverseZ * (dNameTagViewDist * fNameTagViewY) + (double)ybase;
    iNameTagScreenSize = scr_size;
    //_CHP();
    yp = (int)dNameTagScreenY;
    CarPt[0].screen.x = xp * scr_size >> 6;
    //CarPt[0].screen.x = iNameTagXResult >> 6;
    CarPt[0].screen.y = (iNameTagScreenSize * (199 - (int)dNameTagScreenY)) >> 6;
    CarPt[0].view.fX = fNameTagViewX;
    CarPt[0].view.fY = fNameTagViewY;
    CarPt[0].view.fZ = fNameTagCameraZCopy;
    iNameWidth = 0;
    if (pCar->byRacePosition < racers - 1 || racers == 1)
      sprintf(buffer, "%s (%s)", driver_names[iCarIndexCopy], &language_buffer[64 * pCar->byRacePosition + 384]);
    else
      sprintf(buffer, "%s (%s)", driver_names[iCarIndexCopy], &language_buffer[1344]);
    iNameCharIdx = 0;
    if (buffer[0]) {
      do
        iNameWidth += 5;
      while (buffer[++iNameCharIdx]);
    }
    iNameHalfWidth = iNameWidth / 2;
    if (mirror)
      iNameDisplayX = iNameHalfWidth + CarPt[0].screen.x;
    else
      iNameDisplayX = CarPt[0].screen.x - iNameHalfWidth;
    iNameDisplayY = CarPt[0].screen.y - 16;
    if (iNameDisplayX >= 0 && iNameDisplayX < winw - iNameWidth && iNameDisplayY > 0 && iNameDisplayY < winh - 16 && iNameWidth > 0 && !intro) {
      iPrevScrSize = scr_size;
      scr_size = 64;
      if (mirror)
        mini_prt_string_rev(rev_vga[0], buffer, iNameDisplayX, CarPt[0].screen.y - 16);
      else
        mini_prt_string(rev_vga[0], buffer, iNameDisplayX, CarPt[0].screen.y - 16);
      CarPol.vertices[0].x = CarPt[0].screen.x + 6;
      CarPol.vertices[0].y = CarPt[0].screen.y - 7;
      CarPol.vertices[1].x = CarPt[0].screen.x - 5;
      CarPol.vertices[1].y = CarPt[0].screen.y - 7;
      CarPol.vertices[2].x = CarPt[0].screen.x;
      CarPol.vertices[2].y = CarPt[0].screen.y - 1;
      CarPol.vertices[3].x = CarPt[0].screen.x + 6;
      CarPol.vertices[3].y = CarPt[0].screen.y - 7;
      iTeamColIdx = pCar->byCarDesignIdx;
      scr_size = iPrevScrSize;
      CarPol.iSurfaceType = team_col[iTeamColIdx];
      CarPol.uiNumVerts = 4;
      POLYFLAT(pScrBuf, &CarPol);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00056030
int carZcmp(const void *pCar1, const void *pCar2)
{
  const tCarZOrderEntry *pEntry1 = (const tCarZOrderEntry *)pCar1;
  const tCarZOrderEntry *pEntry2 = (const tCarZOrderEntry *)pCar2;

  float fZDepth1 = pEntry1->fZDepth;
  float fZDepth2 = pEntry2->fZDepth;

  if (fZDepth1 == fZDepth2)
    return 0;
  if (fZDepth1 >= fZDepth2)
    return -1;
  return 1;
}

//-------------------------------------------------------------------------------------------------
