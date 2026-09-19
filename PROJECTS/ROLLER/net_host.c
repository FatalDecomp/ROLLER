#include "net_host.h"
#include "net_input.h"
#include "net_sim_seam.h"
#include "net_snapshot.h"
#include "net_race_start.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
  uint32 uiTick;
  tCarInputData aInput[NET_INPUT_MAX_LOCAL_PLAYERS];
  uint8 byValid;
} tNetHostInputSlot;

typedef struct
{
  uint8 byActive, byCarCount;
  uint8 abyCars[NET_INPUT_MAX_LOCAL_PLAYERS];
  tCarInputData aLast[NET_INPUT_MAX_LOCAL_PLAYERS];
  tNetHostInputSlot aQueue[NET_INPUT_QUEUE];
  tNetHostPlayerStats stats;
} tNetHostPlayer;

typedef struct
{
  tNetSnapshot snapshot;
  uint8 byValid;
} tNetHostRingEntry;

struct tNetHost
{
  tNetSessionHost *pSession;
  tNetLobbyHost *pLobby;
  tNetSessionConfig config;
  uint32 uiStartTick, uiNextTick, uiNewestSnapshotTick;
  uint64 ullLastFeedbackMs;
  int iRetentionTicks, iRingNext;
  uint8 byRacing, byHasSnapshot;
  tNetHostPlayer aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetHostRingEntry aRing[NET_HOST_SNAPSHOT_RING];
};

_Static_assert(NET_INPUT_HORIZON < NET_INPUT_QUEUE,
               "the accept window must not alias queue slots");

static void NetHostReceiveInput(tNetHost *pHost, tNetHostPlayer *pPlayer,
                                const tNetMessage *pMessage)
{
  tNetInputBatch batch;
  uint32 uiLastTick;
  int32 iMargin;
  if (!NetInputBatchDecode(pMessage->abData, pMessage->unLength, &batch) ||
      batch.byLocalPlayers != pPlayer->byCarCount) {
    ++pPlayer->stats.uiRejectedBatches;
    return;
  }
  if ((int32)(batch.uiLastDecodedSnapshotTick -
              pPlayer->stats.uiLastDecodedSnapshotTick) > 0)
    pPlayer->stats.uiLastDecodedSnapshotTick = batch.uiLastDecodedSnapshotTick;
  for (int iTick = 0; iTick < batch.byCount; ++iTick) {
    uint32 uiTick = batch.uiFirstTick + (uint32)iTick;
    uint32 uiAhead = uiTick - pHost->uiNextTick;
    tNetHostInputSlot *pSlot;
    /* Range check first; nothing is indexed with a tick outside
       [uiNextTick, uiNextTick + horizon) (4.4). */
    if ((int32)uiAhead < 0)
      continue;
    if (uiAhead >= NET_INPUT_HORIZON) {
      ++pPlayer->stats.uiFutureInputs;
      ++g_netStats.iFutureInputs;
      continue;
    }
    pSlot = &pPlayer->aQueue[uiTick % NET_INPUT_QUEUE];
    /* Redundant copies of an already queued tick are ignored. */
    if (pSlot->byValid && pSlot->uiTick == uiTick)
      continue;
    memset(pSlot, 0, sizeof(*pSlot));
    pSlot->uiTick = uiTick;
    pSlot->byValid = 1;
    for (int iLocal = 0; iLocal < pPlayer->byCarCount; ++iLocal) {
      pSlot->aInput[iLocal] = batch.aInputs[iTick][iLocal];
      if (NetInputClamp(&pSlot->aInput[iLocal], pPlayer->abyCars[iLocal]))
        ++pPlayer->stats.uiClampedInputs;
    }
  }
  uiLastTick = batch.uiFirstTick + batch.byCount - 1u;
  iMargin = (int32)(uiLastTick - pHost->uiNextTick);
  if (iMargin > 32767)
    iMargin = 32767;
  if (iMargin < -32768)
    iMargin = -32768;
  pPlayer->stats.nArrivalMarginTicks = (int16)iMargin;
}

