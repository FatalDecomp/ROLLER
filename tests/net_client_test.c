/* NET-E4-S1/S2/S3/S4 acceptance: client timeline, rings, remote-car world-space
   interpolation, bounded stall extrapolation, rollback/replay, prediction
   modes, and the in-tick puppet hook.

   One process holds one simulation world (D13), and here it is the client's:
   the client predicts, records all three rings and is the thing under test.
   The host is real - session, lobby, input queues, snapshot cadence and
   input feedback all run - but its simulation step is replaced through
   NetHostSetSimulation, which records the inputs it would have simulated.
   That keeps the host's timing behaviour, which is what the acceptance
   measures, without a second world in the process. */
#include "net_client.h"
#include "net_config_internal.h"
#include "net_event.h"
#include "net_headless.h"
#include "net_host.h"
#include "net_input.h"
#include "net_race_start.h"
#include "net_race_state.h"
#include "net_sim_seam.h"
#include "net_snapshot.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "engines.h"
#include "frontend.h"
#include "loadtrak.h"
#include "roller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #iCondition); exit(1); \
} } while (0)

#define NET_TEST_START_TICK 5000u
#define NET_TEST_LATENCY_MS 60      /* 120 ms RTT */
#define NET_TEST_JITTER_MS 10       /* +/- 10 ms each way: 20 ms on the RTT */
#define NET_TEST_LOSS_PERMILLE 30   /* 3 percent, each way */
#define NET_TEST_STEP_LATENCY_MS 60 /* the +60 ms step, each way */
#define NET_TEST_WARMUP_MS 2000
#define NET_TEST_RECOVERY_MS 5000
#define NET_TEST_MAX_TICKS 16384
#define NET_TEST_FRAME_NUMERATOR 50 /* 60 fps: 50/3 ms per frame */

typedef struct
{
  uint64 ullState;
} tNetTestRandom;

typedef struct
{
  uint32 uiTick;
  uint64 ullMs;
  uint8 byLate;
  tCarInputData input;
} tNetTestHostTick;

typedef struct
{
  int iTicks;
  uint8 byCar, byRemoteCar, byRemoteReady;
  uint8 byLocalSwapped;
  uint32 uiStartTick;
  tNetClient *pClient;
  tCar savedLocalCar;
  int iSavedHumanControl, iSavedFinished;
  tNetWorldPose remoteBase, lastPuppetPose;
  float fLastPuppetTick, fMaxPuppetStepPerTick;
  int iPuppetSamples, iSawChunk0, iSawChunk1, iSawAirborne;
  tNetTestHostTick aTicks[NET_TEST_MAX_TICKS];
} tNetTestHostRun;

typedef struct
{
  tCar aCars[MAX_CARS];
  tNetRampState aRamps[NET_MAX_RAMPS];
  tNetSimTickContext context;
  tCopyData aRing[512][16];
  int aiHumanControl[16];
} tNetTestMoment;

static tNetTestHostRun s_hostRun;
static tNetTestMoment s_pristine;

static int NetTestRandomBytes(void *pContext, void *pData, int iLength)
{
  tNetTestRandom *pRandom = (tNetTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  for (int iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState ^= pRandom->ullState << 13;
    pRandom->ullState ^= pRandom->ullState >> 7;
    pRandom->ullState ^= pRandom->ullState << 17;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 24);
  }
  return 1;
}

static void NetTestCapture(tNetTestMoment *pMoment)
{
  memset(pMoment, 0, sizeof(*pMoment));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimSaveCar(iCar, &pMoment->aCars[iCar]);
  NetSimSaveRamps(pMoment->aRamps);
  NetSimCaptureContext(&pMoment->context);
  memcpy(pMoment->aRing, copy_multiple, sizeof(pMoment->aRing));
  memcpy(pMoment->aiHumanControl, human_control, sizeof(pMoment->aiHumanControl));
}

static void NetTestRestore(const tNetTestMoment *pMoment)
{
  CHECK(NetSimRestoreRamps(pMoment->aRamps));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimRestoreCar(iCar, &pMoment->aCars[iCar]);
  NetSimRestoreContext(&pMoment->context);
  memcpy(copy_multiple, pMoment->aRing, sizeof(copy_multiple));
  memcpy(human_control, pMoment->aiHumanControl, sizeof(human_control));
}

/* What the player "presses" on tick uiTick.  Steering stays inside the D9
   clamp so that a host-side difference means a lost or late input, never a
   clamp. */
static tCarInputData NetTestScript(uint32 uiTick, int iCar)
{
  int iLimit = CarEngines.engines[Car[iCar].byCarDesignIdx].iSteeringSensitivity * 256;
  int iPhase = (int)(uiTick % 90u);
  tCarInputData input;
  input.unInput = (uint16)(int16)(((int)(uiTick / 15u) % 3 - 1) * (iLimit / 2));
  input.unFlags = BUTTON_FLAG_ACCEL;
  if (iPhase >= 60 && iPhase < 68)
    input.unFlags = BUTTON_FLAG_BRAKE;
  return input;
}

static tNetWorldPose NetTestRemotePose(const tNetTestHostRun *pRun,
                                       double dTick)
{
  double dRelative = dTick - pRun->uiStartTick;
  tNetWorldPose pose = pRun->remoteBase;
  pose.position.fX += (float)(1.5 * dRelative);
  pose.position.fY += (float)(0.5 * dRelative);
  pose.position.fZ += (float)(0.25 * dRelative);
  pose.nYaw = (int16)((pRun->remoteBase.nYaw +
                       (int)lround(37.0 * dRelative)) & 16383);
  pose.nPitch = (int16)((pRun->remoteBase.nPitch +
                         (int)lround(11.0 * dRelative)) & 16383);
  pose.nRoll = (int16)((pRun->remoteBase.nRoll +
                        (int)lround(19.0 * dRelative)) & 16383);
  pose.nActualYaw = pose.nYaw;
  return pose;
}

static int NetTestAngleDifference(int16 nA, int16 nB)
{
  int iDifference = ((nA - nB + 8192) & 16383) - 8192;
  return iDifference < 0 ? -iDifference : iDifference;
}

static int NetTestInterpolatedState(tNetClient *pClient,
                                    const tNetClientStats *pStats, int iCar,
                                    tNetCarState *pState)
{
  tNetSnapshot snapshot, older = {0}, newer = {0};
  int iHasOlder = 0, iHasNewer = 0;
  for (uint32 uiBack = 0; uiBack < NET_CLIENT_SNAPSHOT_BUFFER; ++uiBack) {
    uint32 uiTick = pStats->uiNewestSnapshotTick - uiBack;
    if (!NetClientSnapshotAt(pClient, uiTick, &snapshot))
      continue;
    if ((float)uiTick <= pStats->fAppliedRenderTick &&
        (!iHasOlder || (int32)(uiTick - older.uiTick) > 0)) {
      older = snapshot;
      iHasOlder = 1;
    }
    if ((float)uiTick >= pStats->fAppliedRenderTick &&
        (!iHasNewer || (int32)(newer.uiTick - uiTick) > 0)) {
      newer = snapshot;
      iHasNewer = 1;
    }
  }
  if (!iHasOlder || !iHasNewer)
    return 0;
  return NetSnapshotInterpolate(&older.aCars[iCar], &newer.aCars[iCar],
      newer.uiTick == older.uiTick ? 1.0f :
      (pStats->fAppliedRenderTick - older.uiTick) /
          (newer.uiTick - older.uiTick),
      pState);
}

static void NetTestSetRemoteHostState(tNetTestHostRun *pRun, uint32 uiTick)
{
  tCar *pCar = &Car[pRun->byRemoteCar];
  tNetWorldPose pose = NetTestRemotePose(pRun, uiTick);
  int iPhase = (int)(((uiTick - pRun->uiStartTick) / 40u) % 3u);
  pCar->nCurrChunk = iPhase == 2 ? -1 : (int16)iPhase;
  pCar->nReferenceChunk = iPhase == 2 ? 1 : (int16)iPhase;
  pCar->iLastValidChunk = iPhase == 2 ? 1 : iPhase;
  CHECK(NetSimWorldToLegacy(&pose, pCar));
  pCar->direction.fX = 1.5f;
  pCar->direction.fY = 0.5f;
  pCar->direction.fZ = 0.25f;
  pCar->fFinalSpeed = 1.75f;
  pCar->fHorizontalSpeed = sqrtf(2.5f);
  pCar->iControlType = iPhase;
}

/* The one world belongs to the client (D13): the host records the tick's
   inputs instead of simulating them. */
static int NetTestHostSimulate(void *pContext, uint32 uiTick,
                               const tCopyData *pInputs, int iNumCars)
{
  tNetTestHostRun *pRun = (tNetTestHostRun *)pContext;
  tNetCarFullState predicted;
  CHECK(iNumCars == numcars && pRun->iTicks < NET_TEST_MAX_TICKS);
  /* The process owns the client world (D13), but NetHostTick builds its
     snapshot immediately after this callback.  Temporarily expose the
     client's recorded state for the host tick so normal traffic is coherent
     and E4-S3 only corrects deliberate disturbances. */
  pRun->byLocalSwapped = 0;
  if (pRun->pClient &&
      NetClientPredictionAt(pRun->pClient, 0, uiTick, &predicted)) {
    NetSimSaveCar(pRun->byCar, &pRun->savedLocalCar);
    pRun->iSavedHumanControl = human_control[pRun->byCar];
    pRun->iSavedFinished = finished_car[pRun->byCar];
    CHECK(NetSnapshotDecodeCarFull(pRun->byCar, &predicted));
    pRun->byLocalSwapped = 1;
  } else if (pRun->pClient && net_puppet_car[pRun->byCar]) {
    tNetWorldPose pose;
    /* There is no second host world in this D13 test.  While the group is
       delayed, move the temporary host pose so the ordinary snapshot puppet
       path still proves that the local car remains visibly live. */
    NetSimSaveCar(pRun->byCar, &pRun->savedLocalCar);
    pRun->iSavedHumanControl = human_control[pRun->byCar];
    pRun->iSavedFinished = finished_car[pRun->byCar];
    CHECK(NetSimLegacyToWorld(&Car[pRun->byCar], &pose));
    pose.position.fX += 4.0f;
    CHECK(NetSimWorldToLegacy(&pose, &Car[pRun->byCar]));
    pRun->byLocalSwapped = 1;
  }
  if (pRun->byRemoteReady)
    NetTestSetRemoteHostState(pRun, uiTick);
  pRun->aTicks[pRun->iTicks].uiTick = uiTick;
  pRun->aTicks[pRun->iTicks].input = pInputs[pRun->byCar].data;
  return 1;
}

