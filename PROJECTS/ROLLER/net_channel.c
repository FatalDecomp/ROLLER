#include "net_channel.h"

#include <stdlib.h>
#include <string.h>

#define NET_MAX_CONNECTIONS 16
#define NET_PACKET_HISTORY 256
#define NET_ORDER_WINDOW NET_RELIABLE_QUEUE

typedef struct {
  uint8 byActive, byType, byFlags, bySent;
  uint16 unLength, unReliableSeq;
  uint32 uiId, uiOrder;
  uint64 ullLastSentMs;
  uint8 abData[NET_MAX_MESSAGE_SIZE];
} tNetOutgoingEntry;

typedef struct {
  uint8 byValid, byAcked, byReliableCount;
  uint16 unSequence;
  uint64 ullSentMs;
  uint32 auiReliableIds[NET_RELIABLE_QUEUE];
} tNetPacketRecord;

typedef struct {
  uint8 byValid, byType, byFlags;
  uint16 unSequence, unLength;
  uint8 abData[NET_MAX_MESSAGE_SIZE];
} tNetOrderedEntry;

struct tNetConnection {
  struct tNetChannel *pChannel;
  tNetAddress peer;
  uint64 ullSessionToken;
  uint8 byGeneration, byExpired, byAckDirty;
  uint16 unNextPacketSequence;
  uint16 unNextOrderedSend, unNextOrderedReceive, unNextReliableSend;
  tNetAckState packetReceive, reliableReceive;
  uint64 ullLastReceiveMs, ullLastSendMs;
  float fRttMs, fJitterMs;
  uint8 byHasRtt;
  int iStalePackets;
  uint32 uiNextEntryId, uiNextOrder;
  tNetOutgoingEntry aOutgoing[NET_RELIABLE_QUEUE];
  tNetPacketRecord aPacketHistory[NET_PACKET_HISTORY];
  tNetOrderedEntry aOrdered[NET_ORDER_WINDOW];
  tNetMessage aDelivery[NET_DELIVERY_QUEUE];
  int iDeliveryHead, iDeliveryCount;
};

struct tNetChannel {
  tNetTransport transport;
  tNetConnection *apConnections[NET_MAX_CONNECTIONS];
  int iConnectionCount;
  tNetChannelAcceptFn pAccept;
  void *pAcceptContext;
  struct tNetChannel *pNext;
};

static tNetChannel *s_pChannels;

static void NetWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static void NetWrite64(uint8 *pData, uint64 ullValue)
{
  NetWrite32(pData, (uint32)ullValue);
  NetWrite32(pData + 4, (uint32)(ullValue >> 32));
}

static uint16 NetRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static uint64 NetRead64(const uint8 *pData)
{
  return (uint64)NetRead32(pData) | ((uint64)NetRead32(pData + 4) << 32);
}

int NetSequenceIsNewer(uint16 unA, uint16 unB)
{
  return unA != unB && (uint16)(unA - unB) < 0x8000u;
}

int NetAckStateReceive(tNetAckState *pState, uint16 unSequence)
{
  uint16 unDistance;
  uint32 uiMask;
  if (!pState)
    return 0;
  if (!pState->byHasLatest) {
    pState->byHasLatest = 1;
    pState->unLatest = unSequence;
    pState->uiBits = 0;
    return 1;
  }
  if (unSequence == pState->unLatest)
    return 0;
  if (NetSequenceIsNewer(unSequence, pState->unLatest)) {
    unDistance = (uint16)(unSequence - pState->unLatest);
    if (unDistance > NET_ACK_BITS)
      pState->uiBits = 0;
    else if (unDistance == NET_ACK_BITS)
      pState->uiBits = 1u << (NET_ACK_BITS - 1);
    else
      pState->uiBits = (pState->uiBits << unDistance) |
          (1u << (unDistance - 1));
    pState->unLatest = unSequence;
    return 1;
  }
  unDistance = (uint16)(pState->unLatest - unSequence);
  if (!unDistance || unDistance > NET_ACK_BITS)
    return 0;
  uiMask = 1u << (unDistance - 1);
  if (pState->uiBits & uiMask)
    return 0;
  pState->uiBits |= uiMask;
  return 1;
}

int NetAckContains(uint16 unAck, uint32 uiAckBits, uint16 unSequence)
{
  uint16 unDistance;
  if (unSequence == unAck)
    return 1;
  if (!NetSequenceIsNewer(unAck, unSequence))
    return 0;
  unDistance = (uint16)(unAck - unSequence);
  return unDistance <= NET_ACK_BITS &&
         (uiAckBits & (1u << (unDistance - 1))) != 0;
}

