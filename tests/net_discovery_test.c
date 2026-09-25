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
  tNetRendezvous *pRendezvous;
  tNetDiscovery *pHostDiscovery, *pBrowserDiscovery;
  tRvzSessionInfo info, listed;
  tNetAddress resolved;
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
        NET_PUNCH_TIMED_OUT);
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
  puts("NET-E6-S3 ordered UDP hole punching and timeout passed");
  return 0;
}
