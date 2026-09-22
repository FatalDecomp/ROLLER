#include "net_host.h"
#include "net_checkpoint.h"
#include "net_event.h"
#include "net_input.h"
#include "net_race_state.h"
#include "net_sim_seam.h"
#include "net_snapshot.h"
#include "net_race_start.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"
#include "loadtrak.h"

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
  uint8 byCheckpointPending, byRestoreOwnership;
  uint8 abyCars[NET_INPUT_MAX_LOCAL_PLAYERS];
  uint64 ullDroppedMs;
  uint32 uiLateWindowTicks, uiLateWindowMisses;
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
  uint32 uiStartTick, uiNextTick, uiNewestSnapshotTick, uiLastEventSeq;
  uint64 ullLastFeedbackMs;
  int iRetentionTicks, iRingNext, iTrackLen;
  uint8 byRacing, byHasSnapshot, byResultsPublished;
  uint8 byForceSnapshot;
  int iResultFinishers, iResultHumanFinishers;
  tNetRaceLifecycle lifecycle;
  uint8 abyCarOwner[MAX_CARS], abyWorldMutated[MAX_TRACK_CHUNKS];
  tNetHostSimulateFn pSimulate;
  void *pSimulateContext;
  tNetHostPlayer aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetHostRingEntry aRing[NET_HOST_SNAPSHOT_RING];
  tNetWorldChangeEntry aWorld[MAX_TRACK_CHUNKS];
};

_Static_assert(NET_INPUT_HORIZON < NET_INPUT_QUEUE,
               "the accept window must not alias queue slots");

static void NetHostDetectDisconnects(tNetHost *pHost);
static int NetHostSendCheckpoint(tNetHost *pHost, int iPlayer);
static int NetHostEmitEvent(tNetHost *pHost, uint32 uiTick, uint8 byType,
                            int iCar, int32 iArg0, int32 iArg1);

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
  if (!pPlayer->stats.uiInputBatches) {
    pPlayer->stats.uiFirstBatchTick = batch.uiFirstTick;
    pPlayer->stats.uiNewestFirstTick = batch.uiFirstTick;
  } else {
    int32 iStep = (int32)(batch.uiFirstTick - pPlayer->stats.uiNewestFirstTick);
    if (iStep <= 0) {
      ++pPlayer->stats.uiBatchReorders;
    } else {
      pPlayer->stats.uiBatchTickGaps += (uint32)(iStep - 1);
      pPlayer->stats.uiNewestFirstTick = batch.uiFirstTick;
    }
  }
  ++pPlayer->stats.uiInputBatches;
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
  else if (pPlayer->byActive &&
           pMessage->byType == NET_MSG_CHECKPOINT_REQUEST &&
           pMessage->byFlags == (NET_MSG_RELIABLE | NET_MSG_ORDERED) &&
           !pMessage->unLength)
    pPlayer->byCheckpointPending = 1;
}

static eNetJoinRefuseReason NetHostAuthorizeRejoin(void *pContext,
                                                   uint8 byPlayerIdx,
                                                   uint64 ullNowMs)
{
  tNetHost *pHost = (tNetHost *)pContext;
  tNetHostPlayer *pPlayer;
  tNetPlayerEntry entry;
  if (!pHost || !pHost->byRacing ||
      byPlayerIdx >= NET_SESSION_MAX_PLAYERS ||
      !NetLobbyHostPlayer(pHost->pLobby, byPlayerIdx, &entry))
    return NET_JOIN_REFUSE_INVALID_REQUEST;
  pPlayer = &pHost->aPlayers[byPlayerIdx];
  if (pPlayer->byActive && entry.byState == NET_PLAYER_RACING) {
    pPlayer->byCheckpointPending = 1;
    pPlayer->byRestoreOwnership = 0;
    return NET_JOIN_REFUSE_NONE;
  }
  if (entry.byState != NET_PLAYER_DROPPED || !pPlayer->ullDroppedMs)
    return NET_JOIN_REFUSE_INVALID_REQUEST;
  if (ullNowMs - pPlayer->ullDroppedMs > NET_REJOIN_GRACE_MS)
    return NET_JOIN_REFUSE_REJOIN_EXPIRED;
  pPlayer->byCheckpointPending = 1;
  pPlayer->byRestoreOwnership = 1;
  return NET_JOIN_REFUSE_NONE;
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
  NetSessionHostSetRejoinCallback(pSession, NetHostAuthorizeRejoin, pHost);
  return pHost;
}

