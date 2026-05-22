#include "transfrm.h"
#include "3d.h"
#include "control.h"
#include "loadtrak.h"
#include "userfns.h"
#include <math.h>
//-------------------------------------------------------------------------------------------------

float vk1;  //00189950
int xp;     //00189954
float vk2;  //00189958
float vk3;  //0018995C
float vk4;  //00189960
float vk5;  //00189964
float vk6;  //00189968
float vk7;  //0018996C
float vk8;  //00189970
float vk9;  //00189974
int yp;     //00189978

//-------------------------------------------------------------------------------------------------
//00060A40
void calculatetransform(int iTrackChunkIdx, int iDirection, int iElevation, int iTilt,
                        float fViewX, float fViewY, float fViewZ,
                        float fPosX, float fPosY, float fPosZ)
{
  double v10; // st7
  double v11; // st7
  double dVk9; // st7
  tData *pData; // eax
  double v14; // st6
  double dZ; // st5
  float v16; // [esp+0h] [ebp-2Ch]
  float v17; // [esp+4h] [ebp-28h]
  float v18; // [esp+8h] [ebp-24h]
  float v19; // [esp+Ch] [ebp-20h]
  float v20; // [esp+10h] [ebp-1Ch]
  float v21; // [esp+14h] [ebp-18h]
  float v22; // [esp+1Ch] [ebp-10h]
  float v23; // [esp+20h] [ebp-Ch]
  float v24; // [esp+24h] [ebp-8h]

  v10 = tcos[iDirection] * tsin[iElevation];
  vk1 = tsin[iDirection] * tcos[iTilt] - (float)v10 * tsin[iTilt];
  vk2 = -tsin[iDirection] * tsin[iTilt] - (float)v10 * tcos[iTilt];
  vk3 = tcos[iDirection] * tcos[iElevation];
  v11 = tsin[iDirection] * tsin[iElevation];
  vk4 = -tcos[iDirection] * tcos[iTilt] - (float)v11 * tsin[iTilt];
  vk5 = tcos[iDirection] * tsin[iTilt] - (float)v11 * tcos[iTilt];
  vk6 = tsin[iDirection] * tcos[iElevation];
  vk7 = tcos[iElevation] * tsin[iTilt];
  vk8 = tcos[iElevation] * tcos[iTilt];
  dVk9 = (double)tsin[iElevation];
  viewx = fViewX;
  viewy = fViewY;
  viewz = fViewZ;
  vk9 = (float)dVk9;
  if (iTrackChunkIdx != -1) {
    pData = &localdata[iTrackChunkIdx];
    viewx = pData->pointAy[0].fX * fViewX
      - pData->pointAy[3].fX
      + pData->pointAy[0].fY * fViewY
      + pData->pointAy[0].fZ * fViewZ;
    viewy = pData->pointAy[1].fX * fViewX
      - pData->pointAy[3].fY
      + pData->pointAy[1].fY * fViewY
      + pData->pointAy[1].fZ * fViewZ;
    viewz = fViewY * pData->pointAy[2].fY
      + fViewX * pData->pointAy[2].fX
      - pData->pointAy[3].fZ
      + fViewZ * pData->pointAy[2].fZ;
    v21 = pData->pointAy[0].fY * vk4 + pData->pointAy[0].fX * vk1 + pData->pointAy[0].fZ * vk7;
    v24 = pData->pointAy[0].fY * vk5 + pData->pointAy[0].fX * vk2 + pData->pointAy[0].fZ * vk8;
    v16 = pData->pointAy[0].fY * vk6 + pData->pointAy[0].fX * vk3 + pData->pointAy[0].fZ * vk9;
    v20 = pData->pointAy[1].fX * vk1 + pData->pointAy[1].fY * vk4 + pData->pointAy[1].fZ * vk7;
    v18 = pData->pointAy[1].fX * vk2 + pData->pointAy[1].fY * vk5 + pData->pointAy[1].fZ * vk8;
    v19 = pData->pointAy[1].fX * vk3 + pData->pointAy[1].fY * vk6 + pData->pointAy[1].fZ * vk9;
    v17 = pData->pointAy[2].fX * vk1 + pData->pointAy[2].fY * vk4 + pData->pointAy[2].fZ * vk7;
    v22 = vk8 * pData->pointAy[2].fZ + pData->pointAy[2].fX * vk2 + pData->pointAy[2].fY * vk5;
    v14 = pData->pointAy[2].fX * vk3 + pData->pointAy[2].fY * vk6;
    dZ = pData->pointAy[2].fZ;
    vk1 = v21;
    vk2 = v24;
    vk3 = v16;
    vk4 = v20;
    vk5 = v18;
    vk6 = v19;
    vk7 = v17;
    v23 = vk9 * (float)dZ + (float)v14;
    vk8 = v22;
    vk9 = v23;
  }
  viewx = vk1 * fPosX + vk2 * fPosY + vk3 * fPosZ + viewx;
  viewy = vk4 * fPosX + vk5 * fPosY + vk6 * fPosZ + viewy;
  viewz = fPosX * vk7 + fPosY * vk8 + fPosZ * vk9 + viewz;
}

//-------------------------------------------------------------------------------------------------
//00060DE0
void initlocaltrack()
{                                               // Initialize each track segment's local data
  int i; // edx

  for (i = 0; i < TRAK_LEN; ++i)
    initlocalsection(i);
  dopitchchanges(0, 2);                         // Calculate pitch angles for inner track lanes (left=0, right=2)
  dopitchchanges(3, 4);                         // Calculate pitch angles for outer track lanes (left=3, right=4)
}

