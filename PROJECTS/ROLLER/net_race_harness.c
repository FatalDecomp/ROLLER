#include "net_race_harness.h"

#include "net_client.h"
#include "net_host.h"
#include "net_input.h"
#include "net_sim_seam.h"
#include "3d.h"
#include "roller.h"

#include <math.h>
#include <stddef.h>

#define NET_HARNESS_ENVELOPE_SIZE 4
#define NET_HARNESS_START_TICK 5000u

typedef enum
{
  NET_HARNESS_RACE_NONE = 0,
  NET_HARNESS_RACE_HOST,
  NET_HARNESS_RACE_CLIENT
} eNetHarnessRaceRole;

struct tNetRaceHarness
{
  tNetSocket udpSocket;
  struct sockaddr_in proxyAddress;
  uint64 ullNowMs, ullLastFrameMs;
  uint64 ullRandomState;
  uint64 ullBytesSent, ullBytesReceived;
  double dHostTickCredit;
  int iEndpoint, iExpectedClients;
  uint8 byRole, byStarted, byReadySent, byLoadedSent, byAbility;
  uint8 abySuppressOwnState[NET_SIM_MAX_ENDPOINTS];
  tNetChannel *pChannel;
  tNetSessionHost *pSessionHost;
  tNetLobbyHost *pLobbyHost;
  tNetHost *pHost;
  tNetConnection *pClientConnection;
  tNetSessionClient *pSessionClient;
  tNetLobbyClient *pLobbyClient;
  tNetClient *pClient;
};

static uint16 NetHarnessRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetHarnessRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static int NetRaceHarnessFilterPacket(tNetRaceHarness *pHarness,
                                      int iDestination,
                                      const uint8 *pInput, int iLength,
                                      uint8 *pOutput)
{
  int iInputOffset = (int)sizeof(tNetPacketHeader);
  int iOutputOffset = iInputOffset;
  int iMessage;
  uint8 byOutputCount = 0;
  if (!pHarness->abySuppressOwnState[iDestination] ||
      iLength < (int)sizeof(tNetPacketHeader) ||
      NetHarnessRead32(pInput) != NET_PROTOCOL_ID) {
    memcpy(pOutput, pInput, (size_t)iLength);
    return iLength;
  }
  memcpy(pOutput, pInput, sizeof(tNetPacketHeader));
  for (iMessage = 0;
       iMessage < pInput[offsetof(tNetPacketHeader, byMessageCount)];
       ++iMessage) {
    int iMessageLength;
    if (iInputOffset + (int)sizeof(tNetMessageHeader) > iLength)
      return 0;
    iMessageLength = (int)sizeof(tNetMessageHeader) +
        NetHarnessRead16(pInput + iInputOffset + 2);
    if (iInputOffset + iMessageLength > iLength)
      return 0;
    if (pInput[iInputOffset] != NET_MSG_OWN_CAR_STATE) {
      memcpy(pOutput + iOutputOffset, pInput + iInputOffset,
             (size_t)iMessageLength);
      iOutputOffset += iMessageLength;
      ++byOutputCount;
    }
    iInputOffset += iMessageLength;
  }
  if (iInputOffset != iLength)
    return 0;
  pOutput[offsetof(tNetPacketHeader, byMessageCount)] = byOutputCount;
  return iOutputOffset;
}