void NetHostDestroy(tNetHost *pHost)
{
  if (!pHost)
    return;
  NetLobbyHostSetRaceCallback(pHost->pLobby, NULL, NULL);
  NetSessionHostSetRejoinCallback(pHost->pSession, NULL, NULL);
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
      numcars < 1 || numcars > MAX_CARS ||
      TRAK_LEN < 1 || TRAK_LEN > MAX_TRACK_CHUNKS)
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
  memcpy(pHost->aPlayers, aPlayers, sizeof(aPlayers));
  memcpy(pHost->abyCarOwner, abyOwner, sizeof(abyOwner));
  memset(pHost->aRing, 0, sizeof(pHost->aRing));
  memset(pHost->abyWorldMutated, 0, sizeof(pHost->abyWorldMutated));
  memset(pHost->aWorld, 0, sizeof(pHost->aWorld));
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk)
    if (!NetWorldChangeCapture(iChunk, &pHost->aWorld[iChunk]))
      return 0;
  /* Ownership only, through human_control[] (4.11).  Install it only after
     every loaded-world value needed by the race has validated. */
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    human_control[iCar] = iCar < numcars ? aiHumanControl[iCar] : 0;
  pHost->iTrackLen = TRAK_LEN;
  pHost->uiStartTick = uiStartTick;
  pHost->uiNextTick = uiStartTick;
  pHost->uiLastEventSeq = 0;
  NetRaceLifecycleReset(&pHost->lifecycle);
  pHost->byResultsPublished = 0;
  pHost->iResultFinishers = 0;
  pHost->iResultHumanFinishers = 0;
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

static int NetHostQueueCheckpointMessage(tNetConnection *pConnection,
                                         uint8 byType, const uint8 *pData,
                                         int iLength)
{
  return iLength >= 0 && iLength <= NET_MAX_MESSAGE_SIZE &&
      NetConnectionQueueMessage(pConnection, byType,
          NET_MSG_RELIABLE | NET_MSG_ORDERED, pData, (uint16)iLength);
}

static int NetHostSendCheckpoint(tNetHost *pHost, int iPlayer)
{
  tNetConnection *pConnection = NetHostLiveConnection(pHost, iPlayer);
  tNetCheckpointHeader header;
  tNetSnapshot snapshot;
  tNetCarFullState aCars[MAX_CARS];
  tNetPlayerEntry aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetWorldChangeEntry aWorld[MAX_TRACK_CHUNKS];
  uint8 abData[NET_MAX_MESSAGE_SIZE];
  uint32 uiCheckpointTick;
  int iPlayers, iWorld = 0, iParts, iLength;
  if (!pConnection || !pHost->uiNextTick ||
      !(iPlayers = NetLobbyHostPlayerSlots(pHost->pLobby)))
    return 0;
  uiCheckpointTick = pHost->uiNextTick - 1u;
  if (!NetSnapshotBuild(&snapshot, uiCheckpointTick,
                        pHost->uiLastEventSeq, pHost->lifecycle.byState,
                        pHost->lifecycle.byPaused))
    return 0;
  for (int iCar = 0; iCar < numcars; ++iCar)
    if (!NetSnapshotEncodeCarFull(iCar, &aCars[iCar]))
      return 0;
  for (int iSlot = 0; iSlot < iPlayers; ++iSlot)
    NetLobbyHostPlayer(pHost->pLobby, (uint8)iSlot, &aPlayers[iSlot]);
  for (int iChunk = 0; iChunk < pHost->iTrackLen; ++iChunk)
    if (pHost->abyWorldMutated[iChunk])
      aWorld[iWorld++] = pHost->aWorld[iChunk];
  memset(&header, 0, sizeof(header));
  header.uiTick = uiCheckpointTick;
  header.uiLastEventSeq = pHost->uiLastEventSeq;
  header.uiRandomState = snapshot.uiRandomState;
  header.context = snapshot.context;
  header.unPauseRevision = pHost->lifecycle.unPauseRevision;
  header.byRaceState = pHost->lifecycle.byState;
  header.byPaused = pHost->lifecycle.byPaused;
  memcpy(header.aRamps, snapshot.aRamps, sizeof(header.aRamps));
  header.byNumCars = (uint8)numcars;
  header.byNumCarParts = (uint8)((numcars +
      NET_CHECKPOINT_CARS_PER_MESSAGE - 1) /
      NET_CHECKPOINT_CARS_PER_MESSAGE);
  header.byNumPlayers = (uint8)iPlayers;
  header.byNumWorldParts = (uint8)((iWorld +
      NET_WORLD_CHANGE_MAX_ENTRIES - 1) /
      NET_WORLD_CHANGE_MAX_ENTRIES);
  iParts = 1 + header.byNumCarParts + 1 + header.byNumWorldParts + 1;
  if (NetConnectionPendingReliable(pConnection) + iParts >
      NET_RELIABLE_QUEUE)
    return 0;
  iLength = NetCheckpointHeaderEncode(&header, abData, sizeof(abData));
  if (!iLength || !NetHostQueueCheckpointMessage(pConnection,
          NET_MSG_CHECKPOINT_HEADER, abData, iLength))
    return 0;
  for (int iFirst = 0; iFirst < numcars;
       iFirst += NET_CHECKPOINT_CARS_PER_MESSAGE) {
    int iCount = numcars - iFirst;
    if (iCount > NET_CHECKPOINT_CARS_PER_MESSAGE)
      iCount = NET_CHECKPOINT_CARS_PER_MESSAGE;
    iLength = NetCheckpointCarsEncode((uint8)iFirst, aCars + iFirst,
                                      iCount, abData, sizeof(abData));
    if (!iLength || !NetHostQueueCheckpointMessage(pConnection,
            NET_MSG_CHECKPOINT_CARS, abData, iLength))
      return 0;
  }
  iLength = NetCheckpointPlayersEncode(aPlayers, iPlayers,
                                       abData, sizeof(abData));
  if (!iLength || !NetHostQueueCheckpointMessage(pConnection,
          NET_MSG_CHECKPOINT_PLAYERS, abData, iLength))
    return 0;
  for (int iFirst = 0; iFirst < iWorld;
       iFirst += NET_WORLD_CHANGE_MAX_ENTRIES) {
    int iCount = iWorld - iFirst;
    if (iCount > NET_WORLD_CHANGE_MAX_ENTRIES)
      iCount = NET_WORLD_CHANGE_MAX_ENTRIES;
    iLength = NetCheckpointWorldEncode(aWorld + iFirst, iCount,
                                       abData, sizeof(abData));
    if (!iLength || !NetHostQueueCheckpointMessage(pConnection,
            NET_MSG_CHECKPOINT_WORLD, abData, iLength))
      return 0;
  }
  iLength = NetCheckpointEndEncode(uiCheckpointTick, abData, sizeof(abData));
  return iLength && NetHostQueueCheckpointMessage(pConnection,
      NET_MSG_CHECKPOINT_END, abData, iLength);
}

