/* NET-E2-S7/E8-S3 acceptance: a lightweight bot joins the real lobby path,
   drives the authoritative headless world, survives checkpoint rejoin, and
   completes a lap. */
#include "net_bot.h"
#include "net_headless.h"
#include "net_host.h"
#include "net_types.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #iCondition); exit(1); \
} } while (0)

#define NET_BOT_TEST_START_TICK 1000u
#define NET_BOT_TEST_MAX_TICKS 6000

typedef struct
{
  uint64 ullState;
} tNetBotTestRandom;

static int NetBotTestRandomBytes(void *pContext, void *pData, int iLength)
{
  tNetBotTestRandom *pRandom = (tNetBotTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  for (int iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState ^= pRandom->ullState << 13;
    pRandom->ullState ^= pRandom->ullState >> 7;
    pRandom->ullState ^= pRandom->ullState << 17;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 24);
  }
  return 1;
}

static tNetAddress NetBotTestAddress(int iEndpoint)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.byFamily = NET_ADDR_IPV4;
  address.abAddress[0] = 127;
  address.abAddress[3] = (uint8)(1 + iEndpoint);
  return address;
}

static tNetSessionConfig NetBotTestConfig(void)
{
  tNetSessionConfig config;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = 36;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = 2;
  config.byHostIsDedicated = 1;
  config.iTrackLoad = 5;
  config.iGameType = 0;
  config.iManualControl = 1;
  config.iCompetitors = 2;
  config.iDamageLevel = 1;
  config.iTextureMode = 1;
  config.uiRandomSeed = 12345;
  config.uiTrackCRC = 0xe257b07u;
  memcpy(config.szBuildHash, "e2-s7-test", 11);
  CHECK(NetSessionConfigValidate(&config));
  return config;
}

static void NetBotTestPump(tNetTransportSim *pSim, uint64 ullNowMs,
                           tNetChannel *pHostChannel,
                           tNetSessionHost *pSessionHost,
                           tNetLobbyHost *pLobbyHost, tNetHost *pHost,
                           tNetChannel *pBotChannel, tNetBot *pBot)
{
  CHECK(NetTransportSimAdvance(pSim, ullNowMs));
  NetChannelPump(pHostChannel);
  NetSessionHostPump(pSessionHost);
  NetLobbyHostPump(pLobbyHost);
  NetHostPump(pHost);
  NetChannelPump(pBotChannel);
  NetBotPump(pBot);
}

