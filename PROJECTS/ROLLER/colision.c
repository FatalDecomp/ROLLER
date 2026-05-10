#include "colision.h"
#include "loadtrak.h"
#include "3d.h"
#include "function.h"
#include "control.h"
#include "sound.h"
#include <math.h>
//-------------------------------------------------------------------------------------------------

float damage_levels[4] = {1.0, 2.0, 4.0, 8.0}; // 000A639C
float coldist[33][33];                         // 00188840
int damage_level;                              // 00189944

//-------------------------------------------------------------------------------------------------
// 0005F8F0
void testcollisions() {
  int iSecondCarIdx;  // esi
  tCar *pSecondCar;   // ecx
  int iChunkDistance; // ebx
  tCar *pTrailingCar; // edx
  tCar *pLeadingCar;  // eax
  int iFirstCarIdx;   // [esp+0h] [ebp-20h]
  int iCurrentCarIdx; // [esp+4h] [ebp-1Ch]

  iFirstCarIdx = 0; // Initialize first car index for outer loop
  if (numcars > 0) {
    iCurrentCarIdx = 0;
    do { // Check if car is AI controlled (type 3) and not destroyed (status bit
         // 2 clear)
      if (Car[iCurrentCarIdx].iControlType == 3 &&
          (Car[iCurrentCarIdx].byStatusFlags & 2) == 0) {
        iSecondCarIdx = iFirstCarIdx + 1; // Start inner loop from next car to
                                          // avoid duplicate collision checks
        if (iFirstCarIdx + 1 < numcars) {
          pSecondCar = &Car[iSecondCarIdx];
          do { // Check if second car is also AI controlled and not destroyed
            if (Car[iSecondCarIdx].iControlType == 3 &&
                (Car[iSecondCarIdx].byStatusFlags & 2) == 0) {
              iChunkDistance =
                  Car[iSecondCarIdx].nCurrChunk -
                  Car[iCurrentCarIdx]
                      .nCurrChunk; // Calculate distance between cars in track
                                   // chunks (handles circular track wraparound)
              if (iChunkDistance < 0)
                iChunkDistance +=
                    TRAK_LEN; // Handle negative distance by adding track length
                              // (circular track)
              if (iChunkDistance >
                  TRAK_LEN / 2) // Use shortest path around circular track
                                // (normalize distance > half track length)
                iChunkDistance -= TRAK_LEN;
              if ((int)abs(iChunkDistance) <
                  4) // Check if cars are close enough for collision (within 4
                     // track chunks)
              {
                pTrailingCar = &Car[iCurrentCarIdx];
                if (iChunkDistance <
                    0) // Determine which car is leading/trailing for collision
                       // calculation
                {
                  iChunkDistance = -iChunkDistance;
                  pLeadingCar = pSecondCar;
                } else {
                  pLeadingCar = &Car[iCurrentCarIdx];
                  pTrailingCar = pSecondCar;
                }
                testcoll(pLeadingCar, pTrailingCar,
                         iChunkDistance); // Perform detailed collision test
                                          // between leading and trailing cars
              }
            }
            ++iSecondCarIdx;
            ++pSecondCar;
          } while (iSecondCarIdx < numcars);
        }
      }
      ++iCurrentCarIdx;
      ++iFirstCarIdx;
    } while (iFirstCarIdx < numcars);
  }
}