void NetHostPump(tNetHost *pHost)
{
  uint64 ullNowMs;
  if (!pHost || !pHost->byRacing)
    return;
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    if (!pPlayer->byCheckpointPending)
      continue;
    if (pPlayer->byRestoreOwnership) {
      tNetPlayerEntry entry;
      int iSecondCar;
      if (!NetLobbyHostPlayer(pHost->pLobby, (uint8)iPlayer, &entry) ||
          entry.byState != NET_PLAYER_DROPPED)
        continue;
      for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar) {
        int iOwnedCar = pPlayer->abyCars[iCar];
        human_control[iOwnedCar] = entry.byHumanControl;
        pHost->abyCarOwner[iOwnedCar] = (uint8)iPlayer;
      }
      memset(pPlayer->aQueue, 0, sizeof(pPlayer->aQueue));
      memset(pPlayer->aLast, 0, sizeof(pPlayer->aLast));
      pPlayer->uiLateWindowTicks = 0;
      pPlayer->uiLateWindowMisses = 0;
      pPlayer->stats.fLateInputRate = 0.0f;
      pPlayer->stats.byLateInputWarning = 0;
      pPlayer->byActive = 1;
      pPlayer->byRestoreOwnership = 0;
      pPlayer->ullDroppedMs = 0;
      NetLobbyHostMarkRejoined(pHost->pLobby, (uint8)iPlayer);
      iSecondCar = pPlayer->byCarCount == 2 ? pPlayer->abyCars[1] : -1;
      NetHostEmitEvent(pHost, pHost->uiNextTick - 1u,
                       NET_EV_PLAYER_REJOINED, pPlayer->abyCars[0],
                       iSecondCar, pPlayer->byCarCount);
    }
    if (NetHostSendCheckpoint(pHost, iPlayer)) {
      pPlayer->byCheckpointPending = 0;
      pHost->byForceSnapshot = 1;
    }
  }
  /* Connection time is independent of simulation time, including pause. */
  NetHostDetectDisconnects(pHost);
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

