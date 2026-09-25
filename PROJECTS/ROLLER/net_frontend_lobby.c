#include "net_frontend_lobby.h"

#include "3d.h"
#include "frontend.h"
#include "loadtrak.h"
#include "net_channel.h"
#include "net_client.h"
#include "net_config.h"
#include "net_discovery.h"
#include "net_host.h"
#include "net_lobby.h"
#include "net_race_start.h"
#include "net_session.h"
#include "net_transport.h"
#include "net_types.h"
#include "network.h"
#include "rollercomms.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
  tNetTransportUdp *pServerUdp, *pClientUdp;
  tNetChannel *pServerChannel, *pClientChannel;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetLobbyHost *pHostLobby;
  tNetLobbyClient *pClientLobby;
  tNetHost *pRaceHost;
  tNetClient *pRaceClient;
  tNetDiscovery *pDiscovery;
  tNetAddress peer;
  tNetAddress rendezvous;
  tRvzSessionInfo hostInfo;
  uint16 unLocalPort;
  uint8 byHasPeer, byOpen, byHost, byLobbyStarted;
  uint8 byHasRendezvous, byResolvePending, byRelayThrottleShown;
  uint8 byHostInfo;
  uint8 byConfigApplied, byPlayerInfoSent, byReadySent;
  uint8 byRaceScheduled, byRaceLoadedSent, byRaceCarsMapped, byRaceStarted;
  uint8 byLocalPlayers, bySelectedCar0, bySelectedCar1, bySelectedControl;
  uint8 byAppResumePending;
  uint64 ullRejoinStartedMs;
  char szStatus[96];
  char szRaceError[96];
} tNetFrontendLobbyState;

static tNetFrontendLobbyState s_frontend = {
  .unLocalPort = ROLLER_DEFAULT_PORT,
  .byLocalPlayers = 1
};

static void NetFrontendStatus(const char *szStatus)
{
  if (!szStatus)
    szStatus = "";
  snprintf(s_frontend.szStatus, sizeof(s_frontend.szStatus), "%s",
           szStatus);
}

static const char *NetFrontendPlayerName(void)
{
  if (my_name[0])
    return my_name;
  if (player_names[0][0])
    return player_names[0];
  return "PLAYER";
}

static void NetFrontendBuildHostInfo(tRvzSessionInfo *pInfo,
                                     const tNetSessionConfig *pConfig)
{
  tNetSessionConfigOptions options;
  int iTrackLoad = pConfig ? pConfig->iTrackLoad : TrackLoad;
  const char *szTrack = iTrackLoad > 0 && iTrackLoad < 8 &&
      names[iTrackLoad] ? names[iTrackLoad] : "COMMUNITY";
  NetSessionConfigOptionsDefault(&options);
  memset(pInfo, 0, sizeof(*pInfo));
  pInfo->unTickRateHz = pConfig ? pConfig->unTickRateHz : 36;
  pInfo->byPlayers = 1;
  pInfo->byMaxPlayers = pConfig ? pConfig->byMaxPlayers :
                                  options.byMaxPlayers;
  snprintf(pInfo->szName, sizeof(pInfo->szName), "%s'S GAME",
           NetFrontendPlayerName());
  snprintf(pInfo->szTrack, sizeof(pInfo->szTrack), "%s", szTrack);
  memcpy(pInfo->szBuildHash,
         pConfig ? pConfig->szBuildHash : options.szBuildHash,
         sizeof(pInfo->szBuildHash));
}

void NetFrontendSetLocalPort(uint16 unPort)
{
  if (unPort)
    s_frontend.unLocalPort = unPort;
}

int NetFrontendSetPeer(const char *szAddress, uint16 unDefaultPort)
{
  tNetAddress peer;
  if (!NetAddressParse(&peer, szAddress, unDefaultPort))
    return 0;
  s_frontend.peer = peer;
  s_frontend.byHasPeer = 1;
  return 1;
}

int NetFrontendSetRendezvous(const char *szAddress, uint16 unDefaultPort)
{
  tNetAddress rendezvous;
  if (!NetAddressParse(&rendezvous, szAddress, unDefaultPort))
    return 0;
  s_frontend.rendezvous = rendezvous;
  s_frontend.byHasRendezvous = 1;
  return 1;
}

int NetFrontendSetLocalPlayers(int iLocalPlayers)
{
  if ((iLocalPlayers != 1 && iLocalPlayers != 2) || s_frontend.byOpen)
    return 0;
  s_frontend.byLocalPlayers = (uint8)iLocalPlayers;
  return 1;
}

