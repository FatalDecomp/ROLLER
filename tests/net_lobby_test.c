#include "net_lobby.h"
#include "net_race_start.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); \
} } while (0)

#define NET_TEST_CLIENTS 3

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
} tNetTestClient;

static int NetTestRandomBytes(void *pContext, void *pData, int iLength)
{
  tNetTestRandom *pRandom = (tNetTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  for (iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState ^= pRandom->ullState << 13;
    pRandom->ullState ^= pRandom->ullState >> 7;
    pRandom->ullState ^= pRandom->ullState << 17;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 24);
  }
  return 1;
}

static tNetAddress NetTestAddress(uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.abAddress[0] = 127;
  address.abAddress[3] = 1;
  address.unPort = unPort;
  address.byFamily = NET_ADDR_IPV4;
  return address;
}

static tNetSessionConfig NetTestConfig(uint8 byMaxPlayers)
{
  tNetSessionConfig config;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = 36;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = byMaxPlayers;
  config.byPauseAllowed = 1;
  config.iTrackLoad = 7;
  config.iGameType = 0;
  config.iManualControl = 1;
  config.iCompetitors = 8;
  config.iDamageLevel = 1;
  config.uiRandomSeed = 0x12345678u;
  config.uiTrackCRC = 0xa1b2c3d4u;
  memcpy(config.szBuildHash, "e2-s2-test", 11);
  CHECK(NetSessionConfigValidate(&config));
  return config;
}

static void NetTestPump(tNetTransportSim *pSim, tNetSessionHost *pHost,
                        tNetLobbyHost *pHostLobby,
                        tNetTestClient *pClients, int iClientCount,
                        int iStart, int iEnd)
{
  int iClient, iTime;
  for (iTime = iStart; iTime <= iEnd; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    NetSessionHostPump(pHost);
    NetLobbyHostPump(pHostLobby);
    for (iClient = 0; iClient < iClientCount; ++iClient)
      NetSessionClientPump(pClients[iClient].pSession);
  }
}

static void NetTestDestroyClients(tNetTestClient *pClients, int iCount)
{
  int iClient;
  for (iClient = 0; iClient < iCount; ++iClient) {
    NetLobbyClientDestroy(pClients[iClient].pLobby);
    NetSessionClientDestroy(pClients[iClient].pSession);
    NetChannelDestroy(pClients[iClient].pChannel);
  }
}