static uint64 NetChannelNow(const tNetChannel *pChannel)
{
  return pChannel->transport.pNowMs(pChannel->transport.pContext);
}

static int NetChannelAddressEqual(const tNetAddress *pA,
                                  const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort)
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 :
      (pA->byFamily == NET_ADDR_IPV6 ? 16 : 0);
  return iLength &&
      (pA->byFamily != NET_ADDR_IPV6 || pA->uiScopeId == pB->uiScopeId) &&
      memcmp(pA->abAddress, pB->abAddress, (size_t)iLength) == 0;
}

static int NetDeliveryPush(tNetConnection *pConnection, uint8 byType,
                           uint8 byFlags, uint16 unReliableSeq,
                           const uint8 *pData, uint16 unLength)
{
  int iSlot;
  tNetMessage *pMessage;
  if (pConnection->iDeliveryCount >= NET_DELIVERY_QUEUE)
    return 0;
  iSlot = (pConnection->iDeliveryHead + pConnection->iDeliveryCount) %
      NET_DELIVERY_QUEUE;
  pMessage = &pConnection->aDelivery[iSlot];
  pMessage->byType = byType;
  pMessage->byFlags = byFlags;
  pMessage->unLength = unLength;
  pMessage->unReliableSeq = unReliableSeq;
  if (unLength)
    memcpy(pMessage->abData, pData, unLength);
  ++pConnection->iDeliveryCount;
  return 1;
}

static tNetOrderedEntry *NetFindOrdered(tNetConnection *pConnection,
                                        uint16 unSequence)
{
  int iEntry;
  for (iEntry = 0; iEntry < NET_ORDER_WINDOW; ++iEntry)
    if (pConnection->aOrdered[iEntry].byValid &&
        pConnection->aOrdered[iEntry].unSequence == unSequence)
      return &pConnection->aOrdered[iEntry];
  return NULL;
}

static void NetDrainOrdered(tNetConnection *pConnection)
{
  tNetOrderedEntry *pEntry;
  while ((pEntry = NetFindOrdered(pConnection,
                                  pConnection->unNextOrderedReceive)) != NULL) {
    if (!NetDeliveryPush(pConnection, pEntry->byType, pEntry->byFlags,
                         pEntry->unSequence, pEntry->abData,
                         pEntry->unLength))
      return;
    pEntry->byValid = 0;
    ++pConnection->unNextOrderedReceive;
  }
}

static void NetReceiveOrdered(tNetConnection *pConnection, uint8 byType,
                              uint8 byFlags, uint16 unSequence,
                              const uint8 *pData, uint16 unLength)
{
  uint16 unDistance;
  int iEntry;
  if (unSequence == pConnection->unNextOrderedReceive) {
    if (NetDeliveryPush(pConnection, byType, byFlags, unSequence,
                        pData, unLength)) {
      ++pConnection->unNextOrderedReceive;
      NetDrainOrdered(pConnection);
    }
    return;
  }
  if (!NetSequenceIsNewer(unSequence, pConnection->unNextOrderedReceive))
    return;
  unDistance = (uint16)(unSequence - pConnection->unNextOrderedReceive);
  if (unDistance >= NET_ORDER_WINDOW || NetFindOrdered(pConnection, unSequence))
    return;
  for (iEntry = 0; iEntry < NET_ORDER_WINDOW; ++iEntry) {
    tNetOrderedEntry *pEntry = &pConnection->aOrdered[iEntry];
    if (!pEntry->byValid) {
      pEntry->byValid = 1;
      pEntry->byType = byType;
      pEntry->byFlags = byFlags;
      pEntry->unSequence = unSequence;
      pEntry->unLength = unLength;
      if (unLength)
        memcpy(pEntry->abData, pData, unLength);
      return;
    }
  }
}

static void NetReceiveMessage(tNetConnection *pConnection, uint8 byType,
                              uint8 byFlags, uint16 unReliableSeq,
                              const uint8 *pData, uint16 unLength)
{
  if (byType == NET_MSG_PING || byType == NET_MSG_PONG) {
    return;
  } else if (!(byFlags & NET_MSG_RELIABLE)) {
    NetDeliveryPush(pConnection, byType, byFlags, 0, pData, unLength);
  } else if (byFlags & NET_MSG_ORDERED) {
    NetReceiveOrdered(pConnection, byType, byFlags, unReliableSeq,
                      pData, unLength);
  } else if (NetAckStateReceive(&pConnection->reliableReceive,
                                unReliableSeq)) {
    NetDeliveryPush(pConnection, byType, byFlags, unReliableSeq,
                    pData, unLength);
  }
}

