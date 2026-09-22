#include "net_client.h"
#include "net_event.h"
#include "net_input.h"
#include "net_race_state.h"
#include "net_snapshot.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"
#include "loadtrak.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Host clock estimator: each observation moves the estimate this fraction
   of the way to the sample. */
#define NET_CLIENT_CLOCK_GAIN 0.1
/* Lead controller: tick scale change per tick of timeline error. */
#define NET_CLIENT_LEAD_GAIN 0.05
/* The integer lead only drops once the formula is this far below it. */
#define NET_CLIENT_LEAD_HYSTERESIS 1.25
/* Feedback-driven lead bias: late inputs while on target raise it by one
   tick, and it decays one tick per quiet interval. */
#define NET_CLIENT_LEAD_BIAS_MAX 4
#define NET_CLIENT_LEAD_BIAS_DECAY_MS 10000u
#define NET_CLIENT_ON_TARGET_TICKS 1.0f
#define NET_CLIENT_FRAME_GAIN 0.1f
#define NET_CLIENT_COMMIT_BUFFER NET_RELIABLE_QUEUE

typedef struct
{
  uint32 uiTick;
  tCarInputData aInputs[NET_INPUT_MAX_LOCAL_PLAYERS];
  uint8 byValid;
} tNetClientInputSlot;

typedef struct
{
  uint32 uiTick;
  tNetCarFullState state;
  uint8 byValid;
} tNetClientPredictionSlot;

typedef struct
{
  uint32 uiTick;
  tNetSimTickContext context;
  uint8 byValid;
} tNetClientContextSlot;

typedef struct
{
  tNetSnapshot snapshot;
  uint8 byValid;
} tNetClientSnapshotSlot;

typedef struct
{
  uint32 uiTick;
  tNetCarExtra aExtras[NET_INPUT_MAX_LOCAL_PLAYERS];
  uint8 byValid;
} tNetClientOwnSlot;

typedef struct
{
  uint32 uiEventSeq, uiTick;
  uint8 byMessageType, byWorldCount, byValid;
  tNetEvent event;
  tNetWorldChangeEntry aWorld[NET_WORLD_CHANGE_MAX_ENTRIES];
} tNetClientCommitSlot;

struct tNetClient
{
  tNetSessionClient *pSession;
  tNetLobbyClient *pLobby;
  tNetSessionConfig config;
  double dTicksPerMs;
  int iRetentionTicks;
  uint8 byRacing, byJoined, byHasEstimate, byHasFeedback, byHasSnapshot;
  uint8 byHasFrame, byGroupCount, byHasReconciled, byHasAuthoritative;
  uint8 byDeferredCounted, byAboveBudget, byBelowBudget, byExitReady;
  uint8 byResultsPublished;
  uint8 abyGroup[NET_INPUT_MAX_LOCAL_PLAYERS];
  /* Timeline (4.4).  uiClientTick is the newest simulated tick; it moves by
     exactly one per NetClientTick. */
  uint32 uiStartTick, uiClientTick, uiRampTick;
  uint64 ullRaceBaseMs, ullLastPumpMs, ullLastBiasMs, ullNewestSnapshotMs;
  double dAccumTicks;
  /* Host clock relative to uiStartTick: (now - ullRaceBaseMs) * dTicksPerMs
     + dHostOffsetTicks. */
  double dHostOffsetTicks;
  int iLeadBase;
  int iReplayBudgetTicks, iReplayPressure, iPredictionMode;
  uint32 uiFeedbackHostTick;
  uint32 uiLastReconciledTick, uiLastAuthoritativeTick, uiDeferredTick;
  uint32 uiLastAppliedEventSeq, uiCommitWatermark;
  uint64 ullAboveBudgetSinceMs, ullBelowBudgetSinceMs;
  tNetRaceLifecycle lifecycle;
  int iResultFinishers, iResultHumanFinishers;
  uint8 abyFinishCommitted[MAX_CARS], abyDestroyedCommitted[MAX_CARS];
  uint8 abyFinishOwner[MAX_CARS], abyFinishPosition[MAX_CARS];
  uint8 abyLapCommitted[MAX_CARS], abyKillCommitted[MAX_CARS];
  uint8 abyCommittedLap[MAX_CARS], abyCommittedKills[MAX_CARS];
  tNetClientStats stats;
  tNetClientInputSlot aInputs[NET_CLIENT_HISTORY];
  tNetClientPredictionSlot aPrediction[NET_INPUT_MAX_LOCAL_PLAYERS][NET_CLIENT_HISTORY];
  tNetClientContextSlot aContext[NET_CLIENT_HISTORY];
  tNetClientSnapshotSlot aSnapshots[NET_CLIENT_SNAPSHOT_BUFFER];
  tNetClientOwnSlot aOwn[NET_CLIENT_SNAPSHOT_BUFFER];
  tNetClientCommitSlot aCommits[NET_CLIENT_COMMIT_BUFFER];
};

/* D13 permits one simulation world per process, so exactly one racing client
   can own the process-global in-tick puppet hook. */
static tNetClient *s_pPuppetClient;

_Static_assert(NET_CLIENT_SNAPSHOT_BUFFER > (NET_SNAPSHOT_RETENTION_MS * 100 + 999) / 1000,
               "the snapshot buffer must hold 600 ms at 100 Hz without aliasing");
_Static_assert(NET_INPUT_REDUNDANCY < NET_CLIENT_HISTORY,
               "a batch must come from the input history");
_Static_assert(NET_MAX_REPLAY_TICKS ==
                   (NET_CLIENT_REPLAY_BUDGET_MS * 36) / 1000,
               "Contract B's 36 Hz horizon must match the session budget");
_Static_assert(NET_CLIENT_COMMIT_BUFFER >= NET_RELIABLE_QUEUE,
               "the client must retain a full reliable commit window");
_Static_assert(sizeof(TrakColour[0][0]) == sizeof(uint32),
               "world-change colours are complete 32-bit words");

static void NetClientPublishReconciliationStats(const tNetClient *pClient);
static void NetClientEnterDelayed(tNetClient *pClient);

static uint64 NetClientNowMs(const tNetClient *pClient)
{
  return NetConnectionNowMs(NetSessionClientConnection(pClient->pSession));
}

/* Signed distance of uiTick from the start tick. */
static double NetClientRelative(const tNetClient *pClient, uint32 uiTick)
{
  return (double)(int32)(uiTick - pClient->uiStartTick);
}

static int NetClientStarted(const tNetClient *pClient, uint32 uiTick)
{
  return (int32)(uiTick - pClient->uiStartTick) >= 0;
}

static double NetClientHostTickAt(const tNetClient *pClient, uint64 ullNowMs)
{
  return (double)(ullNowMs - pClient->ullRaceBaseMs) * pClient->dTicksPerMs +
         pClient->dHostOffsetTicks;
}

/* dHostTick (relative) was the host's clock when it sent what arrived now.
   It is half a round trip older than the host's clock at arrival. */
static void NetClientObserveHost(tNetClient *pClient, double dHostTick,
                                 uint64 ullNowMs)
{
  tNetConnection *pConnection = NetSessionClientConnection(pClient->pSession);
  double dSample = dHostTick +
      NetConnectionRttMs(pConnection) * 0.5 * pClient->dTicksPerMs -
      (double)(ullNowMs - pClient->ullRaceBaseMs) * pClient->dTicksPerMs;
  if (!pClient->byHasEstimate) {
    pClient->dHostOffsetTicks = dSample;
    pClient->byHasEstimate = 1;
    return;
  }
  pClient->dHostOffsetTicks += (dSample - pClient->dHostOffsetTicks) *
                               NET_CLIENT_CLOCK_GAIN;
}

static int NetClientInWindow(const tNetClient *pClient, uint32 uiTick,
                             uint32 uiNewest, int iWindow)
{
  return (int32)(uiNewest - uiTick) >= 0 &&
         (int32)(uiNewest - uiTick) < iWindow;
}

static int NetClientMember(const tNetClient *pClient, uint8 byCar)
{
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    if (pClient->abyGroup[iMember] == byCar)
      return iMember;
  return -1;
}

static int NetClientAngleDifference(int16 nA, int16 nB)
{
  int iDifference = ((nA - nB + 8192) & 16383) - 8192;
  return iDifference < 0 ? -iDifference : iDifference;
}

/* The comparison contains only E0-S4's movement assignment.  Positions are
   world-space track units, angular fields use the 14-bit circle, and speed
   tolerances are legacy speed units (4.5 and D19). */
