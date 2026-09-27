#include "net_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); } } while (0)

static tNetAddress NetTestAddress(uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.abAddress[0] = 127;
  address.abAddress[3] = 1;
  address.unPort = unPort;
  address.byFamily = NET_ADDR_IPV4;
  return address;
}

static void NetTestSequenceAndAck(void)
{
  tNetAckState state = {0};
  CHECK(NetSequenceIsNewer(0, 65535));
  CHECK(NetSequenceIsNewer(1, 65534));
  CHECK(!NetSequenceIsNewer(65535, 0));
  CHECK(!NetSequenceIsNewer(1234, 1234));
  CHECK(NetAckStateReceive(&state, 65534));
  CHECK(NetAckStateReceive(&state, 65535));
  CHECK(NetAckStateReceive(&state, 0));
  CHECK(NetAckStateReceive(&state, 2));
  CHECK(state.unLatest == 2 && state.uiBits == 14);
  CHECK(NetAckStateReceive(&state, 1));
  CHECK(state.uiBits == 15);
  CHECK(!NetAckStateReceive(&state, 1));
  CHECK(NetAckContains(state.unLatest, state.uiBits, 2));
  CHECK(NetAckContains(state.unLatest, state.uiBits, 1));
  CHECK(NetAckContains(state.unLatest, state.uiBits, 0));
  CHECK(NetAckContains(state.unLatest, state.uiBits, 65535));
  CHECK(NetAckContains(state.unLatest, state.uiBits, 65534));
  CHECK(!NetAckContains(state.unLatest, state.uiBits, 65533));
  CHECK(NetAckStateReceive(&state, 34));
  CHECK(state.unLatest == 34 && state.uiBits == 0x80000000u);
  CHECK(NetAckContains(state.unLatest, state.uiBits, 2));
  CHECK(!NetAckContains(state.unLatest, state.uiBits, 1));
}

static void NetTestReliableOrdered(uint32 uiSeed)
{
  enum { MESSAGE_COUNT = 5000, MESSAGE_SIZE = 64 };
  tNetTransportSim *pSim = NetTransportSimCreate(uiSeed);
  tNetSimLink link = {30, 30, 50, 0, 0};
  tNetChannel *pA, *pB;
  tNetConnection *pConnectionA, *pConnectionB;
  tNetAddress addressA = NetTestAddress(0);
  tNetAddress addressB = NetTestAddress(1);
  tNetMessage message;
  uint64 ullToken = 0x123456789abcfde8ull;
  int iQueued = 0, iReceived = 0, iTime;

  CHECK(pSim);
  CHECK(NetTransportSimSetLink(pSim, 0, &link));
  CHECK(NetTransportSimSetLink(pSim, 1, &link));
  pA = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pB = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pA && pB);
  pConnectionA = NetChannelAddConnection(pA, &addressB, ullToken, 7);
  pConnectionB = NetChannelAddConnection(pB, &addressA, ullToken, 7);
  CHECK(pConnectionA && pConnectionB);

  for (iTime = 0; iTime < 60000 && iReceived < MESSAGE_COUNT; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    while (iQueued < MESSAGE_COUNT) {
      uint8 abPayload[MESSAGE_SIZE] = {0};
      memcpy(abPayload, &iQueued, sizeof(iQueued));
      if (!NetConnectionQueueMessage(pConnectionA, NET_MSG_CHAT,
              NET_MSG_RELIABLE | NET_MSG_ORDERED,
              abPayload, sizeof(abPayload)))
        break;
      ++iQueued;
    }
    NetPump();
    while (NetConnectionReceiveMessage(pConnectionB, &message)) {
      int iValue = -1;
      CHECK(message.byType == NET_MSG_CHAT);
      CHECK(message.byFlags == (NET_MSG_RELIABLE | NET_MSG_ORDERED));
      CHECK(message.unLength == MESSAGE_SIZE);
      memcpy(&iValue, message.abData, sizeof(iValue));
      CHECK(iValue == iReceived);
      ++iReceived;
    }
  }
  CHECK(iQueued == MESSAGE_COUNT && iReceived == MESSAGE_COUNT);
  {
    int iStopTime = iTime + 1000;
    for (; iTime < iStopTime; ++iTime) {
      CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
      NetPump();
      CHECK(!NetConnectionReceiveMessage(pConnectionB, &message));
    }
  }
  CHECK(NetConnectionPendingReliable(pConnectionA) == 0);
  CHECK(NetConnectionRttMs(pConnectionA) > 0.0f);
  CHECK(!NetConnectionIsExpired(pConnectionA));
  CHECK(!NetConnectionIsExpired(pConnectionB));
  printf("seed %u: 5000 reliable-ordered messages in %d ms, RTT %.1f ms\n",
         uiSeed, iTime, NetConnectionRttMs(pConnectionA));
  NetChannelDestroy(pB);
  NetChannelDestroy(pA);
  NetTransportSimDestroy(pSim);
}