static int NetHostBroadcastCommit(tNetHost *pHost, uint8 byType,
                                  const uint8 *pData, uint16 unLength)
{
  int iQueued = 1;
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetConnection *pConnection;
    if (!pHost->aPlayers[iPlayer].byActive ||
        !(pConnection = NetHostLiveConnection(pHost, iPlayer)))
      continue;
    if (!NetConnectionQueueMessage(pConnection, byType,
            NET_MSG_RELIABLE | NET_MSG_ORDERED, pData, unLength))
      iQueued = 0;
  }
  return iQueued;
}

static int NetHostEmitEvent(tNetHost *pHost, uint32 uiTick, uint8 byType,
                            int iCar, int32 iArg0, int32 iArg1)
{
  tNetEvent event;
  uint8 abEvent[sizeof(tNetEvent)];
  int iLength;
  if (pHost->uiLastEventSeq == 0xffffffffu)
    return 0;
  memset(&event, 0, sizeof(event));
  event.uiEventSeq = pHost->uiLastEventSeq + 1u;
  event.uiTick = uiTick;
  event.byType = byType;
  event.byCarIdx = (uint8)iCar;
  event.byPlayerIdx = iCar == NET_EVENT_NO_CAR ? NET_EVENT_NO_PLAYER :
      pHost->abyCarOwner[iCar];
  event.iArg0 = iArg0;
  event.iArg1 = iArg1;
  iLength = NetEventEncode(&event, numcars, pHost->config.byMaxPlayers,
                           abEvent, sizeof(abEvent));
  if (!iLength)
    return 0;
  pHost->uiLastEventSeq = event.uiEventSeq;
  return NetHostBroadcastCommit(pHost, NET_MSG_EVENT, abEvent,
                                (uint16)iLength);
}

static int NetHostTransferPlayerToAi(tNetHost *pHost, int iPlayer)
{
  tNetHostPlayer *pPlayer;
  int iSecondCar;
  if (!pHost || iPlayer < 0 || iPlayer >= NET_SESSION_MAX_PLAYERS)
    return 0;
  pPlayer = &pHost->aPlayers[iPlayer];
  if (!pPlayer->byActive || pPlayer->byCarCount < 1 ||
      pPlayer->byCarCount > NET_INPUT_MAX_LOCAL_PLAYERS)
    return 0;
  /* Decide the whole group before mutating it.  Ownership is the only live
     simulation state this transition changes; airborne/physics state stays
     byte-identical and continues through the ordinary AI dispatch (4.11). */
  for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar)
    if (pPlayer->abyCars[iCar] >= numcars ||
        pHost->abyCarOwner[pPlayer->abyCars[iCar]] != (uint8)iPlayer)
      return 0;
  iSecondCar = pPlayer->byCarCount == 2 ? pPlayer->abyCars[1] : -1;
  /* Queueing failure cannot leave an expired player in control forever.
     The reliable commit is best-effort at this boundary, like the other
     host commits; subsequent snapshots still carry the ownership truth. */
  NetHostEmitEvent(pHost, pHost->uiNextTick, NET_EV_AI_TAKEOVER,
                   pPlayer->abyCars[0], iSecondCar, pPlayer->byCarCount);
  for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar) {
    int iOwnedCar = pPlayer->abyCars[iCar];
    human_control[iOwnedCar] = 0;
    pHost->abyCarOwner[iOwnedCar] = NET_EVENT_NO_PLAYER;
  }
  memset(pPlayer->aQueue, 0, sizeof(pPlayer->aQueue));
  memset(pPlayer->aLast, 0, sizeof(pPlayer->aLast));
  pPlayer->byActive = 0;
  pPlayer->ullDroppedMs = NetSessionHostNowMs(pHost->pSession);
  /* The simulation transfer is authoritative even if a roster broadcast
     cannot be queued.  Live clients also receive the numbered event. */
  NetLobbyHostMarkDropped(pHost->pLobby, (uint8)iPlayer);
  return 1;
}

static void NetHostDetectDisconnects(tNetHost *pHost)
{
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetConnection *pConnection;
    if (!pHost->aPlayers[iPlayer].byActive)
      continue;
    pConnection = NetSessionHostPlayerConnection(pHost->pSession,
                                                  (uint8)iPlayer);
    if (NetConnectionIsExpired(pConnection))
      NetHostTransferPlayerToAi(pHost, iPlayer);
  }
}