static void NetTestHostAndThreeClients(void)
{
  static const char *aszNames[NET_TEST_CLIENTS] = {
    "Alpha", "Beta", "Gamma"
  };
  tNetTransportSim *pSim = NetTransportSimCreate(0xe252u);
  tNetAddress hostAddress = NetTestAddress(0);
  tNetChannel *pHostChannel;
  tNetSessionHost *pHost;
  tNetLobbyHost *pHostLobby;
  tNetSessionConfig config = NetTestConfig(4);
  tNetTestRandom random = {0x123456789abcdef0ull};
  tNetTestClient aClients[NET_TEST_CLIENTS] = {0};
  tNetPlayerEntry aBefore[4];
  int aiBeforePresent[4];
  tNetPlayerEntry player;
  tNetChat chat;
  uint8 abBadList[sizeof(tNetPlayerListHeader) +
                  4 * sizeof(tNetPlayerEntry)] = {0};
  uint8 abBadCountdown[sizeof(tNetCountdown)] = {0};
  uint8 abyRaceCar0[4], abyRaceCar1[4], abyBadRaceCar0[4];
  uint32 uiStartTick;
  tNetRaceStartClock aRaceClocks[NET_TEST_CLIENTS];
  int iClient, iPlayer;

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel);
  pHost = NetSessionHostCreate(pHostChannel, config.byMaxPlayers,
                               NetTestRandomBytes, &random);
  CHECK(pHost && NetSessionHostSetConfig(pHost, &config));
  pHostLobby = NetLobbyHostCreate(pHost);
  CHECK(pHostLobby);

  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    aClients[iClient].pChannel = NetChannelCreate(
        NetTransportSimEndpoint(pSim, iClient + 1));
    CHECK(aClients[iClient].pChannel);
    aClients[iClient].pConnection = NetChannelAddConnection(
        aClients[iClient].pChannel, &hostAddress, 0, 0);
    CHECK(aClients[iClient].pConnection);
    aClients[iClient].pSession = NetSessionClientCreate(
        aClients[iClient].pConnection, NET_PROTOCOL_VERSION, 1,
        aszNames[iClient]);
    CHECK(aClients[iClient].pSession);
    aClients[iClient].pLobby = NetLobbyClientCreate(
        aClients[iClient].pSession);
    CHECK(aClients[iClient].pLobby &&
          NetSessionClientStart(aClients[iClient].pSession));
  }

  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS, 0, 500);
  CHECK(NetSessionHostPlayerCount(pHost) == NET_TEST_CLIENTS);
  CHECK(NetLobbyHostPlayerCount(pHostLobby) == NET_TEST_CLIENTS);
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    uint8 byPlayerIdx = NetSessionClientPlayerIndex(
        aClients[iClient].pSession);
    CHECK(NetSessionClientState(aClients[iClient].pSession) ==
          NET_JOIN_ACCEPTED);
    CHECK(NetLobbyClientPlayerCount(aClients[iClient].pLobby) ==
          NET_TEST_CLIENTS);
    CHECK(NetLobbyClientPlayer(aClients[iClient].pLobby, byPlayerIdx,
                               &player));
    CHECK(player.byState == NET_PLAYER_LOBBY);
    CHECK(strcmp(player.szName, aszNames[iClient]) == 0);
  }
  for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    tNetPlayerEntry hostPlayer;
    int iPresent = NetLobbyHostPlayer(pHostLobby, (uint8)iPlayer,
                                      &hostPlayer);
    for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
      int iClientPresent = NetLobbyClientPlayer(aClients[iClient].pLobby,
                                                (uint8)iPlayer, &player);
      CHECK(iClientPresent == iPresent);
      if (iPresent)
        CHECK(memcmp(&player, &hostPlayer, sizeof(player)) == 0);
    }
    aiBeforePresent[iPlayer] = NetLobbyClientPlayer(
        aClients[0].pLobby, (uint8)iPlayer, &aBefore[iPlayer]);
  }

  abBadList[0] = 100;
  abBadList[2] = config.byMaxPlayers;
  for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    uint8 *pEntry = abBadList + sizeof(tNetPlayerListHeader) +
        iPlayer * sizeof(tNetPlayerEntry);
    pEntry[1] = NET_LOBBY_NO_PLAYER;
    pEntry[2] = NET_LOBBY_NO_PLAYER;
  }
  abBadList[4] = NET_PLAYER_LOBBY;
  abBadList[5] = 0;
  abBadList[7] = 1;
  memcpy(abBadList + 8, "Rogue", 6);
  abBadList[17] = NET_PLAYER_LOBBY;
  abBadList[18] = 0;
  abBadList[20] = 1;
  memcpy(abBadList + 21, "Copy", 5);
  CHECK(NetConnectionQueueMessage(NetSessionHostPlayerConnection(
      pHost, NetSessionClientPlayerIndex(aClients[0].pSession)),
      NET_MSG_PLAYER_LIST, NET_MSG_RELIABLE | NET_MSG_ORDERED,
      abBadList, sizeof(abBadList)));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS, 501, 550);
  for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    int iPresent = NetLobbyClientPlayer(aClients[0].pLobby,
                                        (uint8)iPlayer, &player);
    CHECK(iPresent == aiBeforePresent[iPlayer]);
    if (iPresent)
      CHECK(memcmp(&player, &aBefore[iPlayer], sizeof(player)) == 0);
  }

  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient)
    CHECK(NetLobbyClientSetReady(aClients[iClient].pLobby, 1,
                                 config.uiTrackCRC));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS, 551, 900);
  for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    if (!NetLobbyHostPlayer(pHostLobby, (uint8)iPlayer, &player))
      continue;
    CHECK(player.byState == NET_PLAYER_READY);
    for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
      CHECK(NetLobbyClientPlayer(aClients[iClient].pLobby,
                                 (uint8)iPlayer, &player));
      CHECK(player.byState == NET_PLAYER_READY);
    }
  }
  CHECK(NetLobbyHostAllReady(pHostLobby));

  memset(&chat, 0, sizeof(chat));
  chat.bySenderPlayerIdx = NET_LOBBY_NO_PLAYER;
  chat.byTargetPlayerIdx = NET_LOBBY_NO_PLAYER;
  chat.byKind = NET_CHAT_STRATEGY;
  chat.byValue = 1;
  chat.szText[1] = 'X';
  CHECK(NetConnectionQueueMessage(aClients[0].pConnection, NET_MSG_CHAT,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, &chat, sizeof(chat)));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS, 901, 1000);
  CHECK(!NetLobbyHostLastChat(pHostLobby, &chat));

  CHECK(NetLobbyClientSendStrategy(aClients[1].pLobby,
                                   NET_LOBBY_NO_PLAYER, 2));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS,
              1001, 1200);
  CHECK(NetLobbyHostLastChat(pHostLobby, &chat));
  CHECK(chat.bySenderPlayerIdx ==
        NetSessionClientPlayerIndex(aClients[1].pSession));
  CHECK(chat.byTargetPlayerIdx == NET_LOBBY_NO_PLAYER);
  CHECK(chat.byKind == NET_CHAT_STRATEGY && chat.byValue == 2);
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    CHECK(NetLobbyClientLastChat(aClients[iClient].pLobby, &chat));
    CHECK(chat.byValue == 2);
  }

  CHECK(NetLobbyHostStart(pHostLobby, 4242));
  CHECK(!NetLobbyHostAllReady(pHostLobby));
  CHECK(!NetLobbyHostRaceReleased(pHostLobby));
  CHECK(NetLobbyHostStartTick(pHostLobby, &uiStartTick));
  CHECK(uiStartTick == 4242);
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS,
              1201, 1500);
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    CHECK(NetLobbyClientStartTick(aClients[iClient].pLobby, &uiStartTick));
    CHECK(uiStartTick == 4242);
    for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
      if (!NetLobbyClientPlayer(aClients[iClient].pLobby,
                                (uint8)iPlayer, &player))
        continue;
      CHECK(player.byState == NET_PLAYER_RACING);
    }
  }

  abBadCountdown[0] = 0x92;
  abBadCountdown[1] = 0x10;
  abBadCountdown[4] = 100;
  abBadCountdown[6] = NET_PLAYER_RACING;
  abBadCountdown[7] = NET_COUNTDOWN_RELEASE + 1;
  CHECK(NetConnectionQueueMessage(NetSessionHostPlayerConnection(
      pHost, NetSessionClientPlayerIndex(aClients[0].pSession)),
      NET_MSG_COUNTDOWN, NET_MSG_RELIABLE | NET_MSG_ORDERED,
      abBadCountdown, sizeof(abBadCountdown)));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS,
              1501, 1550);
  CHECK(!NetLobbyClientRaceReleased(aClients[0].pLobby, &uiStartTick));

  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    NetRaceClockReset(&aRaceClocks[iClient]);
    CHECK(NetRaceClockSchedule(&aRaceClocks[iClient], uiStartTick));
  }
  for (iClient = 0; iClient < NET_TEST_CLIENTS - 1; ++iClient) {
    CHECK(NetLobbyClientSetRaceLoaded(aClients[iClient].pLobby));
    NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS,
                1551 + iClient * 100, 1650 + iClient * 100);
    CHECK(!NetLobbyHostRaceReleased(pHostLobby));
  }
  CHECK(NetLobbyClientSetRaceLoaded(
      aClients[NET_TEST_CLIENTS - 1].pLobby));
  NetTestPump(pSim, pHost, pHostLobby, aClients, NET_TEST_CLIENTS,
              1751, 2050);
  CHECK(NetLobbyHostRaceReleased(pHostLobby));
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    uint32 uiReleaseTick;
    CHECK(NetLobbyClientRaceReleased(aClients[iClient].pLobby,
                                     &uiReleaseTick));
    CHECK(uiReleaseTick == 4242);
    CHECK(NetRaceClockRelease(&aRaceClocks[iClient], uiReleaseTick));
    CHECK(NetRaceClockPhase(&aRaceClocks[iClient]) ==
          NET_RACE_START_PRE_START);
  }

  /* Car selections are frontend design indices until AllocateCars() has
     produced the real, potentially non-contiguous Car[] slots. */
  memset(abyRaceCar0, NET_LOBBY_NO_PLAYER, sizeof(abyRaceCar0));
  memset(abyRaceCar1, NET_LOBBY_NO_PLAYER, sizeof(abyRaceCar1));
  iClient = 0;
  for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
    if (!NetLobbyHostPlayer(pHostLobby, (uint8)iPlayer, &player))
      continue;
    abyRaceCar0[iPlayer] = (uint8)(iClient * 2);
    ++iClient;
  }
  memcpy(abyBadRaceCar0, abyRaceCar0, sizeof(abyBadRaceCar0));
  abyBadRaceCar0[NetSessionClientPlayerIndex(aClients[1].pSession)] =
      abyBadRaceCar0[NetSessionClientPlayerIndex(aClients[0].pSession)];
  CHECK(!NetLobbyHostSetRaceCars(pHostLobby, abyBadRaceCar0,
                                  abyRaceCar1, config.byMaxPlayers));
  CHECK(NetLobbyHostSetRaceCars(pHostLobby, abyRaceCar0, abyRaceCar1,
                                config.byMaxPlayers));
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    CHECK(NetLobbyClientSetRaceCars(aClients[iClient].pLobby,
                                    abyRaceCar0, abyRaceCar1,
                                    config.byMaxPlayers));
    for (iPlayer = 0; iPlayer < config.byMaxPlayers; ++iPlayer) {
      if (!NetLobbyClientPlayer(aClients[iClient].pLobby,
                                (uint8)iPlayer, &player))
        continue;
      CHECK(player.byCarIdx0 == abyRaceCar0[iPlayer]);
      CHECK(player.byCarIdx1 == NET_LOBBY_NO_PLAYER);
    }
  }
  for (iPlayer = 0; iPlayer <= 145; ++iPlayer) {
    uint32 uiReferenceTick = 0;
    for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
      uint32 uiRaceTick;
      CHECK(NetRaceClockBeginTick(&aRaceClocks[iClient], &uiRaceTick));
      if (!iClient)
        uiReferenceTick = uiRaceTick;
      CHECK(uiRaceTick == uiReferenceTick);
      NetRaceClockEndTick(&aRaceClocks[iClient], iPlayer);
    }
  }
  CHECK(NetRaceClockRunningTick(&aRaceClocks[0]) == 4242u + 145u);
  for (iClient = 0; iClient < NET_TEST_CLIENTS; ++iClient) {
    CHECK(NetRaceClockPhase(&aRaceClocks[iClient]) ==
          NET_RACE_START_RUNNING);
    CHECK(NetRaceClockRunningTick(&aRaceClocks[iClient]) ==
          NetRaceClockRunningTick(&aRaceClocks[0]));
  }

  NetTestDestroyClients(aClients, NET_TEST_CLIENTS);
  NetLobbyHostDestroy(pHostLobby);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestTrackMismatchRefused(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(0xbad5eedu);
  tNetAddress hostAddress = NetTestAddress(0);
  tNetChannel *pHostChannel;
  tNetSessionHost *pHost;
  tNetLobbyHost *pHostLobby;
  tNetSessionConfig config = NetTestConfig(1);
  tNetTestRandom random = {0xfedcba9876543210ull};
  tNetTestClient client = {0};

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  client.pChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pHostChannel && client.pChannel);
  pHost = NetSessionHostCreate(pHostChannel, 1, NetTestRandomBytes, &random);
  CHECK(pHost && NetSessionHostSetConfig(pHost, &config));
  pHostLobby = NetLobbyHostCreate(pHost);
  CHECK(pHostLobby);
  client.pConnection = NetChannelAddConnection(client.pChannel,
                                                &hostAddress, 0, 0);
  CHECK(client.pConnection);
  client.pSession = NetSessionClientCreate(client.pConnection,
      NET_PROTOCOL_VERSION, 1, "WrongCRC");
  CHECK(client.pSession);
  client.pLobby = NetLobbyClientCreate(client.pSession);
  CHECK(client.pLobby && NetSessionClientStart(client.pSession));

  NetTestPump(pSim, pHost, pHostLobby, &client, 1, 0, 400);
  CHECK(NetSessionClientState(client.pSession) == NET_JOIN_ACCEPTED);
  CHECK(NetLobbyClientSetReady(client.pLobby, 1,
                               config.uiTrackCRC ^ 0xffffffffu));
  NetTestPump(pSim, pHost, pHostLobby, &client, 1, 401, 800);
  CHECK(NetSessionClientState(client.pSession) == NET_JOIN_REFUSED);
  CHECK(NetSessionClientRefuseReason(client.pSession) ==
        NET_JOIN_REFUSE_TRACK_CRC_MISMATCH);
  CHECK(NetSessionHostPlayerCount(pHost) == 0);
  CHECK(NetLobbyHostPlayerCount(pHostLobby) == 0);

  NetTestDestroyClients(&client, 1);
  NetLobbyHostDestroy(pHostLobby);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