static void NetFrontendDestroyLobby(void)
{
  NetClientDestroy(s_frontend.pRaceClient);
  s_frontend.pRaceClient = NULL;
  NetHostDestroy(s_frontend.pRaceHost);
  s_frontend.pRaceHost = NULL;
  NetLobbyClientDestroy(s_frontend.pClientLobby);
  s_frontend.pClientLobby = NULL;
  NetSessionClientDestroy(s_frontend.pClient);
  s_frontend.pClient = NULL;
  NetLobbyHostDestroy(s_frontend.pHostLobby);
  s_frontend.pHostLobby = NULL;
  NetSessionHostDestroy(s_frontend.pHost);
  s_frontend.pHost = NULL;
  s_frontend.byLobbyStarted = 0;
  s_frontend.byConfigApplied = 0;
  s_frontend.byPlayerInfoSent = 0;
  s_frontend.byReadySent = 0;
  s_frontend.byRaceScheduled = 0;
  s_frontend.byRaceLoadedSent = 0;
  s_frontend.byRaceCarsMapped = 0;
  s_frontend.byRaceStarted = 0;
  s_frontend.byAppResumePending = 0;
  s_frontend.ullRejoinStartedMs = 0;
  s_frontend.szRaceError[0] = '\0';
}

void NetFrontendClose(void)
{
  NetFrontendDestroyLobby();
  NetDiscoveryDestroy(s_frontend.pDiscovery);
  s_frontend.pDiscovery = NULL;
  NetChannelDestroy(s_frontend.pClientChannel);
  s_frontend.pClientChannel = NULL;
  NetChannelDestroy(s_frontend.pServerChannel);
  s_frontend.pServerChannel = NULL;
  NetTransportUdpDestroy(s_frontend.pClientUdp);
  s_frontend.pClientUdp = NULL;
  NetTransportUdpDestroy(s_frontend.pServerUdp);
  s_frontend.pServerUdp = NULL;
  s_frontend.byOpen = 0;
  s_frontend.byHost = 0;
  s_frontend.byResolvePending = 0;
  s_frontend.byHostInfo = 0;
  net_listen_host = 0;
  NetRaceStartReset();
  network_on = 0;
  players = 1;
  players_waiting = 0;
  NetFrontendStatus("");
}

int NetFrontendOpen(void)
{
  tNetTransport transport;
  NetFrontendClose();
  s_frontend.byHost = network_slot >= 0;

  s_frontend.pServerUdp = NetTransportUdpCreate(s_frontend.unLocalPort);
  if (!s_frontend.pServerUdp) {
    NetFrontendStatus("UNABLE TO OPEN NETWORK PORT");
    return 0;
  }
  transport = NetTransportUdpEndpoint(s_frontend.pServerUdp);
  if (s_frontend.byHost)
    s_frontend.pServerChannel = NetChannelCreate(transport);
  else
    s_frontend.pClientChannel = NetChannelCreate(transport);
  if (s_frontend.byHost ? !s_frontend.pServerChannel :
                          !s_frontend.pClientChannel) {
    NetFrontendClose();
    NetFrontendStatus("UNABLE TO CREATE NETWORK CHANNEL");
    return 0;
  }

  s_frontend.byOpen = 1;
  net_listen_host = s_frontend.byHost;
  network_on = 1;
  players = 1;
  players_waiting = 0;
  if (s_frontend.byHost || !s_frontend.byHasPeer ||
      s_frontend.byHasRendezvous) {
    tNetAddress aCandidates[NET_RVZ_MAX_LOCAL_CANDIDATES];
    tNetChannel *pChannel = s_frontend.byHost ? s_frontend.pServerChannel :
                                                s_frontend.pClientChannel;
    int iCandidates;
    s_frontend.pDiscovery = NetDiscoveryCreate(
        pChannel, s_frontend.byHasRendezvous ? &s_frontend.rendezvous : NULL,
        NetPlatformRandomBytes, NULL);
    if (!s_frontend.pDiscovery ||
        !NetDiscoveryEnableLan(s_frontend.pDiscovery,
                               s_frontend.unLocalPort)) {
      NetFrontendClose();
      NetFrontendStatus("DISCOVERY START FAILED");
      return 0;
    }
    iCandidates = NetAddressEnumerateLocal(
        aCandidates, NET_RVZ_MAX_LOCAL_CANDIDATES,
        NetTransportUdpPort(s_frontend.pServerUdp));
    if (iCandidates > 0)
      NetDiscoverySetLocalCandidates(s_frontend.pDiscovery,
                                      aCandidates, iCandidates);
    if (s_frontend.byHost) {
      tRvzSessionInfo info;
      NetFrontendBuildHostInfo(&info, NULL);
      if (!NetDiscoveryHostStart(s_frontend.pDiscovery, &info)) {
        NetFrontendClose();
        NetFrontendStatus("DISCOVERY REGISTRATION FAILED");
        return 0;
      }
      s_frontend.hostInfo = info;
      s_frontend.byHostInfo = 1;
    } else if (!s_frontend.byHasPeer) {
      tNetSessionConfigOptions options;
      NetSessionConfigOptionsDefault(&options);
      NetDiscoveryList(s_frontend.pDiscovery, options.szBuildHash);
    }
  }
  NetFrontendStatus(s_frontend.byHost ? "HOST READY" :
                    (s_frontend.byHasPeer ? "CLIENT READY" :
                                            "SEARCHING FOR GAMES"));
  return 1;
}