static int NetHostEmitCarEvents(tNetHost *pHost, uint32 uiTick,
                                const int *piLapBefore,
                                const uint8 *pbyFinishedBefore,
                                const uint8 *pbyLivesBefore,
                                const uint8 *pbyKillsBefore)
{
  int iOk = 1;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    int iLapAfter = (int)(int8)Car[iCar].byLap;
    int iFirstLap = piLapBefore[iCar] + 1;
    if (iFirstLap < 2)
      iFirstLap = 2; /* crossing onto lap 1 starts the race; it completes none */
    for (int iLap = iFirstLap; iLap <= iLapAfter; ++iLap) {
      int32 iLapMs = (int32)(Car[iCar].fPreviousLapTime * 1000.0f + 0.5f);
      if (iLapMs < 0)
        iLapMs = 0;
      if (!NetHostEmitEvent(pHost, uiTick, NET_EV_LAP_COMPLETE, iCar,
                            iLap - 1, iLapMs))
        iOk = 0;
    }
    if (!pbyFinishedBefore[iCar] && finished_car[iCar]) {
      if (Car[iCar].byLives) {
        if (!NetHostEmitEvent(pHost, uiTick, NET_EV_FINISHED, iCar,
                              Car[iCar].byRacePosition, finishers))
          iOk = 0;
      } else if (!NetHostEmitEvent(pHost, uiTick, NET_EV_DESTROYED, iCar,
                                   Car[iCar].byAttacker, Destroyed)) {
        iOk = 0;
      }
    }
    uint8 abyVictimUsed[MAX_CARS] = {0};
    for (int iKill = pbyKillsBefore[iCar];
         iKill < Car[iCar].byKills; ++iKill) {
      int iVictim = -1;
      for (int iCandidate = 0; iCandidate < numcars; ++iCandidate)
        if (!abyVictimUsed[iCandidate] &&
            pbyLivesBefore[iCandidate] > Car[iCandidate].byLives &&
            Car[iCandidate].byAttacker == iCar) {
          iVictim = iCandidate;
          abyVictimUsed[iCandidate] = 1;
          break;
        }
      if (!NetHostEmitEvent(pHost, uiTick, NET_EV_KILL, iCar, iVictim,
                            iKill + 1))
        iOk = 0;
    }
  }
  return iOk;
}

static int NetHostAllPlayersSettled(const tNetHost *pHost,
                                    int *piFinishers,
                                    int *piHumanFinishers)
{
  int iActive = 0, iSettled = 0;
  int iFinishers = 0, iHumanFinishers = 0;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    if (!finished_car[iCar])
      continue;
    ++iFinishers;
    if (pHost->abyCarOwner[iCar] != NET_EVENT_NO_PLAYER)
      ++iHumanFinishers;
  }
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    const tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    int iPlayerSettled = 1;
    if (!pPlayer->byActive)
      continue;
    ++iActive;
    for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar)
      if (!finished_car[pPlayer->abyCars[iCar]])
        iPlayerSettled = 0;
    iSettled += iPlayerSettled;
  }
  if (piFinishers)
    *piFinishers = iFinishers;
  if (piHumanFinishers)
    *piHumanFinishers = iHumanFinishers;
  return iActive > 0 && iSettled == iActive;
}

static int NetHostEmitLifecycle(tNetHost *pHost, uint32 uiTick)
{
  int iFinishers, iHumanFinishers;
  if (pHost->lifecycle.byState == NET_RACE_PRE_START && game_frame >= 145) {
    if (!NetHostEmitEvent(pHost, uiTick, NET_EV_RACE_STATE,
                          NET_EVENT_NO_CAR, NET_RACE_RUNNING, 0) ||
        !NetRaceTransition(&pHost->lifecycle, NET_RACE_RUNNING))
      return 0;
  }
  if (pHost->lifecycle.byState != NET_RACE_RUNNING ||
      !NetHostAllPlayersSettled(pHost, &iFinishers, &iHumanFinishers))
    return 1;
  if (!NetHostEmitEvent(pHost, uiTick, NET_EV_RACE_STATE,
                        NET_EVENT_NO_CAR, NET_RACE_OUTCOME_SETTLED, 0) ||
      !NetRaceTransition(&pHost->lifecycle, NET_RACE_OUTCOME_SETTLED))
    return 0;
  pHost->iResultFinishers = iFinishers;
  pHost->iResultHumanFinishers = iHumanFinishers;
  if (!NetHostEmitEvent(pHost, uiTick, NET_EV_RESULTS, NET_EVENT_NO_CAR,
                        iFinishers, iHumanFinishers))
    return 0;
  pHost->byResultsPublished = 1;
  return 1;
}

