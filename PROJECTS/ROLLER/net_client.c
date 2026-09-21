#include "net_client.h"
#include "net_input.h"
#include "net_snapshot.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"
#include "loadtrak.h"

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

struct tNetClient
{
  tNetSessionClient *pSession;
  tNetLobbyClient *pLobby;
  tNetSessionConfig config;
  double dTicksPerMs;
  int iRetentionTicks;
  uint8 byRacing, byJoined, byHasEstimate, byHasFeedback, byHasSnapshot;
  uint8 byHasFrame, byGroupCount;
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
  uint32 uiFeedbackHostTick;
  tNetClientStats stats;
  tNetClientInputSlot aInputs[NET_CLIENT_HISTORY];
  tNetClientPredictionSlot aPrediction[NET_INPUT_MAX_LOCAL_PLAYERS][NET_CLIENT_HISTORY];
  tNetClientContextSlot aContext[NET_CLIENT_HISTORY];
  tNetClientSnapshotSlot aSnapshots[NET_CLIENT_SNAPSHOT_BUFFER];
  tNetClientOwnSlot aOwn[NET_CLIENT_SNAPSHOT_BUFFER];
};

/* D13 permits one simulation world per process, so exactly one racing client
   can own the process-global in-tick puppet hook. */
static tNetClient *s_pPuppetClient;

_Static_assert(NET_CLIENT_SNAPSHOT_BUFFER > (NET_SNAPSHOT_RETENTION_MS * 100 + 999) / 1000,
               "the snapshot buffer must hold 600 ms at 100 Hz without aliasing");
_Static_assert(NET_INPUT_REDUNDANCY < NET_CLIENT_HISTORY,
               "a batch must come from the input history");

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
    /* The host sends a snapshot as soon as it has simulated its tick. */
    NetClientObserveHost(pClient, NetClientRelative(pClient, snapshot.uiTick),
                         ullNowMs);
  }
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
    default:
      /* Events, world changes and pause belong to later stories. */
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
  pClient->config = config;
  pClient->dTicksPerMs = config.unTickRateHz / 1000.0;
  pClient->iRetentionTicks =
      (NET_SNAPSHOT_RETENTION_MS * config.unTickRateHz + 999) / 1000;
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
  pClient->byJoined = 0;
  pClient->byHasEstimate = 0;
  pClient->byHasFeedback = 0;
  pClient->byHasSnapshot = 0;
  pClient->byHasFrame = 0;
  pClient->stats.fTickScale = 1.0f;
  pClient->stats.fInterpolationDelayMs = NET_CLIENT_INTERPOLATION_MIN_MS;
  memset(net_puppet_car, 0, sizeof(net_puppet_car));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimSetPuppet(iCar, NetClientMember(pClient, (uint8)iCar) < 0);
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
  pClient->dAccumTicks += dElapsedMs * pClient->dTicksPerMs * dScale;
  pClient->stats.fTickScale = (float)dScale;
  pClient->stats.byHasHostEstimate = pClient->byHasEstimate;
  pClient->stats.uiSnapshotAgeMs = pClient->byHasSnapshot ?
      (uint32)(ullNowMs - pClient->ullNewestSnapshotMs) : 0;

  g_netStats.iSnapshotAgeMs = (int)pClient->stats.uiSnapshotAgeMs;
  g_netStats.fRttMs = pClient->stats.fRttMs;
  g_netStats.fJitterMs = pClient->stats.fJitterMs;
  g_netStats.fTickScale = pClient->stats.fTickScale;
  g_netStats.iLateInputs = (int)pClient->stats.uiHostLateInputs;
  g_netStats.iFutureInputs = (int)pClient->stats.uiHostFutureInputs;
}

int NetClientTicksDue(const tNetClient *pClient)
{
  if (!pClient || !pClient->byRacing || pClient->dAccumTicks < 1.0)
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

int NetClientTick(tNetClient *pClient, const tCarInputData *pLocalInputs)
{
  tCopyData aInputs[MAX_CARS];
  tNetClientInputSlot *pInput;
  uint32 uiTick;
  int iSavedAuthority;
  if (!pClient || !pLocalInputs || NetClientTicksDue(pClient) < 1)
    return 0;
  uiTick = pClient->uiClientTick + 1u;

  /* 4.3 step 2: compare matching ticks before updatestunts advances the
     live ramps.  Correction installs timing and rebuilds geometry, but never
     advances the authoritative local timeline. */
  if (!NetClientCorrectRamps(pClient))
    return 0;

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
  for (int iMember = 0; iMember < pClient->byGroupCount; ++iMember) {
    tNetClientPredictionSlot *pSlot =
        &pClient->aPrediction[iMember][uiTick % NET_CLIENT_HISTORY];
    pSlot->uiTick = uiTick;
    pSlot->byValid = (uint8)NetSnapshotEncodeCarFull(pClient->abyGroup[iMember],
                                                     &pSlot->state);
  }
  {
    tNetClientContextSlot *pSlot = &pClient->aContext[uiTick % NET_CLIENT_HISTORY];
    NetSimCaptureContext(&pSlot->context);
    pSlot->uiTick = uiTick;
    pSlot->byValid = 1;
  }
  return 1;
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
