#include "net_snapshot.h"
#include "net_sim_seam.h"
#include "control.h"
#include "loadtrak.h"
#include "roller.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct { uint16 unOffset; uint8 bySize; } tNetField;
static const tNetField aCarFields[] = {
  {offsetof(tNetCarState, fWorldPosX), sizeof(float)},
  {offsetof(tNetCarState, fWorldPosY), sizeof(float)},
  {offsetof(tNetCarState, fWorldPosZ), sizeof(float)},
  {offsetof(tNetCarState, fFinalSpeed), sizeof(float)},
  {offsetof(tNetCarState, fHorizontalSpeed), sizeof(float)},
  {offsetof(tNetCarState, fVelX), sizeof(float)},
  {offsetof(tNetCarState, fVelY), sizeof(float)},
  {offsetof(tNetCarState, fVelZ), sizeof(float)},
  {offsetof(tNetCarState, nCurrChunk), sizeof(int16)},
  {offsetof(tNetCarState, nReferenceChunk), sizeof(int16)},
  {offsetof(tNetCarState, nLastValidChunk), sizeof(int16)},
  {offsetof(tNetCarState, nWorldRoll), sizeof(int16)},
  {offsetof(tNetCarState, nWorldPitch), sizeof(int16)},
  {offsetof(tNetCarState, nWorldYaw), sizeof(int16)},
  {offsetof(tNetCarState, nActualYaw), sizeof(int16)},
  {offsetof(tNetCarState, nDeathTimer), sizeof(int16)},
  {offsetof(tNetCarState, nJumpMomentum), sizeof(int16)},
  {offsetof(tNetCarState, byHealth), sizeof(uint8)},
  {offsetof(tNetCarState, byLives), sizeof(uint8)},
  {offsetof(tNetCarState, byLap), sizeof(uint8)},
  {offsetof(tNetCarState, byRacePosition), sizeof(uint8)},
  {offsetof(tNetCarState, byStatusFlags), sizeof(uint8)},
  {offsetof(tNetCarState, byStunned), sizeof(uint8)},
  {offsetof(tNetCarState, byDamageIntensity), sizeof(uint8)},
  {offsetof(tNetCarState, byDamageState), sizeof(uint8)},
  {offsetof(tNetCarState, byWheelAnimationFrame), sizeof(uint8)},
  {offsetof(tNetCarState, byGearAyMax), sizeof(uint8)},
  {offsetof(tNetCarState, byHumanControl), sizeof(uint8)},
  {offsetof(tNetCarState, byControlType), sizeof(uint8)},
  {offsetof(tNetCarState, byCheatAmmo), sizeof(uint8)},
  {offsetof(tNetCarState, byPad), sizeof(uint8)},
};
_Static_assert(sizeof(aCarFields) / sizeof(aCarFields[0]) == 31, "car mask has 31 fields");

