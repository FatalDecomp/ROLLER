/* NET-E3-S1/S2 acceptance: host tick, semantic events and world changes.

   One process holds the one simulation world, the host's (D13).  The three
   clients are session, lobby and message endpoints only: they send scripted
   input batches and decode what the host sends, but simulate nothing, so
   they need no world of their own.  Client prediction is E4-S1. */
#include "net_headless.h"
#include "net_event.h"
#include "net_host.h"
#include "net_input.h"
#include "net_race_start.h"
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

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #iCondition); exit(1); \
} } while (0)

#define NET_TEST_CLIENTS 3
#define NET_TEST_LATENCY_MS 100
#define NET_TEST_LEAD_TICKS 8
#define NET_TEST_START_TICK 5000u
#define NET_TEST_RUNNING_TICKS 2000
#define NET_TEST_REFERENCE_TICKS 400
#define NET_TEST_PROBE_TICK 300
#define NET_TEST_WARMUP_TICKS 16
#define NET_TEST_MAX_TICKS 2400
#define NET_TEST_MAX_SNAPSHOTS (NET_TEST_MAX_TICKS / 2 + 8)
#define NET_TEST_FORBIDDEN (FLAG_DISCONNECT | BUTTON_FLAG_F1 | 0x4000 | 0x8000)
#define NET_TEST_MAX_EVENTS 128
#define NET_TEST_MAX_WORLD_MESSAGES 8
#define NET_TEST_LAP_TICK (NET_TEST_START_TICK + 155u)
#define NET_TEST_FINISH_TICK (NET_TEST_START_TICK + 165u)
#define NET_TEST_LOVEBUN_TICK (NET_TEST_START_TICK + 175u)

typedef struct
{
  uint64 ullState;
} tNetTestRandom;

typedef struct
{
  tNetChannel *pChannel;
  tNetConnection *pConnection;
  tNetSessionClient *pSession;
  tNetLobbyClient *pLobby;
  uint8 byPlayerIdx, byCar;
  int iSnapshots, iOwnStates, iFeedback, iEvents, iWorldChanges;
  int iCommitSeqs, iBadMessages;
  uint32 uiNewestSnapshotTick;
  uint32 auiSnapshotTick[NET_TEST_MAX_SNAPSHOTS];
  uint32 auiSnapshotEventSeq[NET_TEST_MAX_SNAPSHOTS];
  int aiSnapshotFrame[NET_TEST_MAX_SNAPSHOTS];
  uint32 auiSnapshotRandom[NET_TEST_MAX_SNAPSHOTS];
  uint8 abySnapshotHuman[NET_TEST_MAX_SNAPSHOTS];
  uint32 auiOwnTick[NET_TEST_MAX_SNAPSHOTS];
  uint8 abyOwnCar[NET_TEST_MAX_SNAPSHOTS];
  int aiOwnCount[NET_TEST_MAX_SNAPSHOTS];
  tNetCarExtra aOwnExtra[NET_TEST_MAX_SNAPSHOTS];
  tNetInputFeedback lastFeedback;
  tNetEvent aEvents[NET_TEST_MAX_EVENTS];
  uint32 auiCommitSeq[NET_TEST_MAX_EVENTS + NET_TEST_MAX_WORLD_MESSAGES];
  tNetWorldChangeHeader aWorldHeaders[NET_TEST_MAX_WORLD_MESSAGES];
  tNetWorldChangeEntry
      aaWorldEntries[NET_TEST_MAX_WORLD_MESSAGES][NET_WORLD_CHANGE_MAX_ENTRIES];
} tNetTestClient;

typedef struct
{
  int iTicks, iRunningIndex;
  int aiFrame[NET_TEST_MAX_TICKS];
  uint32 auiRandom[NET_TEST_MAX_TICKS];
  uint32 auiWorldHash[NET_TEST_MAX_TICKS];
  tCopyData aWritten[NET_TEST_MAX_TICKS][NET_TEST_CLIENTS];
  tNetCarExtra aExtra[NET_TEST_MAX_TICKS][NET_TEST_CLIENTS];
  tNetHostPlayerStats aWarmStats[NET_TEST_CLIENTS];
  tNetHostPlayerStats aFinalStats[NET_TEST_CLIENTS];
  float afSpeedAtRunning[NET_TEST_CLIENTS];
  int aiChunkAtRunning[NET_TEST_CLIENTS];
  int aiLapEvents[MAX_CARS], aiFinishedEvents[MAX_CARS];
  int aiDestroyedEvents[MAX_CARS], aiKillEvents[MAX_CARS];
  int iLovebunUses, iLovebunCar, iWorldEntries;
  int iRaceState, iResultFinishers, iResultHumanFinishers;
  uint32 uiSettleTick;
  int iRestoreLap, iRestoreFinish, iSavedFinishers;
  uint8 bySavedLap, bySavedRacePosition, bySavedLovebunDesign;
  float fSavedPreviousLapTime;
  uint32 uiLastEventSeq;
  tNetWorldChangeEntry aWorldEntries[NET_WORLD_CHANGE_MAX_ENTRIES];
} tNetTestRun;

typedef struct
{
  int iSawAirborneAi, iLanded, iAiTicks, iMoved;
  float afLandedX[2], afLandedY[2];
} tNetTakeoverRun;

typedef struct
{
  tCar aCars[MAX_CARS];
  tNetRampState aRamps[NET_MAX_RAMPS];
  tNetSimTickContext context;
  tCopyData aRing[512][16];
  int aiHumanControl[16];
  int iTrackLen;
  int aiTrakColour[MAX_TRACK_CHUNKS][3];
  int aiGrip[MAX_TRACK_CHUNKS][3];
} tNetTestMoment;

static tNetTestClient s_aClients[NET_TEST_CLIENTS];
static tNetTestRun s_phoneRun, s_accelRun;
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
  pMoment->iTrackLen = TRAK_LEN;
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
    memcpy(pMoment->aiTrakColour[iChunk], TrakColour[iChunk],
           sizeof(pMoment->aiTrakColour[iChunk]));
    pMoment->aiGrip[iChunk][0] = localdata[iChunk].iCenterGrip;
    pMoment->aiGrip[iChunk][1] = localdata[iChunk].iLeftShoulderGrip;
    pMoment->aiGrip[iChunk][2] = localdata[iChunk].iRightShoulderGrip;
  }
}

static void NetTestRestore(const tNetTestMoment *pMoment)
{
  CHECK(NetSimRestoreRamps(pMoment->aRamps));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimRestoreCar(iCar, &pMoment->aCars[iCar]);
  NetSimRestoreContext(&pMoment->context);
  memcpy(copy_multiple, pMoment->aRing, sizeof(copy_multiple));
  memcpy(human_control, pMoment->aiHumanControl, sizeof(human_control));
  CHECK(TRAK_LEN == pMoment->iTrackLen);
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
    memcpy(TrakColour[iChunk], pMoment->aiTrakColour[iChunk],
           sizeof(pMoment->aiTrakColour[iChunk]));
    localdata[iChunk].iCenterGrip = pMoment->aiGrip[iChunk][0];
    localdata[iChunk].iLeftShoulderGrip = pMoment->aiGrip[iChunk][1];
    localdata[iChunk].iRightShoulderGrip = pMoment->aiGrip[iChunk][2];
  }
}

static uint32 NetTestWorldHash(void)
{
  uint32 uiHash = 2166136261u;
  const uint8 *pBytes = (const uint8 *)Car;
  for (size_t uiByte = 0; uiByte < (size_t)numcars * sizeof(tCar); ++uiByte)
    uiHash = (uiHash ^ pBytes[uiByte]) * 16777619u;
  uiHash = (uiHash ^ ROLLERrandStateGet()) * 16777619u;
  return (uiHash ^ (uint32)ROLLERrandDrawCountGet()) * 16777619u;
}

/* What each client drives with.  Client 1 is the phone-throttle client;
   client 2 sends forbidden flags and out-of-range steering. */
static tCarInputData NetTestScript(int iClient, uint32 uiTick, int iPhone)
{
  tCarInputData input;
  int iPhase = (int)(uiTick % 60u);
  input.unInput = (uint16)(int16)(((int)(uiTick / 20u) % 3 - 1) * (iClient + 1) * 2000);
  input.unFlags = BUTTON_FLAG_ACCEL;
  if (iClient == 0 && iPhase >= 40 && iPhase < 46)
    input.unFlags = BUTTON_FLAG_BRAKE;
  if (iClient == 1 && iPhone)
    input.unFlags = BUTTON_FLAG_PHONE_THROTTLE;
  if (iClient == 2) {
    if (uiTick % 7u == 0)
      input.unFlags |= NET_TEST_FORBIDDEN;
    if (uiTick % 11u == 0)
      input.unInput = (uint16)(uiTick % 22u ? 0x7FFF : 0x8000);
  }
  if (iClient == 0 && uiTick == NET_TEST_LOVEBUN_TICK)
    input.unFlags |= BUTTON_FLAG_SPECIAL;
  return input;
}