static void NetTestRestoreClientWorld(tNetTestHostRun *pRun)
{
  if (!pRun->byLocalSwapped)
    return;
  NetSimRestoreCar(pRun->byCar, &pRun->savedLocalCar);
  human_control[pRun->byCar] = pRun->iSavedHumanControl;
  finished_car[pRun->byCar] = pRun->iSavedFinished;
  pRun->byLocalSwapped = 0;
}

typedef struct
{
  tNetChannel *pHostChannel, *pClientChannel;
  tNetSessionHost *pSessionHost;
  tNetLobbyHost *pLobbyHost;
  tNetHost *pHost;
  tNetConnection *pClientConnection;
  tNetSessionClient *pSessionClient;
  tNetLobbyClient *pLobbyClient;
  tNetClient *pClient;
  tNetTransportSim *pSim;
} tNetTestNodes;

static void NetTestPumpHost(tNetTestNodes *pNodes)
{
  NetChannelPump(pNodes->pHostChannel);
  NetSessionHostPump(pNodes->pSessionHost);
  NetLobbyHostPump(pNodes->pLobbyHost);
  NetHostPump(pNodes->pHost);
}

static void NetTestPumpClient(tNetTestNodes *pNodes)
{
  NetChannelPump(pNodes->pClientChannel);
  NetSessionClientPump(pNodes->pSessionClient);
}

static void NetTestSetLinks(tNetTestNodes *pNodes, uint32 uiLatencyMs)
{
  tNetSimLink link = {0, NET_TEST_JITTER_MS, NET_TEST_LOSS_PERMILLE, 0, 0};
  link.uiLatencyMs = uiLatencyMs;
  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &link));
}

static void NetTestRampStallRecovery(tNetTestNodes *pNodes, uint64 *pullNowMs,
                                     uint16 unTickRateHz, uint8 byCar)
{
  tNetSnapshot snapshot;
  tNetRampState initial, corrupt, expected, actual[NET_MAX_RAMPS];
  tNetClientStats before, after;
  tNetWorldPose parkedWorld, movedWorld;
  tCar parked;
  tCarInputData input;
  uint8 abSnapshot[NET_MAX_PAYLOAD];
  uint32 uiTick = NetClientCurrentTick(pNodes->pClient);
  uint64 ullEndMs = *pullNowMs + 2000u;
  float fWorstPlacementError = 0.0f;
  int iRamp = totalramps;
  int iLength, iDelayTicks;

  {
    tNetSimLink link = {NET_TEST_STEP_LATENCY_MS + NET_TEST_LATENCY_MS,
                        0, 0, 0, 0};
    for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
      CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &link));
  }
  CHECK(totalramps < NET_MAX_RAMPS && TRAK_LEN > 102);
  ramp[totalramps++] = initramp(100, 2, 16, 0, 1, 64, 10, 10, 1024, 63);
  CHECK(ramp[iRamp]);
  NetSimSaveRamps(actual);
  initial = actual[iRamp];

  /* Measure the geometric bound named by 4.6: keep one local ramp pose fixed
     while the ramp advances for one actual interpolation delay. */
  CHECK(NetClientStats(pNodes->pClient, &before));
  parked = Car[(byCar + 1) % numcars];
  parked.nCurrChunk = parked.nReferenceChunk = parked.iLastValidChunk = 100;
  parked.pos.fX = parked.pos.fY = parked.pos.fZ = 0.0f;
  parked.nYaw = parked.nPitch = parked.nRoll = parked.nActualYaw = 0;
  CHECK(NetSimLegacyToWorld(&parked, &parkedWorld));
  iDelayTicks = (int)ceilf(before.fInterpolationDelayMs * unTickRateHz /
                           1000.0f);
  for (int iTick = 0; iTick < iDelayTicks; ++iTick) {
    float fDx, fDy, fDz, fDistance;
    updateramp(ramp[iRamp]);
    CHECK(NetSimLegacyToWorld(&parked, &movedWorld));
    fDx = movedWorld.position.fX - parkedWorld.position.fX;
    fDy = movedWorld.position.fY - parkedWorld.position.fY;
    fDz = movedWorld.position.fZ - parkedWorld.position.fZ;
    fDistance = sqrtf(fDx * fDx + fDy * fDy + fDz * fDz);
    if (fDistance > fWorstPlacementError)
      fWorstPlacementError = fDistance;
  }
  CHECK(NetSimSetRampState(iRamp, &initial));
  CHECK(fWorstPlacementError > 0.0f && isfinite(fWorstPlacementError));

  /* Queue a real full snapshot at the client's matching post-tick boundary,
     then keep pumping transport but stop client frames for two seconds. */
  CHECK(NetSnapshotBuild(&snapshot, uiTick, before.uiLastAppliedEventSeq,
                         (uint8)before.iRaceState,
                         (uint8)before.iPaused));
  iLength = NetSnapshotEncode(&snapshot, abSnapshot, sizeof(abSnapshot));
  CHECK(iLength == (int)sizeof(snapshot));
  CHECK(NetConnectionQueueMessage(
      NetSessionHostPlayerConnection(pNodes->pSessionHost,
                                     NetSessionClientPlayerIndex(
                                         pNodes->pSessionClient)),
      NET_MSG_SNAPSHOT, 0, abSnapshot, (uint16)iLength));
  for (; *pullNowMs <= ullEndMs; ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
  }
  CHECK(NetClientStats(pNodes->pClient, &before));
  CHECK(before.uiNewestSnapshotTick == uiTick);

  /* Disturb timing while stalled.  The first resumed tick must install the
     matching snapshot state, count one correction, then let updatestunts
     advance it exactly once. */
  corrupt = initial;
  CHECK(NetSimAdvanceRampStateCopy(iRamp, &corrupt, 7));
  CHECK(memcmp(&corrupt, &initial, sizeof(corrupt)));
  CHECK(NetSimSetRampState(iRamp, &corrupt));
  NetClientPump(pNodes->pClient);
  CHECK(NetClientTicksDue(pNodes->pClient) > 0);
  input = NetTestScript(uiTick + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
  expected = initial;
  CHECK(NetSimAdvanceRampStateCopy(iRamp, &expected, 1));
  NetSimSaveRamps(actual);
  CHECK(!memcmp(&actual[iRamp], &expected, sizeof(expected)));
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiRampCorrections == before.uiRampCorrections + 1u);
  CHECK(g_netStats.iRampCorrections == (int)after.uiRampCorrections);
  printf("%u Hz: 2 s stall ramp recovery, one correction; %.3f-unit "
         "worst placement error over %.1f ms\n", unTickRateHz,
         (double)fWorstPlacementError, (double)before.fInterpolationDelayMs);
}

static void NetTestDeliverRaceMessages(tNetTestNodes *pNodes,
                                       uint64 *pullNowMs,
                                       uint16 unTickRateHz)
{
  uint64 ullEndMs = *pullNowMs + 1000u / unTickRateHz + 3u;
  NetTestPumpHost(pNodes);
  for (; *pullNowMs <= ullEndMs; ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
  }
  NetClientPump(pNodes->pClient);
  CHECK(NetClientTicksDue(pNodes->pClient) > 0);
}

static void NetTestQueueSyntheticState(tNetTestNodes *pNodes,
                                       uint32 uiBaseTick, uint32 uiTick,
                                       uint8 byCar, float fLateralPush,
                                       float fLapTimePush, int iSendOwn,
                                       tNetCarFullState *pHostState)
{
  tNetConnection *pConnection = NetSessionHostPlayerConnection(
      pNodes->pSessionHost,
      NetSessionClientPlayerIndex(pNodes->pSessionClient));
  tNetSnapshot snapshot;
  tNetCarFullState predicted;
  uint8 abMessage[NET_MAX_PAYLOAD];
  uint8 abyCars[1] = {byCar};
  int iLength;
  CHECK(NetClientSnapshotAt(pNodes->pClient, uiBaseTick, &snapshot));
  CHECK(NetClientPredictionAt(pNodes->pClient, 0, uiTick, &predicted));
  for (int iRamp = 0; iRamp < snapshot.byNumRamps; ++iRamp)
    CHECK(NetSimAdvanceRampStateCopy(
        iRamp, &snapshot.aRamps[iRamp], (int)(uiTick - uiBaseTick)));
  snapshot.uiTick = uiTick;
  snapshot.aCars[byCar] = predicted.state;
  snapshot.aCars[byCar].fWorldPosY += fLateralPush;
  predicted.state = snapshot.aCars[byCar];
  predicted.extra.fRunningLapTime += fLapTimePush;
  CHECK(NetSnapshotCarFullValid(byCar, &predicted));
  iLength = NetSnapshotEncode(&snapshot, abMessage, sizeof(abMessage));
  CHECK(iLength == (int)sizeof(snapshot));
  CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0,
                                  abMessage, (uint16)iLength));
  if (iSendOwn) {
    iLength = NetSnapshotEncodeOwnCarState(
        uiTick, abyCars, &predicted.extra, 1, abMessage, sizeof(abMessage));
    CHECK(iLength > 0);
    CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_OWN_CAR_STATE, 0,
                                    abMessage, (uint16)iLength));
  }
  if (pHostState)
    *pHostState = predicted;
}