/* Canonical little-endian scalars, including IEEE-754 float bit patterns. */
static void NetWireScalar(uint8 *pOutput, const void *pInput, int iSize)
{
  const uint16 unEndian = 1;
  const uint8 *pBytes = pInput;
  if (*(const uint8 *)&unEndian)
    memcpy(pOutput, pInput, (size_t)iSize);
  else
    for (int iByte = 0; iByte < iSize; ++iByte)
      pOutput[iByte] = pBytes[iSize - iByte - 1];
}
static void NetWireCar(uint8 *pOutput, const uint8 *pInput)
{
  for (int iField = 0; iField < 31; ++iField)
    NetWireScalar(pOutput + aCarFields[iField].unOffset,
                  pInput + aCarFields[iField].unOffset, aCarFields[iField].bySize);
}
static void NetWireContext(uint8 *pOutput, const uint8 *pInput)
{
  NetWireScalar(pOutput, pInput, 4);
  NetWireScalar(pOutput + 4, pInput + 4, 4);
  pOutput[8] = pInput[8]; pOutput[9] = pInput[9];
  NetWireScalar(pOutput + 10, pInput + 10, 2);
}
static int NetSnapshotValid(const tNetSnapshot *pSnapshot)
{
  if (!pSnapshot || pSnapshot->byNumCars > MAX_CARS || pSnapshot->byNumRamps > NET_MAX_RAMPS ||
      pSnapshot->byPaused > 1 || pSnapshot->context.byRaceStarted > 1 || pSnapshot->context.byRacing > 1)
    return 0;
  for (int iCar = 0; iCar < pSnapshot->byNumCars; ++iCar) {
    const tNetCarState *pCar = &pSnapshot->aCars[iCar];
    if (pCar->nCurrChunk < -1 || pCar->nCurrChunk >= MAX_TRACK_CHUNKS ||
        pCar->nReferenceChunk < -1 || pCar->nReferenceChunk >= MAX_TRACK_CHUNKS ||
        pCar->nLastValidChunk < -1 || pCar->nLastValidChunk >= MAX_TRACK_CHUNKS ||
        pCar->nWorldYaw < 0 || pCar->nWorldYaw > 16383 ||
        pCar->nWorldPitch < 0 || pCar->nWorldPitch > 16383 ||
        pCar->nWorldRoll < 0 || pCar->nWorldRoll > 16383 ||
        pCar->nActualYaw < 0 || pCar->nActualYaw > 16383 ||
        pCar->byControlType > 3 || pCar->byHumanControl > 2 || pCar->byHealth > 100)
      return 0;
    for (int iField = 0; iField < 8; ++iField) {
      float fValue;
      memcpy(&fValue, (const uint8 *)pCar + aCarFields[iField].unOffset, 4);
      if (!isfinite(fValue))
        return 0;
    }
  }
  return 1;
}

int NetSnapshotEncodeCarFull(int iCar, tNetCarFullState *pState)
{
  const tCar *pCar;
  tNetWorldPose pose;
  if (!pState || iCar < 0 || iCar >= numcars || !NetSimLegacyToWorld(&Car[iCar], &pose))
    return 0;
  pCar = &Car[iCar];
  memset(pState, 0, sizeof(*pState));
  pState->state.fWorldPosX = pose.position.fX;
  pState->state.fWorldPosY = pose.position.fY;
  pState->state.fWorldPosZ = pose.position.fZ;
  pState->state.nWorldYaw = pose.nYaw;
  pState->state.nWorldPitch = pose.nPitch;
  pState->state.nWorldRoll = pose.nRoll;
  pState->state.nActualYaw = pose.nActualYaw;
  pState->state.fFinalSpeed = pCar->fFinalSpeed;
  pState->state.fHorizontalSpeed = pCar->fHorizontalSpeed;
  pState->state.fVelX = pCar->direction.fX;
  pState->state.fVelY = pCar->direction.fY;
  pState->state.fVelZ = pCar->direction.fZ;
  pState->state.nCurrChunk = pCar->nCurrChunk;
  pState->state.nReferenceChunk = pCar->nReferenceChunk;
  pState->state.nLastValidChunk = pCar->iLastValidChunk;
  pState->state.nDeathTimer = pCar->nDeathTimer;
  pState->state.nJumpMomentum = pCar->iJumpMomentum;
  pState->state.byLives = pCar->byLives;
  pState->state.byLap = pCar->byLap;
  pState->state.byRacePosition = pCar->byRacePosition;
  pState->state.byStatusFlags = pCar->byStatusFlags;
  pState->state.byStunned = pCar->iStunned;
  pState->state.byDamageIntensity = pCar->byDamageIntensity;
  pState->state.byDamageState = pCar->iDamageState;
  pState->state.byWheelAnimationFrame = pCar->byWheelAnimationFrame;
  pState->state.byGearAyMax = pCar->byGearAyMax;
  pState->state.byControlType = pCar->iControlType;
  pState->state.byCheatAmmo = pCar->byCheatAmmo;
  pState->state.byHealth = (uint8)fmaxf(0, fminf(100, pCar->fHealth));
  pState->state.byHumanControl = (uint8)human_control[iCar];
  pState->extra.fRunningLapTime = pCar->fRunningLapTime;
  pState->extra.fBestLapTime = pCar->fBestLapTime;
  pState->extra.fPreviousLapTime = pCar->fPreviousLapTime;
  pState->extra.fTotalRaceTime = pCar->fTotalRaceTime;
  pState->extra.fBaseSpeed = pCar->fBaseSpeed;
  pState->extra.fSpeedOverflow = pCar->fSpeedOverflow;
  pState->extra.fPower = pCar->fPower;
  pState->extra.fDurability = pCar->fDurability;
  pState->extra.fRPMRatio = pCar->fRPMRatio;
  pState->extra.iRollMomentum = pCar->iRollMomentum;
  pState->extra.iRollMotion = pCar->iRollMotion;
  pState->extra.iPitchMotion = pCar->iPitchMotion;
  pState->extra.iYawMotion = pCar->iYawMotion;
  pState->extra.iEngineState = pCar->iEngineState;
  pState->extra.iSteeringInput = pCar->iSteeringInput;
  pState->extra.iBankingSteerOffset = pCar->iBankingSteerOffset;
  pState->extra.nTargetChunk = pCar->nTargetChunk;
  pState->extra.nChangeMateCooldown = pCar->nChangeMateCooldown;
  pState->extra.byKills = pCar->byKills;
  pState->extra.byAttacker = pCar->byAttacker;
  pState->extra.byLapNumber = pCar->byLapNumber;
  pState->extra.byDamageToggle = pCar->byDamageToggle;
  pState->extra.byCheatCooldown = pCar->byCheatCooldown;
  pState->extra.byEngineStartTimer = pCar->byEngineStartTimer;
  pState->extra.byThrottlePressed = pCar->byThrottlePressed;
  pState->extra.byAccelerating = pCar->byAccelerating;
  pState->extra.byAIThrottleControl = pCar->byAIThrottleControl;
  pState->extra.byPitLaneActiveFlag = pCar->byPitLaneActiveFlag;
  pState->extra.byCollisionTimer = pCar->byCollisionTimer;
  pState->extra.byFinishPosition = finished_car[iCar] ? pCar->byRacePosition : 255;
  return 1;
}