static int NetClientMovementWithin(const tNetCarFullState *pPredicted,
                                   const tNetCarFullState *pHost)
{
  const tNetCarState *pA = &pPredicted->state;
  const tNetCarState *pB = &pHost->state;
  const tNetCarExtra *pEA = &pPredicted->extra;
  const tNetCarExtra *pEB = &pHost->extra;
  float fDx = pA->fWorldPosX - pB->fWorldPosX;
  float fDy = pA->fWorldPosY - pB->fWorldPosY;
  float fDz = pA->fWorldPosZ - pB->fWorldPosZ;
  if (fDx * fDx + fDy * fDy + fDz * fDz >
          NET_CLIENT_POSITION_TOLERANCE * NET_CLIENT_POSITION_TOLERANCE ||
      NetClientAngleDifference(pA->nWorldYaw, pB->nWorldYaw) >
          NET_CLIENT_ANGLE_TOLERANCE ||
      NetClientAngleDifference(pA->nWorldPitch, pB->nWorldPitch) >
          NET_CLIENT_ANGLE_TOLERANCE ||
      NetClientAngleDifference(pA->nWorldRoll, pB->nWorldRoll) >
          NET_CLIENT_ANGLE_TOLERANCE ||
      NetClientAngleDifference(pA->nActualYaw, pB->nActualYaw) >
          NET_CLIENT_ANGLE_TOLERANCE ||
      fabsf(pA->fFinalSpeed - pB->fFinalSpeed) >
          NET_CLIENT_SPEED_TOLERANCE ||
      fabsf(pA->fHorizontalSpeed - pB->fHorizontalSpeed) >
          NET_CLIENT_SPEED_TOLERANCE ||
      fabsf(pA->fVelX - pB->fVelX) > NET_CLIENT_SPEED_TOLERANCE ||
      fabsf(pA->fVelY - pB->fVelY) > NET_CLIENT_SPEED_TOLERANCE ||
      fabsf(pA->fVelZ - pB->fVelZ) > NET_CLIENT_SPEED_TOLERANCE)
    return 0;
  if (pA->nCurrChunk != pB->nCurrChunk ||
      pA->nReferenceChunk != pB->nReferenceChunk ||
      pA->nLastValidChunk != pB->nLastValidChunk ||
      pA->nJumpMomentum != pB->nJumpMomentum ||
      pA->byGearAyMax != pB->byGearAyMax ||
      pA->byControlType != pB->byControlType ||
      pEA->fBaseSpeed != pEB->fBaseSpeed ||
      pEA->fSpeedOverflow != pEB->fSpeedOverflow ||
      pEA->fPower != pEB->fPower || pEA->fRPMRatio != pEB->fRPMRatio ||
      /* Exact health is both host-owned and branch-relevant (4.15). */
      pEA->fHealth != pEB->fHealth ||
      pEA->iRollMomentum != pEB->iRollMomentum ||
      pEA->iRollMotion != pEB->iRollMotion ||
      pEA->iPitchMotion != pEB->iPitchMotion ||
      pEA->iYawMotion != pEB->iYawMotion ||
      pEA->iEngineState != pEB->iEngineState ||
      pEA->iSteeringInput != pEB->iSteeringInput ||
      pEA->iBankingSteerOffset != pEB->iBankingSteerOffset ||
      pEA->nTargetChunk != pEB->nTargetChunk ||
      pEA->nChangeMateCooldown != pEB->nChangeMateCooldown ||
      pEA->byEngineStartTimer != pEB->byEngineStartTimer ||
      pEA->byThrottlePressed != pEB->byThrottlePressed ||
      pEA->byAccelerating != pEB->byAccelerating ||
      pEA->byAIThrottleControl != pEB->byAIThrottleControl ||
      pEA->byPitLaneActiveFlag != pEB->byPitLaneActiveFlag ||
      pEA->byCollisionTimer != pEB->byCollisionTimer)
    return 0;
  return 1;
}

static int NetClientSnapshotMatchesWorld(const tNetSnapshot *pSnapshot)
{
  if (!pSnapshot || pSnapshot->byNumCars != numcars ||
      pSnapshot->byNumRamps != totalramps)
    return 0;
  for (int iCar = 0; iCar < pSnapshot->byNumCars; ++iCar) {
    const tNetCarState *pCar = &pSnapshot->aCars[iCar];
    if (pCar->nCurrChunk >= TRAK_LEN || pCar->nReferenceChunk >= TRAK_LEN ||
        pCar->nLastValidChunk >= TRAK_LEN)
      return 0;
  }
  /* Zero steps validates every timing field against the loaded ramp without
     touching either timing or geometry. */
  for (int iRamp = 0; iRamp < pSnapshot->byNumRamps; ++iRamp) {
    tNetRampState state = pSnapshot->aRamps[iRamp];
    if (!NetSimAdvanceRampStateCopy(iRamp, &state, 0))
      return 0;
  }
  return 1;
}

typedef struct
{
  const tNetSnapshot *pOlder, *pNewer, *pPrevious;
  double dFraction, dExtraTicks, dAppliedRelative;
  uint8 byExtrapolating, byUnderrun;
} tNetClientPuppetSample;

/* Locate snapshots by their signed distance from the race start, so the
   interpolation comparison remains valid across uint32 tick wrap. */
static int NetClientPuppetSampleAt(tNetClient *pClient, double dTargetRelative,
                                  tNetClientPuppetSample *pSample)
{
  const tNetSnapshot *pOldest = NULL, *pNewest = NULL;
  const tNetSnapshot *pBefore = NULL, *pAfter = NULL, *pPrevious = NULL;
  double dOldest = 0.0, dNewest = 0.0, dBefore = 0.0, dAfter = 0.0;
  double dPrevious = 0.0;
  if (!pClient->byHasSnapshot || !pSample)
    return 0;
  memset(pSample, 0, sizeof(*pSample));
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    double dTick;
    if (!pSlot->byValid ||
        !NetClientInWindow(pClient, pSlot->snapshot.uiTick,
                           pClient->stats.uiNewestSnapshotTick,
                           pClient->iRetentionTicks + 1))
      continue;
    dTick = NetClientRelative(pClient, pSlot->snapshot.uiTick);
    if (!pOldest || dTick < dOldest) {
      pOldest = &pSlot->snapshot;
      dOldest = dTick;
    }
    if (!pNewest || dTick > dNewest) {
      pNewest = &pSlot->snapshot;
      dNewest = dTick;
    }
    if (dTick <= dTargetRelative && (!pBefore || dTick > dBefore)) {
      pBefore = &pSlot->snapshot;
      dBefore = dTick;
    }
    if (dTick >= dTargetRelative && (!pAfter || dTick < dAfter)) {
      pAfter = &pSlot->snapshot;
      dAfter = dTick;
    }
  }
  if (!pOldest || !pNewest)
    return 0;
  if (!pBefore) {
    pSample->pOlder = pSample->pNewer = pOldest;
    pSample->dAppliedRelative = dOldest;
    pSample->dFraction = 1.0;
    pSample->byUnderrun = 1;
    return 1;
  }
  if (pAfter) {
    pSample->pOlder = pBefore;
    pSample->pNewer = pAfter;
    pSample->dAppliedRelative = dTargetRelative;
    pSample->dFraction = dAfter == dBefore ? 1.0 :
        (dTargetRelative - dBefore) / (dAfter - dBefore);
    return 1;
  }

  /* The render cursor passed the newest snapshot.  Estimate visual motion
     from the last two received world poses and hold after 100 ms.  This is
     presentation only; it never feeds a predicted or host-owned car. */
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    double dTick;
    if (!pSlot->byValid || pSlot->snapshot.uiTick == pNewest->uiTick ||
        !NetClientInWindow(pClient, pSlot->snapshot.uiTick,
                           pClient->stats.uiNewestSnapshotTick,
                           pClient->iRetentionTicks + 1))
      continue;
    dTick = NetClientRelative(pClient, pSlot->snapshot.uiTick);
    if (dTick < dNewest && (!pPrevious || dTick > dPrevious)) {
      pPrevious = &pSlot->snapshot;
      dPrevious = dTick;
    }
  }
  pSample->pOlder = pSample->pNewer = pNewest;
  pSample->pPrevious = pPrevious;
  pSample->dExtraTicks = fmin(dTargetRelative - dNewest,
      NET_CLIENT_EXTRAPOLATION_MAX_MS * pClient->dTicksPerMs);
  if (pSample->dExtraTicks < 0.0)
    pSample->dExtraTicks = 0.0;
  if (!pPrevious)
    pSample->dExtraTicks = 0.0;
  pSample->dAppliedRelative = dNewest + pSample->dExtraTicks;
  pSample->dFraction = 1.0;
  pSample->byExtrapolating = 1;
  return 1;
}

static int NetClientSamplePuppetCar(const tNetClient *pClient,
                                    const tNetClientPuppetSample *pSample,
                                    int iCar, tNetCarState *pState)
{
  if (!pSample->pOlder || !pSample->pNewer || iCar < 0 || iCar >= numcars ||
      !NetSnapshotInterpolate(&pSample->pOlder->aCars[iCar],
                              &pSample->pNewer->aCars[iCar],
                              (float)pSample->dFraction, pState))
    return 0;
  if (pSample->byExtrapolating && pSample->pPrevious) {
    double dNewest = NetClientRelative(pClient, pSample->pNewer->uiTick);
    double dPrevious = NetClientRelative(pClient, pSample->pPrevious->uiTick);
    double dSpan = dNewest - dPrevious;
    if (dSpan > 0.0) {
      const tNetCarState *pPrevious = &pSample->pPrevious->aCars[iCar];
      const tNetCarState *pNewest = &pSample->pNewer->aCars[iCar];
      float fScale = (float)(pSample->dExtraTicks / dSpan);
      pState->fWorldPosX += (pNewest->fWorldPosX - pPrevious->fWorldPosX) * fScale;
      pState->fWorldPosY += (pNewest->fWorldPosY - pPrevious->fWorldPosY) * fScale;
      pState->fWorldPosZ += (pNewest->fWorldPosZ - pPrevious->fWorldPosZ) * fScale;
    }
  }
  return 1;
}

static void NetClientPuppetHook(void)
{
  tNetClient *pClient = s_pPuppetClient;
  tNetClientPuppetSample sample;
  double dTargetRelative;
  int iApplied = 0;
  if (!pClient || !pClient->byRacing)
    return;
  ++pClient->stats.uiPuppetHookCalls;
  dTargetRelative = NetClientRelative(pClient, pClient->uiClientTick + 1u) -
      pClient->stats.iLeadTicks -
      pClient->stats.fInterpolationDelayMs * pClient->dTicksPerMs;
  pClient->stats.fRenderTick = (float)(pClient->uiStartTick + dTargetRelative);
  if (!NetClientPuppetSampleAt(pClient, dTargetRelative, &sample)) {
    pClient->stats.byStalled = 1;
    g_netStats.iStalled = 1;
    return;
  }
  if (sample.byUnderrun)
    ++pClient->stats.uiInterpolationUnderruns;
  if (sample.byExtrapolating)
    ++pClient->stats.uiInterpolationExtrapolations;
  pClient->stats.byStalled = sample.byExtrapolating;
  pClient->stats.fAppliedRenderTick =
      (float)(pClient->uiStartTick + sample.dAppliedRelative);
  g_netStats.iStalled = pClient->stats.byStalled;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    tNetCarState state;
    if (!net_puppet_car[iCar])
      continue;
    if (!NetClientSamplePuppetCar(pClient, &sample, iCar, &state) ||
        !NetSnapshotApplyPuppet(iCar, &state)) {
      ++pClient->stats.uiRejectedMessages;
      continue;
    }
    ++iApplied;
  }
  pClient->stats.uiPuppetApplications += (uint32)iApplied;
}