static int NetRaceHarnessSend(void *pContext, const tNetAddress *pTo,
                              const void *pData, int iLength)
{
  tNetRaceHarness *pHarness = (tNetRaceHarness *)pContext;
  uint8 abDatagram[NET_HARNESS_ENVELOPE_SIZE + NET_MAX_PAYLOAD];
  int iDestination, iFilteredLength;
  if (!pHarness || !pTo || !pData || iLength < 1 ||
      iLength > NET_MAX_PAYLOAD)
    return -1;
  iDestination = pTo->unPort;
  if (iDestination < 0 || iDestination >= NET_SIM_MAX_ENDPOINTS ||
      iDestination == pHarness->iEndpoint)
    return -1;
  abDatagram[0] = 'R';
  abDatagram[1] = 'H';
  abDatagram[2] = (uint8)pHarness->iEndpoint;
  abDatagram[3] = (uint8)iDestination;
  iFilteredLength = NetRaceHarnessFilterPacket(
      pHarness, iDestination, (const uint8 *)pData, iLength,
      abDatagram + NET_HARNESS_ENVELOPE_SIZE);
  if (!iFilteredLength)
    return -1;
  if (sendto(pHarness->udpSocket, (const char *)abDatagram,
             iFilteredLength + NET_HARNESS_ENVELOPE_SIZE, 0,
             (struct sockaddr *)&pHarness->proxyAddress,
             sizeof(pHarness->proxyAddress)) !=
      iFilteredLength + NET_HARNESS_ENVELOPE_SIZE)
    return -1;
  pHarness->ullBytesSent += (uint64)iFilteredLength;
  return iLength;
}

static int NetRaceHarnessReceive(void *pContext, tNetAddress *pFrom,
                                 void *pData, int iCapacity)
{
  tNetRaceHarness *pHarness = (tNetRaceHarness *)pContext;
  uint8 abDatagram[NET_HARNESS_ENVELOPE_SIZE + NET_MAX_PAYLOAD];
  struct sockaddr_in from;
  tNetSocketLength iFromSize = sizeof(from);
  int iLength;
  if (!pHarness || !pData || iCapacity < 1 ||
      !NetSocketReadable(pHarness->udpSocket, 0))
    return 0;
  iLength = recvfrom(pHarness->udpSocket, (char *)abDatagram,
                     sizeof(abDatagram), 0, (struct sockaddr *)&from,
                     &iFromSize);
  if (iLength <= NET_HARNESS_ENVELOPE_SIZE || abDatagram[0] != 'R' ||
      abDatagram[1] != 'H' || abDatagram[2] >= NET_SIM_MAX_ENDPOINTS ||
      abDatagram[3] != pHarness->iEndpoint ||
      iLength - NET_HARNESS_ENVELOPE_SIZE > iCapacity)
    return -1;
  iLength -= NET_HARNESS_ENVELOPE_SIZE;
  memcpy(pData, abDatagram + NET_HARNESS_ENVELOPE_SIZE, (size_t)iLength);
  pHarness->ullBytesReceived += (uint64)iLength;
  if (pFrom) {
    memset(pFrom, 0, sizeof(*pFrom));
    pFrom->abAddress[0] = 127;
    pFrom->abAddress[3] = 1;
    pFrom->unPort = abDatagram[2];
    pFrom->byFamily = NET_ADDR_IPV4;
  }
  return iLength;
}

static uint64 NetRaceHarnessNow(void *pContext)
{
  return ((tNetRaceHarness *)pContext)->ullNowMs;
}

static int NetRaceHarnessRandom(void *pContext, void *pData, int iLength)
{
  tNetRaceHarness *pHarness = (tNetRaceHarness *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  for (iByte = 0; iByte < iLength; ++iByte) {
    pHarness->ullRandomState ^= pHarness->ullRandomState << 13;
    pHarness->ullRandomState ^= pHarness->ullRandomState >> 7;
    pHarness->ullRandomState ^= pHarness->ullRandomState << 17;
    pBytes[iByte] = (uint8)(pHarness->ullRandomState >> 24);
  }
  return 1;
}

static tNetAddress NetRaceHarnessAddress(int iEndpoint)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.abAddress[0] = 127;
  address.abAddress[3] = 1;
  address.unPort = (uint16)iEndpoint;
  address.byFamily = NET_ADDR_IPV4;
  return address;
}

