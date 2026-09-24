#include "net_bot.h"

#include "net_checkpoint.h"
#include "net_event.h"
#include "net_sim_seam.h"
#include "net_snapshot.h"
#include "3d.h"
#include "loadtrak.h"
#include "moving.h"

#include <stdlib.h>
#include <string.h>

#define NET_BOT_SNAPSHOT_RING 64

typedef struct
{
  uint32 uiTick;
  tCarInputData input;
  uint8 byValid;
} tNetBotInputSlot;

typedef struct
{
  tNetSnapshot snapshot;
  uint8 byValid;
} tNetBotSnapshotSlot;

typedef struct
{
  tNetCheckpointHeader header;
  tNetCarFullState aCars[MAX_CARS];
  tNetPlayerEntry aPlayers[NET_SESSION_MAX_PLAYERS];
  uint8 abyCarReceived[MAX_CARS], abyWorldReceived[MAX_TRACK_CHUNKS];
  uint8 byHasHeader, byHasPlayers, byCarParts, byWorldParts;
  int iWorldCount;
} tNetBotCheckpoint;

struct tNetBot
{
  tNetSessionClient *pSession;
  tNetLobbyClient *pLobby;
  tNetSessionConfig config;
  eNetBotState state;
  tNetBotStats stats;
  tNetBotInputSlot aInputs[NET_INPUT_REDUNDANCY];
  tNetBotSnapshotSlot aSnapshots[NET_BOT_SNAPSHOT_RING];
  tNetBotCheckpoint checkpoint;
  uint64 ullCheckpointRequestMs;
  uint32 uiLocalTrackCRC, uiNextTick, uiInputBaseTick;
  uint8 byCarIdx, byHumanControl;
  uint8 byStarted, byHasConfig, byInfoSent, byReadySent, byLoadedSent;
  uint8 byCheckpointInstalled;
};

static int NetBotTickNewer(uint32 uiA, uint32 uiB)
{
  return (int32)(uiA - uiB) > 0;
}

static int NetBotInstallCheckpoint(tNetBot *pBot)
{
  const tNetBotCheckpoint *pStage = &pBot->checkpoint;
  uint8 byMe = NetSessionClientPlayerIndex(pBot->pSession);
  int iExpectedCarParts;
  if (!pStage->byHasHeader || !pStage->byHasPlayers ||
      pStage->header.byNumCars != numcars ||
      pStage->header.byNumPlayers != pBot->config.byMaxPlayers ||
      pStage->header.byNumWorldParts != pStage->byWorldParts ||
      pStage->header.byNumCarParts != pStage->byCarParts ||
      byMe >= pStage->header.byNumPlayers)
    return 0;
  iExpectedCarParts =
      (numcars + NET_CHECKPOINT_CARS_PER_MESSAGE - 1) /
      NET_CHECKPOINT_CARS_PER_MESSAGE;
  if (pStage->header.byNumCarParts != iExpectedCarParts)
    return 0;
  for (int iCar = 0; iCar < numcars; ++iCar)
    if (!pStage->abyCarReceived[iCar] ||
        !NetSnapshotCarFullValid(iCar, &pStage->aCars[iCar]))
      return 0;
  for (int iPlayer = 0; iPlayer < pStage->header.byNumPlayers; ++iPlayer) {
    const tNetPlayerEntry *pPlayer = &pStage->aPlayers[iPlayer];
    if (pPlayer->byState == NET_PLAYER_EMPTY)
      continue;
    if (pPlayer->byCarIdx0 >= numcars ||
        (pPlayer->byCarIdx1 != NET_LOBBY_NO_PLAYER &&
         pPlayer->byCarIdx1 >= numcars))
      return 0;
  }
  {
    const tNetPlayerEntry *pMe = &pStage->aPlayers[byMe];
    if (pMe->byState != NET_PLAYER_RACING ||
        pMe->byCarIdx0 != pBot->byCarIdx ||
        pMe->byCarIdx1 != NET_LOBBY_NO_PLAYER ||
        pMe->byHumanControl != pBot->byHumanControl)
      return 0;
  }
  for (int iRamp = 0; iRamp < totalramps; ++iRamp) {
    tNetRampState state = pStage->header.aRamps[iRamp];
    if (!NetSimAdvanceRampStateCopy(iRamp, &state, 0))
      return 0;
  }
  memset(pBot->aInputs, 0, sizeof(pBot->aInputs));
  memset(pBot->aSnapshots, 0, sizeof(pBot->aSnapshots));
  pBot->byCheckpointInstalled = 1;
  ++pBot->stats.uiCheckpoints;
  return 1;
}