static int NetClientCorrectRamps(tNetClient *pClient)
{
  const tNetSnapshot *pNewest = NULL;
  tNetRampState aExpected[NET_MAX_RAMPS], aCurrent[NET_MAX_RAMPS];
  double dRampTick = NetClientRelative(pClient, pClient->uiRampTick);
  double dNewest = 0.0;
  int iAdvance;
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    double dTick;
    if (!pSlot->byValid ||
        !NetClientInWindow(pClient, pSlot->snapshot.uiTick,
                           pClient->stats.uiNewestSnapshotTick,
                           pClient->iRetentionTicks + 1))
      continue;
    dTick = NetClientRelative(pClient, pSlot->snapshot.uiTick);
    if (dTick <= dRampTick && (!pNewest || dTick > dNewest)) {
      pNewest = &pSlot->snapshot;
      dNewest = dTick;
    }
  }
  /* A newly released or stalled client may hold only future snapshots. */
  if (!pNewest)
    return 1;
  iAdvance = (int)(dRampTick - dNewest);
  memcpy(aExpected, pNewest->aRamps, sizeof(aExpected));
  for (int iRamp = 0; iRamp < totalramps; ++iRamp)
    if (!NetSimAdvanceRampStateCopy(iRamp, &aExpected[iRamp], iAdvance))
      return 0;
  NetSimSaveRamps(aCurrent);
  if (!memcmp(aCurrent, aExpected, (size_t)totalramps * sizeof(aCurrent[0])))
    return 1;
  if (!NetSimRestoreRamps(aExpected))
    return 0;
  ++pClient->stats.uiRampCorrections;
  ++g_netStats.iRampCorrections;
  return 1;
}

/* Return 1 for a complete validated pair, 0 while either unreliable message
   is missing, and -1 for a hostile full state. */
static int NetClientFullStatesAt(tNetClient *pClient, uint32 uiTick,
                                 tNetSnapshot *pSnapshot,
                                 tNetCarFullState *pStates)
{
  const tNetClientSnapshotSlot *pSnapshotSlot =
      &pClient->aSnapshots[uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  const tNetClientOwnSlot *pOwn =
      &pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  if (!pSnapshotSlot->byValid || pSnapshotSlot->snapshot.uiTick != uiTick ||
      !pOwn->byValid || pOwn->uiTick != uiTick)
    return 0;
  if (pSnapshotSlot->snapshot.byNumCars != numcars)
    return -1;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    int iCar = pClient->abyGroup[iMember];
    pStates[iMember].state = pSnapshotSlot->snapshot.aCars[iCar];
    pStates[iMember].extra = pOwn->aExtras[iMember];
    if (!NetSnapshotCarFullValid(iCar, &pStates[iMember]))
      return -1;
  }
  if (pSnapshot)
    *pSnapshot = pSnapshotSlot->snapshot;
  return 1;
}

static int NetClientApplyAuthoritativeAt(tNetClient *pClient, uint32 uiTick,
                                         int iForce)
{
  tNetCarFullState aStates[NET_INPUT_MAX_LOCAL_PLAYERS];
  int iResult;
  if (!iForce && pClient->byHasAuthoritative &&
      (int32)(uiTick - pClient->uiLastAuthoritativeTick) <= 0)
    return 1;
  iResult = NetClientFullStatesAt(pClient, uiTick, NULL, aStates);
  if (iResult != 1)
    return iResult;
  /* Validate the whole rollback group before writing any member (D23). */
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    if (!NetSnapshotCarFullValid(pClient->abyGroup[iMember],
                                 &aStates[iMember]))
      return -1;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    if (!NetSnapshotApplyAuthoritative(pClient->abyGroup[iMember],
                                       &aStates[iMember]))
      return -1;
  if (!pClient->byHasAuthoritative ||
      (int32)(uiTick - pClient->uiLastAuthoritativeTick) > 0) {
    pClient->uiLastAuthoritativeTick = uiTick;
    pClient->byHasAuthoritative = 1;
  }
  return 1;
}

static void NetClientApplyNewestAuthoritative(tNetClient *pClient)
{
  uint32 uiNewest = 0;
  int iFound = 0;
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    uint32 uiTick;
    if (!pSlot->byValid)
      continue;
    uiTick = pSlot->snapshot.uiTick;
    if ((int32)(pClient->uiClientTick - uiTick) < 0 ||
        (pClient->byHasAuthoritative &&
         (int32)(uiTick - pClient->uiLastAuthoritativeTick) <= 0) ||
        !pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER].byValid ||
        pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER].uiTick != uiTick)
      continue;
    if (!iFound || (int32)(uiTick - uiNewest) > 0) {
      uiNewest = uiTick;
      iFound = 1;
    }
  }
  if (iFound) {
    int iResult = NetClientApplyAuthoritativeAt(pClient, uiNewest, 0);
    if (iResult < 0) {
      pClient->aOwn[uiNewest % NET_CLIENT_SNAPSHOT_BUFFER].byValid = 0;
      ++pClient->stats.uiRejectedMessages;
    }
  }
}

static tNetClientCommitSlot *NetClientPrepareCommit(tNetClient *pClient,
                                                     uint32 uiEventSeq)
{
  tNetClientCommitSlot *pSlot;
  uint32 uiAhead = uiEventSeq - pClient->uiLastAppliedEventSeq;
  /* Range-check the hostile sequence before it selects a ring slot (D23).
     Reliable ordered delivery bounds a legitimate gap by its send window. */
  if ((int32)uiAhead <= 0) {
    ++pClient->stats.uiStaleMessages;
    return NULL;
  }
  if (uiAhead > NET_CLIENT_COMMIT_BUFFER) {
    ++pClient->stats.uiRejectedMessages;
    return NULL;
  }
  pSlot = &pClient->aCommits[uiEventSeq % NET_CLIENT_COMMIT_BUFFER];
  if (pSlot->byValid) {
    if (pSlot->uiEventSeq == uiEventSeq)
      ++pClient->stats.uiStaleMessages;
    else
      ++pClient->stats.uiRejectedMessages;
    return NULL;
  }
  memset(pSlot, 0, sizeof(*pSlot));
  pSlot->uiEventSeq = uiEventSeq;
  return pSlot;
}

static void NetClientReceiveEvent(tNetClient *pClient,
                                  const tNetMessage *pMessage)
{
  tNetClientCommitSlot *pSlot;
  tNetEvent event;
  if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
      !NetEventDecode(pMessage->abData, pMessage->unLength, numcars,
                      pClient->config.byMaxPlayers, &event)) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  pSlot = NetClientPrepareCommit(pClient, event.uiEventSeq);
  if (!pSlot)
    return;
  pSlot->uiTick = event.uiTick;
  pSlot->byMessageType = NET_MSG_EVENT;
  pSlot->event = event;
  pSlot->byValid = 1;
  ++pClient->stats.uiEvents;
}

static void NetClientReceiveWorldChange(tNetClient *pClient,
                                        const tNetMessage *pMessage)
{
  tNetClientCommitSlot *pSlot;
  tNetWorldChangeHeader header;
  tNetWorldChangeEntry aEntries[NET_WORLD_CHANGE_MAX_ENTRIES];
  if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
      !NetWorldChangeDecode(pMessage->abData, pMessage->unLength, TRAK_LEN,
                            &header, aEntries,
                            NET_WORLD_CHANGE_MAX_ENTRIES)) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  pSlot = NetClientPrepareCommit(pClient, header.uiEventSeq);
  if (!pSlot)
    return;
  pSlot->uiTick = header.uiTick;
  pSlot->byMessageType = NET_MSG_WORLD_CHANGE;
  pSlot->byWorldCount = header.byCount;
  memcpy(pSlot->aWorld, aEntries,
         header.byCount * sizeof(pSlot->aWorld[0]));
  pSlot->byValid = 1;
  ++pClient->stats.uiWorldChanges;
}

static void NetClientPublishEventCommits(tNetClient *pClient)
{
  int iFinishers = 0, iHumanFinishers = 0, iDestroyed = 0;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    if (pClient->abyLapCommitted[iCar]) {
      if ((int)(int8)Car[iCar].byLap < pClient->abyCommittedLap[iCar])
        Car[iCar].byLap = pClient->abyCommittedLap[iCar];
      if ((int)(int8)Car[iCar].byLapNumber <
          pClient->abyCommittedLap[iCar])
        Car[iCar].byLapNumber = pClient->abyCommittedLap[iCar];
    }
    if (pClient->abyKillCommitted[iCar])
      Car[iCar].byKills = pClient->abyCommittedKills[iCar];
    if (!pClient->abyFinishCommitted[iCar])
      continue;
    finished_car[iCar] = -1;
    ++iFinishers;
    iHumanFinishers += pClient->abyFinishOwner[iCar] != NET_EVENT_NO_PLAYER;
    iDestroyed += pClient->abyDestroyedCommitted[iCar] != 0;
    if (!pClient->abyDestroyedCommitted[iCar] &&
        pClient->abyFinishPosition[iCar] < numcars) {
      Car[iCar].byRacePosition = pClient->abyFinishPosition[iCar];
      carorder[pClient->abyFinishPosition[iCar]] = iCar;
    }
  }
  finishers = pClient->byResultsPublished ?
      pClient->iResultFinishers : iFinishers;
  human_finishers = pClient->byResultsPublished ?
      pClient->iResultHumanFinishers : iHumanFinishers;
  Destroyed = iDestroyed;
}

