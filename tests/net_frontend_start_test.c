/* Real frontend sessions in separate processes, without a window/audio device.
   Keep the production ordering: roster, schedule, prepare (AllocateCars),
   fade-frame pumps, loadtrack/placecars, initcarview, load barrier, render.
   In particular do NOT replace player1_car with ViewType before camera init:
   that would hide the loading-time roster overwrite this test guards. */
#include "3d.h"
#include "car.h"
#include "control.h"
#include "colision.h"
#include "frontend.h"
#include "func2.h"
#include "func3.h"
#include "function.h"
#include "loadtrak.h"
#include "moving.h"
#include "net_channel.h"
#include "net_config.h"
#include "net_discovery.h"
#include "net_rendezvous.h"
#include "net_session.h"
#include "net_lobby.h"
#include "net_frontend_lobby.h"
#include "net_types.h"
#include "network.h"
#include "rollercomms.h"
#include "roller.h"
#include "replay.h"
#include "view.h"
#include "transfrm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, \
          NetFrontendLobbyStatus()); exit(1); \
} } while (0)

static int aiCars[2];
static int iLocalPlayers;
static int iMappingFailures;
static float fWorstCameraError;

static void Pump(void)
{
  NetPump();
  NetFrontendPump();
  SDL_Delay(1);
}

static void CheckMapping(void)
{
  if (player1_car != aiCars[0] || ViewType[0] != aiCars[0] ||
      (iLocalPlayers == 2 &&
       (player2_car != aiCars[1] || ViewType[1] != aiCars[1]))) {
    if (!iMappingFailures)
      fprintf(stderr, "loading overwrote allocated cars: expected %d/%d, "
              "players %d/%d, views %d/%d\n", aiCars[0], aiCars[1],
              player1_car, player2_car, ViewType[0], ViewType[1]);
    ++iMappingFailures;
  }
}

static void CheckCamera(int iPlayer)
{
  /* On the stationary starting grid, the chase camera must sit one chase
     distance from THIS car's pull point, towards its initial rear seed.
     Account for the car's small pitch/roll on sloped or banked starting grids.
     Checking all axes detects wrong-car seeds even on parallel grid rows. */
  tCar *pCar = &Car[aiCars[iPlayer]];
  CHECK(pCar->nCurrChunk >= 0 && pCar->nCurrChunk < TRAK_LEN);
  tData *pTrack = &localdata[pCar->nCurrChunk];
  float fUpX = -tcos[pCar->nYaw] * tsin[pCar->nPitch] * tcos[pCar->nRoll] -
      tsin[pCar->nYaw] * tsin[pCar->nRoll];
  float fUpY = -tsin[pCar->nYaw] * tsin[pCar->nPitch] * tcos[pCar->nRoll] +
      tcos[pCar->nYaw] * tsin[pCar->nRoll];
  float fUpZ = tcos[pCar->nPitch] * tcos[pCar->nRoll];
  float fRearX = -2.0f * CHASE_DIST[iPlayer] - PULLZ[iPlayer] * fUpX;
  float fRearY = -PULLZ[iPlayer] * fUpY;
  float fRearZ = PULLZ[iPlayer] * (1.0f - fUpZ);
  float fRatio = CHASE_DIST[iPlayer] /
      sqrtf(fRearX * fRearX + fRearY * fRearY + fRearZ * fRearZ);
  float fX = pCar->pos.fX + PULLZ[iPlayer] * fUpX + fRearX * fRatio;
  float fY = pCar->pos.fY + PULLZ[iPlayer] * fUpY + fRearY * fRatio;
  float fZ = pCar->pos.fZ + PULLZ[iPlayer] * fUpZ + fRearZ * fRatio;
  float fExpectedX = pTrack->pointAy[0].fX * fX +
      pTrack->pointAy[0].fY * fY + pTrack->pointAy[0].fZ * fZ - pTrack->pointAy[3].fX;
  float fExpectedY = pTrack->pointAy[1].fX * fX +
      pTrack->pointAy[1].fY * fY + pTrack->pointAy[1].fZ * fZ - pTrack->pointAy[3].fY;
  float fExpectedZ = pTrack->pointAy[2].fX * fX +
      pTrack->pointAy[2].fY * fY + pTrack->pointAy[2].fZ * fZ - pTrack->pointAy[3].fZ;
  newchaseview(ViewType[iPlayer], iPlayer);
  /* calculatetransform adds the normal render/lens offset after the chase
     calculation; compare the unshifted chase position, not that offset. */
  float fDeltaX = viewx - (vk1 * DDX + vk2 * DDY + vk3 * DDZ) - fExpectedX;
  float fDeltaY = viewy - (vk4 * DDX + vk5 * DDY + vk6 * DDZ) - fExpectedY;
  float fDeltaZ = viewz - (vk7 * DDX + vk8 * DDY + vk9 * DDZ) - fExpectedZ;
  float fError = sqrtf(fDeltaX * fDeltaX + fDeltaY * fDeltaY + fDeltaZ * fDeltaZ);
  CHECK(isfinite(fError));
  if (fError > fWorstCameraError)
    fWorstCameraError = fError;
}