int NetFrontendIsOpen(void)
{
  return s_frontend.byOpen;
}

int NetFrontendIsHost(void)
{
  return s_frontend.byOpen && s_frontend.byHost;
}

int NetFrontendBrowserSessionCount(void)
{
  return s_frontend.pDiscovery && !s_frontend.byHost ?
      NetDiscoverySessionCount(s_frontend.pDiscovery) : 0;
}

int NetFrontendBrowserSession(int iIndex, tRvzSessionInfo *pInfo)
{
  return s_frontend.pDiscovery && !s_frontend.byHost &&
      NetDiscoverySession(s_frontend.pDiscovery, iIndex, pInfo);
}

void NetFrontendAppResumed(void)
{
  if (net_mode == NET_MODE_MODERN && s_frontend.byLobbyStarted)
    s_frontend.byAppResumePending = 1;
}

static int NetFrontendLobbyFailure(const char *szStatus)
{
  NetFrontendClose();
  NetFrontendStatus(szStatus);
  return 0;
}

static int NetFrontendCreateClient(const tNetAddress *pPeer)
{
  tNetConnection *pConnection;
  if (s_frontend.byHost) {
    s_frontend.pClientUdp = NetTransportUdpCreate(0);
    if (!s_frontend.pClientUdp)
      return 0;
    s_frontend.pClientChannel = NetChannelCreate(
        NetTransportUdpEndpoint(s_frontend.pClientUdp));
  }
  if (!s_frontend.pClientChannel)
    return 0;
  pConnection = NetChannelAddConnection(s_frontend.pClientChannel, pPeer,
                                        0, 0);
  if (!pConnection)
    return 0;
  s_frontend.pClient = NetSessionClientCreate(
      pConnection, NET_PROTOCOL_VERSION, s_frontend.byLocalPlayers,
      NetFrontendPlayerName());
  if (!s_frontend.pClient)
    return 0;
  s_frontend.pClientLobby = NetLobbyClientCreate(s_frontend.pClient);
  return s_frontend.pClientLobby &&
      NetSessionClientStart(s_frontend.pClient);
}