static void NetHostRaceMessage(void *pContext, uint8 byPlayerIdx,
                               const tNetMessage *pMessage)
{
  tNetHost *pHost = (tNetHost *)pContext;
  tNetHostPlayer *pPlayer;
  if (!pHost || !pHost->byRacing || byPlayerIdx >= NET_SESSION_MAX_PLAYERS)
    return;
  pPlayer = &pHost->aPlayers[byPlayerIdx];
  if (pPlayer->byActive && pMessage->byType == NET_MSG_INPUT)
    NetHostReceiveInput(pHost, pPlayer, pMessage);
}

tNetHost *NetHostCreate(tNetSessionHost *pSession, tNetLobbyHost *pLobby)
{
  tNetHost *pHost;
  if (!pSession || !pLobby)
    return NULL;
  pHost = (tNetHost *)calloc(1, sizeof(*pHost));
  if (!pHost)
    return NULL;
  if (!NetSessionHostGetConfig(pSession, &pHost->config) ||
      !pHost->config.bySnapshotInterval || !pHost->config.unTickRateHz) {
    free(pHost);
    return NULL;
  }
  pHost->pSession = pSession;
  pHost->pLobby = pLobby;
  /* 600 ms of ticks, rounded up (4.8). */
  pHost->iRetentionTicks =
      (NET_SNAPSHOT_RETENTION_MS * pHost->config.unTickRateHz + 999) / 1000;
  NetLobbyHostSetRaceCallback(pLobby, NetHostRaceMessage, pHost);
  return pHost;
}

void NetHostDestroy(tNetHost *pHost)
{
  if (!pHost)
    return;
  NetLobbyHostSetRaceCallback(pHost->pLobby, NULL, NULL);
  free(pHost);
}

int NetHostBeginRace(tNetHost *pHost)
{
  uint8 abyOwner[MAX_CARS];
  int aiHumanControl[MAX_CARS] = {0};
  tNetHostPlayer aPlayers[NET_SESSION_MAX_PLAYERS];
  uint32 uiStartTick;
  int iPlayers = 0;
  if (!pHost || pHost->byRacing || net_mode != NET_MODE_MODERN ||
      !NetLobbyHostRaceReleased(pHost->pLobby) ||
      !NetLobbyHostStartTick(pHost->pLobby, &uiStartTick) ||
      numcars < 1 || numcars > MAX_CARS)
    return 0;
  memset(abyOwner, 0xff, sizeof(abyOwner));
  memset(aPlayers, 0, sizeof(aPlayers));
  for (int iPlayer = 0; iPlayer < pHost->config.byMaxPlayers; ++iPlayer) {
    tNetPlayerEntry entry;
    tNetHostPlayer *pPlayer = &aPlayers[iPlayer];
    uint8 abyCars[2];
    int iCars;
    if (!NetLobbyHostPlayer(pHost->pLobby, (uint8)iPlayer, &entry) ||
        entry.byState != NET_PLAYER_RACING)
      continue; /* DROPPED players' cars stay AI; E5-S2 owns takeover. */
    abyCars[0] = entry.byCarIdx0;
    abyCars[1] = entry.byCarIdx1;
    iCars = entry.byCarIdx1 == NET_LOBBY_NO_PLAYER ? 1 : 2;
    if (iCars != NetSessionHostPlayerLocalPlayers(pHost->pSession,
                                                  (uint8)iPlayer))
      return 0;
    for (int iCar = 0; iCar < iCars; ++iCar) {
      if (abyCars[iCar] >= numcars || abyOwner[abyCars[iCar]] != 0xff)
        return 0;
      abyOwner[abyCars[iCar]] = (uint8)iPlayer;
      aiHumanControl[abyCars[iCar]] = entry.byHumanControl;
      pPlayer->abyCars[iCar] = abyCars[iCar];
      pPlayer->stats.abyCars[iCar] = abyCars[iCar];
    }
    pPlayer->byActive = 1;
    pPlayer->byCarCount = (uint8)iCars;
    pPlayer->stats.byCarCount = (uint8)iCars;
    ++iPlayers;
  }
  if (!iPlayers)
    return 0;
  /* Ownership only, through human_control[] (4.11). */
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    human_control[iCar] = iCar < numcars ? aiHumanControl[iCar] : 0;
  memcpy(pHost->aPlayers, aPlayers, sizeof(aPlayers));
  memset(pHost->aRing, 0, sizeof(pHost->aRing));
  pHost->uiStartTick = uiStartTick;
  pHost->uiNextTick = uiStartTick;
  pHost->iRingNext = 0;
  pHost->byHasSnapshot = 0;
  pHost->ullLastFeedbackMs = NetSessionHostNowMs(pHost->pSession);
  pHost->byRacing = 1;
  return 1;
}