static int NetHostSendWorldChanges(tNetHost *pHost, uint32 uiTick,
                                   const tNetWorldChangeEntry *pEntries,
                                   int iCount)
{
  uint8 abWorld[sizeof(tNetWorldChangeHeader) +
                NET_WORLD_CHANGE_MAX_ENTRIES * sizeof(tNetWorldChangeEntry)];
  uint32 uiEventSeq;
  int iLength;
  if (pHost->uiLastEventSeq == 0xffffffffu)
    return 0;
  uiEventSeq = pHost->uiLastEventSeq + 1u;
  iLength = NetWorldChangeEncode(uiEventSeq, uiTick, pEntries, iCount,
                                 abWorld, sizeof(abWorld));
  if (!iLength)
    return 0;
  pHost->uiLastEventSeq = uiEventSeq;
  return NetHostBroadcastCommit(pHost, NET_MSG_WORLD_CHANGE, abWorld,
                                (uint16)iLength);
}

static int NetHostEmitWorldChanges(tNetHost *pHost, uint32 uiTick,
                                   const int *piLovebunChunk,
                                   const uint8 *pbyLovebunAmmo)
{
  tNetWorldChangeEntry aCurrent[MAX_TRACK_CHUNKS];
  tNetWorldChangeEntry aBatch[NET_WORLD_CHANGE_MAX_ENTRIES];
  uint8 abyChanged[MAX_TRACK_CHUNKS] = {0};
  uint8 abyCovered[MAX_TRACK_CHUNKS] = {0};
  int iCount, iOk = 1;
  for (int iChunk = 0; iChunk < pHost->iTrackLen; ++iChunk) {
    if (!NetWorldChangeCapture(iChunk, &aCurrent[iChunk]))
      return 0;
    abyChanged[iChunk] = !NetWorldChangeEntryEqual(
        &aCurrent[iChunk], &pHost->aWorld[iChunk]);
  }

  /* A successful LOVEBUN use consumes one ammo and touches the 16 chunks
     from current through current + 15.  Emit one commit for each use;
     overlapping same-tick uses carry the common final post-tick values. */
  for (int iCar = 0; iCar < numcars; ++iCar) {
    uint8 abyAdded[MAX_TRACK_CHUNKS] = {0};
    int iChunk;
    if (piLovebunChunk[iCar] < 0 ||
        Car[iCar].byCheatAmmo >= pbyLovebunAmmo[iCar])
      continue;
    iCount = 0;
    iChunk = piLovebunChunk[iCar];
    for (int iStep = 0; iStep < 16; ++iStep) {
      if (abyChanged[iChunk] && !abyAdded[iChunk]) {
        aBatch[iCount++] = aCurrent[iChunk];
        abyAdded[iChunk] = 1;
        abyCovered[iChunk] = 1;
      }
      if (++iChunk == pHost->iTrackLen)
        iChunk = 0;
    }
    if (iCount && !NetHostSendWorldChanges(pHost, uiTick, aBatch, iCount))
      iOk = 0;
  }

  /* Preserve changes from any later world-mutating mechanic, and changes
     outside a detected LOVEBUN range, without dropping them. */
  iCount = 0;
  for (int iChunk = 0; iChunk < pHost->iTrackLen; ++iChunk) {
    if (!abyChanged[iChunk] || abyCovered[iChunk])
      continue;
    aBatch[iCount++] = aCurrent[iChunk];
    if (iCount == NET_WORLD_CHANGE_MAX_ENTRIES) {
      if (!NetHostSendWorldChanges(pHost, uiTick, aBatch, iCount))
        iOk = 0;
      iCount = 0;
    }
  }
  if (iCount && !NetHostSendWorldChanges(pHost, uiTick, aBatch, iCount))
    iOk = 0;

  for (int iChunk = 0; iChunk < pHost->iTrackLen; ++iChunk) {
    if (!abyChanged[iChunk])
      continue;
    pHost->aWorld[iChunk] = aCurrent[iChunk];
    pHost->abyWorldMutated[iChunk] = 1;
  }
  return iOk;
}