/* Observe real UDP traffic at a configured directory while switching server
   types. LAN must remain discoverable and join directly even with that address
   saved. Query the host over loopback so this does not depend on broadcast
   loopback support on the machine running the test. */
static void NetTestServerType(uint16 unLocalPort)
{
  tNetTransportUdp *pRvzUdp = NetTransportUdpCreate(0);
  tNetTransportUdp *pLanUdp = NetTransportUdpCreate(0);
  tNetAddress frontend;
  tNetSessionConfigOptions options;
  uint8 abLanPacket[sizeof(tNetLanAdvertisement)] = {0};
  CHECK(pRvzUdp && pLanUdp);
  tNetTransport rvzTransport = NetTransportUdpEndpoint(pRvzUdp);
  tNetTransport lanTransport = NetTransportUdpEndpoint(pLanUdp);
  CHECK(NetFrontendServerType() == NET_SERVER_PUBLIC);
  CHECK(!NetFrontendSetServerType((eNetServerType)99));
  CHECK(NetFrontendServerType() == NET_SERVER_PUBLIC);
  CHECK(NetFrontendSetRendezvous("127.0.0.1", NetTransportUdpPort(pRvzUdp)));
  CHECK(NetAddressParse(&frontend, "127.0.0.1", unLocalPort));
  NetFrontendSetLocalPort(unLocalPort);
  NetSessionConfigOptionsDefault(&options);
  net_mode = NET_MODE_MODERN;
  abLanPacket[0] = (uint8)NET_LAN_PROTOCOL_ID;
  abLanPacket[1] = (uint8)(NET_LAN_PROTOCOL_ID >> 8);
  abLanPacket[2] = (uint8)(NET_LAN_PROTOCOL_ID >> 16);
  abLanPacket[3] = (uint8)(NET_LAN_PROTOCOL_ID >> 24);
  abLanPacket[4] = NET_LAN_PROTOCOL_VERSION;
  for (int iHost = 1; iHost >= 0; --iHost) {
    for (int iCase = 0; iCase < 3; ++iCase) {
      int iLan = iCase == 1;
      int iSawDirectory = 0, iSawLan = 0, iSelected = 0;
      int iLanLength;
      uint64 ullDeadline = SDL_GetTicks() + 1200;
      CHECK(NetFrontendSetServerType(iLan ? NET_SERVER_LAN : NET_SERVER_PUBLIC));
      network_slot = iHost ? 0 : -1;
      CHECK(NetFrontendOpen());
      CHECK(!strcmp(NetFrontendRendezvous(), "127.0.0.1"));
      if (iLan) {
        if (iHost) {
          abLanPacket[5] = NET_LAN_MSG_QUERY;
          iLanLength = sizeof(tNetLanQuery);
        } else {
          tRvzSessionInfo info = {0};
          info.uiSessionId = 123;
          info.unPort = NetTransportUdpPort(pLanUdp);
          info.unTickRateHz = 36;
          info.byPlayers = 1;
          info.byMaxPlayers = 16;
          snprintf(info.szName, sizeof(info.szName), "LAN HOST");
          snprintf(info.szTrack, sizeof(info.szTrack), "TRACK5");
          memcpy(info.szBuildHash, options.szBuildHash, sizeof(info.szBuildHash));
          abLanPacket[5] = NET_LAN_MSG_ADVERTISE;
          NetRendezvousEncodeSessionInfo(abLanPacket + 8, &info);
          iLanLength = sizeof(abLanPacket);
        }
        CHECK(lanTransport.pSend(lanTransport.pContext, &frontend,
            abLanPacket, iLanLength) == iLanLength);
        /* Updating the preference must not change the already-open session. */
        CHECK(NetFrontendSetServerType(NET_SERVER_PUBLIC));
      }
      while (SDL_GetTicks() < ullDeadline) {
        uint8 abPacket[NET_MAX_PAYLOAD];
        tNetAddress peer;
        int iLength;
        Pump();
        while ((iLength = rvzTransport.pReceive(rvzTransport.pContext, &peer,
                                                abPacket, sizeof(abPacket))) > 0) {
          tNetRendezvousPacket packet;
          CHECK(!iLan);
          CHECK(NetRendezvousParsePacket(abPacket, iLength, &packet));
          CHECK(packet.byType == (iHost ? NET_RVZ_MSG_REGISTER : NET_RVZ_MSG_LIST));
          iSawDirectory = 1;
        }
        if (!iLan && iSawDirectory)
          break;
        if (iLan && !iHost && !iSelected && NetFrontendBrowserSessionCount()) {
          tRvzSessionInfo info;
          CHECK(NetFrontendBrowserSessionCount() == 1);
          CHECK(NetFrontendBrowserSession(0, &info));
          CHECK(!strcmp(info.szName, "LAN HOST"));
          CHECK(NetFrontendBrowserSelect(info.uiSessionId));
          iSelected = 1;
        }
        while ((iLength = lanTransport.pReceive(lanTransport.pContext, &peer,
                                                abPacket, sizeof(abPacket))) > 0) {
          CHECK(iLan);
          if (iHost) {
            tRvzSessionInfo info;
            CHECK(iLength == sizeof(tNetLanAdvertisement));
            CHECK(abPacket[5] == NET_LAN_MSG_ADVERTISE);
            NetRendezvousDecodeSessionInfo(&info, abPacket + 8);
            CHECK(info.uiSessionId && info.unPort == unLocalPort);
            CHECK(!memcmp(info.szBuildHash, options.szBuildHash, sizeof(info.szBuildHash)));
          } else {
            CHECK(iLength >= 28 && abPacket[22] == NET_MSG_JOIN_REQUEST);
          }
          iSawLan = 1;
        }
      }
      CHECK(iLan ? iSawLan && !iSawDirectory : iSawDirectory);
      NetFrontendClose();
      CHECK(NetFrontendServerType() == NET_SERVER_PUBLIC);
      CHECK(!strcmp(NetFrontendRendezvous(), "127.0.0.1"));
    }
  }
  NetTransportUdpDestroy(pLanUdp);
  NetTransportUdpDestroy(pRvzUdp);
  puts("Frontend server type: Public default, LAN discovery/direct join without directory traffic, Public restored");
}