/* The D9 clamp, restated independently of NetInputClamp. */
static tCarInputData NetTestExpected(tCarInputData input, int iCar)
{
  int iLimit = CarEngines.engines[Car[iCar].byCarDesignIdx].iSteeringSensitivity * 256;
  int iSteering = (int16)input.unInput;
  input.unFlags &= BUTTON_FLAG_ACCEL | BUTTON_FLAG_BRAKE | BUTTON_FLAG_UPGEAR |
                   BUTTON_FLAG_DOWNGEAR | BUTTON_FLAG_SPECIAL | BUTTON_FLAG_PHONE_THROTTLE;
  if (iSteering > iLimit)
    iSteering = iLimit;
  if (iSteering < -iLimit)
    iSteering = -iLimit;
  input.unInput = (uint16)(int16)iSteering;
  return input;
}

static void NetTestClientRace(void *pContext, const tNetMessage *pMessage)
{
  tNetTestClient *pClient = (tNetTestClient *)pContext;
  if (pMessage->byType == NET_MSG_SNAPSHOT) {
    tNetSnapshot snapshot;
    int iEntry = pClient->iSnapshots;
    if (pMessage->byFlags || iEntry >= NET_TEST_MAX_SNAPSHOTS ||
        !NetSnapshotDecode(pMessage->abData, pMessage->unLength, &snapshot)) {
      ++pClient->iBadMessages;
      return;
    }
    pClient->auiSnapshotTick[iEntry] = snapshot.uiTick;
    pClient->auiSnapshotEventSeq[iEntry] = snapshot.uiLastEventSeq;
    pClient->aiSnapshotFrame[iEntry] = snapshot.context.iGameFrame;
    pClient->auiSnapshotRandom[iEntry] = snapshot.uiRandomState;
    pClient->abySnapshotHuman[iEntry] = snapshot.aCars[pClient->byCar].byHumanControl;
    pClient->uiNewestSnapshotTick = snapshot.uiTick;
    ++pClient->iSnapshots;
  } else if (pMessage->byType == NET_MSG_OWN_CAR_STATE) {
    uint8 abyCars[2];
    tNetCarExtra aExtras[2];
    uint32 uiTick;
    int iCount, iEntry = pClient->iOwnStates;
    if (pMessage->byFlags || iEntry >= NET_TEST_MAX_SNAPSHOTS ||
        !NetSnapshotDecodeOwnCarState(pMessage->abData, pMessage->unLength,
                                      &uiTick, abyCars, aExtras, &iCount)) {
      ++pClient->iBadMessages;
      return;
    }
    pClient->auiOwnTick[iEntry] = uiTick;
    pClient->abyOwnCar[iEntry] = abyCars[0];
    pClient->aiOwnCount[iEntry] = iCount;
    pClient->aOwnExtra[iEntry] = aExtras[0];
    ++pClient->iOwnStates;
  } else if (pMessage->byType == NET_MSG_INPUT_FEEDBACK) {
    if (!NetInputFeedbackDecode(pMessage->abData, pMessage->unLength,
                                &pClient->lastFeedback)) {
      ++pClient->iBadMessages;
      return;
    }
    ++pClient->iFeedback;
  } else if (pMessage->byType == NET_MSG_EVENT) {
    int iEntry = pClient->iEvents;
    if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
        iEntry >= NET_TEST_MAX_EVENTS ||
        pClient->iCommitSeqs >= NET_TEST_MAX_EVENTS + NET_TEST_MAX_WORLD_MESSAGES ||
        !NetEventDecode(pMessage->abData, pMessage->unLength, numcars, 4,
                        &pClient->aEvents[iEntry])) {
      ++pClient->iBadMessages;
      return;
    }
    pClient->auiCommitSeq[pClient->iCommitSeqs++] =
        pClient->aEvents[iEntry].uiEventSeq;
    ++pClient->iEvents;
  } else if (pMessage->byType == NET_MSG_WORLD_CHANGE) {
    int iEntry = pClient->iWorldChanges;
    if (pMessage->byFlags != (NET_MSG_RELIABLE | NET_MSG_ORDERED) ||
        iEntry >= NET_TEST_MAX_WORLD_MESSAGES ||
        pClient->iCommitSeqs >= NET_TEST_MAX_EVENTS + NET_TEST_MAX_WORLD_MESSAGES ||
        !NetWorldChangeDecode(pMessage->abData, pMessage->unLength, TRAK_LEN,
                              &pClient->aWorldHeaders[iEntry],
                              pClient->aaWorldEntries[iEntry],
                              NET_WORLD_CHANGE_MAX_ENTRIES)) {
      ++pClient->iBadMessages;
      return;
    }
    pClient->auiCommitSeq[pClient->iCommitSeqs++] =
        pClient->aWorldHeaders[iEntry].uiEventSeq;
    ++pClient->iWorldChanges;
  } else {
    ++pClient->iBadMessages;
  }
}

static int NetTestHostSimulate(void *pContext, uint32 uiTick,
                               const tCopyData *pInputs, int iNumCars)
{
  tNetTestRun *pRun = (tNetTestRun *)pContext;
  tCopyData aInputs[MAX_CARS];
  int iLovebunCar = -1;
  uint8 bySavedDesign = 0;
  memcpy(aInputs, pInputs, sizeof(aInputs));
  if (pRun->iRestoreLap) {
    int iCar = numcars - 2;
    Car[iCar].byLap = (char)pRun->bySavedLap;
    Car[iCar].fPreviousLapTime = pRun->fSavedPreviousLapTime;
    pRun->iRestoreLap = 0;
  }
  if (pRun->iRestoreFinish) {
    int iCar = numcars - 1;
    finished_car[iCar] = 0;
    finishers = pRun->iSavedFinishers;
    Car[iCar].byRacePosition = pRun->bySavedRacePosition;
    pRun->iRestoreFinish = 0;
  }
  if (uiTick == NET_TEST_LOVEBUN_TICK) {
    iLovebunCar = pRun->iLovebunCar;
    if (iLovebunCar < 0 || iLovebunCar >= numcars ||
        Car[iLovebunCar].byCarDesignIdx != 12 ||
        Car[iLovebunCar].byCheatAmmo != 8 ||
        Car[iLovebunCar].byCheatCooldown)
      return 0;
    bySavedDesign = pRun->bySavedLovebunDesign;
    aInputs[iLovebunCar].data.unFlags |= BUTTON_FLAG_SPECIAL;
  }
  if (!NetSimWriteTickInputs(aInputs, iNumCars))
    return 0;
  control_one_tick();
  if (iLovebunCar >= 0) {
    Car[iLovebunCar].byCarDesignIdx = bySavedDesign;
    if (Car[iLovebunCar].byCheatAmmo != 7)
      return 0;
    ++pRun->iLovebunUses;
    pRun->iLovebunCar = iLovebunCar;
  }
  if (uiTick == NET_TEST_LAP_TICK) {
    int iCar = numcars - 2;
    int iLap = (int)(int8)Car[iCar].byLap;
    pRun->bySavedLap = Car[iCar].byLap;
    pRun->fSavedPreviousLapTime = Car[iCar].fPreviousLapTime;
    Car[iCar].byLap = (char)(iLap < 1 ? 2 : iLap + 1);
    Car[iCar].fPreviousLapTime = 5.25f;
    pRun->iRestoreLap = 1;
  }
  if (uiTick == NET_TEST_FINISH_TICK) {
    int iCar = numcars - 1;
    if (finished_car[iCar])
      return 0;
    pRun->iSavedFinishers = finishers;
    pRun->bySavedRacePosition = Car[iCar].byRacePosition;
    finished_car[iCar] = -1;
    Car[iCar].byRacePosition = (uint8)finishers;
    ++finishers;
    pRun->iRestoreFinish = 1;
  }
  if (pRun->uiSettleTick && uiTick == pRun->uiSettleTick) {
    for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
      int iCar = s_aClients[iClient].byCar;
      if (finished_car[iCar])
        return 0;
      finished_car[iCar] = -1;
      Car[iCar].byRacePosition = (uint8)finishers++;
      ++human_finishers;
    }
  }
  return 1;
}