int NetSnapshotDecodeCarFull(int iCar, const tNetCarFullState *pState)
{
  tCar car;
  tNetWorldPose pose;
  tNetSnapshot validation = {0};
  if (!pState || iCar < 0 || iCar >= numcars || pState->state.nCurrChunk >= TRAK_LEN)
    return 0;
  validation.byNumCars = 1;
  validation.aCars[0] = pState->state;
  if (!NetSnapshotValid(&validation))
    return 0;
  for (int iField = 0; iField < 9; ++iField) {
    float fValue;
    memcpy(&fValue, (const uint8 *)&pState->extra + iField * 4, 4);
    if (!isfinite(fValue))
      return 0;
  }
  car = Car[iCar];
  car.fFinalSpeed = pState->state.fFinalSpeed;
  car.fHorizontalSpeed = pState->state.fHorizontalSpeed;
  car.direction.fX = pState->state.fVelX;
  car.direction.fY = pState->state.fVelY;
  car.direction.fZ = pState->state.fVelZ;
  car.nCurrChunk = pState->state.nCurrChunk;
  car.nReferenceChunk = pState->state.nReferenceChunk;
  car.iLastValidChunk = pState->state.nLastValidChunk;
  car.nDeathTimer = pState->state.nDeathTimer;
  car.iJumpMomentum = pState->state.nJumpMomentum;
  car.byLives = pState->state.byLives;
  car.byLap = pState->state.byLap;
  car.byRacePosition = pState->state.byRacePosition;
  car.byStatusFlags = pState->state.byStatusFlags;
  car.iStunned = pState->state.byStunned;
  car.byDamageIntensity = pState->state.byDamageIntensity;
  car.iDamageState = pState->state.byDamageState;
  car.byWheelAnimationFrame = pState->state.byWheelAnimationFrame;
  car.byGearAyMax = pState->state.byGearAyMax;
  car.iControlType = pState->state.byControlType;
  car.byCheatAmmo = pState->state.byCheatAmmo;
  car.fHealth = pState->state.byHealth;
  car.fRunningLapTime = pState->extra.fRunningLapTime;
  car.fBestLapTime = pState->extra.fBestLapTime;
  car.fPreviousLapTime = pState->extra.fPreviousLapTime;
  car.fTotalRaceTime = pState->extra.fTotalRaceTime;
  car.fBaseSpeed = pState->extra.fBaseSpeed;
  car.fSpeedOverflow = pState->extra.fSpeedOverflow;
  car.fPower = pState->extra.fPower;
  car.fDurability = pState->extra.fDurability;
  car.fRPMRatio = pState->extra.fRPMRatio;
  car.iRollMomentum = pState->extra.iRollMomentum;
  car.iRollMotion = pState->extra.iRollMotion;
  car.iPitchMotion = pState->extra.iPitchMotion;
  car.iYawMotion = pState->extra.iYawMotion;
  car.iEngineState = pState->extra.iEngineState;
  car.iSteeringInput = pState->extra.iSteeringInput;
  car.iBankingSteerOffset = pState->extra.iBankingSteerOffset;
  car.nTargetChunk = pState->extra.nTargetChunk;
  car.nChangeMateCooldown = pState->extra.nChangeMateCooldown;
  car.byKills = pState->extra.byKills;
  car.byAttacker = pState->extra.byAttacker;
  car.byLapNumber = pState->extra.byLapNumber;
  car.byDamageToggle = pState->extra.byDamageToggle;
  car.byCheatCooldown = pState->extra.byCheatCooldown;
  car.byEngineStartTimer = pState->extra.byEngineStartTimer;
  car.byThrottlePressed = pState->extra.byThrottlePressed;
  car.byAccelerating = pState->extra.byAccelerating;
  car.byAIThrottleControl = pState->extra.byAIThrottleControl;
  car.byPitLaneActiveFlag = pState->extra.byPitLaneActiveFlag;
  car.byCollisionTimer = pState->extra.byCollisionTimer;
  pose.position.fX = pState->state.fWorldPosX;
  pose.position.fY = pState->state.fWorldPosY;
  pose.position.fZ = pState->state.fWorldPosZ;
  pose.nYaw = pState->state.nWorldYaw; pose.nPitch = pState->state.nWorldPitch;
  pose.nRoll = pState->state.nWorldRoll; pose.nActualYaw = pState->state.nActualYaw;
  if (!NetSimWorldToLegacy(&pose, &car))
    return 0;
  NetSimRehomeChunk(&car);
  Car[iCar] = car;
  human_control[iCar] = pState->state.byHumanControl;
  finished_car[iCar] = pState->extra.byFinishPosition != 255 ? -1 : 0;
  return 1;
}

