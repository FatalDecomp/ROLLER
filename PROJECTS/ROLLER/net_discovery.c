#include "net_discovery.h"

#include <stdlib.h>
#include <string.h>

#define NET_DISCOVERY_RETRY_MS 1000
#define NET_DISCOVERY_REFRESH_MS 5000

struct tNetDiscovery {
  tNetChannel *pChannel;
  tNetAddress rendezvous, resolved;
  tNetRendezvousRandomFn pRandom;
  void *pRandomContext;
  tRvzSessionInfo hostInfo;
  tRvzSessionInfo aSessions[NET_RVZ_MAX_SESSIONS];
  uint64 ullNonce, ullToken, ullNextSendMs, ullNextRefreshMs;
  uint32 uiSessionId, uiResolveId;
  uint16 unSequence, unListPage;
  int iSessionCount;
  uint8 byHosting, byRegistered, byListing, byListReady, byResolved;
  uint8 byFilterBuild;
  char szBuildHash[16];
};

static void NetDiscoveryWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetDiscoveryWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static uint16 NetDiscoveryRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetDiscoveryRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static int NetDiscoveryAddressEqual(const tNetAddress *pA,
                                    const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort || pA->uiScopeId != pB->uiScopeId)
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 :
      (pA->byFamily == NET_ADDR_IPV6 ? 16 : 0);
  return iLength && memcmp(pA->abAddress, pB->abAddress,
                           (size_t)iLength) == 0;
}

static void NetDiscoveryWrite64(uint8 *pData, uint64 ullValue)
{
  NetDiscoveryWrite32(pData, (uint32)ullValue);
  NetDiscoveryWrite32(pData + 4, (uint32)(ullValue >> 32));
}

static int NetDiscoverySend(tNetDiscovery *pDiscovery, uint8 byType,
                            uint64 ullToken, const void *pPayload,
                            uint16 unLength)
{
  uint8 abPacket[NET_MAX_PAYLOAD];
  int iLength = NetRendezvousBuildPacket(abPacket, sizeof(abPacket),
      ++pDiscovery->unSequence, ullToken, byType, pPayload, unLength);
  return iLength && NetChannelSendDatagram(pDiscovery->pChannel,
      &pDiscovery->rendezvous, abPacket, iLength);
}

static void NetDiscoverySendRegister(tNetDiscovery *pDiscovery)
{
  uint8 abRequest[sizeof(tRvzRegisterRequest)];
  NetDiscoveryWrite64(abRequest, pDiscovery->ullNonce);
  NetRendezvousEncodeSessionInfo(abRequest + 8, &pDiscovery->hostInfo);
  NetDiscoverySend(pDiscovery, NET_RVZ_MSG_REGISTER, 0,
                   abRequest, sizeof(abRequest));
}

static void NetDiscoverySendHeartbeat(tNetDiscovery *pDiscovery)
{
  uint8 abInfo[sizeof(tRvzSessionInfo)];
  pDiscovery->hostInfo.uiSessionId = pDiscovery->uiSessionId;
  NetRendezvousEncodeSessionInfo(abInfo, &pDiscovery->hostInfo);
  NetDiscoverySend(pDiscovery, NET_RVZ_MSG_HEARTBEAT,
                   pDiscovery->ullToken, abInfo, sizeof(abInfo));
}

static void NetDiscoverySendList(tNetDiscovery *pDiscovery)
{
  uint8 abRequest[sizeof(tRvzListRequest)] = {0};
  NetDiscoveryWrite16(abRequest, pDiscovery->unListPage);
  abRequest[2] = pDiscovery->byFilterBuild;
  memcpy(abRequest + 4, pDiscovery->szBuildHash, 16);
  NetDiscoverySend(pDiscovery, NET_RVZ_MSG_LIST, 0,
                   abRequest, sizeof(abRequest));
}