//-------------------------------------------------------------------------------------------------
// 0005FA00
void testcoll(tCar *pCar1, tCar *pCar2, int iDistanceSteps) {
  tData *pData2;              // eax
  double dRelativeX1;         // st7
  double dRelativeY1;         // st6
  double dRelativeZ1;         // st5
  tData *pData1;              // eax
  double dRelativeZ1Copy;     // rt0
  double dTransformedY1;      // st5
  double dTransformedX1;      // st4
  double dProjectedX1;        // st7
  double dTransformedX1Copy;  // rt2
  double dTransformedZ1;      // st4
  int nCar2Yaw;               // ebx
  int nCurrChunk;             // eax
  int iChunkStep;             // edx
  int iChunkOffset;           // ecx
  int16 nCar2PredictedYaw;    // bx
  int iCollisionDetected;     // ebp
  int16 nAngleToCollision;    // ax
  int iCar1AngleDiff;         // ecx
  int iCar2AngleDiff;         // eax
  int iCar1NormalizedAngle;   // edx
  tData *pData2_2;            // eax
  double dRelativeX2;         // st7
  double dRelativeY2;         // st6
  double dRelativeZ2;         // st5
  tData *pData1_1;            // eax
  double dRelativeZ2Copy;     // rt0
  double dTransformedY2;      // st5
  double dTransformedX2;      // st4
  double dProjectedX2;        // st7
  double dTransformedX2Copy;  // rt2
  double dTransformedZ2;      // st4
  int iCollisionAngle;        // eax
  int16 nCollisionAngleShort; // dx
  int iCollisionDirection;    // ecx
  int iCar1Angle2Diff;        // ebp
  int iCar2Angle2Diff;        // eax
  int iCar1Normalized2;       // edx
  double dSeparationDistance; // st7
  double dMidpointX;          // st6
  double dMidpointY;          // st5
  int16 nCurrentChunk;        // ax
  tData *pTrackData;          // edx
  int iCar1VelAngle;          // eax
  int iCar2VelAngle;          // ebx
  double dInverseTotalMass;   // st4
  double dCar1NewVelocity;    // st7
  double dCar1NewVelY;        // st6
  double dCar2NewVelocity;    // st7
  double dMomentumFactor;     // st6
  int iDriverIdx;             // edx
  int iSoundEffectId;         // eax
  int iSelectedStrategy;      // eax
  double dCar1SineFactor;     // st7
  int iCar1AITimer;           // edx
  int iCar2Strategy;          // ebx
  double dCar2SineFactor;     // st7
  int iCar2AITimer;           // eax
  double dNegCar2VelX;        // st7
  double dInverseVelSum;      // st7
  int iSpeechId1;             // eax
  int iCar1DriverIdx;         // ecx
  int iSpeechId2;             // eax
  int iCar2DriverIdx;         // ecx
  float fDeltaX1;             // [esp+0h] [ebp-CCh]
  float fDeltaX2;             // [esp+0h] [ebp-CCh]
  float fDeltaX3;             // [esp+0h] [ebp-CCh]
  float fDeltaY1;             // [esp+4h] [ebp-C8h]
  float fDeltaY2;             // [esp+4h] [ebp-C8h]
  float fDeltaY3;             // [esp+4h] [ebp-C8h]
  float fCar1FinalSpeed;      // [esp+4h] [ebp-C8h]
  float fCar2FinalSpeed;      // [esp+4h] [ebp-C8h]
  float fCar1FinalDamage;     // [esp+4h] [ebp-C8h]
  float fCar2FinalDamage;     // [esp+4h] [ebp-C8h]
  float fFinalSpeed;          // [esp+18h] [ebp-B4h]
  float fCar1PosX2;           // [esp+20h] [ebp-ACh]
  float fCar1PosY2;           // [esp+28h] [ebp-A4h]
  float fCar2Speed;           // [esp+2Ch] [ebp-A0h]
  float fTransformedPosX1;    // [esp+38h] [ebp-94h]
  float fTransformedPosX2;    // [esp+38h] [ebp-94h]
  float fTransformedPosY1;    // [esp+44h] [ebp-88h]
  float fTransformedPosY2;    // [esp+44h] [ebp-88h]
  float fCar1Damage;          // [esp+50h] [ebp-7Ch]
  float fCar2VelY;            // [esp+58h] [ebp-74h]
  float fVelocityLimit1;      // [esp+5Ch] [ebp-70h]
  float fCar2Damage;          // [esp+60h] [ebp-6Ch]
  float fVelocityLimit2;      // [esp+64h] [ebp-68h]
  float fCar1VelY;            // [esp+68h] [ebp-64h]
  int16 nCar1Yaw;             // [esp+6Ch] [ebp-60h]
  int iVelocityAngle;         // [esp+70h] [ebp-5Ch]
  float fCar1NewVelY;         // [esp+74h] [ebp-58h]
  float fCar2NewVelX;         // [esp+78h] [ebp-54h]
  float fCar2NewVelY;         // [esp+7Ch] [ebp-50h]
  float fCar1NewVelX;         // [esp+80h] [ebp-4Ch]
  float fCar2VelX;            // [esp+84h] [ebp-48h]
  float fPosX1Copy;           // [esp+88h] [ebp-44h]
  float fY;                   // [esp+8Ch] [ebp-40h]
  float fPosY1Copy;           // [esp+90h] [ebp-3Ch]
  float fCar1VelX;            // [esp+94h] [ebp-38h]
  float fX;                   // [esp+98h] [ebp-34h]
  float fCar2Mass;            // [esp+9Ch] [ebp-30h]
  float fCar1Mass;            // [esp+A0h] [ebp-2Ch]
  float fAdjustedVel1;        // [esp+A4h] [ebp-28h]
  int iCar1RotAngle;          // [esp+A8h] [ebp-24h]
  float fAdjustedVel2;        // [esp+ACh] [ebp-20h]
  int iCar2RotAngle;          // [esp+B0h] [ebp-1Ch]
  float fSpeedDifference;     // [esp+B4h] [ebp-18h]
  float fDamageModifier;      // [esp+B4h] [ebp-18h]

  fX = pCar1->pos
           .fX; // Get current positions of both cars for collision calculation
  fY = pCar1->pos.fY;
  pData2 = &localdata[pCar2->nCurrChunk]; // Transform car2 position from world
                                          // space to track local coordinates
  dRelativeX1 = pData2->pointAy[0].fY * pCar2->pos.fY +
                pData2->pointAy[0].fX * pCar2->pos.fX +
                pData2->pointAy[0].fZ * pCar2->pos.fZ - pData2->pointAy[3].fX;
  dRelativeY1 = pData2->pointAy[1].fY * pCar2->pos.fY +
                pData2->pointAy[1].fX * pCar2->pos.fX +
                pData2->pointAy[1].fZ * pCar2->pos.fZ - pData2->pointAy[3].fY;
  dRelativeZ1 = pData2->pointAy[2].fY * pCar2->pos.fY +
                pData2->pointAy[2].fX * pCar2->pos.fX +
                pData2->pointAy[2].fZ * pCar2->pos.fZ - pData2->pointAy[3].fZ;
  pData1 = &localdata[pCar1->nCurrChunk]; // Transform relative position back to
                                          // car1's local coordinate system
  dRelativeZ1Copy = dRelativeZ1;
  dTransformedY1 = dRelativeY1 + pData1->pointAy[3].fY;
  dTransformedX1 = dRelativeX1 + pData1->pointAy[3].fX;
  dProjectedX1 = pData1->pointAy[1].fX * dTransformedY1 +
                 pData1->pointAy[0].fX * dTransformedX1;
  dTransformedX1Copy = dTransformedX1;
  dTransformedZ1 = (float)dRelativeZ1Copy + pData1->pointAy[3].fZ;
  fTransformedPosX1 =
      (float)dProjectedX1 + pData1->pointAy[2].fX * (float)dTransformedZ1;
  fTransformedPosY1 = (float)(dTransformedZ1 * pData1->pointAy[2].fY +
                              dTransformedX1Copy * pData1->pointAy[0].fY +
                              dTransformedY1 * pData1->pointAy[1].fY);
  fPosX1Copy = fTransformedPosX1;
  fPosY1Copy = fTransformedPosY1;
  nCar2Yaw = pCar2->nYaw;
  nCar1Yaw = pCar1->nYaw;
  nCurrChunk = pCar1->nCurrChunk;
  for (iChunkStep = 0; iChunkStep < iDistanceSteps;
       ++iChunkStep) // Predict car2's future position and angle based on
                     // distance steps ahead
  {
    iChunkOffset = nCurrChunk++ << 7;
    nCar2Yaw += *(int *)((char *)&localdata[0].iYaw + iChunkOffset);
    if (nCurrChunk == TRAK_LEN)
      nCurrChunk ^= TRAK_LEN;
  }
  fDeltaY1 = fTransformedPosY1 - fY;
  fDeltaX1 = fTransformedPosX1 - fX;
  nCar2PredictedYaw = nCar2Yaw & 0x3FFF;
  iCollisionDetected = 0;
  nAngleToCollision =
      getangle(fDeltaX1, fDeltaY1); // Calculate angle from car1 to collision
                                    // point for lookup table indexing
  iCar1AngleDiff = (nCar1Yaw - nAngleToCollision) &
                   0x3FFF; // Normalize angle differences to proper ranges for
                           // collision lookup table
  iCar2AngleDiff = (nCar2PredictedYaw - nAngleToCollision) & 0x3FFF;
  iCar1NormalizedAngle = iCar1AngleDiff;
  if (iCar1AngleDiff > 0x2000)
    iCar1NormalizedAngle = iCar1AngleDiff - 0x2000;
  if (iCar2AngleDiff > 0x2000)
    iCar2AngleDiff -= 0x2000;
  if (iCar1NormalizedAngle > 4096)
    iCar1NormalizedAngle -= 0x2000;
  if (iCar2AngleDiff > 4096)
    iCar2AngleDiff -= 0x2000;
  // TODO look at this and ensure casting is correct
  if (coldist[(((uint32)iCar1NormalizedAngle << 6) + 270336) >> 14]
             [(((uint32)iCar2AngleDiff << 6) + 270336) >> 14] >
      sqrt((fTransformedPosX1 - fX) * (fTransformedPosX1 - fX) +
           (fTransformedPosY1 - fY) *
               (fTransformedPosY1 -
                fY))) // Check if cars are within collision distance using
                      // precomputed lookup table
    iCollisionDetected = -1;
  if (iCollisionDetected) // COLLISION DETECTED: Begin collision resolution
                          // calculations
  {
    fCar1PosX2 = pCar1->posLastFrame.fX;
    fCar1PosY2 = pCar1->posLastFrame.fY;
    pData2_2 = &localdata[pCar2->nReferenceChunk];
    dRelativeX2 = pData2_2->pointAy[0].fY * pCar2->posLastFrame.fY +
                  pData2_2->pointAy[0].fX * pCar2->posLastFrame.fX +
                  pData2_2->pointAy[0].fZ * pCar2->posLastFrame.fZ -
                  pData2_2->pointAy[3].fX;
    dRelativeY2 = pData2_2->pointAy[1].fX * pCar2->posLastFrame.fX +
                  pData2_2->pointAy[1].fY * pCar2->posLastFrame.fY +
                  pData2_2->pointAy[1].fZ * pCar2->posLastFrame.fZ -
                  pData2_2->pointAy[3].fY;
    dRelativeZ2 = pData2_2->pointAy[2].fX * pCar2->posLastFrame.fX +
                  pData2_2->pointAy[2].fY * pCar2->posLastFrame.fY +
                  pData2_2->pointAy[2].fZ * pCar2->posLastFrame.fZ -
                  pData2_2->pointAy[3].fZ;
    pData1_1 = &localdata[pCar1->nReferenceChunk];
    dRelativeZ2Copy = dRelativeZ2;
    dTransformedY2 = dRelativeY2 + pData1_1->pointAy[3].fY;
    dTransformedX2 = dRelativeX2 + pData1_1->pointAy[3].fX;
    dProjectedX2 = pData1_1->pointAy[1].fX * dTransformedY2 +
                   pData1_1->pointAy[0].fX * dTransformedX2;
    dTransformedX2Copy = dTransformedX2;
    dTransformedZ2 = dRelativeZ2Copy + pData1_1->pointAy[3].fZ;
    fTransformedPosX2 =
        (float)(dProjectedX2 + pData1_1->pointAy[2].fX * dTransformedZ2);
    fTransformedPosY2 = (float)(dTransformedZ2 * pData1_1->pointAy[2].fY +
                                dTransformedX2Copy * pData1_1->pointAy[0].fY +
                                dTransformedY2 * pData1_1->pointAy[1].fY);
    fDeltaY2 = fPosY1Copy - fY;
    fDeltaX2 = fPosX1Copy - fX;
    iCollisionAngle = getangle(fDeltaX2, fDeltaY2);
    fDeltaY3 = fTransformedPosY2 - fCar1PosY2;
    fDeltaX3 = fTransformedPosX2 - fCar1PosX2;
    nCollisionAngleShort = iCollisionAngle;
    iCollisionDirection = iCollisionAngle;
    iVelocityAngle = getangle(fDeltaX3, fDeltaY3);
    iCar1Angle2Diff = (nCar1Yaw - nCollisionAngleShort) & 0x3FFF;
    iCar2Angle2Diff = (nCar2PredictedYaw - nCollisionAngleShort) & 0x3FFF;
    iCar1Normalized2 = iCar1Angle2Diff;
    if (iCar1Angle2Diff > 0x2000)
      iCar1Normalized2 = iCar1Angle2Diff - 0x2000;
    if (iCar2Angle2Diff > 0x2000)
      iCar2Angle2Diff -= 0x2000;
    if (iCar1Normalized2 > 4096)
      iCar1Normalized2 -= 0x2000;
    if (iCar2Angle2Diff > 4096)
      iCar2Angle2Diff -= 0x2000;
    // TODO look at this and ensure casting is correct
    dSeparationDistance =
        coldist[(((uint32)iCar1Normalized2 << 6) + 270336) >> 14]
               [(((uint32)iCar2Angle2Diff << 6) + 270336) >> 14] +
        60.0;
    dMidpointX = (fX + fPosX1Copy) * 0.5;
    dMidpointY = (fY + fPosY1Copy) * 0.5;
    pCar1->pos.fX =
        (float)(dMidpointX - dSeparationDistance * tcos[iCollisionDirection] *
                                 0.5); // Separate cars to prevent overlap: move
                                       // each car away from collision point
    pCar1->pos.fY = (float)(dMidpointY - dSeparationDistance *
                                             tsin[iCollisionDirection] * 0.5);
    pCar2->pos.fX = (float)(dMidpointX + dSeparationDistance *
                                             tcos[iCollisionDirection] * 0.5);
    pCar2->pos.fY = (float)(dMidpointY + dSeparationDistance *
                                             tsin[iCollisionDirection] * 0.5);
    nCurrentChunk = pCar1->nCurrChunk;
    pCar2->nCurrChunk = nCurrentChunk;
    pTrackData = &localdata[nCurrentChunk];
    if (fabs(pCar1->pos.fX) > pTrackData->fTrackHalfLength)
      scansection(pCar1);
    if (fabs(pCar2->pos.fX) > pTrackData->fTrackHalfLength)
      scansection(pCar2);
    fFinalSpeed = pCar1->fFinalSpeed;
    iCar1VelAngle = (nCar1Yaw - (int16)iVelocityAngle) & 0x3FFF;
    fCar1VelX = fFinalSpeed *
                tcos[iCar1VelAngle]; // Calculate velocity components for both
                                     // cars relative to collision angle
    fCar1VelY = fFinalSpeed * tsin[iCar1VelAngle];
    fCar2Speed = pCar2->fFinalSpeed;
    iCar2VelAngle = (nCar2PredictedYaw - (int16)iVelocityAngle) & 0x3FFF;
    fCar2VelX = fCar2Speed * tcos[iCar2VelAngle];
    fCar2VelY = fCar2Speed * tsin[iCar2VelAngle];
    if (fCar2VelX < (double)fCar1VelX) {
      fCar1Mass = CarEngines.engines[pCar1->byCarDesignIdx].fMass;
      fCar2Mass = CarEngines.engines[pCar2->byCarDesignIdx].fMass;
      dInverseTotalMass =
          1.0 / (fCar1Mass + fCar2Mass); // Calculate momentum exchange using
                                         // car masses and velocity differences
      dCar1NewVelocity = ((fCar1Mass - fCar2Mass * 0.6) * fCar1VelX +
                          fCar2Mass * 1.6 * fCar2VelX) *
                         dInverseTotalMass;
      fCar1NewVelX = (float)(dCar1NewVelocity * tcos[iVelocityAngle] -
                             fCar1VelY * tsin[iVelocityAngle]);
      dCar1NewVelY = (float)(dCar1NewVelocity * tsin[iVelocityAngle] +
                             fCar1VelY * tcos[iVelocityAngle]);
      dCar2NewVelocity = (fCar2VelX * (fCar2Mass - fCar1Mass * 0.6) +
                          fCar1VelX * (fCar1Mass * 1.6)) *
                         dInverseTotalMass;
      fCar1NewVelY = (float)dCar1NewVelY;
      fCar2NewVelX = (float)(dCar2NewVelocity * tcos[iVelocityAngle] -
                             fCar2VelY * tsin[iVelocityAngle]);
      iCar1RotAngle =
          ((uint16)pCar1->nActualYaw - (int16)iCollisionDirection) & 0x3FFF;
      fCar2NewVelY = (float)(dCar2NewVelocity * tsin[iVelocityAngle] +
                             fCar2VelY * tcos[iVelocityAngle]);
      if ((unsigned int)iCar1RotAngle > 0x2000)
        iCar1RotAngle =
            (((uint16)pCar1->nActualYaw - (int16)iCollisionDirection) &
             0x3FFF) -
            0x4000;
      iCar2RotAngle =
          ((uint16)pCar2->nActualYaw - (int16)iCollisionDirection) & 0x3FFF;
      if ((unsigned int)iCar2RotAngle > 0x2000)
        iCar2RotAngle =
            (((uint16)pCar2->nActualYaw - (int16)iCollisionDirection) &
             0x3FFF) -
            0x4000;
      fSpeedDifference = (float)fabs(fCar2VelX - fCar1VelX);
      dMomentumFactor = 1.0 / ((fCar2Mass + fCar1Mass) * 2048.0);
      dMomentumFactor = floor(dMomentumFactor); //_CHP();
      pCar1->iJumpMomentum =
          (int)(fCar2Mass * 2.0 * (double)iCar2RotAngle * fSpeedDifference *
                dMomentumFactor); // Calculate jump/spin momentum based on
                                  // angular impact and speed difference
      //_CHP();
      pCar2->iJumpMomentum =
          (int)(dMomentumFactor *
                (fCar1Mass * 2.0 * (double)iCar1RotAngle * fSpeedDifference));
      if (pCar1->iJumpMomentum > 1300)
        pCar1->iJumpMomentum = 1300;
      if (pCar2->iJumpMomentum > 1300)
        pCar2->iJumpMomentum = 1300;
      if (pCar1->iJumpMomentum < -1300)
        pCar1->iJumpMomentum = -1300;
      if (pCar2->iJumpMomentum < -1300)
        pCar2->iJumpMomentum = -1300;
      if (ViewType[0] == pCar1->iDriverIdx || ViewType[0] == pCar2->iDriverIdx)
        iDriverIdx = ViewType[0];
      else
        iDriverIdx = pCar1->iDriverIdx;
      //_CHP();
      if (fSpeedDifference >= 100.0)
        iSoundEffectId = 8;
      else
        iSoundEffectId = 7;
      sfxpend(
          iSoundEffectId, iDriverIdx,
          (int)(fSpeedDifference * 32768.0 *
                0.025)); // Play collision sound effect based on impact severity
      pCar1->nActualYaw = getangle(fCar1NewVelX, fCar1NewVelY);
      pCar2->nActualYaw = getangle(fCar2NewVelX, fCar2NewVelY);
      fCar1FinalSpeed = (float)sqrt(fCar1NewVelX * fCar1NewVelX +
                                    fCar1NewVelY * fCar1NewVelY);
      SetEngine(pCar1, fCar1FinalSpeed);
      fCar2FinalSpeed = (float)sqrt(fCar2NewVelX * fCar2NewVelX +
                                    fCar2NewVelY * fCar2NewVelY);
      SetEngine(pCar2, fCar2FinalSpeed);
      iSelectedStrategy = pCar1->iSelectedStrategy;
      if (iSelectedStrategy) {
        if (iSelectedStrategy != 2)
          changestrategy(pCar1);
        iCar1AITimer = 0;
      } else {
        dCar1SineFactor = fabs(tsin[iCar1RotAngle & 0x3FFF]);
        //_CHP();
        iCar1AITimer = 18 * (int)dCar1SineFactor;
      }
      iCar2Strategy = pCar2->iSelectedStrategy;
      if (iCar2Strategy) {
        if (iCar2Strategy != 2)
          changestrategy(pCar2);
        iCar2AITimer = 0;
      } else {
        dCar2SineFactor = fabs(tsin[iCar2RotAngle & 0x3FFF]);
        //_CHP();
        iCar2AITimer = 18 * (int)dCar2SineFactor;
      }
      if (human_control[pCar1->iDriverIdx] && pCar2->byCarDesignIdx != 13)
        iCar2AITimer += 18;
      if (human_control[pCar2->iDriverIdx] && pCar1->byCarDesignIdx != 13)
        iCar1AITimer += 18;
      if (pCar1->iAIUpdateTimer < 144)
        pCar1->iAIUpdateTimer = iCar1AITimer;
      if (pCar1->iAIUpdateTimer < 0)
        pCar1->iAIUpdateTimer = 0;
      if (pCar2->iAIUpdateTimer < 144)
        pCar2->iAIUpdateTimer = iCar2AITimer;
      if (pCar2->iAIUpdateTimer < 0)
        pCar2->iAIUpdateTimer = 0;
      dNegCar2VelX = -fCar2VelX;
      fAdjustedVel1 = (float)dNegCar2VelX;
      fAdjustedVel2 = fCar1VelX;
      if (dNegCar2VelX < 0.01)
        fAdjustedVel1 = 0.0099999998f;
      if (fCar1VelX < 0.01)
        fAdjustedVel2 = 0.0099999998f;
      fVelocityLimit2 = fAdjustedVel2 * 9.0f;
      if (fAdjustedVel1 > (double)fVelocityLimit2)
        fAdjustedVel1 = fAdjustedVel2 * 9.0f;
      fVelocityLimit1 = fAdjustedVel1 * 9.0f;
      if (fAdjustedVel2 > (double)fVelocityLimit1)
        fAdjustedVel2 = fAdjustedVel1 * 9.0f;
      fDamageModifier = fSpeedDifference + damage_levels[damage_level];
      dInverseVelSum =
          1.0 / (fAdjustedVel1 +
                 fAdjustedVel2); // Calculate damage amounts based on velocity
                                 // ratios and impact severity
      fCar1Damage =
          (float)(fAdjustedVel1 * fDamageModifier * 0.006 * dInverseVelSum);
      fCar2Damage =
          (float)(dInverseVelSum * (fAdjustedVel2 * fDamageModifier * 0.006));
      if (fCar1Damage >= 1.0) {
        if (player1_car == pCar2->iDriverIdx ||
            player2_car == pCar2->iDriverIdx) {
          iSpeechId1 = 48;
        LABEL_87:
          speechonly(iSpeechId1, 0x8000, 18,
                     pCar1->iDriverIdx); // Trigger driver speech/taunts based
                                         // on damage dealt to opponents
          goto LABEL_88;
        }
        iCar1DriverIdx = pCar1->iDriverIdx;
        if (player1_car == iCar1DriverIdx || player2_car == iCar1DriverIdx) {
          iSpeechId1 = 47;
          goto LABEL_87;
        }
      }
    LABEL_88:
      if (fCar2Damage >= 1.0) {
        if (player1_car == pCar1->iDriverIdx ||
            player2_car == pCar1->iDriverIdx) {
          iSpeechId2 = 48;
        } else {
          iCar2DriverIdx = pCar2->iDriverIdx;
          if (player1_car != iCar2DriverIdx && player2_car != iCar2DriverIdx)
            goto LABEL_96;
          iSpeechId2 = 47;
        }
        speechonly(iSpeechId2, 0x8000, 18, pCar2->iDriverIdx);
      }
    LABEL_96:
      if (pCar1->fHealth > 0.0)
        pCar1->byAttacker = pCar2->iDriverIdx;
      pCar1->byDamageSourceTimer = -40;
      if (pCar2->fHealth > 0.0)
        pCar2->byAttacker = pCar1->iDriverIdx;
      pCar2->byDamageSourceTimer = -40;
      fCar1FinalDamage = fCar1Damage * damage_levels[damage_level];
      dodamage(pCar1, fCar1FinalDamage); // Apply calculated damage to both cars
                                         // and set attacker information
      fCar2FinalDamage = fCar2Damage * damage_levels[damage_level];
      dodamage(pCar2, fCar2FinalDamage);
    }
  }
}