int NetHostTick(tNetHost *pHost, uint32 uiTick)
{
  tCopyData aInputs[MAX_CARS];
  tNetSnapshot *pSnapshot;
  int aiLapBefore[MAX_CARS];
  int aiLovebunChunk[MAX_CARS];
  uint8 abyFinishedBefore[MAX_CARS];
  uint8 abyLivesBefore[MAX_CARS], abyKillsBefore[MAX_CARS];
  uint8 abyLovebunAmmo[MAX_CARS];
  int iCommitsOk;
  if (!pHost || !pHost->byRacing || pHost->lifecycle.byPaused ||
      uiTick != pHost->uiNextTick ||
      TRAK_LEN != pHost->iTrackLen)
    return 0;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    aiLapBefore[iCar] = (int)(int8)Car[iCar].byLap;
    abyFinishedBefore[iCar] = finished_car[iCar] != 0;
    abyLivesBefore[iCar] = Car[iCar].byLives;
    abyKillsBefore[iCar] = Car[iCar].byKills;
    aiLovebunChunk[iCar] = -1;
    abyLovebunAmmo[iCar] = Car[iCar].byCheatAmmo;
    if (Car[iCar].byCarDesignIdx == 12 && !Car[iCar].byCheatCooldown &&
        Car[iCar].byCheatAmmo && Car[iCar].nCurrChunk >= 0 &&
        Car[iCar].nCurrChunk < pHost->iTrackLen)
      aiLovebunChunk[iCar] = Car[iCar].nCurrChunk;
  }
  memset(aInputs, 0, sizeof(aInputs));
  for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetHostPlayer *pPlayer = &pHost->aPlayers[iPlayer];
    tNetHostInputSlot *pSlot = &pPlayer->aQueue[uiTick % NET_INPUT_QUEUE];
    if (!pPlayer->byActive)
      continue;
    ++pPlayer->uiLateWindowTicks;
    if (pSlot->byValid && pSlot->uiTick == uiTick) {
      memcpy(pPlayer->aLast, pSlot->aInput, sizeof(pPlayer->aLast));
    } else {
      /* A miss repeats the last input and is counted (4.3 step 1). */
      ++pPlayer->stats.uiLateInputs;
      ++pPlayer->uiLateWindowMisses;
      ++g_netStats.iLateInputs;
    }
    if (pPlayer->uiLateWindowTicks >= pHost->config.unTickRateHz) {
      pPlayer->stats.fLateInputRate =
          (float)pPlayer->uiLateWindowMisses /
          (float)pPlayer->uiLateWindowTicks;
      pPlayer->stats.byLateInputWarning =
          pPlayer->stats.fLateInputRate >= NET_HOST_LATE_WARNING_RATE;
      pPlayer->uiLateWindowTicks = 0;
      pPlayer->uiLateWindowMisses = 0;
    }
    pSlot->byValid = 0;
    for (int iCar = 0; iCar < pPlayer->byCarCount; ++iCar)
      aInputs[pPlayer->abyCars[iCar]].data = pPlayer->aLast[iCar];
  }
  if (pHost->pSimulate) {
    if (!pHost->pSimulate(pHost->pSimulateContext, uiTick, aInputs, numcars))
      return 0;
  } else {
    if (!NetSimWriteTickInputs(aInputs, numcars))
      return 0;
    control_one_tick();
  }
  ++pHost->uiNextTick;
  iCommitsOk = NetHostEmitCarEvents(pHost, uiTick, aiLapBefore,
                                    abyFinishedBefore, abyLivesBefore,
                                    abyKillsBefore);
  if (!NetHostEmitWorldChanges(pHost, uiTick, aiLovebunChunk,
                               abyLovebunAmmo))
    iCommitsOk = 0;
  if (!NetHostEmitLifecycle(pHost, uiTick))
    iCommitsOk = 0;

  if (!pHost->byForceSnapshot &&
      (uiTick - pHost->uiStartTick) % pHost->config.bySnapshotInterval)
    return iCommitsOk;
  pHost->byForceSnapshot = 0;
  pSnapshot = &pHost->aRing[pHost->iRingNext].snapshot;
  if (!NetSnapshotBuild(pSnapshot, uiTick, pHost->uiLastEventSeq,
                        pHost->lifecycle.byState,
                        pHost->lifecycle.byPaused)) {
    pHost->aRing[pHost->iRingNext].byValid = 0;
    return 0;
  }
  pHost->aRing[pHost->iRingNext].byValid = 1;
  pHost->iRingNext = (pHost->iRingNext + 1) % NET_HOST_SNAPSHOT_RING;
  pHost->uiNewestSnapshotTick = uiTick;
  pHost->byHasSnapshot = 1;
  NetHostSendSnapshot(pHost, pSnapshot);
  return iCommitsOk;
}

uint32 NetHostNextTick(const tNetHost *pHost)
{
  return pHost ? pHost->uiNextTick : 0;
}