static void NetTestCorrections(tNetTestNodes *pNodes, uint64 *pullNowMs,
                               uint16 unTickRateHz, uint8 bySnapshotInterval,
                               uint8 byCar)
{
  tNetSimLink link = {0, 0, 0, 0, 0};
  tNetClientStats before, after;
  tNetCarFullState host, live, recorded;
  tNetCorrection correction;
  tCarInputData input;
  uint32 uiBaseTick, uiTick;
  int iDepth;
  double dCorrectionMs;
  clock_t tickStarted;
  /* Let every packet from the steady-state link arrive, then advance only
     the client.  This creates a clean 15-tick retained span with no newer
     automatic host snapshot waiting to supersede the synthetic one. */
  {
    uint64 ullEndMs = *pullNowMs + 400u;
    for (; *pullNowMs <= ullEndMs; ++*pullNowMs) {
      CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
      NetTestPumpHost(pNodes);
      NetTestPumpClient(pNodes);
    }
    NetClientPump(pNodes->pClient);
    while (NetClientTicksDue(pNodes->pClient) > 0) {
      input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u,
                            byCar);
      CHECK(NetClientTick(pNodes->pClient, &input));
    }
  }
  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &link));

  CHECK(NetClientStats(pNodes->pClient, &before));
  CHECK(before.uiCorrections * 100u < before.uiTicks);
  printf("%u Hz: steady client had %u corrections in %u ticks; "
         "latest magnitude %.3f\n", unTickRateHz, before.uiCorrections,
         before.uiTicks, (double)before.fCorrectionMagnitude);
  uiBaseTick = before.uiNewestSnapshotTick;
  uiTick = NetClientCurrentTick(pNodes->pClient) - 15u;
  CHECK((int32)(uiTick - uiBaseTick) > 0);
  iDepth = (int)(NetClientCurrentTick(pNodes->pClient) - uiTick);
  NetTestQueueSyntheticState(pNodes, uiBaseTick, uiTick, byCar, 2.0f,
                             0.0f, 1, &host);
  NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
  input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
  tickStarted = clock();
  CHECK(NetClientTick(pNodes->pClient, &input));
  dCorrectionMs = 1000.0 * (clock() - tickStarted) / CLOCKS_PER_SEC;
  CHECK(NetClientStats(pNodes->pClient, &after));
  if (after.uiCorrections != before.uiCorrections + 1u)
    fprintf(stderr, "forced correction: tick %u current %u newest %u, "
            "corrections %u -> %u, deferred %u -> %u, rejected %u\n",
            uiTick, NetClientCurrentTick(pNodes->pClient),
            after.uiNewestSnapshotTick, before.uiCorrections,
            after.uiCorrections, before.uiDeferredCorrections,
            after.uiDeferredCorrections, after.uiRejectedMessages);
  CHECK(after.uiCorrections == before.uiCorrections + 1u);
  CHECK(after.uiReplayTicksTotal ==
        before.uiReplayTicksTotal + (uint32)iDepth);
  CHECK(after.iReplayDepth == iDepth);
  CHECK(after.fCorrectionMagnitude > NET_CLIENT_POSITION_TOLERANCE);
  CHECK(NetSnapshotEncodeCarFull(byCar, &live));
  CHECK(NetClientPredictionAt(pNodes->pClient, 0,
                              NetClientCurrentTick(pNodes->pClient),
                              &recorded));
  CHECK(!memcmp(&live, &recorded, sizeof(live)));
  CHECK(NetSimRenderCorrectionAt(byCar, &correction));
  CHECK(correction.iTicksRemaining == NET_CLIENT_CORRECTION_TICKS);

  /* Authoritative-only drift is displayed immediately but never causes a
     rollback. */
  before = after;
  uiBaseTick = uiTick;
  uiTick += bySnapshotInterval;
  CHECK((int32)(NetClientCurrentTick(pNodes->pClient) - uiTick) >= 0);
  NetTestQueueSyntheticState(pNodes, uiBaseTick, uiTick, byCar, 0.0f,
                             0.5f, 1, &host);
  NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
  CHECK(Car[byCar].fRunningLapTime == host.extra.fRunningLapTime);
  input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiCorrections == before.uiCorrections);

  /* A snapshot cannot mutate through the correction path without its own-car
     extra.  Count the defer once, then correct when the pair arrives. */
  before = after;
  uiBaseTick = uiTick;
  uiTick += bySnapshotInterval;
  CHECK((int32)(NetClientCurrentTick(pNodes->pClient) - uiTick) >= 0);
  NetTestQueueSyntheticState(pNodes, uiBaseTick, uiTick, byCar, 2.0f,
                             0.0f, 0, &host);
  NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
  input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiDeferredCorrections == before.uiDeferredCorrections + 1u);
  CHECK(after.uiCorrections == before.uiCorrections);
  {
    tNetConnection *pConnection = NetSessionHostPlayerConnection(
        pNodes->pSessionHost,
        NetSessionClientPlayerIndex(pNodes->pSessionClient));
    uint8 abMessage[NET_MAX_PAYLOAD];
    uint8 abyCars[1] = {byCar};
    int iLength = NetSnapshotEncodeOwnCarState(
        uiTick, abyCars, &host.extra, 1, abMessage, sizeof(abMessage));
    CHECK(iLength > 0);
    CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_OWN_CAR_STATE, 0,
                                    abMessage, (uint16)iLength));
  }
  NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
  input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiCorrections == before.uiCorrections + 1u);

  /* Cost sample: force one 14-tick correction for each synthetic snapshot.
     Timing is accumulated so sub-millisecond replays survive clock()
     granularity on Windows. */
  {
    const int iCostCorrections = 100;
    tNetClientStats costBefore, costAfter;
    clock_t costTicks = 0;
    double dCostMs;
    uiBaseTick = uiTick;
    CHECK(NetClientStats(pNodes->pClient, &costBefore));
    for (int iCorrection = 0; iCorrection < iCostCorrections; ++iCorrection) {
      clock_t correctionStarted;
      uiTick = uiBaseTick + 1u;
      CHECK((int32)(NetClientCurrentTick(pNodes->pClient) - uiTick) > 0);
      NetTestQueueSyntheticState(pNodes, uiBaseTick, uiTick, byCar, 2.0f,
                                 0.0f, 1, NULL);
      NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
      input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u,
                            byCar);
      correctionStarted = clock();
      CHECK(NetClientTick(pNodes->pClient, &input));
      costTicks += clock() - correctionStarted;
      uiBaseTick = uiTick;
    }
    dCostMs = 1000.0 * costTicks / CLOCKS_PER_SEC;
    CHECK(NetClientStats(pNodes->pClient, &costAfter));
    CHECK(costAfter.uiCorrections ==
          costBefore.uiCorrections + (uint32)iCostCorrections);
    printf("%u Hz: %d forced 14-tick corrections averaged %.3f ms "
           "(%.2f%% of one tick)\n", unTickRateHz, iCostCorrections,
           dCostMs / iCostCorrections,
           100.0 * (dCostMs / iCostCorrections) * unTickRateHz / 1000.0);
  }
  printf("%u Hz: forced %d-tick correction in %.3f ms, "
         "authoritative-only no-op, and one deferred correction\n",
         unTickRateHz, iDepth, dCorrectionMs);
}

static void NetTestContinueRace(tNetTestNodes *pNodes, uint64 *pullNowMs,
                                uint64 ullDurationMs, uint64 ullReleaseMs,
                                int *piHostTickIndex,
                                uint16 unTickRateHz, uint8 byCar)
{
  uint64 ullStartMs = *pullNowMs;
  uint64 ullEndMs = ullStartMs + ullDurationMs;
  uint64 ullFrame = 0, ullNextFrameMs = ullStartMs;
  for (; *pullNowMs <= ullEndMs; ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    while (*pullNowMs >= ullReleaseMs +
           (uint64)*piHostTickIndex * 1000u / unTickRateHz) {
      uint32 uiTick = NetHostNextTick(pNodes->pHost);
      CHECK(s_hostRun.iTicks < NET_TEST_MAX_TICKS);
      CHECK(NetHostTick(pNodes->pHost, uiTick));
      CHECK(NetHostLastEventSeq(pNodes->pHost) == 1u);
      NetTestRestoreClientWorld(&s_hostRun);
      ++s_hostRun.iTicks;
      ++*piHostTickIndex;
    }
    if (*pullNowMs < ullNextFrameMs)
      continue;
    ++ullFrame;
    ullNextFrameMs = ullStartMs + ullFrame * NET_TEST_FRAME_NUMERATOR / 3u;
    NetTestPumpClient(pNodes);
    NetClientPump(pNodes->pClient);
    while (NetClientTicksDue(pNodes->pClient) > 0) {
      tCarInputData input = NetTestScript(
          NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
      CHECK(NetClientTick(pNodes->pClient, &input));
    }
  }
}

static void NetTestPredictionModes(tNetTestNodes *pNodes,
                                   uint64 *pullNowMs,
                                   uint64 ullReleaseMs,
                                   int *piHostTickIndex,
                                   uint16 unTickRateHz, uint8 byCar)
{
  tNetSimLink high = {300, 0, 0, 0, 0}; /* 600 ms RTT */
  tNetSimLink low = {60, 0, 0, 0, 0};   /* 120 ms RTT */
  tNetClientStats before, entered, held, recovered;
  tNetWorldPose startPose, endPose;
  uint32 uiEnteredTick;
  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &high));
  CHECK(NetClientStats(pNodes->pClient, &before));
  NetTestContinueRace(pNodes, pullNowMs, 5000u, ullReleaseMs,
                      piHostTickIndex, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &entered));
  CHECK(entered.iPredictionMode == NET_PREDICT_DELAYED);
  CHECK(entered.iPredictionTransitions == before.iPredictionTransitions + 1);
  CHECK(net_puppet_car[byCar]);
  CHECK(!NetClientPredictionAt(pNodes->pClient, 0,
                               NetClientCurrentTick(pNodes->pClient),
                               &(tNetCarFullState){0}));
  CHECK(NetSimLegacyToWorld(&Car[byCar], &startPose));
  uiEnteredTick = NetClientCurrentTick(pNodes->pClient);
  NetTestContinueRace(pNodes, pullNowMs, 20000u, ullReleaseMs,
                      piHostTickIndex, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &held));
  CHECK(held.iPredictionMode == NET_PREDICT_DELAYED);
  CHECK(held.iPredictionTransitions == entered.iPredictionTransitions);
  CHECK(held.uiReplayTicksTotal == entered.uiReplayTicksTotal);
  CHECK(held.uiBatchesSent - entered.uiBatchesSent ==
        held.uiTicks - entered.uiTicks);
  CHECK(NetClientCurrentTick(pNodes->pClient) > uiEnteredTick);
  CHECK(NetSimLegacyToWorld(&Car[byCar], &endPose));
  CHECK(fabsf(endPose.position.fX - startPose.position.fX) +
        fabsf(endPose.position.fY - startPose.position.fY) +
        fabsf(endPose.position.fZ - startPose.position.fZ) > 1.0f);

  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &low));
  NetTestContinueRace(pNodes, pullNowMs, 5000u, ullReleaseMs,
                      piHostTickIndex, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &recovered));
  CHECK(recovered.iPredictionMode == NET_PREDICT_FULL);
  CHECK(recovered.iPredictionTransitions ==
        entered.iPredictionTransitions + 1);
  CHECK(!net_puppet_car[byCar]);
  CHECK(recovered.uiRampTick == NetClientCurrentTick(pNodes->pClient));
  CHECK(recovered.iReplayDepth <= recovered.iReplayBudgetTicks);
  CHECK(recovered.uiTimeDegradedMs >= 20000u);
  printf("%u Hz: delayed mode held 20 s at 600 ms RTT with zero replay, "
         "then recovered at 120 ms RTT\n", unTickRateHz);
}