/* A local wire-level directory fixture exercises the production frontend
   cache and selection without creating another simulation world. */
static void NetTestBrowser(uint16 unClientPort)
{
  tNetTransportUdp *pUdp = NetTransportUdpCreate(0);
  CHECK(pUdp);
  tNetTransport transport = NetTransportUdpEndpoint(pUdp);
  CHECK(NetFrontendSetRendezvous("localhost", NetTransportUdpPort(pUdp)));
  NetFrontendSetLocalPort(unClientPort);
  net_mode = NET_MODE_MODERN;
  network_slot = -1;
  CHECK(NetFrontendOpen());
  int iSelected = 0, iSawPunch = 0;
  int iBadPageSent = 0, iPageDropped = 0;
  uint64 ullListedMs = 0, ullDeadline = SDL_GetTicks() + 5000;
  while (!iSawPunch) {
    uint8 abPacket[NET_MAX_PAYLOAD];
    tNetAddress peer;
    int iLength;
    CHECK(SDL_GetTicks() < ullDeadline);
    Pump();
    while ((iLength = transport.pReceive(transport.pContext, &peer,
                                         abPacket, sizeof(abPacket))) > 0) {
      tNetRendezvousPacket packet;
      CHECK(NetRendezvousParsePacket(abPacket, iLength, &packet));
      if (packet.byType == NET_RVZ_MSG_LIST) {
        int iPage = packet.pPayload[0];
        if (iPage == 1 && !iPageDropped) {
          iPageDropped = 1;
          continue;
        }
        int iCount = iPage == 0 ? 12 : 5;
        uint8 abPayload[sizeof(tRvzListPageHeader) + 12 * sizeof(tRvzSessionInfo)] = {0};
        abPayload[0] = (uint8)iPage;
        abPayload[2] = 2;
        abPayload[4] = 17;
        abPayload[6] = (uint8)iCount;
        abPayload[7] = (uint8)(iPage == 0);
        for (int iRow = 0; iRow < iCount; ++iRow) {
          tRvzSessionInfo info = {0};
          info.uiSessionId = (uint32)(100 + iPage * 12 + iRow);
          info.unPort = 7777;
          info.unTickRateHz = 36;
          info.byPlayers = 1;
          info.byMaxPlayers = 16;
          snprintf(info.szName, sizeof(info.szName), "SERVER %u", info.uiSessionId);
          snprintf(info.szTrack, sizeof(info.szTrack), "TRACK3");
          memcpy(info.szBuildHash, packet.pPayload + 4, sizeof(info.szBuildHash));
          NetRendezvousEncodeSessionInfo(abPayload + sizeof(tRvzListPageHeader) +
              iRow * sizeof(tRvzSessionInfo), &info);
        }
        if (!iBadPageSent) {
          /* A server name with no terminator must reject the entire page. */
          memset(abPayload + sizeof(tRvzListPageHeader) + 11, 'X', 32);
          iBadPageSent = 1;
        }
        iLength = NetRendezvousBuildPacket(abPacket, sizeof(abPacket),
            packet.unSequence, 0, NET_RVZ_MSG_LIST_PAGE, abPayload,
            (uint16)(sizeof(tRvzListPageHeader) + iCount * sizeof(tRvzSessionInfo)));
        CHECK(transport.pSend(transport.pContext, &peer, abPacket, iLength) == iLength);
      } else if (packet.byType == NET_RVZ_MSG_PUNCH_REQUEST) {
        CHECK(iSelected); /* Browsing alone must never punch the first server. */
        CHECK(packet.pPayload[0] == 116 && !packet.pPayload[1] &&
              !packet.pPayload[2] && !packet.pPayload[3]);
        iSawPunch = 1;
      } else {
        CHECK(0);
      }
    }
    CHECK(!NetFrontendLobbyJoined());
    if (NetFrontendBrowserSessionCount() == 17) {
      tRvzSessionInfo info;
      CHECK(NetFrontendBrowserSession(16, &info));
      CHECK(info.uiSessionId == 116);
      if (!ullListedMs) ullListedMs = SDL_GetTicks();
      if (!iSelected && SDL_GetTicks() - ullListedMs >= 150) {
        CHECK(!NetFrontendBrowserSelect(999));
        CHECK(NetFrontendBrowserSelect(info.uiSessionId));
        iSelected = 1;
      }
    }
  }
  NetFrontendClose();
  CHECK(NetFrontendBrowserSessionCount() == 0);
  CHECK(NetFrontendOpen());
  CHECK(!NetFrontendLobbyJoined());
  NetFrontendClose();
  NetTransportUdpDestroy(pUdp);
  CHECK(iBadPageSent && iPageDropped);
  puts("Frontend browser: 17 entries, explicit last-row selection, malformed/lost page recovery");
}