static void NetClientApplyEvent(tNetClient *pClient,
                                const tNetEvent *pEvent)
{
  tCar *pCar = pEvent->byCarIdx == NET_EVENT_NO_CAR ? NULL :
      &Car[pEvent->byCarIdx];
  switch (pEvent->byType) {
    case NET_EV_LAP_COMPLETE: {
      int iLap = pEvent->iArg0 + 1;
      pClient->abyLapCommitted[pEvent->byCarIdx] = 1;
      pClient->abyCommittedLap[pEvent->byCarIdx] = (uint8)iLap;
      pCar->fPreviousLapTime = pEvent->iArg1 / 1000.0f;
      pCar->fRunningLapTime = 0.0f;
      break;
    }
    case NET_EV_FINISHED:
      pClient->abyFinishCommitted[pEvent->byCarIdx] = 1;
      pClient->abyFinishOwner[pEvent->byCarIdx] = pEvent->byPlayerIdx;
      pClient->abyFinishPosition[pEvent->byCarIdx] = (uint8)pEvent->iArg0;
      break;
    case NET_EV_DESTROYED:
      pCar->byAttacker = (uint8)pEvent->iArg0;
      pClient->abyFinishCommitted[pEvent->byCarIdx] = 1;
      pClient->abyDestroyedCommitted[pEvent->byCarIdx] = 1;
      pClient->abyFinishOwner[pEvent->byCarIdx] = pEvent->byPlayerIdx;
      break;
    case NET_EV_KILL:
      pClient->abyKillCommitted[pEvent->byCarIdx] = 1;
      pClient->abyCommittedKills[pEvent->byCarIdx] =
          (uint8)pEvent->iArg1;
      if (pEvent->iArg0 >= 0)
        Victim = pEvent->iArg0;
      break;
    case NET_EV_RACE_STATE:
      if (!NetRaceTransition(&pClient->lifecycle, (uint8)pEvent->iArg0))
        ++pClient->stats.uiRejectedMessages;
      break;
    case NET_EV_RESULTS: {
      int iFinishers = 0, iHumanFinishers = 0;
      for (int iCar = 0; iCar < numcars; ++iCar) {
        if (!pClient->abyFinishCommitted[iCar])
          continue;
        ++iFinishers;
        iHumanFinishers +=
            pClient->abyFinishOwner[iCar] != NET_EVENT_NO_PLAYER;
      }
      if (pClient->lifecycle.byState != NET_RACE_OUTCOME_SETTLED ||
          pEvent->iArg0 != iFinishers ||
          pEvent->iArg1 != iHumanFinishers) {
        ++pClient->stats.uiRejectedMessages;
        break;
      }
      pClient->iResultFinishers = pEvent->iArg0;
      pClient->iResultHumanFinishers = pEvent->iArg1;
      pClient->byResultsPublished = 1;
      break;
    }
    default:
      /* Later lifecycle stories own the remaining numbered event types. */
      break;
  }
}

static void NetClientApplyWorldChange(const tNetClientCommitSlot *pSlot)
{
  for (int iEntry = 0; iEntry < pSlot->byWorldCount; ++iEntry) {
    const tNetWorldChangeEntry *pEntry = &pSlot->aWorld[iEntry];
    int iChunk = pEntry->nChunk;
    localdata[iChunk].iCenterGrip = pEntry->byCenterGrip;
    localdata[iChunk].iLeftShoulderGrip = pEntry->byLeftShoulderGrip;
    localdata[iChunk].iRightShoulderGrip = pEntry->byRightShoulderGrip;
    memcpy(TrakColour[iChunk], pEntry->auiTrakColour,
           sizeof(pEntry->auiTrakColour));
  }
}

static void NetClientApplyCommits(tNetClient *pClient)
{
  while (pClient->uiLastAppliedEventSeq < pClient->uiCommitWatermark) {
    uint32 uiNext = pClient->uiLastAppliedEventSeq + 1u;
    tNetClientCommitSlot *pSlot =
        &pClient->aCommits[uiNext % NET_CLIENT_COMMIT_BUFFER];
    if (!pSlot->byValid || pSlot->uiEventSeq != uiNext)
      break;
    if (pSlot->byMessageType == NET_MSG_EVENT)
      NetClientApplyEvent(pClient, &pSlot->event);
    else
      NetClientApplyWorldChange(pSlot);
    pSlot->byValid = 0;
    pClient->uiLastAppliedEventSeq = uiNext;
    pClient->stats.uiLastAppliedEventSeq = uiNext;
    ++pClient->stats.uiCommitsApplied;
  }
  NetClientPublishEventCommits(pClient);
}

static void NetClientReceiveSnapshot(tNetClient *pClient,
                                     const tNetMessage *pMessage,
                                     uint64 ullNowMs)
{
  tNetSnapshot snapshot;
  tNetClientSnapshotSlot *pSlot;
  if (!NetSnapshotDecode(pMessage->abData, pMessage->unLength, &snapshot) ||
      !NetClientSnapshotMatchesWorld(&snapshot)) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  if ((!pClient->byHasSnapshot ||
       (int32)(snapshot.uiTick - pClient->stats.uiNewestSnapshotTick) > 0) &&
      (snapshot.uiLastEventSeq < pClient->uiLastAppliedEventSeq ||
       (pClient->byHasSnapshot &&
        snapshot.uiLastEventSeq < pClient->uiCommitWatermark))) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  if (pClient->byHasSnapshot &&
      snapshot.uiTick == pClient->stats.uiNewestSnapshotTick &&
      snapshot.uiLastEventSeq != pClient->uiCommitWatermark) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  if (pClient->byHasSnapshot &&
      (int32)(pClient->stats.uiNewestSnapshotTick - snapshot.uiTick) >=
      pClient->iRetentionTicks) {
    ++pClient->stats.uiStaleMessages;
    return;
  }
  pSlot = &pClient->aSnapshots[snapshot.uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  pSlot->snapshot = snapshot;
  pSlot->byValid = 1;
  ++pClient->stats.uiSnapshots;
  if (!pClient->byHasSnapshot ||
      (int32)(snapshot.uiTick - pClient->stats.uiNewestSnapshotTick) > 0) {
    pClient->stats.uiNewestSnapshotTick = snapshot.uiTick;
    pClient->ullNewestSnapshotMs = ullNowMs;
    pClient->byHasSnapshot = 1;
    pClient->uiCommitWatermark = snapshot.uiLastEventSeq;
    pClient->stats.uiCommitWatermark = snapshot.uiLastEventSeq;
    /* The host sends a snapshot as soon as it has simulated its tick. */
    NetClientObserveHost(pClient, NetClientRelative(pClient, snapshot.uiTick),
                         ullNowMs);
  }
  NetClientApplyNewestAuthoritative(pClient);
}

static void NetClientReceiveOwnCarState(tNetClient *pClient,
                                        const tNetMessage *pMessage)
{
  uint8 abyCars[NET_INPUT_MAX_LOCAL_PLAYERS];
  tNetCarExtra aExtras[NET_INPUT_MAX_LOCAL_PLAYERS];
  tNetClientOwnSlot *pSlot;
  uint32 uiTick;
  int iCount;
  if (!NetSnapshotDecodeOwnCarState(pMessage->abData, pMessage->unLength,
                                    &uiTick, abyCars, aExtras, &iCount) ||
      iCount != pClient->byGroupCount) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  /* The decoder rejects duplicates, so a match per entry covers the group. */
  for (int iEntry = 0; iEntry < iCount; ++iEntry)
    if (NetClientMember(pClient, abyCars[iEntry]) < 0) {
      ++pClient->stats.uiRejectedMessages;
      return;
    }
  if (pClient->byHasSnapshot &&
      (int32)(pClient->stats.uiNewestSnapshotTick - uiTick) >=
      pClient->iRetentionTicks) {
    ++pClient->stats.uiStaleMessages;
    return;
  }
  pSlot = &pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  memset(pSlot, 0, sizeof(*pSlot));
  pSlot->uiTick = uiTick;
  for (int iEntry = 0; iEntry < iCount; ++iEntry)
    pSlot->aExtras[NetClientMember(pClient, abyCars[iEntry])] = aExtras[iEntry];
  pSlot->byValid = 1;
  ++pClient->stats.uiOwnCarStates;
  NetClientApplyNewestAuthoritative(pClient);
}

static void NetClientReceiveFeedback(tNetClient *pClient,
                                     const tNetMessage *pMessage,
                                     uint64 ullNowMs)
{
  tNetInputFeedback feedback;
  if (!NetInputFeedbackDecode(pMessage->abData, pMessage->unLength,
                              &feedback)) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  if (pClient->byHasFeedback &&
      (int32)(feedback.uiHostTick - pClient->uiFeedbackHostTick) < 0) {
    ++pClient->stats.uiStaleMessages;
    return;
  }
  /* uiHostTick is the host's next tick: it has simulated the one before and
     is somewhere inside the interval up to the next, so take the middle. */
  NetClientObserveHost(pClient,
                       NetClientRelative(pClient, feedback.uiHostTick) - 0.5,
                       ullNowMs);
  /* The counts are cumulative; only a rise since the previous feedback is
     news.  Late inputs while the timeline is on target mean the lead formula
     is short for this link, so the bias adds a tick. */
  if (pClient->byHasFeedback && pClient->byJoined &&
      feedback.unLateInputs > pClient->stats.uiHostLateInputs &&
      fabsf(pClient->stats.fLeadErrorTicks) < NET_CLIENT_ON_TARGET_TICKS &&
      pClient->stats.iLeadBias < NET_CLIENT_LEAD_BIAS_MAX) {
    ++pClient->stats.iLeadBias;
    pClient->ullLastBiasMs = ullNowMs;
  }
  pClient->uiFeedbackHostTick = feedback.uiHostTick;
  pClient->stats.uiHostLateInputs = feedback.unLateInputs;
  pClient->stats.uiHostFutureInputs = feedback.unFutureInputs;
  pClient->stats.nArrivalMarginTicks = feedback.nArrivalMarginTicks;
  pClient->byHasFeedback = 1;
  ++pClient->stats.uiFeedback;
}

static void NetClientReceivePause(tNetClient *pClient,
                                  const tNetMessage *pMessage,
                                  uint64 ullNowMs)
{
  tNetPause pause;
  if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
      !NetPauseDecode(pMessage->abData, pMessage->unLength, &pause)) {
    ++pClient->stats.uiRejectedMessages;
    return;
  }
  if (!NetRaceApplyPause(&pClient->lifecycle, pause.unPauseRevision,
                         pause.byPaused)) {
    ++pClient->stats.uiStaleMessages;
    return;
  }
  /* Neither time spent paused nor ticks already owed become simulation work
     after resume.  The next live tick is exactly current + 1. */
  pClient->dAccumTicks = 0.0;
  pClient->ullLastPumpMs = ullNowMs;
  ++pClient->stats.uiPauseChanges;
}

static void NetClientRaceMessage(void *pContext, const tNetMessage *pMessage)
{
  tNetClient *pClient = (tNetClient *)pContext;
  uint64 ullNowMs;
  if (!pClient || !pClient->byRacing)
    return;
  ullNowMs = NetClientNowMs(pClient);
  switch (pMessage->byType) {
    case NET_MSG_SNAPSHOT:
      NetClientReceiveSnapshot(pClient, pMessage, ullNowMs);
      break;
    case NET_MSG_OWN_CAR_STATE:
      NetClientReceiveOwnCarState(pClient, pMessage);
      break;
    case NET_MSG_INPUT_FEEDBACK:
      NetClientReceiveFeedback(pClient, pMessage, ullNowMs);
      break;
    case NET_MSG_EVENT:
      NetClientReceiveEvent(pClient, pMessage);
      break;
    case NET_MSG_WORLD_CHANGE:
      NetClientReceiveWorldChange(pClient, pMessage);
      break;
    case NET_MSG_PAUSE:
      NetClientReceivePause(pClient, pMessage, ullNowMs);
      break;
    default:
      break;
  }
}