/* One client tick, with every per-tick ring check the acceptance asks for. */
static void NetTestClientTick(tNetClient *pClient, uint8 byCar)
{
  tNetSimTickContext entering, recorded;
  tNetCarFullState live, predicted;
  tCarInputData input, canonical, ringInput;
  uint32 uiPrevious = NetClientCurrentTick(pClient);
  uint32 uiTick = uiPrevious + 1u;

  /* context[N] is what the simulation enters tick N + 1 with (4.3 step 7). */
  if (NetClientContextAt(pClient, uiPrevious, &recorded)) {
    NetSimCaptureContext(&entering);
    CHECK(!memcmp(&entering, &recorded, sizeof(entering)));
  }

  input = NetTestScript(uiTick, byCar);
  canonical = input;
  NetSimCanonicaliseInput(&canonical);
  CHECK(NetClientTick(pClient, &input));
  CHECK(NetClientCurrentTick(pClient) == uiTick);

  CHECK(NetClientInputAt(pClient, uiTick, &ringInput));
  CHECK(!memcmp(&ringInput, &canonical, sizeof(canonical)));
  CHECK(NetSnapshotEncodeCarFull(byCar, &live));
  CHECK(NetClientPredictionAt(pClient, 0, uiTick, &predicted));
  CHECK(!memcmp(&live, &predicted, sizeof(live)));
  CHECK(NetClientContextAt(pClient, uiTick, &recorded) && recorded.iGameFrame == game_frame);

  /* The remote script is continuous in world space while its wire frame
     alternates between two banked chunks and airborne.  Any frame-conversion
     discontinuity therefore appears as a placement error or speed spike. */
  {
    tNetClientStats stats;
    CHECK(NetClientStats(pClient, &stats));
    CHECK(stats.uiPuppetHookCalls ==
          stats.uiTicks + stats.uiReplayTicksTotal);
    CHECK(!net_puppet_car[byCar]);
    CHECK(net_puppet_car[s_hostRun.byRemoteCar]);
    if (stats.uiPuppetApplications) {
      tNetWorldPose actual;
      tNetWorldPose expected = NetTestRemotePose(&s_hostRun,
                                                  stats.fAppliedRenderTick);
      float fDx, fDy, fDz, fError;
      CHECK(NetSimLegacyToWorld(&Car[s_hostRun.byRemoteCar], &actual));
      fDx = actual.position.fX - expected.position.fX;
      fDy = actual.position.fY - expected.position.fY;
      fDz = actual.position.fZ - expected.position.fZ;
      fError = sqrtf(fDx * fDx + fDy * fDy + fDz * fDz);
      if (fError >= 0.35f)
        fprintf(stderr, "puppet tick %.3f error %.3f (%f %f %f), chunk %d, "
                "stalled %u\n", (double)stats.fAppliedRenderTick,
                (double)fError, (double)fDx, (double)fDy, (double)fDz,
                Car[s_hostRun.byRemoteCar].nCurrChunk, stats.byStalled);
      CHECK(fError < 0.35f);
      /* Stall extrapolation advances position by velocity and deliberately
         holds orientation.  Bracketed interpolation advances both. */
      if (!stats.byStalled) {
        tNetCarState expectedState;
        /* One unit can be lost in each of host encode, interpolation
           rounding, and client world/local/world conversion. */
        CHECK(NetTestInterpolatedState(pClient, &stats,
                                       s_hostRun.byRemoteCar, &expectedState));
        CHECK(NetTestAngleDifference(actual.nYaw,
                                     expectedState.nWorldYaw) <= 4);
        CHECK(NetTestAngleDifference(actual.nPitch,
                                     expectedState.nWorldPitch) <= 4);
        CHECK(NetTestAngleDifference(actual.nRoll,
                                     expectedState.nWorldRoll) <= 4);
      }
      if (stats.fAppliedRenderTick > s_hostRun.uiStartTick + 100.0f) {
        if (s_hostRun.iPuppetSamples &&
            stats.fAppliedRenderTick > s_hostRun.fLastPuppetTick) {
          float fTickSpan = stats.fAppliedRenderTick - s_hostRun.fLastPuppetTick;
          float fStepX = actual.position.fX - s_hostRun.lastPuppetPose.position.fX;
          float fStepY = actual.position.fY - s_hostRun.lastPuppetPose.position.fY;
          float fStepZ = actual.position.fZ - s_hostRun.lastPuppetPose.position.fZ;
          float fStep = sqrtf(fStepX * fStepX + fStepY * fStepY +
                              fStepZ * fStepZ) / fTickSpan;
          if (fStep > s_hostRun.fMaxPuppetStepPerTick)
            s_hostRun.fMaxPuppetStepPerTick = fStep;
        }
        s_hostRun.lastPuppetPose = actual;
        s_hostRun.fLastPuppetTick = stats.fAppliedRenderTick;
        ++s_hostRun.iPuppetSamples;
        if (Car[s_hostRun.byRemoteCar].nCurrChunk == 0)
          s_hostRun.iSawChunk0 = 1;
        else if (Car[s_hostRun.byRemoteCar].nCurrChunk == 1)
          s_hostRun.iSawChunk1 = 1;
        else if (Car[s_hostRun.byRemoteCar].nCurrChunk == -1)
          s_hostRun.iSawAirborne = 1;
      }
    }
  }
}

static void NetTestQueueEvent(tNetConnection *pConnection,
                              const tNetEvent *pEvent)
{
  uint8 abEvent[sizeof(tNetEvent)];
  int iLength = NetEventEncode(pEvent, numcars, NET_SESSION_MAX_PLAYERS,
                               abEvent, sizeof(abEvent));
  CHECK(iLength == (int)sizeof(tNetEvent));
  CHECK(NetConnectionQueueMessage(
      pConnection, NET_MSG_EVENT, NET_MSG_RELIABLE | NET_MSG_ORDERED,
      abEvent, (uint16)iLength));
}

static void NetTestQueuePause(tNetConnection *pConnection,
                              uint16 unRevision, int iPaused,
                              uint32 uiTick)
{
  tNetPause pause;
  uint8 abPause[sizeof(tNetPause)];
  int iLength;
  memset(&pause, 0, sizeof(pause));
  pause.unPauseRevision = unRevision;
  pause.byPaused = (uint8)(iPaused != 0);
  pause.uiTick = uiTick;
  iLength = NetPauseEncode(&pause, abPause, sizeof(abPause));
  CHECK(iLength == (int)sizeof(tNetPause));
  CHECK(NetConnectionQueueMessage(
      pConnection, NET_MSG_PAUSE, NET_MSG_RELIABLE | NET_MSG_ORDERED,
      abPause, (uint16)iLength));
}