static int NetTestTakeoverSimulate(void *pContext, uint32 uiTick,
                                   const tCopyData *pInputs, int iNumCars)
{
  tNetTakeoverRun *pRun = (tNetTakeoverRun *)pContext;
  (void)uiTick;
  if (!human_control[0] && !human_control[1] &&
      (!Car[0].iControlType || !Car[1].iControlType))
    pRun->iSawAirborneAi = 1;
  if (!NetSimWriteTickInputs(pInputs, iNumCars))
    return 0;
  control_one_tick();
  if (!human_control[0] && !human_control[1] &&
      Car[0].iControlType == 3 && Car[1].iControlType == 3) {
    if (!pRun->iLanded) {
      pRun->iLanded = 1;
      pRun->afLandedX[0] = Car[0].pos.fX;
      pRun->afLandedY[0] = Car[0].pos.fY;
      pRun->afLandedX[1] = Car[1].pos.fX;
      pRun->afLandedY[1] = Car[1].pos.fY;
    }
    ++pRun->iAiTicks;
    if (pRun->iAiTicks >= 20 &&
        (Car[0].pos.fX != pRun->afLandedX[0] ||
         Car[0].pos.fY != pRun->afLandedY[0] ||
         Car[1].pos.fX != pRun->afLandedX[1] ||
         Car[1].pos.fY != pRun->afLandedY[1]))
      pRun->iMoved = 1;
    if (pRun->iAiTicks == 60) {
      for (int iCar = 0; iCar < 2; ++iCar) {
        finished_car[iCar] = -1;
        Car[iCar].byRacePosition = (uint8)finishers++;
      }
    } else if (pRun->iAiTicks == 62) {
      finished_car[2] = -1;
      Car[2].byRacePosition = (uint8)finishers++;
      ++human_finishers;
    }
  }
  return 1;
}

static void NetTestSendRaw(tNetTestClient *pClient, const tNetInputBatch *pBatch)
{
  uint8 abBatch[NET_INPUT_BATCH_MAX_BYTES];
  int iLength = NetInputBatchEncode(pBatch, abBatch, sizeof(abBatch));
  CHECK(iLength);
  CHECK(NetConnectionQueueMessage(pClient->pConnection, NET_MSG_INPUT, 0,
                                  abBatch, (uint16)iLength));
}

/* Redundant batch of the last NET_INPUT_REDUNDANCY ticks up to uiNewest. */
static void NetTestSendInputs(int iClient, uint32 uiNewest, int iPhone)
{
  tNetTestClient *pClient = &s_aClients[iClient];
  tNetInputBatch batch;
  uint32 uiFirst = uiNewest - (NET_INPUT_REDUNDANCY - 1);
  if ((int32)(uiFirst - NET_TEST_START_TICK) < 0)
    uiFirst = NET_TEST_START_TICK;
  memset(&batch, 0, sizeof(batch));
  batch.uiFirstTick = uiFirst;
  batch.uiLastDecodedSnapshotTick = pClient->uiNewestSnapshotTick;
  batch.byCount = (uint8)(uiNewest - uiFirst + 1);
  batch.byLocalPlayers = 1;
  for (int iTick = 0; iTick < batch.byCount; ++iTick)
    batch.aInputs[iTick][0] = NetTestScript(iClient, uiFirst + (uint32)iTick, iPhone);
  NetTestSendRaw(pClient, &batch);
}

static void NetTestPumpAll(tNetTransportSim *pSim, uint64 ullNowMs,
                           tNetSessionHost *pSession, tNetLobbyHost *pLobby,
                           tNetHost *pHost)
{
  CHECK(NetTransportSimAdvance(pSim, ullNowMs));
  NetPump();
  NetSessionHostPump(pSession);
  NetLobbyHostPump(pLobby);
  NetHostPump(pHost);
  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
    NetSessionClientPump(s_aClients[iClient].pSession);
}