static tNetOutgoingEntry *NetFindOutgoingById(tNetConnection *pConnection,
                                              uint32 uiId)
{
  int iEntry;
  for (iEntry = 0; iEntry < NET_RELIABLE_QUEUE; ++iEntry)
    if (pConnection->aOutgoing[iEntry].byActive &&
        pConnection->aOutgoing[iEntry].uiId == uiId)
      return &pConnection->aOutgoing[iEntry];
  return NULL;
}

static void NetUpdateRtt(tNetConnection *pConnection, uint64 ullNowMs,
                         uint64 ullSentMs)
{
  float fSample = (float)(ullNowMs - ullSentMs);
  if (!pConnection->byHasRtt) {
    pConnection->fRttMs = fSample;
    pConnection->fJitterMs = fSample * 0.5f;
    pConnection->byHasRtt = 1;
  } else {
    float fDelta = fSample - pConnection->fRttMs;
    if (fDelta < 0.0f)
      fDelta = -fDelta;
    pConnection->fJitterMs += (fDelta - pConnection->fJitterMs) * 0.25f;
    pConnection->fRttMs += (fSample - pConnection->fRttMs) * 0.125f;
  }
}

static void NetProcessAcks(tNetConnection *pConnection, uint16 unAck,
                           uint32 uiAckBits, uint64 ullNowMs)
{
  int iRecord, iReliable;
  for (iRecord = 0; iRecord < NET_PACKET_HISTORY; ++iRecord) {
    tNetPacketRecord *pRecord = &pConnection->aPacketHistory[iRecord];
    if (!pRecord->byValid || pRecord->byAcked ||
        !NetAckContains(unAck, uiAckBits, pRecord->unSequence))
      continue;
    pRecord->byAcked = 1;
    NetUpdateRtt(pConnection, ullNowMs, pRecord->ullSentMs);
    for (iReliable = 0; iReliable < pRecord->byReliableCount; ++iReliable) {
      tNetOutgoingEntry *pEntry = NetFindOutgoingById(
          pConnection, pRecord->auiReliableIds[iReliable]);
      if (pEntry)
        pEntry->byActive = 0;
    }
  }
}

static int NetValidatePacket(const uint8 *pPacket, int iLength)
{
  int iOffset = (int)sizeof(tNetPacketHeader);
  int iMessage;
  uint8 byMessageCount;
  if (iLength < (int)sizeof(tNetPacketHeader) ||
      NetRead32(pPacket) != NET_PROTOCOL_ID)
    return 0;
  byMessageCount = pPacket[21];
  for (iMessage = 0; iMessage < byMessageCount; ++iMessage) {
    uint8 byType, byFlags;
    uint16 unLength;
    if (iOffset + (int)sizeof(tNetMessageHeader) > iLength)
      return 0;
    byType = pPacket[iOffset];
    byFlags = pPacket[iOffset + 1];
    unLength = NetRead16(pPacket + iOffset + 2);
    if (byType < NET_MSG_PING || byType > NET_MSG_KICK ||
        (byFlags & ~(NET_MSG_RELIABLE | NET_MSG_ORDERED)) ||
        ((byFlags & NET_MSG_ORDERED) && !(byFlags & NET_MSG_RELIABLE)) ||
        unLength > NET_MAX_MESSAGE_SIZE ||
        iOffset + (int)sizeof(tNetMessageHeader) + unLength > iLength)
      return 0;
    iOffset += (int)sizeof(tNetMessageHeader) + unLength;
  }
  return iOffset == iLength;
}