//-------------------------------------------------------------------------------------------------
//00060E20
void initlocalsection(int iChunkIdx)
{
  int iNextChunk; // ecx
  int iNextNextChunk; // esi
  int iArrayIndex; // eax
  tData *pData; // edx
  uint32 uiOffset; // eax
  double fTemp; // st7
  int iLoop1; // ebx
  double fAngleY; // st7
  double dSinY; // rt0
  double dTempRot; // st5
  int iLoop2; // ebx
  double fAngleZ; // st7
  double dSinZ; // st6
  double dTempRotZ; // st5
  int iLoop3; // ebx
  double dAngleX; // st6
  double dCosX; // st7
  double dTempRotX; // st5
  //int16 nFpStatus1; // fps
  double fBankAngle; // st7
  //int16 nFpStatus2; // fps
  double fBankDelta; // st7
  double dCosAngleY; // rt0
  double dCosAngleZ; // rt1
  uint32 uiOffset2; // eax
  double dMatrix_2_2; // st7
  double dMatrix_0_2; // st6
  double dMatrix_1_2; // st5
  double dMatrix_2_1; // st4
  double dMatrix_0_1; // st3
  double dTransformedZ; // st2
  int iLoop4; // ebx
  double dNextAngleY; // st7
  double dSinNextY; // rt0
  double dTemPRotNextY; // st5
  int iLoop5; // ebx
  double dNextAngleZ; // st7
  double dSinNextZ; // st6
  double dTempRotNextZ; // st5
  double dAngleYFixed; // st7
  double dAngleZFixed; // st7
  double dAngleXFixed; // st7
  int iYawTemp; // esi
  int iPitchTemp; // ebp
  int iRollTemp; // ebx
  double matrixWork[30]; // [esp+0h] [ebp-2A8h]
  double pointArray[27]; // [esp+F0h] [ebp-1B8h]
  double dRotatedZ; // [esp+1C8h] [ebp-E0h]
  double dRotatedY; // [esp+1D0h] [ebp-D8h]
  double dRotatedX; // [esp+1D8h] [ebp-D0h]
  double dAngleZSaved; // [esp+1E0h] [ebp-C8h]
  double dCosXSaved; // [esp+1E8h] [ebp-C0h]
  double dSinXSaved; // [esp+1F0h] [ebp-B8h]
  double dCosY_1; // [esp+1F8h] [ebp-B0h]
  double dCosYSinZ; // [esp+208h] [ebp-A0h]
  double dSinZMat; // [esp+210h] [ebp-98h]
  double dAngleXBank; // [esp+218h] [ebp-90h]
  double dNextAngleZSaved; // [esp+220h] [ebp-88h]
  double dMatrix00; // [esp+228h] [ebp-80h]
  double dMatrix01; // [esp+230h] [ebp-78h]
  double dSinZSaved; // [esp+238h] [ebp-70h]
  double dMatrix10; // [esp+240h] [ebp-68h]
  double dMatrix12; // [esp+248h] [ebp-60h]
  double dMatrix02; // [esp+250h] [ebp-58h]
  double dMatrix21; // [esp+258h] [ebp-50h]
  double dMatrix10_2; // [esp+260h] [ebp-48h]
  double dCosY; // [esp+268h] [ebp-40h]
  double dNextAngleYSaved; // [esp+270h] [ebp-38h]
  double dMatrix22; // [esp+278h] [ebp-30h]
  double dWorkAngle; // [esp+280h] [ebp-28h]
  double dAngleYSaved; // [esp+288h] [ebp-20h]

  iNextChunk = iChunkIdx + 1;                   // Get next chunk index with wraparound
  if (iChunkIdx + 1 == TRAK_LEN)
    iNextChunk ^= TRAK_LEN;
  iNextNextChunk = iNextChunk + 1;              // Get chunk index two ahead with wraparound
  if (iNextChunk + 1 == TRAK_LEN)
    iNextNextChunk ^= TRAK_LEN;
  iArrayIndex = iChunkIdx;

  // Extract track points for current chunk (left and right edges)
  pointArray[0] = TrakPt[iArrayIndex].pointAy[2].fX;// Point 0: Current left edge
  pointArray[1] = TrakPt[iArrayIndex].pointAy[2].fY;
  pointArray[2] = TrakPt[iArrayIndex].pointAy[2].fZ;
  pointArray[3] = TrakPt[iArrayIndex].pointAy[3].fX;// Point 1: Current right edge
  pointArray[4] = TrakPt[iArrayIndex].pointAy[3].fY;
  pointArray[5] = TrakPt[iArrayIndex].pointAy[3].fZ;
  pointArray[6] = TrakPt[iNextChunk].pointAy[2].fX;// Point 2: Next left edge
  pointArray[7] = TrakPt[iNextChunk].pointAy[2].fY;
  pointArray[8] = TrakPt[iNextChunk].pointAy[2].fZ;
  pointArray[9] = TrakPt[iNextChunk].pointAy[3].fX;// Point 3: Next right edge
  pointArray[10] = TrakPt[iNextChunk].pointAy[3].fY;
  pointArray[11] = TrakPt[iNextChunk].pointAy[3].fZ;
  // Calculate midpoints between left and right track edges
  pointArray[12] = (pointArray[0] + pointArray[3]) * 0.5;
  pointArray[13] = (pointArray[1] + pointArray[4]) * 0.5;
  pointArray[14] = (pointArray[2] + pointArray[5]) * 0.5;
  pointArray[15] = (pointArray[6] + pointArray[9]) * 0.5;
  pointArray[16] = (pointArray[7] + pointArray[10]) * 0.5;
  pointArray[17] = (pointArray[8] + pointArray[11]) * 0.5;
  pointArray[18] = (pointArray[0] + pointArray[6]) * 0.5;
  pointArray[19] = (pointArray[1] + pointArray[7]) * 0.5;
  pointArray[20] = (pointArray[2] + pointArray[8]) * 0.5;
  pointArray[21] = (pointArray[3] + pointArray[9]) * 0.5;
  pointArray[22] = (pointArray[4] + pointArray[10]) * 0.5;
  pointArray[23] = (pointArray[5] + pointArray[11]) * 0.5;
  // Calculate track center position
  pointArray[24] = (pointArray[12] + pointArray[15]) * 0.5;
  pointArray[25] = (pointArray[13] + pointArray[16]) * 0.5;
  pointArray[26] = (pointArray[14] + pointArray[17]) * 0.5;
  pData = &localdata[iChunkIdx];
  // Store negated center position as origin for local coordinate system
  pData->pointAy[3].fX = (float)-pointArray[24];
  pData->pointAy[3].fY = (float)-pointArray[25];
  uiOffset = 0;
  pData->pointAy[3].fZ = (float) - pointArray[26];
  do {
    // Transform all points to local coordinate system (centered at track section)
    pointArray[uiOffset] = pData->pointAy[3].fX + pointArray[uiOffset];
    pointArray[uiOffset + 1] = pData->pointAy[3].fY + pointArray[uiOffset + 1];
    pointArray[uiOffset + 2] = pData->pointAy[3].fZ + pointArray[uiOffset + 2];
    pointArray[uiOffset + 3] = pData->pointAy[3].fX + pointArray[uiOffset + 3];
    pointArray[uiOffset + 4] = pData->pointAy[3].fY + pointArray[uiOffset + 4];
    pointArray[uiOffset + 5] = pData->pointAy[3].fZ + pointArray[uiOffset + 5];
    pointArray[uiOffset + 6] = pData->pointAy[3].fX + pointArray[uiOffset + 6];
    pointArray[uiOffset + 7] = pData->pointAy[3].fY + pointArray[uiOffset + 7];
    fTemp = pData->pointAy[3].fZ + pointArray[uiOffset + 8];

    //moving this above offset increment
    pointArray[uiOffset + 8] = fTemp;
    
    uiOffset += 9u;
    //matrixWork[uiOffset + 29] = fTemp;          // pointArray[uiOffset + 8] = if moved above uiOffset increment
  } while (uiOffset != 27);
  iLoop1 = 0;
  fAngleY = getdirection(pointArray[15] - pointArray[12], pointArray[16] - pointArray[13]);// Calculate Y rotation angle from track direction
  dAngleYSaved = fAngleY;
  do {
    dSinY = sin(-fAngleY);                      // Apply Y-axis rotation to align track with coordinate axes
    dCosY = cos(-fAngleY);
    dTempRot = pointArray[iLoop1 + 1] * dSinY;
    dCosY_1 = dCosY;
    dRotatedX = pointArray[iLoop1] * dCosY - dTempRot;
    dRotatedY = dSinY * pointArray[iLoop1] + pointArray[iLoop1 + 1] * dCosY;

    //moving this above loop increment
    pointArray[iLoop1] = dRotatedX;
    pointArray[iLoop1 + 1] = dRotatedY;

    iLoop1 += 3;
    //LODWORD(matrixWork[iLoop1 + 27]) = LODWORD(dRotatedX);// pointArray[iLoop] = if moved above iLoop increment
    //HIDWORD(matrixWork[iLoop1 + 27]) = HIDWORD(dRotatedX);
    //LODWORD(matrixWork[iLoop1 + 28]) = LODWORD(dRotatedY);
    //HIDWORD(matrixWork[iLoop1 + 28]) = HIDWORD(dRotatedY);
  } while (iLoop1 != 27);
  iLoop2 = 0;
  fAngleZ = getdirection(pointArray[15] - pointArray[12], pointArray[17] - pointArray[14]);// Calculate Z rotation angle (track slope/elevation)
  dAngleZSaved = fAngleZ;
  do {
    dSinZ = sin(fAngleZ);
    dCosY = cos(fAngleZ);
    dTempRotZ = pointArray[iLoop2 + 2] * dSinZ;
    dCosY_1 = dCosY;
    dRotatedX = dTempRotZ + pointArray[iLoop2] * dCosY;
    dRotatedZ = pointArray[iLoop2 + 2] * dCosY + -dSinZ * pointArray[iLoop2];

    //moving this above loop increment
    pointArray[iLoop2] = dRotatedX;
    pointArray[iLoop2 + 2] = dRotatedZ;

    iLoop2 += 3;
    //LODWORD(matrixWork[iLoop2 + 27]) = LODWORD(dRotatedX);// pointArray[iLoop2] = if moved above iLoop2 increment
    //HIDWORD(matrixWork[iLoop2 + 27]) = HIDWORD(dRotatedX);
    //LODWORD(matrixWork[iLoop2 + 29]) = LODWORD(dRotatedZ);
    //HIDWORD(matrixWork[iLoop2 + 29]) = HIDWORD(dRotatedZ);
  } while (iLoop2 != 27);
  iLoop3 = 0;
  dAngleXBank = getdirection(pointArray[19] - pointArray[22], pointArray[23] - pointArray[20]);// Calculate X rotation angle (track banking)
  dAngleX = dAngleXBank;
  do {
    dCosX = cos(dAngleX);
    dCosY = sin(dAngleX);
    dTempRotX = pointArray[iLoop3 + 1] * dCosX;
    dSinXSaved = dCosY;
    dRotatedY = dTempRotX - pointArray[iLoop3 + 2] * dCosY;
    dRotatedZ = pointArray[iLoop3 + 1] * dCosY + pointArray[iLoop3 + 2] * dCosX;

    //moving this above loop increment
    pointArray[iLoop3 + 1] = dRotatedY;
    pointArray[iLoop3 + 2] = dRotatedZ;

    iLoop3 += 3;
    //LODWORD(matrixWork[iLoop3 + 28]) = LODWORD(dRotatedY);// pointArray[iLoop3] = if moved above iLoop3 increment
    //HIDWORD(matrixWork[iLoop3 + 28]) = HIDWORD(dRotatedY);
    //LODWORD(matrixWork[iLoop3 + 29]) = LODWORD(dRotatedZ);
    //HIDWORD(matrixWork[iLoop3 + 29]) = HIDWORD(dRotatedZ);
  } while (iLoop3 != 27);
  dCosXSaved = dCosX;

  // Calculate banking angle difference for physics
  fBankAngle = atan2(pointArray[7] - pointArray[10], pointArray[11] - pointArray[8]);
  //fBankAngle = IF_DATAN2(nFpStatus1, pointArray[7] - pointArray[10], pointArray[11] - pointArray[8]);
  fBankDelta = (fBankAngle - atan2(pointArray[1] - pointArray[4], pointArray[5] - pointArray[2])) * 16384.0 / 6.28318530718 + 0.5;
  //fBankDelta = (fBankAngle - atan2(pointArray[5] - pointArray[2], fBankAngle)) * 16384.0 / 6.28318530718 + 0.5;
  //fBankDelta = (fBankAngle - IF_DATAN2(nFpStatus2, pointArray[5] - pointArray[2], fBankAngle)) * 16384.0 / 6.28318530718 + 0.5;
  floor(fBankDelta);
  //_CHP();
  pData->iBankDelta = (int)-fBankDelta;

  // Store track section dimensions
  pData->fTrackHalfLength = (float)((pointArray[15] - pointArray[12]) * 0.5);
  pData->fTrackHalfWidth = (float)((pointArray[1] - pointArray[4]) * 0.5);

  // Build rotation matrix from three angles
  dCosAngleY = cos(dAngleYSaved);
  dCosY = sin(dAngleYSaved);
  dCosAngleZ = cos(dAngleZSaved);
  dMatrix00 = dCosAngleY * dCosAngleZ;
  dSinZMat = sin(dAngleZSaved);
  dCosYSinZ = dCosAngleY * dSinZMat;
  dWorkAngle = dCosY;
  dMatrix01 = dCosYSinZ * dSinXSaved + -dCosY * dCosXSaved;
  dMatrix02 = -dCosY * dSinXSaved - dCosYSinZ * dCosXSaved;
  dMatrix10_2 = dCosY * dCosAngleZ;
  dMatrix10 = dCosY * dSinZMat * dSinXSaved + dCosAngleY * dCosXSaved;
  dMatrix12 = dCosAngleY * dSinXSaved - dCosY * dSinZMat * dCosXSaved;
  dMatrix21 = dSinXSaved * -dCosAngleZ;
  dMatrix22 = dCosXSaved * dCosAngleZ;

  // Store rotation matrix rows in localdata structure
  pData->pointAy[0].fX = (float)dMatrix00;
  pData->pointAy[0].fY = (float)dMatrix01;
  pData->pointAy[0].fZ = (float)dMatrix02;
  dSinZSaved = dSinZMat;
  pData->pointAy[1].fX = (float)dMatrix10_2;
  pData->pointAy[1].fY = (float)dMatrix10;
  pData->pointAy[1].fZ = (float)dMatrix12;
  pData->pointAy[2].fX = (float)dSinZMat;
  pData->pointAy[2].fY = (float)dMatrix21;
  pData->pointAy[2].fZ = (float)dMatrix22;

  // Process next track section for smooth transitions
  pointArray[0] = TrakPt[iNextChunk].pointAy[2].fX;
  pointArray[1] = TrakPt[iNextChunk].pointAy[2].fY;
  pointArray[2] = TrakPt[iNextChunk].pointAy[2].fZ;
  pointArray[3] = TrakPt[iNextChunk].pointAy[3].fX;
  pointArray[4] = TrakPt[iNextChunk].pointAy[3].fY;
  pointArray[5] = TrakPt[iNextChunk].pointAy[3].fZ;
  pointArray[6] = TrakPt[iNextNextChunk].pointAy[2].fX;
  pointArray[7] = TrakPt[iNextNextChunk].pointAy[2].fY;
  pointArray[8] = TrakPt[iNextNextChunk].pointAy[2].fZ;
  pointArray[9] = TrakPt[iNextNextChunk].pointAy[3].fX;
  pointArray[10] = TrakPt[iNextNextChunk].pointAy[3].fY;
  pointArray[11] = TrakPt[iNextNextChunk].pointAy[3].fZ;
  pointArray[12] = (pointArray[0] + pointArray[3]) * 0.5;
  pointArray[13] = (pointArray[1] + pointArray[4]) * 0.5;
  pointArray[14] = (pointArray[2] + pointArray[5]) * 0.5;
  pointArray[15] = (pointArray[6] + pointArray[9]) * 0.5;
  pointArray[16] = (pointArray[7] + pointArray[10]) * 0.5;
  pointArray[17] = (pointArray[8] + pointArray[11]) * 0.5;
  pointArray[18] = (pointArray[0] + pointArray[6]) * 0.5;
  pointArray[19] = (pointArray[1] + pointArray[7]) * 0.5;
  pointArray[20] = (pointArray[2] + pointArray[8]) * 0.5;
  pointArray[21] = (pointArray[3] + pointArray[9]) * 0.5;
  pointArray[22] = (pointArray[4] + pointArray[10]) * 0.5;
  pointArray[23] = (pointArray[5] + pointArray[11]) * 0.5;
  pointArray[24] = (pointArray[12] + pointArray[15]) * 0.5;
  pointArray[25] = (pointArray[13] + pointArray[16]) * 0.5;
  uiOffset2 = 0;
  pointArray[26] = (pointArray[14] + pointArray[17]) * 0.5;
  dMatrix_2_2 = dMatrix22;
  dMatrix_0_2 = dMatrix02;
  dMatrix_1_2 = dMatrix12;
  dMatrix_2_1 = dMatrix21;
  dMatrix_0_1 = dMatrix01;
  do {
    matrixWork[uiOffset2 + 3] = (pData->pointAy[3].fY + pointArray[uiOffset2 + 1]) * dMatrix10_2
      + (pData->pointAy[3].fX + pointArray[uiOffset2]) * dMatrix00
      + (pData->pointAy[3].fZ + pointArray[uiOffset2 + 2]) * dSinZSaved;
    matrixWork[uiOffset2 + 4] = (pData->pointAy[3].fY + pointArray[uiOffset2 + 1]) * dMatrix10
      + (pData->pointAy[3].fX + pointArray[uiOffset2]) * dMatrix_0_1
      + (pData->pointAy[3].fZ + pointArray[uiOffset2 + 2]) * dMatrix_2_1;
    dTransformedZ = (pData->pointAy[3].fY + pointArray[uiOffset2 + 1]) * dMatrix_1_2
      + (pData->pointAy[3].fX + pointArray[uiOffset2]) * dMatrix_0_2
      + (pData->pointAy[3].fZ + pointArray[uiOffset2 + 2]) * dMatrix_2_2;
    uiOffset2 += 3;
    matrixWork[uiOffset2 + 2] = dTransformedZ;
  } while (uiOffset2 != 27);
  iLoop4 = 0;
  dNextAngleY = getdirection(matrixWork[18] - matrixWork[15], matrixWork[19] - matrixWork[16]);
  dNextAngleYSaved = dNextAngleY;
  do {
    dSinNextY = sin(-dNextAngleY);
    dWorkAngle = cos(-dNextAngleY);
    dTemPRotNextY = matrixWork[iLoop4 + 4] * dSinNextY;
    dCosY = dWorkAngle;
    dRotatedX = matrixWork[iLoop4 + 3] * dWorkAngle - dTemPRotNextY;
    dRotatedY = dSinNextY * matrixWork[iLoop4 + 3] + matrixWork[iLoop4 + 4] * dWorkAngle;
    iLoop4 += 3;

    matrixWork[iLoop4] = dRotatedX;
    matrixWork[iLoop4 + 1] = dRotatedY;
    //LODWORD(matrixWork[iLoop4]) = LODWORD(dRotatedX);
    //HIDWORD(matrixWork[iLoop4]) = HIDWORD(dRotatedX);
    //LODWORD(matrixWork[iLoop4 + 1]) = LODWORD(dRotatedY);
    //HIDWORD(matrixWork[iLoop4 + 1]) = HIDWORD(dRotatedY);
  } while (iLoop4 != 27);
  iLoop5 = 0;
  dNextAngleZ = getdirection(matrixWork[18] - matrixWork[15], matrixWork[20] - matrixWork[17]);
  dNextAngleZSaved = dNextAngleZ;
  do {
    dSinNextZ = sin(dNextAngleZ);
    dWorkAngle = cos(dNextAngleZ);
    dTempRotNextZ = matrixWork[iLoop5 + 5] * dSinNextZ;
    dCosY_1 = dWorkAngle;
    dRotatedX = dTempRotNextZ + matrixWork[iLoop5 + 3] * dWorkAngle;
    dRotatedZ = matrixWork[iLoop5 + 5] * dWorkAngle + -dSinNextZ * matrixWork[iLoop5 + 3];
    iLoop5 += 3;

    matrixWork[iLoop5] = dRotatedX;
    matrixWork[iLoop5 + 2] = dRotatedZ;
    //LODWORD(matrixWork[iLoop5]) = LODWORD(dRotatedX);
    //HIDWORD(matrixWork[iLoop5]) = HIDWORD(dRotatedX);
    //LODWORD(matrixWork[iLoop5 + 2]) = LODWORD(dRotatedZ);
    //HIDWORD(matrixWork[iLoop5 + 2]) = HIDWORD(dRotatedZ);
  } while (iLoop5 != 27);

  dWorkAngle = getdirection(matrixWork[22] - matrixWork[25], matrixWork[26] - matrixWork[23]);
  dCosY = 1.0 / 6.28318530718;                  // Convert angles to internal fixed-point format (16384 = 2pi radians)
  dAngleYFixed = dNextAngleYSaved * 16384.0 * dCosY + 0.5;
  floor(dAngleYFixed);
  //_CHP();
  pData->iYaw = (int)dAngleYFixed;
  dAngleZFixed = dNextAngleZSaved * 16384.0 * dCosY + 0.5;
  floor(dAngleZFixed);
  //_CHP();
  pData->iPitch = (int)dAngleZFixed;
  dAngleXFixed = dWorkAngle * 16384.0 * dCosY + 0.5;
  floor(dAngleXFixed);
  //_CHP();
  pData->iPitch &= 0x3FFFu;
  iYawTemp = pData->iYaw;
  pData->iRoll = (int)dAngleXFixed;
  if (iYawTemp > 0x2000)
    pData->iYaw = iYawTemp - 0x4000;
  iPitchTemp = pData->iPitch;
  if (iPitchTemp > 0x2000)
    pData->iPitch = iPitchTemp - 0x4000;
  iRollTemp = pData->iRoll;
  if (iRollTemp > 0x2000)
    pData->iRoll = iRollTemp - 0x4000;
  pData->gravity.fX = (float)(dSinZSaved * -3.0);        // Store up vector scaled by -3 for physics calculations
  pData->gravity.fY = (float)(dMatrix21 * -3.0);
  pData->gravity.fZ = (float)(dMatrix22 * -3.0);
}