static void NetRaceHarnessClearRole(tNetRaceHarness *pHarness)
{
  if (!pHarness)
    return;
  NetClientDestroy(pHarness->pClient);
  NetLobbyClientDestroy(pHarness->pLobbyClient);
  NetSessionClientDestroy(pHarness->pSessionClient);
  NetHostDestroy(pHarness->pHost);
  NetLobbyHostDestroy(pHarness->pLobbyHost);
  NetSessionHostDestroy(pHarness->pSessionHost);
  NetChannelDestroy(pHarness->pChannel);
  pHarness->pClient = NULL;
  pHarness->pLobbyClient = NULL;
  pHarness->pSessionClient = NULL;
  pHarness->pHost = NULL;
  pHarness->pLobbyHost = NULL;
  pHarness->pSessionHost = NULL;
  pHarness->pChannel = NULL;
  pHarness->byRole = NET_HARNESS_RACE_NONE;
}

static int NetRaceHarnessInitTransport(tNetRaceHarness *pHarness,
                                       int iProxyPort, int iEndpoint)
{
  tNetTransport transport;
  if (iProxyPort < 1 || iProxyPort > 65535 || iEndpoint < 0 ||
      iEndpoint >= NET_SIM_MAX_ENDPOINTS)
    return 0;
  pHarness->proxyAddress = NetSocketAddress(iProxyPort);
  pHarness->iEndpoint = iEndpoint;
  memset(&transport, 0, sizeof(transport));
  transport.pContext = pHarness;
  transport.pSend = NetRaceHarnessSend;
  transport.pReceive = NetRaceHarnessReceive;
  transport.pNowMs = NetRaceHarnessNow;
  pHarness->pChannel = NetChannelCreate(transport);
  return pHarness->pChannel != NULL;
}

static int NetRaceHarnessInitHost(tNetRaceHarness *pHarness, int iProxyPort,
                                  int iEndpoint, int iClients)
{
  tNetSessionConfig config;
  if (pHarness->byRole != NET_HARNESS_RACE_NONE || iEndpoint != 0 ||
      iClients < 1 || iClients > NET_SESSION_MAX_PLAYERS ||
      !NetRaceHarnessInitTransport(pHarness, iProxyPort, iEndpoint))
    return 0;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.unTickRateHz = 36;
  config.bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  config.byMaxPlayers = (uint8)iClients;
  config.byPauseAllowed = 1;
  config.iTrackLoad = 7;
  config.iManualControl = 1;
  config.iCompetitors = 16;
  config.iDamageLevel = 1;
  config.uiRandomSeed = 12345;
  config.uiTrackCRC = 0xe8a10001u;
  memcpy(config.szBuildHash, "e8-s1-harness", 14);
  if (!NetSessionConfigValidate(&config) ||
      !(pHarness->pSessionHost = NetSessionHostCreate(
            pHarness->pChannel, config.byMaxPlayers,
            NetRaceHarnessRandom, pHarness)) ||
      !NetSessionHostSetConfig(pHarness->pSessionHost, &config) ||
      !(pHarness->pLobbyHost = NetLobbyHostCreate(pHarness->pSessionHost)) ||
      !(pHarness->pHost = NetHostCreate(pHarness->pSessionHost,
                                        pHarness->pLobbyHost))) {
    NetRaceHarnessClearRole(pHarness);
    return 0;
  }
  pHarness->byRole = NET_HARNESS_RACE_HOST;
  pHarness->iExpectedClients = iClients;
  pHarness->ullLastFrameMs = pHarness->ullNowMs;
  net_mode = NET_MODE_MODERN;
  return 1;
}

static int NetRaceHarnessInitClient(tNetRaceHarness *pHarness,
                                    int iProxyPort, int iEndpoint)
{
  tNetAddress hostAddress = NetRaceHarnessAddress(0);
  char szName[9];
  if (pHarness->byRole != NET_HARNESS_RACE_NONE || iEndpoint < 1 ||
      !NetRaceHarnessInitTransport(pHarness, iProxyPort, iEndpoint))
    return 0;
  snprintf(szName, sizeof(szName), "Client%d", iEndpoint);
  pHarness->pClientConnection = NetChannelAddConnection(
      pHarness->pChannel, &hostAddress, 0, 0);
  if (!pHarness->pClientConnection ||
      !(pHarness->pSessionClient = NetSessionClientCreate(
            pHarness->pClientConnection, NET_PROTOCOL_VERSION, 1, szName)) ||
      !(pHarness->pLobbyClient =
            NetLobbyClientCreate(pHarness->pSessionClient)) ||
      !(pHarness->pClient = NetClientCreate(pHarness->pSessionClient,
                                            pHarness->pLobbyClient)) ||
      !NetSessionClientStart(pHarness->pSessionClient)) {
    NetRaceHarnessClearRole(pHarness);
    return 0;
  }
  pHarness->byRole = NET_HARNESS_RACE_CLIENT;
  pHarness->ullLastFrameMs = pHarness->ullNowMs;
  net_mode = NET_MODE_MODERN;
  return 1;
}