static void NetTestPause(tNetTestNodes *pNodes, uint64 *pullNowMs,
                         uint16 unTickRateHz, uint8 byCar)
{
  tNetConnection *pHostConnection = NetSessionHostPlayerConnection(
      pNodes->pSessionHost,
      NetSessionClientPlayerIndex(pNodes->pSessionClient));
  tNetSimLink loss = {NET_TEST_LATENCY_MS, 0, 50, 0, 0};
  tNetClientStats before, after;
  tNetSimTickContext beforeContext, afterContext;
  tCarInputData input;
  uint64 ullPauseEnd = *pullNowMs + 30000u;
  uint32 uiHostTick, uiClientTick;

  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &loss));
  CHECK(NetClientStats(pNodes->pClient, &before));
  uiHostTick = NetHostNextTick(pNodes->pHost);
  uiClientTick = NetClientCurrentTick(pNodes->pClient);
  CHECK(NetClientContextAt(pNodes->pClient, uiClientTick, &beforeContext));
  CHECK(NetHostSetPaused(pNodes->pHost, 1));
  CHECK(NetHostPaused(pNodes->pHost) && NetHostPauseRevision(pNodes->pHost) == 1);
  CHECK(!NetHostTick(pNodes->pHost, uiHostTick));

  for (; *pullNowMs <= ullPauseEnd; ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
    if (!(*pullNowMs % 16u))
      NetClientPump(pNodes->pClient);
  }
  NetClientPump(pNodes->pClient);
  CHECK(NetClientPaused(pNodes->pClient));
  CHECK(NetClientPauseRevision(pNodes->pClient) == 1);
  CHECK(!NetClientTicksDue(pNodes->pClient));
  CHECK(NetHostNextTick(pNodes->pHost) == uiHostTick);
  CHECK(NetClientCurrentTick(pNodes->pClient) == uiClientTick);
  CHECK(!NetConnectionIsExpired(pHostConnection));
  CHECK(!NetConnectionIsExpired(pNodes->pClientConnection));

  CHECK(NetHostSetPaused(pNodes->pHost, 0));
  CHECK(!NetHostPaused(pNodes->pHost) && NetHostPauseRevision(pNodes->pHost) == 2);
  for (uint64 ullEnd = *pullNowMs + 1000u;
       *pullNowMs <= ullEnd && NetClientPaused(pNodes->pClient);
       ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
    NetClientPump(pNodes->pClient);
  }
  CHECK(!NetClientPaused(pNodes->pClient));
  while (!NetClientTicksDue(pNodes->pClient)) {
    ++*pullNowMs;
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
    NetClientPump(pNodes->pClient);
  }
  input = NetTestScript(uiClientTick + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
  CHECK(NetClientCurrentTick(pNodes->pClient) == uiClientTick + 1u);
  CHECK(NetClientContextAt(pNodes->pClient, uiClientTick + 1u,
                           &afterContext));
  CHECK(afterContext.iGameFrame == beforeContext.iGameFrame + 1);

  /* Drive the authority to revision 4, then deliver a delayed revision 3.
     The stale pause must not undo the newer unpause. */
  CHECK(NetHostSetPaused(pNodes->pHost, 1));
  CHECK(NetHostSetPaused(pNodes->pHost, 0));
  CHECK(NetHostPauseRevision(pNodes->pHost) == 4);
  NetTestQueuePause(pHostConnection, 3, 1, NetHostNextTick(pNodes->pHost));
  for (uint64 ullEnd = *pullNowMs + 1500u; *pullNowMs <= ullEnd;
       ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
    NetClientPump(pNodes->pClient);
  }
  CHECK(!NetClientPaused(pNodes->pClient));
  CHECK(NetClientPauseRevision(pNodes->pClient) == 4);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiPauseChanges == before.uiPauseChanges + 4u);
  CHECK(after.uiStaleMessages >= before.uiStaleMessages + 1u);
  printf("%u Hz: 30 s pause at 5%% loss kept both connections alive; "
         "revision 3 ignored after revision 4\n", unTickRateHz);
}