int main(int iArgc, char **ppArgv)
{
  tNetTransportSim *pSim;
  tNetChannel *pHostChannel, *pBotChannel;
  tNetSessionHost *pSessionHost;
  tNetLobbyHost *pLobbyHost;
  tNetHost *pHost;
  tNetConnection *pBotConnection;
  tNetConnection *pRejoinConnection;
  tNetBot *pBot;
  tNetSessionConfig config;
  tNetAddress hostAddress = NetBotTestAddress(0);
  tNetBotTestRandom random = {0x4e4554424f545345ull};
  tNetBotStats botStats;
  tNetHostPlayerStats hostStats;
  tNetPlayerEntry player;
  uint64 ullNowMs = 0;
  uint64 ullDropStart, ullNextTickMs;
  uint8 byInitialLap;
  int iTicks, iDropTicks;
  char szError[512];

  CHECK(iArgc >= 3);
  CHECK(NetHeadlessInit(ppArgv[1], ppArgv[2], 2, 12345,
                        szError, sizeof(szError)));
  NoOfLaps = 1;
  net_mode = NET_MODE_MODERN;
  config = NetBotTestConfig();

  pSim = NetTransportSimCreate(0xe257u);
  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pBotChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pHostChannel && pBotChannel);
  pSessionHost = NetSessionHostCreate(pHostChannel, config.byMaxPlayers,
                                      NetBotTestRandomBytes, &random);
  CHECK(pSessionHost && NetSessionHostSetConfig(pSessionHost, &config));
  pLobbyHost = NetLobbyHostCreate(pSessionHost);
  CHECK(pLobbyHost);
  pHost = NetHostCreate(pSessionHost, pLobbyHost);
  CHECK(pHost);
  pBotConnection = NetChannelAddConnection(pBotChannel, &hostAddress, 0, 0);
  CHECK(pBotConnection);
  pBot = NetBotCreate(pBotConnection, "BOT", 0, 1, config.uiTrackCRC);
  CHECK(pBot && NetBotStart(pBot));

  while (!NetLobbyHostAllReady(pLobbyHost)) {
    CHECK(++ullNowMs < 5000);
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
  }
  CHECK(NetSessionHostPlayerCount(pSessionHost) == 1);
  CHECK(NetLobbyHostPlayer(pLobbyHost, 0, &player));
  CHECK(player.byState == NET_PLAYER_READY && player.byCarIdx0 == 0 &&
        !strcmp(player.szName, "BOT"));
  CHECK(NetBotState(pBot) == NET_BOT_LOBBY);
  CHECK(NetLobbyHostStart(pLobbyHost, NET_BOT_TEST_START_TICK));

  while (!NetLobbyHostRaceReleased(pLobbyHost) ||
         NetBotState(pBot) != NET_BOT_RACING) {
    CHECK(++ullNowMs < 10000);
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
  }
  CHECK(NetHostBeginRace(pHost));
  CHECK(NetHostNextTick(pHost) == NET_BOT_TEST_START_TICK);

  for (iTicks = 0; iTicks < NET_BOT_TEST_MAX_TICKS &&
       (!race_started || game_frame <= 145); ++iTicks) {
    uint32 uiTick = NetHostNextTick(pHost);
    CHECK(NetBotTick(pBot, uiTick, NULL));
    for (int iPump = 0; iPump < 3; ++iPump) {
      ++ullNowMs;
      NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                     pLobbyHost, pHost, pBotChannel, pBot);
    }
    CHECK(NetHostTick(pHost, uiTick));
  }
  CHECK(race_started && game_frame > 145);

  /* E5-S3 with an endpoint-only bot: stop both directions for 15 seconds,
     let the ordinary timeout transfer its car to AI, then authenticate a
     fresh generation.  Host simulation continues throughout the gap. */
  {
    tNetSimLink dead = {0, 0, 1000, 0, 0};
    CHECK(NetTransportSimSetLink(pSim, 0, &dead));
    CHECK(NetTransportSimSetLink(pSim, 1, &dead));
  }
  ullDropStart = ullNowMs;
  ullNextTickMs = ullDropStart;
  iDropTicks = 0;
  while (ullNowMs <= ullDropStart + 15000u) {
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
    while (ullNowMs >= ullNextTickMs) {
      CHECK(NetHostTick(pHost, NetHostNextTick(pHost)));
      ++iTicks;
      ++iDropTicks;
      ullNextTickMs = ullDropStart +
          (uint64)iDropTicks * 1000u / config.unTickRateHz;
    }
    ++ullNowMs;
  }
  CHECK(NetLobbyHostPlayer(pLobbyHost, 0, &player));
  CHECK(player.byState == NET_PLAYER_DROPPED && !human_control[0]);

  {
    tNetSimLink slow = {750, 0, 0, 0, 0};
    CHECK(NetTransportSimSetLink(pSim, 0, &slow));
    CHECK(NetTransportSimSetLink(pSim, 1, &slow));
  }
  pRejoinConnection = NetChannelAddConnection(
      pBotChannel, &hostAddress, NetBotSessionToken(pBot),
      (uint8)(NetBotGeneration(pBot) + 1u));
  CHECK(pRejoinConnection);
  CHECK(NetBotBeginRejoin(pBot, pRejoinConnection));
  CHECK(NetBotState(pBot) == NET_BOT_RECOVERING);
  ullDropStart = ullNowMs;
  ullNextTickMs = ullDropStart;
  iDropTicks = 0;
  while (NetBotState(pBot) == NET_BOT_RECOVERING) {
    CHECK(ullNowMs < ullDropStart + 8000u);
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
    while (ullNowMs >= ullNextTickMs) {
      CHECK(NetHostTick(pHost, NetHostNextTick(pHost)));
      ++iTicks;
      ++iDropTicks;
      ullNextTickMs = ullDropStart +
          (uint64)iDropTicks * 1000u / config.unTickRateHz;
    }
    ++ullNowMs;
  }
  CHECK(NetBotState(pBot) == NET_BOT_RACING);
  CHECK(NetLobbyHostPlayer(pLobbyHost, 0, &player));
  CHECK(player.byState == NET_PLAYER_RACING && human_control[0] == 1);
  CHECK(NetBotStats(pBot, &botStats));
  CHECK(botStats.uiCheckpoints == 1 && botStats.uiRejoins == 1);

  /* The endpoint bot has no simulation to replay.  Fill the tick gap from
     its recovery snapshot through the host's next tick, then resume the
     ordinary one-input-per-host-tick driver. */
  {
    tNetSimLink direct = {0, 0, 0, 0, 0};
    CHECK(NetTransportSimSetLink(pSim, 0, &direct));
    CHECK(NetTransportSimSetLink(pSim, 1, &direct));
  }
  CHECK((int32)(NetHostNextTick(pHost) - NetBotNextTick(pBot)) >= 0);
  while (NetBotNextTick(pBot) != NetHostNextTick(pHost))
    CHECK(NetBotTick(pBot, NetBotNextTick(pBot), NULL));
  for (int iPump = 0; iPump < 10; ++iPump) {
    ++ullNowMs;
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
  }

  for (; iTicks < NET_BOT_TEST_MAX_TICKS && Car[0].byLap < 1; ++iTicks) {
    uint32 uiTick = NetHostNextTick(pHost);
    CHECK(NetBotTick(pBot, uiTick, NULL));
    for (int iPump = 0; iPump < 3; ++iPump) {
      ++ullNowMs;
      NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                     pLobbyHost, pHost, pBotChannel, pBot);
    }
    CHECK(NetHostTick(pHost, uiTick));
  }
  CHECK(Car[0].byLap == 1);
  byInitialLap = Car[0].byLap;
  for (; iTicks < NET_BOT_TEST_MAX_TICKS &&
       Car[0].byLap == byInitialLap; ++iTicks) {
    uint32 uiTick = NetHostNextTick(pHost);
    CHECK(NetBotTick(pBot, uiTick, NULL));
    for (int iPump = 0; iPump < 3; ++iPump) {
      ++ullNowMs;
      NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                     pLobbyHost, pHost, pBotChannel, pBot);
    }
    CHECK(NetHostTick(pHost, uiTick));
  }
  for (int iPump = 0; iPump < 10; ++iPump) {
    ++ullNowMs;
    NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                   pLobbyHost, pHost, pBotChannel, pBot);
  }

  CHECK(iTicks < NET_BOT_TEST_MAX_TICKS);
  CHECK(Car[0].byLap == (uint8)(byInitialLap + 1));
  CHECK(NetBotStats(pBot, &botStats));
  CHECK(botStats.uiInputsSent > 0 && botStats.uiCheckpoints == 1 &&
        botStats.uiRejoins == 1);
  CHECK(botStats.uiSnapshots > 0 && botStats.uiLapCompletions == 1);
  CHECK(!botStats.uiRejectedMessages && botStats.byFinished);
  CHECK(NetHostPlayerStats(pHost, 0, &hostStats));
  /* Recovery deliberately backfills from the first post-checkpoint snapshot,
     so those already-simulated labels are counted late, never malformed. */
  CHECK(hostStats.uiLateInputs && !hostStats.uiClampedInputs &&
        !hostStats.uiRejectedBatches);

  printf("NET-E8-S3 bot passed: generation-2 checkpoint recovery and one "
         "completed lap in %d host ticks (%u snapshots)\n",
         iTicks, botStats.uiSnapshots);

  NetBotDestroy(pBot);
  NetHostDestroy(pHost);
  NetLobbyHostDestroy(pLobbyHost);
  NetSessionHostDestroy(pSessionHost);
  NetChannelDestroy(pBotChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
  return 0;
}