int NetSnapshotBuild(tNetSnapshot *pSnapshot, uint32 uiTick, uint32 uiLastEventSeq, uint8 byRaceState, uint8 byPaused)
{
  tNetSnapshot snapshot = {0};
  if (!pSnapshot || numcars < 0 || numcars > MAX_CARS || totalramps < 0 || totalramps > NET_MAX_RAMPS)
    return 0;
  snapshot.uiTick = uiTick; snapshot.uiLastEventSeq = uiLastEventSeq;
  snapshot.uiRandomState = ROLLERrandStateGet();
  snapshot.context.iGameFrame = game_frame; snapshot.context.iCountdown = countdown;
  snapshot.context.byRaceStarted = race_started != 0; snapshot.context.byRacing = racing != 0;
  snapshot.context.nWarpAngle = (int16)warp_angle;
  snapshot.byNumCars = (uint8)numcars; snapshot.byNumRamps = (uint8)totalramps;
  snapshot.byRaceState = byRaceState; snapshot.byPaused = byPaused;
  NetSimSaveRamps(snapshot.aRamps);
  for (int iCar = 0; iCar < numcars; ++iCar) {
    tNetCarFullState full;
    if (!NetSnapshotEncodeCarFull(iCar, &full))
      return 0;
    snapshot.aCars[iCar] = full.state;
  }
  if (!NetSnapshotValid(&snapshot))
    return 0;
  *pSnapshot = snapshot;
  return 1;
}