int NetFrontendLobbyBegin(void)
{
  tNetAddress peer;
  if (!s_frontend.byOpen || s_frontend.byLobbyStarted)
    return s_frontend.byLobbyStarted;
  if (!s_frontend.byHost && !s_frontend.byHasPeer) {
    NetFrontendStatus("WAITING FOR DIRECT CONNECTION");
    return 0;
  }
  if (player1_car < 0 || player1_car >= MAX_CARS ||
      (s_frontend.byLocalPlayers == 2 &&
       (player2_car < 0 || player2_car >= MAX_CARS ||
        player2_car == player1_car)))
    return NetFrontendLobbyFailure("LOCAL PLAYER SELECTION INVALID");
  s_frontend.bySelectedCar0 =
      (uint8)(Players_Cars[player1_car] < 0 ? 0 : Players_Cars[player1_car]);
  s_frontend.bySelectedCar1 = s_frontend.byLocalPlayers == 2 ?
      (uint8)(Players_Cars[player2_car] < 0 ? 1 : Players_Cars[player2_car]) :
      NET_LOBBY_NO_PLAYER;
  if (s_frontend.bySelectedCar0 == s_frontend.bySelectedCar1)
    s_frontend.bySelectedCar1 = s_frontend.bySelectedCar0 ? 0 : 1;
  s_frontend.bySelectedControl =
      (uint8)(manual_control[player1_car] == 2 ? 2 : 1);

  if (s_frontend.byHost) {
    tNetSessionConfigOptions options;
    tNetSessionConfig config;
    NetSessionConfigOptionsDefault(&options);
    if (!NetSessionConfigBuild(&config, &options))
      return NetFrontendLobbyFailure("SESSION CONFIGURATION INVALID");
    s_frontend.pHost = NetSessionHostCreate(
        s_frontend.pServerChannel, config.byMaxPlayers,
        NetPlatformRandomBytes, NULL);
    if (!s_frontend.pHost ||
        !NetSessionHostSetConfig(s_frontend.pHost, &config))
      return NetFrontendLobbyFailure("SECURE SESSION START FAILED");
    if (s_frontend.pDiscovery) {
      tRvzSessionInfo info;
      NetFrontendBuildHostInfo(&info, &config);
      if (s_frontend.byHostInfo)
        NetDiscoveryHostUpdate(s_frontend.pDiscovery, &info);
      else if (!NetDiscoveryHostStart(s_frontend.pDiscovery, &info))
        return NetFrontendLobbyFailure("DISCOVERY REGISTRATION FAILED");
      s_frontend.hostInfo = info;
      s_frontend.byHostInfo = 1;
    }
    s_frontend.pHostLobby = NetLobbyHostCreate(s_frontend.pHost);
    if (!s_frontend.pHostLobby ||
        !NetAddressParse(&peer, "127.0.0.1", s_frontend.unLocalPort) ||
        !NetFrontendCreateClient(&peer))
      return NetFrontendLobbyFailure("LOCAL HOST JOIN FAILED");
    /* The listen host's in-process client uses this address again if the
       Android activity sleeps and resumes during a race. */
    s_frontend.peer = peer;
  } else if (!NetFrontendCreateClient(&s_frontend.peer))
    return NetFrontendLobbyFailure("JOIN START FAILED");

  s_frontend.byLobbyStarted = 1;
  NetFrontendStatus(s_frontend.byHost ? "WAITING FOR PLAYERS" :
                    "CONNECTING TO HOST");
  return 1;
}

static uint32 NetFrontendLocalTrackCRC(const tNetSessionConfig *pConfig)
{
  const char *szPath;
  if (pConfig->iTrackLoad == TRACK_LOAD_COMMUNITY) {
    if (g_iCommunityTrackMissing || !community_track_available())
      return 0;
    szPath = community_track_path();
  } else {
    szPath = names[pConfig->iTrackLoad];
  }
  return szPath ? community_track_crc(szPath) : 0;
}

static void NetFrontendSyncLegacyRoster(void)
{
  uint8 byLocalPlayer = NetSessionClientPlayerIndex(s_frontend.pClient);
  int iDisplay = 0;
  int iPlayer;
  int iLocalDisplay = 0;
  int iReady = 0;
  for (iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    tNetPlayerEntry player;
    uint8 abyCars[2];
    int iCars;
    if (!NetLobbyClientPlayer(s_frontend.pClientLobby, (uint8)iPlayer,
                              &player))
      continue;
    abyCars[0] = player.byCarIdx0;
    abyCars[1] = player.byCarIdx1;
    iCars = player.byCarIdx1 == NET_LOBBY_NO_PLAYER ? 1 : 2;
    if (iPlayer == byLocalPlayer) {
      iLocalDisplay = iDisplay;
      player2_car = iCars == 2 ? iDisplay + 1 : -1;
    }
    for (int iCar = 0; iCar < iCars; ++iCar) {
      Players_Cars[iDisplay] = abyCars[iCar];
      manual_control[iDisplay] = player.byHumanControl;
      player_started[iDisplay] =
          player.byState >= NET_PLAYER_READY ? -1 : 0;
      memset(player_names[iDisplay], 0, sizeof(player_names[iDisplay]));
      memcpy(player_names[iDisplay], player.szName,
             sizeof(player_names[iDisplay]));
      if (player_started[iDisplay])
        ++iReady;
      ++iDisplay;
    }
  }
  for (iPlayer = iDisplay; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    Players_Cars[iPlayer] = -1;
    player_started[iPlayer] = 0;
    memset(player_names[iPlayer], 0, sizeof(player_names[iPlayer]));
  }
  network_on = iDisplay ? iDisplay : 1;
  players = network_on;
  players_waiting = iReady;
  player1_car = iLocalDisplay;
  player_type = s_frontend.byLocalPlayers == 2 ? 2 : 1;
  wConsoleNode = (int16)iLocalDisplay;
  master = s_frontend.byHost ? -1 : 0;
}

