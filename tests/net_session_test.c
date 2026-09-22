#include "net_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); } } while (0)

typedef struct
{
  uint64 ullState;
  int iFail;
} tNetTestRandom;

typedef struct
{
  int iAuthorizations, iMessages;
  eNetJoinRefuseReason reason;
} tNetTestRejoin;

static eNetJoinRefuseReason NetTestAuthorizeRejoin(void *pContext,
                                                   uint8 byPlayerIdx,
                                                   uint64 ullNowMs)
{
  tNetTestRejoin *pRejoin = (tNetTestRejoin *)pContext;
  CHECK(byPlayerIdx == 0);
  CHECK(ullNowMs > 0);
  ++pRejoin->iAuthorizations;
  return pRejoin->reason;
}

static void NetTestHostMessage(void *pContext, uint8 byPlayerIdx,
                               const tNetMessage *pMessage)
{
  tNetTestRejoin *pRejoin = (tNetTestRejoin *)pContext;
  CHECK(byPlayerIdx == 0);
  CHECK(pMessage->byType == NET_MSG_CHAT);
  ++pRejoin->iMessages;
}

static int NetTestRandomBytes(void *pContext, void *pData, int iLength)
{
  tNetTestRandom *pRandom = (tNetTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  if (pRandom->iFail)
    return 0;
  for (iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState ^= pRandom->ullState << 13;
    pRandom->ullState ^= pRandom->ullState >> 7;
    pRandom->ullState ^= pRandom->ullState << 17;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 24);
  }
  return 1;
}

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

static void NetTestPumpJoin(tNetTransportSim *pSim, tNetSessionHost *pHost,
                            tNetSessionClient *pClient, int iStart,
                            int iEnd)
{
  int iTime;
  for (iTime = iStart; iTime <= iEnd; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    NetSessionHostPump(pHost);
    NetSessionClientPump(pClient);
  }
}