static void NetTestRace(tNetTestRun *pRun, int iPhone, int iRunningTicks)
{
  static const char *aszNames[NET_TEST_CLIENTS] = {"Alpha", "Phone", "Rogue"};
  tNetTransportSim *pSim = NetTransportSimCreate(0xe351u);
  tNetSimLink link = {NET_TEST_LATENCY_MS, 0, 0, 0, 0};
  tNetAddress hostAddress;
  tNetSessionConfig config;
  tNetTestRandom random = {0x5eed0e3510000001ull};
  tNetChannel *pHostChannel;
  tNetSessionHost *pSession;
  tNetLobbyHost *pLobby;
  tNetHost *pHost;
  tNetRaceStartClock clock;
  uint64 ullNowMs = 0, ullReleaseMs;
  int iTickIndex = 0, iRunning = -1, iProbed = 0, iLocalInputProbed = 0;
  tNetHostPlayerStats probeStats;
  tNetWorldChangeEntry aWorldBefore[MAX_TRACK_CHUNKS];

  NetTestRestore(&s_pristine);
  memset(&g_netStats, 0, sizeof(g_netStats));
  memset(pRun, 0, sizeof(*pRun));
  pRun->iLovebunCar = -1;
  memset(s_aClients, 0, sizeof(s_aClients));
  CHECK(pSim);
  for (int iEndpoint = 0; iEndpoint <= NET_TEST_CLIENTS; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pSim, iEndpoint, &link));

  memset(&hostAddress, 0, sizeof(hostAddress));
  hostAddress.abAddress[0] = 127;
  hostAddress.abAddress[3] = 1;
  hostAddress.byFamily = NET_ADDR_IPV4;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = 36;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = 4;
  config.byPauseAllowed = 1;
  config.iTrackLoad = 7;
  config.iManualControl = 1;
  config.iCompetitors = 16;
  config.iDamageLevel = 1;
  config.uiRandomSeed = 12345;
  config.uiTrackCRC = 0xe351e351u;
  memcpy(config.szBuildHash, "e3-s1-test", 11);
  CHECK(NetSessionConfigValidate(&config));

  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel);
  pSession = NetSessionHostCreate(pHostChannel, config.byMaxPlayers,
                                  NetTestRandomBytes, &random);
  CHECK(pSession && NetSessionHostSetConfig(pSession, &config));
  pLobby = NetLobbyHostCreate(pSession);
  CHECK(pLobby);
  pHost = NetHostCreate(pSession, pLobby);
  CHECK(pHost);
  CHECK(!NetHostTick(pHost, NET_TEST_START_TICK)); /* not racing yet */

  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    tNetTestClient *pClient = &s_aClients[iClient];
    pClient->pChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, iClient + 1));
    CHECK(pClient->pChannel);
    pClient->pConnection = NetChannelAddConnection(pClient->pChannel, &hostAddress, 0, 0);
    CHECK(pClient->pConnection);
    pClient->pSession = NetSessionClientCreate(pClient->pConnection,
                                               NET_PROTOCOL_VERSION, 1, aszNames[iClient]);
    CHECK(pClient->pSession);
    pClient->pLobby = NetLobbyClientCreate(pClient->pSession);
    CHECK(pClient->pLobby && NetSessionClientStart(pClient->pSession));
    NetLobbyClientSetRaceCallback(pClient->pLobby, NetTestClientRace, pClient);
  }

  /* Lobby: join, ready, start, loaded, release (E2-S2 and E2-S4). */
  for (; ullNowMs <= 1500; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    tNetTestClient *pClient = &s_aClients[iClient];
    CHECK(NetSessionClientState(pClient->pSession) == NET_JOIN_ACCEPTED);
    pClient->byPlayerIdx = NetSessionClientPlayerIndex(pClient->pSession);
    pClient->byCar = pClient->byPlayerIdx; /* the lobby's default car */
    CHECK(NetLobbyClientSetReady(pClient->pLobby, 1, config.uiTrackCRC));
  }
  for (; ullNowMs <= 2500; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  CHECK(NetLobbyHostStart(pLobby, NET_TEST_START_TICK));
  for (; ullNowMs <= 3500; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
    CHECK(NetLobbyClientSetRaceLoaded(s_aClients[iClient].pLobby));
  for (; !NetLobbyHostRaceReleased(pLobby); ++ullNowMs) {
    CHECK(ullNowMs < 6000);
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  }
  CHECK(NetHostBeginRace(pHost));
  CHECK(!NetHostBeginRace(pHost));
  for (int iCar = 0; iCar < numcars; ++iCar) {
    int iOwned = 0;
    for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
      iOwned |= s_aClients[iClient].byCar == iCar;
    CHECK(human_control[iCar] == iOwned);
  }
  NetHostSetSimulation(pHost, NetTestHostSimulate, pRun);
  NetRaceClockReset(&clock);
  CHECK(NetRaceClockSchedule(&clock, NET_TEST_START_TICK));
  CHECK(NetRaceClockRelease(&clock, NET_TEST_START_TICK));
  CHECK(!NetHostTick(pHost, NET_TEST_START_TICK + 1)); /* ticks are consecutive */
  ullReleaseMs = ullNowMs;

  /* Race: host ticks at 36 Hz on the virtual clock; each client sends a
     redundant batch NET_TEST_LEAD_TICKS ahead after every host tick. */
  for (;; ++ullNowMs) {
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
    while (ullNowMs >= ullReleaseMs + (uint64)iTickIndex * 1000u / 36u) {
      uint32 uiTick;
      int iIndex;
      int aiLapBefore[MAX_CARS];
      uint8 abyFinishedBefore[MAX_CARS], abyKillsBefore[MAX_CARS];
      CHECK(NetRaceClockBeginTick(&clock, &uiTick));
      CHECK(uiTick == NetHostNextTick(pHost));
      iIndex = (int)(uiTick - NET_TEST_START_TICK);
      CHECK(iIndex == iTickIndex && iIndex < NET_TEST_MAX_TICKS);
      for (int iCar = 0; iCar < numcars; ++iCar) {
        aiLapBefore[iCar] = (int)(int8)Car[iCar].byLap;
        abyFinishedBefore[iCar] = finished_car[iCar] != 0;
        abyKillsBefore[iCar] = Car[iCar].byKills;
      }
      if (uiTick == NET_TEST_LOVEBUN_TICK) {
        int iCar = s_aClients[0].byCar;
        CHECK(Car[iCar].nCurrChunk >= 0 && Car[iCar].nCurrChunk < TRAK_LEN);
        pRun->iLovebunCar = iCar;
        pRun->bySavedLovebunDesign = Car[iCar].byCarDesignIdx;
        Car[iCar].byCarDesignIdx = 12;
        Car[iCar].byCheatAmmo = 8;
        Car[iCar].byCheatCooldown = 0;
      }
      if (uiTick == NET_TEST_LOVEBUN_TICK) {
        CHECK(pRun->iLovebunCar >= 0);
        for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk)
          CHECK(NetWorldChangeCapture(iChunk, &aWorldBefore[iChunk]));
      }
      if (iIndex == NET_TEST_WARMUP_TICKS + 5) {
        tCarInputData input = NetTestExpected(
            NetTestScript(0, uiTick, iPhone), s_aClients[0].byCar);
        CHECK(!NetHostSetLocalInputs(pHost, s_aClients[0].byPlayerIdx,
                                     uiTick + 1u, &input, 1));
        CHECK(!NetHostSetLocalInputs(pHost, s_aClients[0].byPlayerIdx,
                                     uiTick, &input, 2));
        CHECK(NetHostSetLocalInputs(pHost, s_aClients[0].byPlayerIdx,
                                    uiTick, &input, 1));
        iLocalInputProbed = 1;
      }
      if (iRunningTicks == NET_TEST_RUNNING_TICKS && iRunning >= 0 &&
          iIndex - iRunning == iRunningTicks - 4)
        pRun->uiSettleTick = uiTick;
      if (!NetHostTick(pHost, uiTick)) {
        fprintf(stderr, "host tick %u failed (events %u, LOVEBUN uses %d)\n",
                uiTick, NetHostLastEventSeq(pHost), pRun->iLovebunUses);
        CHECK(0);
      }
      for (int iCar = 0; iCar < numcars; ++iCar) {
        int iLapAfter = (int)(int8)Car[iCar].byLap;
        int iFirstLap = aiLapBefore[iCar] + 1;
        if (iFirstLap < 2)
          iFirstLap = 2;
        if (iLapAfter >= iFirstLap)
          pRun->aiLapEvents[iCar] += iLapAfter - iFirstLap + 1;
        if (!abyFinishedBefore[iCar] && finished_car[iCar]) {
          if (Car[iCar].byLives)
            ++pRun->aiFinishedEvents[iCar];
          else
            ++pRun->aiDestroyedEvents[iCar];
        }
        if (Car[iCar].byKills > abyKillsBefore[iCar])
          pRun->aiKillEvents[iCar] += Car[iCar].byKills - abyKillsBefore[iCar];
      }
      if (uiTick == NET_TEST_LOVEBUN_TICK) {
        for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
          tNetWorldChangeEntry current, retained;
          CHECK(NetWorldChangeCapture(iChunk, &current));
          if (NetWorldChangeEntryEqual(&aWorldBefore[iChunk], &current))
            continue;
          CHECK(pRun->iWorldEntries < NET_WORLD_CHANGE_MAX_ENTRIES);
          pRun->aWorldEntries[pRun->iWorldEntries++] = current;
          CHECK(NetHostWorldChangeAt(pHost, iChunk, &retained));
          CHECK(NetWorldChangeEntryEqual(&current, &retained));
        }
        CHECK(pRun->iLovebunUses == 1 && pRun->iWorldEntries > 0);
      }
      NetRaceClockEndTick(&clock, game_frame);
      pRun->aiFrame[iIndex] = game_frame;
      pRun->auiRandom[iIndex] = ROLLERrandStateGet();
      pRun->auiWorldHash[iIndex] = NetTestWorldHash();
      for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
        tNetCarFullState full;
        int iCar = s_aClients[iClient].byCar;
        pRun->aWritten[iIndex][iClient] = copy_multiple[(writeptr - 1) & 511][iCar];
        CHECK(NetSnapshotEncodeCarFull(iCar, &full));
        pRun->aExtra[iIndex][iClient] = full.extra;
      }
      if (iIndex == NET_TEST_WARMUP_TICKS)
        for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
          CHECK(NetHostPlayerStats(pHost, s_aClients[iClient].byPlayerIdx,
                                   &pRun->aWarmStats[iClient]));
      if (iRunning < 0 && NetRaceClockPhase(&clock) == NET_RACE_START_RUNNING) {
        iRunning = iIndex;
        pRun->iRunningIndex = iIndex;
        for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
          pRun->afSpeedAtRunning[iClient] = Car[s_aClients[iClient].byCar].fFinalSpeed;
          pRun->aiChunkAtRunning[iClient] = Car[s_aClients[iClient].byCar].iLastValidChunk;
        }
      }
      for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
        if (NetLobbyClientRaceReleased(s_aClients[iClient].pLobby, &uiTick))
          NetTestSendInputs(iClient, NetHostNextTick(pHost) + NET_TEST_LEAD_TICKS, iPhone);
      if (iRunning >= 0 && iIndex - iRunning == NET_TEST_PROBE_TICK && !iProbed) {
        /* Horizon and shape probes from client 0, beside its normal batch:
           a batch 200 ticks ahead (every tick beyond the 48-tick horizon),
           one 100 ticks old (ignored, not counted), and one claiming two
           local players for a one-car player (rejected whole). */
        tNetInputBatch batch;
        CHECK(NetHostPlayerStats(pHost, s_aClients[0].byPlayerIdx, &probeStats));
        memset(&batch, 0, sizeof(batch));
        batch.byCount = NET_INPUT_REDUNDANCY;
        batch.byLocalPlayers = 1;
        batch.uiFirstTick = NetHostNextTick(pHost) + 200u;
        NetTestSendRaw(&s_aClients[0], &batch);
        batch.uiFirstTick = NetHostNextTick(pHost) - 100u;
        NetTestSendRaw(&s_aClients[0], &batch);
        batch.byLocalPlayers = 2;
        batch.uiFirstTick = NetHostNextTick(pHost) + 2u;
        NetTestSendRaw(&s_aClients[0], &batch);
        iProbed = 1;
      }
      ++iTickIndex;
    }
    if (iRunning >= 0 && iTickIndex - 1 - iRunning >= iRunningTicks)
      break;
  }
  pRun->iTicks = iTickIndex;
  pRun->uiLastEventSeq = NetHostLastEventSeq(pHost);
  pRun->iRaceState = NetHostRaceState(pHost);
  CHECK(NetHostResults(pHost, &pRun->iResultFinishers,
                       &pRun->iResultHumanFinishers) ==
        (iRunningTicks == NET_TEST_RUNNING_TICKS));
  CHECK(iLocalInputProbed);
  /* Let the snapshots in flight land; no further ticks. */
  for (uint64 ullEnd = ullNowMs + 3 * NET_TEST_LATENCY_MS; ullNowMs <= ullEnd; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
    CHECK(NetHostPlayerStats(pHost, s_aClients[iClient].byPlayerIdx,
                             &pRun->aFinalStats[iClient]));
  {
    /* Host ring (4.8): 600 ms of snapshots at 36 Hz is 22 ticks. */
    tNetSnapshot snapshot;
    int iNewest = (pRun->iTicks - 1) / NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL *
        NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
    uint32 uiNewest = NET_TEST_START_TICK + (uint32)iNewest;
    CHECK(NetHostSnapshotAt(pHost, uiNewest, &snapshot));
    CHECK(snapshot.uiTick == uiNewest && snapshot.context.iGameFrame == pRun->aiFrame[iNewest]);
    CHECK(snapshot.uiRandomState == pRun->auiRandom[iNewest]);
    CHECK(snapshot.uiLastEventSeq == pRun->uiLastEventSeq);
    CHECK(NetHostSnapshotAt(pHost, uiNewest - 22u, &snapshot));
    CHECK(snapshot.context.iGameFrame == pRun->aiFrame[iNewest - 22]);
    CHECK(!NetHostSnapshotAt(pHost, uiNewest - 24u, &snapshot));
    CHECK(!NetHostSnapshotAt(pHost, uiNewest - 1u, &snapshot));
    CHECK(!NetHostSnapshotAt(pHost, uiNewest + 2u, &snapshot));
  }
  if (iProbed) {
    const tNetHostPlayerStats *pFinal = &pRun->aFinalStats[0];
    CHECK(pFinal->uiFutureInputs == probeStats.uiFutureInputs + NET_INPUT_REDUNDANCY);
    CHECK(pFinal->uiRejectedBatches == probeStats.uiRejectedBatches + 1);
    CHECK(pFinal->uiLateInputs == probeStats.uiLateInputs);
    CHECK(g_netStats.iFutureInputs == (int)pFinal->uiFutureInputs);
  }

  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    tNetTestClient *pClient = &s_aClients[iClient];
    NetLobbyClientDestroy(pClient->pLobby);
    NetSessionClientDestroy(pClient->pSession);
    NetChannelDestroy(pClient->pChannel);
    pClient->pLobby = NULL;
    pClient->pSession = NULL;
    pClient->pChannel = NULL;
    pClient->pConnection = NULL;
  }
  NetHostDestroy(pHost);
  NetLobbyHostDestroy(pLobby);
  NetSessionHostDestroy(pSession);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestCheckAcceptance(const tNetTestRun *pRun)
{
  const int iInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  const uint32 uiLastTick = NET_TEST_START_TICK + (uint32)pRun->iTicks - 1u;
  int iLateTicks[NET_TEST_CLIENTS] = {0};
  int iExpectedEvents = 0;

  for (int iCar = 0; iCar < numcars; ++iCar)
    iExpectedEvents += pRun->aiLapEvents[iCar] +
                       pRun->aiFinishedEvents[iCar] +
                       pRun->aiDestroyedEvents[iCar] +
                       pRun->aiKillEvents[iCar];
  iExpectedEvents += 3; /* RUNNING, OUTCOME_SETTLED, RESULTS */
  CHECK(pRun->iLovebunUses == 1 && pRun->iWorldEntries > 0);
  printf("host commits: %u total, %d semantic, %d LOVEBUN chunks\n",
         pRun->uiLastEventSeq, iExpectedEvents, pRun->iWorldEntries);
  CHECK(pRun->uiLastEventSeq == (uint32)(iExpectedEvents + 1));
  CHECK(pRun->iRaceState == NET_RACE_OUTCOME_SETTLED);
  CHECK(pRun->iResultFinishers == NET_TEST_CLIENTS &&
        pRun->iResultHumanFinishers == NET_TEST_CLIENTS);

  CHECK(pRun->iTicks - 1 - pRun->iRunningIndex >= NET_TEST_RUNNING_TICKS);
  CHECK(pRun->aiFrame[pRun->iRunningIndex] == 145);
  for (int iIndex = 1; iIndex < pRun->iTicks; ++iIndex)
    CHECK(pRun->aiFrame[iIndex] == pRun->aiFrame[iIndex - 1] + 1);

  for (int iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    const tNetTestClient *pClient = &s_aClients[iClient];
    const tNetHostPlayerStats *pWarm = &pRun->aWarmStats[iClient];
    const tNetHostPlayerStats *pFinal = &pRun->aFinalStats[iClient];
    int iCar = pClient->byCar;
    int iNewInputs = 0;

    /* Remote cars follow the scripted inputs: after warm-up, every tick the
       host simulated used exactly that client's clamped input for the tick,
       and no forbidden flag ever reached the simulation. */
    for (int iIndex = 0; iIndex < pRun->iTicks; ++iIndex) {
      uint32 uiTick = NET_TEST_START_TICK + (uint32)iIndex;
      tCarInputData expected = NetTestExpected(NetTestScript(iClient, uiTick, 1), iCar);
      const tCarInputData *pWritten = &pRun->aWritten[iIndex][iClient].data;
      tCarInputData raw = NetTestScript(iClient, uiTick, 1);
      CHECK(!(pWritten->unFlags & ~NET_INPUT_ALLOWED_FLAGS));
      if (iIndex < NET_TEST_WARMUP_TICKS) {
        iLateTicks[iClient] += memcmp(pWritten, &expected, sizeof(expected)) != 0;
        continue;
      }
      CHECK(!memcmp(pWritten, &expected, sizeof(expected)));
      iNewInputs += memcmp(&raw, &expected, sizeof(raw)) != 0;
    }
    CHECK(iLateTicks[iClient] > 0 && iLateTicks[iClient] < NET_TEST_WARMUP_TICKS);
    CHECK(pFinal->uiLateInputs == pWarm->uiLateInputs);
    CHECK(pFinal->uiRejectedBatches == (iClient == 0 ? 1u : 0u));
    CHECK(pFinal->uiFutureInputs == (iClient == 0 ? (uint32)NET_INPUT_REDUNDANCY : 0u));
    if (iClient == 2)
      CHECK(iNewInputs > 100 && pFinal->uiClampedInputs >= (uint32)iNewInputs);
    else
      CHECK(!iNewInputs && !pFinal->uiClampedInputs);
    CHECK(pFinal->byCarCount == 1 && pFinal->abyCars[0] == iCar);
    CHECK(pFinal->nArrivalMarginTicks > 0);
    CHECK((int32)(pFinal->uiLastDecodedSnapshotTick - (uiLastTick - 20u)) > 0);

    /* Driving: each scripted car made progress under its own input. */
    CHECK(human_control[iCar] == 1);
    CHECK(Car[iCar].iLastValidChunk != pRun->aiChunkAtRunning[iClient]);

    /* Snapshot cadence exact and gap-free; context iGameFrame and RNG equal
       the host's at that tick and step by the interval; own-car state for
       the same ticks, for the client's car, equal to the host's. */
    CHECK(!pClient->iBadMessages);
    CHECK(pClient->iSnapshots >= NET_TEST_RUNNING_TICKS / iInterval);
    CHECK(pClient->auiSnapshotTick[0] == NET_TEST_START_TICK);
    CHECK(uiLastTick - pClient->auiSnapshotTick[pClient->iSnapshots - 1] < (uint32)iInterval);
    CHECK(pClient->iOwnStates == pClient->iSnapshots);
    for (int iEntry = 0; iEntry < pClient->iSnapshots; ++iEntry) {
      uint32 uiTick = pClient->auiSnapshotTick[iEntry];
      int iIndex = (int)(uiTick - NET_TEST_START_TICK);
      CHECK(uiTick == NET_TEST_START_TICK + (uint32)(iEntry * iInterval));
      CHECK(pClient->aiSnapshotFrame[iEntry] == pRun->aiFrame[iIndex]);
      CHECK(pClient->auiSnapshotRandom[iEntry] == pRun->auiRandom[iIndex]);
      CHECK(pClient->abySnapshotHuman[iEntry] == 1);
      if (iEntry)
        CHECK(pClient->aiSnapshotFrame[iEntry] ==
              pClient->aiSnapshotFrame[iEntry - 1] + iInterval);
      if (iEntry)
        CHECK(pClient->auiSnapshotEventSeq[iEntry] >=
              pClient->auiSnapshotEventSeq[iEntry - 1]);
      CHECK(pClient->auiSnapshotEventSeq[iEntry] <= pRun->uiLastEventSeq);
      CHECK(pClient->auiOwnTick[iEntry] == uiTick);
      CHECK(pClient->aiOwnCount[iEntry] == 1 && pClient->abyOwnCar[iEntry] == iCar);
      CHECK(!memcmp(&pClient->aOwnExtra[iEntry], &pRun->aExtra[iIndex][iClient],
                    sizeof(tNetCarExtra)));
    }
    CHECK(pClient->auiSnapshotEventSeq[pClient->iSnapshots - 1] ==
          pRun->uiLastEventSeq);

    /* Reliable ordered host commits share one monotonic sequence. */
    CHECK(pClient->iEvents == iExpectedEvents);
    CHECK(pClient->iWorldChanges == pRun->iLovebunUses);
    CHECK(pClient->iCommitSeqs == (int)pRun->uiLastEventSeq);
    for (int iCommit = 0; iCommit < pClient->iCommitSeqs; ++iCommit)
      CHECK(pClient->auiCommitSeq[iCommit] == (uint32)iCommit + 1u);
    for (int iCar = 0; iCar < numcars; ++iCar) {
      int iLapEvents = 0, iFinishedEvents = 0, iDestroyedEvents = 0;
      int iKillEvents = 0;
      for (int iEvent = 0; iEvent < pClient->iEvents; ++iEvent) {
        const tNetEvent *pEvent = &pClient->aEvents[iEvent];
        if (pEvent->byCarIdx != iCar)
          continue;
        iLapEvents += pEvent->byType == NET_EV_LAP_COMPLETE;
        iFinishedEvents += pEvent->byType == NET_EV_FINISHED;
        iDestroyedEvents += pEvent->byType == NET_EV_DESTROYED;
        iKillEvents += pEvent->byType == NET_EV_KILL;
      }
      CHECK(iLapEvents == pRun->aiLapEvents[iCar]);
      CHECK(iFinishedEvents == pRun->aiFinishedEvents[iCar]);
      CHECK(iDestroyedEvents == pRun->aiDestroyedEvents[iCar]);
      CHECK(iKillEvents == pRun->aiKillEvents[iCar]);
    }
    {
      int iRaceRunning = 0, iRaceSettled = 0, iResults = 0;
      for (int iEvent = 0; iEvent < pClient->iEvents; ++iEvent) {
        const tNetEvent *pEvent = &pClient->aEvents[iEvent];
        iRaceRunning += pEvent->byType == NET_EV_RACE_STATE &&
                        pEvent->iArg0 == NET_RACE_RUNNING;
        iRaceSettled += pEvent->byType == NET_EV_RACE_STATE &&
                        pEvent->iArg0 == NET_RACE_OUTCOME_SETTLED;
        iResults += pEvent->byType == NET_EV_RESULTS;
        if (pEvent->byType == NET_EV_RESULTS)
          CHECK(pEvent->iArg0 == NET_TEST_CLIENTS &&
                pEvent->iArg1 == NET_TEST_CLIENTS);
      }
      CHECK(iRaceRunning == 1 && iRaceSettled == 1 && iResults == 1);
    }
    CHECK(pClient->aWorldHeaders[0].byCount == pRun->iWorldEntries);
    CHECK(pClient->aWorldHeaders[0].uiTick == NET_TEST_LOVEBUN_TICK);
    for (int iExpected = 0; iExpected < pRun->iWorldEntries; ++iExpected) {
      int iFound = 0;
      for (int iEntry = 0; iEntry < pRun->iWorldEntries; ++iEntry)
        if (NetWorldChangeEntryEqual(&pClient->aaWorldEntries[0][iEntry],
                                     &pRun->aWorldEntries[iExpected])) {
          iFound = 1;
          break;
        }
      CHECK(iFound);
    }

    /* Input feedback every 250 ms of race time, with the cumulative counts. */
    CHECK(pClient->iFeedback >= (pRun->iTicks * 1000 / 36) / NET_HOST_FEEDBACK_MS - 2);
    CHECK(pClient->iFeedback <= (pRun->iTicks * 1000 / 36 + 400) / NET_HOST_FEEDBACK_MS + 1);
    CHECK(pClient->lastFeedback.unLateInputs == pFinal->uiLateInputs);
    CHECK(pClient->lastFeedback.unFutureInputs == pFinal->uiFutureInputs);
    CHECK(pClient->lastFeedback.nArrivalMarginTicks > 0);
    CHECK((int32)(pClient->lastFeedback.uiHostTick - (uiLastTick - 20u)) > 0);
    printf("client %d car %d: %d snapshots, %d own-car states, %d feedback, "
           "%u late (warm-up), %u clamped, %u future, speed %.1f\n",
           iClient, iCar, pClient->iSnapshots, pClient->iOwnStates, pClient->iFeedback,
           pFinal->uiLateInputs, pFinal->uiClampedInputs, pFinal->uiFutureInputs,
           Car[iCar].fFinalSpeed);
  }
}