/* NET-FIX-3: one of two clients never reports loaded.  With iCrash the
   client process disappears (its connection expires); otherwise it stays
   connected and silent until the grace-window deadline. */
static void NetTestLoadDeadline(int iCrash)
{
  static const char *aszNames[2] = {"Loaded", "Silent"};
  tNetTransportSim *pSim = NetTransportSimCreate(iCrash ? 0xc4a5u : 0x51e7u);
  tNetAddress hostAddress = NetTestAddress(0);
  tNetChannel *pHostChannel;
  tNetSessionHost *pHost;
  tNetLobbyHost *pHostLobby;
  tNetSessionConfig config = NetTestConfig(2);
  tNetTestRandom random = {0x0badc0ffee123457ull};
  tNetTestClient aClients[2] = {0};
  tNetRaceStartClock clock;
  tNetPlayerEntry player;
  uint32 uiStartTick = 0, uiRaceTick;
  uint8 bySilentIdx;
  int iClient, iTick, iStartMs, iReleaseMs, iLastMs;

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel);
  pHost = NetSessionHostCreate(pHostChannel, config.byMaxPlayers,
                               NetTestRandomBytes, &random);
  CHECK(pHost && NetSessionHostSetConfig(pHost, &config));
  pHostLobby = NetLobbyHostCreate(pHost);
  CHECK(pHostLobby);
  for (iClient = 0; iClient < 2; ++iClient) {
    aClients[iClient].pChannel = NetChannelCreate(
        NetTransportSimEndpoint(pSim, iClient + 1));
    CHECK(aClients[iClient].pChannel);
    aClients[iClient].pConnection = NetChannelAddConnection(
        aClients[iClient].pChannel, &hostAddress, 0, 0);
    aClients[iClient].pSession = NetSessionClientCreate(
        aClients[iClient].pConnection, NET_PROTOCOL_VERSION, 1,
        aszNames[iClient]);
    CHECK(aClients[iClient].pSession);
    aClients[iClient].pLobby = NetLobbyClientCreate(
        aClients[iClient].pSession);
    CHECK(aClients[iClient].pLobby &&
          NetSessionClientStart(aClients[iClient].pSession));
  }
  NetTestPump(pSim, pHost, pHostLobby, aClients, 2, 0, 500);
  for (iClient = 0; iClient < 2; ++iClient)
    CHECK(NetLobbyClientSetReady(aClients[iClient].pLobby, 1,
                                 config.uiTrackCRC));
  NetTestPump(pSim, pHost, pHostLobby, aClients, 2, 501, 900);
  CHECK(NetLobbyHostAllReady(pHostLobby));
  bySilentIdx = NetSessionClientPlayerIndex(aClients[1].pSession);

  iStartMs = 900;
  CHECK(NetLobbyHostStart(pHostLobby, 777));
  NetTestPump(pSim, pHost, pHostLobby, aClients, 2, 901, 1200);
  CHECK(NetLobbyClientStartTick(aClients[0].pLobby, &uiStartTick));
  CHECK(uiStartTick == 777);
  NetRaceClockReset(&clock);
  CHECK(NetRaceClockSchedule(&clock, uiStartTick));
  CHECK(NetLobbyClientSetRaceLoaded(aClients[0].pLobby));
  NetTestPump(pSim, pHost, pHostLobby, aClients, 2, 1201, 1500);
  CHECK(!NetLobbyHostRaceReleased(pHostLobby));

  if (iCrash) {
    /* The process dies mid-load: nothing more is sent from it. */
    NetLobbyClientDestroy(aClients[1].pLobby);
    NetSessionClientDestroy(aClients[1].pSession);
    NetChannelDestroy(aClients[1].pChannel);
    memset(&aClients[1], 0, sizeof(aClients[1]));
    iLastMs = 1500 + NET_CONNECTION_TIMEOUT_MS + 1000;
  } else {
    iLastMs = iStartMs + NET_REJOIN_GRACE_MS + 1000;
  }
  for (iReleaseMs = 1501; iReleaseMs <= iLastMs; ++iReleaseMs) {
    NetTestPump(pSim, pHost, pHostLobby, aClients, iCrash ? 1 : 2,
                iReleaseMs, iReleaseMs);
    if (NetLobbyHostRaceReleased(pHostLobby))
      break;
  }
  CHECK(NetLobbyHostRaceReleased(pHostLobby));
  if (iCrash)
    CHECK(iReleaseMs > 1500 - NET_KEEPALIVE_MS + NET_CONNECTION_TIMEOUT_MS &&
          iReleaseMs < iStartMs + NET_REJOIN_GRACE_MS);
  else
    CHECK(iReleaseMs >= iStartMs + NET_REJOIN_GRACE_MS);
  NetTestPump(pSim, pHost, pHostLobby, aClients, iCrash ? 1 : 2,
              iReleaseMs + 1, iReleaseMs + 300);

  CHECK(NetLobbyHostPlayer(pHostLobby, bySilentIdx, &player));
  CHECK(player.byState == NET_PLAYER_DROPPED);
  CHECK(player.byCarIdx0 == bySilentIdx);
  CHECK(NetLobbyClientPlayer(aClients[0].pLobby, bySilentIdx, &player));
  CHECK(player.byState == NET_PLAYER_DROPPED);
  if (!iCrash) {
    uint32 uiIgnored;
    CHECK(NetSessionClientState(aClients[1].pSession) == NET_JOIN_REFUSED);
    CHECK(NetSessionClientRefuseReason(aClients[1].pSession) ==
          NET_JOIN_REFUSE_LOAD_TIMEOUT);
    CHECK(!NetLobbyClientRaceReleased(aClients[1].pLobby, &uiIgnored));
  }

  /* The loaded client's tick stream starts at the agreed tick and runs on
     without a gap across the release. */
  CHECK(NetLobbyClientRaceReleased(aClients[0].pLobby, &uiRaceTick));
  CHECK(uiRaceTick == 777);
  CHECK(NetRaceClockRelease(&clock, uiRaceTick));
  for (iTick = 0; iTick <= 145; ++iTick) {
    CHECK(NetRaceClockBeginTick(&clock, &uiRaceTick));
    CHECK(uiRaceTick == 777u + (uint32)iTick);
    NetRaceClockEndTick(&clock, iTick);
  }
  CHECK(NetRaceClockPhase(&clock) == NET_RACE_START_RUNNING);
  CHECK(NetRaceClockRunningTick(&clock) == 777u + 145u);

  NetTestDestroyClients(aClients, iCrash ? 1 : 2);
  NetLobbyHostDestroy(pHostLobby);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
  printf("load deadline (%s): released at %d ms, %d ms after start\n",
         iCrash ? "crashed client" : "silent client", iReleaseMs,
         iReleaseMs - iStartMs);
}

int main(void)
{
  NetTestHostAndThreeClients();
  NetTestTrackMismatchRefused();
  NetTestLoadDeadline(0);
  NetTestLoadDeadline(1);
  puts("net lobby tests passed");
  return 0;
}