static void NetTestAcceptAndAddressChange(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(101);
  tNetChannel *pHostChannel, *pClientChannel;
  tNetConnection *pClientConnection, *pHostConnection;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetTestRandom random = {0x123456789abcdef0ull, 0};
  tNetAddress hostAddress = NetTestAddress(1);
  tNetAddress changedAddress = NetTestAddress(77), peer;
  uint64 ullToken;
  uint8 byPayload = 42;

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHost = NetSessionHostCreate(pHostChannel, 8, NetTestRandomBytes, &random);
  CHECK(pHost);
  pClientConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pClientConnection);
  pClient = NetSessionClientCreate(pClientConnection, NET_PROTOCOL_VERSION, 1,
                                   "Driver");
  CHECK(pClient && NetSessionClientStart(pClient));

  NetTestPumpJoin(pSim, pHost, pClient, 0, 500);
  CHECK(NetSessionClientState(pClient) == NET_JOIN_ACCEPTED);
  CHECK(NetSessionHostPlayerCount(pHost) == 1);
  CHECK(NetSessionClientPlayerIndex(pClient) == 0);
  CHECK(NetSessionClientGeneration(pClient) == 1);
  ullToken = NetSessionClientToken(pClient);
  CHECK(ullToken && NetSessionHostPlayerToken(pHost, 0) == ullToken);
  pHostConnection = NetSessionHostPlayerConnection(pHost, 0);
  CHECK(pHostConnection);
  CHECK(NetConnectionSessionToken(pHostConnection) == ullToken);
  CHECK(NetConnectionPendingReliable(pHostConnection) == 0);

  CHECK(NetTransportSimSetEndpointAddress(pSim, 0, &changedAddress));
  CHECK(NetConnectionQueueMessage(pClientConnection, NET_MSG_CHAT,
                                  NET_MSG_RELIABLE, &byPayload, 1));
  NetTestPumpJoin(pSim, pHost, pClient, 501, 700);
  CHECK(NetSessionHostPlayerCount(pHost) == 1);
  CHECK(NetSessionHostPlayerConnection(pHost, 0) == pHostConnection);
  CHECK(NetSessionHostPlayerToken(pHost, 0) == ullToken);
  CHECK(NetConnectionPeer(pHostConnection, &peer));
  CHECK(peer.unPort == changedAddress.unPort);

  NetSessionClientDestroy(pClient);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestVersionRefusal(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(202);
  tNetChannel *pHostChannel, *pClientChannel;
  tNetConnection *pClientConnection;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetTestRandom random = {0xfedcba9876543210ull, 0};
  tNetAddress hostAddress = NetTestAddress(1);

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHost = NetSessionHostCreate(pHostChannel, 8, NetTestRandomBytes, &random);
  CHECK(pHost);
  pClientConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pClientConnection);
  pClient = NetSessionClientCreate(pClientConnection,
      (uint16)(NET_PROTOCOL_VERSION + 1), 1, "OldBuild");
  CHECK(pClient && NetSessionClientStart(pClient));

  NetTestPumpJoin(pSim, pHost, pClient, 0, 300);
  CHECK(NetSessionClientState(pClient) == NET_JOIN_REFUSED);
  CHECK(NetSessionClientRefuseReason(pClient) ==
        NET_JOIN_REFUSE_VERSION_MISMATCH);
  CHECK(NetSessionHostPlayerCount(pHost) == 0);

  NetSessionClientDestroy(pClient);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestGenerationRejoin(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(212);
  tNetChannel *pHostChannel, *pClientChannel;
  tNetConnection *pOldConnection, *pNewConnection, *pHostConnection;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetTestRandom random = {0x3141592653589793ull, 0};
  tNetTestRejoin rejoin = {0};
  tNetAddress hostAddress = NetTestAddress(1);
  uint8 byPayload = 7;
  int iStaleBefore;

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHost = NetSessionHostCreate(pHostChannel, 8, NetTestRandomBytes, &random);
  CHECK(pHost);
  NetSessionHostSetRejoinCallback(pHost, NetTestAuthorizeRejoin, &rejoin);
  NetSessionHostSetMessageCallback(pHost, NetTestHostMessage, &rejoin);
  pOldConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pOldConnection);
  pClient = NetSessionClientCreate(pOldConnection, NET_PROTOCOL_VERSION, 1,
                                   "Driver");
  CHECK(pClient && NetSessionClientStart(pClient));
  NetTestPumpJoin(pSim, pHost, pClient, 0, 300);
  CHECK(NetSessionClientState(pClient) == NET_JOIN_ACCEPTED);
  pHostConnection = NetSessionHostPlayerConnection(pHost, 0);
  CHECK(pHostConnection && NetConnectionGeneration(pHostConnection) == 1);

  pNewConnection = NetChannelAddConnection(pClientChannel, &hostAddress,
      NetSessionClientToken(pClient), 2);
  CHECK(pNewConnection && NetSessionClientRejoin(pClient, pNewConnection));
  NetTestPumpJoin(pSim, pHost, pClient, 301, 650);
  CHECK(NetSessionClientState(pClient) == NET_JOIN_ACCEPTED);
  CHECK(NetSessionClientGeneration(pClient) == 2);
  CHECK(NetConnectionGeneration(pHostConnection) == 2);
  CHECK(NetSessionHostPlayerConnection(pHost, 0) == pHostConnection);
  CHECK(rejoin.iAuthorizations == 1);

  iStaleBefore = NetConnectionStalePackets(pHostConnection);
  CHECK(NetConnectionQueueMessage(pOldConnection, NET_MSG_CHAT,
                                  NET_MSG_RELIABLE, &byPayload, 1));
  NetTestPumpJoin(pSim, pHost, pClient, 651, 800);
  CHECK(NetConnectionStalePackets(pHostConnection) > iStaleBefore);
  CHECK(rejoin.iMessages == 0);

  CHECK(NetConnectionQueueMessage(pNewConnection, NET_MSG_CHAT,
                                  NET_MSG_RELIABLE, &byPayload, 1));
  NetTestPumpJoin(pSim, pHost, pClient, 801, 950);
  CHECK(rejoin.iMessages == 1);

  NetSessionClientDestroy(pClient);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestCSPRNGRequired(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(303);
  tNetChannel *pChannel;
  tNetTestRandom random = {1, 1};
  CHECK(pSim);
  pChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pChannel);
  CHECK(!NetSessionHostCreate(pChannel, 8, NULL, NULL));
  CHECK(!NetSessionHostCreate(pChannel, 8, NetTestRandomBytes, &random));
  NetChannelDestroy(pChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestMalformedMessages(void)
{
  tNetTransportSim *pSim = NetTransportSimCreate(404);
  tNetChannel *pHostChannel, *pClientChannel;
  tNetConnection *pHostConnection, *pClientConnection;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetTestRandom random = {0x1122334455667788ull, 0};
  tNetAddress hostAddress = NetTestAddress(1);
  tNetAddress clientAddress = NetTestAddress(0);
  uint8 abRequest[sizeof(tNetJoinRequest)] = {0};
  uint8 abAccept[sizeof(tNetJoinAccept)] = {0};
  tNetMessage message;

  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHost = NetSessionHostCreate(pHostChannel, 8, NetTestRandomBytes, &random);
  CHECK(pHost);
  pClientConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pClientConnection);
  abRequest[0] = (uint8)NET_PROTOCOL_VERSION;
  abRequest[2] = 3;
  memcpy(abRequest + 4, "Bad", 4);
  CHECK(NetConnectionQueueMessage(pClientConnection, NET_MSG_JOIN_REQUEST,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, abRequest, sizeof(abRequest)));
  for (int iTime = 0; iTime <= 200; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    NetSessionHostPump(pHost);
  }
  CHECK(NetConnectionReceiveMessage(pClientConnection, &message));
  CHECK(message.byType == NET_MSG_JOIN_REFUSE);
  CHECK(message.unLength == sizeof(tNetJoinRefuse));
  CHECK(message.abData[0] == NET_JOIN_REFUSE_INVALID_REQUEST);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);

  pSim = NetTransportSimCreate(405);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHostConnection = NetChannelAddConnection(pHostChannel, &clientAddress, 0, 0);
  pClientConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pHostConnection && pClientConnection);
  pClient = NetSessionClientCreate(pClientConnection, NET_PROTOCOL_VERSION, 1,
                                   "Driver");
  CHECK(pClient && NetSessionClientStart(pClient));
  abAccept[0] = 99;
  abAccept[8] = 1;
  abAccept[10] = 1;
  CHECK(NetConnectionQueueMessage(pHostConnection, NET_MSG_JOIN_ACCEPT,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, abAccept, sizeof(abAccept)));
  for (int iTime = 0; iTime <= 200; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    NetSessionClientPump(pClient);
  }
  CHECK(NetSessionClientState(pClient) == NET_JOIN_WAITING);
  CHECK(NetConnectionSessionToken(pClientConnection) == 0);
  NetSessionClientDestroy(pClient);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

int main(void)
{
  NetTestAcceptAndAddressChange();
  NetTestVersionRefusal();
  NetTestGenerationRejoin();
  NetTestCSPRNGRequired();
  NetTestMalformedMessages();
  puts("NET-E1-S3 join, refusal, token and address migration passed");
  return 0;
}