/* The lobby carries selected car designs so the old frontend can display
   them.  AllocateCars() turns those selections into real Car[] slots while
   loading; race ownership must use those slots, not the design numbers. */
static int NetFrontendMapRaceCars(void)
{
  uint8 abyCarIdx0[NET_SESSION_MAX_PLAYERS];
  uint8 abyCarIdx1[NET_SESSION_MAX_PLAYERS];
  uint8 abyCarUsed[MAX_CARS] = {0};
  int iDisplay = 0;
  int iSlots = NetLobbyClientPlayerSlots(s_frontend.pClientLobby);
  int iPlayer;
  if (iSlots < 1 || iSlots > NET_SESSION_MAX_PLAYERS)
    return 0;
  memset(abyCarIdx0, NET_LOBBY_NO_PLAYER, sizeof(abyCarIdx0));
  memset(abyCarIdx1, NET_LOBBY_NO_PLAYER, sizeof(abyCarIdx1));
  for (iPlayer = 0; iPlayer < iSlots; ++iPlayer) {
    tNetPlayerEntry player;
    int iCars;
    if (!NetLobbyClientPlayer(s_frontend.pClientLobby, (uint8)iPlayer,
                              &player))
      continue;
    iCars = player.byCarIdx1 == NET_LOBBY_NO_PLAYER ? 1 : 2;
    for (int iCar = 0; iCar < iCars; ++iCar) {
      int iRaceCar;
      if (iDisplay >= network_on)
        return 0;
      iRaceCar = player_to_car[iDisplay++];
      if (iRaceCar < 0 || iRaceCar >= numcars || iRaceCar >= MAX_CARS ||
          abyCarUsed[iRaceCar])
        return 0;
      abyCarUsed[iRaceCar] = 1;
      if (!iCar)
        abyCarIdx0[iPlayer] = (uint8)iRaceCar;
      else
        abyCarIdx1[iPlayer] = (uint8)iRaceCar;
    }
  }
  if (iDisplay != network_on ||
      !NetLobbyClientSetRaceCars(s_frontend.pClientLobby, abyCarIdx0,
                                  abyCarIdx1, iSlots) ||
      (s_frontend.byHost &&
       !NetLobbyHostSetRaceCars(s_frontend.pHostLobby, abyCarIdx0,
                                abyCarIdx1, iSlots)))
    return 0;
  s_frontend.byRaceCarsMapped = 1;
  return 1;
}

static int NetFrontendBeginRejoin(void)
{
  tNetConnection *pOldConnection;
  tNetConnection *pRejoin;
  if (!s_frontend.pRaceClient || !s_frontend.pClient ||
      NetClientRecoveryState(s_frontend.pRaceClient) != NET_RECOVERY_RACING)
    return 1;
  pOldConnection = NetSessionClientConnection(s_frontend.pClient);
  pRejoin = NetChannelAddConnection(
      s_frontend.pClientChannel, &s_frontend.peer,
      NetSessionClientToken(s_frontend.pClient),
      (uint8)(NetSessionClientGeneration(s_frontend.pClient) + 1u));
  if (!pRejoin || !NetClientBeginRejoin(s_frontend.pRaceClient, pRejoin)) {
    if (pRejoin)
      NetChannelRemoveConnection(s_frontend.pClientChannel, pRejoin);
    snprintf(s_frontend.szRaceError, sizeof(s_frontend.szRaceError),
             "%s", "Session rejoin failed");
    return 0;
  }
  /* NetClientBeginRejoin has moved every session lookup to pRejoin.  Retire
     the old generation so repeated mobile resumes cannot exhaust the
     channel's bounded connection table. */
  NetChannelRemoveConnection(s_frontend.pClientChannel, pOldConnection);
  s_frontend.ullRejoinStartedMs = NetConnectionNowMs(pRejoin);
  return 1;
}