static void NetBotReceiveCheckpoint(tNetBot *pBot,
                                    const tNetMessage *pMessage)
{
  tNetBotCheckpoint *pStage = &pBot->checkpoint;
  if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
      pBot->state != NET_BOT_RECOVERING || pBot->byCheckpointInstalled)
    goto reject;
  switch (pMessage->byType) {
    case NET_MSG_CHECKPOINT_HEADER: {
      tNetCheckpointHeader header;
      if (!NetCheckpointHeaderDecode(pMessage->abData, pMessage->unLength,
                                     &header) ||
          header.byNumCars != numcars ||
          header.byNumPlayers != pBot->config.byMaxPlayers ||
          header.byNumWorldParts > NET_CHECKPOINT_MAX_WORLD_PARTS)
        goto reject;
      memset(pStage, 0, sizeof(*pStage));
      pStage->header = header;
      pStage->byHasHeader = 1;
      return;
    }
    case NET_MSG_CHECKPOINT_CARS: {
      tNetCarFullState aCars[NET_CHECKPOINT_CARS_PER_MESSAGE];
      uint8 byFirst;
      int iCount;
      if (!pStage->byHasHeader ||
          !NetCheckpointCarsDecode(pMessage->abData, pMessage->unLength,
                                   &byFirst, aCars, &iCount) ||
          byFirst + iCount > pStage->header.byNumCars)
        goto reject;
      for (int iCar = 0; iCar < iCount; ++iCar)
        if (pStage->abyCarReceived[byFirst + iCar])
          goto reject;
      memcpy(pStage->aCars + byFirst, aCars,
             (size_t)iCount * sizeof(aCars[0]));
      memset(pStage->abyCarReceived + byFirst, 1, (size_t)iCount);
      ++pStage->byCarParts;
      return;
    }
    case NET_MSG_CHECKPOINT_PLAYERS: {
      int iCount;
      if (!pStage->byHasHeader || pStage->byHasPlayers ||
          !NetCheckpointPlayersDecode(pMessage->abData, pMessage->unLength,
                                      pStage->aPlayers,
                                      NET_SESSION_MAX_PLAYERS, &iCount) ||
          iCount != pStage->header.byNumPlayers)
        goto reject;
      pStage->byHasPlayers = 1;
      return;
    }
    case NET_MSG_CHECKPOINT_WORLD: {
      tNetWorldChangeEntry aEntries[NET_WORLD_CHANGE_MAX_ENTRIES];
      int iCount;
      if (!pStage->byHasHeader ||
          !NetCheckpointWorldDecode(pMessage->abData, pMessage->unLength,
                                    TRAK_LEN, aEntries,
                                    NET_WORLD_CHANGE_MAX_ENTRIES, &iCount) ||
          pStage->byWorldParts >= pStage->header.byNumWorldParts ||
          pStage->iWorldCount + iCount > MAX_TRACK_CHUNKS)
        goto reject;
      for (int iEntry = 0; iEntry < iCount; ++iEntry)
        if (pStage->abyWorldReceived[aEntries[iEntry].nChunk])
          goto reject;
      for (int iEntry = 0; iEntry < iCount; ++iEntry)
        pStage->abyWorldReceived[aEntries[iEntry].nChunk] = 1;
      pStage->iWorldCount += iCount;
      ++pStage->byWorldParts;
      return;
    }
    case NET_MSG_CHECKPOINT_END: {
      uint32 uiTick;
      if (!pStage->byHasHeader ||
          !NetCheckpointEndDecode(pMessage->abData, pMessage->unLength,
                                  &uiTick) ||
          uiTick != pStage->header.uiTick ||
          !NetBotInstallCheckpoint(pBot))
        goto reject;
      return;
    }
    default:
      return;
  }

reject:
  ++pBot->stats.uiRejectedMessages;
}