//-------------------------------------------------------------------------------------------------
// 000604B0
void initcollisions() {
  int iAngleIdx1;        // ecx
  int iTableOffset;      // esi
  int iCurrentOffset;    // ebx
  double dCarBaseX;      // [esp+38h] [ebp-38h]
  double dCarBaseY;      // [esp+40h] [ebp-30h]
  int iAngleIdx2;        // [esp+4Ch] [ebp-24h]
  int iCurrentDistance;  // [esp+50h] [ebp-20h]
  int iBinarySearchStep; // [esp+54h] [ebp-1Ch]

  dCarBaseX =
      CarBaseX; // Load car bounding box dimensions for collision calculations
  iAngleIdx1 = -4096; // Initialize first car angle index (-4096 to 4352, 256
                      // steps = 34 angles)
  iTableOffset =
      0; // Initialize collision distance table offset (34x34 lookup table)
  dCarBaseY = CarBaseY;
  do {
    iCurrentOffset = iTableOffset; // Start building row in collision distance
                                   // table for current angle pair
    for (iAngleIdx2 = -4096; iAngleIdx2 != 4352;
         iAngleIdx2 += 256) // Inner loop: iterate through second car angles
                            // (-4096 to 4352, 256 steps)
    {
      iCurrentDistance = 1600; // Initialize binary search for minimum collision
                               // distance (start at 1600)
      iBinarySearchStep = 1024; // Binary search step size starts at 1024
                                // (halved each iteration)
      do { // Test collision at current distance between cars at angle
           // difference
        if (!checkintersect(0.0, 0.0, (double)iAngleIdx1,
                            (double)iCurrentDistance -
                                (double)iBinarySearchStep,
                            0.0, (double)iAngleIdx2, dCarBaseX, dCarBaseY))
          iCurrentDistance -=
              iBinarySearchStep; // No collision detected: reduce distance and
                                 // continue binary search
        iBinarySearchStep /=
            2; // Halve binary search step size for next iteration
      } while (iBinarySearchStep);
      // TODO make this more readable
      *(float *)((char *)&coldist + iCurrentOffset) =
          (float)iCurrentDistance; // Store minimum collision distance in lookup
                                   // table (coldist array)
      iCurrentOffset += 4;
      //*(float *)((char *)&result_p1_pos + iCurrentOffset) =
      //(float)iCurrentDistance;// Store minimum collision distance in lookup
      // table (coldist array)
    }
    iAngleIdx1 += 256;   // Move to next angle index for first car (256 units =
                         // ~5.625 degrees)
    iTableOffset += 132; // Move to next row in collision distance table (132
                         // bytes = 33 floats)
  } while (iAngleIdx1 != 4352);
}

