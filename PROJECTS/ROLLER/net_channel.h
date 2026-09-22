#ifndef ROLLER_NET_CHANNEL_H
#define ROLLER_NET_CHANNEL_H

#include "net_transport.h"

#define NET_RELIABLE_QUEUE 64
#define NET_DELIVERY_QUEUE 128
#define NET_MAX_MESSAGE_SIZE \
  (NET_MAX_PAYLOAD - (int)sizeof(tNetPacketHeader) - (int)sizeof(tNetMessageHeader))
#define NET_RELIABLE_RESEND_MS 100
#define NET_KEEPALIVE_MS 1000
#define NET_CONNECTION_TIMEOUT_MS 10000

typedef struct {
  uint16 unLatest;
  uint32 uiBits;
  uint8 byHasLatest;
} tNetAckState;

typedef struct {
  uint8 byType, byFlags;
  uint16 unLength, unReliableSeq;
  uint8 abData[NET_MAX_MESSAGE_SIZE];
} tNetMessage;

typedef struct tNetChannel tNetChannel;
typedef struct tNetConnection tNetConnection;
typedef tNetConnection *(*tNetChannelAcceptFn)(void *pContext,
                                               tNetChannel *pChannel,
                                               const tNetAddress *pPeer);

int NetSequenceIsNewer(uint16 unA, uint16 unB);
int NetAckStateReceive(tNetAckState *pState, uint16 unSequence);
int NetAckContains(uint16 unAck, uint32 uiAckBits, uint16 unSequence);

tNetChannel *NetChannelCreate(tNetTransport transport);
void NetChannelDestroy(tNetChannel *pChannel);
tNetConnection *NetChannelAddConnection(tNetChannel *pChannel,
                                        const tNetAddress *pPeer,
                                        uint64 ullSessionToken,
                                        uint8 byGeneration);
/* Removes a connection only after every session object has stopped referring
   to it. This lets a reconnect replace, rather than leak, its old generation. */
int NetChannelRemoveConnection(tNetChannel *pChannel,
                               tNetConnection *pConnection);
void NetChannelSetAcceptCallback(tNetChannel *pChannel,
                                 tNetChannelAcceptFn pAccept,
                                 void *pContext);
/* The transport clock the channel runs on (simulated in tests). */
uint64 NetChannelNowMs(const tNetChannel *pChannel);

int NetConnectionQueueMessage(tNetConnection *pConnection, uint8 byType,
                              uint8 byFlags, const void *pData,
                              uint16 unLength);
int NetConnectionReceiveMessage(tNetConnection *pConnection,
                                tNetMessage *pMessage);
int NetConnectionIsExpired(const tNetConnection *pConnection);
int NetConnectionPendingReliable(const tNetConnection *pConnection);
int NetConnectionStalePackets(const tNetConnection *pConnection);
uint64 NetConnectionLastReceiveMs(const tNetConnection *pConnection);
/* The transport clock of the connection's channel. */
uint64 NetConnectionNowMs(const tNetConnection *pConnection);
float NetConnectionRttMs(const tNetConnection *pConnection);
float NetConnectionJitterMs(const tNetConnection *pConnection);
void NetConnectionSetIdentity(tNetConnection *pConnection,
                              uint64 ullSessionToken,
                              uint8 byGeneration);
uint64 NetConnectionSessionToken(const tNetConnection *pConnection);
uint8 NetConnectionGeneration(const tNetConnection *pConnection);
int NetConnectionPeer(const tNetConnection *pConnection,
                      tNetAddress *pPeer);

void NetChannelPump(tNetChannel *pChannel);
void NetPump(void);

#endif
