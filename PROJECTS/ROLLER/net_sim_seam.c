#include "net_sim_seam.h"
#include "3d.h"
#include "control.h"
#include "function.h"
#include "loadtrak.h"
#include "roller.h"
#include "transfrm.h"
#include <math.h>
#include <string.h>

int net_sim_replaying;
int net_sim_authority = NET_AUTHORITY_LOCAL;
uint8 net_puppet_car[MAX_CARS];
void (*net_sim_puppet_hook)(void);
static tNetCorrection s_aRenderCorrections[MAX_CARS];

void NetSimCaptureContext(tNetSimTickContext *pContext)
{
  memset(pContext, 0, sizeof(*pContext));
  pContext->iGameFrame = game_frame;
  pContext->iCountdown = countdown;
  pContext->iRaceStarted = race_started;
  pContext->iRacing = racing;
  pContext->iWarpAngle = warp_angle;
  pContext->iView0Cnt = view0_cnt;
  pContext->iView1Cnt = view1_cnt;
  pContext->iQuitCount = Quit_Count;
  pContext->iNearcarcheck = nearcarcheck;
  pContext->iUpdates = updates;
  pContext->iReadptr = readptr;
  pContext->iWriteptr = writeptr;
  pContext->iStartRace = start_race;
  pContext->iFatality = Fatality;
  pContext->iFatalityCount = Fatality_Count;
  pContext->iDestroyed = Destroyed;
  pContext->iVictim = Victim;
  pContext->iCheatControl = cheat_control;
  pContext->iFudgeWait = fudge_wait;
  pContext->iFinishers = finishers;
  pContext->iHumanFinishers = human_finishers;
  pContext->iLastsample = lastsample;
  pContext->iGameOvers = game_overs;
  pContext->iDisableMessages = disable_messages;
  memcpy(pContext->aiCarOrder, carorder, sizeof(carorder));
  memcpy(pContext->aiFinished, finished_car, sizeof(finished_car));
  memcpy(pContext->aiNearCall, nearcall, sizeof(nearcall));
  memcpy(pContext->afRecordLaps, RecordLaps, sizeof(RecordLaps));
  memcpy(pContext->aiRecordCars, RecordCars, sizeof(RecordCars));
  memcpy(pContext->aiRecordKills, RecordKills, sizeof(RecordKills));
  memcpy(pContext->aszRecordNames, RecordNames, sizeof(RecordNames));
  memcpy(pContext->aSpray, CarSpray, sizeof(CarSpray));
  memcpy(pContext->aLights, SLight, sizeof(SLight));
  pContext->iReadsample = readsample;
  pContext->iWritesample = writesample;
  pContext->iChampCount = champ_count;
  memcpy(pContext->aSpeech, speechinfo, sizeof(speechinfo));
  memcpy(pContext->aiGameCount, game_count, sizeof(game_count));
  memcpy(pContext->aiSubOn, sub_on, sizeof(sub_on));
  memcpy(pContext->afGameScale, game_scale, sizeof(game_scale));
  memcpy(pContext->afPullZ, PULLZ, sizeof(PULLZ));
  pContext->uiRandomState = ROLLERrandStateGet();
  pContext->ullRandomDraws = ROLLERrandDrawCountGet();
}

