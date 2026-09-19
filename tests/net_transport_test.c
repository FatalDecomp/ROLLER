#include "net_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(iCondition) do { if (!(iCondition)) { fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); } } while (0)

static uint64 NetTestClock(void *pContext)
{
  return *(const uint64 *)pContext;
}

static void NetTestAddress(void)
{
  tNetAddress address, roundTrip;
  tNetAddress localAddresses[64];
  char szAddress[NET_ADDRESS_STRING_CAPACITY];
  int iCount, iSawV4 = 0, iSawV6 = 0;

  CHECK(NetAddressParse(&address, "127.0.0.1:4321", 9));
  CHECK(address.byFamily == NET_ADDR_IPV4 && address.unPort == 4321);
  CHECK(NetAddressFormat(&address, szAddress, sizeof(szAddress)));
  CHECK(strcmp(szAddress, "127.0.0.1:4321") == 0);
  CHECK(NetAddressParse(&roundTrip, szAddress, 0));
  CHECK(NetAddressEqual(&address, &roundTrip));

  CHECK(NetAddressParse(&address, "::1", 8765));
  CHECK(address.byFamily == NET_ADDR_IPV6 && address.unPort == 8765);
  CHECK(NetAddressFormat(&address, szAddress, sizeof(szAddress)));
  CHECK(strcmp(szAddress, "[::1]:8765") == 0);
  CHECK(NetAddressParse(&roundTrip, szAddress, 0));
  CHECK(NetAddressEqual(&address, &roundTrip));

  CHECK(NetAddressParse(&address, "[fe80::1%7]:55", 0));
  CHECK(address.uiScopeId == 7 && address.unPort == 55);
  CHECK(NetAddressFormat(&address, szAddress, sizeof(szAddress)));
  CHECK(NetAddressParse(&roundTrip, szAddress, 0));
  CHECK(NetAddressEqual(&address, &roundTrip));
  CHECK(!NetAddressParse(&address, "127.0.0.1:65536", 0));
  CHECK(!NetAddressParse(&address, "[::1", 0));
  CHECK(!NetAddressParse(&address, "localhost:1234", 0));
  CHECK(!NetAddressFormat(&roundTrip, szAddress, 4));

  iCount = NetAddressEnumerateLocal(localAddresses, 64, 2468);
  CHECK(iCount > 0);
  for (int iAddress = 0; iAddress < iCount; ++iAddress) {
    CHECK(localAddresses[iAddress].unPort == 2468);
    iSawV4 |= localAddresses[iAddress].byFamily == NET_ADDR_IPV4;
    iSawV6 |= localAddresses[iAddress].byFamily == NET_ADDR_IPV6;
  }
  CHECK(iSawV4 && iSawV6);
}

static void NetTestLoopback(const char *szLoopback, int iFamily)
{
  static const char szPayload[] = "ROLLER dual-stack UDP";
  tNetTransportUdp *pSender = NetTransportUdpCreate(0);
  tNetTransportUdp *pReceiver = NetTransportUdpCreate(0);
  tNetTransport sender, receiver;
  tNetAddress destination, source;
  uint64 ullDeadline;
  char szReceived[64];
  int iReceived = 0;

  CHECK(pSender && pReceiver);
  sender = NetTransportUdpEndpoint(pSender);
  receiver = NetTransportUdpEndpoint(pReceiver);
  CHECK(sender.pSend && sender.pReceive && sender.pNowMs);
  CHECK(receiver.pReceive(receiver.pContext, NULL, szReceived, sizeof(szReceived)) == 0);
  CHECK(NetAddressParse(&destination, szLoopback, NetTransportUdpPort(pReceiver)));
  CHECK(destination.byFamily == iFamily);
  CHECK(sender.pSend(sender.pContext, &destination, szPayload, sizeof(szPayload)) == sizeof(szPayload));
  ullDeadline = receiver.pNowMs(receiver.pContext) + 2000;
  while (receiver.pNowMs(receiver.pContext) <= ullDeadline && !iReceived)
    iReceived = receiver.pReceive(receiver.pContext, &source, szReceived, sizeof(szReceived));
  CHECK(iReceived == sizeof(szPayload));
  CHECK(memcmp(szReceived, szPayload, sizeof(szPayload)) == 0);
  CHECK(source.byFamily == iFamily);
  CHECK(source.unPort == NetTransportUdpPort(pSender));
  CHECK(sender.pSend(sender.pContext, &destination, "", 0) == -1);
  CHECK(sender.pSend(sender.pContext, &destination, szReceived, NET_MAX_PAYLOAD + 1) == -1);
  NetTransportUdpDestroy(pReceiver);
  NetTransportUdpDestroy(pSender);
}