static tNetConnection *NetFindConnection(tNetChannel *pChannel,
                                         const tNetAddress *pFrom,
                                         uint64 ullToken,
                                         uint8 byGeneration,
                                         const uint8 *pPacket)
{
  int iConnection;
  /* Prefer an exact identity.  A reconnecting client may temporarily keep
     its old-generation object beside the fresh one on the same channel. */
  for (iConnection = 0; iConnection < pChannel->iConnectionCount;
       ++iConnection) {
    tNetConnection *pConnection = pChannel->apConnections[iConnection];
    if (pConnection->ullSessionToken != ullToken ||
        pConnection->byGeneration != byGeneration ||
        (!ullToken && !NetChannelAddressEqual(&pConnection->peer, pFrom)))
      continue;
    if (ullToken && !NetChannelAddressEqual(&pConnection->peer, pFrom))
      pConnection->peer = *pFrom;
    return pConnection;
  }
  for (iConnection = 0; iConnection < pChannel->iConnectionCount;
       ++iConnection) {
    tNetConnection *pConnection = pChannel->apConnections[iConnection];
    if (pConnection->ullSessionToken == ullToken) {
      if (!ullToken && !NetChannelAddressEqual(&pConnection->peer, pFrom))
        continue;
      if (pConnection->byGeneration != byGeneration) {
        const uint8 *pMessage = pPacket + sizeof(tNetPacketHeader);
        const uint8 *pPayload = pMessage + sizeof(tNetMessageHeader);
        /* A credentialled rejoin is the sole operation allowed to replace a
           generation.  Reset every reliability window before accepting its
           first packet; an old-generation packet can then never enter the
           new delivery stream.  The session validates the repeated token,
           generation and player index before accepting the rejoin. */
        if (ullToken && byGeneration &&
            byGeneration == (uint8)(pConnection->byGeneration + 1u) &&
            pPacket[21] == 1 && pMessage[0] == NET_MSG_REJOIN_REQUEST &&
            pMessage[1] == (NET_MSG_RELIABLE | NET_MSG_ORDERED) &&
            NetRead16(pMessage + 2) == sizeof(tNetRejoinRequest) &&
            NetRead64(pPayload) == ullToken &&
            pPayload[8] == byGeneration) {
          tNetChannel *pOwner = pConnection->pChannel;
          int iStalePackets = pConnection->iStalePackets;
          uint16 unInitial = (uint16)ullToken;
          memset(pConnection, 0, sizeof(*pConnection));
          pConnection->pChannel = pOwner;
          pConnection->peer = *pFrom;
          pConnection->ullSessionToken = ullToken;
          pConnection->byGeneration = byGeneration;
          pConnection->unNextPacketSequence = unInitial ? unInitial : 1;
          pConnection->unNextOrderedSend = unInitial;
          pConnection->unNextOrderedReceive = unInitial;
          pConnection->unNextReliableSend = unInitial;
          pConnection->ullLastReceiveMs = NetChannelNow(pChannel);
          pConnection->ullLastSendMs = pConnection->ullLastReceiveMs;
          pConnection->iStalePackets = iStalePackets;
          pConnection->uiNextEntryId = 1;
          pConnection->uiNextOrder = 1;
          return pConnection;
        }
        ++pConnection->iStalePackets;
        return NULL;
      }
      if (ullToken && !NetChannelAddressEqual(&pConnection->peer, pFrom))
        pConnection->peer = *pFrom;
      return pConnection;
    }
  }

  /* A join accept carries the newly minted token in both the packet and its
     payload.  Let the address-bound provisional client consume it, then the
     session promotes that connection to the authenticated identity. */
  if (ullToken && byGeneration && pPacket[21] &&
      pPacket[sizeof(tNetPacketHeader)] == NET_MSG_JOIN_ACCEPT &&
      NetRead16(pPacket + sizeof(tNetPacketHeader) + 2) ==
          sizeof(tNetJoinAccept) &&
      NetRead64(pPacket + sizeof(tNetPacketHeader) +
                sizeof(tNetMessageHeader)) == ullToken &&
      pPacket[sizeof(tNetPacketHeader) + sizeof(tNetMessageHeader) + 8] ==
          byGeneration) {
    for (iConnection = 0; iConnection < pChannel->iConnectionCount;
         ++iConnection) {
      tNetConnection *pConnection = pChannel->apConnections[iConnection];
      if (!pConnection->ullSessionToken && !pConnection->byGeneration &&
          NetChannelAddressEqual(&pConnection->peer, pFrom))
        return pConnection;
    }
  }

  /* Only an uncredentialled JOIN_REQUEST may ask the session listener to
     allocate a provisional host-side connection. */
  if (!ullToken && !byGeneration && pChannel->pAccept && pPacket[21] &&
      pPacket[sizeof(tNetPacketHeader)] == NET_MSG_JOIN_REQUEST &&
      pPacket[sizeof(tNetPacketHeader) + 1] ==
          (NET_MSG_RELIABLE | NET_MSG_ORDERED) &&
      NetRead16(pPacket + sizeof(tNetPacketHeader) + 2) ==
          sizeof(tNetJoinRequest))
    return pChannel->pAccept(pChannel->pAcceptContext, pChannel, pFrom);
  return NULL;
}