tNetClient *NetClientCreate(tNetSessionClient *pSession,
                            tNetLobbyClient *pLobby)
{
  tNetClient *pClient;
  if (!pSession || !pLobby)
    return NULL;
  pClient = (tNetClient *)calloc(1, sizeof(*pClient));
  if (!pClient)
    return NULL;
  pClient->pSession = pSession;
  pClient->pLobby = pLobby;
  NetLobbyClientSetRaceCallback(pLobby, NetClientRaceMessage, pClient);
  return pClient;
}

void NetClientDestroy(tNetClient *pClient)
{
  if (!pClient)
    return;
  if (s_pPuppetClient == pClient) {
    net_sim_puppet_hook = NULL;
    s_pPuppetClient = NULL;
    memset(net_puppet_car, 0, sizeof(net_puppet_car));
    for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
      NetSimClearRenderCorrection(pClient->abyGroup[iMember]);
  }
  NetLobbyClientSetRaceCallback(pClient->pLobby, NULL, NULL);
  free(pClient);
}

int NetClientBeginRace(tNetClient *pClient)
{
  uint8 abyOwner[MAX_CARS];
  int aiHumanControl[MAX_CARS] = {0};
  uint8 abyGroup[NET_INPUT_MAX_LOCAL_PLAYERS] = {0};
  tNetSessionConfig config;
  uint32 uiStartTick;
  uint8 byMe;
  int iGroupCount = 0;
  if (!pClient || pClient->byRacing || net_mode != NET_MODE_MODERN ||
      (s_pPuppetClient && s_pPuppetClient != pClient) ||
      !NetLobbyClientRaceReleased(pClient->pLobby, &uiStartTick) ||
      !NetSessionClientGetConfig(pClient->pSession, &config) ||
      !config.unTickRateHz || numcars < 1 || numcars > MAX_CARS)
    return 0;
  byMe = NetSessionClientPlayerIndex(pClient->pSession);
  memset(abyOwner, 0xff, sizeof(abyOwner));
  /* The same assignment the host makes in NetHostBeginRace (4.11), from the
     roster the host broadcast when it froze it. */
  for (int iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    tNetPlayerEntry entry;
    uint8 abyCars[2];
    int iCars;
    if (!NetLobbyClientPlayer(pClient->pLobby, (uint8)iPlayer, &entry) ||
        entry.byState != NET_PLAYER_RACING)
      continue;
    abyCars[0] = entry.byCarIdx0;
    abyCars[1] = entry.byCarIdx1;
    iCars = entry.byCarIdx1 == NET_LOBBY_NO_PLAYER ? 1 : 2;
    for (int iCar = 0; iCar < iCars; ++iCar) {
      if (abyCars[iCar] >= numcars || abyOwner[abyCars[iCar]] != 0xff)
        return 0;
      abyOwner[abyCars[iCar]] = (uint8)iPlayer;
      aiHumanControl[abyCars[iCar]] = entry.byHumanControl;
      if (iPlayer == byMe)
        abyGroup[iGroupCount++] = abyCars[iCar];
    }
  }
  if (!iGroupCount)
    return 0;
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    human_control[iCar] = iCar < numcars ? aiHumanControl[iCar] : 0;

  memset(&pClient->stats, 0, sizeof(pClient->stats));
  memset(pClient->aInputs, 0, sizeof(pClient->aInputs));
  memset(pClient->aPrediction, 0, sizeof(pClient->aPrediction));
  memset(pClient->aContext, 0, sizeof(pClient->aContext));
  memset(pClient->aSnapshots, 0, sizeof(pClient->aSnapshots));
  memset(pClient->aOwn, 0, sizeof(pClient->aOwn));
  memset(pClient->aCommits, 0, sizeof(pClient->aCommits));
  memset(pClient->abyFinishCommitted, 0,
         sizeof(pClient->abyFinishCommitted));
  memset(pClient->abyDestroyedCommitted, 0,
         sizeof(pClient->abyDestroyedCommitted));
  memset(pClient->abyFinishOwner, NET_EVENT_NO_PLAYER,
         sizeof(pClient->abyFinishOwner));
  memset(pClient->abyFinishPosition, 0xff,
         sizeof(pClient->abyFinishPosition));
  memset(pClient->abyLapCommitted, 0, sizeof(pClient->abyLapCommitted));
  memset(pClient->abyKillCommitted, 0, sizeof(pClient->abyKillCommitted));
  memset(pClient->abyCommittedLap, 0, sizeof(pClient->abyCommittedLap));
  memset(pClient->abyCommittedKills, 0,
         sizeof(pClient->abyCommittedKills));
  pClient->config = config;
  pClient->dTicksPerMs = config.unTickRateHz / 1000.0;
  pClient->iRetentionTicks =
      (NET_SNAPSHOT_RETENTION_MS * config.unTickRateHz + 999) / 1000;
  pClient->iReplayBudgetTicks =
      (NET_CLIENT_REPLAY_BUDGET_MS * config.unTickRateHz) / 1000;
  if (pClient->iReplayBudgetTicks < 16)
    pClient->iReplayBudgetTicks = 16;
  if (pClient->iReplayBudgetTicks > 64)
    pClient->iReplayBudgetTicks = 64;
  memcpy(pClient->abyGroup, abyGroup, sizeof(abyGroup));
  pClient->byGroupCount = (uint8)iGroupCount;
  pClient->uiStartTick = uiStartTick;
  pClient->uiClientTick = uiStartTick - 1u;
  pClient->uiRampTick = uiStartTick - 1u;
  pClient->ullRaceBaseMs = NetClientNowMs(pClient);
  pClient->ullLastPumpMs = pClient->ullRaceBaseMs;
  pClient->ullLastBiasMs = pClient->ullRaceBaseMs;
  pClient->dAccumTicks = 0.0;
  pClient->dHostOffsetTicks = 0.0;
  pClient->iLeadBase = 0;
  pClient->iReplayPressure = 0;
  pClient->iPredictionMode = NET_PREDICT_FULL;
  pClient->uiLastReconciledTick = uiStartTick - 1u;
  pClient->uiLastAuthoritativeTick = uiStartTick - 1u;
  pClient->uiDeferredTick = uiStartTick - 1u;
  pClient->uiLastAppliedEventSeq = 0;
  pClient->uiCommitWatermark = 0;
  NetRaceLifecycleReset(&pClient->lifecycle);
  pClient->byResultsPublished = 0;
  pClient->iResultFinishers = 0;
  pClient->iResultHumanFinishers = 0;
  pClient->byJoined = 0;
  pClient->byHasEstimate = 0;
  pClient->byHasFeedback = 0;
  pClient->byHasSnapshot = 0;
  pClient->byHasFrame = 0;
  pClient->byHasReconciled = 0;
  pClient->byHasAuthoritative = 0;
  pClient->byDeferredCounted = 0;
  pClient->byAboveBudget = 0;
  pClient->byBelowBudget = 0;
  pClient->byExitReady = 0;
  pClient->stats.fTickScale = 1.0f;
  pClient->stats.fInterpolationDelayMs = NET_CLIENT_INTERPOLATION_MIN_MS;
  pClient->stats.iPredictionMode = NET_PREDICT_FULL;
  pClient->stats.iReplayBudgetTicks = pClient->iReplayBudgetTicks;
  memset(net_puppet_car, 0, sizeof(net_puppet_car));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimSetPuppet(iCar, NetClientMember(pClient, (uint8)iCar) < 0);
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    NetSimClearRenderCorrection(pClient->abyGroup[iMember]);
  s_pPuppetClient = pClient;
  net_sim_puppet_hook = NetClientPuppetHook;
  pClient->byRacing = 1;
  return 1;
}

/* lead = ceil((RTT/2 + jitter + one tick period) / tick period) (4.4), plus
   the frame interval, because a batch waits in the send queue until the next
   frame's NetPump, plus the feedback bias. */
static void NetClientUpdateLead(tNetClient *pClient, float fRttMs,
                                float fJitterMs)
{
  double dPeriodMs = 1.0 / pClient->dTicksPerMs;
  double dLead = (fRttMs * 0.5 + fJitterMs + dPeriodMs +
                  pClient->stats.fFrameMs) / dPeriodMs;
  int iLead;
  if (dLead > pClient->iLeadBase ||
      dLead < pClient->iLeadBase - NET_CLIENT_LEAD_HYSTERESIS)
    pClient->iLeadBase = (int)ceil(dLead);
  if (pClient->iLeadBase < 1)
    pClient->iLeadBase = 1;
  iLead = pClient->iLeadBase + pClient->stats.iLeadBias;
  /* Never so far ahead that the batch leaves the host's accept window. */
  if (iLead > NET_INPUT_HORIZON - NET_INPUT_REDUNDANCY)
    iLead = NET_INPUT_HORIZON - NET_INPUT_REDUNDANCY;
  pClient->stats.iLeadTicks = iLead;
}

static int NetClientExpectedReplayTicks(const tNetClient *pClient)
{
  return pClient->stats.iLeadTicks +
      (int)ceil(pClient->stats.fRttMs * 0.5 * pClient->dTicksPerMs);
}

static void NetClientUpdatePredictionPressure(tNetClient *pClient,
                                              uint64 ullNowMs,
                                              uint32 uiElapsedMs)
{
  int iExpected = NetClientExpectedReplayTicks(pClient);
  if (pClient->iPredictionMode == NET_PREDICT_FULL) {
    pClient->byBelowBudget = 0;
    if (iExpected > pClient->iReplayBudgetTicks) {
      if (!pClient->byAboveBudget) {
        pClient->byAboveBudget = 1;
        pClient->ullAboveBudgetSinceMs = ullNowMs;
      } else if (ullNowMs - pClient->ullAboveBudgetSinceMs >=
                 NET_CLIENT_HIGH_RTT_MS) {
        NetClientEnterDelayed(pClient);
      }
    } else {
      pClient->byAboveBudget = 0;
    }
  } else {
    uint32 uiBefore = pClient->stats.uiTimeDegradedMs;
    pClient->byAboveBudget = 0;
    pClient->stats.uiTimeDegradedMs += uiElapsedMs;
    if (pClient->stats.uiTimeDegradedMs < uiBefore)
      pClient->stats.uiTimeDegradedMs = UINT32_MAX;
    if ((float)iExpected <=
        (float)pClient->iReplayBudgetTicks * NET_CLIENT_RTT_HYSTERESIS) {
      if (!pClient->byBelowBudget) {
        pClient->byBelowBudget = 1;
        pClient->ullBelowBudgetSinceMs = ullNowMs;
      } else if (ullNowMs - pClient->ullBelowBudgetSinceMs >=
                 NET_CLIENT_LOW_RTT_MS) {
        pClient->byExitReady = 1;
      }
    } else {
      pClient->byBelowBudget = 0;
      pClient->byExitReady = 0;
    }
  }
}