typedef struct {
  tNetTransport transport;
  int iHost, iAllowPunch, iDropOffer, iDropRelay;
  int iDroppedOffers, iRelayRequests;
  int iLoopbackCandidate, iInjectedCandidates;
} tNetTestInternetTransport;

static int NetTestInternetSend(void *pContext, const tNetAddress *pPeer,
                                const void *pData, int iLength)
{
  tNetTestInternetTransport *pTest = pContext;
  tNetRendezvousPacket packet;
  if (pTest->iLoopbackCandidate &&
      NetRendezvousParsePacket(pData, iLength, &packet) &&
      packet.byType == NET_RVZ_MSG_PUNCH_ANSWER) {
    uint8 abPayload[sizeof(tRvzCandidateSet)], abPacket[NET_MAX_PAYLOAD];
    tNetAddress loopback = {0};
    CHECK(packet.unPayloadLength == sizeof(abPayload));
    CHECK(packet.pPayload[12] < NET_RVZ_MAX_CANDIDATES);
    memcpy(abPayload, packet.pPayload, sizeof(abPayload));
    memmove(abPayload + 16 + sizeof(tRvzCandidate), abPayload + 16,
            abPayload[12] * sizeof(tRvzCandidate));
    ++abPayload[12];
    /* An older remote host advertises its loopback on the shared game port.
       On the client this points back to the client's own real UDP socket. */
    CHECK(NetAddressParse(&loopback,
        pTest->iLoopbackCandidate == 2 ? "127.0.0.1" : "::1", pPeer->unPort));
    if (pTest->iLoopbackCandidate == 3) {
      /* Numeric parsing normally canonicalizes mapped IPv6 into IPv4;
         encode the mapped form explicitly to exercise the received wire. */
      loopback.abAddress[10] = loopback.abAddress[11] = 255;
      loopback.abAddress[12] = 127;
    }
    NetRendezvousEncodeCandidate(abPayload + 16, &loopback);
    int iSentLength = NetRendezvousBuildPacket(abPacket, sizeof(abPacket),
        packet.unSequence, packet.ullToken, packet.byType,
        abPayload, sizeof(abPayload));
    CHECK(iSentLength == iLength);
    ++pTest->iInjectedCandidates;
    return pTest->transport.pSend(pTest->transport.pContext, pPeer,
                                   abPacket, iSentLength);
  }
  if (NetRendezvousParsePacket(pData, iLength, &packet) &&
      packet.byType == NET_RVZ_MSG_RELAY_OFFER && pTest->iDropOffer) {
    pTest->iDropOffer = 0;
    ++pTest->iDroppedOffers;
    return iLength;
  }
  return pTest->transport.pSend(pTest->transport.pContext, pPeer, pData, iLength);
}