void NetSimRestoreContext(const tNetSimTickContext *pContext)
{
  game_frame = pContext->iGameFrame;
  countdown = pContext->iCountdown;
  race_started = pContext->iRaceStarted;
  racing = pContext->iRacing;
  warp_angle = pContext->iWarpAngle;
  view0_cnt = pContext->iView0Cnt;
  view1_cnt = pContext->iView1Cnt;
  Quit_Count = pContext->iQuitCount;
  nearcarcheck = pContext->iNearcarcheck;
  updates = pContext->iUpdates;
  readptr = pContext->iReadptr;
  writeptr = pContext->iWriteptr;
  start_race = pContext->iStartRace;
  Fatality = pContext->iFatality;
  Fatality_Count = pContext->iFatalityCount;
  Destroyed = pContext->iDestroyed;
  Victim = pContext->iVictim;
  cheat_control = pContext->iCheatControl;
  fudge_wait = pContext->iFudgeWait;
  finishers = pContext->iFinishers;
  human_finishers = pContext->iHumanFinishers;
  lastsample = pContext->iLastsample;
  game_overs = pContext->iGameOvers;
  disable_messages = pContext->iDisableMessages;
  memcpy(carorder, pContext->aiCarOrder, sizeof(carorder));
  memcpy(finished_car, pContext->aiFinished, sizeof(finished_car));
  memcpy(nearcall, pContext->aiNearCall, sizeof(nearcall));
  memcpy(RecordLaps, pContext->afRecordLaps, sizeof(RecordLaps));
  memcpy(RecordCars, pContext->aiRecordCars, sizeof(RecordCars));
  memcpy(RecordKills, pContext->aiRecordKills, sizeof(RecordKills));
  memcpy(RecordNames, pContext->aszRecordNames, sizeof(RecordNames));
  memcpy(CarSpray, pContext->aSpray, sizeof(CarSpray));
  memcpy(SLight, pContext->aLights, sizeof(SLight));
  readsample = pContext->iReadsample;
  writesample = pContext->iWritesample;
  champ_count = pContext->iChampCount;
  memcpy(speechinfo, pContext->aSpeech, sizeof(speechinfo));
  memcpy(game_count, pContext->aiGameCount, sizeof(game_count));
  memcpy(sub_on, pContext->aiSubOn, sizeof(sub_on));
  memcpy(game_scale, pContext->afGameScale, sizeof(game_scale));
  memcpy(PULLZ, pContext->afPullZ, sizeof(PULLZ));
  ROLLERrandStateSet(pContext->uiRandomState);
  ROLLERrandDrawCountSet(pContext->ullRandomDraws);
}

int NetSimRestoreInputRing(const tNetInputSlot *pSlots, uint32 uiFirstTick,
                           int iCount, int iReadPtr)
{
  if (!pSlots || iCount < 1 || iCount > 256 || iReadPtr < 0 || iReadPtr >= 512)
    return 0;
  for (int iSlot = 0; iSlot < iCount; ++iSlot)
    if (pSlots[iSlot].uiTick != uiFirstTick + (uint32)iSlot)
      return 0;
  /* Slot zero is the previous tick; iReadPtr consumes slot one. */
  for (int iSlot = 0; iSlot < iCount; ++iSlot)
    memcpy(copy_multiple[(iReadPtr - 1 + iSlot) & 511], pSlots[iSlot].aInputs,
           sizeof(copy_multiple[0]));
  readptr = iReadPtr;
  writeptr = (iReadPtr + iCount - 1) & 511;
  return 1;
}

int NetSimWriteTickInputs(const tCopyData *pInputs, int iNumCars)
{
  if (!pInputs || iNumCars < 1 || iNumCars > MAX_CARS ||
      readptr < 0 || readptr >= 512 || writeptr < 0 || writeptr >= 512 ||
      ((writeptr + 1) & 511) == readptr)
    return 0;
  memset(copy_multiple[writeptr], 0, sizeof(copy_multiple[0]));
  memcpy(copy_multiple[writeptr], pInputs, (size_t)iNumCars * sizeof(*pInputs));
  writeptr = (writeptr + 1) & 511;
  return 1;
}

void NetSimSaveCar(int iCar, tCar *pSaved)
{
  if (iCar >= 0 && iCar < MAX_CARS && pSaved)
    memcpy(pSaved, &Car[iCar], sizeof(*pSaved));
}
void NetSimRestoreCar(int iCar, const tCar *pSaved)
{
  if (iCar >= 0 && iCar < MAX_CARS && pSaved)
    memcpy(&Car[iCar], pSaved, sizeof(*pSaved));
}