//-------------------------------------------------------------------------------------------------
//00061C90
void dopitchchanges(int iLLaneIdx, int iRLaneIdx)
{
  int iTrackIndex; // esi
  int iLeftLaneOffset; // ebp
  int iRightLaneOffset; // edi
  int iNextTrackIndex; // ecx
  tData *pCurrentTrackData; // ebx
  int uiLoopCounter1; // eax
  double dTransformZ1; // st7
  int iBankAngle1; // eax
  int iBankAngle2; // eax
  int uiLoopCounter2; // eax
  double dTransformZ2; // st7
  int iPointIndex; // eax
  double dLaneCenterOffsetZ; // st7
  double dLaneCenterOffsetY; // st6
  double dLaneCenterOffsetX; // st5
  double dTempValue; // st4
  int iRotationIndex1; // ebx
  double dLaneDirection1; // st7
  double dSinValue1; // rt0
  double dCosValue1; // st5
  int iRotationIndex2; // ebx
  double dLaneDirection2; // st7
  double dSinValue2; // st6
  double dCosValue2; // st5
  int iRotationIndex3; // ebx
  double dLaneDirection3; // st6
  double dCosValue3; // st7
  double dSinValue3; // st5
  double dMatrixComp1; // rt2
  double dMatrixComp2; // st6
  tData *pNextTrackData; // ebx
  int uiTransformLoop; // eax
  double dNextTransformZ; // st7
  int iNextBankAngle1; // eax
  int iNextBankAngle2; // eax
  int uiNextTransformLoop; // eax
  double dNextSegmentTransformZ; // st7
  int uiFinalRotationLoop; // eax
  double dMatrixRotValue1; // st7
  double dMatrixRotValue2; // st6
  double dPointY; // st5
  double dPointX; // st3
  double dPointZ; // rt2
  int iDirectionCalcLoop; // ebx
  double dFinalDirection; // st7
  double dSinFinal; // rtt
  double dCosFinal; // st5
  double dFinalLanePitchAngle; // st7
  tData *pOutputTrackData; // edx
  //double transformCoordsOffset[3]; // [esp+0h] [ebp-3DCh]
  double transformCoords[27]; // [esp+18h] [ebp-3C4h]
  double bankRotData[24]; // [esp+F0h] [ebp-2ECh]
  //double laneCornerPointsOffset[3]; // [esp+1B0h] [ebp-22Ch]
  double laneCornerPoints[27]; // [esp+1C8h] [ebp-214h]
  double dRotMatrix00; // [esp+2A0h] [ebp-13Ch]
  double dRotMatrix01; // [esp+2A8h] [ebp-134h]
  double dRotMatrix02; // [esp+2B0h] [ebp-12Ch]
  double dRotMatrix10; // [esp+2B8h] [ebp-124h]
  double dRotMatrix11; // [esp+2C0h] [ebp-11Ch]
  double dRotMatrix12; // [esp+2C8h] [ebp-114h]
  double dRotMatrix20; // [esp+2D0h] [ebp-10Ch]
  double dRotMatrix21; // [esp+2D8h] [ebp-104h]
  double dRotMatrix22; // [esp+2E8h] [ebp-F4h]
  double dTempRotValue1; // [esp+2F0h] [ebp-ECh]
  double dSinBank2; // [esp+2F8h] [ebp-E4h]
  double dNextSinBank2; // [esp+300h] [ebp-DCh]
  double dNextSinBank1; // [esp+308h] [ebp-D4h]
  double dSinBank1; // [esp+310h] [ebp-CCh]
  double dNextCosBank1; // [esp+318h] [ebp-C4h]
  double dLaneDirection1Stored; // [esp+320h] [ebp-BCh]
  double dLaneDirectionAngle; // [esp+328h] [ebp-B4h]
  double dCosMatrix22; // [esp+330h] [ebp-ACh]
  double dSinMatrix10; // [esp+338h] [ebp-A4h]
  double dNegSinMatrix20; // [esp+340h] [ebp-9Ch]
  double dCosMatrix11; // [esp+348h] [ebp-94h]
  double dCosMatrix00; // [esp+350h] [ebp-8Ch]
  double dLaneDirection2Stored; // [esp+358h] [ebp-84h]
  double dCosDirection3; // [esp+360h] [ebp-7Ch]
  double dSinDirection3; // [esp+368h] [ebp-74h]
  double dNegCenterOffsetY; // [esp+370h] [ebp-6Ch]
  double dNegCenterOffsetZ; // [esp+378h] [ebp-64h]
  double dNegCenterOffsetX; // [esp+380h] [ebp-5Ch]
  double dTempSin; // [esp+388h] [ebp-54h]
  double dStoredPitchAngle; // [esp+390h] [ebp-4Ch]
  double dNextCosBank2; // [esp+398h] [ebp-44h]
  double dCosBank2; // [esp+3A0h] [ebp-3Ch]
  double dCosBank1; // [esp+3A8h] [ebp-34h]
  double dTempCos; // [esp+3B0h] [ebp-2Ch]
  unsigned int uiLeftLaneOffset12; // [esp+3B8h] [ebp-24h]
  unsigned int uiRightLaneOffset12; // [esp+3BCh] [ebp-20h]
  int iNextTrackIndex2; // [esp+3C0h] [ebp-1Ch]
  int iLeftLaneIndex; // [esp+3C4h] [ebp-18h]

  // a1 = iLeftLaneIndex (0=inner left, 3=outer left), a2 = iRightLaneIndex (2=inner right, 4=outer right)
  iLeftLaneIndex = iLLaneIdx;
  iTrackIndex = 0;

  // Main loop through all track segments
  if (TRAK_LEN > 0) {
    // Calculate 12-byte offsets for left and right lane indices
    uiLeftLaneOffset12 = 12 * iLLaneIdx;
    iLeftLaneOffset = iLLaneIdx;
    uiRightLaneOffset12 = 12 * iRLaneIdx;
    iRightLaneOffset = iRLaneIdx;
    do {
      // Calculate next track index with wraparound
      iNextTrackIndex = iTrackIndex + 1;
      if (iTrackIndex + 1 == TRAK_LEN)
        iNextTrackIndex ^= TRAK_LEN;
      iNextTrackIndex2 = iNextTrackIndex + 1;
      if (iNextTrackIndex + 1 == TRAK_LEN)
        iNextTrackIndex2 = 0;
      laneCornerPoints[0] = TrakPt[0].pointAy[iLeftLaneOffset].fX;// Load current track segment lane corner points into working array
      laneCornerPoints[1] = TrakPt[0].pointAy[iLeftLaneOffset].fY;
      laneCornerPoints[2] = TrakPt[0].pointAy[iLeftLaneOffset].fZ;
      laneCornerPoints[3] = TrakPt[0].pointAy[iRightLaneOffset].fX;
      laneCornerPoints[4] = TrakPt[0].pointAy[iRightLaneOffset].fY;
      laneCornerPoints[5] = TrakPt[0].pointAy[iRightLaneOffset].fZ;
      laneCornerPoints[6] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fX;
      laneCornerPoints[7] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fY;
      laneCornerPoints[8] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fZ;
      laneCornerPoints[9] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fX;
      laneCornerPoints[10] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fY;
      pCurrentTrackData = &localdata[iTrackIndex];
      uiLoopCounter1 = 0;
      laneCornerPoints[11] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fZ;
      do {
        transformCoords[uiLoopCounter1] = (pCurrentTrackData->pointAy[3].fY + laneCornerPoints[uiLoopCounter1 + 1]) * pCurrentTrackData->pointAy[1].fX
          + (pCurrentTrackData->pointAy[3].fX + laneCornerPoints[uiLoopCounter1]) * pCurrentTrackData->pointAy[0].fX
          + (pCurrentTrackData->pointAy[3].fZ + laneCornerPoints[uiLoopCounter1 + 2]) * pCurrentTrackData->pointAy[2].fX;// Transform points using matrix multiplication
        transformCoords[uiLoopCounter1 + 1] = (pCurrentTrackData->pointAy[3].fY + laneCornerPoints[uiLoopCounter1 + 1]) * pCurrentTrackData->pointAy[1].fY
          + (pCurrentTrackData->pointAy[3].fX + laneCornerPoints[uiLoopCounter1]) * pCurrentTrackData->pointAy[0].fY
          + (pCurrentTrackData->pointAy[3].fZ + laneCornerPoints[uiLoopCounter1 + 2]) * pCurrentTrackData->pointAy[2].fY;
        dTransformZ1 = (pCurrentTrackData->pointAy[3].fY + laneCornerPoints[uiLoopCounter1 + 1]) * pCurrentTrackData->pointAy[1].fZ
          + (pCurrentTrackData->pointAy[3].fX + laneCornerPoints[uiLoopCounter1]) * pCurrentTrackData->pointAy[0].fZ
          + (pCurrentTrackData->pointAy[3].fZ + laneCornerPoints[uiLoopCounter1 + 2]) * pCurrentTrackData->pointAy[2].fZ;
        transformCoords[uiLoopCounter1 + 2] = dTransformZ1;
        uiLoopCounter1 += 3;
        //transformCoordsOffset[uiLoopCounter1 + 2] = dTransformZ1;// Store Z coordinate into transformCoords[2+loop_index] via offset alias
      } while (uiLoopCounter1 != 12);

      // Calculate bank angle for rotation (negative half)
      iBankAngle1 = (-pCurrentTrackData->iBankDelta / 2) & 0x3FFF;
      dCosBank1 = tcos[iBankAngle1];            // Get cosine from lookup table
      dSinBank1 = tsin[iBankAngle1];
      bankRotData[1] = transformCoords[1] * dCosBank1 - transformCoords[2] * dSinBank1;// Apply first bank rotation transformation
      bankRotData[2] = transformCoords[1] * dSinBank1 + transformCoords[2] * dCosBank1;
      bankRotData[4] = transformCoords[4] * dCosBank1 - transformCoords[5] * dSinBank1;
      bankRotData[5] = transformCoords[4] * dSinBank1 + transformCoords[5] * dCosBank1;
      bankRotData[0] = transformCoords[0];
      bankRotData[3] = transformCoords[3];
      iBankAngle2 = (pCurrentTrackData->iBankDelta / 2) & 0x3FFF;// Calculate bank angle for rotation (positive half)
      dCosBank2 = tcos[iBankAngle2];
      dSinBank2 = tsin[iBankAngle2];
      bankRotData[7] = transformCoords[7] * dCosBank2 - transformCoords[8] * dSinBank2;// Apply second bank rotation transformation
      bankRotData[8] = transformCoords[7] * dSinBank2 + transformCoords[8] * dCosBank2;
      bankRotData[10] = transformCoords[10] * dCosBank2 - transformCoords[11] * dSinBank2;
      bankRotData[6] = transformCoords[6];
      bankRotData[9] = transformCoords[9];
      uiLoopCounter2 = 0;
      bankRotData[11] = transformCoords[10] * dSinBank2 + transformCoords[11] * dCosBank2;
      do {
        laneCornerPoints[uiLoopCounter2] = pCurrentTrackData->pointAy[0].fY * bankRotData[uiLoopCounter2 + 1]
          + pCurrentTrackData->pointAy[0].fX * bankRotData[uiLoopCounter2]
          + pCurrentTrackData->pointAy[0].fZ * bankRotData[uiLoopCounter2 + 2]
          - pCurrentTrackData->pointAy[3].fX;// Transform coordinates and subtract offset
        laneCornerPoints[uiLoopCounter2 + 1] = pCurrentTrackData->pointAy[1].fY * bankRotData[uiLoopCounter2 + 1]
          + pCurrentTrackData->pointAy[1].fX * bankRotData[uiLoopCounter2]
          + pCurrentTrackData->pointAy[1].fZ * bankRotData[uiLoopCounter2 + 2]
          - pCurrentTrackData->pointAy[3].fY;
        dTransformZ2 = pCurrentTrackData->pointAy[2].fY * bankRotData[uiLoopCounter2 + 1]
          + pCurrentTrackData->pointAy[2].fX * bankRotData[uiLoopCounter2]
          + pCurrentTrackData->pointAy[2].fZ * bankRotData[uiLoopCounter2 + 2]
          - pCurrentTrackData->pointAy[3].fZ;
        laneCornerPoints[uiLoopCounter2 + 2] = dTransformZ2;
        uiLoopCounter2 += 3;
        //laneCornerPointsOffset[uiLoopCounter2 + 2] = dTransformZ2;// laneCornerPoints[uiLoopCounter2 + 2] when above loop inc
      } while (uiLoopCounter2 != 12);

      // Calculate midpoints between corner pairs
      laneCornerPoints[12] = (laneCornerPoints[0] + laneCornerPoints[3]) * 0.5;
      laneCornerPoints[13] = (laneCornerPoints[1] + laneCornerPoints[4]) * 0.5;
      laneCornerPoints[14] = (laneCornerPoints[2] + laneCornerPoints[5]) * 0.5;
      laneCornerPoints[15] = (laneCornerPoints[6] + laneCornerPoints[9]) * 0.5;
      laneCornerPoints[16] = (laneCornerPoints[7] + laneCornerPoints[10]) * 0.5;
      laneCornerPoints[17] = (laneCornerPoints[8] + laneCornerPoints[11]) * 0.5;
      laneCornerPoints[18] = (laneCornerPoints[0] + laneCornerPoints[6]) * 0.5;
      laneCornerPoints[19] = (laneCornerPoints[1] + laneCornerPoints[7]) * 0.5;
      laneCornerPoints[20] = (laneCornerPoints[2] + laneCornerPoints[8]) * 0.5;
      laneCornerPoints[21] = (laneCornerPoints[3] + laneCornerPoints[9]) * 0.5;
      laneCornerPoints[22] = (laneCornerPoints[4] + laneCornerPoints[10]) * 0.5;
      laneCornerPoints[23] = (laneCornerPoints[5] + laneCornerPoints[11]) * 0.5;
      laneCornerPoints[24] = (laneCornerPoints[12] + laneCornerPoints[15]) * 0.5;
      laneCornerPoints[25] = (laneCornerPoints[13] + laneCornerPoints[16]) * 0.5;
      laneCornerPoints[26] = (laneCornerPoints[14] + laneCornerPoints[17]) * 0.5;
      dNegCenterOffsetX = -laneCornerPoints[24];
      dNegCenterOffsetY = -laneCornerPoints[25];
      dNegCenterOffsetZ = -laneCornerPoints[26];
      laneCornerPoints[0] = laneCornerPoints[0] - laneCornerPoints[24];
      laneCornerPoints[1] = laneCornerPoints[1] - laneCornerPoints[25];
      iPointIndex = 3;
      laneCornerPoints[2] = laneCornerPoints[2] - laneCornerPoints[26];
      dLaneCenterOffsetZ = dNegCenterOffsetZ;
      dLaneCenterOffsetY = dNegCenterOffsetY;
      dLaneCenterOffsetX = dNegCenterOffsetX;
      do {
        laneCornerPoints[iPointIndex] = laneCornerPoints[iPointIndex] + dLaneCenterOffsetX;
        laneCornerPoints[iPointIndex + 1] = laneCornerPoints[iPointIndex + 1] + dLaneCenterOffsetY;
        laneCornerPoints[iPointIndex + 2] = laneCornerPoints[iPointIndex + 2] + dLaneCenterOffsetZ;
        laneCornerPoints[iPointIndex + 3] = laneCornerPoints[iPointIndex + 3] + dLaneCenterOffsetX;
        laneCornerPoints[iPointIndex + 4] = laneCornerPoints[iPointIndex + 4] + dLaneCenterOffsetY;
        laneCornerPoints[iPointIndex + 5] = laneCornerPoints[iPointIndex + 5] + dLaneCenterOffsetZ;
        laneCornerPoints[iPointIndex + 6] = laneCornerPoints[iPointIndex + 6] + dLaneCenterOffsetX;
        laneCornerPoints[iPointIndex + 7] = laneCornerPoints[iPointIndex + 7] + dLaneCenterOffsetY;
        laneCornerPoints[iPointIndex + 8] = laneCornerPoints[iPointIndex + 8] + dLaneCenterOffsetZ;
        laneCornerPoints[iPointIndex + 9] = laneCornerPoints[iPointIndex + 9] + dLaneCenterOffsetX;
        laneCornerPoints[iPointIndex + 10] = laneCornerPoints[iPointIndex + 10] + dLaneCenterOffsetY;
        dTempValue = laneCornerPoints[iPointIndex + 11] + dLaneCenterOffsetZ;
        laneCornerPoints[iPointIndex + 11] = dTempValue;
        iPointIndex += 12;
        //laneCornerPointsOffset[iPointIndex + 2] = dTempValue;// laneCornerPoints[iPointIndex + 11] when before loop inc
      } while (iPointIndex != 27);
      iRotationIndex1 = 0;
      dLaneDirection1 = getdirection(laneCornerPoints[15] - laneCornerPoints[12], laneCornerPoints[16] - laneCornerPoints[13]);// Get direction angle for first rotation axis
      dLaneDirection1Stored = dLaneDirection1;
      do {
        // First rotation pass around calculated direction
        dSinValue1 = sin(-dLaneDirection1);
        dTempCos = cos(-dLaneDirection1);
        dCosValue1 = laneCornerPoints[iRotationIndex1 + 1] * dSinValue1;
        dRotMatrix21 = dTempCos;
        dRotMatrix02 = laneCornerPoints[iRotationIndex1] * dTempCos - dCosValue1;
        dRotMatrix01 = dSinValue1 * laneCornerPoints[iRotationIndex1] + laneCornerPoints[iRotationIndex1 + 1] * dTempCos;
        laneCornerPoints[iRotationIndex1] = dRotMatrix02;
        laneCornerPoints[iRotationIndex1 + 1]  = dRotMatrix01;
        iRotationIndex1 += 3;
        //LODWORD(laneCornerPointsOffset[iRotationIndex1]) = LODWORD(dRotMatrix02);// laneCornerPoints[iRotationIndex1] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex1]) = HIDWORD(dRotMatrix02);
        //LODWORD(laneCornerPointsOffset[iRotationIndex1 + 1]) = LODWORD(dRotMatrix01);// laneCornerPoints[iRotationIndex1 + 1] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex1 + 1]) = HIDWORD(dRotMatrix01);
      } while (iRotationIndex1 != 27);
      iRotationIndex2 = 0;
      dLaneDirection2 = getdirection(laneCornerPoints[15] - laneCornerPoints[12], laneCornerPoints[17] - laneCornerPoints[14]);// Get direction angle for second rotation axis
      dLaneDirection2Stored = dLaneDirection2;
      do {
        // Second rotation pass around calculated direction
        dSinValue2 = sin(dLaneDirection2);
        dTempCos = cos(dLaneDirection2);
        dCosValue2 = laneCornerPoints[iRotationIndex2 + 2] * dSinValue2;
        dRotMatrix21 = dTempCos;
        dRotMatrix02 = dCosValue2 + laneCornerPoints[iRotationIndex2] * dTempCos;
        dRotMatrix00 = laneCornerPoints[iRotationIndex2 + 2] * dTempCos + -dSinValue2 * laneCornerPoints[iRotationIndex2];
        laneCornerPoints[iRotationIndex2] = dRotMatrix02;
        laneCornerPoints[iRotationIndex2 + 2] = dRotMatrix00;
        iRotationIndex2 += 3;
        //LODWORD(laneCornerPointsOffset[iRotationIndex2]) = LODWORD(dRotMatrix02);// laneCornerPoints[iRotationIndex2] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex2]) = HIDWORD(dRotMatrix02);
        //LODWORD(laneCornerPointsOffset[iRotationIndex2 + 2]) = LODWORD(dRotMatrix00);// laneCornerPoints[iRotationIndex2 + 2] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex2 + 2]) = HIDWORD(dRotMatrix00);
      } while (iRotationIndex2 != 27);
      iRotationIndex3 = 0;
      dLaneDirectionAngle = getdirection(laneCornerPoints[19] - laneCornerPoints[22], laneCornerPoints[23] - laneCornerPoints[20]);// Get direction for third rotation axis
      dLaneDirection3 = dLaneDirectionAngle;
      do {
        dCosValue3 = cos(dLaneDirection3);
        dTempCos = sin(dLaneDirection3);
        dSinValue3 = laneCornerPoints[iRotationIndex3 + 1] * dCosValue3;
        dSinDirection3 = dTempCos;
        dRotMatrix01 = dSinValue3 - laneCornerPoints[iRotationIndex3 + 2] * dTempCos;
        dRotMatrix00 = laneCornerPoints[iRotationIndex3 + 1] * dTempCos + laneCornerPoints[iRotationIndex3 + 2] * dCosValue3;
        laneCornerPoints[iRotationIndex3 + 1] = dRotMatrix01;
        laneCornerPoints[iRotationIndex3 + 2] = dRotMatrix00;
        iRotationIndex3 += 3;
        //LODWORD(laneCornerPointsOffset[iRotationIndex3 + 1]) = LODWORD(dRotMatrix01);// laneCornerPoints[iRotationIndex3 + 1] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex3 + 1]) = HIDWORD(dRotMatrix01);
        //LODWORD(laneCornerPointsOffset[iRotationIndex3 + 2]) = LODWORD(dRotMatrix00);// laneCornerPoints[iRotationIndex3 + 2] when before loop inc
        //HIDWORD(laneCornerPointsOffset[iRotationIndex3 + 2]) = HIDWORD(dRotMatrix00);
      } while (iRotationIndex3 != 27);

      // Prepare rotation matrix components
      dCosDirection3 = dCosValue3;
      dMatrixComp1 = cos(dLaneDirection1Stored);
      dTempSin = sin(dLaneDirection1Stored);
      dMatrixComp2 = cos(dLaneDirection2Stored);
      dCosMatrix00 = dMatrixComp1 * dMatrixComp2;
      dRotMatrix22 = sin(dLaneDirection2Stored);
      dTempRotValue1 = dMatrixComp1 * dRotMatrix22;
      dTempCos = dTempSin;
      dRotMatrix10 = dTempRotValue1 * dSinDirection3 + -dTempSin * dCosValue3;
      dNegSinMatrix20 = -dTempSin * dSinDirection3 - dTempRotValue1 * dCosValue3;
      dSinMatrix10 = dTempSin * dMatrixComp2;
      dCosMatrix11 = dTempSin * dRotMatrix22 * dSinDirection3 + dMatrixComp1 * dCosValue3;
      dRotMatrix11 = dMatrixComp1 * dSinDirection3 - dTempSin * dRotMatrix22 * dCosValue3;
      dRotMatrix20 = dSinDirection3 * -dMatrixComp2;
      dCosMatrix22 = dCosValue3 * dMatrixComp2;
      dRotMatrix12 = dRotMatrix22;

      // Load next track segment lane points for pitch comparison
      laneCornerPoints[0] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fX;
      laneCornerPoints[1] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fY;
      laneCornerPoints[2] = TrakPt[iNextTrackIndex].pointAy[uiLeftLaneOffset12 / 0xC].fZ;
      laneCornerPoints[3] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fX;
      laneCornerPoints[4] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fY;
      laneCornerPoints[5] = TrakPt[iNextTrackIndex].pointAy[uiRightLaneOffset12 / 0xC].fZ;
      laneCornerPoints[6] = TrakPt[iNextTrackIndex2].pointAy[uiLeftLaneOffset12 / 0xC].fX;
      laneCornerPoints[7] = TrakPt[iNextTrackIndex2].pointAy[uiLeftLaneOffset12 / 0xC].fY;
      laneCornerPoints[8] = TrakPt[iNextTrackIndex2].pointAy[uiLeftLaneOffset12 / 0xC].fZ;
      laneCornerPoints[9] = TrakPt[iNextTrackIndex2].pointAy[uiRightLaneOffset12 / 0xC].fX;
      laneCornerPoints[10] = TrakPt[iNextTrackIndex2].pointAy[uiRightLaneOffset12 / 0xC].fY;
      pNextTrackData = &localdata[iNextTrackIndex];
      uiTransformLoop = 0;
      laneCornerPoints[11] = TrakPt[iNextTrackIndex2].pointAy[uiRightLaneOffset12 / 0xC].fZ;
      do {
        transformCoords[uiTransformLoop] = (pNextTrackData->pointAy[3].fY + laneCornerPoints[uiTransformLoop + 1]) * pNextTrackData->pointAy[1].fX
          + (pNextTrackData->pointAy[3].fX + laneCornerPoints[uiTransformLoop]) * pNextTrackData->pointAy[0].fX
          + (pNextTrackData->pointAy[3].fZ + laneCornerPoints[uiTransformLoop + 2]) * pNextTrackData->pointAy[2].fX;
        transformCoords[uiTransformLoop + 1] = (pNextTrackData->pointAy[3].fY + laneCornerPoints[uiTransformLoop + 1]) * pNextTrackData->pointAy[1].fY
          + (pNextTrackData->pointAy[3].fX + laneCornerPoints[uiTransformLoop]) * pNextTrackData->pointAy[0].fY
          + (pNextTrackData->pointAy[3].fZ + laneCornerPoints[uiTransformLoop + 2]) * pNextTrackData->pointAy[2].fY;
        dNextTransformZ = (pNextTrackData->pointAy[3].fY + laneCornerPoints[uiTransformLoop + 1]) * pNextTrackData->pointAy[1].fZ
          + (pNextTrackData->pointAy[3].fX + laneCornerPoints[uiTransformLoop]) * pNextTrackData->pointAy[0].fZ
          + (pNextTrackData->pointAy[3].fZ + laneCornerPoints[uiTransformLoop + 2]) * pNextTrackData->pointAy[2].fZ;
        transformCoords[uiTransformLoop + 2] = dNextTransformZ;
        uiTransformLoop += 3;
        //transformCoordsOffset[uiTransformLoop + 2] = dNextTransformZ;// transformCoords[uiTransformLoop + 2] when before loop inc
      } while (uiTransformLoop != 12);
      iNextBankAngle1 = (-pNextTrackData->iBankDelta / 2) & 0x3FFF;
      dNextCosBank2 = tcos[iNextBankAngle1];
      dNextSinBank1 = tsin[iNextBankAngle1];
      bankRotData[1] = transformCoords[1] * dNextCosBank2 - transformCoords[2] * dNextSinBank1;
      bankRotData[2] = transformCoords[1] * dNextSinBank1 + transformCoords[2] * dNextCosBank2;
      bankRotData[4] = transformCoords[4] * dNextCosBank2 - transformCoords[5] * dNextSinBank1;
      bankRotData[5] = transformCoords[4] * dNextSinBank1 + transformCoords[5] * dNextCosBank2;
      bankRotData[0] = transformCoords[0];
      bankRotData[3] = transformCoords[3];
      iNextBankAngle2 = (pNextTrackData->iBankDelta / 2) & 0x3FFF;
      dNextCosBank1 = tcos[iNextBankAngle2];
      dNextSinBank2 = tsin[iNextBankAngle2];
      bankRotData[7] = transformCoords[7] * dNextCosBank1 - transformCoords[8] * dNextSinBank2;
      bankRotData[8] = transformCoords[7] * dNextSinBank2 + transformCoords[8] * dNextCosBank1;
      bankRotData[10] = transformCoords[10] * dNextCosBank1 - transformCoords[11] * dNextSinBank2;
      bankRotData[6] = transformCoords[6];
      bankRotData[9] = transformCoords[9];
      uiNextTransformLoop = 0;
      bankRotData[11] = transformCoords[10] * dNextSinBank2 + transformCoords[11] * dNextCosBank1;
      do {
        laneCornerPoints[uiNextTransformLoop] = pNextTrackData->pointAy[0].fY * bankRotData[uiNextTransformLoop + 1]
          + pNextTrackData->pointAy[0].fX * bankRotData[uiNextTransformLoop]
          + pNextTrackData->pointAy[0].fZ * bankRotData[uiNextTransformLoop + 2]
          - pNextTrackData->pointAy[3].fX;
        laneCornerPoints[uiNextTransformLoop + 1] = pNextTrackData->pointAy[1].fY * bankRotData[uiNextTransformLoop + 1]
          + pNextTrackData->pointAy[1].fX * bankRotData[uiNextTransformLoop]
          + pNextTrackData->pointAy[1].fZ * bankRotData[uiNextTransformLoop + 2]
          - pNextTrackData->pointAy[3].fY;
        dNextSegmentTransformZ = pNextTrackData->pointAy[2].fY * bankRotData[uiNextTransformLoop + 1]
          + pNextTrackData->pointAy[2].fX * bankRotData[uiNextTransformLoop]
          + pNextTrackData->pointAy[2].fZ * bankRotData[uiNextTransformLoop + 2]
          - pNextTrackData->pointAy[3].fZ;
        laneCornerPoints[uiNextTransformLoop + 2] = dNextSegmentTransformZ;
        uiNextTransformLoop += 3;
        //laneCornerPointsOffset[uiNextTransformLoop + 2] = dNextSegmentTransformZ;// laneCornerPoints[uiNextTransformLoop + 2] when before loop inc
      } while (uiNextTransformLoop != 12);
      laneCornerPoints[12] = (laneCornerPoints[0] + laneCornerPoints[3]) * 0.5;
      laneCornerPoints[13] = (laneCornerPoints[1] + laneCornerPoints[4]) * 0.5;
      laneCornerPoints[14] = (laneCornerPoints[2] + laneCornerPoints[5]) * 0.5;
      laneCornerPoints[15] = (laneCornerPoints[6] + laneCornerPoints[9]) * 0.5;
      laneCornerPoints[16] = (laneCornerPoints[7] + laneCornerPoints[10]) * 0.5;
      laneCornerPoints[17] = (laneCornerPoints[8] + laneCornerPoints[11]) * 0.5;
      laneCornerPoints[18] = (laneCornerPoints[0] + laneCornerPoints[6]) * 0.5;
      laneCornerPoints[19] = (laneCornerPoints[1] + laneCornerPoints[7]) * 0.5;
      laneCornerPoints[20] = (laneCornerPoints[2] + laneCornerPoints[8]) * 0.5;
      laneCornerPoints[21] = (laneCornerPoints[3] + laneCornerPoints[9]) * 0.5;
      laneCornerPoints[22] = (laneCornerPoints[4] + laneCornerPoints[10]) * 0.5;
      laneCornerPoints[23] = (laneCornerPoints[5] + laneCornerPoints[11]) * 0.5;
      laneCornerPoints[24] = (laneCornerPoints[12] + laneCornerPoints[15]) * 0.5;
      laneCornerPoints[25] = (laneCornerPoints[13] + laneCornerPoints[16]) * 0.5;
      uiFinalRotationLoop = 0;
      laneCornerPoints[26] = (laneCornerPoints[14] + laneCornerPoints[17]) * 0.5;
      dMatrixRotValue1 = dNegSinMatrix20;
      dMatrixRotValue2 = dRotMatrix10;
      do {
        // Apply final 3D rotation matrix to transformed points
        dPointY = laneCornerPoints[uiFinalRotationLoop + 1] + dNegCenterOffsetY;
        dPointX = laneCornerPoints[uiFinalRotationLoop] + dNegCenterOffsetX;
        dPointZ = laneCornerPoints[uiFinalRotationLoop + 2] + dNegCenterOffsetZ;
        transformCoords[uiFinalRotationLoop] = dSinMatrix10 * dPointY + dCosMatrix00 * dPointX + dRotMatrix12 * dPointZ;
        transformCoords[uiFinalRotationLoop + 1] = dCosMatrix11 * dPointY + dMatrixRotValue2 * dPointX + dRotMatrix20 * dPointZ;
        transformCoords[uiFinalRotationLoop + 2] = dPointY * dRotMatrix11 + dPointX * dMatrixRotValue1 + dPointZ * dCosMatrix22;
        uiFinalRotationLoop += 3;
        //transformCoordsOffset[uiFinalRotationLoop + 2] = dPointY * dRotMatrix11 + dPointX * dMatrixRotValue1 + dPointZ * dCosMatrix22;// transformCoords[uiFinalRotationLoop + 1] when before loop inc
      } while (uiFinalRotationLoop != 27);
      iDirectionCalcLoop = 0;
      dFinalDirection = getdirection(transformCoords[15] - transformCoords[12], transformCoords[16] - transformCoords[13]);// Get final direction angle for last rotation
      do {
        // Final rotation transformation
        dSinFinal = sin(-dFinalDirection);
        dTempCos = cos(-dFinalDirection);
        dCosFinal = transformCoords[iDirectionCalcLoop + 1] * dSinFinal;
        dTempSin = dTempCos;
        dRotMatrix02 = transformCoords[iDirectionCalcLoop] * dTempCos - dCosFinal;
        dRotMatrix01 = dSinFinal * transformCoords[iDirectionCalcLoop] + transformCoords[iDirectionCalcLoop + 1] * dTempCos;
        transformCoords[iDirectionCalcLoop] = dRotMatrix02;
        transformCoords[iDirectionCalcLoop + 1] = dRotMatrix01;
        iDirectionCalcLoop += 3;
        //LODWORD(transformCoordsOffset[iDirectionCalcLoop]) = LODWORD(dRotMatrix02);// transformCoords[iDirectionCalcLoop] when before loop inc
        //HIDWORD(transformCoordsOffset[iDirectionCalcLoop]) = HIDWORD(dRotMatrix02);
        //LODWORD(transformCoordsOffset[iDirectionCalcLoop + 1]) = LODWORD(dRotMatrix01);// transformCoords[iDirectionCalcLoop + 1] when before loop inc
        //HIDWORD(transformCoordsOffset[iDirectionCalcLoop + 1]) = HIDWORD(dRotMatrix01);
      } while (iDirectionCalcLoop != 27);

      // Calculate final pitch angle in degrees and store result
      dFinalLanePitchAngle = getdirection(transformCoords[15] - transformCoords[12], transformCoords[17] - transformCoords[14]) * 16384.0 / 6.28318530718 + 0.5;
      pOutputTrackData = &localdata[iTrackIndex];
      dStoredPitchAngle = dFinalLanePitchAngle;

      // Store calculated pitch angle: iUnk18 for inner lanes, iUnk19 for outer lanes
      if (iLeftLaneIndex) {
        floor(dStoredPitchAngle);
        //_CHP();
        pOutputTrackData->iOuterLanePitchAngle = (int)dFinalLanePitchAngle;
      } else {
        floor(dStoredPitchAngle);
        //_CHP();
        pOutputTrackData->iInnerLanePitchAngle = (int)dFinalLanePitchAngle;
      }
      iRightLaneOffset += 6;
      ++iTrackIndex;
      iLeftLaneOffset += 6;
    } while (iTrackIndex < TRAK_LEN);
  }
}