static tNetConnection *NetHostLiveConnection(const tNetHost *pHost,
                                             int iPlayer)
{
  tNetConnection *pConnection =
      NetSessionHostPlayerConnection(pHost->pSession, (uint8)iPlayer);
  return pConnection && !NetConnectionIsExpired(pConnection) ?
      pConnection : NULL;
}

static uint16 NetHostSaturate16(uint32 uiValue)
{
  return (uint16)(uiValue > 65535u ? 65535u : uiValue);
}

void NetHostPump(tNetHost *pHost)
{
  uint64 ullNowMs;
  if (!pHost || !pHost->byRacing)
    return;
  ullNowMs = NetSessionHostNowMs(pHost->pSession);
  if (ullNowMs - pHost->ullLastFeedbackMs < NET_HOST_FEEDBACK_MS)
    return;
  pHost->ullLastFeedbackMs = ullNowMs;
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    const tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    tNetConnection *pConnection;
    tNetInputFeedback feedback;
    uint8 abFeedback[sizeof(tNetInputFeedback)];
    if (!pPlayer->byActive ||
        !(pConnection = NetHostLiveConnection(pHost, iPlayer)))
      continue;
    memset(&feedback, 0, sizeof(feedback));
    feedback.uiHostTick = pHost->uiNextTick;
    feedback.unLateInputs = NetHostSaturate16(pPlayer->stats.uiLateInputs);
    feedback.unFutureInputs = NetHostSaturate16(pPlayer->stats.uiFutureInputs);
    feedback.nArrivalMarginTicks = pPlayer->stats.nArrivalMarginTicks;
    if (NetInputFeedbackEncode(&feedback, abFeedback, sizeof(abFeedback)))
      NetConnectionQueueMessage(pConnection, NET_MSG_INPUT_FEEDBACK, 0,
                                abFeedback, sizeof(abFeedback));
  }
}

static void NetHostSendSnapshot(tNetHost *pHost, const tNetSnapshot *pSnapshot)
{
  uint8 abSnapshot[sizeof(tNetSnapshot)];
  uint8 abOwn[sizeof(tNetOwnCarStateHeader) + 2 * NET_OWN_CAR_ENTRY_SIZE];
  int iSnapshotLength = NetSnapshotEncode(pSnapshot, abSnapshot,
                                          sizeof(abSnapshot));
  if (!iSnapshotLength)
    return;
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    const tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    tNetCarExtra aExtras[2];
    tNetConnection *pConnection;
    int iOwnLength, iCar;
    if (!pPlayer->byActive ||
        !(pConnection = NetHostLiveConnection(pHost, iPlayer)))
      continue;
    for (iCar = 0; iCar < pPlayer->byCarCount; ++iCar) {
      tNetCarFullState full;
      if (!NetSnapshotEncodeCarFull(pPlayer->abyCars[iCar], &full))
        break;
      aExtras[iCar] = full.extra;
    }
    if (iCar != pPlayer->byCarCount)
      continue;
    iOwnLength = NetSnapshotEncodeOwnCarState(pSnapshot->uiTick,
        pPlayer->abyCars, aExtras, pPlayer->byCarCount, abOwn, sizeof(abOwn));
    /* Snapshot then own-car state for the same tick, both unreliable; a
       correction needs both (4.5), and the channel spills the second into
       its own packet when they do not fit together. */
    NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0, abSnapshot,
                              (uint16)iSnapshotLength);
    if (iOwnLength)
      NetConnectionQueueMessage(pConnection, NET_MSG_OWN_CAR_STATE, 0, abOwn,
                                (uint16)iOwnLength);
  }
}