void NetFrontendPump(void)
{
  tNetSessionConfig config;
  eNetJoinState state;
  if (net_mode != NET_MODE_MODERN || !s_frontend.byOpen)
    return;

  NetDiscoveryPump(s_frontend.pDiscovery);
  if (!s_frontend.byHost && !s_frontend.byHasPeer &&
      s_frontend.pDiscovery) {
    tRvzSessionInfo info;
    if (!s_frontend.byResolvePending &&
        NetDiscoverySession(s_frontend.pDiscovery, 0, &info)) {
      s_frontend.byResolvePending = (uint8)NetDiscoveryPunch(
          s_frontend.pDiscovery, info.uiSessionId);
    }
    if (s_frontend.byResolvePending) {
      eNetPunchState ePunchState = NetDiscoveryPunchState(
          s_frontend.pDiscovery, &s_frontend.peer);
      if (ePunchState == NET_PUNCH_SUCCEEDED ||
          ePunchState == NET_PUNCH_RELAY_SUCCEEDED) {
        s_frontend.byHasPeer = 1;
        s_frontend.byResolvePending = 0;
        NetFrontendStatus(ePunchState == NET_PUNCH_SUCCEEDED ?
                          "GAME FOUND" : "GAME FOUND VIA RELAY");
      }
    }
  }
  if (!s_frontend.byRelayThrottleShown && s_frontend.pDiscovery &&
      NetDiscoveryPunchState(s_frontend.pDiscovery, NULL) ==
          NET_PUNCH_RELAY_THROTTLED) {
    s_frontend.byRelayThrottleShown = 1;
    NetFrontendStatus("RELAY BANDWIDTH LIMIT REACHED");
  }
  if (!s_frontend.byLobbyStarted)
    return;

  if (s_frontend.byHostInfo && s_frontend.pHostLobby) {
    int iPlayers = NetLobbyHostPlayerCount(s_frontend.pHostLobby);
    s_frontend.hostInfo.byPlayers = (uint8)(iPlayers > 0 ? iPlayers : 1);
    if (s_frontend.byRaceStarted)
      s_frontend.hostInfo.byFlags |= NET_RVZ_SESSION_IN_RACE;
    NetDiscoveryHostUpdate(s_frontend.pDiscovery, &s_frontend.hostInfo);
  }

  NetSessionHostPump(s_frontend.pHost);
  NetLobbyHostPump(s_frontend.pHostLobby);
  if (s_frontend.byAppResumePending) {
    s_frontend.byAppResumePending = 0;
    if (!NetFrontendBeginRejoin())
      return;
  } else if (s_frontend.pRaceClient && s_frontend.pClient &&
      NetSessionClientState(s_frontend.pClient) == NET_JOIN_ACCEPTED &&
      NetConnectionIsExpired(
          NetSessionClientConnection(s_frontend.pClient))) {
    if (!NetFrontendBeginRejoin())
      return;
  }
  NetSessionClientPump(s_frontend.pClient);
  NetHostPump(s_frontend.pRaceHost);
  NetClientPump(s_frontend.pRaceClient);
  if (s_frontend.pRaceClient &&
      NetClientRecoveryState(s_frontend.pRaceClient) != NET_RECOVERY_RACING) {
    tNetConnection *pConnection =
        NetSessionClientConnection(s_frontend.pClient);
    uint64 ullNowMs = NetConnectionNowMs(pConnection);
    if (s_frontend.ullRejoinStartedMs &&
        ullNowMs - s_frontend.ullRejoinStartedMs > NET_REJOIN_GRACE_MS)
      snprintf(s_frontend.szRaceError, sizeof(s_frontend.szRaceError),
               "%s", NetJoinRefuseReasonString(
                   NET_JOIN_REFUSE_REJOIN_EXPIRED));
  } else if (s_frontend.pRaceClient) {
    s_frontend.ullRejoinStartedMs = 0;
    s_frontend.szRaceError[0] = '\0';
  }
  state = NetSessionClientState(s_frontend.pClient);
  if (state == NET_JOIN_REFUSED) {
    eNetJoinRefuseReason reason =
        NetSessionClientRefuseReason(s_frontend.pClient);
    if (reason == NET_JOIN_REFUSE_TRACK_CRC_MISMATCH)
      g_iNetworkTrackFileCRCMismatch = -1;
    NetFrontendStatus(NetJoinRefuseReasonString(reason));
    return;
  }
  if (state != NET_JOIN_ACCEPTED ||
      !NetSessionClientGetConfig(s_frontend.pClient, &config))
    return;

  if (!s_frontend.byConfigApplied) {
    if (!NetSessionConfigApply(&config)) {
      NetFrontendStatus("SESSION CONFIGURATION REJECTED");
      return;
    }
    s_frontend.byConfigApplied = 1;
  }
  if (!s_frontend.byPlayerInfoSent) {
    s_frontend.byPlayerInfoSent = (uint8)NetLobbyClientSetPlayerInfo(
        s_frontend.pClientLobby, s_frontend.bySelectedCar0,
        s_frontend.bySelectedCar1, s_frontend.bySelectedControl);
  }
  if (s_frontend.byPlayerInfoSent && !s_frontend.byReadySent) {
    s_frontend.byReadySent = (uint8)NetLobbyClientSetReady(
        s_frontend.pClientLobby, 1,
        NetFrontendLocalTrackCRC(&config));
  }
  if (!s_frontend.byRaceCarsMapped)
    NetFrontendSyncLegacyRoster();
  if (s_frontend.byReadySent)
    NetFrontendStatus(s_frontend.byHost ? "READY - WAITING FOR PLAYERS" :
                      "READY");
}