static int16 NetSnapshotAngle(int16 nFrom, int16 nTo, float fFraction)
{
  int iDelta = ((nTo - nFrom + 8192) & 16383) - 8192;
  return (int16)((nFrom + (int)lroundf(iDelta * fFraction)) & 16383);
}
int NetSnapshotInterpolate(const tNetCarState *pOlder, const tNetCarState *pNewer, float fFraction, tNetCarState *pResult)
{
  if (!pOlder || !pNewer || !pResult || !isfinite(fFraction))
    return 0;
  fFraction = fmaxf(0, fminf(1, fFraction));
  *pResult = *pNewer;
  for (int iField = 0; iField < 8; ++iField) {
    float fOlder, fNewer, fValue;
    int iOffset = aCarFields[iField].unOffset;
    memcpy(&fOlder, (const uint8 *)pOlder + iOffset, 4);
    memcpy(&fNewer, (const uint8 *)pNewer + iOffset, 4);
    fValue = fOlder + (fNewer - fOlder) * fFraction;
    memcpy((uint8 *)pResult + iOffset, &fValue, 4);
  }
#define NET_ANGLE(nField) pResult->nField = NetSnapshotAngle(pOlder->nField, pNewer->nField, fFraction)
  NET_ANGLE(nWorldRoll); NET_ANGLE(nWorldPitch); NET_ANGLE(nWorldYaw); NET_ANGLE(nActualYaw);
#undef NET_ANGLE
  return 1;
}

static void NetWireSnapshot(uint8 *pOutput, const uint8 *pInput)
{
  for (int iField = 0; iField < 3; ++iField)
    NetWireScalar(pOutput + iField * 4, pInput + iField * 4, 4);
  NetWireContext(pOutput + 12, pInput + 12);
  memcpy(pOutput + 24, pInput + 24, 8);
  for (int iField = 0; iField < 24; ++iField)
    NetWireScalar(pOutput + 32 + iField * 2, pInput + 32 + iField * 2, 2);
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    NetWireCar(pOutput + 80 + iCar * 64, pInput + 80 + iCar * 64);
}
int NetSnapshotEncode(const tNetSnapshot *pSnapshot, uint8 *pBytes, int iCapacity)
{
  if (!pBytes || iCapacity < (int)sizeof(*pSnapshot) || !NetSnapshotValid(pSnapshot))
    return 0;
  NetWireSnapshot(pBytes, (const uint8 *)pSnapshot);
  return sizeof(*pSnapshot);
}
int NetSnapshotDecode(const uint8 *pBytes, int iLength, tNetSnapshot *pSnapshot)
{
  tNetSnapshot snapshot;
  if (!pBytes || !pSnapshot || iLength != sizeof(snapshot))
    return 0;
  NetWireSnapshot((uint8 *)&snapshot, pBytes);
  if (!NetSnapshotValid(&snapshot))
    return 0;
  *pSnapshot = snapshot;
  return 1;
}