//-------------------------------------------------------------------------------------------------
//00063990
void getworldangles(int iYaw, int iPitch, int iRoll, int iChunkIdx, int *piAzimuth, int *piElevation, int *piBank)
{
  double dCosPitch; // st7
  int iYaw_1; // ecx
  double dDirX; // st7
  double dDirY; // st6
  double dDirZ; // st5
  tData *pData; // edx
  int iNewAzimuth; // eax
  int iNewAzimuth_1; // ebx
  int iNewElevation; // eax
  double dRollSin; // st7
  double dRollVecX; // st7
  double fRollRotatedX; // st7
  float fCosPitch; // [esp+0h] [ebp-40h]
  float fRollVecX; // [esp+0h] [ebp-40h]
  float fRollProjZ; // [esp+4h] [ebp-3Ch]
  float fFinalX; // [esp+4h] [ebp-3Ch]
  float fProjZ; // [esp+8h] [ebp-38h]
  float fProjY; // [esp+Ch] [ebp-34h]
  float fProjX; // [esp+10h] [ebp-30h]
  float fRotatedX; // [esp+10h] [ebp-30h]
  float fRollProjX; // [esp+20h] [ebp-20h]
  float fRollVecY; // [esp+24h] [ebp-1Ch]
  float fRollProjY; // [esp+28h] [ebp-18h]
  float fRollRotatedY; // [esp+28h] [ebp-18h]
  float fRollCos; // [esp+2Ch] [ebp-14h]
  float fRollVecZ; // [esp+2Ch] [ebp-14h]
  float fRollSin_1; // [esp+30h] [ebp-10h]
  float fRollVecW; // [esp+30h] [ebp-10h]


  // Compute base vector components
  dCosPitch = tcos[iPitch] * 25600.0;
  fCosPitch = (float)dCosPitch;
  iYaw_1 = iYaw;
  dDirX = dCosPitch * tcos[iYaw];
  dDirY = fCosPitch * tsin[iYaw];
  dDirZ = tsin[iPitch] * 25600.0;

  // Get chunk data
  pData = &localdata[iChunkIdx];

  // Project reference points onto a direction vector
  fProjX = (float)(pData->pointAy[0].fY * dDirY + pData->pointAy[0].fX * dDirX + pData->pointAy[0].fZ * dDirZ);
  fProjY = (float)(pData->pointAy[1].fX * dDirX + pData->pointAy[1].fY * dDirY + pData->pointAy[1].fZ * dDirZ);
  fProjZ = (float)(dDirX * pData->pointAy[2].fX + dDirY * pData->pointAy[2].fY + dDirZ * pData->pointAy[2].fZ);

  // Calculate azimuth
  iNewAzimuth = getangle(fProjX, fProjY);
  iNewAzimuth_1 = iNewAzimuth;
  *piAzimuth = iNewAzimuth;

  // Rotate projection into azimuth-aligned coordinate system
  fRotatedX = fProjX * tcos[iNewAzimuth] + fProjY * tsin[iNewAzimuth];

  // Calculate elevation
  iNewElevation = getangle(fRotatedX, fProjZ);
  *piElevation = iNewElevation;

  // Compute roll-adjusted direction vector
  fRollCos = tcos[iRoll] * 25600.0f;
  dRollSin = tsin[iRoll] * -25600.0f;
  fRollSin_1 = (float)dRollSin;

  // Apply pitch and yaw transformations to roll vector
  dRollVecX = -dRollSin * tsin[iPitch];
  fRollVecX = (float)(dRollVecX);
  fRollVecY = (float)(dRollVecX * tcos[iYaw_1] - fRollCos * tsin[iYaw_1]);
  fRollVecZ = fRollVecX * tsin[iYaw_1] + fRollCos * tcos[iYaw_1];
  fRollVecW = fRollSin_1 * tcos[iPitch];

  // Project reference points onto roll-adjusted vector
  fRollProjX = pData->pointAy[0].fY * fRollVecZ + pData->pointAy[0].fX * fRollVecY + pData->pointAy[0].fZ * fRollVecW;
  fRollProjY = pData->pointAy[1].fX * fRollVecY + pData->pointAy[1].fY * fRollVecZ + pData->pointAy[1].fZ * fRollVecW;
  fRollProjZ = pData->pointAy[2].fX * fRollVecY + pData->pointAy[2].fY * fRollVecZ + pData->pointAy[2].fZ * fRollVecW;

  // Rotate projections using azimuth
  fRollRotatedX = fRollProjX * tcos[iNewAzimuth_1] + fRollProjY * tsin[iNewAzimuth_1];
  fRollRotatedY = -fRollProjX * tsin[iNewAzimuth_1] + fRollProjY * tcos[iNewAzimuth_1];

  // Apply elevation rotation
  fFinalX = (float)(fRollRotatedX * tsin[iNewElevation] - fRollProjZ * tcos[iNewElevation]);

  // Calculate bank
  *piBank = getangle(fRollRotatedY, fFinalX);
}

