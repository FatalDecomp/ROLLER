#include "net_capture.h"
#include "net_channel.h"
#include "net_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); \
} } while (0)

typedef struct {
  int iCorrections, iDeferred, iReplayTicks;
  float fCorrectionMagnitude, fReplayMs;
} tNetCaptureStatsDelta;

static tNetAddress NetCaptureTestAddress(uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.abAddress[0] = 127;
  address.abAddress[3] = 1;
  address.unPort = unPort;
  address.byFamily = NET_ADDR_IPV4;
  return address;
}

static void NetCaptureTestDrain(tNetConnection *pConnection)
{
  tNetMessage message;
  while (NetConnectionReceiveMessage(pConnection, &message)) {
    tNetCaptureStatsDelta delta;
    CHECK(message.byType == NET_MSG_INPUT_FEEDBACK);
    CHECK(message.unLength == sizeof(delta));
    memcpy(&delta, message.abData, sizeof(delta));
    g_netStats.iCorrectionCount += delta.iCorrections;
    g_netStats.iDeferredCorrections += delta.iDeferred;
    g_netStats.iReplayTicksTotal += delta.iReplayTicks;
    g_netStats.fCorrectionMagnitude = delta.fCorrectionMagnitude;
    g_netStats.fReplayMsTotal += delta.fReplayMs;
    if (delta.fReplayMs > g_netStats.fReplayMsWorst)
      g_netStats.fReplayMsWorst = delta.fReplayMs;
  }
}

static void NetCaptureTestRecord(const char *szPath, tNetStats *pExpected)
{
  static const tNetCaptureStatsDelta aDeltas[] = {
    {1, 0, 4, 0.25f, 0.11f},
    {2, 3, 9, 1.50f, 0.37f},
    {0, 1, 2, 0.75f, 0.08f}
  };
  tNetTransportSim *pSim = NetTransportSimCreate(0xe852u);
  tNetSimLink link = {5, 0, 0, 0, 0};
  tNetPacketCapture *pCapture;
  tNetChannel *pHost, *pClient;
  tNetConnection *pHostConnection, *pClientConnection;
  tNetAddress hostAddress = NetCaptureTestAddress(0);
  tNetAddress clientAddress = NetCaptureTestAddress(1);
  uint64 ullToken = 0x8520cafef00d1234ull;
  int iNextDelta = 0;
  CHECK(pSim);
  CHECK(NetTransportSimSetLink(pSim, 0, &link));
  CHECK(NetTransportSimSetLink(pSim, 1, &link));
  pCapture = NetPacketCaptureCreate(NetTransportSimEndpoint(pSim, 1),
                                    szPath);
  CHECK(pCapture);
  pHost = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pClient = NetChannelCreate(NetPacketCaptureEndpoint(pCapture));
  CHECK(pHost && pClient);
  pHostConnection = NetChannelAddConnection(pHost, &clientAddress, ullToken, 1);
  pClientConnection = NetChannelAddConnection(pClient, &hostAddress, ullToken, 1);
  CHECK(pHostConnection && pClientConnection);
  memset(&g_netStats, 0, sizeof(g_netStats));
  for (int iTime = 0; iTime <= 60; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    if (iNextDelta < (int)(sizeof(aDeltas) / sizeof(aDeltas[0])) &&
        iTime == iNextDelta * 20) {
      CHECK(NetConnectionQueueMessage(pHostConnection,
          NET_MSG_INPUT_FEEDBACK, NET_MSG_RELIABLE | NET_MSG_ORDERED,
          &aDeltas[iNextDelta], sizeof(aDeltas[iNextDelta])));
      ++iNextDelta;
    }
    NetChannelPump(pHost);
    NetChannelPump(pClient);
    NetCaptureTestDrain(pClientConnection);
  }
  CHECK(iNextDelta == (int)(sizeof(aDeltas) / sizeof(aDeltas[0])));
  CHECK(NetConnectionPendingReliable(pHostConnection) == 0);
  *pExpected = g_netStats;
  NetChannelDestroy(pClient);
  NetChannelDestroy(pHost);
  CHECK(NetPacketCaptureClose(pCapture));
  NetTransportSimDestroy(pSim);
}

static void NetCaptureTestReplay(const char *szPath,
                                 const tNetStats *pExpected)
{
  tNetPacketPlayback *pPlayback = NetPacketPlaybackCreate(szPath);
  tNetChannel *pClient;
  tNetConnection *pClientConnection;
  tNetAddress hostAddress = NetCaptureTestAddress(0);
  uint64 ullToken = 0x8520cafef00d1234ull;
  uint64 ullFirst, ullLast;
  CHECK(pPlayback);
  ullFirst = NetPacketPlaybackFirstMs(pPlayback);
  ullLast = NetPacketPlaybackLastMs(pPlayback);
  CHECK(ullFirst == 5 && ullLast == 45);
  pClient = NetChannelCreate(NetPacketPlaybackEndpoint(pPlayback));
  CHECK(pClient);
  pClientConnection = NetChannelAddConnection(pClient, &hostAddress,
                                               ullToken, 1);
  CHECK(pClientConnection);
  memset(&g_netStats, 0, sizeof(g_netStats));
  for (uint64 ullTime = ullFirst; ullTime <= ullLast; ++ullTime) {
    CHECK(NetPacketPlaybackAdvance(pPlayback, ullTime));
    NetChannelPump(pClient);
    NetCaptureTestDrain(pClientConnection);
  }
  CHECK(NetPacketPlaybackOk(pPlayback));
  CHECK(NetPacketPlaybackComplete(pPlayback));
  CHECK(!memcmp(&g_netStats, pExpected, sizeof(g_netStats)));
  NetChannelDestroy(pClient);
  NetPacketPlaybackDestroy(pPlayback);
}

int main(void)
{
  const char *szPath = "net_packet_capture_test.bin";
  tNetStats expected;
  NetCaptureTestRecord(szPath, &expected);
  NetCaptureTestReplay(szPath, &expected);
  CHECK(remove(szPath) == 0);
  puts("NET-E8-S2 packet capture/playback acceptance passed");
  return 0;
}
