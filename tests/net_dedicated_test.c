/* NET-E2-S6 acceptance: the dedicated runtime hosts two lightweight bots
   through a complete one-lap authoritative race. */
#include "net_bot.h"
#include "net_dedicated.h"
#include "net_headless.h"
#include "net_types.h"
#include "3d.h"
#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #iCondition); exit(1); \
} } while (0)

#define NET_DEDICATED_TEST_MAX_MS 120000u

typedef struct
{
  uint64 ullState;
} tNetDedicatedTestRandom;

static int NetDedicatedTestRandomBytes(void *pContext, void *pData,
                                       int iLength)
{
  tNetDedicatedTestRandom *pRandom =
      (tNetDedicatedTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  for (int iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState ^= pRandom->ullState << 13;
    pRandom->ullState ^= pRandom->ullState >> 7;
    pRandom->ullState ^= pRandom->ullState << 17;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 24);
  }
  return 1;
}

static tNetAddress NetDedicatedTestAddress(int iEndpoint)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.byFamily = NET_ADDR_IPV4;
  address.abAddress[0] = 127;
  address.abAddress[3] = (uint8)(1 + iEndpoint);
  return address;
}

static tNetSessionConfig NetDedicatedTestConfig(void)
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
  memcpy(config.szBuildHash, "e2-s6-test", 11);
  CHECK(NetSessionConfigValidate(&config));
  return config;
}

static void NetDedicatedTestPumpBot(tNetChannel *pChannel, tNetBot *pBot,
                                    tNetDedicated *pDedicated)
{
  tNetBotStats stats;
  NetChannelPump(pChannel);
  NetBotPump(pBot);
  CHECK(NetBotStats(pBot, &stats));
  if (NetBotState(pBot) == NET_BOT_RACING &&
      stats.uiStartTick + stats.uiInputsSent ==
          NetDedicatedNextTick(pDedicated))
    CHECK(NetBotTick(pBot, stats.uiStartTick + stats.uiInputsSent, NULL));
  NetChannelPump(pChannel);
}

int main(int iArgc, char **ppArgv)
{
  tNetTransportSim *pSim;
  tNetChannel *pServerChannel, *apBotChannels[2];
  tNetConnection *pConnection;
  tNetDedicated *pDedicated;
  tNetBot *apBots[2];
  tNetDedicatedTestRandom random = {0x4445444943415445ull};
  tNetSessionConfig config;
  tNetDedicatedStats serverStats;
  tNetBotStats botStats;
  tNetHostPlayerStats playerStats;
  tNetAddress serverAddress = NetDedicatedTestAddress(0);
  uint64 ullNowMs;
  char szError[512];

  CHECK(iArgc >= 3);
  config = NetDedicatedTestConfig();
  CHECK(NetSessionConfigApply(&config));
  CHECK(NetHeadlessInit(ppArgv[1], ppArgv[2], 2, 12345,
                        szError, sizeof(szError)));
  NoOfLaps = 1;
  pSim = NetTransportSimCreate(0xe256u);
  CHECK(pSim);
  pServerChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pServerChannel);
  pDedicated = NetDedicatedCreate(pServerChannel, &config,
      NetDedicatedTestRandomBytes, &random);
  CHECK(pDedicated);

  for (int iBot = 0; iBot < 2; ++iBot) {
    char szName[sizeof(((tNetPlayerEntry *)0)->szName)];
    apBotChannels[iBot] = NetChannelCreate(
        NetTransportSimEndpoint(pSim, iBot + 1));
    CHECK(apBotChannels[iBot]);
    pConnection = NetChannelAddConnection(apBotChannels[iBot],
                                          &serverAddress, 0, 0);
    CHECK(pConnection);
    snprintf(szName, sizeof(szName), "BOT%d", iBot + 1);
    apBots[iBot] = NetBotCreate(pConnection, szName, (uint8)iBot, 1,
                                config.uiTrackCRC);
    CHECK(apBots[iBot] && NetBotStart(apBots[iBot]));
  }

  for (ullNowMs = 0; ullNowMs < NET_DEDICATED_TEST_MAX_MS; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(pSim, ullNowMs));
    CHECK(NetDedicatedPump(pDedicated));
    for (int iBot = 0; iBot < 2; ++iBot)
      NetDedicatedTestPumpBot(apBotChannels[iBot], apBots[iBot], pDedicated);
    CHECK(NetDedicatedPump(pDedicated));
    if (NetDedicatedState(pDedicated) == NET_DEDICATED_COMPLETE) {
      int iFinishedBots = 0;
      for (int iBot = 0; iBot < 2; ++iBot) {
        NetChannelPump(apBotChannels[iBot]);
        NetBotPump(apBots[iBot]);
        CHECK(NetBotStats(apBots[iBot], &botStats));
        iFinishedBots += botStats.byFinished != 0;
      }
      if (iFinishedBots == 2)
        break;
    }
  }

  CHECK(ullNowMs < NET_DEDICATED_TEST_MAX_MS);
  CHECK(NetDedicatedStats(pDedicated, &serverStats));
  CHECK(serverStats.state == NET_DEDICATED_COMPLETE);
  CHECK(serverStats.iPlayers == 2 && serverStats.iFinishers == 2 &&
        serverStats.iHumanFinishers == 2);
  CHECK(serverStats.uiTicksSimulated > 145 &&
        serverStats.uiNextTick == NET_DEDICATED_START_TICK +
            serverStats.uiTicksSimulated);
  for (int iBot = 0; iBot < 2; ++iBot) {
    CHECK(NetBotStats(apBots[iBot], &botStats));
    CHECK(botStats.byFinished && botStats.uiLapCompletions == 1 &&
          botStats.uiSnapshots > 0 && !botStats.uiRejectedMessages);
    CHECK(NetDedicatedPlayerStats(pDedicated, (uint8)iBot, &playerStats));
    CHECK(!playerStats.uiLateInputs && !playerStats.uiClampedInputs &&
          !playerStats.uiRejectedBatches);
  }

  printf("NET-E2-S6 dedicated server passed: two bots completed one lap in "
         "%u ticks (%llu virtual ms)\n", serverStats.uiTicksSimulated,
         (unsigned long long)ullNowMs);

  for (int iBot = 0; iBot < 2; ++iBot) {
    NetBotDestroy(apBots[iBot]);
    NetChannelDestroy(apBotChannels[iBot]);
  }
  NetDedicatedDestroy(pDedicated);
  NetChannelDestroy(pServerChannel);
  NetTransportSimDestroy(pSim);
  return 0;
}