static void NetDiscoveryDatagram(void *pContext, const tNetAddress *pPeer,
                                 const void *pData, int iLength)
{
  tNetDiscovery *pDiscovery = (tNetDiscovery *)pContext;
  tNetRendezvousPacket packet;
  if (!pDiscovery || !NetDiscoveryAddressEqual(
          pPeer, &pDiscovery->rendezvous) ||
      !NetRendezvousParsePacket(pData, iLength, &packet))
    return;
  if (packet.byType == NET_RVZ_MSG_REGISTERED && pDiscovery->byHosting &&
      packet.unPayloadLength == sizeof(tRvzAck) && packet.ullToken) {
    pDiscovery->uiSessionId = NetDiscoveryRead32(packet.pPayload);
    pDiscovery->ullToken = packet.ullToken;
    pDiscovery->byRegistered = pDiscovery->uiSessionId != 0;
    pDiscovery->ullNextSendMs = NetChannelNowMs(pDiscovery->pChannel) +
        NET_RVZ_HEARTBEAT_MS;
  } else if (packet.byType == NET_RVZ_MSG_ACK &&
             pDiscovery->byRegistered &&
             packet.unPayloadLength == sizeof(tRvzAck) &&
             NetDiscoveryRead32(packet.pPayload) == pDiscovery->uiSessionId) {
    pDiscovery->ullNextSendMs = NetChannelNowMs(pDiscovery->pChannel) +
        NET_RVZ_HEARTBEAT_MS;
  } else if (packet.byType == NET_RVZ_MSG_LIST_PAGE &&
             pDiscovery->byListing && packet.ullToken == 0 &&
             packet.unPayloadLength >= sizeof(tRvzListPageHeader)) {
    uint16 unPage = NetDiscoveryRead16(packet.pPayload);
    uint16 unPages = NetDiscoveryRead16(packet.pPayload + 2);
    uint16 unTotal = NetDiscoveryRead16(packet.pPayload + 4);
    uint8 byCount = packet.pPayload[6], byMore = packet.pPayload[7];
    int iEntry;
    if (unPage != pDiscovery->unListPage || unPages > 43 ||
        unTotal > NET_RVZ_MAX_SESSIONS || byCount > NET_RVZ_MAX_PAGE_SESSIONS ||
        packet.unPayloadLength != sizeof(tRvzListPageHeader) +
            byCount * sizeof(tRvzSessionInfo) ||
        byMore != (uint8)(unPage + 1 < unPages) ||
        pDiscovery->iSessionCount + byCount > NET_RVZ_MAX_SESSIONS)
      return;
    for (iEntry = 0; iEntry < byCount; ++iEntry)
      NetRendezvousDecodeSessionInfo(
          &pDiscovery->aSessions[pDiscovery->iSessionCount++],
          packet.pPayload + sizeof(tRvzListPageHeader) +
              iEntry * sizeof(tRvzSessionInfo));
    if (byMore) {
      ++pDiscovery->unListPage;
      NetDiscoverySendList(pDiscovery);
    } else {
      pDiscovery->byListing = 0;
      pDiscovery->byListReady = 1;
      pDiscovery->ullNextRefreshMs = NetChannelNowMs(pDiscovery->pChannel) +
          NET_DISCOVERY_REFRESH_MS;
    }
  } else if (packet.byType == NET_RVZ_MSG_RESOLVED &&
             packet.unPayloadLength == sizeof(tRvzResolved) &&
             NetDiscoveryRead32(packet.pPayload) == pDiscovery->uiResolveId &&
             (packet.pPayload[4] == NET_ADDR_IPV4 ||
              packet.pPayload[4] == NET_ADDR_IPV6)) {
    memset(&pDiscovery->resolved, 0, sizeof(pDiscovery->resolved));
    pDiscovery->resolved.byFamily = packet.pPayload[4];
    pDiscovery->resolved.unPort = NetDiscoveryRead16(packet.pPayload + 8);
    pDiscovery->resolved.uiScopeId = NetDiscoveryRead32(packet.pPayload + 12);
    memcpy(pDiscovery->resolved.abAddress, packet.pPayload + 16, 16);
    pDiscovery->byResolved = pDiscovery->resolved.unPort != 0;
  }
}

tNetDiscovery *NetDiscoveryCreate(tNetChannel *pChannel,
                                  const tNetAddress *pRendezvous,
                                  tNetRendezvousRandomFn pRandom,
                                  void *pRandomContext)
{
  tNetDiscovery *pDiscovery;
  if (!pChannel || !pRendezvous || !pRandom)
    return NULL;
  pDiscovery = (tNetDiscovery *)calloc(1, sizeof(*pDiscovery));
  if (!pDiscovery)
    return NULL;
  pDiscovery->pChannel = pChannel;
  pDiscovery->rendezvous = *pRendezvous;
  pDiscovery->pRandom = pRandom;
  pDiscovery->pRandomContext = pRandomContext;
  NetChannelSetDatagramCallback(pChannel, NetDiscoveryDatagram, pDiscovery);
  return pDiscovery;
}

void NetDiscoveryDestroy(tNetDiscovery *pDiscovery)
{
  if (!pDiscovery)
    return;
  NetDiscoveryHostStop(pDiscovery);
  NetChannelSetDatagramCallback(pDiscovery->pChannel, NULL, NULL);
  free(pDiscovery);
}

int NetDiscoveryHostStart(tNetDiscovery *pDiscovery,
                          const tRvzSessionInfo *pInfo)
{
  if (!pDiscovery || !pInfo || !pDiscovery->pRandom(
          pDiscovery->pRandomContext, &pDiscovery->ullNonce,
          sizeof(pDiscovery->ullNonce)) || !pDiscovery->ullNonce)
    return 0;
  pDiscovery->hostInfo = *pInfo;
  pDiscovery->hostInfo.uiSessionId = 0;
  pDiscovery->byHosting = 1;
  NetDiscoverySendRegister(pDiscovery);
  pDiscovery->ullNextSendMs = NetChannelNowMs(pDiscovery->pChannel) +
      NET_DISCOVERY_RETRY_MS;
  return 1;
}