void NetClientPump(tNetClient *pClient)
{
  tNetConnection *pConnection;
  uint64 ullNowMs;
  double dElapsedMs, dScale = 1.0;
  if (!pClient || !pClient->byRacing)
    return;
  pConnection = NetSessionClientConnection(pClient->pSession);
  ullNowMs = NetConnectionNowMs(pConnection);
  dElapsedMs = (double)(ullNowMs - pClient->ullLastPumpMs);
  pClient->ullLastPumpMs = ullNowMs;
  if (dElapsedMs > 0.0) {
    if (!pClient->byHasFrame)
      pClient->stats.fFrameMs = (float)dElapsedMs;
    else
      pClient->stats.fFrameMs += ((float)dElapsedMs - pClient->stats.fFrameMs) *
                                 NET_CLIENT_FRAME_GAIN;
    pClient->byHasFrame = 1;
  }
  if (pClient->stats.iLeadBias > 0 &&
      ullNowMs - pClient->ullLastBiasMs >= NET_CLIENT_LEAD_BIAS_DECAY_MS) {
    --pClient->stats.iLeadBias;
    pClient->ullLastBiasMs = ullNowMs;
  }
  pClient->stats.fRttMs = NetConnectionRttMs(pConnection);
  pClient->stats.fJitterMs = NetConnectionJitterMs(pConnection);
  pClient->stats.fInterpolationDelayMs =
      (float)(pClient->config.bySnapshotInterval / pClient->dTicksPerMs) +
      2.0f * pClient->stats.fJitterMs;
  if (pClient->stats.fInterpolationDelayMs < NET_CLIENT_INTERPOLATION_MIN_MS)
    pClient->stats.fInterpolationDelayMs = NET_CLIENT_INTERPOLATION_MIN_MS;
  if (pClient->stats.fInterpolationDelayMs > NET_CLIENT_INTERPOLATION_MAX_MS)
    pClient->stats.fInterpolationDelayMs = NET_CLIENT_INTERPOLATION_MAX_MS;
  NetClientUpdateLead(pClient, pClient->stats.fRttMs, pClient->stats.fJitterMs);
  if (!pClient->lifecycle.byPaused)
    NetClientUpdatePredictionPressure(
        pClient, ullNowMs,
        dElapsedMs >= (double)UINT32_MAX ? UINT32_MAX : (uint32)dElapsedMs);

  if (pClient->byHasEstimate) {
    double dHost = NetClientHostTickAt(pClient, ullNowMs);
    double dPosition = NetClientRelative(pClient, pClient->uiClientTick) +
                       pClient->dAccumTicks;
    double dError = dPosition - (dHost + pClient->stats.iLeadTicks);
    /* Join (4.4): the one time the timeline may jump.  The ticks are still
       all simulated, just in a burst; dilation takes over from here. */
    if (!pClient->byJoined) {
      if (dError < 0.0)
        pClient->dAccumTicks += fmin(-dError, (double)NET_INPUT_HORIZON);
      pClient->byJoined = 1;
      dError = NetClientRelative(pClient, pClient->uiClientTick) +
               pClient->dAccumTicks - (dHost + pClient->stats.iLeadTicks);
    }
    dScale = 1.0 - NET_CLIENT_LEAD_GAIN * dError;
    if (dScale < NET_CLIENT_TICK_SCALE_MIN)
      dScale = NET_CLIENT_TICK_SCALE_MIN;
    if (dScale > NET_CLIENT_TICK_SCALE_MAX)
      dScale = NET_CLIENT_TICK_SCALE_MAX;
    pClient->stats.fHostTick = (float)dHost;
    pClient->stats.fLeadErrorTicks = (float)dError;
  }
  if (!pClient->lifecycle.byPaused)
    pClient->dAccumTicks += dElapsedMs * pClient->dTicksPerMs * dScale;
  else
    pClient->dAccumTicks = 0.0;
  pClient->stats.fTickScale = (float)dScale;
  pClient->stats.byHasHostEstimate = pClient->byHasEstimate;
  pClient->stats.uiSnapshotAgeMs = pClient->byHasSnapshot ?
      (uint32)(ullNowMs - pClient->ullNewestSnapshotMs) : 0;
  pClient->stats.iRaceState = pClient->lifecycle.byState;
  pClient->stats.iPaused = pClient->lifecycle.byPaused;
  pClient->stats.unPauseRevision = pClient->lifecycle.unPauseRevision;
  pClient->stats.iResultsPublished = pClient->byResultsPublished;
  pClient->stats.iResultFinishers = pClient->iResultFinishers;
  pClient->stats.iResultHumanFinishers = pClient->iResultHumanFinishers;

  g_netStats.iSnapshotAgeMs = (int)pClient->stats.uiSnapshotAgeMs;
  g_netStats.fRttMs = pClient->stats.fRttMs;
  g_netStats.fJitterMs = pClient->stats.fJitterMs;
  g_netStats.fTickScale = pClient->stats.fTickScale;
  g_netStats.iLateInputs = (int)pClient->stats.uiHostLateInputs;
  g_netStats.iFutureInputs = (int)pClient->stats.uiHostFutureInputs;
  NetClientPublishReconciliationStats(pClient);
}

int NetClientTicksDue(const tNetClient *pClient)
{
  if (!pClient || !pClient->byRacing || pClient->lifecycle.byPaused ||
      pClient->dAccumTicks < 1.0)
    return 0;
  return (int)pClient->dAccumTicks;
}

static void NetClientSendBatch(tNetClient *pClient, uint32 uiTick)
{
  tNetInputBatch batch;
  uint8 abBatch[NET_INPUT_BATCH_MAX_BYTES];
  int iLength;
  memset(&batch, 0, sizeof(batch));
  /* Always the last NET_INPUT_REDUNDANCY ticks, so uiFirstTick advances by
     exactly one per client tick; ticks before the start carry neutral
     input, and the host ignores them as old. */
  batch.uiFirstTick = uiTick - (NET_INPUT_REDUNDANCY - 1);
  batch.uiLastDecodedSnapshotTick = pClient->stats.uiNewestSnapshotTick;
  batch.byCount = NET_INPUT_REDUNDANCY;
  batch.byLocalPlayers = pClient->byGroupCount;
  for (int iTick = 0; iTick < NET_INPUT_REDUNDANCY; ++iTick) {
    uint32 uiBatchTick = batch.uiFirstTick + (uint32)iTick;
    const tNetClientInputSlot *pSlot =
        &pClient->aInputs[uiBatchTick % NET_CLIENT_HISTORY];
    if (NetClientStarted(pClient, uiBatchTick) && pSlot->byValid &&
        pSlot->uiTick == uiBatchTick)
      memcpy(batch.aInputs[iTick], pSlot->aInputs, sizeof(pSlot->aInputs));
  }
  iLength = NetInputBatchEncode(&batch, abBatch, sizeof(abBatch));
  if (iLength &&
      NetConnectionQueueMessage(NetSessionClientConnection(pClient->pSession),
                                NET_MSG_INPUT, 0, abBatch, (uint16)iLength))
    ++pClient->stats.uiBatchesSent;
}

static int NetClientRecordPostTick(tNetClient *pClient, uint32 uiTick,
                                   int iRecordPrediction)
{
  if (iRecordPrediction) {
    for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
      tNetClientPredictionSlot *pSlot =
          &pClient->aPrediction[iMember][uiTick % NET_CLIENT_HISTORY];
      pSlot->uiTick = uiTick;
      pSlot->byValid = (uint8)NetSnapshotEncodeCarFull(
          pClient->abyGroup[iMember], &pSlot->state);
      if (!pSlot->byValid)
        return 0;
    }
  }
  {
    tNetClientContextSlot *pSlot =
        &pClient->aContext[uiTick % NET_CLIENT_HISTORY];
    NetSimCaptureContext(&pSlot->context);
    pSlot->uiTick = uiTick;
    pSlot->byValid = 1;
  }
  return 1;
}

static void NetClientPublishReconciliationStats(const tNetClient *pClient)
{
  g_netStats.fCorrectionMagnitude = pClient->stats.fCorrectionMagnitude;
  g_netStats.iCorrectionCount = (int)pClient->stats.uiCorrections;
  g_netStats.iDeferredCorrections =
      (int)pClient->stats.uiDeferredCorrections;
  g_netStats.iReplayTicksTotal = (int)pClient->stats.uiReplayTicksTotal;
  g_netStats.iReplayDepth = pClient->stats.iReplayDepth;
  g_netStats.fReplayMsTotal = pClient->stats.fReplayMsTotal;
  g_netStats.fReplayMsWorst = pClient->stats.fReplayMsWorst;
  g_netStats.iPredictionMode = pClient->stats.iPredictionMode;
  g_netStats.iPredictionTransitions = pClient->stats.iPredictionTransitions;
  g_netStats.uiTimeDegradedMs = pClient->stats.uiTimeDegradedMs;
}

static void NetClientEnterDelayed(tNetClient *pClient)
{
  if (pClient->iPredictionMode == NET_PREDICT_DELAYED)
    return;
  pClient->iPredictionMode = NET_PREDICT_DELAYED;
  pClient->stats.iPredictionMode = NET_PREDICT_DELAYED;
  ++pClient->stats.iPredictionTransitions;
  pClient->byAboveBudget = 0;
  pClient->byBelowBudget = 0;
  pClient->byExitReady = 0;
  memset(pClient->aPrediction, 0, sizeof(pClient->aPrediction));
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    NetSimSetPuppet(pClient->abyGroup[iMember], 1);
    NetSimClearRenderCorrection(pClient->abyGroup[iMember]);
  }
  NetClientPublishReconciliationStats(pClient);
}

