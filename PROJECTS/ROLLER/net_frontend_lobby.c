#include "net_frontend_lobby.h"

#include "3d.h"
#include "frontend.h"
#include "loadtrak.h"
#include "net_channel.h"
#include "net_client.h"
#include "net_config.h"
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
  tNetAddress peer;
  uint16 unLocalPort;
  uint8 byHasPeer, byOpen, byHost, byLobbyStarted;
  uint8 byConfigApplied, byPlayerInfoSent, byReadySent;
  uint8 byRaceScheduled, byRaceLoadedSent, byRaceStarted;
  uint8 bySelectedCar, bySelectedControl;
  char szStatus[96];
} tNetFrontendLobbyState;

static tNetFrontendLobbyState s_frontend = {
  .unLocalPort = ROLLER_DEFAULT_PORT
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
  s_frontend.byRaceStarted = 0;
}

void NetFrontendClose(void)
{
  NetFrontendDestroyLobby();
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
  if (!s_frontend.byHost && !s_frontend.byHasPeer) {
    NetFrontendStatus("DIRECT CONNECT ADDRESS REQUIRED");
    return 0;
  }

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
  NetFrontendStatus(s_frontend.byHost ? "HOST READY" : "CLIENT READY");
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
      pConnection, NET_PROTOCOL_VERSION, 1, NetFrontendPlayerName());
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
  s_frontend.bySelectedCar =
      (uint8)(Players_Cars[player1_car] < 0 ? 0 : Players_Cars[player1_car]);
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
    s_frontend.pHostLobby = NetLobbyHostCreate(s_frontend.pHost);
    if (!s_frontend.pHostLobby ||
        !NetAddressParse(&peer, "127.0.0.1", s_frontend.unLocalPort) ||
        !NetFrontendCreateClient(&peer))
      return NetFrontendLobbyFailure("LOCAL HOST JOIN FAILED");
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
    if (!NetLobbyClientPlayer(s_frontend.pClientLobby, (uint8)iPlayer,
                              &player))
      continue;
    if (iPlayer == byLocalPlayer)
      iLocalDisplay = iDisplay;
    Players_Cars[iDisplay] = player.byCarIdx0;
    manual_control[iDisplay] = player.byHumanControl;
    player_started[iDisplay] = player.byState >= NET_PLAYER_READY ? -1 : 0;
    memset(player_names[iDisplay], 0, sizeof(player_names[iDisplay]));
    memcpy(player_names[iDisplay], player.szName,
           sizeof(player_names[iDisplay]));
    if (player_started[iDisplay])
      ++iReady;
    ++iDisplay;
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
  wConsoleNode = (int16)iLocalDisplay;
  master = s_frontend.byHost ? -1 : 0;
}

void NetFrontendPump(void)
{
  tNetSessionConfig config;
  eNetJoinState state;
  if (net_mode != NET_MODE_MODERN || !s_frontend.byLobbyStarted)
    return;

  NetSessionHostPump(s_frontend.pHost);
  NetLobbyHostPump(s_frontend.pHostLobby);
  NetSessionClientPump(s_frontend.pClient);
  NetHostPump(s_frontend.pRaceHost);
  NetClientPump(s_frontend.pRaceClient);
  state = NetSessionClientState(s_frontend.pClient);
  if (state == NET_JOIN_REFUSED) {
    if (NetSessionClientRefuseReason(s_frontend.pClient) ==
        NET_JOIN_REFUSE_TRACK_CRC_MISMATCH) {
      g_iNetworkTrackFileCRCMismatch = -1;
      NetFrontendStatus("TRACK FILE CRC MISMATCH");
    } else {
      NetFrontendStatus("SESSION JOIN REFUSED");
    }
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
        s_frontend.pClientLobby, s_frontend.bySelectedCar,
        NET_LOBBY_NO_PLAYER, s_frontend.bySelectedControl);
  }
  if (s_frontend.byPlayerInfoSent && !s_frontend.byReadySent) {
    s_frontend.byReadySent = (uint8)NetLobbyClientSetReady(
        s_frontend.pClientLobby, 1,
        NetFrontendLocalTrackCRC(&config));
  }
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
