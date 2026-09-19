#include "net_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(iCondition) do { if (!(iCondition)) { fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); } } while (0)

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
  CHECK(NetTestRun(2718, 0, 0) == NetTestRun(2718, 0, 0));
  CHECK(NetTestRun(3141, 200, 200) == NetTestRun(3141, 200, 200));
  puts("NET-E0 transport and wire bounds passed");
  return 0;
}