static tCarInputData NetRaceHarnessInput(tNetRaceHarness *pHarness,
                                         uint32 uiTick, int iMember)
{
  tCarInputData input;
  int iPhase = (int)((uiTick + (uint32)(31 * iMember)) % 120u);
  input.unInput = 0;
  input.unFlags = BUTTON_FLAG_ACCEL;
  if (iPhase >= 90 && iPhase < 96)
    input.unFlags = BUTTON_FLAG_BRAKE;
  if (pHarness->byAbility) {
    input.unFlags |= BUTTON_FLAG_SPECIAL;
    pHarness->byAbility = 0;
  }
  return input;
}

static int NetRaceHarnessFrameHost(tNetRaceHarness *pHarness)
{
  uint64 ullDeltaMs = pHarness->ullNowMs - pHarness->ullLastFrameMs;
  NetChannelPump(pHarness->pChannel);
  NetSessionHostPump(pHarness->pSessionHost);
  NetLobbyHostPump(pHarness->pLobbyHost);
  if (!pHarness->byStarted &&
      NetLobbyHostPlayerCount(pHarness->pLobbyHost) ==
          pHarness->iExpectedClients &&
      NetLobbyHostAllReady(pHarness->pLobbyHost))
    NetLobbyHostStart(pHarness->pLobbyHost, NET_HARNESS_START_TICK);
  NetLobbyHostPump(pHarness->pLobbyHost);
  if (!pHarness->byStarted &&
      NetLobbyHostRaceReleased(pHarness->pLobbyHost)) {
    if (!NetHostBeginRace(pHarness->pHost))
      return 0;
    pHarness->byStarted = 1;
    pHarness->dHostTickCredit = 0.0;
    ullDeltaMs = 0;
  }
  NetHostPump(pHarness->pHost);
  if (pHarness->byStarted && !NetHostPaused(pHarness->pHost)) {
    pHarness->dHostTickCredit += (double)ullDeltaMs * 36.0;
    while (pHarness->dHostTickCredit >= 1000.0) {
      if (!NetHostTick(pHarness->pHost,
                       NetHostNextTick(pHarness->pHost)))
        return 0;
      pHarness->dHostTickCredit -= 1000.0;
    }
  }
  pHarness->ullLastFrameMs = pHarness->ullNowMs;
  return 1;
}