static int NetTestInternetReceive(void *pContext, tNetAddress *pPeer,
                                   void *pData, int iCapacity)
{
  tNetTestInternetTransport *pTest = pContext;
  int iLength;
  while ((iLength = pTest->transport.pReceive(pTest->transport.pContext,
                                               pPeer, pData, iCapacity)) > 0) {
    const uint8 *pBytes = pData;
    uint32 uiProtocol = iLength >= 4 ? (uint32)pBytes[0] |
        ((uint32)pBytes[1] << 8) | ((uint32)pBytes[2] << 16) |
        ((uint32)pBytes[3] << 24) : 0;
    tNetRendezvousPacket packet;
    /* Direct game traffic is unreachable. Relay envelopes still arrive.
       One case lets probes succeed before blackholing the game handshake. */
    if (pTest->iHost && (uiProtocol == NET_PROTOCOL_ID ||
        (!pTest->iAllowPunch && uiProtocol == NET_PUNCH_PROTOCOL_ID)))
      continue;
    if (NetRendezvousParsePacket(pData, iLength, &packet) &&
        packet.byType == NET_RVZ_MSG_RELAY_REQUEST) {
      ++pTest->iRelayRequests;
      if (pTest->iDropRelay)
        continue;
    }
    return iLength;
  }
  return iLength;
}

static uint64 NetTestInternetNow(void *pContext)
{
  tNetTestInternetTransport *pTest = pContext;
  return pTest->transport.pNowMs(pTest->transport.pContext);
}

static tNetTransport NetTestInternetEndpoint(tNetTestInternetTransport *pTest)
{
  tNetTransport transport = {pTest, NetTestInternetSend,
                             NetTestInternetReceive, NetTestInternetNow};
  return transport;
}