//-------------------------------------------------------------------------------------------------
//00063B90
void getlocalangles(int iYaw, int iPitch, int iRoll, int iChunkIdx, int *piAzimuth, int *piElevation, int *piBank)
{
  double dCosPitch; // st7
  int iYaw2; // ecx
  double dDirX; // st7
  double dDirY; // st6
  double dDirZ; // st5
  tData *pData; // edx
  int iNewAzimuth; // eax
  int iAz2; // ebx
  int iElevation; // eax
  double fRollSin; // st7
  double fNegRollSinTimesPitchSin; // st7
  double fRollRotatedX; // st7
  float fCosPitch; // [esp+0h] [ebp-40h]
  float fNegRollSinTimesPitchSin2; // [esp+0h] [ebp-40h]
  float v24; // [esp+4h] [ebp-3Ch]
  float fRollProjZ; // [esp+4h] [ebp-3Ch]
  float fProjZ; // [esp+8h] [ebp-38h]
  float fProjY; // [esp+Ch] [ebp-34h]
  float fProjX; // [esp+10h] [ebp-30h]
  float fRotatedX; // [esp+10h] [ebp-30h]
  float fRollProjX; // [esp+20h] [ebp-20h]
  float fRollVecX; // [esp+24h] [ebp-1Ch]
  float fRollProjY; // [esp+28h] [ebp-18h]
  float fRollRotatedY; // [esp+28h] [ebp-18h]
  float fRollCos; // [esp+2Ch] [ebp-14h]
  float fRollVecY; // [esp+2Ch] [ebp-14h]
  float fRollSin2; // [esp+30h] [ebp-10h]
  float fRollVecZ; // [esp+30h] [ebp-10h]

  // direction vector components
  dCosPitch = tcos[iPitch] * 25600.0;
  fCosPitch = (float)dCosPitch;
  iYaw2 = iYaw;

  // Compute base vector components
  dDirX = dCosPitch * tcos[iYaw];
  dDirY = fCosPitch * tsin[iYaw];
  dDirZ = tsin[iPitch] * 25600.0;

  // Get chunk data
  pData = &localdata[iChunkIdx];

  // Project reference points onto direction vector
  fProjX = (float)(pData->pointAy[1].fX * dDirY + pData->pointAy[0].fX * dDirX + pData->pointAy[2].fX * dDirZ);
  fProjY = (float)(pData->pointAy[0].fY * dDirX + pData->pointAy[1].fY * dDirY + pData->pointAy[2].fY * dDirZ);
  fProjZ = (float)(dDirX * pData->pointAy[0].fZ + dDirY * pData->pointAy[1].fZ + dDirZ * pData->pointAy[2].fZ);

  // Calculate azimuth
  iNewAzimuth = getangle(fProjX, fProjY);
  iAz2 = iNewAzimuth;
  *piAzimuth = iNewAzimuth;

  // Rotate into azimuth-aligned coordinate system
  fRotatedX = fProjX * tcos[iNewAzimuth] + fProjY * tsin[iNewAzimuth];

  // Calculate elevation
  iElevation = getangle(fRotatedX, fProjZ);
  *piElevation = iElevation;

  // Compute roll-adjusted direction vector
  fRollCos = tcos[iRoll] * 25600.0f;
  fRollSin = tsin[iRoll] * -25600.0f;
  fRollSin2 = (float)fRollSin;

  // Apply pitch and yaw transformations to roll vector
  fNegRollSinTimesPitchSin = -fRollSin * tsin[iPitch];
  fNegRollSinTimesPitchSin2 = (float)fNegRollSinTimesPitchSin;
  fRollVecX = (float)(fNegRollSinTimesPitchSin * tcos[iYaw2] - fRollCos * tsin[iYaw2]);
  fRollVecY = (float)(fNegRollSinTimesPitchSin2 * tsin[iYaw2] + fRollCos * tcos[iYaw2]);
  fRollVecZ = (float)(fRollSin2 * tcos[iPitch]);

  // Project reference points onto roll-adjusted vector
  fRollProjX = pData->pointAy[1].fX * fRollVecY + pData->pointAy[0].fX * fRollVecX + pData->pointAy[2].fX * fRollVecZ;
  fRollProjY = pData->pointAy[0].fY * fRollVecX + pData->pointAy[1].fY * fRollVecY + pData->pointAy[2].fY * fRollVecZ;
  v24 = pData->pointAy[0].fZ * fRollVecX + pData->pointAy[1].fZ * fRollVecY + pData->pointAy[2].fZ * fRollVecZ;

  // Apply azimuth rotation
  fRollRotatedX = fRollProjX * tcos[iAz2] + fRollProjY * tsin[iAz2];
  fRollRotatedY = -fRollProjX * tsin[iAz2] + fRollProjY * tcos[iAz2];

  // Apply elevation rotation
  fRollProjZ = (float)(fRollRotatedX * tsin[iElevation] - v24 * tcos[iElevation]);

  // Calculate bank
  *piBank = getangle(fRollRotatedY, fRollProjZ);
}

//-------------------------------------------------------------------------------------------------