static void NetTestRunCommitTick(tNetTestNodes *pNodes,
                                 uint64 *pullNowMs,
                                 uint16 unTickRateHz, uint8 byCar)
{
  tCarInputData input;
  NetTestDeliverRaceMessages(pNodes, pullNowMs, unTickRateHz);
  input = NetTestScript(NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
  CHECK(NetClientTick(pNodes->pClient, &input));
}

static void NetTestHostCommits(tNetTestNodes *pNodes, uint64 *pullNowMs,
                               uint16 unTickRateHz, uint8 byCar)
{
  tNetConnection *pConnection = NetSessionHostPlayerConnection(
      pNodes->pSessionHost,
      NetSessionClientPlayerIndex(pNodes->pSessionClient));
  tNetClientStats before, after;
  tNetSnapshot snapshot, olderSnapshot;
  tNetEvent aEvents[9];
  tNetWorldChangeEntry aBefore[MAX_TRACK_CHUNKS];
  tNetWorldChangeEntry aWorld[16];
  tNetSimLink link = {0, 0, 0, 0, 0};
  tCar savedCar;
  uint8 abMessage[NET_MAX_MESSAGE_SIZE];
  uint32 uiSnapshotTick;
  uint32 uiBaseEventSeq;
  int iSavedAuthority, iLength;
  int aiRemote[3], iRemoteCount = 0;

  /* Drain the old-latency link before introducing the synthetic watermark,
     then make the ordering of each focused probe exact. */
  for (uint64 ullEndMs = *pullNowMs + 200u;
       *pullNowMs <= ullEndMs; ++*pullNowMs) {
    CHECK(NetTransportSimAdvance(pNodes->pSim, *pullNowMs));
    NetTestPumpHost(pNodes);
    NetTestPumpClient(pNodes);
    NetClientPump(pNodes->pClient);
    while (NetClientTicksDue(pNodes->pClient) > 0) {
      tCarInputData input = NetTestScript(
          NetClientCurrentTick(pNodes->pClient) + 1u, byCar);
      CHECK(NetClientTick(pNodes->pClient, &input));
    }
  }
  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pNodes->pSim, iEndpoint, &link));
  for (int iCar = 0; iCar < numcars && iRemoteCount < 3; ++iCar)
    if (iCar != byCar)
      aiRemote[iRemoteCount++] = iCar;
  CHECK(iRemoteCount == 3 && pConnection);
  CHECK(!finishers && !human_finishers && !Destroyed);

  /* D14: a locally predicted crossing cannot commit a result.  Restoring
     the car models the host's one-tick-later correction/teleport. */
  NetSimSaveCar(byCar, &savedCar);
  Car[byCar].nCurrChunk = 0;
  Car[byCar].nReferenceChunk = (int16)(TRAK_LEN - 1);
  Car[byCar].byLap = Car[byCar].byLapNumber = (uint8)NoOfLaps;
  iSavedAuthority = net_sim_authority;
  net_sim_authority = NET_AUTHORITY_REMOTE;
  check_crossed_line(&Car[byCar]);
  net_sim_authority = iSavedAuthority;
  CHECK(!finished_car[byCar] && !human_finishers && !finishers);
  NetSimRestoreCar(byCar, &savedCar);

  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk)
    CHECK(NetWorldChangeCapture(iChunk, &aBefore[iChunk]));
  for (int iEntry = 0; iEntry < 16; ++iEntry) {
    aWorld[iEntry] = aBefore[iEntry];
    aWorld[iEntry].byCenterGrip = 12;
    aWorld[iEntry].byLeftShoulderGrip = 12;
    aWorld[iEntry].byRightShoulderGrip = 12;
    for (int iLane = 0; iLane < 3; ++iLane) {
      uint32 uiFlags = aBefore[iEntry].auiTrakColour[iLane] &
                       SURFACE_MASK_FLAGS;
      aWorld[iEntry].auiTrakColour[iLane] =
          (uiFlags ^ SURFACE_FLAG_APPLY_TEXTURE) |
          (uint32)(159 - (iEntry & 7));
    }
  }

  memset(aEvents, 0, sizeof(aEvents));
  aEvents[0].uiEventSeq = 1;
  aEvents[0].byType = NET_EV_LAP_COMPLETE;
  aEvents[0].byCarIdx = (uint8)aiRemote[0];
  aEvents[0].byPlayerIdx = NET_EVENT_NO_PLAYER;
  aEvents[0].iArg0 = 1;
  aEvents[0].iArg1 = 12345;
  aEvents[1] = aEvents[0];
  aEvents[1].uiEventSeq = 2;
  aEvents[1].iArg0 = 2;
  aEvents[1].iArg1 = 22345;
  aEvents[2] = aEvents[0];
  aEvents[2].uiEventSeq = 3;
  aEvents[2].iArg0 = 3;
  aEvents[2].iArg1 = 32345;
  aEvents[3].uiEventSeq = 4;
  aEvents[3].byType = NET_EV_KILL;
  aEvents[3].byCarIdx = (uint8)aiRemote[1];
  aEvents[3].byPlayerIdx = NET_EVENT_NO_PLAYER;
  aEvents[3].iArg0 = aiRemote[2];
  aEvents[3].iArg1 = 3;
  aEvents[4].uiEventSeq = 5;
  aEvents[4].byType = NET_EV_FINISHED;
  aEvents[4].byCarIdx = byCar;
  aEvents[4].byPlayerIdx = NetSessionClientPlayerIndex(
      pNodes->pSessionClient);
  aEvents[4].iArg0 = 0;
  aEvents[4].iArg1 = 1;
  aEvents[5].uiEventSeq = 6;
  aEvents[5].byType = NET_EV_DESTROYED;
  aEvents[5].byCarIdx = (uint8)aiRemote[2];
  aEvents[5].byPlayerIdx = NET_EVENT_NO_PLAYER;
  aEvents[5].iArg0 = aiRemote[1];
  aEvents[5].iArg1 = 1;

  CHECK(NetClientStats(pNodes->pClient, &before));
  CHECK(before.uiCommitWatermark == before.uiLastAppliedEventSeq);
  CHECK(before.iRaceState == NET_RACE_RUNNING);
  uiBaseEventSeq = before.uiLastAppliedEventSeq;
  for (int iEvent = 0; iEvent < 6; ++iEvent)
    aEvents[iEvent].uiEventSeq += uiBaseEventSeq;
  aEvents[6].uiEventSeq = uiBaseEventSeq + 7u;
  aEvents[6].byType = NET_EV_AI_TAKEOVER;
  aEvents[6].byCarIdx = (uint8)aiRemote[0];
  aEvents[6].byPlayerIdx = 1;
  aEvents[6].iArg0 = -1;
  aEvents[6].iArg1 = 1;
  aEvents[7].uiEventSeq = uiBaseEventSeq + 8u;
  aEvents[7].byType = NET_EV_RACE_STATE;
  aEvents[7].byCarIdx = NET_EVENT_NO_CAR;
  aEvents[7].byPlayerIdx = NET_EVENT_NO_PLAYER;
  aEvents[7].iArg0 = NET_RACE_OUTCOME_SETTLED;
  aEvents[8].uiEventSeq = uiBaseEventSeq + 9u;
  aEvents[8].byType = NET_EV_RESULTS;
  aEvents[8].byCarIdx = NET_EVENT_NO_CAR;
  aEvents[8].byPlayerIdx = NET_EVENT_NO_PLAYER;
  aEvents[8].iArg0 = 2;
  aEvents[8].iArg1 = 1;
  CHECK(NetClientSnapshotAt(pNodes->pClient,
                            before.uiNewestSnapshotTick, &snapshot));
  /* Model the last pre-drop snapshot.  The retained takeover commit must
     win again after this older ownership state is applied. */
  human_control[aiRemote[0]] = 1;
  snapshot.aCars[aiRemote[0]].byHumanControl = 1;
  olderSnapshot = snapshot;
  uiSnapshotTick = before.uiNewestSnapshotTick + 2u;
  for (int iRamp = 0; iRamp < snapshot.byNumRamps; ++iRamp)
    CHECK(NetSimAdvanceRampStateCopy(iRamp, &snapshot.aRamps[iRamp], 2));
  snapshot.uiTick = uiSnapshotTick;
  snapshot.uiLastEventSeq = uiBaseEventSeq + 10u;
  snapshot.byRaceState = NET_RACE_OUTCOME_SETTLED;
  snapshot.aCars[aiRemote[0]].byLap = 4;
  snapshot.aCars[byCar].byRacePosition = 0;
  snapshot.aCars[aiRemote[2]].byRacePosition = (uint8)(numcars - 1);
  iLength = NetSnapshotEncode(&snapshot, abMessage, sizeof(abMessage));
  CHECK(iLength == (int)sizeof(snapshot));
  CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0,
                                  abMessage, (uint16)iLength));
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiCommitWatermark == uiBaseEventSeq + 10u &&
        after.uiCommitsApplied == before.uiCommitsApplied);

  /* Retain the later lifecycle/result commits behind the first missing car
     commit.  Neither result globals nor the world
     may advance across the shared-sequence gap. */
  for (int iEvent = 1; iEvent < 9; ++iEvent) {
    aEvents[iEvent].uiTick = uiSnapshotTick;
    NetTestQueueEvent(pConnection, &aEvents[iEvent]);
  }
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiCommitsApplied == before.uiCommitsApplied &&
        !finishers && !human_finishers &&
        !Destroyed && !Car[aiRemote[1]].byKills);
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
    tNetWorldChangeEntry current;
    CHECK(NetWorldChangeCapture(iChunk, &current));
    CHECK(NetWorldChangeEntryEqual(&current, &aBefore[iChunk]));
  }

  aEvents[0].uiTick = uiSnapshotTick;
  NetTestQueueEvent(pConnection, &aEvents[0]);
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiEvents == before.uiEvents + 9u);
  CHECK(after.uiCommitsApplied == before.uiCommitsApplied + 9u);
  CHECK(after.uiLastAppliedEventSeq == uiBaseEventSeq + 9u);
  CHECK(Car[aiRemote[0]].byLap == 4 &&
        Car[aiRemote[0]].byLapNumber >= 4);
  CHECK(Car[aiRemote[1]].byKills == 3);
  CHECK(finished_car[byCar] && finished_car[aiRemote[2]]);
  CHECK(Car[byCar].byRacePosition == 0 && carorder[0] == byCar);
  CHECK(finishers == 2 && human_finishers == 1 && Destroyed == 1);
  CHECK(!human_control[aiRemote[0]]);
  CHECK(NetClientRaceState(pNodes->pClient) == NET_RACE_OUTCOME_SETTLED);
  {
    int iResultFinishers, iResultHumanFinishers;
    CHECK(NetClientResults(pNodes->pClient, &iResultFinishers,
                           &iResultHumanFinishers));
    CHECK(iResultFinishers == 2 && iResultHumanFinishers == 1);
  }

  /* Result publication is idempotent even if a later movement correction
     restores a context recorded before these reliable commits arrived. */
  finishers = human_finishers = Destroyed = 0;
  human_control[aiRemote[0]] = 1;
  finished_car[byCar] = finished_car[aiRemote[2]] = 0;
  Car[aiRemote[0]].byLap = Car[aiRemote[0]].byLapNumber = 0;
  Car[aiRemote[1]].byKills = 0;
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(finishers == 2 && human_finishers == 1 && Destroyed == 1);
  CHECK(finished_car[byCar] && finished_car[aiRemote[2]]);
  CHECK(Car[aiRemote[0]].byLap == 4 && Car[aiRemote[1]].byKills == 3);
  CHECK(!human_control[aiRemote[0]]);

  iLength = NetWorldChangeEncode(uiBaseEventSeq + 10u, uiSnapshotTick,
                                 aWorld, 16,
                                 abMessage, sizeof(abMessage));
  CHECK(iLength > 0);
  CHECK(NetConnectionQueueMessage(
      pConnection, NET_MSG_WORLD_CHANGE,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, abMessage, (uint16)iLength));
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiWorldChanges == before.uiWorldChanges + 1u);
  CHECK(after.uiCommitsApplied == before.uiCommitsApplied + 10u);
  CHECK(after.uiLastAppliedEventSeq == after.uiCommitWatermark &&
        after.uiCommitWatermark == uiBaseEventSeq + 10u);
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
    tNetWorldChangeEntry current;
    CHECK(NetWorldChangeCapture(iChunk, &current));
    if (iChunk < 16)
      CHECK(NetWorldChangeEntryEqual(&current, &aWorld[iChunk]));
    else
      CHECK(NetWorldChangeEntryEqual(&current, &aBefore[iChunk]));
  }

  /* An ordinary reordered snapshot from before the commits has a lower
     watermark.  It remains usable for interpolation and must not be treated
     as a hostile regression of the newest snapshot. */
  iLength = NetSnapshotEncode(&olderSnapshot, abMessage, sizeof(abMessage));
  CHECK(iLength == (int)sizeof(olderSnapshot));
  CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0,
                                  abMessage, (uint16)iLength));
  NetTestRunCommitTick(pNodes, pullNowMs, unTickRateHz, byCar);
  CHECK(NetClientStats(pNodes->pClient, &after));
  CHECK(after.uiRejectedMessages == before.uiRejectedMessages);
  CHECK(after.uiCommitWatermark == uiBaseEventSeq + 10u &&
        after.uiLastAppliedEventSeq == uiBaseEventSeq + 10u);
  CHECK(!human_control[aiRemote[0]]);

  /* This process owns one world, so restore the synthetic host mutation for
     the second tick-rate pass. */
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
    localdata[iChunk].iCenterGrip = aBefore[iChunk].byCenterGrip;
    localdata[iChunk].iLeftShoulderGrip =
        aBefore[iChunk].byLeftShoulderGrip;
    localdata[iChunk].iRightShoulderGrip =
        aBefore[iChunk].byRightShoulderGrip;
    memcpy(TrakColour[iChunk], aBefore[iChunk].auiTrakColour,
           sizeof(aBefore[iChunk].auiTrakColour));
  }
  printf("%u Hz: 10 ordered host commits applied after a watermark/gap; "
         "16 world chunks exact\n", unTickRateHz);
}