static void NetTestInternetJoin(uint16 unClientPort, int iCase,
                                 const char *szTrack)
{
  tNetTransportUdp *pHostUdp = NetTransportUdpCreate(0);
  tNetTransportUdp *pRvzUdp = NetTransportUdpCreate(0);
  tNetTestInternetTransport hostTransport = {0}, rvzTransport = {0};
  tNetAddress rendezvous;
  tRvzSessionInfo info = {0};
  tNetSessionConfigOptions options;
  tNetSessionConfig config;
  uint32 uiSessionId = 0;
  int iSelected = 0;
  CHECK(pHostUdp && pRvzUdp);
  hostTransport.transport = NetTransportUdpEndpoint(pHostUdp);
  hostTransport.iHost = 1;
  hostTransport.iAllowPunch = iCase == 1;
  rvzTransport.transport = NetTransportUdpEndpoint(pRvzUdp);
  rvzTransport.iDropOffer = iCase == 0;
  rvzTransport.iDropRelay = iCase == 2;
  rvzTransport.iLoopbackCandidate = iCase >= 3 ? iCase - 2 : 0;
  CHECK(NetAddressParse(&rendezvous, "127.0.0.1", NetTransportUdpPort(pRvzUdp)));
  tNetChannel *pHostChannel = NetChannelCreate(NetTestInternetEndpoint(&hostTransport));
  tNetRendezvous *pRvz = NetRendezvousCreate(NetTestInternetEndpoint(&rvzTransport),
                                           NetPlatformRandomBytes, NULL);
  tNetDiscovery *pDiscovery = NetDiscoveryCreate(pHostChannel, &rendezvous,
                                                 NetPlatformRandomBytes, NULL);
  tNetSessionHost *pHost = NetSessionHostCreate(pHostChannel, 16,
                                               NetPlatformRandomBytes, NULL);
  CHECK(pHostChannel && pRvz && pDiscovery && pHost);
  names[3] = (char *)szTrack;
  TrackLoad = 3;
  competitors = 16;
  level = damage_level = manual_control[0] = 1;
  NetSessionConfigOptionsDefault(&options);
  CHECK(NetSessionConfigBuild(&config, &options));
  CHECK(NetSessionHostSetConfig(pHost, &config));
  tNetLobbyHost *pLobby = NetLobbyHostCreate(pHost);
  CHECK(pLobby);
  memcpy(info.szBuildHash, options.szBuildHash, sizeof(info.szBuildHash));
  snprintf(info.szName, sizeof(info.szName), "INTERNET JOIN TEST");
  snprintf(info.szTrack, sizeof(info.szTrack), "TRACK5");
  info.unTickRateHz = 36;
  info.byPlayers = 1;
  info.byMaxPlayers = 16;
  CHECK(NetDiscoveryHostStart(pDiscovery, &info));
  CHECK(NetFrontendSetRendezvous("127.0.0.1", rendezvous.unPort));
  NetFrontendSetLocalPort(unClientPort);
  net_mode = NET_MODE_MODERN;
  network_slot = -1;
  player1_car = 0;
  player2_car = -1;
  Players_Cars[0] = 0;
  manual_control[0] = 1;
  CHECK(NetFrontendOpen());
  uint64 ullDeadline = SDL_GetTicks() + 18000;
  while ((!NetFrontendLobbyJoined() || !NetLobbyHostAllReady(pLobby)) &&
         !strstr(NetFrontendLobbyStatus(), "TIMED OUT")) {
    CHECK(SDL_GetTicks() < ullDeadline);
    NetRendezvousPump(pRvz);
    Pump();
    NetDiscoveryPump(pDiscovery);
    NetSessionHostPump(pHost);
    NetLobbyHostPump(pLobby);
    if (!iSelected && NetFrontendBrowserSessionCount()) {
      CHECK(NetDiscoveryHostRegistered(pDiscovery, &uiSessionId));
      CHECK(NetFrontendBrowserSelect(uiSessionId));
      iSelected = 1;
    }
  }
  CHECK(iSelected && rvzTransport.iRelayRequests);
  if (iCase == 2)
    CHECK(!NetFrontendLobbyJoined() && !NetFrontendIsOpen());
  else
    CHECK(NetFrontendLobbyJoined() && NetLobbyHostAllReady(pLobby));
  if (iCase == 0)
    CHECK(rvzTransport.iDroppedOffers == 1 && rvzTransport.iRelayRequests >= 2);
  if (iCase >= 3)
    CHECK(rvzTransport.iInjectedCandidates);
  /* Only the joining client requests a relay; the host must not relay to itself. */
  CHECK(NetDiscoveryPunchState(pDiscovery, NULL) != NET_PUNCH_RELAY_SUCCEEDED);
  NetFrontendClose();
  NetLobbyHostDestroy(pLobby);
  NetSessionHostDestroy(pHost);
  NetDiscoveryDestroy(pDiscovery);
  NetRendezvousDestroy(pRvz);
  NetChannelDestroy(pHostChannel);
  NetTransportUdpDestroy(pHostUdp);
  NetTransportUdpDestroy(pRvzUdp);
  printf("Frontend Internet join case %d passed\n", iCase);
}

