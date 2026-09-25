/* NET-E2-S6 acceptance: the dedicated runtime hosts two lightweight bots
   through a complete one-lap authoritative race. */
#include "net_bot.h"
#include "net_config_internal.h"
#include "net_dedicated.h"
#include "net_discovery.h"
#include "net_headless.h"
#include "net_rendezvous.h"
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
  address.unPort = (uint16)(40000 + iEndpoint);
  return address;
}

static tNetSessionConfig NetDedicatedTestConfig(uint16 unTickRateHz)
{
  tNetSessionConfig config;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = unTickRateHz;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = 2;
  config.byHostIsDedicated = 1;
  config.iTrackLoad = 5;
  config.iGameType = 0;
  config.iManualControl = 1;
  config.iCompetitors = 2;
  config.iDamageLevel = 1;
  config.iTextureMode = 1;
  if (unTickRateHz == 100)
    config.iLevelFlags = NET_SESSION_50HZ_FLAG | NET_SESSION_100HZ_FLAG;
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
      NetBotNextTick(pBot) <= NetDedicatedNextTick(pDedicated))
    NetBotTick(pBot, NetBotNextTick(pBot), NULL);
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
  tNetRendezvousStats relayStats;
  tNetBotStats botStats;
  tNetHostPlayerStats playerStats;
  tNetAddress serverAddress = NetDedicatedTestAddress(0);
  tNetAddress rendezvousAddress = NetDedicatedTestAddress(3);
  tNetRendezvous *pRendezvous = NULL;
  tNetDiscovery *pHostDiscovery = NULL, *apBotDiscovery[2] = {0};
  uint64 ullNowMs = 0, ullDeadlineMs;
  uint32 uiSessionId;
  uint16 unTickRateHz = 36;
  int iRelay = 0;
  char szError[512];

  CHECK(iArgc >= 3);
  if (iArgc >= 4)
    unTickRateHz = (uint16)atoi(ppArgv[3]);
  if (iArgc >= 5 && strcmp(ppArgv[4], "relay") == 0)
    iRelay = 1;
  CHECK(unTickRateHz == 36 || unTickRateHz == 100);
  config = NetDedicatedTestConfig(unTickRateHz);
  CHECK(NetSessionConfigApply(&config));
  CHECK(NetHeadlessInit(ppArgv[1], ppArgv[2], 2, 12345,
                        szError, sizeof(szError)));
  NoOfLaps = 1;
  pSim = NetTransportSimCreate(0xe256u);
  CHECK(pSim);
  for (int iEndpoint = 0; iEndpoint < (iRelay ? 4 : 3); ++iEndpoint) {
    tNetAddress endpointAddress = NetDedicatedTestAddress(iEndpoint);
    CHECK(NetTransportSimSetEndpointAddress(pSim, iEndpoint,
                                             &endpointAddress));
  }
  pServerChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pServerChannel);
  for (int iBot = 0; iBot < 2; ++iBot) {
    apBotChannels[iBot] = NetChannelCreate(
        NetTransportSimEndpoint(pSim, iBot + 1));
    CHECK(apBotChannels[iBot]);
  }
  if (iRelay) {
    tRvzSessionInfo info;
    pRendezvous = NetRendezvousCreate(NetTransportSimEndpoint(pSim, 3),
        NetDedicatedTestRandomBytes, &random);
    pHostDiscovery = NetDiscoveryCreate(pServerChannel, &rendezvousAddress,
        NetDedicatedTestRandomBytes, &random);
    CHECK(pRendezvous && pHostDiscovery);
    memset(&info, 0, sizeof(info));
    info.unTickRateHz = unTickRateHz;
    info.byPlayers = 1;
    info.byMaxPlayers = 2;
    info.byFlags = NET_RVZ_SESSION_DEDICATED;
    memcpy(info.szName, "RELAY RACE", 11);
    memcpy(info.szTrack, "TRACK5", 7);
    memcpy(info.szBuildHash, "e6-s4-test", 11);
    CHECK(NetDiscoveryHostStart(pHostDiscovery, &info));
    for (; ullNowMs < 2; ++ullNowMs) {
      CHECK(NetTransportSimAdvance(pSim, ullNowMs));
      NetRendezvousPump(pRendezvous);
      CHECK(NetTransportSimAdvance(pSim, ullNowMs));
      NetChannelPump(pServerChannel);
      NetDiscoveryPump(pHostDiscovery);
    }
    CHECK(NetDiscoveryHostRegistered(pHostDiscovery, &uiSessionId));
    for (int iBot = 0; iBot < 2; ++iBot) {
      apBotDiscovery[iBot] = NetDiscoveryCreate(apBotChannels[iBot],
          &rendezvousAddress, NetDedicatedTestRandomBytes, &random);
      CHECK(apBotDiscovery[iBot]);
      CHECK(NetDiscoveryPunch(apBotDiscovery[iBot], uiSessionId));
      for (uint64 ullEndMs = ullNowMs + NET_PUNCH_TIMEOUT_MS;
           ullNowMs <= ullEndMs; ++ullNowMs) {
        CHECK(NetTransportSimAdvance(pSim, ullNowMs));
        NetRendezvousPump(pRendezvous);
        CHECK(NetTransportSimAdvance(pSim, ullNowMs));
        NetChannelPump(apBotChannels[iBot]);
        NetDiscoveryPump(apBotDiscovery[iBot]);
      }
      CHECK(NetDiscoveryPunchState(apBotDiscovery[iBot], NULL) ==
            NET_PUNCH_RELAY_IN_PROGRESS);
      CHECK(NetTransportSimAdvance(pSim, ullNowMs));
      NetRendezvousPump(pRendezvous);
      CHECK(NetTransportSimAdvance(pSim, ullNowMs));
      NetChannelPump(pServerChannel);
      NetChannelPump(apBotChannels[iBot]);
      NetDiscoveryPump(pHostDiscovery);
      NetDiscoveryPump(apBotDiscovery[iBot]);
      CHECK(NetDiscoveryPunchState(apBotDiscovery[iBot], NULL) ==
            NET_PUNCH_RELAY_SUCCEEDED);
      ++ullNowMs;
    }
  }
  pDedicated = NetDedicatedCreate(pServerChannel, &config,
      NetDedicatedTestRandomBytes, &random);
  CHECK(pDedicated);

  for (int iBot = 0; iBot < 2; ++iBot) {
    char szName[sizeof(((tNetPlayerEntry *)0)->szName)];
    pConnection = NetChannelAddConnection(apBotChannels[iBot],
                                          &serverAddress, 0, 0);
    CHECK(pConnection);
    snprintf(szName, sizeof(szName), "BOT%d", iBot + 1);
    apBots[iBot] = NetBotCreate(pConnection, szName, (uint8)iBot, 1,
                                config.uiTrackCRC);
    CHECK(apBots[iBot] && NetBotStart(apBots[iBot]));
  }

  ullDeadlineMs = ullNowMs + NET_DEDICATED_TEST_MAX_MS;
  for (; ullNowMs < ullDeadlineMs; ++ullNowMs) {
    CHECK(NetTransportSimAdvance(pSim, ullNowMs));
    if (pRendezvous)
      NetRendezvousPump(pRendezvous);
    if (pHostDiscovery)
      NetDiscoveryPump(pHostDiscovery);
    CHECK(NetDedicatedPump(pDedicated));
    for (int iBot = 0; iBot < 2; ++iBot) {
      NetDedicatedTestPumpBot(apBotChannels[iBot], apBots[iBot], pDedicated);
      if (apBotDiscovery[iBot])
        NetDiscoveryPump(apBotDiscovery[iBot]);
    }
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

  if (ullNowMs >= ullDeadlineMs) {
    tNetRendezvousStats relayStats;
    NetRendezvousGetStats(pRendezvous, &relayStats);
    fprintf(stderr, "race timeout: relay=%llu/%llu throttled=%llu\n",
            (unsigned long long)relayStats.ullRelayPackets[0],
            (unsigned long long)relayStats.ullRelayPackets[1],
            (unsigned long long)relayStats.ullRelayThrottledPackets);
    for (int iBot = 0; iBot < 2; ++iBot) {
      CHECK(NetBotStats(apBots[iBot], &botStats));
      fprintf(stderr, "bot %d state=%d next=%u sent=%u snapshots=%u\n",
              iBot, NetBotState(apBots[iBot]), NetBotNextTick(apBots[iBot]),
              botStats.uiInputsSent, botStats.uiSnapshots);
    }
    exit(1);
  }
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

  NetRendezvousGetStats(pRendezvous, &relayStats);
  if (iRelay)
    CHECK(relayStats.iActiveRelays == 2 &&
          !relayStats.ullRelayThrottledPackets &&
          relayStats.ullRelayPackets[0] && relayStats.ullRelayPackets[1]);

  printf("NET-%s dedicated server at %u Hz passed: two bots completed one "
         "lap in %u ticks (%llu virtual ms)\n", iRelay ? "E6-S4 relay" :
         "E2-S6", unTickRateHz, serverStats.uiTicksSimulated,
         (unsigned long long)ullNowMs);
  if (iRelay)
    printf("  relay packets=%llu/%llu bytes=%llu/%llu, prediction mode=N/A "
           "(endpoint-only bots)\n",
           (unsigned long long)relayStats.ullRelayPackets[0],
           (unsigned long long)relayStats.ullRelayPackets[1],
           (unsigned long long)relayStats.ullRelayBytes[0],
           (unsigned long long)relayStats.ullRelayBytes[1]);

  for (int iBot = 0; iBot < 2; ++iBot) {
    NetBotDestroy(apBots[iBot]);
    NetDiscoveryDestroy(apBotDiscovery[iBot]);
    NetChannelDestroy(apBotChannels[iBot]);
  }
  NetDedicatedDestroy(pDedicated);
  NetDiscoveryDestroy(pHostDiscovery);
  NetRendezvousDestroy(pRendezvous);
  NetChannelDestroy(pServerChannel);
  NetTransportSimDestroy(pSim);
  return 0;
}