static void NetTestRace(uint16 unTickRateHz, uint64 ullSteadyMs,
                        uint64 ullAfterStepMs)
{
  tNetTestNodes nodes;
  tNetAddress hostAddress;
  tNetSessionConfig config;
  tNetTestRandom random = {0x5eed0e4510000001ull};
  tNetClientStats stats, stepStats;
  tNetHostPlayerStats hostStats;
  uint64 ullNowMs = 0, ullReleaseMs = 0, ullStepMs, ullEndMs;
  uint64 ullFrame = 0, ullNextFrameMs = 0;
  uint32 uiFirstSentTick = 0;
  int iHostTickIndex = 0, iStarted = 0, iRunningIndex = -1, iStepIndex = -1;
  int iRunningChunk = -1;
  float fTopSpeed = 0.0f;
  int iLateSteady = 0, iTicksSteady = 0, iLateAfterRecovery = 0;
  int iLastLateIndex = -1;
  uint8 byCar;

  NetTestRestore(&s_pristine);
  memset(&g_netStats, 0, sizeof(g_netStats));
  memset(&s_hostRun, 0, sizeof(s_hostRun));
  memset(&nodes, 0, sizeof(nodes));
  nodes.pSim = NetTransportSimCreate(0xc1e47u);
  CHECK(nodes.pSim);
  NetTestSetLinks(&nodes, NET_TEST_LATENCY_MS);

  memset(&hostAddress, 0, sizeof(hostAddress));
  hostAddress.abAddress[0] = 127;
  hostAddress.abAddress[3] = 1;
  hostAddress.byFamily = NET_ADDR_IPV4;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = unTickRateHz;
  /* SPEEDY and NUCLEAR carry the rate in the level flags. */
  if (unTickRateHz >= 50)
    config.iLevelFlags |= NET_SESSION_50HZ_FLAG;
  if (unTickRateHz >= 100)
    config.iLevelFlags |= NET_SESSION_100HZ_FLAG;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = 4;
  config.byPauseAllowed = 1;
  config.iTrackLoad = 7;
  config.iManualControl = 1;
  config.iCompetitors = 16;
  config.iDamageLevel = 1;
  config.uiRandomSeed = 12345;
  config.uiTrackCRC = 0xe4a1e4a1u;
  memcpy(config.szBuildHash, "e4-s1-test", 11);
  CHECK(NetSessionConfigValidate(&config));

  nodes.pHostChannel = NetChannelCreate(NetTransportSimEndpoint(nodes.pSim, 0));
  CHECK(nodes.pHostChannel);
  nodes.pSessionHost = NetSessionHostCreate(nodes.pHostChannel, config.byMaxPlayers,
                                            NetTestRandomBytes, &random);
  CHECK(nodes.pSessionHost && NetSessionHostSetConfig(nodes.pSessionHost, &config));
  nodes.pLobbyHost = NetLobbyHostCreate(nodes.pSessionHost);
  CHECK(nodes.pLobbyHost);
  nodes.pHost = NetHostCreate(nodes.pSessionHost, nodes.pLobbyHost);
  CHECK(nodes.pHost);
  NetHostSetSimulation(nodes.pHost, NetTestHostSimulate, &s_hostRun);

  nodes.pClientChannel = NetChannelCreate(NetTransportSimEndpoint(nodes.pSim, 1));
  CHECK(nodes.pClientChannel);
  nodes.pClientConnection = NetChannelAddConnection(nodes.pClientChannel,
                                                    &hostAddress, 0, 0);
  CHECK(nodes.pClientConnection);
  nodes.pSessionClient = NetSessionClientCreate(nodes.pClientConnection,
                                                NET_PROTOCOL_VERSION, 1, "Alpha");
  CHECK(nodes.pSessionClient);
  nodes.pLobbyClient = NetLobbyClientCreate(nodes.pSessionClient);
  CHECK(nodes.pLobbyClient && NetSessionClientStart(nodes.pSessionClient));
  nodes.pClient = NetClientCreate(nodes.pSessionClient, nodes.pLobbyClient);
  CHECK(nodes.pClient);
  CHECK(!NetClientBeginRace(nodes.pClient)); /* not released yet */
  CHECK(!NetClientTicksDue(nodes.pClient));

  /* Lobby: join, ready, start, loaded, release (E2-S2 and E2-S4). */
  for (; ullNowMs <= 1500; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(nodes.pSim, ullNowMs));
    NetTestPumpHost(&nodes);
    NetTestPumpClient(&nodes);
  }
  CHECK(NetSessionClientState(nodes.pSessionClient) == NET_JOIN_ACCEPTED);
  byCar = NetSessionClientPlayerIndex(nodes.pSessionClient);
  s_hostRun.byCar = byCar;
  s_hostRun.byRemoteCar = (uint8)((byCar + 1) % numcars);
  s_hostRun.uiStartTick = NET_TEST_START_TICK;
  CHECK(NetSimLegacyToWorld(&Car[s_hostRun.byRemoteCar],
                            &s_hostRun.remoteBase));
  /* Keep the scripted puppet away from the predicted car; its frame still
     switches between chunks 0 and 1 and airborne every 40 host ticks. */
  s_hostRun.remoteBase.position.fX += 4000.0f;
  s_hostRun.remoteBase.position.fZ += 1000.0f;
  s_hostRun.byRemoteReady = 1;
  CHECK(NetLobbyClientSetReady(nodes.pLobbyClient, 1, config.uiTrackCRC));
  for (; ullNowMs <= 2500; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(nodes.pSim, ullNowMs));
    NetTestPumpHost(&nodes);
    NetTestPumpClient(&nodes);
  }
  CHECK(NetLobbyHostStart(nodes.pLobbyHost, NET_TEST_START_TICK));
  for (; ullNowMs <= 3500; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(nodes.pSim, ullNowMs));
    NetTestPumpHost(&nodes);
    NetTestPumpClient(&nodes);
  }
  CHECK(NetLobbyClientSetRaceLoaded(nodes.pLobbyClient));
  for (; !NetLobbyHostRaceReleased(nodes.pLobbyHost); ++ullNowMs) {
    CHECK(ullNowMs < 8000);
    CHECK(NetTransportSimAdvance(nodes.pSim, ullNowMs));
    NetTestPumpHost(&nodes);
    NetTestPumpClient(&nodes);
  }
  CHECK(NetHostBeginRace(nodes.pHost));
  ullReleaseMs = ullNowMs;
  ullStepMs = ullReleaseMs + ullSteadyMs;
  ullEndMs = ullStepMs + ullAfterStepMs;
  ullNextFrameMs = ullReleaseMs;

  for (; ullNowMs <= ullEndMs; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(nodes.pSim, ullNowMs));
    NetTestPumpHost(&nodes);

    /* The host runs its own clock; every tick it hands out is simulated. */
    while (ullNowMs >= ullReleaseMs +
           (uint64)iHostTickIndex * 1000u / unTickRateHz) {
      uint32 uiTick = NetHostNextTick(nodes.pHost);
      int iIndex = s_hostRun.iTicks;
      uint32 uiLateBefore;
      CHECK(NetHostPlayerStats(nodes.pHost, 0, &hostStats));
      uiLateBefore = hostStats.uiLateInputs;
      CHECK(NetHostTick(nodes.pHost, uiTick));
      CHECK(NetHostLastEventSeq(nodes.pHost) <= 1u);
      CHECK((NetHostRaceState(nodes.pHost) == NET_RACE_RUNNING) ==
            (NetHostLastEventSeq(nodes.pHost) == 1u));
      NetTestRestoreClientWorld(&s_hostRun);
      CHECK(NetHostPlayerStats(nodes.pHost, 0, &hostStats));
      s_hostRun.aTicks[iIndex].ullMs = ullNowMs;
      s_hostRun.aTicks[iIndex].byLate =
          hostStats.uiLateInputs != uiLateBefore;
      ++s_hostRun.iTicks;
      /* Steady state is everything from two seconds in up to the latency
         step; the step has its own five-second recovery rule. */
      if (ullNowMs >= ullReleaseMs + NET_TEST_WARMUP_MS && ullNowMs < ullStepMs)
        ++iTicksSteady;
      if (s_hostRun.aTicks[iIndex].byLate) {
        if (ullNowMs >= ullReleaseMs + NET_TEST_WARMUP_MS) {
          if (ullNowMs < ullStepMs)
            ++iLateSteady;
          iLastLateIndex = iIndex;
        }
        if (ullNowMs >= ullStepMs + NET_TEST_RECOVERY_MS)
          ++iLateAfterRecovery;
      }
      if (iStepIndex < 0 && ullNowMs >= ullStepMs)
        iStepIndex = iIndex;
      ++iHostTickIndex;
    }

    /* The client runs on its own frame loop at 60 fps: pump, then exactly
       the ticks the dilated accumulator owes. */
    if (ullNowMs < ullNextFrameMs)
      continue;
    ++ullFrame;
    ullNextFrameMs = ullReleaseMs + ullFrame * NET_TEST_FRAME_NUMERATOR / 3u;
    NetTestPumpClient(&nodes);
    if (!iStarted) {
      uint32 uiStartTick;
      if (!NetLobbyClientRaceReleased(nodes.pLobbyClient, &uiStartTick))
        continue;
      CHECK(uiStartTick == NET_TEST_START_TICK);
      CHECK(NetClientBeginRace(nodes.pClient));
      s_hostRun.pClient = nodes.pClient;
      CHECK(!NetClientBeginRace(nodes.pClient));
      CHECK(NetClientCurrentTick(nodes.pClient) == NET_TEST_START_TICK - 1u);
      CHECK(NetClientGroup(nodes.pClient, NULL) == 1);
      CHECK(human_control[byCar] == 1);
      uiFirstSentTick = NET_TEST_START_TICK - (NET_INPUT_REDUNDANCY - 1);
      iStarted = 1;
    }
    NetClientPump(nodes.pClient);
    CHECK(NetClientStats(nodes.pClient, &stats));
    if (iRunningIndex >= 0 && iRunningChunk < 0)
      iRunningChunk = Car[byCar].iLastValidChunk;
    if (Car[byCar].fFinalSpeed > fTopSpeed)
      fTopSpeed = Car[byCar].fFinalSpeed;
    CHECK(stats.fTickScale >= NET_CLIENT_TICK_SCALE_MIN &&
          stats.fTickScale <= NET_CLIENT_TICK_SCALE_MAX);
    while (NetClientTicksDue(nodes.pClient) > 0) {
      NetTestClientTick(nodes.pClient, byCar);
      if (iRunningIndex < 0 && game_frame == 145)
        iRunningIndex = (int)(NetClientCurrentTick(nodes.pClient) -
                              NET_TEST_START_TICK);
    }
    if (ullNowMs == ullStepMs) {
      /* The +60 ms latency step, applied to both directions. */
      NetTestSetLinks(&nodes, NET_TEST_LATENCY_MS + NET_TEST_STEP_LATENCY_MS);
      CHECK(NetClientStats(nodes.pClient, &stepStats));
    }
  }

  /* Acceptance. */
  CHECK(NetClientStats(nodes.pClient, &stats));
  CHECK(NetHostPlayerStats(nodes.pHost, 0, &hostStats));
  printf("%u Hz: %u client ticks, lead %d (+%d bias), scale %.3f, error %.2f, "
         "RTT %.0f ms, %d/%d late in steady state, %u gaps, %u reorders, "
         "%u snapshots\n",
         unTickRateHz, stats.uiTicks, stats.iLeadTicks, stats.iLeadBias,
         (double)stats.fTickScale, (double)stats.fLeadErrorTicks,
         (double)stats.fRttMs, iLateSteady, iTicksSteady,
         hostStats.uiBatchTickGaps, hostStats.uiBatchReorders, stats.uiSnapshots);
  {
    /* Where the late inputs fell, in seconds from the race start. */
    int iSecond = -1, iCount = 0;
    for (int iIndex = 0; iIndex < s_hostRun.iTicks; ++iIndex) {
      int iAt = (int)((s_hostRun.aTicks[iIndex].ullMs - ullReleaseMs) / 1000u);
      if (!s_hostRun.aTicks[iIndex].byLate)
        continue;
      if (iAt != iSecond) {
        if (iCount)
          printf("  late: %d in second %d\n", iCount, iSecond);
        iSecond = iAt;
        iCount = 0;
      }
      ++iCount;
    }
    if (iCount)
      printf("  late: %d in second %d\n", iCount, iSecond);
  }
  CHECK(iRunningIndex > 0 && iStepIndex > 0);
  /* The client predicted its own car under its own input all race long. */
  CHECK(human_control[byCar] == 1 && net_sim_authority == NET_AUTHORITY_LOCAL);
  CHECK(iRunningChunk >= 0 && Car[byCar].iLastValidChunk != iRunningChunk);
  CHECK(fTopSpeed > 0.0f);
  CHECK(s_hostRun.iTicks > 100 && iTicksSteady > 100);

  /* The timeline: one batch per tick, consecutive ticks, nothing skipped or
     repeated, and inside the host's accept window all race long. */
  CHECK(stats.uiTicks == NetClientCurrentTick(nodes.pClient) - NET_TEST_START_TICK + 1u);
  CHECK(stats.uiRampTick == NetClientCurrentTick(nodes.pClient));
  CHECK(stats.uiPuppetHookCalls ==
        stats.uiTicks + stats.uiReplayTicksTotal);
  CHECK(stats.uiPuppetApplications > stats.uiPuppetHookCalls);
  CHECK(stats.fInterpolationDelayMs >= NET_CLIENT_INTERPOLATION_MIN_MS &&
        stats.fInterpolationDelayMs <= NET_CLIENT_INTERPOLATION_MAX_MS);
  CHECK(!stats.uiRampCorrections && !g_netStats.iRampCorrections);
  CHECK(stats.uiSnapshotAgeMs < 1000u);
  CHECK(stats.uiBatchesSent == stats.uiTicks);
  CHECK(hostStats.uiFirstBatchTick == uiFirstSentTick ||
        (int32)(hostStats.uiFirstBatchTick - uiFirstSentTick) > 0);
  /* Every uiFirstTick the host saw is accounted for: what arrived, plus what
     a later reordered batch filled in, covers the span exactly, and what is
     left over is loss on the link, not a hole in the client's timeline. */
  CHECK(hostStats.uiInputBatches + hostStats.uiBatchTickGaps -
        hostStats.uiBatchReorders ==
        hostStats.uiNewestFirstTick - hostStats.uiFirstBatchTick + 1u);
  CHECK(hostStats.uiBatchTickGaps >= hostStats.uiBatchReorders);
  CHECK((hostStats.uiBatchTickGaps - hostStats.uiBatchReorders) * 12u <
        stats.uiBatchesSent); /* 3 percent loss, with headroom */
  CHECK(!hostStats.uiFutureInputs && !hostStats.uiRejectedBatches);
  CHECK(hostStats.uiClampedInputs == 0);

  /* Input arrival: under 1 percent late in steady state, and the host
     simulated exactly the scripted input whenever it was not late. */
  CHECK(iLateSteady * 100 < iTicksSteady);
  for (int iIndex = 0; iIndex < s_hostRun.iTicks; ++iIndex) {
    tCarInputData expected = NetTestScript(s_hostRun.aTicks[iIndex].uiTick, byCar);
    NetSimCanonicaliseInput(&expected);
    CHECK(s_hostRun.aTicks[iIndex].uiTick ==
          NET_TEST_START_TICK + (uint32)iIndex);
    if (!s_hostRun.aTicks[iIndex].byLate)
      CHECK(!memcmp(&s_hostRun.aTicks[iIndex].input, &expected, sizeof(expected)));
  }

  /* The latency step: the inputs in flight when it lands are late, and the
     lead controller has absorbed it inside five seconds.  The tick scale
     never left its clamp (asserted every frame above). */
  CHECK(!iLateAfterRecovery);
  CHECK(iLastLateIndex < 0 ||
        s_hostRun.aTicks[iLastLateIndex].ullMs <= ullStepMs + NET_TEST_RECOVERY_MS);
  CHECK(fabsf(stats.fLeadErrorTicks) < 1.5f);
  CHECK(stats.iLeadTicks >= 1 && stats.iLeadTicks < NET_INPUT_HORIZON);
  CHECK(stats.fRttMs > 2.0f * NET_TEST_LATENCY_MS);

  /* The three rings over the last NET_CLIENT_HISTORY ticks. */
  {
    uint32 uiNewest = NetClientCurrentTick(nodes.pClient);
    tNetSimTickContext context, previous = {0};
    tNetCarFullState state;
    tCarInputData input;
    CHECK(stats.uiTicks > NET_CLIENT_HISTORY);
    CHECK(!NetClientContextAt(nodes.pClient, uiNewest - NET_CLIENT_HISTORY, &context));
    for (uint32 uiBack = NET_CLIENT_HISTORY - 1; uiBack > 0; --uiBack) {
      uint32 uiTick = uiNewest - uiBack;
      CHECK(NetClientContextAt(nodes.pClient, uiTick, &context));
      CHECK(NetClientPredictionAt(nodes.pClient, 0, uiTick, &state));
      CHECK(NetClientInputAt(nodes.pClient, uiTick, &input));
      if (uiBack < NET_CLIENT_HISTORY - 1)
        CHECK(context.iGameFrame == previous.iGameFrame + 1);
      previous = context;
    }
  }

  /* Host state arrived and is retained for its 600 ms (4.6). */
  {
    tNetSnapshot snapshot;
    tNetCarExtra extra;
    uint32 uiNewestSnapshot = stats.uiNewestSnapshotTick;
    int iRetention = (NET_SNAPSHOT_RETENTION_MS * unTickRateHz + 999) / 1000;
    CHECK(!stats.uiRejectedMessages);
    CHECK(stats.uiSnapshots > 100 && stats.uiOwnCarStates > 100);
    CHECK(stats.uiFeedback > 10);
    int iPaired = 0;
    CHECK(NetClientSnapshotAt(nodes.pClient, uiNewestSnapshot, &snapshot));
    CHECK(snapshot.uiTick == uiNewestSnapshot);
    /* Own-car state travels as its own message, so the newest snapshot's may
       still be in flight or lost; a correction needs a paired tick inside the
       retention window (4.5). */
    for (uint32 uiBack = 0; uiBack <= (uint32)iRetention; ++uiBack) {
      uint32 uiTick = uiNewestSnapshot - uiBack;
      if (NetClientSnapshotAt(nodes.pClient, uiTick, &snapshot) &&
          NetClientOwnCarStateAt(nodes.pClient, uiTick, &extra))
        ++iPaired;
    }
    CHECK(iPaired > iRetention / 4);
    CHECK(!NetClientSnapshotAt(nodes.pClient,
                               uiNewestSnapshot - (uint32)iRetention - 1u, &snapshot));
    /* The client is ahead of the host, and the host knows which snapshot it
       has decoded (4.8). */
    CHECK((int32)(NetClientCurrentTick(nodes.pClient) - uiNewestSnapshot) > 0);
    CHECK((int32)(hostStats.uiLastDecodedSnapshotTick -
                  (uiNewestSnapshot - 4u * (uint32)iRetention)) > 0);
  }

  /* Interpolation stayed smooth while the newer snapshot's discrete frame
     crossed two chunks and the airborne boundary.  The scripted median is
     sqrt(1.5^2 + 0.5^2 + 0.25^2) = 1.6008 units per tick; the story's spike
     threshold is three times that value. */
  CHECK(s_hostRun.iPuppetSamples > 100);
  CHECK(s_hostRun.iSawChunk0 && s_hostRun.iSawChunk1 &&
        s_hostRun.iSawAirborne);
  CHECK(s_hostRun.fMaxPuppetStepPerTick > 1.4f);
  CHECK(s_hostRun.fMaxPuppetStepPerTick < 4.8024f);

  if (unTickRateHz == 100)
    NetTestRampStallRecovery(&nodes, &ullNowMs, unTickRateHz, byCar);

  NetTestCorrections(&nodes, &ullNowMs, unTickRateHz,
                     config.bySnapshotInterval, byCar);
  NetTestPredictionModes(&nodes, &ullNowMs, ullReleaseMs,
                         &iHostTickIndex, unTickRateHz, byCar);
  NetTestPause(&nodes, &ullNowMs, unTickRateHz, byCar);
  NetTestHostCommits(&nodes, &ullNowMs, unTickRateHz, byCar);

  NetClientDestroy(nodes.pClient);
  NetLobbyClientDestroy(nodes.pLobbyClient);
  NetSessionClientDestroy(nodes.pSessionClient);
  NetChannelDestroy(nodes.pClientChannel);
  NetHostDestroy(nodes.pHost);
  NetLobbyHostDestroy(nodes.pLobbyHost);
  NetSessionHostDestroy(nodes.pSessionHost);
  NetChannelDestroy(nodes.pHostChannel);
  NetTransportSimDestroy(nodes.pSim);
}

int main(int iArgc, const char **ppArgv)
{
  char szError[512];
  CHECK(iArgc == 3);
  if (!NetHeadlessInit(ppArgv[1], ppArgv[2], 16, 12345, szError, sizeof(szError))) {
    fprintf(stderr, "%s\n", szError);
    return 1;
  }
  net_mode = NET_MODE_MODERN;
  NetTestCapture(&s_pristine);

  /* Five minutes of virtual race time is E4-S2's steady puppet soak. */
  NetTestRace(36, 285000, 15000);
  puts("NET-E4-S1/S2/S3/S4 client acceptance passed at 36 Hz");
  NetTestRace(100, 8000, 8000);
  puts("NET-E4-S1/S2/S3/S4 client acceptance passed at 100 Hz");
  return 0;
}