void NetSimSaveRamps(tNetRampState *pStates)
{
  memset(pStates, 0, NET_MAX_RAMPS * sizeof(*pStates));
  for (int iRamp = 0; iRamp < totalramps && iRamp < NET_MAX_RAMPS; ++iRamp)
    if (ramp[iRamp]) {
      pStates[iRamp].nTickStartIdx = (int16)ramp[iRamp]->iTickStartIdx;
      pStates[iRamp].nTimingGroup2 = (int16)ramp[iRamp]->iTimingGroup2;
      pStates[iRamp].nRunningTimer = (int16)ramp[iRamp]->iRunningTimer;
    }
}
int NetSimSetRampState(int iRamp, const tNetRampState *pState)
{
  tStuntData *pRamp;
  if (!pState || iRamp < 0 || iRamp >= totalramps || iRamp >= NET_MAX_RAMPS || !ramp[iRamp])
    return 0;
  pRamp = ramp[iRamp];
  if (pState->nTickStartIdx < 0 || pState->nTickStartIdx >= pRamp->iNumTicks ||
      (pState->nTimingGroup2 != -1 && pState->nTimingGroup2 != 1) || pState->nRunningTimer < 0)
    return 0;
  pRamp->iTickStartIdx = pState->nTickStartIdx;
  pRamp->iTimingGroup2 = pState->nTimingGroup2;
  pRamp->iRunningTimer = pState->nRunningTimer;
  rebuildrampgeometry(pRamp);
  return 1;
}
int NetSimRestoreRamps(const tNetRampState *pStates)
{
  if (!pStates || totalramps < 0 || totalramps > NET_MAX_RAMPS)
    return 0;
  for (int iRamp = 0; iRamp < totalramps; ++iRamp)
    if (!NetSimSetRampState(iRamp, &pStates[iRamp]))
      return 0;
  return 1;
}
int NetSimAdvanceRampStateCopy(int iRamp, tNetRampState *pState, int iTicks)
{
  int iTick, iGroup, iTimer;
  if (!pState || iRamp < 0 || iRamp >= totalramps || iRamp >= NET_MAX_RAMPS ||
      !ramp[iRamp] || iTicks < 0 || iTicks > 10000)
    return 0;
  iTick = pState->nTickStartIdx;
  iGroup = pState->nTimingGroup2;
  iTimer = pState->nRunningTimer;
  if (iTick < 0 || iTick >= ramp[iRamp]->iNumTicks || iTimer < 0 || (iGroup != -1 && iGroup != 1))
    return 0;
  for (int iStep = 0; iStep < iTicks; ++iStep)
    advancerampstate(&iTick, &iGroup, &iTimer, ramp[iRamp]);
  pState->nTickStartIdx = (int16)iTick;
  pState->nTimingGroup2 = (int16)iGroup;
  pState->nRunningTimer = (int16)iTimer;
  return 1;
}

void NetSimSetPuppet(int iCar, int iPuppet)
{
  if (iCar >= 0 && iCar < MAX_CARS)
    net_puppet_car[iCar] = iPuppet != 0;
}
int NetSimIsPuppet(const tCar *pCar)
{
  return pCar && pCar->iDriverIdx >= 0 && pCar->iDriverIdx < MAX_CARS &&
         net_puppet_car[pCar->iDriverIdx];
}
void NetSimCanonicaliseInput(tCarInputData *pInput)
{
  pInput->unFlags &= BUTTON_FLAG_ACCEL | BUTTON_FLAG_BRAKE | BUTTON_FLAG_UPGEAR |
                     BUTTON_FLAG_DOWNGEAR | BUTTON_FLAG_SPECIAL | BUTTON_FLAG_PHONE_THROTTLE;
}