static void NetBotReceiveSnapshot(tNetBot *pBot,
                                  const tNetMessage *pMessage)
{
  tNetSnapshot snapshot;
  int iDecoded = 0;
  if (pMessage->byFlags)
    goto reject;
  if (pMessage->byType == NET_MSG_SNAPSHOT) {
    iDecoded = NetSnapshotDecode(pMessage->abData, pMessage->unLength,
                                 &snapshot);
  } else {
    uint32 uiTick, uiBaseTick;
    if (NetSnapshotDeltaTicks(pMessage->abData, pMessage->unLength,
                              &uiTick, &uiBaseTick)) {
      const tNetBotSnapshotSlot *pBase =
          &pBot->aSnapshots[uiBaseTick % NET_BOT_SNAPSHOT_RING];
      if (pBase->byValid && pBase->snapshot.uiTick == uiBaseTick)
        iDecoded = NetSnapshotDecodeDelta(
            &pBase->snapshot, pMessage->abData, pMessage->unLength,
            &snapshot) && snapshot.uiTick == uiTick;
      else
        ++pBot->stats.uiDroppedDeltas;
    }
  }
  if (!iDecoded || snapshot.byNumCars != numcars ||
      snapshot.byNumRamps != totalramps)
    goto reject;
  if (pBot->state == NET_BOT_RECOVERING) {
    if (!pBot->byCheckpointInstalled ||
        !NetBotTickNewer(snapshot.uiTick, pBot->checkpoint.header.uiTick))
      return;
    pBot->uiNextTick = snapshot.uiTick + 1u;
    pBot->uiInputBaseTick = pBot->uiNextTick;
    pBot->state = NET_BOT_RACING;
    ++pBot->stats.uiRejoins;
  }
  pBot->aSnapshots[snapshot.uiTick % NET_BOT_SNAPSHOT_RING].snapshot = snapshot;
  pBot->aSnapshots[snapshot.uiTick % NET_BOT_SNAPSHOT_RING].byValid = 1;
  if (!pBot->stats.uiSnapshots ||
      NetBotTickNewer(snapshot.uiTick,
                      pBot->stats.uiLastDecodedSnapshotTick))
    pBot->stats.uiLastDecodedSnapshotTick = snapshot.uiTick;
  ++pBot->stats.uiSnapshots;
  return;

reject:
  ++pBot->stats.uiRejectedMessages;
}

static void NetBotRaceMessage(void *pContext, const tNetMessage *pMessage)
{
  tNetBot *pBot = (tNetBot *)pContext;
  if (!pBot || !pMessage)
    return;
  if (pMessage->byType == NET_MSG_SNAPSHOT ||
      pMessage->byType == NET_MSG_SNAPSHOT_DELTA) {
    NetBotReceiveSnapshot(pBot, pMessage);
  } else if (pMessage->byType == NET_MSG_EVENT) {
    tNetEvent event;
    if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
        !NetEventDecode(pMessage->abData, pMessage->unLength, numcars,
                        pBot->config.byMaxPlayers, &event)) {
      ++pBot->stats.uiRejectedMessages;
      return;
    }
    if (event.byCarIdx == pBot->byCarIdx) {
      if (event.byType == NET_EV_LAP_COMPLETE)
        ++pBot->stats.uiLapCompletions;
      else if (event.byType == NET_EV_FINISHED)
        pBot->stats.byFinished = 1;
    }
  } else if (pMessage->byType == NET_MSG_INPUT_FEEDBACK) {
    tNetInputFeedback feedback;
    if (!NetInputFeedbackDecode(pMessage->abData, pMessage->unLength,
                                &feedback))
      ++pBot->stats.uiRejectedMessages;
  } else if (pMessage->byType == NET_MSG_CHECKPOINT_HEADER ||
             pMessage->byType == NET_MSG_CHECKPOINT_CARS ||
             pMessage->byType == NET_MSG_CHECKPOINT_PLAYERS ||
             pMessage->byType == NET_MSG_CHECKPOINT_WORLD ||
             pMessage->byType == NET_MSG_CHECKPOINT_END) {
    NetBotReceiveCheckpoint(pBot, pMessage);
  }
  /* Own-car state, world changes and pause are safe to ignore here.  This
     endpoint bot validates recovery checkpoints but installs no world. */
}