static void NetProcessPacket(tNetChannel *pChannel, const tNetAddress *pFrom,
                             const uint8 *pPacket, int iLength,
                             uint64 ullNowMs)
{
  tNetConnection *pConnection;
  uint16 unSequence, unAck;
  uint32 uiAckBits;
  int iOffset, iMessage;
  if (!NetValidatePacket(pPacket, iLength))
    return;
  pConnection = NetFindConnection(pChannel, pFrom, NetRead64(pPacket + 12),
                                  pPacket[20], pPacket);
  if (!pConnection || pConnection->byExpired)
    return;
  if (pConnection->iDeliveryCount + pPacket[21] > NET_DELIVERY_QUEUE)
    return;
  unSequence = NetRead16(pPacket + 4);
  unAck = NetRead16(pPacket + 6);
  uiAckBits = NetRead32(pPacket + 8);
  pConnection->ullLastReceiveMs = ullNowMs;
  NetProcessAcks(pConnection, unAck, uiAckBits, ullNowMs);
  if (!NetAckStateReceive(&pConnection->packetReceive, unSequence))
    return;
  if (pPacket[21])
    pConnection->byAckDirty = 1;
  iOffset = (int)sizeof(tNetPacketHeader);
  for (iMessage = 0; iMessage < pPacket[21]; ++iMessage) {
    uint8 byType = pPacket[iOffset];
    uint8 byFlags = pPacket[iOffset + 1];
    uint16 unLength = NetRead16(pPacket + iOffset + 2);
    uint16 unReliableSeq = NetRead16(pPacket + iOffset + 4);
    iOffset += (int)sizeof(tNetMessageHeader);
    NetReceiveMessage(pConnection, byType, byFlags, unReliableSeq,
                      pPacket + iOffset, unLength);
    iOffset += unLength;
  }
}

static void NetBuildPacketHeader(const tNetConnection *pConnection,
                                 uint8 *pPacket, uint16 unSequence,
                                 uint8 byMessageCount)
{
  NetWrite32(pPacket, NET_PROTOCOL_ID);
  NetWrite16(pPacket + 4, unSequence);
  NetWrite16(pPacket + 6, pConnection->packetReceive.byHasLatest ?
             pConnection->packetReceive.unLatest : 0);
  NetWrite32(pPacket + 8, pConnection->packetReceive.byHasLatest ?
             pConnection->packetReceive.uiBits : 0);
  NetWrite64(pPacket + 12, pConnection->ullSessionToken);
  pPacket[20] = pConnection->byGeneration;
  pPacket[21] = byMessageCount;
}

static int NetSendPacket(tNetConnection *pConnection, uint8 *pPacket,
                         int iLength, uint8 byMessageCount,
                         const int *piEntries, int iEntryCount,
                         uint64 ullNowMs)
{
  tNetChannel *pChannel = pConnection->pChannel;
  uint16 unSequence = pConnection->unNextPacketSequence;
  int iEntry;
  NetBuildPacketHeader(pConnection, pPacket, unSequence, byMessageCount);
  if (pChannel->transport.pSend(pChannel->transport.pContext,
                                &pConnection->peer, pPacket, iLength) != iLength)
    return 0;
  ++pConnection->unNextPacketSequence;
  pConnection->ullLastSendMs = ullNowMs;
  pConnection->byAckDirty = 0;
  if (byMessageCount) {
    tNetPacketRecord *pRecord =
        &pConnection->aPacketHistory[unSequence % NET_PACKET_HISTORY];
    memset(pRecord, 0, sizeof(*pRecord));
    pRecord->byValid = 1;
    pRecord->unSequence = unSequence;
    pRecord->ullSentMs = ullNowMs;
    for (iEntry = 0; iEntry < iEntryCount; ++iEntry) {
      tNetOutgoingEntry *pOutgoing =
          &pConnection->aOutgoing[piEntries[iEntry]];
      if (pOutgoing->byFlags & NET_MSG_RELIABLE)
        pRecord->auiReliableIds[pRecord->byReliableCount++] = pOutgoing->uiId;
      pOutgoing->bySent = 1;
      pOutgoing->ullLastSentMs = ullNowMs;
      if (!(pOutgoing->byFlags & NET_MSG_RELIABLE))
        pOutgoing->byActive = 0;
    }
  }
  return 1;
}