//-------------------------------------------------------------------------------------------------
// 000605C0
int checkintersect(double dCar1PosX, double dCar1PosY, double dAngleIdx1,
                   double dCar2PosX, double dCar2PosY, double dAngleIdx2,
                   double dCarBaseX, double dCarBaseY) {
  double dAngleDifference;   // st6
  double dCosAngle2;         // rt2
  int iIntersects;           // edx
  double dCosAngle1;         // rt1
  double dTransformedDistX1; // [esp+48h] [ebp-3Ch]
  double dTransformedDistY2; // [esp+50h] [ebp-34h]
  double dTransformedDistY1; // [esp+58h] [ebp-2Ch]
  double dTransformedDistX2; // [esp+60h] [ebp-24h]
  double dCosAngleDelta;     // [esp+68h] [ebp-1Ch]
  double dSinAngleDelta;     // [esp+70h] [ebp-14h]
  double dSinAngle2;         // [esp+78h] [ebp-Ch]
  double dSinAngle1;         // [esp+78h] [ebp-Ch]

  dAngleDifference =
      (dAngleIdx1 - dAngleIdx2) *
      0.0003834951969714356; // Calculate angle difference between two cars
                             // (convert from angle indices to radians)
  dCosAngleDelta =
      cos(dAngleDifference); // Calculate trigonometric values for angle
                             // difference (rotation matrix components)
  dSinAngleDelta = sin(dAngleDifference);
  dCosAngle2 =
      cos(dAngleIdx2 * 0.0003834951969714356); // Calculate trigonometric values
                                               // for second car's orientation
  dSinAngle2 = sin(dAngleIdx2 * 0.0003834951969714356);
  dTransformedDistX1 = (dCar1PosX - dCar2PosX) * dCosAngle2 +
                       (dCar1PosY - dCar2PosY) *
                           dSinAngle2; // Transform car position difference into
                                       // second car's local coordinate system
  dTransformedDistY1 = dCosAngle2 * (dCar1PosY - dCar2PosY) +
                       (dCar2PosX - dCar1PosX) * dSinAngle2;
  iIntersects = 0; // Initialize intersection result (0 = no intersection, -1 =
                   // intersection found)
  if (fabs(dCosAngleDelta * dCarBaseX - dSinAngleDelta * dCarBaseY +
           dTransformedDistX1) <= dCarBaseX &&
      fabs(dCarBaseY * dCosAngleDelta + dSinAngleDelta * dCarBaseX +
           dTransformedDistY1) <=
          dCarBaseY) // Test intersection for first car's corner (+X, +Y)
                     // against second car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * dCarBaseX - dSinAngleDelta * -dCarBaseY +
           dTransformedDistX1) <= dCarBaseX &&
      fabs(-dCarBaseY * dCosAngleDelta + dSinAngleDelta * dCarBaseX +
           dTransformedDistY1) <=
          dCarBaseY) // Test intersection for first car's corner (+X, -Y)
                     // against second car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * -dCarBaseX - dSinAngleDelta * dCarBaseY +
           dTransformedDistX1) <= dCarBaseX &&
      fabs(-dCarBaseX * dSinAngleDelta + dCarBaseY * dCosAngleDelta +
           dTransformedDistY1) <=
          dCarBaseY) // Test intersection for first car's corner (-X, +Y)
                     // against second car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * -dCarBaseX - dSinAngleDelta * -dCarBaseY +
           dTransformedDistX1) <= dCarBaseX &&
      fabs(-dCarBaseX * dSinAngleDelta + dCosAngleDelta * -dCarBaseY +
           dTransformedDistY1) <=
          dCarBaseY) // Test intersection for first car's corner (-X, -Y)
                     // against second car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dTransformedDistX1) <= dCarBaseX &&
      fabs(dTransformedDistY1) <=
          dCarBaseY) // Test if first car's center point is inside second car's
                     // bounding box
    iIntersects = -1;
  // HIBYTE(dSinAngleDelta) ^= 0x80u;              // Flip sign of sine
  // component to reverse transformation direction
  dSinAngleDelta = -dSinAngleDelta;
  dCosAngle1 =
      cos(dAngleIdx1 * 0.0003834951969714356); // Calculate trigonometric values
                                               // for first car's orientation
  dSinAngle1 = sin(dAngleIdx1 * 0.0003834951969714356);
  dTransformedDistX2 = (dCar2PosX - dCar1PosX) * dCosAngle1 +
                       (dCar2PosY - dCar1PosY) *
                           dSinAngle1; // Transform car position difference into
                                       // first car's local coordinate system
  dTransformedDistY2 = dCosAngle1 * (dCar2PosY - dCar1PosY) +
                       (dCar1PosX - dCar2PosX) * dSinAngle1;
  if (fabs(dCosAngleDelta * dCarBaseX - dSinAngleDelta * dCarBaseY +
           dTransformedDistX2) <= dCarBaseX &&
      fabs(dSinAngleDelta * dCarBaseX + dCarBaseY * dCosAngleDelta +
           dTransformedDistY2) <=
          dCarBaseY) // Test intersection for second car's corner (+X, +Y)
                     // against first car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * dCarBaseX - dSinAngleDelta * -dCarBaseY +
           dTransformedDistX2) <= dCarBaseX &&
      fabs(-dCarBaseY * dCosAngleDelta + dSinAngleDelta * dCarBaseX +
           dTransformedDistY2) <=
          dCarBaseY) // Test intersection for second car's corner (+X, -Y)
                     // against first car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * -dCarBaseX - dSinAngleDelta * dCarBaseY +
           dTransformedDistX2) <= dCarBaseX &&
      fabs(-dCarBaseX * dSinAngleDelta + dCarBaseY * dCosAngleDelta +
           dTransformedDistY2) <=
          dCarBaseY) // Test intersection for second car's corner (-X, +Y)
                     // against first car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dCosAngleDelta * -dCarBaseX - dSinAngleDelta * -dCarBaseY +
           dTransformedDistX2) <= dCarBaseX &&
      fabs(-dCarBaseX * dSinAngleDelta + dCosAngleDelta * -dCarBaseY +
           dTransformedDistY2) <=
          dCarBaseY) // Test intersection for second car's corner (-X, -Y)
                     // against first car's bounding box
  {
    iIntersects = -1;
  }
  if (fabs(dTransformedDistX2) <= dCarBaseX &&
      fabs(dTransformedDistY2) <=
          dCarBaseY) // Test if second car's center point is inside first car's
                     // bounding box
    return -1;
  return iIntersects;
}

//-------------------------------------------------------------------------------------------------