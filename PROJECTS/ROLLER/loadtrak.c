#include "loadtrak.h"
#include "3d.h"
#include "car.h"
#include "moving.h"
#include "graphics.h"
#include "polytex.h"
#include "building.h"
#include "roller.h"
#include "tower.h"
#include "function.h"
#include "transfrm.h"
#include "view.h"
#include "control.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
//-------------------------------------------------------------------------------------------------

tSurface surface[14] =      //000A5FA8
{
  { 50.0, 100, 500.0, 75 },
  { 50.0, 95, 500.0, 65 },
  { 50.0, 90, 500.0, 55 },
  { 50.0, 85, 500.0, 50 },
  { 50.0, 80, 500.0, 45 },
  { 50.0, 75, 500.0, 40 },
  { 50.0, 70, 500.0, 35 },
  { 50.0, 65, 500.0, 30 },
  { 50.0, 60, 500.0, 25 },
  { 50.0, 55, 500.0, 20 },
  { 50.0, 50, 500.0, 15 },
  { 50.0, 40, 500.0, 10 },
  { 30.0, 30, 500.0, 5 },
  { 10.0, 20, 500.0, 0 }
};
uint8 TrackSelect = 0;      //000A5F9C
char *delims = " ,\n\t\r";  //000A6088
char *names[25] = {         //000A608C
    "TRACK0.TRK",   // 0
    "TRACK1.TRK",   // 1
    "TRACK2.TRK",   // 2
    "TRACK3.TRK",   // 3
    "TRACK4.TRK",   // 4
    "TRACK5.TRK",   // 5
    "TRACK6.TRK",   // 6
    "TRACK7.TRK",   // 7
    "TRACK8.TRK",   // 8
    "TRACK9.TRK",   // 9
    "TRACK10.TRK",  // 10
    "TRACK11.TRK",  // 11
    "TRACK12.TRK",  // 12
    "TRACK13.TRK",  // 13
    "TRACK14.TRK",  // 14
    "TRACK15.TRK",  // 15
    "TRACK16.TRK",  // 16
    "TRACK17.TRK",  // 17
    "TRACK18.TRK",  // 18
    "TRACK19.TRK",  // 19
    "TRACK20.TRK",  // 20
    "TRACK21.TRK",  // 21
    "TRACK22.TRK",  // 22
    "TRACK23.TRK",  // 23
    "TRACK24.TRK"   // 24
};
tTrakView TrakView[MAX_TRACK_CHUNKS];    //0016FF20
int16 samplespeed[MAX_SAMPLES];     //00170EC0
int16 samplemax[MAX_SAMPLES];       //001712A8
float GroundLevel[MAX_TRACK_CHUNKS];     //00171690
tTrackInfo TrackInfo[MAX_TRACK_CHUNKS];  //00171E60
int cur_mapsect;            //00178044
float cur_TrackZ;           //00178048
float cur_mapsize;          //0017804C
int TRAK_LEN;               //00178050
int16 samplemin[MAX_SAMPLES];       //001764B0
int cur_laps[6];            //00176898
uint8 fp_buf[512];          //001768B0
int actualtrack;            //00176AB8
uint8 *start_f;             //00176ABC
int TrackFlags;             //00176AC0
int meof;                   //00176AC4
tSubdivide Subdivide[MAX_TRACK_CHUNKS];  //00176AC8