static void NetTestGeneration(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(77);
  tNetChannel *pA, *pB;
  tNetConnection *pAConnection, *pBConnection;
  tNetAddress addressA = NetTestAddress(0);
  tNetAddress addressB = NetTestAddress(1);
  tNetMessage message;
  uint32 uiValue = 42;
  CHECK(pSim);
  pA = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pB = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pA && pB);
  pAConnection = NetChannelAddConnection(pA, &addressB, 991, 2);
  pBConnection = NetChannelAddConnection(pB, &addressA, 991, 3);
  CHECK(pAConnection && pBConnection);
  CHECK(NetConnectionQueueMessage(pAConnection, NET_MSG_READY,
        NET_MSG_RELIABLE | NET_MSG_ORDERED, &uiValue, sizeof(uiValue)));
  CHECK(NetTransportSimAdvance(pSim, 0));
  NetPump();
  NetPump();
  CHECK(!NetConnectionReceiveMessage(pBConnection, &message));
  CHECK(NetConnectionStalePackets(pBConnection) == 1);
  NetChannelDestroy(pB);
  NetChannelDestroy(pA);
  NetTransportSimDestroy(pSim);
}

static void NetTestKeepaliveAndExpiry(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(88);
  tNetSimLink link = {5, 0, 0, 0, 0};
  tNetChannel *pA, *pB;
  tNetConnection *pAConnection, *pBConnection;
  tNetAddress addressA = NetTestAddress(0);
  tNetAddress addressB = NetTestAddress(1);
  int iTime;
  CHECK(pSim);
  CHECK(NetTransportSimSetLink(pSim, 0, &link));
  CHECK(NetTransportSimSetLink(pSim, 1, &link));
  pA = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  pB = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  CHECK(pA && pB);
  pAConnection = NetChannelAddConnection(pA, &addressB, 1234, 1);
  pBConnection = NetChannelAddConnection(pB, &addressA, 1234, 1);
  CHECK(pAConnection && pBConnection);
  for (iTime = 0; iTime <= 30000; iTime += 10) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    CHECK(!NetConnectionIsExpired(pAConnection));
    CHECK(!NetConnectionIsExpired(pBConnection));
  }
  CHECK(NetConnectionLastReceiveMs(pAConnection) >= 29000);
  CHECK(NetConnectionLastReceiveMs(pBConnection) >= 29000);
  CHECK(NetConnectionRttMs(pAConnection) > 0.0f);
  CHECK(NetConnectionRttMs(pBConnection) > 0.0f);
  NetChannelDestroy(pB);
  for (; iTime <= 41050; iTime += 10) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
  }
  CHECK(NetConnectionIsExpired(pAConnection));
  NetChannelDestroy(pA);
  NetTransportSimDestroy(pSim);
}

static void NetTestPacketSpill(void)
{
  uint8 abSnapshot[1104] = {0};
  uint8 abOwnCar[80] = {0};
  uint8 abPacket[NET_MAX_PAYLOAD];
  tNetAddress addressB = NetTestAddress(1);
  tNetTransportSim *pSim = NetTransportSimCreate(99);
  tNetTransport endpointB;
  tNetChannel *pA;
  tNetConnection *pConnection;
  int aiLengths[3] = {0}, iCount = 0, iLength;
  CHECK(pSim);
  endpointB = NetTransportSimEndpoint(pSim, 1);
  pA = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pA);
  pConnection = NetChannelAddConnection(pA, &addressB, 5678, 1);
  CHECK(pConnection);
  CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0,
                                   abSnapshot, sizeof(abSnapshot)));
  CHECK(NetConnectionQueueMessage(pConnection, NET_MSG_OWN_CAR_STATE, 0,
                                   abOwnCar, sizeof(abOwnCar)));
  CHECK(NetTransportSimAdvance(pSim, 0));
  NetChannelPump(pA);
  while ((iLength = endpointB.pReceive(endpointB.pContext, NULL, abPacket,
                                        sizeof(abPacket))) > 0)
    aiLengths[iCount++] = iLength;
  CHECK(iCount == 2);
  CHECK(aiLengths[0] == 1132 && aiLengths[1] == 108);
  CHECK(aiLengths[0] <= NET_MAX_PAYLOAD && aiLengths[1] <= NET_MAX_PAYLOAD);
  CHECK(!NetConnectionQueueMessage(pConnection, NET_MSG_SNAPSHOT, 0,
                                    abSnapshot, NET_MAX_MESSAGE_SIZE + 1));
  NetChannelDestroy(pA);
  NetTransportSimDestroy(pSim);
}

static void NetTestConnectionRemoval(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(100);
  tNetChannel *pChannel;
  tNetAddress peer = NetTestAddress(1);
  CHECK(pSim);
  pChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pChannel);
  /* More replacements than the channel's fixed capacity prove each retired
     generation actually releases its slot. */
  for (int iGeneration = 1; iGeneration <= 32; ++iGeneration) {
    tNetConnection *pConnection = NetChannelAddConnection(
        pChannel, &peer, 5678, (uint8)iGeneration);
    CHECK(pConnection);
    CHECK(NetChannelRemoveConnection(pChannel, pConnection));
  }
  NetChannelDestroy(pChannel);
  NetTransportSimDestroy(pSim);
}

int main(void)
{
  NetTestSequenceAndAck();
  NetTestReliableOrdered(2718);
  NetTestReliableOrdered(3141);
  NetTestReliableOrdered(1618);
  NetTestGeneration();
  NetTestKeepaliveAndExpiry();
  NetTestPacketSpill();
  NetTestConnectionRemoval();
  puts("NET-E1-S2 channel acceptance passed");
  return 0;
}