int NetSimLegacyToWorld(const tCar *pCar, tNetWorldPose *pPose)
{
  int iYaw, iPitch, iRoll, iActualYaw;
  if (!pCar || !pPose || pCar->nCurrChunk < -1 || pCar->nCurrChunk >= TRAK_LEN)
    return 0;
  memset(pPose, 0, sizeof(*pPose));
  pPose->position = pCar->pos;
  iYaw = pCar->nYaw; iPitch = pCar->nPitch; iRoll = pCar->nRoll; iActualYaw = pCar->nActualYaw;
  if (pCar->nCurrChunk >= 0) {
    const tData *pData = &localdata[pCar->nCurrChunk];
    const tVec3 *pLocal = &pCar->pos;
    pPose->position.fX = pData->pointAy[0].fY * pLocal->fY + pData->pointAy[0].fX * pLocal->fX + pData->pointAy[0].fZ * pLocal->fZ - pData->pointAy[3].fX;
    pPose->position.fY = pData->pointAy[1].fY * pLocal->fY + pData->pointAy[1].fX * pLocal->fX + pData->pointAy[1].fZ * pLocal->fZ - pData->pointAy[3].fY;
    pPose->position.fZ = pData->pointAy[2].fY * pLocal->fY + pData->pointAy[2].fX * pLocal->fX + pData->pointAy[2].fZ * pLocal->fZ - pData->pointAy[3].fZ;
    getworldangles(pCar->nActualYaw, pCar->nPitch, pCar->nRoll, pCar->nCurrChunk, &iActualYaw, &iPitch, &iRoll);
    getworldangles(pCar->nYaw, pCar->nPitch, pCar->nRoll, pCar->nCurrChunk, &iYaw, &iPitch, &iRoll);
  }
  pPose->nYaw = (int16)iYaw; pPose->nPitch = (int16)iPitch;
  pPose->nRoll = (int16)iRoll; pPose->nActualYaw = (int16)iActualYaw;
  return 1;
}
int NetSimWorldToLegacy(const tNetWorldPose *pPose, tCar *pCar)
{
  int iYaw, iPitch, iRoll, iActualYaw;
  if (!pPose || !pCar || pCar->nCurrChunk < -1 || pCar->nCurrChunk >= TRAK_LEN)
    return 0;
  iYaw = pPose->nYaw; iPitch = pPose->nPitch; iRoll = pPose->nRoll; iActualYaw = pPose->nActualYaw;
  pCar->pos = pPose->position;
  if (pCar->nCurrChunk >= 0) {
    const tData *pData = &localdata[pCar->nCurrChunk];
    double dX = pPose->position.fX + pData->pointAy[3].fX;
    double dY = pPose->position.fY + pData->pointAy[3].fY;
    double dZ = pPose->position.fZ + pData->pointAy[3].fZ;
    pCar->pos.fX = (float)(pData->pointAy[1].fX * dY + pData->pointAy[0].fX * dX + pData->pointAy[2].fX * dZ);
    pCar->pos.fY = (float)(pData->pointAy[0].fY * dX + pData->pointAy[1].fY * dY + pData->pointAy[2].fY * dZ);
    pCar->pos.fZ = (float)(pData->pointAy[0].fZ * dX + pData->pointAy[1].fZ * dY + pData->pointAy[2].fZ * dZ);
    getlocalangles(pPose->nActualYaw, pPose->nPitch, pPose->nRoll, pCar->nCurrChunk, &iActualYaw, &iPitch, &iRoll);
    getlocalangles(pPose->nYaw, pPose->nPitch, pPose->nRoll, pCar->nCurrChunk, &iYaw, &iPitch, &iRoll);
  }
  pCar->nYaw = (int16)iYaw; pCar->nPitch = (int16)iPitch;
  pCar->nRoll = (int16)iRoll; pCar->nActualYaw = iActualYaw;
  return 1;
}
void NetSimRehomeChunk(tCar *pCar)
{
  if (pCar->nCurrChunk >= 0 && pCar->nCurrChunk < TRAK_LEN &&
      fabsf(pCar->pos.fX) > localdata[pCar->nCurrChunk].fTrackHalfLength)
    scansection(pCar);
}

void NetSimBootstrapContext(const tNetSimContextWire *pWire,
                            uint32 uiRandomState, int iRingPosition,
                            tNetSimTickContext *pContext)
{
  if (!pWire || !pContext || iRingPosition < 0 || iRingPosition >= 512)
    return;
  /* Recovery deliberately starts without local history.  E0's context
     audit left only these movement-affecting values on the wire; the rest
     take the same zero values as a fresh race before its first live tick. */
  memset(pContext, 0, sizeof(*pContext));
  pContext->iGameFrame = pWire->iGameFrame;
  pContext->iCountdown = pWire->iCountdown;
  pContext->iRaceStarted = pWire->byRaceStarted;
  pContext->iRacing = pWire->byRacing;
  pContext->iWarpAngle = pWire->nWarpAngle;
  pContext->iReadptr = iRingPosition;
  pContext->iWriteptr = iRingPosition;
  pContext->uiRandomState = uiRandomState;
}