static int NetClientReplayInputs(tNetClient *pClient, uint32 uiFirstTick,
                                 int iCount, tNetInputSlot *pSlots)
{
  if (iCount < 1 || iCount > NET_CLIENT_HISTORY)
    return 0;
  memset(pSlots, 0, (size_t)iCount * sizeof(*pSlots));
  for (int iSlot = 0; iSlot < iCount; ++iSlot) {
    uint32 uiTick = uiFirstTick + (uint32)iSlot;
    const tNetClientInputSlot *pInput =
        &pClient->aInputs[uiTick % NET_CLIENT_HISTORY];
    if (!pInput->byValid || pInput->uiTick != uiTick)
      return 0;
    pSlots[iSlot].uiTick = uiTick;
    for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
      pSlots[iSlot].aInputs[pClient->abyGroup[iMember]].data =
          pInput->aInputs[iMember];
  }
  return 1;
}

static int NetClientReplayFrom(tNetClient *pClient, uint32 uiTick)
{
  tNetSnapshot snapshot;
  tNetCarFullState aHost[NET_INPUT_MAX_LOCAL_PLAYERS];
  tNetInputSlot aInputs[NET_CLIENT_HISTORY];
  tNetSimTickContext context;
  tNetWorldPose aBefore[NET_INPUT_MAX_LOCAL_PLAYERS];
  tNetWorldPose aAfter[NET_INPUT_MAX_LOCAL_PLAYERS];
  const tNetClientContextSlot *pContext;
  uint64 ullStartedMs, ullElapsedMs;
  uint32 uiCurrent = pClient->uiClientTick;
  int iReplayTicks = (int)(uiCurrent - uiTick);
  int iInputCount = iReplayTicks + 1;
  int iSavedAuthority, iSavedReplaying;
  float fMagnitude = 0.0f;

  if ((int32)(uiCurrent - uiTick) < 0 ||
      iReplayTicks > pClient->iReplayBudgetTicks ||
      NetClientFullStatesAt(pClient, uiTick, &snapshot, aHost) != 1)
    return 0;
  pContext = &pClient->aContext[uiTick % NET_CLIENT_HISTORY];
  if (!pContext->byValid || pContext->uiTick != uiTick ||
      !NetClientReplayInputs(pClient, uiTick, iInputCount, aInputs))
    return 0;
  context = pContext->context;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    int iCar = pClient->abyGroup[iMember];
    if (!NetSnapshotCarFullValid(iCar, &aHost[iMember]) ||
        !NetSimLegacyToWorld(&Car[iCar], &aBefore[iMember]))
      return 0;
  }

  ullStartedMs = NetClientNowMs(pClient);
  /* D18 restore order: geometry, the entire rollback group, matched
     post-tick context, then the input slot for T and every consumed slot. */
  if (!NetSimRestoreRamps(snapshot.aRamps))
    return 0;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    if (!NetSnapshotDecodeCarFull(pClient->abyGroup[iMember],
                                  &aHost[iMember]))
      return 0;
#ifndef NDEBUG
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    tNetWorldPose restored;
    const tNetCarState *pState = &aHost[iMember].state;
    assert(NetSimLegacyToWorld(&Car[pClient->abyGroup[iMember]], &restored));
    assert(fabsf(restored.position.fX - pState->fWorldPosX) < 0.05f);
    assert(fabsf(restored.position.fY - pState->fWorldPosY) < 0.05f);
    assert(fabsf(restored.position.fZ - pState->fWorldPosZ) < 0.05f);
    assert(NetClientAngleDifference(restored.nYaw, pState->nWorldYaw) <= 2);
  }
#endif
  NetSimRestoreContext(&context);
  if (!NetSimRestoreInputRing(aInputs, uiTick, iInputCount,
                              context.iReadptr))
    return 0;
  if (!NetClientRecordPostTick(pClient, uiTick, 1))
    return 0;
  /* The replay input ring is preloaded through uiCurrent, so its physical
     writeptr is ahead.  History records the logical post-tick boundary a
     live run had at T, where producer and consumer had caught up. */
  pClient->aContext[uiTick % NET_CLIENT_HISTORY].context.iWriteptr =
      context.iWriteptr;

  iSavedAuthority = net_sim_authority;
  iSavedReplaying = net_sim_replaying;
  net_sim_authority = NET_AUTHORITY_REMOTE;
  net_sim_replaying = 1;
  for (uint32 uiReplay = uiTick + 1u;
       (int32)(uiCurrent - uiReplay) >= 0; ++uiReplay) {
    control_one_tick();
    if (!NetClientRecordPostTick(pClient, uiReplay, 1)) {
      net_sim_replaying = iSavedReplaying;
      net_sim_authority = iSavedAuthority;
      return 0;
    }
    pClient->aContext[uiReplay % NET_CLIENT_HISTORY].context.iWriteptr =
        pClient->aContext[uiReplay % NET_CLIENT_HISTORY].context.iReadptr;
  }
  net_sim_replaying = iSavedReplaying;
  net_sim_authority = iSavedAuthority;
  pClient->uiRampTick = uiCurrent;
  pClient->stats.uiRampTick = uiCurrent;

  if (NetClientApplyAuthoritativeAt(pClient, uiTick, 1) != 1 ||
      !NetClientRecordPostTick(pClient, uiCurrent, 1))
    return 0;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    float fDx, fDy, fDz, fDistance;
    int iCar = pClient->abyGroup[iMember];
    if (!NetSimLegacyToWorld(&Car[iCar], &aAfter[iMember]))
      return 0;
    fDx = aBefore[iMember].position.fX - aAfter[iMember].position.fX;
    fDy = aBefore[iMember].position.fY - aAfter[iMember].position.fY;
    fDz = aBefore[iMember].position.fZ - aAfter[iMember].position.fZ;
    fDistance = sqrtf(fDx * fDx + fDy * fDy + fDz * fDz);
    if (fDistance > fMagnitude)
      fMagnitude = fDistance;
    NetSimSetRenderCorrection(iCar, &aBefore[iMember], &aAfter[iMember],
                              NET_CLIENT_CORRECTION_TICKS);
  }
  ullElapsedMs = NetClientNowMs(pClient) - ullStartedMs;
  ++pClient->stats.uiCorrections;
  pClient->stats.uiReplayTicksTotal += (uint32)iReplayTicks;
  pClient->stats.iReplayDepth = iReplayTicks;
  pClient->stats.fCorrectionMagnitude = fMagnitude;
  pClient->stats.fReplayMsTotal += (float)ullElapsedMs;
  if ((float)ullElapsedMs > pClient->stats.fReplayMsWorst)
    pClient->stats.fReplayMsWorst = (float)ullElapsedMs;
  pClient->uiLastReconciledTick = uiTick;
  pClient->byHasReconciled = 1;
  pClient->byDeferredCounted = 0;
  if (pClient->iReplayPressure > 0)
    --pClient->iReplayPressure;
  pClient->stats.iReplayPressure = pClient->iReplayPressure;
  NetClientPublishReconciliationStats(pClient);
  return 1;
}

static int NetClientNewestCandidate(const tNetClient *pClient,
                                    uint32 *puiTick)
{
  uint32 uiNewest = 0;
  int iFound = 0;
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    uint32 uiTick;
    if (!pSlot->byValid)
      continue;
    uiTick = pSlot->snapshot.uiTick;
    if ((int32)(pClient->uiClientTick - uiTick) < 0 ||
        (pClient->byHasReconciled &&
         (int32)(uiTick - pClient->uiLastReconciledTick) <= 0))
      continue;
    if (!iFound || (int32)(uiTick - uiNewest) > 0) {
      uiNewest = uiTick;
      iFound = 1;
    }
  }
  if (iFound)
    *puiTick = uiNewest;
  return iFound;
}

static void NetClientDefer(tNetClient *pClient, uint32 uiTick)
{
  if (!pClient->byDeferredCounted || pClient->uiDeferredTick != uiTick) {
    pClient->uiDeferredTick = uiTick;
    pClient->byDeferredCounted = 1;
    ++pClient->stats.uiDeferredCorrections;
    NetClientPublishReconciliationStats(pClient);
  }
}

static int NetClientReconcile(tNetClient *pClient)
{
  tNetSnapshot snapshot;
  tNetCarFullState aHost[NET_INPUT_MAX_LOCAL_PLAYERS];
  uint32 uiTick;
  int iPair;
  int iReplayTicks;
  if (pClient->iPredictionMode != NET_PREDICT_FULL ||
      !NetClientNewestCandidate(pClient, &uiTick))
    return 1;
  iPair = NetClientFullStatesAt(pClient, uiTick, &snapshot, aHost);
  if (iPair < 0) {
    pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER].byValid = 0;
    ++pClient->stats.uiRejectedMessages;
    return 1;
  }
  if (!iPair) {
    NetClientDefer(pClient, uiTick);
    return 1;
  }
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    const tNetClientPredictionSlot *pPredicted =
        &pClient->aPrediction[iMember][uiTick % NET_CLIENT_HISTORY];
    if (!pPredicted->byValid || pPredicted->uiTick != uiTick) {
      NetClientDefer(pClient, uiTick);
      return 1;
    }
  }
  {
    const tNetClientContextSlot *pContext =
        &pClient->aContext[uiTick % NET_CLIENT_HISTORY];
    if (!pContext->byValid || pContext->uiTick != uiTick) {
      NetClientDefer(pClient, uiTick);
      return 1;
    }
  }
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    const tNetClientPredictionSlot *pPredicted =
        &pClient->aPrediction[iMember][uiTick % NET_CLIENT_HISTORY];
    if (!NetClientMovementWithin(&pPredicted->state, &aHost[iMember]))
      goto correct;
  }
  pClient->uiLastReconciledTick = uiTick;
  pClient->byHasReconciled = 1;
  pClient->byDeferredCounted = 0;
  if (pClient->iReplayPressure > 0)
    --pClient->iReplayPressure;
  pClient->stats.iReplayPressure = pClient->iReplayPressure;
  NetClientApplyAuthoritativeAt(pClient, uiTick, 1);
  return 1;