int NetSnapshotEncodeDelta(const tNetSnapshot *pBase, const tNetSnapshot *pCurrent, uint8 *pBytes, int iCapacity)
{
  uint8 abEncoded[NET_MAX_PAYLOAD];
  tNetSnapshotDeltaHeader header = {0};
  int iLength = sizeof(header);
  if (!pBytes || !NetSnapshotValid(pBase) || !NetSnapshotValid(pCurrent))
    return 0;
  header.uiTick = pCurrent->uiTick; header.uiBaseTick = pBase->uiTick;
  header.uiLastEventSeq = pCurrent->uiLastEventSeq; header.uiRandomState = pCurrent->uiRandomState;
  header.context = pCurrent->context;
  header.byNumCars = pCurrent->byNumCars; header.byNumRamps = pCurrent->byNumRamps;
  header.byRaceState = pCurrent->byRaceState; header.byPaused = pCurrent->byPaused;
  for (int iRamp = 0; iRamp < NET_MAX_RAMPS; ++iRamp)
    if (memcmp(&pCurrent->aRamps[iRamp], &pBase->aRamps[iRamp], 6)) {
      header.unRampMask |= (uint16)(1u << iRamp);
      for (int iField = 0; iField < 3; ++iField)
        NetWireScalar(abEncoded + iLength + iField * 2, (const uint8 *)&pCurrent->aRamps[iRamp] + iField * 2, 2);
      iLength += 6;
    }
  for (int iCar = 0; iCar < MAX_CARS; ++iCar) {
    uint32 uiMask = 0;
    int iMaskOffset = iLength;
    for (int iField = 0; iField < 31; ++iField)
      if (memcmp((const uint8 *)&pCurrent->aCars[iCar] + aCarFields[iField].unOffset,
                 (const uint8 *)&pBase->aCars[iCar] + aCarFields[iField].unOffset, aCarFields[iField].bySize))
        uiMask |= 1u << iField;
    if (!uiMask)
      continue;
    header.unCarMask |= (uint16)(1u << iCar);
    iLength += 4;
    for (int iField = 0; iField < 31; ++iField)
      if (uiMask & (1u << iField)) {
        int iSize = aCarFields[iField].bySize;
        if (iLength + iSize > NET_MAX_PAYLOAD - 28)
          return 0; /* Caller sends a full snapshot when a delta is larger. */
        NetWireScalar(abEncoded + iLength, (const uint8 *)&pCurrent->aCars[iCar] + aCarFields[iField].unOffset, iSize);
        iLength += iSize;
      }
    NetWireScalar(abEncoded + iMaskOffset, &uiMask, 4);
  }
  for (int iField = 0; iField < 4; ++iField)
    NetWireScalar(abEncoded + 4 * iField, (const uint8 *)&header + 4 * iField, 4);
  NetWireContext(abEncoded + 16, (const uint8 *)&header.context);
  memcpy(abEncoded + 28, &header.byNumCars, 4);
  NetWireScalar(abEncoded + 32, &header.unCarMask, 2);
  NetWireScalar(abEncoded + 34, &header.unRampMask, 2);
  if (iLength > iCapacity)
    return 0;
  memcpy(pBytes, abEncoded, (size_t)iLength);
  return iLength;
}
int NetSnapshotDecodeDelta(const tNetSnapshot *pBase, const uint8 *pBytes, int iLength, tNetSnapshot *pResult)
{
  tNetSnapshot snapshot;
  tNetSnapshotDeltaHeader header;
  int iOffset = sizeof(header);
  if (!pBytes || !pResult || !NetSnapshotValid(pBase) || iLength < iOffset || iLength > NET_MAX_PAYLOAD - 28)
    return 0;
  for (int iField = 0; iField < 4; ++iField)
    NetWireScalar((uint8 *)&header + 4 * iField, pBytes + 4 * iField, 4);
  NetWireContext((uint8 *)&header.context, pBytes + 16);
  memcpy(&header.byNumCars, pBytes + 28, 4);
  NetWireScalar((uint8 *)&header.unCarMask, pBytes + 32, 2);
  NetWireScalar((uint8 *)&header.unRampMask, pBytes + 34, 2);
  if (header.uiBaseTick != pBase->uiTick || header.unRampMask & 0xff00u)
    return 0;
  snapshot = *pBase;
  snapshot.uiTick = header.uiTick; snapshot.uiLastEventSeq = header.uiLastEventSeq;
  snapshot.uiRandomState = header.uiRandomState; snapshot.context = header.context;
  snapshot.byNumCars = header.byNumCars; snapshot.byNumRamps = header.byNumRamps;
  snapshot.byRaceState = header.byRaceState; snapshot.byPaused = header.byPaused;
  memset(snapshot.byPad, 0, sizeof(snapshot.byPad));
  for (int iRamp = 0; iRamp < NET_MAX_RAMPS; ++iRamp)
    if (header.unRampMask & (1u << iRamp)) {
      if (iOffset + 6 > iLength)
        return 0;
      for (int iField = 0; iField < 3; ++iField)
        NetWireScalar((uint8 *)&snapshot.aRamps[iRamp] + 2 * iField, pBytes + iOffset + 2 * iField, 2);
      iOffset += 6;
    }
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    if (header.unCarMask & (1u << iCar)) {
      uint32 uiMask;
      if (iOffset + 4 > iLength)
        return 0;
      NetWireScalar((uint8 *)&uiMask, pBytes + iOffset, 4);
      iOffset += 4;
      if (uiMask & 0x80000000u)
        return 0;
      for (int iField = 0; iField < 31; ++iField)
        if (uiMask & (1u << iField)) {
          int iSize = aCarFields[iField].bySize;
          if (iOffset + iSize > iLength)
            return 0;
          NetWireScalar((uint8 *)&snapshot.aCars[iCar] + aCarFields[iField].unOffset, pBytes + iOffset, iSize);
          iOffset += iSize;
        }
    }
  if (iOffset != iLength || !NetSnapshotValid(&snapshot))
    return 0;
  *pResult = snapshot;
  return 1;
}
