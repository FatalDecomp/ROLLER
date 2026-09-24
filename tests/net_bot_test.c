/* NET-E2-S7 acceptance: a lightweight bot joins the real lobby path, drives
   the authoritative headless world, and completes a lap. */
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
  tNetBot *pBot;
  tNetSessionConfig config;
  tNetAddress hostAddress = NetBotTestAddress(0);
  tNetBotTestRandom random = {0x4e4554424f545345ull};
  tNetBotStats botStats;
  tNetHostPlayerStats hostStats;
  tNetPlayerEntry player;
  uint64 ullNowMs = 0;
  uint8 byInitialLap;
  int iTicks;
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
       (!race_started || game_frame <= 145 || Car[0].byLap < 1); ++iTicks) {
    uint32 uiTick = NetHostNextTick(pHost);
    CHECK(NetBotTick(pBot, uiTick, NULL));
    for (int iPump = 0; iPump < 3; ++iPump) {
      ++ullNowMs;
      NetBotTestPump(pSim, ullNowMs, pHostChannel, pSessionHost,
                     pLobbyHost, pHost, pBotChannel, pBot);
    }
    CHECK(NetHostTick(pHost, uiTick));
  }
  CHECK(race_started && game_frame > 145 && Car[0].byLap == 1);
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
  CHECK(botStats.uiInputsSent == (uint32)iTicks);
  CHECK(botStats.uiSnapshots > 0 && botStats.uiLapCompletions == 1);
  CHECK(!botStats.uiRejectedMessages && botStats.byFinished);
  CHECK(NetHostPlayerStats(pHost, 0, &hostStats));
  CHECK(!hostStats.uiLateInputs && !hostStats.uiClampedInputs &&
        !hostStats.uiRejectedBatches);

  printf("NET-E2-S7 bot passed: joined, raced, and completed one lap in "
         "%d ticks (%u snapshots)\n", iTicks, botStats.uiSnapshots);

  NetBotDestroy(pBot);
  NetHostDestroy(pHost);
  NetLobbyHostDestroy(pLobbyHost);
  NetSessionHostDestroy(pSessionHost);
  NetChannelDestroy(pBotChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
  return 0;
}