static int NetOutgoingEligible(const tNetOutgoingEntry *pEntry,
                               uint64 ullNowMs)
{
  return pEntry->byActive &&
      (!(pEntry->byFlags & NET_MSG_RELIABLE) || !pEntry->bySent ||
       ullNowMs - pEntry->ullLastSentMs >= NET_RELIABLE_RESEND_MS);
}

static int NetFindNextOutgoing(const tNetConnection *pConnection,
                               uint64 ullNowMs, uint32 uiAfterOrder)
{
  uint32 uiBestOrder = UINT32_MAX;
  int iBest = -1, iEntry;
  for (iEntry = 0; iEntry < NET_RELIABLE_QUEUE; ++iEntry) {
    const tNetOutgoingEntry *pEntry = &pConnection->aOutgoing[iEntry];
    if (NetOutgoingEligible(pEntry, ullNowMs) &&
        pEntry->uiOrder > uiAfterOrder && pEntry->uiOrder < uiBestOrder) {
      iBest = iEntry;
      uiBestOrder = pEntry->uiOrder;
    }
  }
  return iBest;
}

static int NetSendQueued(tNetConnection *pConnection, uint64 ullNowMs)
{
  int iPackets = 0;
  while (iPackets < NET_RELIABLE_QUEUE) {
    uint8 abPacket[NET_MAX_PAYLOAD];
    int aiEntries[NET_RELIABLE_QUEUE];
    int iLength = (int)sizeof(tNetPacketHeader), iEntryCount = 0;
    uint32 uiAfterOrder = 0;
    int iEntry;
    while ((iEntry = NetFindNextOutgoing(pConnection, ullNowMs,
                                         uiAfterOrder)) >= 0) {
      tNetOutgoingEntry *pOutgoing = &pConnection->aOutgoing[iEntry];
      int iMessageSize = (int)sizeof(tNetMessageHeader) + pOutgoing->unLength;
      if (iLength + iMessageSize > NET_MAX_PAYLOAD)
        break;
      abPacket[iLength] = pOutgoing->byType;
      abPacket[iLength + 1] = pOutgoing->byFlags;
      NetWrite16(abPacket + iLength + 2, pOutgoing->unLength);
      NetWrite16(abPacket + iLength + 4, pOutgoing->unReliableSeq);
      if (pOutgoing->unLength)
        memcpy(abPacket + iLength + sizeof(tNetMessageHeader),
               pOutgoing->abData, pOutgoing->unLength);
      iLength += iMessageSize;
      aiEntries[iEntryCount++] = iEntry;
      uiAfterOrder = pOutgoing->uiOrder;
    }
    if (!iEntryCount)
      break;
    if (!NetSendPacket(pConnection, abPacket, iLength, (uint8)iEntryCount,
                       aiEntries, iEntryCount, ullNowMs))
      break;
    ++iPackets;
  }
  return iPackets;
}

static void NetSendKeepalive(tNetConnection *pConnection, uint64 ullNowMs)
{
  uint8 abPacket[sizeof(tNetPacketHeader) + sizeof(tNetMessageHeader)] = {0};
  int iOffset = (int)sizeof(tNetPacketHeader);
  abPacket[iOffset] = NET_MSG_PING;
  NetSendPacket(pConnection, abPacket, sizeof(abPacket), 1, NULL, 0,
                ullNowMs);
}

static void NetPumpConnection(tNetConnection *pConnection, uint64 ullNowMs)
{
  uint8 abPacket[sizeof(tNetPacketHeader)];
  if (pConnection->byExpired)
    return;
  if (ullNowMs - pConnection->ullLastReceiveMs > NET_CONNECTION_TIMEOUT_MS) {
    pConnection->byExpired = 1;
    return;
  }
  if (!NetSendQueued(pConnection, ullNowMs)) {
    if (pConnection->byAckDirty)
      NetSendPacket(pConnection, abPacket, sizeof(abPacket), 0, NULL, 0,
                    ullNowMs);
    else if (ullNowMs - pConnection->ullLastSendMs >= NET_KEEPALIVE_MS)
      NetSendKeepalive(pConnection, ullNowMs);
  }
}