int NetHostTick(tNetHost *pHost, uint32 uiTick)
{
  tCopyData aInputs[MAX_CARS];
  tNetSnapshot *pSnapshot;
  if (!pHost || !pHost->byRacing || uiTick != pHost->uiNextTick)
    return 0;
  memset(aInputs, 0, sizeof(aInputs));
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    tNetHostInputSlot *pSlot = &pPlayer->aQueue[uiTick % NET_INPUT_QUEUE];
    if (!pPlayer->byActive)
      continue;
    if (pSlot->byValid && pSlot->uiTick == uiTick) {
      memcpy(pPlayer->aLast, pSlot->aInput, sizeof(pPlayer->aLast));
    } else {
      /* A miss repeats the last input and is counted (4.3 step 1). */
      ++pPlayer->stats.uiLateInputs;
      ++g_netStats.iLateInputs;
    }
    pSlot->byValid = 0;
    for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar)
      aInputs[pPlayer->abyCars[iCar]].data = pPlayer->aLast[iCar];
  }
  if (!NetSimWriteTickInputs(aInputs, numcars))
    return 0;
  control_one_tick();
  ++pHost->uiNextTick;

  if ((uiTick - pHost->uiStartTick) % pHost->config.bySnapshotInterval)
    return 1;
  pSnapshot = &pHost->aRing[pHost->iRingNext].snapshot;
  /* byRaceState carries the race-clock phase until E5-S1 defines the race
     state machine; E3-S2 supplies uiLastEventSeq and E5-S1 byPaused. */
  if (!NetSnapshotBuild(pSnapshot, uiTick, 0,
                        (uint8)(game_frame >= 145 ? NET_RACE_START_RUNNING :
                                                    NET_RACE_START_PRE_START),
                        0)) {
    pHost->aRing[pHost->iRingNext].byValid = 0;
    return 0;
  }
  pHost->aRing[pHost->iRingNext].byValid = 1;
  pHost->iRingNext = (pHost->iRingNext + 1) % NET_HOST_SNAPSHOT_RING;
  pHost->uiNewestSnapshotTick = uiTick;
  pHost->byHasSnapshot = 1;
  NetHostSendSnapshot(pHost, pSnapshot);
  return 1;
}

uint32 NetHostNextTick(const tNetHost *pHost)
{
  return pHost ? pHost->uiNextTick : 0;
}

int NetHostSnapshotAt(const tNetHost *pHost, uint32 uiTick,
                      tNetSnapshot *pSnapshot)
{
  if (!pHost || !pSnapshot || !pHost->byHasSnapshot ||
      pHost->uiNewestSnapshotTick - uiTick > (uint32)pHost->iRetentionTicks)
    return 0;
  for (int iEntry = 0; iEntry < NET_HOST_SNAPSHOT_RING; ++iEntry)
    if (pHost->aRing[iEntry].byValid &&
        pHost->aRing[iEntry].snapshot.uiTick == uiTick) {
      *pSnapshot = pHost->aRing[iEntry].snapshot;
      return 1;
    }
  return 0;
}

int NetHostPlayerStats(const tNetHost *pHost, uint8 byPlayerIdx,
                       tNetHostPlayerStats *pStats)
{
  if (!pHost || !pStats || byPlayerIdx >= NET_SESSION_MAX_PLAYERS ||
      !pHost->aPlayers[byPlayerIdx].byActive)
    return 0;
  *pStats = pHost->aPlayers[byPlayerIdx].stats;
  return 1;
}