void NetSimClearRenderCorrection(int iCar)
{
  if (iCar >= 0 && iCar < MAX_CARS)
    memset(&s_aRenderCorrections[iCar], 0,
           sizeof(s_aRenderCorrections[iCar]));
}

void NetSimSetRenderCorrection(int iCar, const tNetWorldPose *pBefore,
                               const tNetWorldPose *pAfter, int iTicks)
{
  tNetCorrection *pCorrection;
  int iYaw;
  if (iCar < 0 || iCar >= MAX_CARS || !pBefore || !pAfter || iTicks < 1)
    return;
  pCorrection = &s_aRenderCorrections[iCar];
  pCorrection->worldPosOffset.fX =
      pBefore->position.fX - pAfter->position.fX;
  pCorrection->worldPosOffset.fY =
      pBefore->position.fY - pAfter->position.fY;
  pCorrection->worldPosOffset.fZ =
      pBefore->position.fZ - pAfter->position.fZ;
  iYaw = ((pBefore->nYaw - pAfter->nYaw + 8192) & 16383) - 8192;
  pCorrection->nYawOffset = (int16)iYaw;
  pCorrection->iTicksRemaining = iTicks;
}

void NetSimAdvanceRenderCorrections(void)
{
  for (int iCar = 0; iCar < MAX_CARS; ++iCar) {
    tNetCorrection *pCorrection = &s_aRenderCorrections[iCar];
    float fScale;
    if (pCorrection->iTicksRemaining <= 0)
      continue;
    if (pCorrection->iTicksRemaining == 1) {
      NetSimClearRenderCorrection(iCar);
      continue;
    }
    fScale = (float)(pCorrection->iTicksRemaining - 1) /
             (float)pCorrection->iTicksRemaining;
    pCorrection->worldPosOffset.fX *= fScale;
    pCorrection->worldPosOffset.fY *= fScale;
    pCorrection->worldPosOffset.fZ *= fScale;
    pCorrection->nYawOffset = (int16)lroundf(
        (float)pCorrection->nYawOffset * fScale);
    --pCorrection->iTicksRemaining;
  }
}

int NetSimApplyRenderCorrection(int iCar, tVec3 *pPosition, int *piYaw,
                                int *piPitch, int *piRoll)
{
  const tNetCorrection *pCorrection;
  tNetWorldPose pose;
  tCar renderCar;
  if (iCar < 0 || iCar >= numcars || !pPosition || !piYaw || !piPitch ||
      !piRoll)
    return 0;
  pCorrection = &s_aRenderCorrections[iCar];
  if (pCorrection->iTicksRemaining <= 0)
    return 1;
  renderCar = Car[iCar];
  if (!NetSimLegacyToWorld(&renderCar, &pose))
    return 0;
  pose.position.fX += pCorrection->worldPosOffset.fX;
  pose.position.fY += pCorrection->worldPosOffset.fY;
  pose.position.fZ += pCorrection->worldPosOffset.fZ;
  pose.nYaw = (int16)((pose.nYaw + pCorrection->nYawOffset) & 16383);
  if (!NetSimWorldToLegacy(&pose, &renderCar))
    return 0;
  *pPosition = renderCar.pos;
  *piYaw = renderCar.nYaw;
  *piPitch = renderCar.nPitch;
  *piRoll = renderCar.nRoll;
  return 1;
}

int NetSimRenderCorrectionAt(int iCar, tNetCorrection *pCorrection)
{
  if (iCar < 0 || iCar >= MAX_CARS || !pCorrection)
    return 0;
  *pCorrection = s_aRenderCorrections[iCar];
  return 1;
}