tNetChannel *NetChannelCreate(tNetTransport transport)
{
  tNetChannel *pChannel;
  if (!transport.pSend || !transport.pReceive || !transport.pNowMs)
    return NULL;
  pChannel = (tNetChannel *)calloc(1, sizeof(*pChannel));
  if (!pChannel)
    return NULL;
  pChannel->transport = transport;
  pChannel->pNext = s_pChannels;
  s_pChannels = pChannel;
  return pChannel;
}

void NetChannelDestroy(tNetChannel *pChannel)
{
  tNetChannel **ppChannel;
  int iConnection;
  if (!pChannel)
    return;
  for (ppChannel = &s_pChannels; *ppChannel && *ppChannel != pChannel;
       ppChannel = &(*ppChannel)->pNext) { }
  if (*ppChannel)
    *ppChannel = pChannel->pNext;
  for (iConnection = 0; iConnection < pChannel->iConnectionCount;
       ++iConnection)
    free(pChannel->apConnections[iConnection]);
  free(pChannel);
}

tNetConnection *NetChannelAddConnection(tNetChannel *pChannel,
                                        const tNetAddress *pPeer,
                                        uint64 ullSessionToken,
                                        uint8 byGeneration)
{
  tNetConnection *pConnection;
  uint16 unInitial;
  if (!pChannel || !pPeer ||
      (pPeer->byFamily != NET_ADDR_IPV4 &&
       pPeer->byFamily != NET_ADDR_IPV6) ||
      pChannel->iConnectionCount >= NET_MAX_CONNECTIONS)
    return NULL;
  pConnection = (tNetConnection *)calloc(1, sizeof(*pConnection));
  if (!pConnection)
    return NULL;
  unInitial = (uint16)ullSessionToken;
  pConnection->pChannel = pChannel;
  pConnection->peer = *pPeer;
  pConnection->ullSessionToken = ullSessionToken;
  pConnection->byGeneration = byGeneration;
  pConnection->unNextPacketSequence = unInitial ? unInitial : 1;
  pConnection->unNextOrderedSend = unInitial;
  pConnection->unNextOrderedReceive = unInitial;
  pConnection->unNextReliableSend = unInitial;
  pConnection->ullLastReceiveMs = NetChannelNow(pChannel);
  pConnection->ullLastSendMs = pConnection->ullLastReceiveMs;
  pConnection->uiNextEntryId = 1;
  pConnection->uiNextOrder = 1;
  pChannel->apConnections[pChannel->iConnectionCount++] = pConnection;
  return pConnection;
}

void NetChannelSetAcceptCallback(tNetChannel *pChannel,
                                 tNetChannelAcceptFn pAccept,
                                 void *pContext)
{
  if (!pChannel)
    return;
  pChannel->pAccept = pAccept;
  pChannel->pAcceptContext = pAccept ? pContext : NULL;
}

static int NetOrderedWindowAvailable(const tNetConnection *pConnection)
{
  uint16 unOldest = pConnection->unNextOrderedSend;
  int iFound = 0, iEntry;
  for (iEntry = 0; iEntry < NET_RELIABLE_QUEUE; ++iEntry) {
    const tNetOutgoingEntry *pEntry = &pConnection->aOutgoing[iEntry];
    if (pEntry->byActive &&
        (pEntry->byFlags & (NET_MSG_RELIABLE | NET_MSG_ORDERED)) ==
            (NET_MSG_RELIABLE | NET_MSG_ORDERED) &&
        (!iFound || NetSequenceIsNewer(unOldest, pEntry->unReliableSeq))) {
      unOldest = pEntry->unReliableSeq;
      iFound = 1;
    }
  }
  return !iFound || (uint16)(pConnection->unNextOrderedSend - unOldest) <
      NET_ORDER_WINDOW;
}