static int NetRaceHarnessFrameClient(tNetRaceHarness *pHarness)
{
  tNetSessionConfig config;
  uint32 uiReleaseTick;
  NetChannelPump(pHarness->pChannel);
  NetSessionClientPump(pHarness->pSessionClient);
  if (!pHarness->byReadySent &&
      NetSessionClientState(pHarness->pSessionClient) == NET_JOIN_ACCEPTED &&
      NetSessionClientGetConfig(pHarness->pSessionClient, &config)) {
    if (!NetLobbyClientSetReady(pHarness->pLobbyClient, 1,
                                config.uiTrackCRC))
      return 0;
    pHarness->byReadySent = 1;
  }
  if (!pHarness->byLoadedSent) {
    uint32 uiStartTick;
    if (NetLobbyClientStartTick(pHarness->pLobbyClient, &uiStartTick)) {
      if (!NetLobbyClientSetRaceLoaded(pHarness->pLobbyClient))
        return 0;
      pHarness->byLoadedSent = 1;
    }
  }
  if (!pHarness->byStarted &&
      NetLobbyClientRaceReleased(pHarness->pLobbyClient, &uiReleaseTick)) {
    if (!NetClientBeginRace(pHarness->pClient))
      return 0;
    pHarness->byStarted = 1;
  }
  if (pHarness->byStarted) {
    NetClientPump(pHarness->pClient);
    while (NetClientTicksDue(pHarness->pClient) > 0) {
      tCarInputData aInputs[NET_INPUT_MAX_LOCAL_PLAYERS];
      int iMembers = NetClientGroup(pHarness->pClient, NULL);
      uint32 uiTick = NetClientCurrentTick(pHarness->pClient) + 1u;
      int iMember;
      if (iMembers < 1 || iMembers > NET_INPUT_MAX_LOCAL_PLAYERS)
        return 0;
      for (iMember = 0; iMember < iMembers; ++iMember)
        aInputs[iMember] = NetRaceHarnessInput(pHarness, uiTick, iMember);
      if (!NetClientTick(pHarness->pClient, aInputs))
        return 0;
    }
  }
  pHarness->ullLastFrameMs = pHarness->ullNowMs;
  return 1;
}

static int NetRaceHarnessFrame(tNetRaceHarness *pHarness, uint64 ullNowMs)
{
  if (pHarness->byRole == NET_HARNESS_RACE_NONE ||
      ullNowMs < pHarness->ullNowMs)
    return 0;
  pHarness->ullNowMs = ullNowMs;
  if (pHarness->byRole == NET_HARNESS_RACE_HOST)
    return NetRaceHarnessFrameHost(pHarness);
  return NetRaceHarnessFrameClient(pHarness);
}

static void NetRaceHarnessStats(tNetRaceHarness *pHarness, char *szReply,
                                int iReplyCapacity)
{
  if (pHarness->byRole == NET_HARNESS_RACE_HOST) {
    int iFinishers = 0, iHumanFinishers = 0;
    uint32 uiFullSnapshots = 0, uiDeltaSnapshots = 0;
    uint64 ullSnapshotBytes = 0;
    uint32 uiTick = pHarness->byStarted ?
        NetHostNextTick(pHarness->pHost) - 1u : 0;
    NetHostResults(pHarness->pHost, &iFinishers, &iHumanFinishers);
    for (int iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
      tNetHostPlayerStats playerStats;
      if (NetHostPlayerStats(pHarness->pHost, (uint8)iPlayer, &playerStats)) {
        uiFullSnapshots += playerStats.uiFullSnapshots;
        uiDeltaSnapshots += playerStats.uiDeltaSnapshots;
        ullSnapshotBytes += playerStats.ullSnapshotBytes;
      }
    }
    snprintf(szReply, (size_t)iReplyCapacity,
        "{\"role\":\"host\",\"running\":%s,\"tick\":%u,"
        "\"game_frame\":%d,\"players\":%d,\"released\":%s,"
        "\"paused\":%d,\"race_state\":%d,\"finishers\":%d,"
        "\"human_finishers\":%d,\"full_snapshots\":%u,"
        "\"delta_snapshots\":%u,\"snapshot_bytes\":%llu,"
        "\"wire_bytes_sent\":%llu,\"wire_bytes_received\":%llu}\n",
        pHarness->byStarted ? "true" : "false", uiTick, game_frame,
        NetLobbyHostPlayerCount(pHarness->pLobbyHost),
        NetLobbyHostRaceReleased(pHarness->pLobbyHost) ? "true" : "false",
        NetHostPaused(pHarness->pHost), NetHostRaceState(pHarness->pHost),
        iFinishers, iHumanFinishers, uiFullSnapshots, uiDeltaSnapshots,
        (unsigned long long)ullSnapshotBytes,
        (unsigned long long)pHarness->ullBytesSent,
        (unsigned long long)pHarness->ullBytesReceived);
  } else if (pHarness->byRole == NET_HARNESS_RACE_CLIENT) {
    tNetClientStats stats;
    uint8 abyCars[NET_INPUT_MAX_LOCAL_PLAYERS] = {NET_LOBBY_NO_PLAYER,
                                                  NET_LOBBY_NO_PLAYER};
    int iGroupCount = pHarness->byStarted ?
        NetClientGroup(pHarness->pClient, abyCars) : 0;
    memset(&stats, 0, sizeof(stats));
    NetClientStats(pHarness->pClient, &stats);
    snprintf(szReply, (size_t)iReplyCapacity,
        "{\"role\":\"client\",\"running\":%s,\"tick\":%u,"
        "\"game_frame\":%d,\"join_state\":%d,\"generation\":%u,"
        "\"group_count\":%d,\"cars\":[%u,%u],"
        "\"snapshots\":%u,\"corrections\":%u,\"deferred\":%u,"
        "\"replay_depth\":%d,\"replay_ticks\":%u,"
        "\"replay_ms_total\":%.9g,\"replay_ms_worst\":%.9g,"
        "\"prediction_mode\":%d,\"prediction_transitions\":%d,"
        "\"time_degraded_ms\":%u,\"recovery\":%d,"
        "\"rejected_messages\":%u,"
        "\"rtt_ms\":%.9g,\"wire_bytes_sent\":%llu,"
        "\"wire_bytes_received\":%llu,\"status\":\"%s\"}\n",
        pHarness->byStarted ? "true" : "false",
        pHarness->byStarted ? NetClientCurrentTick(pHarness->pClient) : 0,
        game_frame, NetSessionClientState(pHarness->pSessionClient),
        NetSessionClientGeneration(pHarness->pSessionClient),
        iGroupCount, abyCars[0], abyCars[1],
        stats.uiSnapshots, stats.uiCorrections, stats.uiDeferredCorrections,
        stats.iReplayDepth, stats.uiReplayTicksTotal,
        (double)stats.fReplayMsTotal, (double)stats.fReplayMsWorst,
        stats.iPredictionMode, stats.iPredictionTransitions,
        stats.uiTimeDegradedMs, NetClientRecoveryState(pHarness->pClient),
        stats.uiRejectedMessages, (double)stats.fRttMs,
        (unsigned long long)pHarness->ullBytesSent,
        (unsigned long long)pHarness->ullBytesReceived,
        NetClientStatus(pHarness->pClient));
  } else {
    snprintf(szReply, (size_t)iReplyCapacity,
             "{\"role\":\"none\",\"running\":false}\n");
  }
}