static void NetTestDisconnectTakeover(void)
{
  static const char *aszNames[2] = {"Split", "Observer"};
  tNetTransportSim *pSim = NetTransportSimCreate(0xe5e5u);
  tNetSimLink link = {0, 0, 0, 0, 0};
  tNetSimLink dead = {0, 0, 1000, 0, 0};
  tNetAddress hostAddress;
  tNetSessionConfig config;
  tNetTestRandom random = {0x5eed0e5e50000001ull};
  tNetChannel *pHostChannel;
  tNetSessionHost *pSession;
  tNetLobbyHost *pLobby;
  tNetHost *pHost;
  tNetTakeoverRun run;
  tCar aAirborne[2];
  tCarInputData aSplitInput[2] = {{0}};
  tCarInputData observerInput = {0};
  tNetPlayerEntry player;
  uint64 ullNowMs = 0;
  uint32 uiEventBeforeDrop;
  int iTakeovers = 0;

  NetTestRestore(&s_pristine);
  memset(s_aClients, 0, sizeof(s_aClients));
  memset(&run, 0, sizeof(run));
  CHECK(pSim);
  for (int iEndpoint = 0; iEndpoint < 3; ++iEndpoint)
    CHECK(NetTransportSimSetLink(pSim, iEndpoint, &link));
  memset(&hostAddress, 0, sizeof(hostAddress));
  hostAddress.abAddress[0] = 127;
  hostAddress.abAddress[3] = 1;
  hostAddress.byFamily = NET_ADDR_IPV4;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = 36;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = 4;
  config.byPauseAllowed = 1;
  config.iTrackLoad = 7;
  config.iManualControl = 1;
  config.iCompetitors = 16;
  config.iDamageLevel = 1;
  config.uiRandomSeed = 12345;
  config.uiTrackCRC = 0xe5e5e5e5u;
  memcpy(config.szBuildHash, "e5-s2-test", 11);
  CHECK(NetSessionConfigValidate(&config));

  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel);
  pSession = NetSessionHostCreate(pHostChannel, config.byMaxPlayers,
                                  NetTestRandomBytes, &random);
  CHECK(pSession && NetSessionHostSetConfig(pSession, &config));
  pLobby = NetLobbyHostCreate(pSession);
  pHost = NetHostCreate(pSession, pLobby);
  CHECK(pLobby && pHost);
  for (int iClient = 0; iClient < 2; ++iClient) {
    tNetTestClient *pClient = &s_aClients[iClient];
    pClient->pChannel = NetChannelCreate(
        NetTransportSimEndpoint(pSim, iClient + 1));
    CHECK(pClient->pChannel);
    pClient->pConnection = NetChannelAddConnection(
        pClient->pChannel, &hostAddress, 0, 0);
    CHECK(pClient->pConnection);
    pClient->pSession = NetSessionClientCreate(
        pClient->pConnection, NET_PROTOCOL_VERSION,
        (uint8)(iClient == 0 ? 2 : 1), aszNames[iClient]);
    CHECK(pClient->pSession);
    pClient->pLobby = NetLobbyClientCreate(pClient->pSession);
    CHECK(pClient->pLobby && NetSessionClientStart(pClient->pSession));
    NetLobbyClientSetRaceCallback(pClient->pLobby,
                                  NetTestClientRace, pClient);
  }
  for (; ullNowMs <= 1000; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iClient = 0; iClient < 2; ++iClient) {
    CHECK(NetSessionClientState(s_aClients[iClient].pSession) ==
          NET_JOIN_ACCEPTED);
    s_aClients[iClient].byPlayerIdx = NetSessionClientPlayerIndex(
        s_aClients[iClient].pSession);
  }
  /* Move the observer first so the split player can claim cars 0 and 1. */
  CHECK(NetLobbyClientSetPlayerInfo(s_aClients[1].pLobby, 2,
                                    NET_LOBBY_NO_PLAYER, 1));
  for (; ullNowMs <= 1200; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  CHECK(NetLobbyClientSetPlayerInfo(s_aClients[0].pLobby, 0, 1, 1));
  for (; ullNowMs <= 1400; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  CHECK(NetLobbyHostPlayer(pLobby, s_aClients[0].byPlayerIdx, &player));
  CHECK(player.byCarIdx0 == 0 && player.byCarIdx1 == 1);
  CHECK(NetLobbyHostPlayer(pLobby, s_aClients[1].byPlayerIdx, &player));
  CHECK(player.byCarIdx0 == 2 && player.byCarIdx1 == NET_LOBBY_NO_PLAYER);
  for (int iClient = 0; iClient < 2; ++iClient)
    CHECK(NetLobbyClientSetReady(s_aClients[iClient].pLobby, 1,
                                 config.uiTrackCRC));
  for (; ullNowMs <= 1700; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  CHECK(NetLobbyHostStart(pLobby, NET_TEST_START_TICK));
  for (; ullNowMs <= 2000; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iClient = 0; iClient < 2; ++iClient)
    CHECK(NetLobbyClientSetRaceLoaded(s_aClients[iClient].pLobby));
  for (; !NetLobbyHostRaceReleased(pLobby); ++ullNowMs) {
    CHECK(ullNowMs < 3000);
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  }
  CHECK(NetHostBeginRace(pHost));
  CHECK(human_control[0] == 1 && human_control[1] == 1 &&
        human_control[2] == 1);
  NetHostSetSimulation(pHost, NetTestTakeoverSimulate, &run);

  /* Twenty seconds of ordinary race time.  Neutral input leaves both split
     cars grounded so the airborne boundary below is deterministic. */
  for (int iTick = 0; iTick < 20 * 36; ++iTick) {
    uint32 uiTick = NetHostNextTick(pHost);
    ullNowMs += (iTick % 9 == 0) ? 28u : 27u;
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
    CHECK(NetHostSetLocalInputs(pHost, s_aClients[0].byPlayerIdx,
                                uiTick, aSplitInput, 2));
    CHECK(NetHostSetLocalInputs(pHost, s_aClients[1].byPlayerIdx,
                                uiTick, &observerInput, 1));
    CHECK(NetHostTick(pHost, uiTick));
  }
  CHECK(NetHostRaceState(pHost) == NET_RACE_RUNNING);
  for (int iCar = 0; iCar < 2; ++iCar) {
    CHECK(Car[iCar].iControlType == 3 && Car[iCar].nCurrChunk >= 0);
    Car[iCar].nPitch = 0;
    Car[iCar].nRoll = 0;
    converttoair(&Car[iCar]);
    Car[iCar].direction.fZ = 15.0f;
    CHECK(Car[iCar].iControlType == 0 && Car[iCar].nCurrChunk == -1);
    aAirborne[iCar] = Car[iCar];
  }
  uiEventBeforeDrop = NetHostLastEventSeq(pHost);
  CHECK(NetHostSetPaused(pHost, 1));
  CHECK(NetTransportSimSetLink(pSim, 1, &dead));
  /* No simulation tick runs while the channel/session clock expires the
     connection.  Transfer must therefore preserve both airborne cars. */
  for (uint64 ullEndMs = ullNowMs + NET_CONNECTION_TIMEOUT_MS + 2u;
       ullNowMs <= ullEndMs; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  CHECK(!human_control[0] && !human_control[1] && human_control[2] == 1);
  CHECK(!memcmp(&Car[0], &aAirborne[0], sizeof(tCar)) &&
        !memcmp(&Car[1], &aAirborne[1], sizeof(tCar)));
  CHECK(NetHostLastEventSeq(pHost) == uiEventBeforeDrop + 1u);
  CHECK(NetLobbyHostPlayer(pLobby, s_aClients[0].byPlayerIdx, &player));
  CHECK(player.byState == NET_PLAYER_DROPPED &&
        player.byCarIdx0 == 0 && player.byCarIdx1 == 1);
  {
    tNetHostPlayerStats stats;
    /* The same inactive bit gates the lifecycle settlement set. */
    CHECK(!NetHostPlayerStats(pHost, s_aClients[0].byPlayerIdx, &stats));
  }
  for (uint64 ullEndMs = ullNowMs + 10u;
       ullNowMs <= ullEndMs; ++ullNowMs)
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
  for (int iEvent = 0; iEvent < s_aClients[1].iEvents; ++iEvent) {
    const tNetEvent *pEvent = &s_aClients[1].aEvents[iEvent];
    if (pEvent->byType != NET_EV_AI_TAKEOVER)
      continue;
    CHECK(pEvent->byCarIdx == 0 && pEvent->byPlayerIdx ==
          s_aClients[0].byPlayerIdx && pEvent->iArg0 == 1 &&
          pEvent->iArg1 == 2);
    ++iTakeovers;
  }
  CHECK(iTakeovers == 1);

  CHECK(NetHostSetPaused(pHost, 0));
  for (int iTick = 0; iTick < 300 &&
       NetHostRaceState(pHost) != NET_RACE_OUTCOME_SETTLED; ++iTick) {
    uint32 uiTick = NetHostNextTick(pHost);
    ullNowMs += (iTick % 9 == 0) ? 28u : 27u;
    NetTestPumpAll(pSim, ullNowMs, pSession, pLobby, pHost);
    CHECK(NetHostSetLocalInputs(pHost, s_aClients[1].byPlayerIdx,
                                uiTick, &observerInput, 1));
    CHECK(!NetHostSetLocalInputs(pHost, s_aClients[0].byPlayerIdx,
                                 uiTick, aSplitInput, 2));
    CHECK(NetHostTick(pHost, uiTick));
  }
  CHECK(run.iSawAirborneAi && run.iLanded && run.iMoved);
  CHECK(finished_car[0] && finished_car[1] && finished_car[2]);
  CHECK(NetHostRaceState(pHost) == NET_RACE_OUTCOME_SETTLED);
  {
    int iFinishers, iHumanFinishers;
    CHECK(NetHostResults(pHost, &iFinishers, &iHumanFinishers));
    CHECK(iFinishers == 3 && iHumanFinishers == 1);
  }
  {
    tNetSnapshot snapshot;
    uint32 uiTick = NetHostNextTick(pHost) - 1u;
    while (!NetHostSnapshotAt(pHost, uiTick, &snapshot))
      --uiTick;
    CHECK(!snapshot.aCars[0].byHumanControl &&
          !snapshot.aCars[1].byHumanControl &&
          snapshot.aCars[2].byHumanControl == 1);
  }
  puts("NET-E5-S2 split-screen airborne disconnect transferred atomically");

  for (int iClient = 0; iClient < 2; ++iClient) {
    NetLobbyClientDestroy(s_aClients[iClient].pLobby);
    NetSessionClientDestroy(s_aClients[iClient].pSession);
    NetChannelDestroy(s_aClients[iClient].pChannel);
  }
  NetHostDestroy(pHost);
  NetLobbyHostDestroy(pLobby);
  NetSessionHostDestroy(pSession);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestCodecs(void)
{
  tNetInputBatch batch, decoded;
  tNetInputFeedback feedback, decodedFeedback;
  uint8 abBytes[NET_INPUT_BATCH_MAX_BYTES + 4];
  tCarInputData input;
  int iLength;
  tNetEvent event, decodedEvent;
  tNetWorldChangeHeader worldHeader;
  tNetWorldChangeEntry aWorld[2], aDecodedWorld[2];
  uint8 abCommit[sizeof(tNetWorldChangeHeader) +
                 2 * sizeof(tNetWorldChangeEntry)];

  memset(&batch, 0, sizeof(batch));
  batch.uiFirstTick = 0x01020304u;
  batch.uiLastDecodedSnapshotTick = 0xa0b0c0d0u;
  batch.byCount = 2;
  batch.byLocalPlayers = 2;
  batch.aInputs[0][0].unInput = 0x1234;
  batch.aInputs[0][1].unFlags = 0x2001;
  batch.aInputs[1][0].unInput = 0xfedc;
  batch.aInputs[1][1].unFlags = 0x0002;
  iLength = NetInputBatchEncode(&batch, abBytes, sizeof(abBytes));
  CHECK(iLength == 10 + 2 * 2 * 4);
  CHECK(abBytes[0] == 0x04 && abBytes[3] == 0x01 && abBytes[4] == 0xd0);
  CHECK(abBytes[10] == 0x34 && abBytes[11] == 0x12);
  CHECK(NetInputBatchDecode(abBytes, iLength, &decoded));
  CHECK(!memcmp(&batch, &decoded, sizeof(batch)));
  memset(&decoded, 0x5a, sizeof(decoded));
  CHECK(!NetInputBatchDecode(abBytes, iLength - 1, &decoded));
  abBytes[8] = 0;
  CHECK(!NetInputBatchDecode(abBytes, iLength, &decoded));
  abBytes[8] = NET_INPUT_REDUNDANCY + 1;
  CHECK(!NetInputBatchDecode(abBytes, iLength, &decoded));
  abBytes[8] = 2;
  abBytes[9] = 3;
  CHECK(!NetInputBatchDecode(abBytes, iLength, &decoded));
  CHECK(decoded.byCount == 0x5a); /* rejection leaves the output untouched */

  memset(&feedback, 0, sizeof(feedback));
  feedback.uiHostTick = 77;
  feedback.unLateInputs = 3;
  feedback.unFutureInputs = 9;
  feedback.nArrivalMarginTicks = -4;
  CHECK(NetInputFeedbackEncode(&feedback, abBytes, sizeof(abBytes)) == 12);
  CHECK(NetInputFeedbackDecode(abBytes, 12, &decodedFeedback));
  CHECK(!memcmp(&feedback, &decodedFeedback, sizeof(feedback)));
  abBytes[11] = 1;
  CHECK(!NetInputFeedbackDecode(abBytes, 12, &decodedFeedback));

  input.unFlags = BUTTON_FLAG_ACCEL | FLAG_FINISHED | FLAG_MASTER_CHANGE;
  input.unInput = 0;
  CHECK(NetInputClamp(&input, 0) && input.unFlags == BUTTON_FLAG_ACCEL);
  input.unFlags = BUTTON_FLAG_PHONE_THROTTLE | BUTTON_FLAG_SPECIAL;
  input.unInput = 100;
  CHECK(!NetInputClamp(&input, 0));
  input.unInput = (uint16)-32768;
  CHECK(NetInputClamp(&input, 0));
  CHECK((int16)input.unInput == -CarEngines.engines[Car[0].byCarDesignIdx].iSteeringSensitivity * 256);

  memset(&event, 0, sizeof(event));
  event.uiEventSeq = 0x01020304u;
  event.uiTick = 0xa0b0c0d0u;
  event.byType = NET_EV_LAP_COMPLETE;
  event.byCarIdx = 1;
  event.byPlayerIdx = NET_EVENT_NO_PLAYER;
  event.iArg0 = 2;
  event.iArg1 = 5432;
  CHECK(NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)) == 20);
  CHECK(abCommit[0] == 0x04 && abCommit[3] == 0x01 && abCommit[4] == 0xd0);
  CHECK(NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  CHECK(!memcmp(&event, &decodedEvent, sizeof(event)));
  memset(&decodedEvent, 0x5a, sizeof(decodedEvent));
  abCommit[11] = 1;
  CHECK(!NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  CHECK(decodedEvent.byType == 0x5a);
  abCommit[11] = 0;
  abCommit[9] = (uint8)numcars;
  CHECK(!NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  abCommit[9] = 1;
  abCommit[8] = 0xff;
  CHECK(!NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  memset(&event, 0, sizeof(event));
  event.uiEventSeq = 5;
  event.uiTick = 101;
  event.byType = NET_EV_KILL;
  event.byCarIdx = 2;
  event.byPlayerIdx = NET_EVENT_NO_PLAYER;
  event.iArg0 = -1; /* the victim may be unavailable to the post-tick diff */
  event.iArg1 = 7;
  CHECK(NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)) == 20);
  CHECK(NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  CHECK(!memcmp(&event, &decodedEvent, sizeof(event)));

  memset(&event, 0, sizeof(event));
  event.uiEventSeq = 6;
  event.uiTick = 102;
  event.byType = NET_EV_AI_TAKEOVER;
  event.byCarIdx = 0;
  event.byPlayerIdx = 1;
  event.iArg0 = 2;
  event.iArg1 = 2;
  CHECK(NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)) == 20);
  CHECK(NetEventDecode(abCommit, 20, numcars, 4, &decodedEvent));
  CHECK(!memcmp(&event, &decodedEvent, sizeof(event)));
  event.iArg0 = 0;
  CHECK(!NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)));
  event.iArg0 = -1;
  CHECK(!NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)));
  event.iArg1 = 1;
  CHECK(NetEventEncode(&event, numcars, 4, abCommit, sizeof(abCommit)) == 20);

  CHECK(NetWorldChangeCapture(0, &aWorld[0]));
  CHECK(NetWorldChangeCapture(TRAK_LEN - 1, &aWorld[1]));
  iLength = NetWorldChangeEncode(7, 99, aWorld, 2,
                                 abCommit, sizeof(abCommit));
  CHECK(iLength == (int)sizeof(abCommit));
  CHECK(NetWorldChangeDecode(abCommit, iLength, TRAK_LEN, &worldHeader,
                             aDecodedWorld, 2));
  CHECK(worldHeader.uiEventSeq == 7 && worldHeader.uiTick == 99 &&
        worldHeader.byCount == 2);
  CHECK(NetWorldChangeEntryEqual(&aWorld[0], &aDecodedWorld[0]));
  CHECK(NetWorldChangeEntryEqual(&aWorld[1], &aDecodedWorld[1]));
  CHECK(!NetWorldChangeEncode(8, 100, aWorld, 0,
                              abCommit, sizeof(abCommit)));
  aWorld[1] = aWorld[0];
  CHECK(!NetWorldChangeEncode(8, 100, aWorld, 2,
                              abCommit, sizeof(abCommit)));
  CHECK(NetWorldChangeCapture(TRAK_LEN - 1, &aWorld[1]));
  CHECK(NetWorldChangeEncode(8, 100, aWorld, 2,
                             abCommit, sizeof(abCommit)) == iLength);
  abCommit[9] = 1;
  CHECK(!NetWorldChangeDecode(abCommit, iLength, TRAK_LEN, &worldHeader,
                              aDecodedWorld, 2));
  abCommit[9] = 0;
  abCommit[sizeof(tNetWorldChangeHeader) + 2] = NET_WORLD_GRIP_COUNT;
  CHECK(!NetWorldChangeDecode(abCommit, iLength, TRAK_LEN, &worldHeader,
                              aDecodedWorld, 2));
  CHECK(NetWorldChangeEncode(8, 100, aWorld, 2,
                             abCommit, sizeof(abCommit)) == iLength);
  abCommit[sizeof(tNetWorldChangeHeader)] = (uint8)TRAK_LEN;
  abCommit[sizeof(tNetWorldChangeHeader) + 1] = (uint8)(TRAK_LEN >> 8);
  CHECK(!NetWorldChangeDecode(abCommit, iLength, TRAK_LEN, &worldHeader,
                              aDecodedWorld, 2));
  CHECK(NetWorldChangeEncode(8, 100, aWorld, 2,
                             abCommit, sizeof(abCommit)) == iLength);
  memcpy(abCommit + sizeof(tNetWorldChangeHeader) + sizeof(tNetWorldChangeEntry),
         abCommit + sizeof(tNetWorldChangeHeader),
         sizeof(tNetWorldChangeEntry));
  CHECK(!NetWorldChangeDecode(abCommit, iLength, TRAK_LEN, &worldHeader,
                              aDecodedWorld, 2));
  puts("NET-E3-S1/S2 input, event and world-change codecs passed");
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
  NetTestCodecs();
  NetTestCapture(&s_pristine);

  NetTestRace(&s_phoneRun, 1, NET_TEST_RUNNING_TICKS);
  NetTestCheckAcceptance(&s_phoneRun);
  puts("NET-E3-S1/S2 host tick and commit acceptance passed");

  /* Phone throttle on a desktop host: the same race with client 1 sending
     the throttle bit instead must leave the world byte-identical, tick for
     tick, including the RNG. */
  NetTestRace(&s_accelRun, 0, NET_TEST_REFERENCE_TICKS);
  CHECK(s_accelRun.iRunningIndex == s_phoneRun.iRunningIndex);
  for (int iIndex = 0; iIndex < s_accelRun.iTicks; ++iIndex)
    CHECK(s_accelRun.auiWorldHash[iIndex] == s_phoneRun.auiWorldHash[iIndex]);
  CHECK(s_accelRun.aExtra[s_accelRun.iTicks - 1][1].fBaseSpeed > 0.0f);
  printf("phone throttle equals throttle bit over %d host ticks\n", s_accelRun.iTicks);
  NetTestDisconnectTakeover();
  return 0;
}