int NetConnectionQueueMessage(tNetConnection *pConnection, uint8 byType,
                              uint8 byFlags, const void *pData,
                              uint16 unLength)
{
  int iEntry;
  tNetOutgoingEntry *pEntry;
  if (!pConnection || pConnection->byExpired ||
      byType < NET_MSG_PING || byType > NET_MSG_KICK ||
      (byFlags & ~(NET_MSG_RELIABLE | NET_MSG_ORDERED)) ||
      ((byFlags & NET_MSG_ORDERED) && !(byFlags & NET_MSG_RELIABLE)) ||
      unLength > NET_MAX_MESSAGE_SIZE || (unLength && !pData) ||
      ((byFlags & NET_MSG_ORDERED) &&
       !NetOrderedWindowAvailable(pConnection)))
    return 0;
  for (iEntry = 0; iEntry < NET_RELIABLE_QUEUE; ++iEntry)
    if (!pConnection->aOutgoing[iEntry].byActive)
      break;
  if (iEntry == NET_RELIABLE_QUEUE)
    return 0;
  pEntry = &pConnection->aOutgoing[iEntry];
  memset(pEntry, 0, sizeof(*pEntry));
  pEntry->byActive = 1;
  pEntry->byType = byType;
  pEntry->byFlags = byFlags;
  pEntry->unLength = unLength;
  pEntry->uiId = pConnection->uiNextEntryId++;
  pEntry->uiOrder = pConnection->uiNextOrder++;
  if (byFlags & NET_MSG_ORDERED)
    pEntry->unReliableSeq = pConnection->unNextOrderedSend++;
  else if (byFlags & NET_MSG_RELIABLE)
    pEntry->unReliableSeq = pConnection->unNextReliableSend++;
  if (unLength)
    memcpy(pEntry->abData, pData, unLength);
  return 1;
}

int NetConnectionReceiveMessage(tNetConnection *pConnection,
                                tNetMessage *pMessage)
{
  if (!pConnection || !pMessage || !pConnection->iDeliveryCount)
    return 0;
  *pMessage = pConnection->aDelivery[pConnection->iDeliveryHead];
  pConnection->iDeliveryHead =
      (pConnection->iDeliveryHead + 1) % NET_DELIVERY_QUEUE;
  --pConnection->iDeliveryCount;
  NetDrainOrdered(pConnection);
  return 1;
}

uint64 NetChannelNowMs(const tNetChannel *pChannel)
{
  return pChannel ? NetChannelNow(pChannel) : 0;
}

int NetConnectionIsExpired(const tNetConnection *pConnection)
{
  return !pConnection || pConnection->byExpired;
}

int NetConnectionPendingReliable(const tNetConnection *pConnection)
{
  int iCount = 0, iEntry;
  if (!pConnection)
    return 0;
  for (iEntry = 0; iEntry < NET_RELIABLE_QUEUE; ++iEntry)
    iCount += pConnection->aOutgoing[iEntry].byActive &&
        (pConnection->aOutgoing[iEntry].byFlags & NET_MSG_RELIABLE);
  return iCount;
}

int NetConnectionStalePackets(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->iStalePackets : 0;
}

uint64 NetConnectionLastReceiveMs(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->ullLastReceiveMs : 0;
}

uint64 NetConnectionNowMs(const tNetConnection *pConnection)
{
  return pConnection && pConnection->pChannel ?
      NetChannelNow(pConnection->pChannel) : 0;
}

float NetConnectionRttMs(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->fRttMs : 0.0f;
}

float NetConnectionJitterMs(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->fJitterMs : 0.0f;
}

void NetConnectionSetIdentity(tNetConnection *pConnection,
                              uint64 ullSessionToken,
                              uint8 byGeneration)
{
  if (!pConnection)
    return;
  pConnection->ullSessionToken = ullSessionToken;
  pConnection->byGeneration = byGeneration;
}

uint64 NetConnectionSessionToken(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->ullSessionToken : 0;
}

uint8 NetConnectionGeneration(const tNetConnection *pConnection)
{
  return pConnection ? pConnection->byGeneration : 0;
}

int NetConnectionPeer(const tNetConnection *pConnection,
                      tNetAddress *pPeer)
{
  if (!pConnection || !pPeer)
    return 0;
  *pPeer = pConnection->peer;
  return 1;
}

void NetChannelPump(tNetChannel *pChannel)
{
  uint8 abPacket[NET_MAX_PAYLOAD];
  tNetAddress from;
  uint64 ullNowMs;
  int iLength, iConnection;
  if (!pChannel)
    return;
  ullNowMs = NetChannelNow(pChannel);
  while ((iLength = pChannel->transport.pReceive(
              pChannel->transport.pContext, &from, abPacket,
              sizeof(abPacket))) > 0)
    NetProcessPacket(pChannel, &from, abPacket, iLength, ullNowMs);
  for (iConnection = 0; iConnection < pChannel->iConnectionCount;
       ++iConnection)
    NetPumpConnection(pChannel->apConnections[iConnection], ullNowMs);
}

void NetPump(void)
{
  tNetChannel *pChannel;
  for (pChannel = s_pChannels; pChannel; pChannel = pChannel->pNext)
    NetChannelPump(pChannel);
}