tNetRaceHarness *NetRaceHarnessCreate(tNetSocket udpSocket)
{
  tNetRaceHarness *pHarness =
      (tNetRaceHarness *)calloc(1, sizeof(*pHarness));
  if (pHarness) {
    pHarness->udpSocket = udpSocket;
    pHarness->ullRandomState = 0xe8a15eed12345678ull;
  }
  return pHarness;
}

void NetRaceHarnessDestroy(tNetRaceHarness *pHarness)
{
  if (!pHarness)
    return;
  NetRaceHarnessClearRole(pHarness);
  free(pHarness);
}

int NetRaceHarnessCommand(tNetRaceHarness *pHarness, const char *szLine,
                          char *szReply, int iReplyCapacity)
{
  unsigned long long ullNowMs;
  int iProxyPort, iEndpoint, iClients, iCar, iValue, iTarget;
  float fX, fY, fZ;
  if (!pHarness || !szLine || !szReply || iReplyCapacity < 1)
    return 0;
  if (sscanf(szLine, "race host %d %d %d", &iProxyPort, &iEndpoint,
             &iClients) == 3) {
    snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":%s}\n",
        NetRaceHarnessInitHost(pHarness, iProxyPort, iEndpoint, iClients) ?
            "true" : "false");
    return 1;
  }
  if (sscanf(szLine, "race client %d %d", &iProxyPort, &iEndpoint) == 2) {
    snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":%s}\n",
        NetRaceHarnessInitClient(pHarness, iProxyPort, iEndpoint) ?
            "true" : "false");
    return 1;
  }
  if (sscanf(szLine, "frame %llu", &ullNowMs) == 1) {
    if (!NetRaceHarnessFrame(pHarness, (uint64)ullNowMs))
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"race frame failed\"}\n");
    else
      NetRaceHarnessStats(pHarness, szReply, iReplyCapacity);
    return 1;
  }
  if (!strcmp(szLine, "race_stats") ||
      (!strcmp(szLine, "stats") &&
       pHarness->byRole != NET_HARNESS_RACE_NONE)) {
    NetRaceHarnessStats(pHarness, szReply, iReplyCapacity);
    return 1;
  }
  if (sscanf(szLine, "disturb %d %f %f %f", &iCar, &fX, &fY, &fZ) == 4) {
    if (iCar < 0 || iCar >= numcars || !isfinite(fX) || !isfinite(fY) ||
        !isfinite(fZ))
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"invalid car disturbance\"}\n");
    else {
      Car[iCar].pos.fX += fX;
      Car[iCar].pos.fY += fY;
      Car[iCar].pos.fZ += fZ;
      snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    }
    return 1;
  }
  if (sscanf(szLine, "disturb_engine %d %f", &iCar, &fX) == 2) {
    if (iCar < 0 || iCar >= numcars || !isfinite(fX))
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"invalid engine disturbance\"}\n");
    else {
      Car[iCar].fBaseSpeed += fX;
      snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    }
    return 1;
  }
  if (sscanf(szLine, "disturb_lap_time %d %f", &iCar, &fX) == 2) {
    if (iCar < 0 || iCar >= numcars || !isfinite(fX))
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"invalid lap-time disturbance\"}\n");
    else {
      Car[iCar].fRunningLapTime += fX;
      snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    }
    return 1;
  }
  if (sscanf(szLine, "pause %d", &iValue) == 1 &&
      pHarness->byRole == NET_HARNESS_RACE_HOST) {
    snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":%s}\n",
        NetHostSetPaused(pHarness->pHost, iValue != 0) ? "true" : "false");
    return 1;
  }
  if (sscanf(szLine, "strategy %d %d", &iTarget, &iValue) == 2 &&
      pHarness->byRole == NET_HARNESS_RACE_CLIENT) {
    snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":%s}\n",
        NetLobbyClientSendStrategy(pHarness->pLobbyClient,
            iTarget < 0 ? NET_LOBBY_NO_PLAYER : (uint8)iTarget,
            (uint8)iValue) ? "true" : "false");
    return 1;
  }
  if (!strcmp(szLine, "ability") &&
      pHarness->byRole == NET_HARNESS_RACE_CLIENT) {
    pHarness->byAbility = 1;
    snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    return 1;
  }
  if (sscanf(szLine, "suppress_own_car_state %d %d", &iEndpoint,
             &iValue) == 2 &&
      pHarness->byRole == NET_HARNESS_RACE_HOST) {
    if (iEndpoint < 1 || iEndpoint >= NET_SIM_MAX_ENDPOINTS ||
        (iValue != 0 && iValue != 1))
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"invalid suppression target\"}\n");
    else {
      pHarness->abySuppressOwnState[iEndpoint] = (uint8)iValue;
      snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    }
    return 1;
  }
  if (!strcmp(szLine, "rejoin") &&
      pHarness->byRole == NET_HARNESS_RACE_CLIENT) {
    tNetAddress hostAddress = NetRaceHarnessAddress(0);
    tNetConnection *pConnection = NetChannelAddConnection(
        pHarness->pChannel, &hostAddress,
        NetSessionClientToken(pHarness->pSessionClient),
        (uint8)(NetSessionClientGeneration(pHarness->pSessionClient) + 1u));
    if (pConnection && NetClientBeginRejoin(pHarness->pClient, pConnection)) {
      pHarness->pClientConnection = pConnection;
      snprintf(szReply, (size_t)iReplyCapacity, "{\"ok\":true}\n");
    } else {
      snprintf(szReply, (size_t)iReplyCapacity,
               "{\"error\":\"rejoin failed\"}\n");
    }
    return 1;
  }
  return 0;
}