int NetHostSetPaused(tNetHost *pHost, int iPaused)
{
  tNetPause pause;
  uint8 abPause[sizeof(tNetPause)];
  uint16 unRevision;
  int iLength, iQueued;
  if (!pHost || !pHost->byRacing || !NetRacePauseAllowed(&pHost->config))
    return 0;
  iPaused = iPaused != 0;
  if (pHost->lifecycle.byPaused == (uint8)iPaused)
    return 1;
  unRevision = (uint16)(pHost->lifecycle.unPauseRevision + 1u);
  if (!unRevision)
    unRevision = 1;
  memset(&pause, 0, sizeof(pause));
  pause.unPauseRevision = unRevision;
  pause.byPaused = (uint8)iPaused;
  pause.uiTick = pHost->uiNextTick;
  iLength = NetPauseEncode(&pause, abPause, sizeof(abPause));
  if (!iLength || !NetRaceApplyPause(&pHost->lifecycle, unRevision,
                                      pause.byPaused))
    return 0;
  iQueued = NetHostBroadcastCommit(pHost, NET_MSG_PAUSE, abPause,
                                   (uint16)iLength);
  return iQueued;
}

int NetHostPaused(const tNetHost *pHost)
{
  return pHost && pHost->byRacing && pHost->lifecycle.byPaused;
}

uint16 NetHostPauseRevision(const tNetHost *pHost)
{
  return pHost ? pHost->lifecycle.unPauseRevision : 0;
}

eNetRaceState NetHostRaceState(const tNetHost *pHost)
{
  return pHost ? (eNetRaceState)pHost->lifecycle.byState :
      NET_RACE_STOPPED;
}

int NetHostResults(const tNetHost *pHost, int *piFinishers,
                   int *piHumanFinishers)
{
  if (!pHost || !pHost->byResultsPublished)
    return 0;
  if (piFinishers)
    *piFinishers = pHost->iResultFinishers;
  if (piHumanFinishers)
    *piHumanFinishers = pHost->iResultHumanFinishers;
  return 1;
}

int NetHostSetLocalInputs(tNetHost *pHost, uint8 byPlayerIdx, uint32 uiTick,
                          const tCarInputData *pInputs, int iCount)
{
  tNetHostPlayer *pPlayer;
  tNetHostInputSlot *pSlot;
  if (!pHost || !pHost->byRacing || pHost->lifecycle.byPaused || !pInputs ||
      byPlayerIdx >= NET_SESSION_MAX_PLAYERS || uiTick != pHost->uiNextTick)
    return 0;
  pPlayer = &pHost->aPlayers[byPlayerIdx];
  if (!pPlayer->byActive || iCount != pPlayer->byCarCount)
    return 0;
  pSlot = &pPlayer->aQueue[uiTick % NET_INPUT_QUEUE];
  memset(pSlot, 0, sizeof(*pSlot));
  pSlot->uiTick = uiTick;
  pSlot->byValid = 1;
  for (int iLocal = 0; iLocal < iCount; ++iLocal) {
    pSlot->aInput[iLocal] = pInputs[iLocal];
    if (NetInputClamp(&pSlot->aInput[iLocal], pPlayer->abyCars[iLocal]))
      ++pPlayer->stats.uiClampedInputs;
  }
  return 1;
}

void NetHostSetSimulation(tNetHost *pHost, tNetHostSimulateFn pSimulate,
                          void *pContext)
{
  if (!pHost)
    return;
  pHost->pSimulate = pSimulate;
  pHost->pSimulateContext = pSimulate ? pContext : NULL;
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

uint32 NetHostLastEventSeq(const tNetHost *pHost)
{
  return pHost ? pHost->uiLastEventSeq : 0;
}

int NetHostWorldChangeAt(const tNetHost *pHost, int iChunk,
                         tNetWorldChangeEntry *pEntry)
{
  if (!pHost || !pEntry || iChunk < 0 || iChunk >= pHost->iTrackLen ||
      !pHost->abyWorldMutated[iChunk])
    return 0;
  *pEntry = pHost->aWorld[iChunk];
  return 1;
}

int NetHostPlayerStats(const tNetHost *pHost, uint8 byPlayerIdx,
                       tNetHostPlayerStats *pStats)
{
  tNetConnection *pConnection;
  if (!pHost || !pStats || byPlayerIdx >= NET_SESSION_MAX_PLAYERS ||
      !pHost->aPlayers[byPlayerIdx].byActive)
    return 0;
  *pStats = pHost->aPlayers[byPlayerIdx].stats;
  pConnection = NetSessionHostPlayerConnection(pHost->pSession, byPlayerIdx);
  pStats->fRttMs = NetConnectionRttMs(pConnection);
  return 1;
}
