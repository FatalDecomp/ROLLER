#include "net_frontend_lobby.h"

#include "3d.h"
#include "frontend.h"
#include "func3.h"
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
  tNetTransport transport;
  uint32 auiGamePackets[2], auiRelayPackets[2];
  uint32 auiLoggedMessages[4];
  uint32 uiSendErrors, uiReceiveErrors;
} tNetFrontendTransportTrace;

typedef struct
{
  tNetTransportUdp *pServerUdp, *pClientUdp;
  tNetFrontendTransportTrace serverTrace, clientTrace;
  tNetChannel *pServerChannel, *pClientChannel;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetLobbyHost *pHostLobby;
  tNetLobbyClient *pClientLobby;
  tNetHost *pRaceHost;
  tNetClient *pRaceClient;
  tNetDiscovery *pDiscovery;
  tNetAddress peer;
  tNetAddress directPeer;
  tNetAddress rendezvous;
  tRvzSessionInfo hostInfo;
  tRvzSessionInfo aBrowserSessions[NET_RVZ_MAX_SESSIONS];
  eNetServerType eServerType;
  char szRendezvous[320];
  uint16 unRendezvousPort;
  uint8 byRendezvousOverride, byHasDirectPeer, byJoinSelected;
  uint8 abyDisplayPlayers[NET_SESSION_MAX_PLAYERS];
  tNetSessionConfig appliedConfig;
  uint16 unLocalPort;
  uint8 byLocalPortOverride;
  uint8 byHasPeer, byOpen, byHost, byLobbyStarted;
  uint8 byHasRendezvous, byResolvePending, byRelayThrottleShown;
  uint8 byHostInfo;
  int iBrowserSessionCount;
  uint8 byConfigApplied, byPlayerInfoSent, byReadySent;
  uint8 byRaceScheduled, byRaceLoadedSent, byRaceCarsMapped, byRaceStarted;
  uint8 byLocalPlayers, bySelectedCar0, bySelectedCar1, bySelectedControl;
  uint8 byAppResumePending;
  uint64 ullRejoinStartedMs;
  uint64 ullJoinStartedMs;
  char szStatus[96];
  char szRaceError[96];
} tNetFrontendLobbyState;

static tNetFrontendLobbyState s_frontend = {
  .unLocalPort = ROLLER_DEFAULT_PORT,
  .eServerType = NET_SERVER_PUBLIC,
  .szRendezvous = "rvz.fatal.racing:7778",
  .unRendezvousPort = NET_RVZ_DEFAULT_PORT,
  .byLocalPlayers = 1
};

