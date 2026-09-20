/* NET-E4-S1 acceptance: client tick timeline, input send, clock sync, lead
   control, and the three rings.

   One process holds one simulation world (D13), and here it is the client's:
   the client predicts, records all three rings and is the thing under test.
   The host is real - session, lobby, input queues, snapshot cadence and
   input feedback all run - but its simulation step is replaced through
   NetHostSetSimulation, which records the inputs it would have simulated.
   That keeps the host's timing behaviour, which is what the acceptance
   measures, without a second world in the process. */
#include "net_client.h"
#include "net_config_internal.h"
#include "net_headless.h"
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
#include "roller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define NET_TEST_MAX_TICKS 4096
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
  uint8 byCar;
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

/* The one world belongs to the client (D13): the host records the tick's
   inputs instead of simulating them. */
static int NetTestHostSimulate(void *pContext, uint32 uiTick,
                               const tCopyData *pInputs, int iNumCars)
{
  tNetTestHostRun *pRun = (tNetTestHostRun *)pContext;
  CHECK(iNumCars == numcars && pRun->iTicks < NET_TEST_MAX_TICKS);
  pRun->aTicks[pRun->iTicks].uiTick = uiTick;
  pRun->aTicks[pRun->iTicks].input = pInputs[pRun->byCar].data;
  return 1;
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

  NetTestRace(36, 20000, 10000);
  puts("NET-E4-S1 client timeline acceptance passed at 36 Hz");
  NetTestRace(100, 8000, 8000);
  puts("NET-E4-S1 client timeline acceptance passed at 100 Hz");
  return 0;
}