correct:
  iReplayTicks = (int)(pClient->uiClientTick - uiTick);
  if (iReplayTicks > pClient->iReplayBudgetTicks) {
    if (pClient->iReplayPressure < NET_CLIENT_REPLAY_PRESSURE_MAX)
      ++pClient->iReplayPressure;
    pClient->stats.iReplayPressure = pClient->iReplayPressure;
    if (pClient->iReplayPressure >= NET_CLIENT_REPLAY_PRESSURE_MAX)
      NetClientEnterDelayed(pClient);
    return 1;
  }
  return NetClientReplayFrom(pClient, uiTick);
}

static int NetClientTryLeaveDelayed(tNetClient *pClient)
{
  uint32 uiNewest = 0;
  int iFound = 0;
  if (pClient->iPredictionMode != NET_PREDICT_DELAYED ||
      !pClient->byExitReady)
    return 1;
  for (int iSlot = 0; iSlot < NET_CLIENT_SNAPSHOT_BUFFER; ++iSlot) {
    const tNetClientSnapshotSlot *pSlot = &pClient->aSnapshots[iSlot];
    uint32 uiTick;
    tNetCarFullState aStates[NET_INPUT_MAX_LOCAL_PLAYERS];
    if (!pSlot->byValid)
      continue;
    uiTick = pSlot->snapshot.uiTick;
    if ((int32)(pClient->uiClientTick - uiTick) < 0 ||
        (int)(pClient->uiClientTick - uiTick) > pClient->iReplayBudgetTicks ||
        NetClientFullStatesAt(pClient, uiTick, NULL, aStates) != 1)
      continue;
    if (!iFound || (int32)(uiTick - uiNewest) > 0) {
      uiNewest = uiTick;
      iFound = 1;
    }
  }
  if (!iFound)
    return 1;
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
    NetSimSetPuppet(pClient->abyGroup[iMember], 0);
  if (!NetClientReplayFrom(pClient, uiNewest)) {
    for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember)
      NetSimSetPuppet(pClient->abyGroup[iMember], 1);
    return 0;
  }
  pClient->iPredictionMode = NET_PREDICT_FULL;
  pClient->iReplayPressure = 0;
  pClient->stats.iPredictionMode = NET_PREDICT_FULL;
  pClient->stats.iReplayPressure = 0;
  ++pClient->stats.iPredictionTransitions;
  pClient->byExitReady = 0;
  pClient->byBelowBudget = 0;
  NetClientPublishReconciliationStats(pClient);
  return 1;
}

int NetClientTick(tNetClient *pClient, const tCarInputData *pLocalInputs)
{
  tCopyData aInputs[MAX_CARS];
  tNetClientInputSlot *pInput;
  uint32 uiTick;
  int iSavedAuthority;
  if (!pClient || !pLocalInputs || pClient->lifecycle.byPaused ||
      NetClientTicksDue(pClient) < 1)
    return 0;
  uiTick = pClient->uiClientTick + 1u;
  NetSimAdvanceRenderCorrections();

  /* 4.3 step 1: a snapshot may outrun its reliable commits.  Advance only
     through the contiguous shared sequence covered by the newest snapshot. */
  NetClientApplyCommits(pClient);
  NetClientApplyNewestAuthoritative(pClient);

  /* 4.3 step 2: compare matching ticks before updatestunts advances the
     live ramps.  Correction installs timing and rebuilds geometry, but never
     advances the authoritative local timeline. */
  if (!NetClientCorrectRamps(pClient))
    return 0;

  /* 4.3 steps 3 and 4: a delayed client resumes through the same restore
     and replay path; a fully predicting client reconciles before this tick
     is sampled, so its post-tick history contains the corrected run. */
  if (!NetClientTryLeaveDelayed(pClient) || !NetClientReconcile(pClient))
    return 0;
  /* A correction restores a locally recorded context, which can predate a
     late reliable result commit.  Re-publish those host-owned globals after
     the replay without turning them into movement corrections (D19). */
  NetClientPublishEventCommits(pClient);

  /* 4.3 step 5: canonicalise, record in the input history, send. */
  pInput = &pClient->aInputs[uiTick % NET_CLIENT_HISTORY];
  memset(pInput, 0, sizeof(*pInput));
  pInput->uiTick = uiTick;
  memset(aInputs, 0, sizeof(aInputs));
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    pInput->aInputs[iMember] = pLocalInputs[iMember];
    NetSimCanonicaliseInput(&pInput->aInputs[iMember]);
    aInputs[pClient->abyGroup[iMember]].data = pInput->aInputs[iMember];
  }
  if (!NetSimWriteTickInputs(aInputs, numcars))
    return 0;
  pInput->byValid = 1;
  NetClientSendBatch(pClient, uiTick);

  /* 4.3 step 6: the client simulates movement only (4.14). */
  iSavedAuthority = net_sim_authority;
  net_sim_authority = NET_AUTHORITY_REMOTE;
  control_one_tick();
  net_sim_authority = iSavedAuthority;
  pClient->uiClientTick = uiTick;
  pClient->uiRampTick = uiTick;
  pClient->stats.uiRampTick = uiTick;
  pClient->dAccumTicks -= 1.0;
  ++pClient->stats.uiTicks;

  /* 4.3 step 7: record after the tick, so context[N] and pred[N] are the
     state the simulation enters tick N + 1 with. */
  NetClientApplyNewestAuthoritative(pClient);
  /* The puppet hook may have sampled a pre-commit snapshot for its delayed
     render cursor.  Reliable result commits remain the displayed truth. */
  NetClientPublishEventCommits(pClient);
  return NetClientRecordPostTick(
      pClient, uiTick, pClient->iPredictionMode == NET_PREDICT_FULL);
}

uint32 NetClientCurrentTick(const tNetClient *pClient)
{
  return pClient ? pClient->uiClientTick : 0;
}

int NetClientGroup(const tNetClient *pClient, uint8 *pbyCars)
{
  if (!pClient || !pClient->byRacing)
    return 0;
  if (pbyCars)
    memcpy(pbyCars, pClient->abyGroup, pClient->byGroupCount);
  return pClient->byGroupCount;
}

int NetClientStats(const tNetClient *pClient, tNetClientStats *pStats)
{
  if (!pClient || !pStats || !pClient->byRacing)
    return 0;
  *pStats = pClient->stats;
  return 1;
}

int NetClientPaused(const tNetClient *pClient)
{
  return pClient && pClient->byRacing && pClient->lifecycle.byPaused;
}

uint16 NetClientPauseRevision(const tNetClient *pClient)
{
  return pClient ? pClient->lifecycle.unPauseRevision : 0;
}

eNetRaceState NetClientRaceState(const tNetClient *pClient)
{
  return pClient ? (eNetRaceState)pClient->lifecycle.byState :
      NET_RACE_STOPPED;
}

int NetClientResults(const tNetClient *pClient, int *piFinishers,
                     int *piHumanFinishers)
{
  if (!pClient || !pClient->byResultsPublished)
    return 0;
  if (piFinishers)
    *piFinishers = pClient->iResultFinishers;
  if (piHumanFinishers)
    *piHumanFinishers = pClient->iResultHumanFinishers;
  return 1;
}

static int NetClientRingTick(const tNetClient *pClient, uint32 uiTick)
{
  return pClient && pClient->byRacing && NetClientStarted(pClient, uiTick) &&
         NetClientInWindow(pClient, uiTick, pClient->uiClientTick,
                           NET_CLIENT_HISTORY);
}

int NetClientInputAt(const tNetClient *pClient, uint32 uiTick,
                     tCarInputData *pInputs)
{
  const tNetClientInputSlot *pSlot;
  if (!pInputs || !NetClientRingTick(pClient, uiTick))
    return 0;
  pSlot = &pClient->aInputs[uiTick % NET_CLIENT_HISTORY];
  if (!pSlot->byValid || pSlot->uiTick != uiTick)
    return 0;
  memcpy(pInputs, pSlot->aInputs,
         pClient->byGroupCount * sizeof(pSlot->aInputs[0]));
  return 1;
}

int NetClientPredictionAt(const tNetClient *pClient, int iMember,
                          uint32 uiTick, tNetCarFullState *pState)
{
  const tNetClientPredictionSlot *pSlot;
  if (!pState || !NetClientRingTick(pClient, uiTick) || iMember < 0 ||
      iMember >= pClient->byGroupCount)
    return 0;
  pSlot = &pClient->aPrediction[iMember][uiTick % NET_CLIENT_HISTORY];
  if (!pSlot->byValid || pSlot->uiTick != uiTick)
    return 0;
  *pState = pSlot->state;
  return 1;
}

int NetClientContextAt(const tNetClient *pClient, uint32 uiTick,
                       tNetSimTickContext *pContext)
{
  const tNetClientContextSlot *pSlot;
  if (!pContext || !NetClientRingTick(pClient, uiTick))
    return 0;
  pSlot = &pClient->aContext[uiTick % NET_CLIENT_HISTORY];
  if (!pSlot->byValid || pSlot->uiTick != uiTick)
    return 0;
  *pContext = pSlot->context;
  return 1;
}

int NetClientSnapshotAt(const tNetClient *pClient, uint32 uiTick,
                        tNetSnapshot *pSnapshot)
{
  const tNetClientSnapshotSlot *pSlot;
  if (!pClient || !pSnapshot || !pClient->byRacing || !pClient->byHasSnapshot ||
      !NetClientInWindow(pClient, uiTick, pClient->stats.uiNewestSnapshotTick,
                         pClient->iRetentionTicks + 1))
    return 0;
  pSlot = &pClient->aSnapshots[uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  if (!pSlot->byValid || pSlot->snapshot.uiTick != uiTick)
    return 0;
  *pSnapshot = pSlot->snapshot;
  return 1;
}

int NetClientOwnCarStateAt(const tNetClient *pClient, uint32 uiTick,
                           tNetCarExtra *pExtras)
{
  const tNetClientOwnSlot *pSlot;
  if (!pClient || !pExtras || !pClient->byRacing || !pClient->byHasSnapshot ||
      !NetClientInWindow(pClient, uiTick, pClient->stats.uiNewestSnapshotTick,
                         pClient->iRetentionTicks + 1))
    return 0;
  pSlot = &pClient->aOwn[uiTick % NET_CLIENT_SNAPSHOT_BUFFER];
  if (!pSlot->byValid || pSlot->uiTick != uiTick)
    return 0;
  memcpy(pExtras, pSlot->aExtras,
         pClient->byGroupCount * sizeof(pSlot->aExtras[0]));
  return 1;
}