tNetBot *NetBotCreate(tNetConnection *pConnection, const char *szName,
                      uint8 byCarIdx, uint8 byHumanControl,
                      uint32 uiLocalTrackCRC)
{
  tNetBot *pBot;
  if (!pConnection || !szName || !szName[0] ||
      byCarIdx >= NET_SESSION_MAX_PLAYERS ||
      (byHumanControl != 1 && byHumanControl != 2))
    return NULL;
  pBot = (tNetBot *)calloc(1, sizeof(*pBot));
  if (!pBot)
    return NULL;
  pBot->pSession = NetSessionClientCreate(
      pConnection, NET_PROTOCOL_VERSION, 1, szName);
  if (!pBot->pSession) {
    free(pBot);
    return NULL;
  }
  pBot->pLobby = NetLobbyClientCreate(pBot->pSession);
  if (!pBot->pLobby) {
    NetSessionClientDestroy(pBot->pSession);
    free(pBot);
    return NULL;
  }
  pBot->state = NET_BOT_JOINING;
  pBot->byCarIdx = byCarIdx;
  pBot->byHumanControl = byHumanControl;
  pBot->uiLocalTrackCRC = uiLocalTrackCRC;
  NetLobbyClientSetRaceCallback(pBot->pLobby, NetBotRaceMessage, pBot);
  return pBot;
}

void NetBotDestroy(tNetBot *pBot)
{
  if (!pBot)
    return;
  NetLobbyClientDestroy(pBot->pLobby);
  NetSessionClientDestroy(pBot->pSession);
  free(pBot);
}

int NetBotStart(tNetBot *pBot)
{
  if (!pBot || pBot->byStarted || !NetSessionClientStart(pBot->pSession))
    return 0;
  pBot->byStarted = 1;
  return 1;
}

void NetBotPump(tNetBot *pBot)
{
  uint32 uiStartTick;
  eNetJoinState joinState;
  if (!pBot || !pBot->byStarted || pBot->state == NET_BOT_ERROR)
    return;
  NetSessionClientPump(pBot->pSession);
  joinState = NetSessionClientState(pBot->pSession);
  if (joinState == NET_JOIN_REFUSED) {
    pBot->state = NET_BOT_REFUSED;
    return;
  }
  if (joinState != NET_JOIN_ACCEPTED)
    return;
  if (pBot->state == NET_BOT_RECOVERING) {
    uint64 ullNowMs = NetConnectionNowMs(
        NetSessionClientConnection(pBot->pSession));
    if (ullNowMs - pBot->ullCheckpointRequestMs >= 2000u) {
      NetConnectionQueueMessage(NetSessionClientConnection(pBot->pSession),
          NET_MSG_CHECKPOINT_REQUEST, NET_MSG_RELIABLE | NET_MSG_ORDERED,
          NULL, 0);
      pBot->ullCheckpointRequestMs = ullNowMs;
    }
    return;
  }
  if (!pBot->byHasConfig) {
    if (!NetSessionClientGetConfig(pBot->pSession, &pBot->config))
      return;
    if (pBot->byCarIdx >= pBot->config.byMaxPlayers) {
      pBot->state = NET_BOT_ERROR;
      return;
    }
    pBot->byHasConfig = 1;
  }
  if (!pBot->byInfoSent) {
    if (!NetLobbyClientSetPlayerInfo(pBot->pLobby, pBot->byCarIdx,
                                     NET_LOBBY_NO_PLAYER,
                                     pBot->byHumanControl)) {
      pBot->state = NET_BOT_ERROR;
      return;
    }
    pBot->byInfoSent = 1;
  }
  if (!pBot->byReadySent) {
    if (!NetLobbyClientSetReady(pBot->pLobby, 1,
                                pBot->uiLocalTrackCRC)) {
      pBot->state = NET_BOT_ERROR;
      return;
    }
    pBot->byReadySent = 1;
    pBot->state = NET_BOT_LOBBY;
  }
  if (!NetLobbyClientStartTick(pBot->pLobby, &uiStartTick))
    return;
  if (!pBot->byLoadedSent) {
    if (!NetLobbyClientSetRaceLoaded(pBot->pLobby)) {
      pBot->state = NET_BOT_ERROR;
      return;
    }
    pBot->byLoadedSent = 1;
    pBot->stats.uiStartTick = uiStartTick;
    pBot->uiNextTick = uiStartTick;
    pBot->uiInputBaseTick = uiStartTick;
    pBot->state = NET_BOT_LOADING;
  }
  if (NetLobbyClientRaceReleased(pBot->pLobby, &uiStartTick)) {
    if (uiStartTick != pBot->stats.uiStartTick) {
      pBot->state = NET_BOT_ERROR;
      return;
    }
    pBot->state = NET_BOT_RACING;
  }
}

