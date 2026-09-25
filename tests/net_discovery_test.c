#include "net_discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)

static int TestRandom(void *pContext, void *pData, int iLength)
{
  uint32 *pNext = (uint32 *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  for (iByte = 0; iByte < iLength; ++iByte)
    pBytes[iByte] = (uint8)(++*pNext);
  return 1;
}

static tNetAddress TestAddress(uint8 byLast, uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.byFamily = NET_ADDR_IPV4;
  address.abAddress[0] = 203;
  address.abAddress[1] = 0;
  address.abAddress[2] = 113;
  address.abAddress[3] = byLast;
  address.unPort = unPort;
  return address;
}

static void TestCycle(tNetTransportSim *pSim, tNetRendezvous *pRendezvous,
                      tNetChannel *pHost, tNetChannel *pBrowser,
                      tNetDiscovery *pHostDiscovery,
                      tNetDiscovery *pBrowserDiscovery, uint64 ullNowMs)
{
  NetTransportSimAdvance(pSim, ullNowMs);
  NetRendezvousPump(pRendezvous);
  NetTransportSimAdvance(pSim, ullNowMs);
  NetChannelPump(pHost);
  NetChannelPump(pBrowser);
  NetDiscoveryPump(pHostDiscovery);
  NetDiscoveryPump(pBrowserDiscovery);
}

static void TestBrowserCycle(tNetTransportSim *pSim,
                             tNetRendezvous *pRendezvous,
                             tNetChannel *pBrowser,
                             tNetDiscovery *pBrowserDiscovery,
                             uint64 ullNowMs)
{
  NetTransportSimAdvance(pSim, ullNowMs);
  NetRendezvousPump(pRendezvous);
  NetTransportSimAdvance(pSim, ullNowMs);
  NetChannelPump(pBrowser);
  NetDiscoveryPump(pBrowserDiscovery);
}

int main(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(1);
  tNetAddress hostAddress = TestAddress(10, 7777);
  tNetAddress browserAddress = TestAddress(20, 7780);
  tNetAddress rendezvousAddress = TestAddress(30, 7778);
  tNetChannel *pHost, *pBrowser;
  tNetConnection *pHostConnection, *pBrowserConnection;
  tNetRendezvous *pRendezvous;
  tNetDiscovery *pHostDiscovery, *pBrowserDiscovery;
  tRvzSessionInfo info, listed;
  tNetAddress resolved;
  tNetTransport attacker;
  uint8 abLanPacket[sizeof(tNetLanAdvertisement)];
  tNetAddress hostLocal = TestAddress(110, 7777);
  tNetAddress browserLocal = TestAddress(120, 7780);
  eNetPunchState ePunchState;
  uint32 uiRandom = 100, uiServerRandom = 1000, uiSessionId;
  CHECK(pSim);
  CHECK(NetTransportSimSetEndpointAddress(pSim, 0, &hostAddress));
  CHECK(NetTransportSimSetEndpointAddress(pSim, 1, &browserAddress));
  CHECK(NetTransportSimSetEndpointAddress(pSim, 2, &rendezvousAddress));
  pHost = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pBrowser = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pRendezvous = NetRendezvousCreate(NetTransportSimEndpoint(pSim, 2),
                                    TestRandom, &uiServerRandom);
  CHECK(pHost && pBrowser && pRendezvous);
  pHostDiscovery = NetDiscoveryCreate(pHost, &rendezvousAddress,
                                      TestRandom, &uiRandom);
  pBrowserDiscovery = NetDiscoveryCreate(pBrowser, &rendezvousAddress,
                                         TestRandom, &uiRandom);
  CHECK(pHostDiscovery && pBrowserDiscovery);
  CHECK(NetDiscoverySetLocalCandidates(pHostDiscovery, &hostLocal, 1));
  CHECK(NetDiscoverySetLocalCandidates(pBrowserDiscovery, &browserLocal, 1));
  memset(&info, 0, sizeof(info));
  info.unTickRateHz = 36;
  info.byPlayers = 1;
  info.byMaxPlayers = 8;
  memcpy(info.szName, "INTERNET HOST", 14);
  memcpy(info.szTrack, "TRACK5", 7);
  memcpy(info.szBuildHash, "build-a", 8);
  CHECK(NetDiscoveryHostStart(pHostDiscovery, &info));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 0);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 1);
  CHECK(NetDiscoveryHostRegistered(pHostDiscovery, &uiSessionId));
  CHECK(uiSessionId);
  CHECK(NetDiscoveryList(pBrowserDiscovery, "build-a"));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 2);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 3);
  CHECK(NetDiscoveryListReady(pBrowserDiscovery));
  CHECK(NetDiscoverySessionCount(pBrowserDiscovery) == 1);
  CHECK(NetDiscoverySession(pBrowserDiscovery, 0, &listed));
  CHECK(listed.uiSessionId == uiSessionId);
  CHECK(strcmp(listed.szName, "INTERNET HOST") == 0);
  CHECK(listed.unPort == hostAddress.unPort);
  CHECK(NetDiscoveryResolve(pBrowserDiscovery, uiSessionId));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 4);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 5);
  CHECK(NetDiscoveryResolved(pBrowserDiscovery, uiSessionId, &resolved));
  CHECK(resolved.byFamily == hostAddress.byFamily);
  CHECK(resolved.unPort == hostAddress.unPort);
  CHECK(memcmp(resolved.abAddress, hostAddress.abAddress, 4) == 0);
  CHECK(NetDiscoveryPunch(pBrowserDiscovery, uiSessionId));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 6);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 7);
  CHECK(NetDiscoveryPunchState(pBrowserDiscovery, NULL) ==
        NET_PUNCH_IN_PROGRESS);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 106);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 107);
  ePunchState = NetDiscoveryPunchState(pBrowserDiscovery, &resolved);
  CHECK(ePunchState == NET_PUNCH_SUCCEEDED);
  CHECK(resolved.byFamily == hostAddress.byFamily &&
        resolved.unPort == hostAddress.unPort &&
        memcmp(resolved.abAddress, hostAddress.abAddress, 4) == 0);
  CHECK(NetDiscoveryPunchState(pHostDiscovery, &resolved) ==
        NET_PUNCH_SUCCEEDED);
  CHECK(resolved.byFamily == browserAddress.byFamily &&
        resolved.unPort == browserAddress.unPort &&
        memcmp(resolved.abAddress, browserAddress.abAddress, 4) == 0);

  /* A fresh attempt receives candidates but cannot complete while the host
     endpoint is not pumped. It must stop at the fixed deadline. */
  CHECK(NetDiscoveryPunch(pBrowserDiscovery, uiSessionId));
  TestBrowserCycle(pSim, pRendezvous, pBrowser, pBrowserDiscovery, 200);
  TestBrowserCycle(pSim, pRendezvous, pBrowser, pBrowserDiscovery, 201);
  TestBrowserCycle(pSim, pRendezvous, pBrowser, pBrowserDiscovery, 301);
  TestBrowserCycle(pSim, pRendezvous, pBrowser, pBrowserDiscovery,
                   3201);
  CHECK(NetDiscoveryPunchState(pBrowserDiscovery, NULL) ==
        NET_PUNCH_RELAY_IN_PROGRESS);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 3202);
  CHECK(NetDiscoveryPunchState(pBrowserDiscovery, &resolved) ==
        NET_PUNCH_RELAY_SUCCEEDED);
  CHECK(resolved.byFamily == hostAddress.byFamily &&
        resolved.unPort == hostAddress.unPort &&
        memcmp(resolved.abAddress, hostAddress.abAddress, 4) == 0);

  /* The channel sees the original logical peers while RLY1 carries the
     complete game packet through the rendezvous endpoint. */
  pHostConnection = NetChannelAddConnection(pHost, &browserAddress,
                                             0x12345678ull, 1);
  pBrowserConnection = NetChannelAddConnection(pBrowser, &hostAddress,
                                                0x12345678ull, 1);
  CHECK(pHostConnection && pBrowserConnection);
  CHECK(NetConnectionQueueMessage(pBrowserConnection, NET_MSG_CHAT,
                                  NET_MSG_RELIABLE, "relay", 6));
  NetChannelPump(pBrowser);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, 3203);
  {
    tNetMessage message;
    CHECK(NetConnectionReceiveMessage(pHostConnection, &message));
    CHECK(message.byType == NET_MSG_CHAT && message.unLength == 6 &&
          memcmp(message.abData, "relay", 6) == 0);
  }
  info.byPlayers = 3;
  NetDiscoveryHostUpdate(pHostDiscovery, &info);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 2);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 3);
  CHECK(NetDiscoveryList(pBrowserDiscovery, "build-a"));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 4);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 5);
  CHECK(NetDiscoverySession(pBrowserDiscovery, 0, &listed));
  CHECK(listed.byPlayers == 3);
  NetDiscoveryHostStop(pHostDiscovery);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 6);
  CHECK(NetDiscoveryList(pBrowserDiscovery, "build-a"));
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 7);
  TestCycle(pSim, pRendezvous, pHost, pBrowser, pHostDiscovery,
            pBrowserDiscovery, NET_RVZ_HEARTBEAT_MS + 8);
  CHECK(NetDiscoverySessionCount(pBrowserDiscovery) == 0);
  NetDiscoveryDestroy(pBrowserDiscovery);
  NetDiscoveryDestroy(pHostDiscovery);
  NetRendezvousDestroy(pRendezvous);
  NetChannelDestroy(pBrowser);
  NetChannelDestroy(pHost);
  NetTransportSimDestroy(pSim);

  /* LAN discovery uses only the game sockets: no rendezvous endpoint exists.
     A broadcast query receives a validated unicast advertisement, and the
     cached source address goes straight into the normal session join path. */
  pSim = NetTransportSimCreate(2);
  hostAddress = TestAddress(40, 7777);
  browserAddress = TestAddress(41, 7777);
  CHECK(pSim);
  CHECK(NetTransportSimSetEndpointAddress(pSim, 0, &hostAddress));
  CHECK(NetTransportSimSetEndpointAddress(pSim, 1, &browserAddress));
  pHost = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pBrowser = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pHost && pBrowser);
  pHostDiscovery = NetDiscoveryCreate(pHost, NULL, TestRandom, &uiRandom);
  pBrowserDiscovery = NetDiscoveryCreate(pBrowser, NULL,
                                         TestRandom, &uiRandom);
  CHECK(pHostDiscovery && pBrowserDiscovery);
  CHECK(NetDiscoveryEnableLan(pHostDiscovery, 7777));
  CHECK(NetDiscoveryEnableLan(pBrowserDiscovery, 7777));

  /* Hostile advertisements are discarded before they can populate the
     browser. This one has a non-terminated display name. */
  memset(&listed, 0, sizeof(listed));
  listed.uiSessionId = 99;
  listed.unPort = 7777;
  listed.unTickRateHz = 36;
  listed.byPlayers = 1;
  listed.byMaxPlayers = 8;
  memset(listed.szName, 'A', sizeof(listed.szName));
  memcpy(listed.szTrack, "TRACK5", 7);
  memcpy(listed.szBuildHash, "build-lan", 10);
  memset(abLanPacket, 0, sizeof(abLanPacket));
  abLanPacket[0] = (uint8)NET_LAN_PROTOCOL_ID;
  abLanPacket[1] = (uint8)(NET_LAN_PROTOCOL_ID >> 8);
  abLanPacket[2] = (uint8)(NET_LAN_PROTOCOL_ID >> 16);
  abLanPacket[3] = (uint8)(NET_LAN_PROTOCOL_ID >> 24);
  abLanPacket[4] = NET_LAN_PROTOCOL_VERSION;
  abLanPacket[5] = NET_LAN_MSG_ADVERTISE;
  NetRendezvousEncodeSessionInfo(abLanPacket + 8, &listed);
  attacker = NetTransportSimEndpoint(pSim, 2);
  CHECK(attacker.pSend(attacker.pContext, &browserAddress, abLanPacket,
                       sizeof(abLanPacket)) == sizeof(abLanPacket));
  NetTransportSimAdvance(pSim, 0);
  NetChannelPump(pBrowser);
  CHECK(NetDiscoverySessionCount(pBrowserDiscovery) == 0);

  memset(&info, 0, sizeof(info));
  info.unTickRateHz = 100;
  info.byPlayers = 2;
  info.byMaxPlayers = 8;
  memcpy(info.szName, "LAN HOST", 9);
  memcpy(info.szTrack, "TRACK5", 7);
  memcpy(info.szBuildHash, "build-lan", 10);
  CHECK(NetDiscoveryHostStart(pHostDiscovery, &info));
  CHECK(NetDiscoveryList(pBrowserDiscovery, "build-lan"));
  NetTransportSimAdvance(pSim, 0);
  NetChannelPump(pHost);
  NetDiscoveryPump(pHostDiscovery);
  NetChannelPump(pBrowser);
  NetDiscoveryPump(pBrowserDiscovery);
  CHECK(NetDiscoveryListReady(pBrowserDiscovery));
  CHECK(NetDiscoverySessionCount(pBrowserDiscovery) == 1);
  CHECK(NetDiscoverySession(pBrowserDiscovery, 0, &listed));
  CHECK(!strcmp(listed.szName, "LAN HOST"));
  CHECK(listed.unPort == 7777);
  CHECK(NetDiscoveryPunch(pBrowserDiscovery, listed.uiSessionId));
  CHECK(NetDiscoveryPunchState(pBrowserDiscovery, &resolved) ==
        NET_PUNCH_SUCCEEDED);
  CHECK(resolved.byFamily == hostAddress.byFamily &&
        resolved.unPort == hostAddress.unPort &&
        !memcmp(resolved.abAddress, hostAddress.abAddress, 4));

  NetDiscoveryHostStop(pHostDiscovery);
  NetTransportSimAdvance(pSim, NET_LAN_SESSION_TIMEOUT_MS + 1);
  NetDiscoveryPump(pBrowserDiscovery);
  CHECK(NetDiscoverySessionCount(pBrowserDiscovery) == 0);
  NetDiscoveryDestroy(pBrowserDiscovery);
  NetDiscoveryDestroy(pHostDiscovery);
  NetChannelDestroy(pBrowser);
  NetChannelDestroy(pHost);
  NetTransportSimDestroy(pSim);
  puts("NET-E6-S5 rendezvous, punch, relay and LAN discovery passed");
  return 0;
}