static uint32 NetFrontendReadProtocol(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static void NetFrontendTracePacket(tNetFrontendTransportTrace *pTrace,
                                    const tNetAddress *pPeer,
                                    const void *pData, int iLength, int iReceive)
{
  const uint8 *pBytes = pData;
  uint32 uiProtocol;
  int iOffset = 0;
  int iLogPath;
  uint8 byType;
  char szPeer[NET_ADDRESS_STRING_CAPACITY];
  if (iLength < 4 || s_frontend.byRaceStarted)
    return;
  uiProtocol = NetFrontendReadProtocol(pBytes);
  if (uiProtocol == NET_RELAY_PROTOCOL_ID) {
    ++pTrace->auiRelayPackets[iReceive];
    iOffset = NET_RELAY_HEADER_SIZE;
  } else if (uiProtocol == NET_PROTOCOL_ID) {
    ++pTrace->auiGamePackets[iReceive];
  } else if (uiProtocol == NET_RVZ_PROTOCOL_ID && iLength >= 28) {
    byType = pBytes[22];
    if (byType != NET_RVZ_MSG_RELAY_OFFER &&
        byType != NET_RVZ_MSG_RELAY_ALLOCATED && byType != NET_RVZ_MSG_ERROR)
      return;
    NetAddressFormat(pPeer, szPeer, sizeof(szPeer));
    SDL_Log("[NET] %s directory type=%u bytes=%d endpoint=%s",
            iReceive ? "RX" : "TX", (unsigned)byType, iLength, szPeer);
    return;
  } else {
    return;
  }
  if (iLength < iOffset + 28 ||
      NetFrontendReadProtocol(pBytes + iOffset) != NET_PROTOCOL_ID ||
      !pBytes[iOffset + 21])
    return;
  byType = pBytes[iOffset + 22];
  iLogPath = iReceive + (iOffset ? 2 : 0);
  if (byType < NET_MSG_JOIN_REQUEST || byType > NET_MSG_SESSION_CONFIG ||
      (pTrace->auiLoggedMessages[iLogPath] & (1u << byType)))
    return;
  pTrace->auiLoggedMessages[iLogPath] |= 1u << byType;
  NetAddressFormat(pPeer, szPeer, sizeof(szPeer));
  /* Metadata only: never log payloads, session credentials or relay tokens. */
  SDL_Log("[NET] %s %s first-message=%u bytes=%d endpoint=%s",
          iReceive ? "RX" : "TX", iOffset ? "relay" : "direct",
          (unsigned)byType, iLength, szPeer);
}

static int NetFrontendTraceSend(void *pContext, const tNetAddress *pPeer,
                                const void *pData, int iLength)
{
  tNetFrontendTransportTrace *pTrace = pContext;
  int iSent = pTrace->transport.pSend(pTrace->transport.pContext,
                                      pPeer, pData, iLength);
  if (iSent != iLength)
    ++pTrace->uiSendErrors;
  else
    NetFrontendTracePacket(pTrace, pPeer, pData, iLength, 0);
  return iSent;
}

static int NetFrontendTraceReceive(void *pContext, tNetAddress *pPeer,
                                   void *pData, int iCapacity)
{
  tNetFrontendTransportTrace *pTrace = pContext;
  int iLength = pTrace->transport.pReceive(pTrace->transport.pContext,
                                          pPeer, pData, iCapacity);
  if (iLength < 0)
    ++pTrace->uiReceiveErrors;
  else if (iLength > 0)
    NetFrontendTracePacket(pTrace, pPeer, pData, iLength, 1);
  return iLength;
}

static uint64 NetFrontendTraceNowMs(void *pContext)
{
  tNetFrontendTransportTrace *pTrace = pContext;
  return pTrace->transport.pNowMs(pTrace->transport.pContext);
}

static tNetTransport NetFrontendTraceEndpoint(
    tNetFrontendTransportTrace *pTrace, tNetTransportUdp *pUdp)
{
  tNetTransport transport = {pTrace, NetFrontendTraceSend,
                             NetFrontendTraceReceive, NetFrontendTraceNowMs};
  memset(pTrace, 0, sizeof(*pTrace));
  pTrace->transport = NetTransportUdpEndpoint(pUdp);
  return transport;
}

static void NetFrontendStatus(const char *szStatus)
{
  if (!szStatus)
    szStatus = "";
  if (*szStatus && strcmp(s_frontend.szStatus, szStatus))
    SDL_Log("[NET] %s", szStatus);
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

int NetFrontendSetServerType(eNetServerType eServerType)
{
  if (eServerType != NET_SERVER_PUBLIC && eServerType != NET_SERVER_LAN)
    return 0;
  s_frontend.eServerType = eServerType;
  return 1;
}

eNetServerType NetFrontendServerType(void)
{
  return s_frontend.eServerType;
}

void NetFrontendSetLocalPort(uint16 unPort)
{
  if (unPort) {
    s_frontend.unLocalPort = unPort;
    s_frontend.byLocalPortOverride = 1;
  }
}

int NetFrontendSetPeer(const char *szAddress, uint16 unDefaultPort)
{
  tNetAddress peer;
  if (!NetAddressParse(&peer, szAddress, unDefaultPort))
    return 0;
  s_frontend.directPeer = peer;
  s_frontend.byHasDirectPeer = 1;
  return 1;
}

int NetFrontendSetRendezvous(const char *szAddress, uint16 unDefaultPort)
{
  if (!szAddress || strlen(szAddress) >= sizeof(s_frontend.szRendezvous) ||
      (*szAddress && !NetAddressEndpointValid(szAddress, unDefaultPort)))
    return 0;
  snprintf(s_frontend.szRendezvous, sizeof(s_frontend.szRendezvous), "%s", szAddress);
  s_frontend.unRendezvousPort = unDefaultPort;
  s_frontend.byRendezvousOverride = 1;
  return 1;
}

void NetFrontendLoadRendezvous(const char *szAddress)
{
  if (!s_frontend.byRendezvousOverride) {
    NetFrontendSetRendezvous(szAddress, NET_RVZ_DEFAULT_PORT);
    s_frontend.byRendezvousOverride = 0;
  }
}

const char *NetFrontendRendezvous(void)
{
  return s_frontend.szRendezvous;
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
  s_frontend.ullJoinStartedMs = 0;
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
  s_frontend.iBrowserSessionCount = 0;
  s_frontend.byHasPeer = 0;
  s_frontend.byJoinSelected = 0;
  s_frontend.byRelayThrottleShown = 0;
  send_message_to = -1;
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
  s_frontend.byHasPeer = !s_frontend.byHost && s_frontend.byHasDirectPeer;
  if (s_frontend.byHasPeer)
    s_frontend.peer = s_frontend.directPeer;
  s_frontend.byHasRendezvous = (uint8)(
      s_frontend.eServerType == NET_SERVER_PUBLIC && s_frontend.szRendezvous[0] &&
      NetAddressResolve(&s_frontend.rendezvous, s_frontend.szRendezvous,
                         s_frontend.unRendezvousPort));

  /* Hosts own the advertised port. Browsers need a distinct source port so
     another instance can host on this machine, even if browsing began first.
     An explicit --port still selects the client's source port. */
  s_frontend.pServerUdp = NetTransportUdpCreate(
      s_frontend.byHost || s_frontend.byLocalPortOverride ?
          s_frontend.unLocalPort : 0);
  if (!s_frontend.pServerUdp) {
    NetFrontendStatus("UNABLE TO OPEN NETWORK PORT");
    return 0;
  }
  transport = NetFrontendTraceEndpoint(&s_frontend.serverTrace,
                                        s_frontend.pServerUdp);
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
      s_frontend.iBrowserSessionCount + s_frontend.byHasDirectPeer :
      (!s_frontend.byHost && s_frontend.byHasDirectPeer ? 1 : 0);
}

int NetFrontendBrowserSession(int iIndex, tRvzSessionInfo *pInfo)
{
  if (s_frontend.byHost || !pInfo || iIndex < 0 ||
      iIndex >= NetFrontendBrowserSessionCount())
    return 0;
  if (s_frontend.byHasDirectPeer) {
    if (!iIndex) {
      memset(pInfo, 0, sizeof(*pInfo));
      snprintf(pInfo->szName, sizeof(pInfo->szName), "DIRECT CONNECTION");
      pInfo->byMaxPlayers = NET_SESSION_MAX_PLAYERS;
      return 1;
    }
    --iIndex;
  }
  *pInfo = s_frontend.aBrowserSessions[iIndex];
  return 1;
}

int NetFrontendBrowserSelect(uint32 uiSessionId)
{
  if (!s_frontend.byOpen || s_frontend.byHost || s_frontend.byLobbyStarted)
    return 0;
  if (!uiSessionId && s_frontend.byHasDirectPeer) {
    s_frontend.peer = s_frontend.directPeer;
    s_frontend.byHasPeer = 1;
  } else {
    int iSession;
    for (iSession = 0; iSession < s_frontend.iBrowserSessionCount; ++iSession)
      if (s_frontend.aBrowserSessions[iSession].uiSessionId == uiSessionId)
        break;
    if (iSession == s_frontend.iBrowserSessionCount ||
        s_frontend.aBrowserSessions[iSession].byPlayers >=
            s_frontend.aBrowserSessions[iSession].byMaxPlayers ||
        (s_frontend.aBrowserSessions[iSession].byFlags & NET_RVZ_SESSION_IN_RACE) ||
        !NetDiscoveryPunch(s_frontend.pDiscovery, uiSessionId))
      return 0;
    s_frontend.byHasPeer = 0;
    s_frontend.byResolvePending = 1;
  }
  s_frontend.byJoinSelected = 1;
  NetFrontendStatus("CONNECTING TO SELECTED GAME");
  return 1;
}

int NetFrontendLobbyJoined(void)
{
  return s_frontend.pClient &&
      NetSessionClientState(s_frontend.pClient) == NET_JOIN_ACCEPTED;
}

int NetFrontendMessagePlayer(int iSelection)
{
  /* The legacy composer reserves row zero for ALL PLAYERS. */
  int iDisplay = iSelection - 1;
  if (iDisplay >= player1_car)
    ++iDisplay;
  return iSelection > 0 && iDisplay < network_on ? iDisplay : -1;
}

void NetFrontendLobbyUpdatePlayerInfo(void)
{
  if (!s_frontend.byLobbyStarted || s_frontend.byRaceScheduled ||
      player1_car < 0 || player1_car >= MAX_CARS ||
      (s_frontend.byLocalPlayers == 2 && (player2_car < 0 || player2_car >= MAX_CARS)))
    return;
  s_frontend.bySelectedCar0 = (uint8)(Players_Cars[player1_car] < 0 ? 0 : Players_Cars[player1_car]);
  s_frontend.bySelectedCar1 = s_frontend.byLocalPlayers == 2 ?
      (uint8)(Players_Cars[player2_car] < 0 ? 1 : Players_Cars[player2_car]) : NET_LOBBY_NO_PLAYER;
  s_frontend.bySelectedControl = (uint8)(manual_control[player1_car] == 2 ? 2 : 1);
  s_frontend.byPlayerInfoSent = 0;
}

void NetFrontendLobbyUpdateConfig(void)
{
  tNetSessionConfigOptions options;
  tNetSessionConfig config;
  if (!s_frontend.byLobbyStarted || s_frontend.byRaceScheduled)
    return;
  if (!s_frontend.byHost) {
    if (s_frontend.byConfigApplied)
      NetSessionConfigApply(&s_frontend.appliedConfig);
    return;
  }
  NetSessionConfigOptionsDefault(&options);
  if (s_frontend.byConfigApplied && NetSessionConfigBuild(&config, &options) &&
      !memcmp(&config, &s_frontend.appliedConfig, sizeof(config)))
    return;
  if (!NetSessionConfigBuild(&config, &options) ||
      !NetLobbyHostUpdateConfig(s_frontend.pHostLobby, &config)) {
    if (s_frontend.byConfigApplied)
      NetSessionConfigApply(&s_frontend.appliedConfig);
    NetFrontendStatus("SETTINGS CONFLICT WITH CONNECTED PLAYERS");
    return;
  }
  NetFrontendBuildHostInfo(&s_frontend.hostInfo, &config);
  NetDiscoveryHostUpdate(s_frontend.pDiscovery, &s_frontend.hostInfo);
}

void NetFrontendAppResumed(void)
{
  if (net_mode == NET_MODE_MODERN && s_frontend.byLobbyStarted)
    s_frontend.byAppResumePending = 1;
}

static int NetFrontendLobbyFailure(const char *szStatus)
{
  const tNetFrontendTransportTrace *pTrace = &s_frontend.serverTrace;
  tNetConnection *pConnection = s_frontend.pClient ?
      NetSessionClientConnection(s_frontend.pClient) : NULL;
  char szPeer[NET_ADDRESS_STRING_CAPACITY] = "none";
  if (s_frontend.byHasPeer)
    NetAddressFormat(&s_frontend.peer, szPeer, sizeof(szPeer));
  SDL_Log("[NET] join failed: %s; role=%s state=%d route=%d peer=%s "
          "direct tx/rx=%u/%u relay tx/rx=%u/%u socket errors tx/rx=%u/%u "
          "authenticated=%d expired=%d pending=%d",
          szStatus, s_frontend.byHost ? "host" : "client",
          (int)NetSessionClientState(s_frontend.pClient),
          (int)NetDiscoveryPunchState(s_frontend.pDiscovery, NULL), szPeer,
          (unsigned)pTrace->auiGamePackets[0], (unsigned)pTrace->auiGamePackets[1],
          (unsigned)pTrace->auiRelayPackets[0], (unsigned)pTrace->auiRelayPackets[1],
          (unsigned)pTrace->uiSendErrors, (unsigned)pTrace->uiReceiveErrors,
          NetConnectionSessionToken(pConnection) != 0,
          NetConnectionIsExpired(pConnection), NetConnectionPendingReliable(pConnection));
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
        NetFrontendTraceEndpoint(&s_frontend.clientTrace,
                                  s_frontend.pClientUdp));
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
  s_frontend.ullJoinStartedMs = NetChannelNowMs(s_frontend.pClientChannel);
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
    if (iDisplay + iCars > NET_SESSION_MAX_PLAYERS)
      return;
    if (iPlayer == byLocalPlayer) {
      iLocalDisplay = iDisplay;
      player2_car = iCars == 2 ? iDisplay + 1 : -1;
    }
    for (int iCar = 0; iCar < iCars; ++iCar) {
      Players_Cars[iDisplay] = abyCars[iCar];
      s_frontend.abyDisplayPlayers[iDisplay] = (uint8)iPlayer;
      manual_control[iDisplay] = player.byHumanControl;
      player_started[iDisplay] =
          player.byState >= NET_PLAYER_READY ? -1 : 0;
      memset(player_names[iDisplay], 0, sizeof(player_names[iDisplay]));
      memcpy(player_names[iDisplay], player.szName,
             sizeof(player_names[iDisplay]) - 1);
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
  check_cars();
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
  if (!s_frontend.byHost && s_frontend.pDiscovery &&
      (NetDiscoveryListReady(s_frontend.pDiscovery) ||
       !s_frontend.byHasRendezvous)) {
    int iSessions = NetDiscoverySessionCount(s_frontend.pDiscovery);
    if (iSessions > (int)(sizeof(s_frontend.aBrowserSessions) /
                          sizeof(s_frontend.aBrowserSessions[0])))
      iSessions = (int)(sizeof(s_frontend.aBrowserSessions) /
                        sizeof(s_frontend.aBrowserSessions[0]));
    s_frontend.iBrowserSessionCount = iSessions;
    for (int iSession = 0; iSession < iSessions; ++iSession)
      NetDiscoverySession(s_frontend.pDiscovery, iSession,
                          &s_frontend.aBrowserSessions[iSession]);
  }
  if (!s_frontend.byHost &&
      s_frontend.pDiscovery) {
    if (s_frontend.byResolvePending) {
      eNetPunchState ePunchState = NetDiscoveryPunchState(
          s_frontend.pDiscovery, &s_frontend.peer);
      if (ePunchState == NET_PUNCH_SUCCEEDED ||
          ePunchState == NET_PUNCH_RELAY_SUCCEEDED) {
        if (s_frontend.pClient && !NetConnectionSetPeer(
                NetSessionClientConnection(s_frontend.pClient), &s_frontend.peer)) {
          NetFrontendLobbyFailure("CONNECTION TIMED OUT - PLEASE TRY AGAIN");
          return;
        }
        s_frontend.byHasPeer = 1;
        s_frontend.byResolvePending = 0;
        NetFrontendStatus(ePunchState == NET_PUNCH_SUCCEEDED ?
                          "GAME FOUND" : "GAME FOUND VIA RELAY");
      } else if (ePunchState == NET_PUNCH_RELAY_IN_PROGRESS) {
        NetFrontendStatus("CONNECTING VIA RELAY");
      } else if (ePunchState == NET_PUNCH_TIMED_OUT) {
        NetFrontendLobbyFailure("CONNECTION TIMED OUT - PLEASE TRY AGAIN");
        return;
      }
    }
  }
  if (s_frontend.byJoinSelected && s_frontend.byHasPeer && !s_frontend.byLobbyStarted)
    NetFrontendLobbyBegin();
  if (!s_frontend.byRelayThrottleShown && s_frontend.pDiscovery &&
      NetDiscoveryPunchState(s_frontend.pDiscovery, NULL) ==
          NET_PUNCH_RELAY_THROTTLED) {
    s_frontend.byRelayThrottleShown = 1;
    NetFrontendStatus("RELAY BANDWIDTH LIMIT REACHED");
  }
  if (!s_frontend.byLobbyStarted)
    return;

  if (s_frontend.byHostInfo && s_frontend.pHostLobby) {
    int iPlayers = 0;
    for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
      tNetPlayerEntry player;
      if (NetLobbyHostPlayer(s_frontend.pHostLobby, (uint8)iPlayer, &player))
        iPlayers += player.byCarIdx1 == NET_LOBBY_NO_PLAYER ? 1 : 2;
    }
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
  if (send_message_to >= 0) {
    int iDisplay = NetFrontendMessagePlayer(send_message_to);
    uint8 byTarget = send_message_to == 0 ? NET_LOBBY_NO_PLAYER :
        (iDisplay >= 0 ? s_frontend.abyDisplayPlayers[iDisplay] : NET_LOBBY_NO_PLAYER);
    send_mes_buf[sizeof(send_mes_buf) - 1] = 0;
    send_status = (send_message_to == 0 || iDisplay >= 0) &&
        NetLobbyClientSendText(s_frontend.pClientLobby, byTarget, send_mes_buf) ? 18 : -18;
    send_message_to = -1;
  }
  if (!rec_status) {
    tNetChat chat;
    tNetPlayerEntry sender;
    if (NetLobbyClientTakeText(s_frontend.pClientLobby, &chat) &&
        NetLobbyClientPlayer(s_frontend.pClientLobby, chat.bySenderPlayerIdx, &sender)) {
      snprintf(rec_mes_name, sizeof(rec_mes_name), "%.8s", sender.szName);
      snprintf(rec_mes_buf, sizeof(rec_mes_buf), "%.31s", chat.szText);
      rec_status = 36;
    }
  }
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
      !NetSessionClientGetConfig(s_frontend.pClient, &config)) {
    if (!s_frontend.byHost && !s_frontend.pRaceClient) {
      uint64 ullElapsedMs = NetChannelNowMs(s_frontend.pClientChannel) -
          s_frontend.ullJoinStartedMs;
      if (ullElapsedMs >= NET_CONNECTION_TIMEOUT_MS) {
        NetFrontendLobbyFailure("CONNECTION TIMED OUT - PLEASE TRY AGAIN");
        return;
      }
      if (state == NET_JOIN_WAITING && !s_frontend.byResolvePending &&
          ullElapsedMs >= NET_PUNCH_TIMEOUT_MS &&
          NetDiscoveryRequestRelay(s_frontend.pDiscovery)) {
        s_frontend.byResolvePending = 1;
        NetFrontendStatus("CONNECTING VIA RELAY");
      }
    }
    return;
  }
  NetDiscoveryConfirmJoin(s_frontend.pDiscovery);

  if (!s_frontend.byConfigApplied ||
      (!s_frontend.byRaceScheduled &&
       memcmp(&config, &s_frontend.appliedConfig, sizeof(config)))) {
    if (!NetSessionConfigApply(&config)) {
      NetFrontendStatus("SESSION CONFIGURATION REJECTED");
      return;
    }
    s_frontend.byConfigApplied = 1;
    s_frontend.appliedConfig = config;
    s_frontend.byReadySent = 0;
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
  /* The roster is frozen once loading begins.  AllocateCars() replaces the
     frontend display indices in player1_car/player2_car with real Car[]
     slots; applying the lobby roster after that would corrupt the camera and
     other local-player state while waiting at the load barrier. */
  if (!s_frontend.byRaceScheduled)
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