static void NetTestClockOverride(void)
{
  tNetTransportUdp *pUdp = NetTransportUdpCreate(0);
  tNetTransport transport;
  uint64 ullVirtualNow = 9876543210ull;
  uint64 ullFirst, ullSecond;
  CHECK(pUdp);
  transport = NetTransportUdpEndpoint(pUdp);
  NetTransportUdpSetClock(pUdp, NetTestClock, &ullVirtualNow);
  CHECK(transport.pNowMs(transport.pContext) == ullVirtualNow);
  ullVirtualNow += 17;
  CHECK(transport.pNowMs(transport.pContext) == ullVirtualNow);
  NetTransportUdpSetClock(pUdp, NULL, NULL);
  ullFirst = transport.pNowMs(transport.pContext);
  ullSecond = transport.pNowMs(transport.pContext);
  CHECK(ullSecond >= ullFirst);
  NetTransportUdpDestroy(pUdp);
}

static void NetTestPlatformRandom(void)
{
  uint8 abFirst[32] = {0}, abSecond[32] = {0}, abZero[32] = {0};
  CHECK(NetPlatformRandomBytes(NULL, abFirst, sizeof(abFirst)));
  CHECK(NetPlatformRandomBytes(NULL, abSecond, sizeof(abSecond)));
  CHECK(memcmp(abFirst, abZero, sizeof(abFirst)) != 0);
  CHECK(memcmp(abFirst, abSecond, sizeof(abFirst)) != 0);
  CHECK(!NetPlatformRandomBytes(NULL, abFirst, 0));
}

static uint64 NetTestRun(uint32 uiSeed, int iDuplicate, int iReorder)
{
  tNetTransportSim *pSim = NetTransportSimCreate(uiSeed);
  tNetSimLink link = {100, 20, 50, (uint16)iDuplicate, (uint16)iReorder};
  tNetTransport sender, receiver;
  uint64 ullHash = 1469598103934665603ull;
  int iReceived = 0, iOutOfOrder = 0, iLast = -1;
  CHECK(pSim);
  sender = NetTransportSimEndpoint(pSim, 0);
  receiver = NetTransportSimEndpoint(pSim, 1);
  CHECK(NetTransportSimSetLink(pSim, 0, &link));
  for (int iTime = 0; iTime <= 1300; ++iTime) {
    uint32 uiPacket;
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    if (iTime < 1000) {
      uiPacket = (uint32)iTime;
      CHECK(sender.pSend(sender.pContext, NULL, &uiPacket, sizeof(uiPacket)) == sizeof(uiPacket));
    }
    while (receiver.pReceive(receiver.pContext, NULL, &uiPacket, sizeof(uiPacket))) {
      int iLatency = iTime - (int)uiPacket;
      CHECK(iLatency >= 80 && iLatency <= (iReorder ? 241 : 120));
      CHECK(receiver.pNowMs(receiver.pContext) == (uint64)iTime);
      iOutOfOrder += (int)uiPacket < iLast;
      iLast = (int)uiPacket;
      ullHash = (ullHash ^ uiPacket ^ ((uint64)iTime << 32)) * 1099511628211ull;
      ++iReceived;
    }
  }
  CHECK(iReceived >= (iDuplicate ? 1050 : 915) && iReceived <= (iDuplicate ? 1260 : 980));
  CHECK(iOutOfOrder > 0);
  CHECK(!NetTransportSimAdvance(pSim, 1299));
  CHECK(sender.pSend(sender.pContext, NULL, "", NET_MAX_PAYLOAD + 1) == -1);
  link.unLossPermille = 1001;
  CHECK(!NetTransportSimSetLink(pSim, 0, &link));
  printf("seed %u: received %d, reordered %d\n", uiSeed, iReceived, iOutOfOrder);
  NetTransportSimDestroy(pSim);
  return ullHash;
}

int main(void)
{
  NetTestAddress();
  NetTestLoopback("127.0.0.1", NET_ADDR_IPV4);
  NetTestLoopback("::1", NET_ADDR_IPV6);
  NetTestClockOverride();
  NetTestPlatformRandom();
  CHECK(NetTestRun(2718, 0, 0) == NetTestRun(2718, 0, 0));
  CHECK(NetTestRun(3141, 200, 200) == NetTestRun(3141, 200, 200));
  puts("NET-E0/E1 transport, address and wire bounds passed");
  return 0;
}