int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--server-type")) {
    NetTestServerType((uint16)atoi(argv[2]));
    return 0;
  }
  if (argc == 5 && !strcmp(argv[1], "--internet-join")) {
    NetTestInternetJoin((uint16)atoi(argv[2]), atoi(argv[3]), argv[4]);
    return 0;
  }
  if (argc == 3 && !strcmp(argv[1], "--browser")) {
    NetTestBrowser((uint16)atoi(argv[2]));
    return 0;
  }
  /* track assets role local-port host-port design locals competitors seed delay
     [--lan-discovery] */
  CHECK(argc == 11 || (argc == 12 && !strcmp(argv[11], "--lan-discovery")));
  int iLanDiscovery = argc == 12;
  int iHost = !strcmp(argv[3], "host");
  int iDesign = atoi(argv[6]);
  iLocalPlayers = atoi(argv[7]);
  CHECK(iLocalPlayers == 1 || iLocalPlayers == 2);
  init();
  InitCarStructs();
  net_mode = NET_MODE_MODERN;
  network_slot = iHost ? 0 : -1;
  names[3] = argv[1];
  TrackLoad = 3;
  game_type = replaytype = intro = quit_game = cheat_mode = 0;
  level = damage_level = 1;
  competitors = atoi(argv[8]);
  random_seed = atoi(argv[9]);
  ROLLERsrand((unsigned int)random_seed);
  player1_car = 0;
  player2_car = iLocalPlayers == 2 ? 1 : -1;
  Players_Cars[0] = (iDesign + (iLocalPlayers == 2 ? 1 : 4)) % 8;
  Players_Cars[1] = iLocalPlayers == 2 ? iDesign : (iDesign + 1) % 8;
  manual_control[0] = manual_control[1] = 1;
  snprintf(my_name, sizeof(my_name), "%s", iHost ? "HOST" : "CLIENT");
  NetFrontendSetLocalPort((uint16)atoi(argv[4]));
  if (iHost || iLanDiscovery) {
    CHECK(NetFrontendServerType() == NET_SERVER_PUBLIC);
    CHECK(NetFrontendSetServerType(NET_SERVER_LAN));
    /* A nonempty configured address must not prevent a complete LAN game. */
    CHECK(NetFrontendSetRendezvous("unused.invalid:7778", 7778));
  } else {
    CHECK(NetFrontendSetRendezvous("", 7778));
  }
  CHECK(NetFrontendSetLocalPlayers(iLocalPlayers));
  if (!iHost && !iLanDiscovery)
    CHECK(NetFrontendSetPeer("127.0.0.1", (uint16)atoi(argv[5])));
  CHECK(NetFrontendOpen());
  if (iHost) {
    CHECK(NetFrontendLobbyBegin());
  } else {
    tRvzSessionInfo info;
    if (iLanDiscovery) {
      uint64 ullDiscoveryDeadline = SDL_GetTicks() + 5000;
      while (!NetFrontendBrowserSessionCount()) {
        CHECK(SDL_GetTicks() < ullDiscoveryDeadline);
        Pump();
        CHECK(!NetFrontendLobbyJoined());
      }
    }
    CHECK(NetFrontendBrowserSessionCount() == 1);
    CHECK(NetFrontendBrowserSession(0, &info));
    if (iLanDiscovery) {
      CHECK(!strcmp(info.szName, "HOST'S GAME"));
      CHECK(info.unPort == (uint16)atoi(argv[5]));
    }
    for (int iFrame = 0; iFrame < 100; ++iFrame) {
      Pump();
      CHECK(!NetFrontendLobbyJoined());
    }
    CHECK(NetFrontendBrowserSelect(info.uiSessionId));
  }
  uint64 ullDeadline = SDL_GetTicks() + 15000;
  uint32 uiStartTick = 0;
  int iRequested = 0;
  int iChatSent = 0, iChatReceived = 0;
  int iCarChangeSent = 0;
  while (!NetFrontendLobbyStartTick(&uiStartTick)) {
    CHECK(SDL_GetTicks() < ullDeadline);
    Pump();
    if (NetFrontendLobbyJoined() && players == 2 * iLocalPlayers &&
        players_waiting == players && !iCarChangeSent) {
      Players_Cars[player1_car] = iDesign;
      if (iLocalPlayers == 2)
        Players_Cars[player2_car] = (iDesign + 1) % 8;
      NetFrontendLobbyUpdatePlayerInfo();
      iCarChangeSent = 1;
      continue;
    }
    if (iCarChangeSent && Players_Cars[player1_car] == iDesign && !iChatSent) {
      /* Exercise the existing composer globals and received-message seam.
         Clients must be able to address the host (display player zero). */
      if (iHost) {
        CHECK(!strcmp(player_names[0], "HOST"));
        CHECK(!strcmp(player_names[iLocalPlayers], "CLIENT"));
      }
      snprintf(send_mes_buf, sizeof(send_mes_buf), "HELLO FROM %s", iHost ? "HOST" : "CLIENT");
      send_message_to = iHost ? 0 : 1;
      iChatSent = 1;
    }
    if (rec_status) {
      CHECK(!strcmp(rec_mes_name, iHost ? "CLIENT" : "HOST"));
      CHECK(!strcmp(rec_mes_buf, iHost ? "HELLO FROM CLIENT" : "HELLO FROM HOST"));
      rec_status = 0;
      iChatReceived = 1;
    }
    if (iHost && !iRequested && players == 2 * iLocalPlayers &&
        iChatSent && iChatReceived && players_waiting == players && NetFrontendLobbyCanStart()) {
      CHECK(NetFrontendLobbyRequestStart(0));
      iRequested = 1;
    }
  }
  CHECK(players == 2 * iLocalPlayers);
  CHECK(iChatSent && iChatReceived);
  int iDisplay = player1_car;
  time_to_start = -1;
  frontend_main_menu_prepare_race_start();
  aiCars[0] = player1_car;
  aiCars[1] = player2_car;
  CHECK(aiCars[0] == player_to_car[iDisplay]);
  CHECK(aiCars[0] == ViewType[0] && human_control[aiCars[0]]);
  CHECK(Drivers_Car[aiCars[0]] == iDesign);
  /* A nonidentity assignment is essential: identity-only harnesses missed it. */
  CHECK(aiCars[0] != iDisplay ||
        (iLocalPlayers == 2 && aiCars[1] != iDisplay + 1));
  if (iLocalPlayers == 2) {
    CHECK(aiCars[1] == player_to_car[iDisplay + 1]);
    CHECK(aiCars[1] == ViewType[1] && human_control[aiCars[1]]);
  }
  for (int i = 0; i < 90 + atoi(argv[10]); ++i) {
    Pump();
    CheckMapping();
  }

  /* These are the camera-affecting portions of play_game_init. Track loading
     itself also calls initcarview(ViewType), then play_game_init reseeds with
     player1_car/player2_car. No synthetic car positions or camera resets. */
  game_frame = -1;
  race_started = replaytype = 0;
  SelectedView[0] = 1;
  SelectedView[1] = 3;
  DeathView[0] = DeathView[1] = -1;
  char szError[512];
  CHECK(loadtrack_from_path_with_assets_ex(argv[1], argv[2], argv[2], 0,
          szError, sizeof(szError)) == ROLLER_ED_RESULT_OK);
  InitCars();
  select_view(0);
  initcarview(player1_car, 0);
  if (iLocalPlayers == 2) {
    select_view(1);
    initcarview(player2_car, 1);
  }
  ullDeadline = SDL_GetTicks() + 15000;
  while (!NetFrontendRaceSynchronise()) {
    CHECK(SDL_GetTicks() < ullDeadline);
    Pump();
    CheckMapping();
    for (int iPlayer = 0; iPlayer < iLocalPlayers; ++iPlayer)
      CheckCamera(iPlayer);
  }
  CHECK(NetFrontendRaceLocalPlayers() == iLocalPlayers);
  for (int i = 0; i < 180; ++i) {
    Pump();
    CheckMapping();
    for (int iPlayer = 0; iPlayer < iLocalPlayers; ++iPlayer)
      CheckCamera(iPlayer);
  }
  printf("%s display %d -> car %d/%d: %d mapping errors, "
         "worst stationary camera error %.3f\n", argv[3], iDisplay,
         aiCars[0], aiCars[1], iMappingFailures, fWorstCameraError);
  CHECK(!iMappingFailures);
  CHECK(fWorstCameraError < 2.0f);
  NetFrontendClose();
  return 0;
}