//-------------------------------------------------------------------------------------------------
//0004AF80
void loadtrack(int iTrackIdx, int iPreviewMode)
{
  int iCarIdx; // ecx
  tCar *pCar; // edi
  FILE *pFile; // edx
  int iCompactedFileLength; // edx
  //int iFileHandle_1; // edx
  unsigned int iFileLength; // ebx
  uint8 *pTrackBuffer_1; // eax
  uint8 *pTrackBuffer; // eax
  FILE *pFile_1; // eax
  int iChunkIdx; // edi
  int iTrackInfoIdx; // esi
  int iTrakColourIdx; // ebp
  uint32 *p_uiLUOWallType; // edx
  int iSubdivideIdx; // ebx
  int iTemp1; // edx
  int iTemp2; // eax
  double dTemp1; // st7
  int iSubdivLoopIdx; // edx
  int iSubdivOffset; // ebx
  int iSubdivArrayIdx; // eax
  unsigned int v22; // edx
  int uiTrakViewOffset1; // ecx
  int iBackwardExtraStart; // ecx
  unsigned int uiTrakViewOffset2; // edx
  int *pTowerBase; // edx
  int *v27; // edx
  int *p_iData1; // ebx
  int *pTowerBase2; // ecx
  int *pTowerBase3; // edx
  int *p_iBuildingData; // eax
  int *p_iBuildingData2; // ebx
  float *p_fBuildingAngles; // eax
  int v34; // edx
  double dAngleCalc1; // st7
  double dLeftAngle; // st6
  double dRightAngle; // st6
  double dAILine1_1; // st7
  unsigned int uiLocalDataOffset1; // eax
  unsigned int uiLocalDataOffset2; // edx
  double dAIMaxSpeed_1; // st7
  unsigned int uiGroundColourOffset1; // eax
  int iOFloorType; // edx
  int iLeftSurfaceType; // eax
  int iRightSurfaceType; // ecx
  int iLeftSurfCheck; // ebx
  int iRightSurfCheck; // edx
  double dCos; // rt1
  int iDifficulty; // eax
  int iTrackLapOffset; // edx
  int iLapValue; // ebx
  double dTrackZ; // st7
  int16 *p_nAudioBelowTrigger; // [esp+30h] [ebp-25Ch]
  double dZ; // [esp+34h] [ebp-258h] BYREF
  double dY; // [esp+3Ch] [ebp-250h] BYREF
  double dX; // [esp+44h] [ebp-248h] BYREF
  double dRoofHeight; // [esp+4Ch] [ebp-240h] BYREF
  double dRUOWallHeight; // [esp+54h] [ebp-238h] BYREF
  double dRLOWallHeight; // [esp+5Ch] [ebp-230h] BYREF
  double dROFloorHeight; // [esp+64h] [ebp-228h] BYREF
  double dLOFloorHeight; // [esp+6Ch] [ebp-220h] BYREF
  double dLLOWallHeight; // [esp+74h] [ebp-218h] BYREF
  double dLUOWallHeight; // [esp+7Ch] [ebp-210h] BYREF
  double dRUOWallHOffset; // [esp+84h] [ebp-208h] BYREF
  double dRLOWallHOffset; // [esp+8Ch] [ebp-200h] BYREF
  double dROFloorHOffset; // [esp+94h] [ebp-1F8h] BYREF
  double dLOFloorHOffset; // [esp+9Ch] [ebp-1F0h] BYREF
  double dLLOWallHOffset; // [esp+A4h] [ebp-1E8h] BYREF
  double dLUOWallHOffset; // [esp+ACh] [ebp-1E0h] BYREF
  double dAIMaxSpeed; // [esp+B4h] [ebp-1D8h] BYREF
  double dAILine4; // [esp+BCh] [ebp-1D0h] BYREF
  double dAILine3; // [esp+C4h] [ebp-1C8h] BYREF
  double dAILine2; // [esp+CCh] [ebp-1C0h] BYREF
  double dAILine1; // [esp+D4h] [ebp-1B8h] BYREF
  double dRoll; // [esp+DCh] [ebp-1B0h] BYREF
  double dPitch; // [esp+E4h] [ebp-1A8h] BYREF
  double dYaw; // [esp+ECh] [ebp-1A0h] BYREF
  double dLength; // [esp+F4h] [ebp-198h] BYREF
  double dRightShoulderHeight; // [esp+FCh] [ebp-190h] BYREF
  double dLeftShoulderHeight; // [esp+104h] [ebp-188h] BYREF
  double dRightShoulderWidth; // [esp+10Ch] [ebp-180h] BYREF
  double dRightLaneWidth; // [esp+114h] [ebp-178h] BYREF
  double dLeftLaneWidth; // [esp+11Ch] [ebp-170h] BYREF
  double dLeftShoulderWidth; // [esp+124h] [ebp-168h] BYREF
  double dWallCalc1; // [esp+12Ch] [ebp-160h] BYREF
  double dWallCalc2; // [esp+134h] [ebp-158h] BYREF
  double dWallCalc3; // [esp+13Ch] [ebp-150h] BYREF
  double dTempPitch_3; // [esp+144h] [ebp-148h]
  double dTempYaw_1; // [esp+14Ch] [ebp-140h]
  double dLeftWallTotalHeight; // [esp+154h] [ebp-138h]
  double dTempPitch; // [esp+15Ch] [ebp-130h]
  double dTempYaw; // [esp+164h] [ebp-128h]
  double dTempPitch_2; // [esp+16Ch] [ebp-120h]
  double dTempYaw_2; // [esp+174h] [ebp-118h]
  double dTempYaw_3; // [esp+17Ch] [ebp-110h]
  double dTempPitch_1; // [esp+184h] [ebp-108h]
  double dClampedPitch; // [esp+18Ch] [ebp-100h]
  double dClampedYaw; // [esp+194h] [ebp-F8h]
  double dTempCalc1; // [esp+19Ch] [ebp-F0h]
  double dRadiansPerDegree; // [esp+1A4h] [ebp-E8h]
  double dTempCalc2; // [esp+1BCh] [ebp-D0h]
  int *p_iOFloorType; // [esp+1C4h] [ebp-C8h]
  int *p_iLLOWallType; // [esp+1C8h] [ebp-C4h]
  int iLeftShoulderGrip; // [esp+1CCh] [ebp-C0h] BYREF
  int iDrawOrderBackward; // [esp+1D0h] [ebp-BCh] BYREF
  int iRightShoulderGrip; // [esp+1D4h] [ebp-B8h] BYREF
  int *p_iRUOWallType; // [esp+1D8h] [ebp-B4h]
  int iDrawOrder3; // [esp+1DCh] [ebp-B0h] BYREF
  int *p_iCenterSurfType; // [esp+1E0h] [ebp-ACh]
  int iBackwardExtraStart2; // [esp+1E4h] [ebp-A8h] BYREF
  int iBackwardExtraChunks; // [esp+1E8h] [ebp-A4h] BYREF
  uint8 *pData; // [esp+1ECh] [ebp-A0h] BYREF
  int iSignYaw; // [esp+1F0h] [ebp-9Ch] BYREF
  int iSignVOffset; // [esp+1F4h] [ebp-98h] BYREF
  int iSignHOffset; // [esp+1F8h] [ebp-94h] BYREF
  int iSignType; // [esp+1FCh] [ebp-90h] BYREF
  int iForwardExtraStart; // [esp+200h] [ebp-8Ch] BYREF
  int iTrackIdx_1; // [esp+204h] [ebp-88h]
  uint8 *pCurrDataPtr = NULL; // [esp+208h] [ebp-84h] BYREF
  int iSignRoll; // [esp+20Ch] [ebp-80h] BYREF
  int iSignPitch; // [esp+210h] [ebp-7Ch] BYREF
  int iDrawOrder1; // [esp+214h] [ebp-78h] BYREF
  int *p_iRLOWallType; // [esp+218h] [ebp-74h]
  int iCompactedFlag; // [esp+21Ch] [ebp-70h]
  FILE *pFile_2; // [esp+220h] [ebp-6Ch]
  int iTemp3; // [esp+224h] [ebp-68h]
  int iTrackGrip; // [esp+228h] [ebp-64h] BYREF
  int iSubdivArrayBaseOffset; // [esp+22Ch] [ebp-60h]
  int *p_uiRoofType; // [esp+230h] [ebp-5Ch]
  int16 *p_nAudioAboveTrigger; // [esp+234h] [ebp-58h]
  int16 *p_nSampleMin; // [esp+238h] [ebp-54h]
  float *p_fGroundLevel; // [esp+23Ch] [ebp-50h]
  int16 *p_nAudioTriggerSpeed; // [esp+240h] [ebp-4Ch]
  int *p_iHorizonType; // [esp+244h] [ebp-48h]
  int *p_iRightWallType; // [esp+248h] [ebp-44h]
  int *p_iRightSurfType; // [esp+24Ch] [ebp-40h]
  int *p_iLeftWallType; // [esp+250h] [ebp-3Ch]
  int bMinimalMode; // [esp+254h] [ebp-38h]
  unsigned int uiGroundLevelOffset; // [esp+258h] [ebp-34h]
  unsigned int uiTrakViewOffset; // [esp+25Ch] [ebp-30h]
  float *p_fBuildingAnglesBase; // [esp+260h] [ebp-2Ch]
  unsigned int uiLocalDataOffset; // [esp+264h] [ebp-28h]
  int *p_iBuildingBase; // [esp+268h] [ebp-24h]
  unsigned int uiGroundColourOffset; // [esp+26Ch] [ebp-20h]
  int *pTowerBasePtr; // [esp+270h] [ebp-1Ch]
  unsigned int uiGroundPtOffset; // [esp+274h] [ebp-18h]

  iTrackIdx_1 = iTrackIdx;                      // Initialize variables and clear car structures
  bMinimalMode = iPreviewMode;
  p_iBuildingBase = BuildingBase[0];
  p_fBuildingAnglesBase = BuildingAngles;
  pTowerBasePtr = (int *)TowerBase;
  iCompactedFlag = 0;
  pData = 0;
  NumBuildings = 0;
  NumTowers = 0;
  iCarIdx = 0;
  if (numcars > 0) {
    pCar = Car;
    do {
      memset(pCar, 0, sizeof(tCar));
      ++iCarIdx;
      ++pCar;
    } while (iCarIdx < numcars);
  }
  pFile_2 = 0;
  if ((unsigned int)iTrackIdx_1 <= 0x18) {
    pFile = ROLLERfopen(names[iTrackIdx_1], "r");     // Open and validate track file
    if (!pFile) {
      ErrorBoxExit("Track %d not found\n", iTrackIdx_1);
      //__asm { int     10h; -VIDEO - SET VIDEO MODE }
      //printf("Track %d not found\n", iTrackIdx_1);
      //doexit();
    }
    fclose(pFile);
    iCompactedFileLength = getcompactedfilelength(names[iTrackIdx_1]);
    if ((int16)iCompactedFileLength == 8224)// Check if file is compacted (magic number 8224)
    {
      iFileLength = ROLLERfilelength(names[iTrackIdx_1]);
      //iFileHandle_1 = ROLLERopen(names[iTrackIdx_1], 0);
      //iFileLength = filelength(iFileHandle_1);
      //close(iFileHandle_1);
      if (bMinimalMode)
        pTrackBuffer_1 = scrbuf;
      else
        pTrackBuffer_1 = (uint8 *)getbuffer(iFileLength + 1);
      pData = pTrackBuffer_1;
      pCurrDataPtr = pTrackBuffer_1;
      pFile_2 = ROLLERfopen(names[iTrackIdx_1], "rb");
      fread(pData, iFileLength, 1u, pFile_2);
      iCompactedFlag = 0;
      pData[iFileLength] = 26;
    } else {
      if (bMinimalMode)
        pTrackBuffer = scrbuf;
      else
        pTrackBuffer = (uint8 *)getbuffer(iCompactedFileLength + 1);
      pData = pTrackBuffer;
      pCurrDataPtr = pTrackBuffer;
      loadcompactedfile(names[iTrackIdx_1], pTrackBuffer);
      pFile_1 = ROLLERfopen(names[iTrackIdx_1], "r");
      pData[iCompactedFileLength] = 26;
      pFile_2 = pFile_1;
      iCompactedFlag = -1;
    }
  }
  meof = 0;
  if (iTrackIdx_1 >= 0)
    readline2(&pCurrDataPtr, "iddd", &TRAK_LEN, &dWallCalc3, &dWallCalc2, &dWallCalc1);// Read track header: length and initial position
  iChunkIdx = 0;
  TrackFlags = 0;
  if (TRAK_LEN > 0) {
    p_iCenterSurfType = &TrakColour[0][TRAK_COLOUR_CENTER];// Main track chunk processing loop
    p_iRightSurfType = &TrakColour[0][TRAK_COLOUR_RIGHT_LANE];
    p_iLeftWallType = &TrakColour[0][TRAK_COLOUR_LEFT_WALL];
    uiTrakViewOffset = 0;
    uiLocalDataOffset = 0;
    p_iRightWallType = &TrakColour[0][TRAK_COLOUR_RIGHT_WALL];
    uiGroundPtOffset = 0;
    uiGroundColourOffset = 0;
    p_uiRoofType = &TrakColour[0][TRAK_COLOUR_ROOF];
    iSubdivArrayBaseOffset = 11;
    p_fGroundLevel = GroundLevel;
    p_iLLOWallType = &GroundColour[0][GROUND_COLOUR_LLOWALL];
    p_nAudioAboveTrigger = samplemax;
    p_nAudioTriggerSpeed = samplespeed;
    p_iOFloorType = &GroundColour[0][GROUND_COLOUR_OFLOOR];
    p_nSampleMin = samplemin;
    p_iRLOWallType = &GroundColour[0][GROUND_COLOUR_RLOWALL];
    iTrackInfoIdx = 0;
    iTrakColourIdx = 0;
    p_iRUOWallType = &GroundColour[0][GROUND_COLOUR_RUOWALL];
    uiGroundLevelOffset = 0;
    p_iHorizonType = HorizonColour;
    dRadiansPerDegree = 1.0 / 6.28318530718;
    do {
      if (iTrackIdx_1 >= 0) {
        p_uiLUOWallType = (uint32 *)&GroundColour[uiGroundColourOffset / 0x14];
        GroundLevel[uiGroundLevelOffset / 4] = 0.0;
        p_nAudioBelowTrigger = p_nSampleMin;
        samplemin[iChunkIdx] = 0;
        samplemax[iChunkIdx] = 0;
        samplespeed[iChunkIdx] = 0;
        readline2(
          &pCurrDataPtr,
          "ddddddddddddddiiidfsss",
          &dLeftShoulderWidth,
          &dLeftLaneWidth,
          &dRightLaneWidth,
          &dRightShoulderWidth,
          &dLeftShoulderHeight,
          &dRightShoulderHeight,
          &dLength,
          &dYaw,
          &dPitch,
          &dRoll,
          &dAILine1,
          &dAILine2,
          &dAILine3,
          &dAILine4,
          &iTrackGrip,
          &iLeftShoulderGrip,
          &iRightShoulderGrip,
          &dAIMaxSpeed,
          p_fGroundLevel,
          p_nAudioAboveTrigger,
          p_nAudioTriggerSpeed,
          p_nAudioBelowTrigger);                // Read track segment geometry data
        readline2(
          &pCurrDataPtr,
          "iiiiiiiiiiiiiiiiii",
          &TrakColour[iTrakColourIdx],          // LeftSurfType
          p_iCenterSurfType,
          p_iRightSurfType,
          p_iLeftWallType,
          p_iRightWallType,
          p_uiRoofType,
          p_uiLUOWallType,
          p_iLLOWallType,
          p_iOFloorType,
          p_iRLOWallType,
          p_iRUOWallType,
          p_iHorizonType,
          &iSignType,
          &iSignHOffset,
          &iSignVOffset,
          &iSignYaw,
          &iSignPitch,
          &iSignRoll);                          // Read track surface and wall color data
        iTemp1 = iSubdivArrayBaseOffset;
        iTemp2 = 11 * iChunkIdx;
        // Clear the subdivides array for the current chunk
        for (int i = 0; i < 11; i++) {
          Subdivide[iChunkIdx].subdivides[i] = 0;
        }
        //do
        //  *((uint8 *)&meof + ++iTemp2 + 3) = 0;
        //while (iTemp2 != iTemp1);
        iSubdivideIdx = iChunkIdx;
        readline2(
          &pCurrDataPtr,
          "dddddddddddddiiiuuuuuuuuuuuiii",
          &dLUOWallHOffset,
          &dLLOWallHOffset,
          &dLOFloorHOffset,
          &dROFloorHOffset,
          &dRLOWallHOffset,
          &dRUOWallHOffset,
          &dLUOWallHeight,
          &dLLOWallHeight,
          &dLOFloorHeight,
          &dROFloorHeight,
          &dRLOWallHeight,
          &dRUOWallHeight,
          &dRoofHeight,
          &iDrawOrder1,
          &iForwardExtraStart,
          &iDrawOrder3,
          &Subdivide[iSubdivideIdx],
          &Subdivide[iSubdivideIdx].subdivides[1],
          &Subdivide[iSubdivideIdx].subdivides[2],
          &Subdivide[iSubdivideIdx].subdivides[3],
          &Subdivide[iSubdivideIdx].subdivides[4],
          &Subdivide[iSubdivideIdx].subdivides[5],
          &Subdivide[iSubdivideIdx].subdivides[6],
          &Subdivide[iSubdivideIdx].subdivides[7],
          &Subdivide[iSubdivideIdx].subdivides[8],
          &Subdivide[iSubdivideIdx].subdivides[9],
          &Subdivide[iSubdivideIdx].subdivides[10],
          &iDrawOrderBackward,
          &iBackwardExtraStart2,
          &iBackwardExtraChunks);               // Read wall heights, offsets and view distance data
        if (bMinimalMode) {
          dLeftLaneWidth = dLeftLaneWidth * 2.0;
          dRightLaneWidth = dRightLaneWidth * 2.0;
        }
        if ((cheat_mode & 0x1000) != 0 && !bMinimalMode)// Apply double track width cheat if enabled
        {
          dLeftShoulderWidth = dLeftShoulderWidth * 2.0;
          dLeftLaneWidth = dLeftLaneWidth * 2.0;
          dRightLaneWidth = dRightLaneWidth * 2.0;
          dRightShoulderWidth = dRightShoulderWidth * 2.0;
          dLeftShoulderHeight = dLeftShoulderHeight * 2.0;
          dRightShoulderHeight = dRightShoulderHeight * 2.0;
          dLength = dLength * 2.0;
          dAILine1 = dAILine1 * 2.0;
          dAILine2 = dAILine2 * 2.0;
          dAILine3 = dAILine3 * 2.0;
          dAILine4 = dAILine4 * 2.0;
          dLUOWallHOffset = dLUOWallHOffset * 2.0;
          dLLOWallHOffset = dLLOWallHOffset * 2.0;
          dLOFloorHOffset = dLOFloorHOffset * 2.0;
          dROFloorHOffset = dROFloorHOffset * 2.0;
          dRLOWallHOffset = dRLOWallHOffset * 2.0;
          dRUOWallHOffset = dRUOWallHOffset * 2.0;
          dLUOWallHeight = dLUOWallHeight * 2.0;
          dLLOWallHeight = dLLOWallHeight * 2.0;
          dLOFloorHeight = dLOFloorHeight * 2.0;
          dROFloorHeight = dROFloorHeight * 2.0;
          dRLOWallHeight = dRLOWallHeight * 2.0;
          dRUOWallHeight = dRUOWallHeight * 2.0;
          dRoofHeight = dRoofHeight * 2.0;
          iSignVOffset *= 2;
          dTemp1 = GroundLevel[uiGroundLevelOffset / 4] * 2.0;
          iSignHOffset *= 2;
          GroundLevel[uiGroundLevelOffset / 4] = (float)dTemp1;
        }
        iSubdivLoopIdx = 0;                     // Initialize subdivision array for current chunk
        iSubdivOffset = 0;
        iSubdivArrayIdx = 11 * iChunkIdx;
        do {
          //int iChunkIdx = iSubdivArrayIdx / 11;  // Which chunk we're in
          int iElementIdx = iSubdivArrayIdx % 11; // Which element within the chunk's subdivides array
          if (!Subdivide[iChunkIdx].subdivides[iElementIdx] && (unsigned int)iSubdivLoopIdx <= 10) {
            switch (iSubdivLoopIdx) {
              case 0:
              case 1:
              case 2:
              case 3:
              case 4:
              case 5:
              case 7:
              case 9:
                Subdivide[iChunkIdx].subdivides[iElementIdx] = 20;  // 0x14 - Default subdivision level
                break;
              case 6:
              case 8:
              case 10:
                Subdivide[iChunkIdx].subdivides[iElementIdx] = 4;   // Special subdivision level for specific geometry
                break;
              default:
                  // Should never reach here due to the <= 0xA check
                break;
            }
          }
          //if (!Subdivide[0].subdivides[iSubdivArrayIdx] && (unsigned int)iSubdivLoopIdx <= 10)
          //  __asm { jmp     cs : off_4AF00[ebx]; jumptable 0004BC6D case 257 }
          iSubdivOffset += 4;
          ++iSubdivLoopIdx;
          ++iSubdivArrayIdx;
        } while (iSubdivLoopIdx <= 10);
        if (iDrawOrder1 < 8)
          iDrawOrder1 = 32;
        if (iDrawOrder1 > 128)
          iDrawOrder1 = 32;
        TrakView[uiTrakViewOffset / 8].byForwardMainChunks = iDrawOrder1;
        if (iForwardExtraStart < 0 || iForwardExtraStart > TRAK_LEN || iDrawOrder3 < 8 || iDrawOrder3 > 128) {
          iForwardExtraStart = -1;
          iDrawOrder3 = 0;
        }
        v22 = uiTrakViewOffset;
        TrakView[uiTrakViewOffset / 8].nForwardExtraStart = iForwardExtraStart;
        uiTrakViewOffset1 = iDrawOrderBackward;
        *(&TrakView[0].byForwardExtraChunks + v22) = iDrawOrder3;
        if (uiTrakViewOffset1 < 8)
          iDrawOrderBackward = 32;
        if (iDrawOrderBackward > 128)
          iDrawOrderBackward = 32;
        iBackwardExtraStart = iBackwardExtraStart2;
        TrakView[uiTrakViewOffset / 8].byBackwardMainChunks = iDrawOrderBackward;
        if (iBackwardExtraStart < 0 || iBackwardExtraStart > TRAK_LEN || iBackwardExtraChunks < 8 || iBackwardExtraChunks > 128) {
          iBackwardExtraStart2 = -1;
          iBackwardExtraChunks = 0;
        }
        uiTrakViewOffset2 = uiTrakViewOffset;
        TrakView[uiTrakViewOffset / 8].nBackwardExtraStart = iBackwardExtraStart2;
        TrakView[uiTrakViewOffset2 / 8].byBackwardExtraChunks = iBackwardExtraChunks;
        //*(&TrakView[0].byBackwardExtraChunks + uiTrakViewOffset2) = iBackwardExtraChunks;
      }
      if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] == -1)
        TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] = 0;
      if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] == -1)
        TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] = 0;
      if (!iChunkIdx) {
        dTempYaw = dYaw;
        dTempPitch = dPitch;
      }
      dTempYaw_2 = dYaw;
      dTempYaw_1 = dTempYaw;
      if (dYaw - dTempYaw > 180.0)
        dTempYaw_2 = dYaw + -360.0;
      if (dTempYaw_2 - dTempYaw_1 < -180.0)
        dTempYaw_2 = dTempYaw_2 + 360.0;
      dTempYaw_3 = dTempYaw_2 - dTempYaw_1;
      if (dTempYaw_3 > 180.0)
        dTempYaw_3 = dTempYaw_3 + -360.0;
      if (dTempYaw_3 < -180.0)
        dTempYaw_3 = dTempYaw_3 + 360.0;
      dClampedYaw = dTempYaw_3 * 0.5 + dTempYaw;
      dTempPitch_2 = dPitch;
      dTempPitch_3 = dTempPitch;
      if (dPitch - dTempPitch > 180.0)
        dTempPitch_2 = dPitch + -360.0;
      if (dTempPitch_2 - dTempPitch_3 < -180.0)
        dTempPitch_2 = dTempPitch_2 + 360.0;
      dTempPitch_1 = dTempPitch_2 - dTempPitch_3;
      if (dTempPitch_1 > 180.0)
        dTempPitch_1 = dTempPitch_1 + -360.0;
      if (dTempPitch_1 < -180.0)
        dTempPitch_1 = dTempPitch_1 + 360.0;
      dClampedPitch = dTempPitch_1 * 0.5 + dTempPitch;
      if (iSignType != -1) {                                         // Process signs/objects: buildings vs towers
        if (iSignType < 256) {
          p_iBuildingData = p_iBuildingBase + 1;
          *p_iBuildingBase = iSignType;
          *p_iBuildingData = iChunkIdx;
          p_iBuildingData[1] = iSignHOffset;
          p_iBuildingData2 = p_iBuildingData + 2;
          p_iBuildingData[2] = iSignVOffset;
          p_fBuildingAngles = p_fBuildingAnglesBase;
          *p_fBuildingAnglesBase = (float)iSignYaw;
          p_fBuildingAngles[1] = (float)iSignPitch;
          p_iBuildingBase = p_iBuildingData2 + 1;
          ++p_fBuildingAngles;
          ++NumBuildings;
          p_fBuildingAnglesBase = p_fBuildingAngles + 2;
          p_fBuildingAngles[1] = (float)iSignRoll;
        } else {
          *pTowerBasePtr++ = iChunkIdx;
          pTowerBase = pTowerBasePtr;
          *pTowerBasePtr = iSignHOffset;
          pTowerBase[1] = iSignVOffset;
          v27 = pTowerBase + 1;
          p_iData1 = v27 + 1;
          pTowerBasePtr = v27 + 1;
          switch (iSignType & 0xFF0F) {
            case 0x101:
              pTowerBasePtr = v27 + 2;
              *p_iData1 = -4;
              break;
            case 0x103:
              pTowerBasePtr = v27 + 2;
              *p_iData1 = -2;
              break;
            case 0x104:
              pTowerBasePtr = v27 + 2;
              *p_iData1 = -5;
              break;
            case 0x105:
              pTowerBasePtr = v27 + 2;
              *p_iData1 = -3;
              break;
            default:
              pTowerBase2 = pTowerBasePtr + 1;
              *pTowerBasePtr = -1;
              pTowerBasePtr = pTowerBase2;
              break;
          }
          pTowerBase3 = pTowerBasePtr;
          *pTowerBasePtr = (iSignType - 256) / 16;
          //*pTowerBasePtr = (iSignType - 256 - (__CFSHL__((iSignType - 256) >> 31, 4) + 16 * ((iSignType - 256) >> 31))) >> 4;
          pTowerBasePtr = pTowerBase3 + 1;
          ++NumTowers;
        }
      }
      rotatepoint(0.0, dLeftShoulderWidth + dLeftLaneWidth, dLeftShoulderHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);// Calculate and set track geometry points
      setpoint(iChunkIdx, 0, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      rotatepoint(0.0, dLeftShoulderWidth + dLeftLaneWidth, 0.0, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
      setpoint(iChunkIdx, 1, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      rotatepoint(0.0, dLeftLaneWidth, 0.0, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
      setpoint(iChunkIdx, 2, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      rotatepoint(0.0, -dRightLaneWidth, 0.0, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
      setpoint(iChunkIdx, 3, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      rotatepoint(0.0, -dRightLaneWidth - dRightShoulderWidth, dRightShoulderHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
      setpoint(iChunkIdx, 4, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      rotatepoint(0.0, -dRightLaneWidth - dRightShoulderWidth, 0.0, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
      setpoint(iChunkIdx, 5, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
      if (!bMinimalMode) {                                         // Calculate ground points based on floor type
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          dLOFloorHeight = -dLeftShoulderHeight;
          dLOFloorHOffset = dLeftShoulderWidth;
          dROFloorHOffset = dRightShoulderWidth;
          dROFloorHeight = -dRightShoulderHeight;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_LANE] < 0) {
          dLOFloorHOffset = dLeftShoulderWidth;
          dLOFloorHeight = 0.0;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_LANE] < 0) {
          dROFloorHOffset = dRightShoulderWidth;
          dROFloorHeight = 0.0;
        }
        dTempCalc2 = dLUOWallHOffset + dLLOWallHOffset;
        dLeftWallTotalHeight = dLUOWallHeight + dLLOWallHeight;
        v34 = GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR];
        dTempCalc1 = dTempCalc2 + dLOFloorHOffset + dLeftLaneWidth;
        if (v34 == -2) {
          rotatepoint(0.0, dTempCalc1, dLeftWallTotalHeight - dLOFloorHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 0, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
          rotatepoint(0.0, dLLOWallHOffset + dLOFloorHOffset + dLeftLaneWidth, dLLOWallHeight - dLOFloorHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 1, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
          rotatepoint(0.0, dLOFloorHOffset + dLeftLaneWidth, -dLOFloorHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 2, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
          rotatepoint(0.0, -dROFloorHOffset - dRightLaneWidth, -dROFloorHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 3, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
          rotatepoint(0.0, -dRLOWallHOffset - dROFloorHOffset - dRightLaneWidth, dRLOWallHeight - dROFloorHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 4, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
          rotatepoint(
            0.0,
            -dRUOWallHOffset - dRLOWallHOffset - dROFloorHOffset - dRightLaneWidth,
            dRUOWallHeight + dRLOWallHeight - dROFloorHeight,
            dClampedYaw,
            dClampedPitch,
            dRoll,
            &dX,
            &dY,
            &dZ);
          setgpoint(iChunkIdx, 5, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
        } else {
          rotatepoint(0.0, dTempCalc1, dLeftWallTotalHeight + dLOFloorHeight, dClampedYaw, dClampedPitch, 0.0, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 0, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
          rotatepoint(0.0, dLLOWallHOffset + dLOFloorHOffset + dLeftLaneWidth, dLLOWallHeight + dLOFloorHeight, dClampedYaw, dClampedPitch, 0.0, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 1, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
          rotatepoint(0.0, dLOFloorHOffset + dLeftLaneWidth, dLOFloorHeight, dClampedYaw, dClampedPitch, 0.0, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 2, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
          rotatepoint(0.0, -dROFloorHOffset - dRightLaneWidth, dROFloorHeight, dClampedYaw, dClampedPitch, 0.0, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 3, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
          rotatepoint(0.0, -dRLOWallHOffset - dROFloorHOffset - dRightLaneWidth, dRLOWallHeight + dROFloorHeight, dClampedYaw, dClampedPitch, 0.0, &dX, &dY, &dZ);
          setgpoint(iChunkIdx, 4, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
          rotatepoint(
            0.0,
            -dRUOWallHOffset - dRLOWallHOffset - dROFloorHOffset - dRightLaneWidth,
            dRUOWallHeight + dRLOWallHeight + dROFloorHeight,
            dClampedYaw,
            dClampedPitch,
            0.0,
            &dX,
            &dY,
            &dZ);
          setgpoint(iChunkIdx, 5, dWallCalc3 + dX, dWallCalc2 + dY, dZ);
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] >= 0) {
          rotatepoint(0.0, dLeftLaneWidth + dLeftShoulderWidth, dRoofHeight + dLeftShoulderHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
        } else {
          dLeftShoulderHeight = 0.0;
          rotatepoint(0.0, dLeftLaneWidth, dRoofHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
        }
        setpoint(iChunkIdx, 1, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] >= 0) {
          rotatepoint(0.0, -dRightLaneWidth - dRightShoulderWidth, dRoofHeight + dRightShoulderHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
        } else {
          dRightShoulderHeight = 0.0;
          rotatepoint(0.0, -dRightLaneWidth, dRoofHeight, dClampedYaw, dClampedPitch, dRoll, &dX, &dY, &dZ);
        }
        setpoint(iChunkIdx, 5, dWallCalc3 + dX, dWallCalc2 + dY, dWallCalc1 + dZ);
        TrackInfo[iTrackInfoIdx].fLShoulderWidth = (float)dLeftShoulderWidth;// Store track info data for current chunk
        TrackInfo[iTrackInfoIdx].fLShoulderHeight = (float)dLeftShoulderHeight;
        TrackInfo[iTrackInfoIdx].fRShoulderWidth = (float)dRightShoulderWidth;
        TrackInfo[iTrackInfoIdx].fRShoulderHeight = (float)dRightShoulderHeight;
        TrackInfo[iTrackInfoIdx].fRoofHeight = (float)dRoofHeight;
        dAngleCalc1 = dRadiansPerDegree;
        dLeftAngle = dLeftShoulderWidth != 0.0 ? atan2(dLeftShoulderHeight / dLeftShoulderWidth, 1.0) * 16384.0 * dRadiansPerDegree : 0.0;
        //_CHP();
        iTemp3 = (int)dLeftAngle;
        dRightAngle = dRightShoulderWidth != 0.0 ? dRightShoulderHeight / dRightShoulderWidth : 0.0;
        TrackInfo[iTrackInfoIdx].iLeftBankAngle = iTemp3;
        //_CHP();
        iTemp3 = (int)(dAngleCalc1 * (atan2(dRightAngle, 1.0) * 16384.0));
        dAILine1_1 = dAILine1;
        TrackInfo[iTrackInfoIdx].iRightBankAngle = iTemp3;
        uiLocalDataOffset1 = uiLocalDataOffset;
        localdata[uiLocalDataOffset / 0x80].fAILine1 = (float)dAILine1_1;
        localdata[uiLocalDataOffset1 / 0x80].fAILine2 = (float)dAILine2;
        localdata[uiLocalDataOffset1 / 0x80].fAILine3 = (float)dAILine3;
        localdata[uiLocalDataOffset1 / 0x80].fAILine4 = (float)dAILine4;
        //*(float *)((char *)&localdata[0].fAILine2 + uiLocalDataOffset1) = dAILine2;
        //*(float *)((char *)&localdata[0].fAILine3 + uiLocalDataOffset1) = dAILine3;
        //*(float *)((char *)&localdata[0].fAILine4 + uiLocalDataOffset1) = dAILine4;
        uiLocalDataOffset2 = uiLocalDataOffset;
        localdata[uiLocalDataOffset / 0x80].iCenterGrip = iTrackGrip;
        dAIMaxSpeed_1 = dAIMaxSpeed;
        localdata[uiLocalDataOffset2 / 0x80].iLeftShoulderGrip = iLeftShoulderGrip;
        localdata[uiLocalDataOffset2 / 0x80].iRightShoulderGrip = iRightShoulderGrip;
        //*(int *)((char *)&localdata[0].iLeftShoulderGrip + uiLocalDataOffset2) = iLeftShoulderGrip;
        //*(int *)((char *)&localdata[0].iRightShoulderGrip + uiLocalDataOffset2) = iRightShoulderGrip;
        uiGroundColourOffset1 = uiGroundColourOffset;
        TrackInfo[iTrackInfoIdx].iLeftSurfaceType = -100;
        localdata[uiLocalDataOffset2 / 0x80].fAIMaxSpeed = (float)dAIMaxSpeed_1;
        //*(float *)((char *)&localdata[0].fAIMaxSpeed + uiLocalDataOffset2) = dAIMaxSpeed_1;
        iOFloorType = GroundColour[uiGroundColourOffset1 / 0x14][GROUND_COLOUR_OFLOOR];
        //iOFloorType = *(int *)((char *)&GroundColour[0][GROUND_COLOUR_OFLOOR] + uiGroundColourOffset1);
        TrackInfo[iTrackInfoIdx].iRightSurfaceType = -100;// Determine surface grip types for physics
        if (iOFloorType != -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 0;
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 0;
        }
        if (GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && !TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL]
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 1;
        }
        if (GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && !TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL]
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 1;
        }
        if (GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && !TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL]
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 2;
        }
        if (GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && !TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL]
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 2;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] > 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 3;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] > 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 3;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] > 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 4;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] > 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 4;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] < 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 5;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] < 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ >= (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 5;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] < 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[1].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[2].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 6;
        }
        if (TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] < 0
          && GroundPt[uiGroundPtOffset / 0x48].pointAy[4].fZ < (double)GroundPt[uiGroundPtOffset / 0x48].pointAy[3].fZ
          && GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -2) {
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 6;
        }
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -1 && TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] > 0)
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 7;
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -1 && TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] > 0)
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 7;
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] >= 0 && TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] > 0)
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 8;
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] >= 0 && TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] > 0)
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 8;
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -1 && TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL] < 0)
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 9;
        if (GroundColour[uiGroundColourOffset / 0x14][GROUND_COLOUR_OFLOOR] == -1 && TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL] < 0)
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 9;
        iLeftSurfaceType = TrackInfo[iTrackInfoIdx].iLeftSurfaceType;
        if ((!iLeftSurfaceType || iLeftSurfaceType == 2) && (abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_LANE]) & SURFACE_FLAG_BOUNCE_20) == 0)
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 1;
        iRightSurfaceType = TrackInfo[iTrackInfoIdx].iRightSurfaceType;
        if ((!iRightSurfaceType || iRightSurfaceType == 2) && (abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_LANE]) & SURFACE_FLAG_BOUNCE_20) == 0)
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 1;
        iLeftSurfCheck = TrackInfo[iTrackInfoIdx].iLeftSurfaceType;
        if ((iLeftSurfCheck == 6 || iLeftSurfCheck == 9) && (abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_LANE]) & SURFACE_FLAG_BOUNCE_20) == 0)
          TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 5;
        iRightSurfCheck = TrackInfo[iTrackInfoIdx].iRightSurfaceType;
        if ((iRightSurfCheck == 6 || iRightSurfCheck == 9) && (abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_LANE]) & SURFACE_FLAG_BOUNCE_20) == 0)
          TrackInfo[iTrackInfoIdx].iRightSurfaceType = 5;
        if ((abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_LEFT_WALL]) & SURFACE_FLAG_SKIP_RENDER) != 0) {
          switch (TrackInfo[iTrackInfoIdx].iLeftSurfaceType) {
            case 3:
            case 5:
              TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 1;
              break;
            case 4:
            case 6:
              TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 2;
              break;
            case 7:
            case 8:
            case 9:
              TrackInfo[iTrackInfoIdx].iLeftSurfaceType = 0;
              break;
            default:
              break;
          }
        }
        if ((abs(TrakColour[iTrakColourIdx][TRAK_COLOUR_RIGHT_WALL]) & SURFACE_FLAG_SKIP_RENDER) != 0) {
          switch (TrackInfo[iTrackInfoIdx].iRightSurfaceType) {
            case 3:
            case 5:
              TrackInfo[iTrackInfoIdx].iRightSurfaceType = 1;
              break;
            case 4:
            case 6:
              TrackInfo[iTrackInfoIdx].iRightSurfaceType = 2;
              break;
            case 7:
            case 8:
            case 9:
              TrackInfo[iTrackInfoIdx].iRightSurfaceType = 0;
              break;
            default:
              break;
          }
        }
      }
      dCos = cos(0.0174532925199 * dPitch);     // Calculate cosine for length projection
      ++iTrakColourIdx;
      ++iTrackInfoIdx;
      ++iChunkIdx;
      p_iCenterSurfType += 6;
      dWallCalc3 = cos(0.0174532925199 * dYaw) * (dCos * dLength) + dWallCalc3;// Update position coordinates using trigonometry
      p_iRightSurfType += 6;
      p_iLeftWallType += 6;
      p_uiRoofType += 6;
      p_iLLOWallType += 5;
      p_iOFloorType += 5;
      p_iRUOWallType += 5;
      ++p_fGroundLevel;
      ++p_nAudioAboveTrigger;
      dTempYaw = dYaw;                          // Increment array offsets and pointers for next chunk
      ++p_nSampleMin;
      dWallCalc2 = dCos * dLength * sin(0.0174532925199 * dYaw) + dWallCalc2;
      ++p_iHorizonType;
      dTempPitch = dPitch;
      uiGroundLevelOffset += 4;
      iSubdivArrayBaseOffset += 11;
      p_iRightWallType += 6;
      p_iRLOWallType += 5;
      uiLocalDataOffset += 128;
      ++p_nAudioTriggerSpeed;
      uiGroundPtOffset += 72;
      uiGroundColourOffset += 20;
      uiTrakViewOffset += 8;
      dWallCalc1 = sin(0.0174532925199 * dPitch) * dLength + dWallCalc1;
    } while (iChunkIdx < TRAK_LEN);
  }
  if (!bMinimalMode)
    ReadAnimData(pFile_2, &pCurrDataPtr);       // Load animation data if not in minimal mode
  if (bMinimalMode) {
    if (iTrackIdx_1 >= 0)
      min_skip_stuff(&pCurrDataPtr);
  } else {
    initlocaltrack();                           // Initialize track objects and car placement
    InitTowers();
    InitBuildings();
    placecars();
    if (iTrackIdx_1 >= 0) {
      start_f = pCurrDataPtr;                   // Read additional track data: stunts, textures, buildings
      readstuntdata(&pCurrDataPtr);
      read_texturemap(&pCurrDataPtr);
      read_bldmap(&pCurrDataPtr);
      read_backs(&pCurrDataPtr);
    }
    if (Play_View == 1)
      testteaminit(&Car[ViewType[0]]);
    else
      initcarview(ViewType[0], 0);
    if (player_type == 2)
      initcarview(ViewType[1], 1);
    initpits();
  }
  if (iTrackIdx_1 >= 0) {
    readline2(&pCurrDataPtr, "i", &actualtrack);// Read actual track ID and validate against requested
    if (iCompactedFlag && replaytype != 2 && iTrackIdx_1 != actualtrack && !bMinimalMode) {
      ErrorBoxExit("Cheat!!!! Track %d is really track %d!!!\n", iTrackIdx_1, actualtrack);
      //__asm { int     10h; -VIDEO - SET VIDEO MODE }
      //printf("Cheat!!!! Track %d is really track %d!!!\n", iTrackIdx_1, actualtrack);
      //doexit();
    }
    cur_laps[0] = 0;                            // Initialize lap counters
    if (!meof) {
      readline2(&pCurrDataPtr, "iiiiii", cur_laps, &cur_laps[1], &cur_laps[2], &cur_laps[3], &cur_laps[4], &cur_laps[5]);// Read lap configuration and map settings
      readline2(&pCurrDataPtr, "fif", &cur_mapsize, &cur_mapsect, &cur_TrackZ);
    }
    if (meof) {
      iDifficulty = 0;                          // Use default values if EOF reached
      iTrackLapOffset = 6 * actualtrack;
      do {
        iLapValue = track_laps[0][iTrackLapOffset++];
        cur_laps[iDifficulty] = iLapValue;
        ++iDifficulty;
        //iLapValue = track_laps[0][iTrackLapOffset++];
        //*(_DWORD *)&samplemin[2 * iDifficulty + 498] = iLapValue;// offset into cur_laps
      } while (iDifficulty < 6);
      cur_mapsize = mapsize[actualtrack];
      dTrackZ = TrackZs[actualtrack];
      cur_mapsect = mapsect[actualtrack];
      cur_TrackZ = (float)dTrackZ;
    }
    fclose(pFile_2);
  }
  if (!bMinimalMode) {                                             // Cleanup and load textures if not in minimal mode
    if (pData)
      fre((void **)&pData);
    if (iTrackIdx_1 >= 0) {
      activatestunts();
      LoadBldTextures();
      LoadTextures();
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004D6C0
void read_backs(uint8 **ppTrackData)
{
  int iBacksEnd; // ecx
  int iFoundBacks; // esi
  uint8 *pTrackData; // edi
  int iTextureIndex; // [esp+0h] [ebp-1Ch] BYREF
  int iReplacementTexture; // [esp+4h] [ebp-18h] BYREF

  iBacksEnd = 0;
  iFoundBacks = 0;
  do {
    if (iFoundBacks) {
      readline2(ppTrackData, "ii", &iTextureIndex, &iReplacementTexture);
      if ((unsigned int)iTextureIndex < 0x100)
        texture_back[iTextureIndex] = iReplacementTexture;
      else
        iBacksEnd = -1;
    } else {
      pTrackData = *ppTrackData;
      memgets(fp_buf, ppTrackData);
      if (pTrackData == *ppTrackData)
        return;
      if (fp_buf[0] == 'B' && fp_buf[1] == 'A' && fp_buf[2] == 'C' && fp_buf[3] == 'K' && fp_buf[4] == 'S')
        iFoundBacks = -1;
    }
  } while (!iBacksEnd);
}

//-------------------------------------------------------------------------------------------------
//0004D780
void read_texturemap(uint8 **ppTrackData)
{
  char *pszBufPtr; // eax
  char byCurrentChar; // dh
  char *pszFilenamePtr; // eax
  int iIndex; // edx
  char byChar; // bl

  do
    memgets(fp_buf, ppTrackData);      // Read lines from track data until we find a line starting with 'T' (texture map entry)
  while (fp_buf[0] != 84);
  pszBufPtr = fp_buf;
  do
    byCurrentChar = *++pszBufPtr;               // Scan forward in the line to find the ':' separator
  while (byCurrentChar != 58);
  pszFilenamePtr = pszBufPtr + 1;               // Start of filename is right after the ':'
  iIndex = 0;
  do {
    byChar = *pszFilenamePtr++;                 // Copy filename character by character
    texture_file[iIndex] = byChar;
    if ((uint8)byChar >= 0x61u && (uint8)byChar <= 0x7Au)// Convert lowercase letters to uppercase (a-z -> A-Z)
      texture_file[iIndex] = byChar - 32;
    ++iIndex;
  } while (*pszFilenamePtr != 13 && *pszFilenamePtr != 10);
  texture_file[iIndex] = 0;                     // Null-terminate the texture filename string
}

//-------------------------------------------------------------------------------------------------
//0004D7F0
void read_bldmap(uint8 **ppTrackData)
{
  uint8 *pbyOriginalTrackData; // ebp
  int iFoundBLD; // ecx
  uint8 *pbyPrevPos; // esi
  uint8 *pbyBufPtr; // eax
  char byCurrentChar; // dh
  char *pszFilenamePtr; // eax
  int iIndex; // edx
  char byChar; // cl

  pbyOriginalTrackData = *ppTrackData;          // Save original track data pointer to restore later
  iFoundBLD = 0;
  *ppTrackData = start_f;              // Reset track data pointer to start of file for searching
  do {
    pbyPrevPos = *ppTrackData;
    memgets(fp_buf, ppTrackData);               // Read next line from track data
    if (pbyPrevPos == *ppTrackData)           // Check if we reached end of file (pointer didn't advance)
      iFoundBLD = -1;
    if (fp_buf[0] == 66 && fp_buf[1] == 76 && fp_buf[2] == 68)// Check if line starts with "BLD" (building map entry)
      iFoundBLD = -1;
  } while (!iFoundBLD);
  if (fp_buf[0] == 66 && fp_buf[1] == 76 && fp_buf[2] == 68)// Process the BLD line if found
  {
    pbyBufPtr = fp_buf;
    if (fp_buf[0] != 58) {
      do
        byCurrentChar = *++pbyBufPtr;           // Scan forward to find ':' separator
      while (byCurrentChar != 58);
    }
    pszFilenamePtr = (char *)(pbyBufPtr + 1);   // Start of filename is right after the ':'
    iIndex = 0;
    do {
      byChar = *pszFilenamePtr++;               // Copy building texture filename character by character
      bldtex_file[iIndex] = byChar;
      if ((uint8)byChar >= 0x61u && (uint8)byChar <= 0x7Au)// Convert lowercase letters to uppercase (a-z -> A-Z)
        bldtex_file[iIndex] = byChar - 32;
      ++iIndex;
    } while (*pszFilenamePtr != 13 && *pszFilenamePtr != 10);
    bldtex_file[iIndex] = 0;                    // Null-terminate the building texture filename
  } else {
    strcpy((char *)fp_buf, "building.drh");     // Use default building texture if no BLD entry found
  }
  *ppTrackData = pbyOriginalTrackData;          // Restore original track data pointer
}

//-------------------------------------------------------------------------------------------------
//0004D8D0
void readstuntdata(uint8 **pTrackData)
{
  int *pStuntData; // edx
  int *pDataItr; // edx
  int iFlags; // [esp+0h] [ebp-3Ch] BYREF
  int iRampSideLength; // [esp+4h] [ebp-38h] BYREF
  int iTimeFlat; // [esp+8h] [ebp-34h] BYREF
  int iTimeBulging; // [esp+Ch] [ebp-30h] BYREF
  int iTimingGroup; // [esp+10h] [ebp-2Ch] BYREF
  int iHeight; // [esp+14h] [ebp-28h] BYREF
  int iTickStartIdx; // [esp+18h] [ebp-24h] BYREF
  int iNumTicks; // [esp+1Ch] [ebp-20h] BYREF
  int iChunkCount; // [esp+20h] [ebp-1Ch] BYREF
  int iGeometryIndex; // [esp+24h] [ebp-18h] BYREF

  pStuntData = (int *)scrbuf;                   // tStuntData
  totalramps = 0;

  // Free existing stunts at the beginning of scrbuf
  freestunts(pTrackData, (int *)scrbuf);

  // Read stunt entries until we encounter a terminator (iGeometryIndex == -1)
  do {
    readline2(

      pTrackData,
      "iiiiiiiiii",
      &iGeometryIndex,
      &iChunkCount,
      &iNumTicks,
      &iTickStartIdx,
      &iTimingGroup,
      &iHeight,
      &iTimeBulging,
      &iTimeFlat,
      &iRampSideLength,
      &iFlags);
    if (iGeometryIndex != -1) {
      pDataItr = pStuntData + 1;
      *(pDataItr++ - 1) = iGeometryIndex;
      *(pDataItr++ - 1) = iChunkCount;
      *(pDataItr++ - 1) = iNumTicks;
      *(pDataItr++ - 1) = iTickStartIdx;
      *(pDataItr++ - 1) = iTimingGroup;
      *(pDataItr++ - 1) = iHeight;
      *(pDataItr++ - 1) = iTimeBulging;
      *(pDataItr++ - 1) = iTimeFlat;
      *(pDataItr - 1) = iRampSideLength;
      pStuntData = pDataItr + 1;
      *(pStuntData - 1) = iFlags;
    }
  } while (iGeometryIndex != -1);

  // Write the terminator to mark the end of stunt data
  *pStuntData = -1;
}

//-------------------------------------------------------------------------------------------------
//0004D9B0
void activatestunts()
{
  uint8 *pScrBuf; // esi
  int iRampIdx; // ebp
  int iGeometryIdx; // edi
  int iChunkCount_1; // eax
  int *pBufItr; // esi
  int iNumTicks_1; // eax
  int iTickStartIdx_1; // eax
  int iTimingGroup_1; // eax
  int iHeight_1; // eax
  int iTimeBulging_1; // eax
  int iTimeFlat_1; // eax
  int iRampSideLength_1; // eax
  int iFlags; // eax
  tStuntData *pRampBuf; // eax
  int iNewTotalRamps; // edx
  int iTimeFlat; // [esp+0h] [ebp-3Ch]
  int iTimeBulging; // [esp+4h] [ebp-38h]
  int iRampSideLength; // [esp+Ch] [ebp-30h]
  int iTickStartIdx; // [esp+10h] [ebp-2Ch]
  int iNumTicks; // [esp+14h] [ebp-28h]
  int iChunkCount; // [esp+18h] [ebp-24h]
  int iTimingGroup; // [esp+1Ch] [ebp-20h]
  int iHeight; // [esp+20h] [ebp-1Ch]

  pScrBuf = scrbuf;
  iRampIdx = 0;
  totalramps = 0;
  do {
    iGeometryIdx = *(int *)pScrBuf;
    pScrBuf += 4;
    if (iGeometryIdx == -1) {
      ramp[iRampIdx] = 0;
    } else {
      iChunkCount_1 = *(int *)pScrBuf;
      pBufItr = (int *)(pScrBuf + 4);
      iChunkCount = iChunkCount_1;
      iNumTicks_1 = *pBufItr++;
      iNumTicks = iNumTicks_1;
      iTickStartIdx_1 = *pBufItr++;
      iTickStartIdx = iTickStartIdx_1;
      iTimingGroup_1 = *pBufItr++;
      iTimingGroup = iTimingGroup_1;
      iHeight_1 = *pBufItr++;
      iHeight = iHeight_1;
      iTimeBulging_1 = *pBufItr++;
      iTimeBulging = iTimeBulging_1;
      iTimeFlat_1 = *pBufItr++;
      iTimeFlat = iTimeFlat_1;
      iRampSideLength_1 = *pBufItr++;
      iRampSideLength = iRampSideLength_1;
      iFlags = *pBufItr;
      pScrBuf = (uint8 *)(pBufItr + 1);
      pRampBuf = initramp(iGeometryIdx, iChunkCount, iNumTicks, iTickStartIdx, iTimingGroup, iHeight, iTimeBulging, iTimeFlat, iRampSideLength, iFlags);
      iNewTotalRamps = totalramps + 1;
      ramp[iRampIdx] = pRampBuf;
      totalramps = iNewTotalRamps;
    }
    ++iRampIdx;
  } while (iGeometryIdx != -1);
}

//-------------------------------------------------------------------------------------------------
//0004DA90
void ReadAnimData(FILE *pFile, uint8 **ppFileData)
{
  int iSignType; // [esp+0h] [ebp-14h] BYREF
  int iSignTex; // [esp+4h] [ebp-10h] BYREF

  iSignType = 0;
  iSignTex = 0;
  if (pFile) {
    do {
      readline2(ppFileData, "ss", &iSignType, &iSignTex);
      if ((iSignType & 0x8000u) == 0) {
        advert_list[(int16)iSignType] = iSignTex;
      } else if ((int16)iSignType < -1) {
        advert_list[-iSignType - 1] = 0;
        //VisibleBuildings_variable_1[-(__int16)iSignType] = 0;// offset into advert_list
      }
    } while ((iSignTex & 0x8000u) == 0);
  }
}

//-------------------------------------------------------------------------------------------------
//0004DB00
void readline(FILE *pFile, const char *szFmt, ...)
{
  char szBuffer[0x200];
  char *szTok;
  va_list args;
  int iFieldIndex = 0;
  char *szDelims = delims;

  va_start(args, szFmt);
  double dScale = 256.0;

  // Skip comments and blank lines
  while (1) {
    if (!fgets(szBuffer, sizeof(szBuffer), pFile)) {
      va_end(args);
      return; // EOF or error
    }

    szTok = strtok(szBuffer, szDelims);
    if (!szTok) continue;

    if (strncmp(szTok, "//", 2) == 0 || strchr(szTok, ';') == szTok) {
      continue;
    }
    break;
  }

  char *szFmtPtr = (char *)szFmt;
  char *szCurrTok = szTok;

  while (*szFmtPtr && szCurrTok) {
    void *pDst = va_arg(args, void *);

    switch (*szFmtPtr) {
      case 'D':
      { // double scaled
        double val = strtod(szCurrTok, NULL) * dScale;
        *(double *)pDst = val;
        break;
      }
      case 'F':
      { // float scaled
        float val = (float)(strtod(szCurrTok, NULL) * dScale);
        *(float *)pDst = val;
        break;
      }
      case 'I':
      { // int shifted
        int val = strtol(szCurrTok, NULL, 10) << 8;
        *(int *)pDst = val;
        break;
      }
      case 'S':
      { // short shifted
        short val = (short)(strtol(szCurrTok, NULL, 10) << 8);
        *(short *)pDst = val;
        break;
      }
      case 'd':
      {
        *(double *)pDst = strtod(szCurrTok, NULL);
        break;
      }
      case 'f':
      {
        *(float *)pDst = (float)strtod(szCurrTok, NULL);
        break;
      }
      case 'i':
      {
        *(int *)pDst = strtol(szCurrTok, NULL, 10);
        break;
      }
      case 's':
      {
        *(short *)pDst = (short)strtol(szCurrTok, NULL, 10);
        break;
      }
      case 'u':
      {
        *(uint8*)pDst = (uint8)strtol(szCurrTok, NULL, 10);
        break;
      }
      case 'C':
      case 'c':
      {
        char *szDest = (char *)pDst;
        const char *szSrc = szCurrTok;
        // Custom wide copy: 2 bytes per char
        while (*szSrc) {
          *szDest++ = *szSrc++;
          if (!*szSrc)
            break;
          *szDest++ = *szSrc++;
        }
        *szDest = '\0';
        break;
      }
    }

    szFmtPtr++;
    szCurrTok = strtok(NULL, szDelims);
    iFieldIndex++;
  }

  va_end(args);
}

//-------------------------------------------------------------------------------------------------
//0004DDF0
uint8 *memgets(uint8 *pDst, uint8 **ppSrc)
{
  int iEof; // esi
  uint8 *pDst2; // eax
  uint8 byte; // bl
  uint8 *ppSrcNext; // ebx

  iEof = 0;
  do {
    pDst2 = pDst;
    do {
      byte = **ppSrc;
      *pDst2 = byte;
      if (byte == 0x1A)
        iEof = -1;
      ppSrcNext = *ppSrc + 1;
      *ppSrc = ppSrcNext;
      ++pDst2;
    } while (*(ppSrcNext - 1) > 13u && !iEof);
  } while (*pDst <= 13u);
  meof = iEof;
  return pDst2;
}

//-------------------------------------------------------------------------------------------------
//0004DE30
void readline2(uint8 **ppFileData, const char *pszFormat, ...)
{
  char szLineBuffer[512];
  va_list va;
  va_list vaCopy;
  char *pszToken;
  uint32 uiFormatIndex;

  va_start(va, pszFormat);
  va_copy(vaCopy, va);

  while (1) {
      // Read next line from file into buffer
    memgets((uint8 *)szLineBuffer, ppFileData);

    // Check for end of file
    if (meof)
      break;

  // Tokenize line using delimiters (whitespace)
    pszToken = strtok(szLineBuffer, delims);

    // Skip C++ style comments (//)
    if (pszToken && strstr(pszToken, "//") == pszToken)
      pszToken = NULL;

  // Skip lines starting with semicolon
    if (pszToken && strstr(pszToken, ";") == pszToken)
      pszToken = NULL;

  // Skip newlines and carriage returns
    if (szLineBuffer[0] == '\n' || szLineBuffer[0] == '\r')
      pszToken = NULL;

    if (pszToken) {
        // Process each format specifier in the format string
      for (uiFormatIndex = 0; uiFormatIndex < strlen(pszFormat); ++uiFormatIndex) {
        switch (pszFormat[uiFormatIndex]) {
          case 'D': // double scaled by 256
          {
            double *pdblDest = va_arg(vaCopy, double *);
            double dblValue = strtod(pszToken, NULL);
            *pdblDest = dblValue * 256.0;
            break;
          }

          case 'F': // float scaled by 256
          {
            float *pfDest = va_arg(vaCopy, float *);
            double dblFloatValue = strtod(pszToken, NULL);
            *pfDest = (float)(dblFloatValue * 256.0);
            break;
          }

          case 'I': // 32-bit integer shifted left by 8
          {
            uint32 *puiDest = va_arg(vaCopy, uint32 *);
            int32 iIntValue = strtol(pszToken, NULL, 10);
            *puiDest = iIntValue << 8;
            break;
          }

          case 'S': // 16-bit short shifted left by 8
          {
            int16 *pnDest = va_arg(vaCopy, int16 *);
            int16 nShortValue = (int16)strtol(pszToken, NULL, 10);
            *pnDest = nShortValue << 8;
            break;
          }

          case 'd': // double (no scaling)
          {
            double *pdblDest = va_arg(vaCopy, double *);
            double dblPlainValue = strtod(pszToken, NULL);
            *pdblDest = dblPlainValue;
            break;
          }

          case 'f': // float (no scaling)
          {
            float *pfDest = va_arg(vaCopy, float *);
            double fltPlainValue = strtod(pszToken, NULL);
            *pfDest = (float)fltPlainValue;
            break;
          }

          case 'i': // 32-bit integer (no shifting)
          {
            int32 *piDest = va_arg(vaCopy, int32 *);
            int32 iPlainValue = strtol(pszToken, NULL, 10);
            *piDest = iPlainValue;
            break;
          }

          case 's': // 16-bit short (no shifting)
          {
            int16 *pnDest = va_arg(vaCopy, int16 *);
            int16 nPlainValue = (int16)strtol(pszToken, NULL, 10);
            *pnDest = nPlainValue;
            break;
          }

          case 'C': // copy string (uppercase)
          case 'c': // copy string (lowercase)
          {
            char *pszDestStr = va_arg(vaCopy, char *);
            strcpy(pszDestStr, pszToken);
            break;
          }

          case 'u': // unsigned byte
          {
            uint8 *pbyDest = va_arg(vaCopy, uint8 *);
            uint8 byByteValue = (uint8)strtol(pszToken, NULL, 10);
            *pbyDest = byByteValue;
            break;
          }
        }

        // Get next token from the line
        pszToken = strtok(NULL, delims);
        if (!pszToken)
          break;
      }

      va_end(vaCopy);
      va_end(va);
      return;
    }
  }

  va_end(vaCopy);
  va_end(va);
}

//-------------------------------------------------------------------------------------------------
//0004E190
void rotatepoint(double dX, double dY, double dZ, double dYaw, double dPitch, double dRoll, double *pdOutX, double *pdOutY, double *pdOutZ)
{
  long double dblCosX; // st7
  long double dblSinY; // st6
  long double dblSinZ; // st5
  double dblSinX; // [esp+8h] [ebp-28h]
  long double dblCosZ; // [esp+10h] [ebp-20h]
  double dblCosY; // [esp+18h] [ebp-18h]
  long double dYawRad; // [esp+4Ch] [ebp+1Ch]
  long double dPitchRad; // [esp+54h] [ebp+24h]
  long double dRollRad; // [esp+5Ch] [ebp+2Ch]

  dYawRad = dYaw * 0.0174532925199;             // Convert X angle from degrees to radians (PI/180 = 0.0174532925199)
  dPitchRad = dPitch * 0.0174532925199;         // Convert Y angle from degrees to radians
  dRollRad = dRoll * 0.0174532925199;           // Convert Z angle from degrees to radians
  dblCosX = cos(dYawRad);                       // Calculate cos(X angle)
  dblSinY = sin(dPitchRad);                     // Calculate sin(Y angle)
  dblSinZ = sin(dRollRad);                      // Calculate sin(Z angle)
  dblSinX = sin(dYawRad);                       // Calculate sin(X angle)
  dblCosZ = cos(dRollRad);                      // Calculate cos(Z angle)
  dblCosY = cos(dPitchRad);                     // Calculate cos(Y angle)
  *pdOutX = (dblCosX * dblSinY * dblSinZ - dblSinX * dblCosZ) * dY + dX * dblCosX * dblCosY + (-dblCosX * dblSinY * dblCosZ - dblSinX * dblSinZ) * dZ;// Apply 3D rotation matrix to calculate new X coordinate
  *pdOutY = (dblSinX * dblSinY * dblSinZ + dblCosX * dblCosZ) * dY + dX * dblSinX * dblCosY + (-dblSinX * dblSinY * dblCosZ + dblCosX * dblSinZ) * dZ;// Apply 3D rotation matrix to calculate new Y coordinate
  *pdOutZ = dblSinY * dX + dblSinZ * (-dblCosY * dY) + dZ * dblCosY * dblCosZ;// Apply 3D rotation matrix to calculate new Z coordinate
}

//-------------------------------------------------------------------------------------------------
//0004E2D0
void setpoint(int iChunkIdx, int iPointIdx, double dX, double dY, double dZ)
{
  TrakPt[iChunkIdx].pointAy[iPointIdx].fX = (float)dX;
  TrakPt[iChunkIdx].pointAy[iPointIdx].fY = (float)dY;
  TrakPt[iChunkIdx].pointAy[iPointIdx].fZ = (float)dZ;
}

//-------------------------------------------------------------------------------------------------
//0004E320
void setgpoint(int iChunkIdx, int iPointIdx, double dX, double dY, double dZ)
{
  GroundPt[iChunkIdx].pointAy[iPointIdx].fX = (float)dX;
  GroundPt[iChunkIdx].pointAy[iPointIdx].fY = (float)dY;
  GroundPt[iChunkIdx].pointAy[iPointIdx].fZ = (float)dZ;
}

//-------------------------------------------------------------------------------------------------
//0004E370
void resetcars()
{
  int iCarIdx; // esi
  tCar *pCurrCar; // ecx

  iCarIdx = 0;
  if (numcars > 0) {
    pCurrCar = Car;
    do {
      memset(pCurrCar, 0, sizeof(tCar));
      ++iCarIdx;
      ++pCurrCar;
    } while (iCarIdx < numcars);
  }
}

//-------------------------------------------------------------------------------------------------
//0004E3B0
void min_skip_stuff(uint8 **ppFileData)
{
  char szBuf[256]; // [esp-100h] [ebp-130h] BYREF
  int iData9; // [esp+0h] [ebp-30h] BYREF
  int iData8; // [esp+4h] [ebp-2Ch] BYREF
  int iData7; // [esp+8h] [ebp-28h] BYREF
  int iData6; // [esp+Ch] [ebp-24h] BYREF
  int iData5; // [esp+10h] [ebp-20h] BYREF
  int iData4; // [esp+14h] [ebp-1Ch] BYREF
  int iData3; // [esp+18h] [ebp-18h] BYREF
  int iData2; // [esp+1Ch] [ebp-14h] BYREF
  int iData1; // [esp+20h] [ebp-10h] BYREF
  int iData0; // [esp+24h] [ebp-Ch] BYREF
  do {
    do
      readline2(ppFileData, "ii", &iData0, &iData1);
    while (iData0 != -1);
  } while (iData1 != -1);
  do
    readline2(ppFileData, "iiiiiiiiii", &iData0, &iData1, &iData2, &iData3, &iData4, &iData5, &iData6, &iData7, &iData8, &iData9);
  while (iData0 != -1);
  do
    readline2(ppFileData, "c", szBuf);
  while (!strstr(szBuf, "BACKS:"));
  do
    readline2(ppFileData, "ii", &iData0, &iData1);
  while (iData0 != -1);
}

//-------------------------------------------------------------------------------------------------