void NetDiscoveryHostUpdate(tNetDiscovery *pDiscovery,
                            const tRvzSessionInfo *pInfo)
{
  if (pDiscovery && pInfo) {
    uint32 uiSessionId = pDiscovery->uiSessionId;
    pDiscovery->hostInfo = *pInfo;
    pDiscovery->hostInfo.uiSessionId = uiSessionId;
  }
}

void NetDiscoveryHostStop(tNetDiscovery *pDiscovery)
{
  uint8 abRequest[4];
  if (!pDiscovery || !pDiscovery->byHosting)
    return;
  if (pDiscovery->byRegistered) {
    NetDiscoveryWrite32(abRequest, pDiscovery->uiSessionId);
    NetDiscoverySend(pDiscovery, NET_RVZ_MSG_UNREGISTER,
                     pDiscovery->ullToken, abRequest, sizeof(abRequest));
  }
  pDiscovery->byHosting = pDiscovery->byRegistered = 0;
}

int NetDiscoveryHostRegistered(const tNetDiscovery *pDiscovery,
                               uint32 *puiSessionId)
{
  if (!pDiscovery || !pDiscovery->byRegistered)
    return 0;
  if (puiSessionId)
    *puiSessionId = pDiscovery->uiSessionId;
  return 1;
}

int NetDiscoveryList(tNetDiscovery *pDiscovery, const char *szBuildHash)
{
  if (!pDiscovery)
    return 0;
  pDiscovery->iSessionCount = 0;
  pDiscovery->unListPage = 0;
  pDiscovery->byListing = 1;
  pDiscovery->byListReady = 0;
  memset(pDiscovery->szBuildHash, 0, sizeof(pDiscovery->szBuildHash));
  pDiscovery->byFilterBuild = szBuildHash != NULL;
  if (szBuildHash) {
    int iChar;
    for (iChar = 0; iChar < 16 && szBuildHash[iChar]; ++iChar)
      pDiscovery->szBuildHash[iChar] = szBuildHash[iChar];
  }
  NetDiscoverySendList(pDiscovery);
  return 1;
}

int NetDiscoveryListReady(const tNetDiscovery *pDiscovery)
{ return pDiscovery && pDiscovery->byListReady; }
int NetDiscoverySessionCount(const tNetDiscovery *pDiscovery)
{ return pDiscovery ? pDiscovery->iSessionCount : 0; }
int NetDiscoverySession(const tNetDiscovery *pDiscovery, int iIndex,
                        tRvzSessionInfo *pInfo)
{
  if (!pDiscovery || !pInfo || iIndex < 0 ||
      iIndex >= pDiscovery->iSessionCount)
    return 0;
  *pInfo = pDiscovery->aSessions[iIndex];
  return 1;
}

int NetDiscoveryResolve(tNetDiscovery *pDiscovery, uint32 uiSessionId)
{
  uint8 abRequest[4];
  if (!pDiscovery || !uiSessionId)
    return 0;
  pDiscovery->uiResolveId = uiSessionId;
  pDiscovery->byResolved = 0;
  NetDiscoveryWrite32(abRequest, uiSessionId);
  return NetDiscoverySend(pDiscovery, NET_RVZ_MSG_RESOLVE, 0,
                          abRequest, sizeof(abRequest));
}

int NetDiscoveryResolved(tNetDiscovery *pDiscovery, uint32 uiSessionId,
                         tNetAddress *pAddress)
{
  if (!pDiscovery || !pAddress || !pDiscovery->byResolved ||
      uiSessionId != pDiscovery->uiResolveId)
    return 0;
  *pAddress = pDiscovery->resolved;
  return 1;
}

void NetDiscoveryPump(tNetDiscovery *pDiscovery)
{
  uint64 ullNowMs;
  if (!pDiscovery)
    return;
  ullNowMs = NetChannelNowMs(pDiscovery->pChannel);
  if (pDiscovery->byHosting && ullNowMs >= pDiscovery->ullNextSendMs) {
    if (pDiscovery->byRegistered)
      NetDiscoverySendHeartbeat(pDiscovery);
    else
      NetDiscoverySendRegister(pDiscovery);
    pDiscovery->ullNextSendMs = ullNowMs +
        (pDiscovery->byRegistered ? NET_RVZ_HEARTBEAT_MS :
                                    NET_DISCOVERY_RETRY_MS);
  }
  if (pDiscovery->byListReady && ullNowMs >= pDiscovery->ullNextRefreshMs) {
    char szBuildHash[16];
    const char *szFilter = NULL;
    if (pDiscovery->byFilterBuild) {
      memcpy(szBuildHash, pDiscovery->szBuildHash, sizeof(szBuildHash));
      szFilter = szBuildHash;
    }
    NetDiscoveryList(pDiscovery, szFilter);
  }
}