eNetBotState NetBotState(const tNetBot *pBot)
{
  return pBot ? pBot->state : NET_BOT_ERROR;
}

eNetJoinRefuseReason NetBotRefuseReason(const tNetBot *pBot)
{
  return pBot ? NetSessionClientRefuseReason(pBot->pSession) :
      NET_JOIN_REFUSE_INVALID_REQUEST;
}

uint64 NetBotSessionToken(const tNetBot *pBot)
{
  return pBot ? NetSessionClientToken(pBot->pSession) : 0;
}

uint8 NetBotGeneration(const tNetBot *pBot)
{
  return pBot ? NetSessionClientGeneration(pBot->pSession) : 0;
}

int NetBotBeginRejoin(tNetBot *pBot, tNetConnection *pConnection)
{
  if (!pBot || !pConnection || pBot->state != NET_BOT_RACING ||
      !NetSessionClientRejoin(pBot->pSession, pConnection))
    return 0;
  memset(pBot->aInputs, 0, sizeof(pBot->aInputs));
  memset(pBot->aSnapshots, 0, sizeof(pBot->aSnapshots));
  memset(&pBot->checkpoint, 0, sizeof(pBot->checkpoint));
  pBot->byCheckpointInstalled = 0;
  pBot->ullCheckpointRequestMs = NetConnectionNowMs(pConnection);
  pBot->state = NET_BOT_RECOVERING;
  return 1;
}

int NetBotTick(tNetBot *pBot, uint32 uiTick,
               const tCarInputData *pInput)
{
  tNetInputBatch batch;
  tCarInputData input = {0};
  uint32 uiFirstTick;
  uint8 abData[NET_INPUT_BATCH_MAX_BYTES];
  int iLength;
  if (!pBot || pBot->state != NET_BOT_RACING ||
      uiTick != pBot->uiNextTick)
    return 0;
  if (pInput)
    input = *pInput;
  else
    input.unFlags = BUTTON_FLAG_ACCEL;
  NetSimCanonicaliseInput(&input);
  pBot->aInputs[uiTick % NET_INPUT_REDUNDANCY].uiTick = uiTick;
  pBot->aInputs[uiTick % NET_INPUT_REDUNDANCY].input = input;
  pBot->aInputs[uiTick % NET_INPUT_REDUNDANCY].byValid = 1;
  if (uiTick - pBot->uiInputBaseTick < NET_INPUT_REDUNDANCY - 1u)
    uiFirstTick = pBot->uiInputBaseTick;
  else
    uiFirstTick = uiTick - (NET_INPUT_REDUNDANCY - 1u);
  memset(&batch, 0, sizeof(batch));
  batch.uiFirstTick = uiFirstTick;
  batch.uiLastDecodedSnapshotTick =
      pBot->stats.uiLastDecodedSnapshotTick;
  batch.byCount = (uint8)(uiTick - uiFirstTick + 1u);
  batch.byLocalPlayers = 1;
  for (int iTick = 0; iTick < batch.byCount; ++iTick) {
    uint32 uiHistoryTick = uiFirstTick + (uint32)iTick;
    const tNetBotInputSlot *pSlot =
        &pBot->aInputs[uiHistoryTick % NET_INPUT_REDUNDANCY];
    if (!pSlot->byValid || pSlot->uiTick != uiHistoryTick)
      return 0;
    batch.aInputs[iTick][0] = pSlot->input;
  }
  iLength = NetInputBatchEncode(&batch, abData, sizeof(abData));
  if (!iLength || !NetConnectionQueueMessage(
          NetSessionClientConnection(pBot->pSession), NET_MSG_INPUT, 0,
          abData, (uint16)iLength))
    return 0;
  ++pBot->uiNextTick;
  ++pBot->stats.uiInputsSent;
  return 1;
}

uint32 NetBotNextTick(const tNetBot *pBot)
{
  return pBot ? pBot->uiNextTick : 0;
}

int NetBotStats(const tNetBot *pBot, tNetBotStats *pStats)
{
  if (!pBot || !pStats)
    return 0;
  *pStats = pBot->stats;
  return 1;
}