int NetFrontendLobbyCanStart(void)
{
  return s_frontend.byLobbyStarted && s_frontend.byHost &&
      s_frontend.pHostLobby &&
      NetLobbyHostAllReady(s_frontend.pHostLobby);
}

int NetFrontendLobbyRequestStart(uint32 uiStartTick)
{
  return NetFrontendLobbyCanStart() &&
      NetLobbyHostStart(s_frontend.pHostLobby, uiStartTick);
}

int NetFrontendLobbyStartTick(uint32 *puiStartTick)
{
  if (!s_frontend.pClientLobby ||
      !NetLobbyClientStartTick(s_frontend.pClientLobby, puiStartTick))
    return 0;
  if (!s_frontend.byRaceScheduled) {
    if (!NetRaceStartSchedule(*puiStartTick))
      return 0;
    s_frontend.byRaceScheduled = 1;
  }
  return 1;
}

int NetFrontendRaceSynchronise(void)
{
  uint32 uiStartTick;
  if (!s_frontend.pClientLobby || !s_frontend.byRaceScheduled)
    return 0;
  if (!s_frontend.byRaceLoadedSent) {
    if (!NetLobbyClientSetRaceLoaded(s_frontend.pClientLobby))
      return 0;
    s_frontend.byRaceLoadedSent = 1;
    NetFrontendStatus("WAITING FOR PLAYERS TO LOAD");
  }
  if (!NetLobbyClientRaceReleased(s_frontend.pClientLobby, &uiStartTick))
    return 0;
  if (NetRaceStartPhase() == NET_RACE_START_LOADING &&
      !NetRaceStartRelease(uiStartTick))
    return 0;
  if (!s_frontend.byRaceCarsMapped && !NetFrontendMapRaceCars()) {
    NetFrontendStatus("RACE CAR ASSIGNMENT FAILED");
    return 0;
  }
  if (!s_frontend.byRaceStarted) {
    memset(&g_netStats, 0, sizeof(g_netStats));
    g_netStats.iPredictionMode = NET_PREDICT_FULL;
    if (s_frontend.byHost) {
      s_frontend.pRaceHost = NetHostCreate(s_frontend.pHost,
                                           s_frontend.pHostLobby);
      if (!s_frontend.pRaceHost ||
          !NetHostBeginRace(s_frontend.pRaceHost)) {
        NetHostDestroy(s_frontend.pRaceHost);
        s_frontend.pRaceHost = NULL;
        NetFrontendStatus("HOST RACE START FAILED");
        return 0;
      }
      /* A listen host renders the authoritative world and never puppets it. */
      memset(net_puppet_car, 0, sizeof(net_puppet_car));
      net_sim_puppet_hook = NULL;
      net_sim_authority = NET_AUTHORITY_LOCAL;
      net_sim_replaying = 0;
    } else {
      s_frontend.pRaceClient = NetClientCreate(s_frontend.pClient,
                                               s_frontend.pClientLobby);
      if (!s_frontend.pRaceClient ||
          !NetClientBeginRace(s_frontend.pRaceClient)) {
        NetClientDestroy(s_frontend.pRaceClient);
        s_frontend.pRaceClient = NULL;
        NetFrontendStatus("CLIENT RACE START FAILED");
        return 0;
      }
    }
    s_frontend.byRaceStarted = 1;
  }
  NetFrontendStatus("");
  return NetRaceStartPhase() >= NET_RACE_START_PRE_START;
}

int NetFrontendRaceTicksDue(void)
{
  return s_frontend.byRaceStarted && !s_frontend.byHost ?
      NetClientTicksDue(s_frontend.pRaceClient) : 0;
}

int NetFrontendRaceLocalPlayers(void)
{
  if (!s_frontend.byRaceStarted)
    return 0;
  if (s_frontend.byHost) {
    uint8 byPlayerIdx = NetSessionClientPlayerIndex(s_frontend.pClient);
    return NetSessionHostPlayerLocalPlayers(s_frontend.pHost, byPlayerIdx);
  }
  return NetClientGroup(s_frontend.pRaceClient, NULL);
}

int NetFrontendRaceTick(uint32 uiTick, const tCarInputData *pInputs,
                        int iCount)
{
  if (!s_frontend.byRaceStarted || !pInputs || iCount < 1)
    return 0;
  if (s_frontend.byHost) {
    uint8 byPlayerIdx = NetSessionClientPlayerIndex(s_frontend.pClient);
    if (NetHostNextTick(s_frontend.pRaceHost) != uiTick ||
        !NetHostSetLocalInputs(s_frontend.pRaceHost, byPlayerIdx, uiTick,
                               pInputs, iCount))
      return 0;
    return NetHostTick(s_frontend.pRaceHost, uiTick);
  }
  if (NetClientCurrentTick(s_frontend.pRaceClient) + 1u != uiTick)
    return 0;
  return NetClientTick(s_frontend.pRaceClient, pInputs);
}

int NetFrontendRaceSetPaused(int iPaused)
{
  return s_frontend.byRaceStarted && s_frontend.byHost &&
      NetHostSetPaused(s_frontend.pRaceHost, iPaused);
}

int NetFrontendRacePaused(void)
{
  if (!s_frontend.byRaceStarted)
    return 0;
  return s_frontend.byHost ? NetHostPaused(s_frontend.pRaceHost) :
      NetClientPaused(s_frontend.pRaceClient);
}

eNetRaceState NetFrontendRaceState(void)
{
  if (!s_frontend.byRaceStarted)
    return NET_RACE_STOPPED;
  return s_frontend.byHost ? NetHostRaceState(s_frontend.pRaceHost) :
      NetClientRaceState(s_frontend.pRaceClient);
}

int NetFrontendRaceResults(int *piFinishers, int *piHumanFinishers)
{
  if (!s_frontend.byRaceStarted)
    return 0;
  return s_frontend.byHost ?
      NetHostResults(s_frontend.pRaceHost, piFinishers, piHumanFinishers) :
      NetClientResults(s_frontend.pRaceClient, piFinishers,
                       piHumanFinishers);
}

int NetFrontendSendStrategy(uint8 byMessage)
{
  uint8 byTarget = NET_LOBBY_NO_PLAYER;
  if (!s_frontend.pClientLobby)
    return 0;
  if (network_mes_mode < -1)
    return 0;
  if (network_mes_mode >= 0) {
    if (network_mes_mode >= MAX_CARS || !human_control[network_mes_mode] ||
        car_to_player[network_mes_mode] < 0 ||
        car_to_player[network_mes_mode] >= NET_SESSION_MAX_PLAYERS)
      return 0;
    byTarget = (uint8)car_to_player[network_mes_mode];
  }
  return NetLobbyClientSendStrategy(s_frontend.pClientLobby, byTarget,
                                    byMessage);
}

const char *NetFrontendLobbyStatus(void)
{
  return s_frontend.szStatus;
}

const char *NetFrontendRaceStatus(void)
{
  if (s_frontend.szRaceError[0])
    return s_frontend.szRaceError;
  return s_frontend.pRaceClient ? NetClientStatus(s_frontend.pRaceClient) :
      "";
}

int NetFrontendHostNetworkStatus(int iPlayer, char *szStatus,
                                 size_t uiStatusSize)
{
  tNetHostPlayerStats stats;
  const char *szName;
  if (!szStatus || !uiStatusSize || !s_frontend.pRaceHost ||
      iPlayer < 0 || iPlayer >= NET_SESSION_MAX_PLAYERS ||
      !NetHostPlayerStats(s_frontend.pRaceHost, (uint8)iPlayer, &stats))
    return 0;
  szName = NetSessionHostPlayerName(s_frontend.pHost, (uint8)iPlayer);
  snprintf(szStatus, uiStatusSize, "%s: %.0f ms%s",
           szName ? szName : "PLAYER", stats.fRttMs,
           stats.byLateInputWarning ? " !" : "");
  return 1;
}
