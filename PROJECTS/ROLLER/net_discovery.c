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
  tNetAddress aLocalCandidates[NET_RVZ_MAX_LOCAL_CANDIDATES];
  tNetAddress aPunchCandidates[NET_RVZ_MAX_CANDIDATES];
  tNetAddress tPunchAddress;
  uint64 ullNonce, ullToken, ullNextSendMs, ullNextRefreshMs;
  uint64 ullPunchNonce, ullPunchDeadlineMs, ullNextPunchMs;
  uint64 ullNextPunchRequestMs;
  uint64 ullRelayToken;
  uint32 uiSessionId, uiResolveId, uiPunchSessionId, uiRelayId;
  uint16 unSequence, unListPage;
  int iSessionCount, iLocalCandidateCount, iPunchCandidateCount;
  int iNextPunchCandidate;
  uint8 byHosting, byRegistered, byListing, byListReady, byResolved;
  uint8 byFilterBuild, byPunchHasAnswer;
  eNetPunchState ePunchState;
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

static uint64 NetDiscoveryRead64(const uint8 *pData)
{
  return (uint64)NetDiscoveryRead32(pData) |
      ((uint64)NetDiscoveryRead32(pData + 4) << 32);
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
  uint8 abRequest[sizeof(tRvzRegisterRequest)] = {0};
  int iCandidate;
  NetDiscoveryWrite64(abRequest, pDiscovery->ullNonce);
  NetRendezvousEncodeSessionInfo(abRequest + 8, &pDiscovery->hostInfo);
  abRequest[8 + sizeof(tRvzSessionInfo)] =
      (uint8)pDiscovery->iLocalCandidateCount;
  for (iCandidate = 0; iCandidate < pDiscovery->iLocalCandidateCount;
       ++iCandidate)
    NetRendezvousEncodeCandidate(
        abRequest + 8 + sizeof(tRvzSessionInfo) + 4 +
            iCandidate * sizeof(tRvzCandidate),
        &pDiscovery->aLocalCandidates[iCandidate]);
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

static int NetDiscoveryDecodeCandidateSet(tNetAddress *pCandidates,
                                          int *piCount,
                                          const tNetRendezvousPacket *pPacket,
                                          uint32 *puiSessionId,
                                          uint64 *pullPunchNonce)
{
  uint8 byCount;
  int iCandidate, iByte;
  if (pPacket->unPayloadLength != sizeof(tRvzCandidateSet) ||
      pPacket->pPayload[13] || pPacket->pPayload[14] ||
      pPacket->pPayload[15])
    return 0;
  byCount = pPacket->pPayload[12];
  if (!byCount || byCount > NET_RVZ_MAX_CANDIDATES)
    return 0;
  for (iCandidate = 0; iCandidate < byCount; ++iCandidate) {
    int iPrior;
    if (!NetRendezvousDecodeCandidate(
          &pCandidates[iCandidate],
          pPacket->pPayload + 16 + iCandidate * sizeof(tRvzCandidate)))
      return 0;
    for (iPrior = 0; iPrior < iCandidate; ++iPrior)
      if (NetDiscoveryAddressEqual(
            &pCandidates[iPrior], &pCandidates[iCandidate]))
        return 0;
  }
  for (iByte = 16 + byCount * (int)sizeof(tRvzCandidate);
       iByte < (int)sizeof(tRvzCandidateSet); ++iByte)
    if (pPacket->pPayload[iByte])
      return 0;
  *puiSessionId = NetDiscoveryRead32(pPacket->pPayload);
  *pullPunchNonce = NetDiscoveryRead64(pPacket->pPayload + 4);
  if (!*puiSessionId || !*pullPunchNonce)
    return 0;
  *piCount = byCount;
  return 1;
}

static int NetDiscoverySendPunchPacket(tNetDiscovery *pDiscovery,
                                       const tNetAddress *pPeer,
                                       uint8 byType)
{
  uint8 abPacket[sizeof(tNetPunchPacket)] = {0};
  NetDiscoveryWrite32(abPacket, NET_PUNCH_PROTOCOL_ID);
  NetDiscoveryWrite32(abPacket + 4, pDiscovery->uiPunchSessionId);
  NetDiscoveryWrite64(abPacket + 8, pDiscovery->ullPunchNonce);
  abPacket[16] = byType;
  return NetChannelSendDatagram(pDiscovery->pChannel, pPeer,
                                abPacket, sizeof(abPacket));
}

static void NetDiscoverySendPunchRequest(tNetDiscovery *pDiscovery)
{
  uint8 abRequest[sizeof(tRvzCandidateSet)] = {0};
  int iCandidate;
  NetDiscoveryWrite32(abRequest, pDiscovery->uiPunchSessionId);
  NetDiscoveryWrite64(abRequest + 4, pDiscovery->ullPunchNonce);
  abRequest[12] = (uint8)pDiscovery->iLocalCandidateCount;
  for (iCandidate = 0; iCandidate < pDiscovery->iLocalCandidateCount;
       ++iCandidate)
    NetRendezvousEncodeCandidate(
        abRequest + 16 + iCandidate * sizeof(tRvzCandidate),
        &pDiscovery->aLocalCandidates[iCandidate]);
  NetDiscoverySend(pDiscovery, NET_RVZ_MSG_PUNCH_REQUEST, 0,
                   abRequest, sizeof(abRequest));
}

static void NetDiscoverySendRelayRequest(tNetDiscovery *pDiscovery)
{
  uint8 abRequest[sizeof(tRvzRelayRequest)];
  NetDiscoveryWrite32(abRequest, pDiscovery->uiPunchSessionId);
  NetDiscoveryWrite64(abRequest + 4, pDiscovery->ullPunchNonce);
  NetDiscoverySend(pDiscovery, NET_RVZ_MSG_RELAY_REQUEST, 0,
                   abRequest, sizeof(abRequest));
}

static int NetDiscoveryDecodeRelayAllocation(
    const tNetRendezvousPacket *pPacket, uint32 *puiSessionId,
    uint32 *puiRelayId, uint64 *pullRelayToken, tNetAddress *pPeer)
{
  if (pPacket->unPayloadLength != sizeof(tRvzRelayAllocation) ||
      !NetRendezvousDecodeCandidate(pPeer, pPacket->pPayload + 16))
    return 0;
  *puiSessionId = NetDiscoveryRead32(pPacket->pPayload);
  *puiRelayId = NetDiscoveryRead32(pPacket->pPayload + 4);
  *pullRelayToken = NetDiscoveryRead64(pPacket->pPayload + 8);
  return *puiSessionId && *puiRelayId && *pullRelayToken;
}

static int NetDiscoveryHandleDirect(tNetDiscovery *pDiscovery,
                                    const tNetAddress *pPeer,
                                    const uint8 *pData, int iLength)
{
  uint8 byType;
  if (iLength != sizeof(tNetPunchPacket) ||
      NetDiscoveryRead32(pData) != NET_PUNCH_PROTOCOL_ID ||
      NetDiscoveryRead32(pData + 4) != pDiscovery->uiPunchSessionId ||
      NetDiscoveryRead64(pData + 8) != pDiscovery->ullPunchNonce ||
      pData[17] || pData[18] || pData[19])
    return 0;
  byType = pData[16];
  if (pDiscovery->ePunchState != NET_PUNCH_IN_PROGRESS &&
      pDiscovery->ePunchState != NET_PUNCH_SUCCEEDED)
    return 1;
  if (byType == NET_PUNCH_PROBE)
    NetDiscoverySendPunchPacket(pDiscovery, pPeer, NET_PUNCH_ACK);
  else if (byType != NET_PUNCH_ACK)
    return 1;
  if (pDiscovery->ePunchState == NET_PUNCH_SUCCEEDED)
    return 1;
  pDiscovery->tPunchAddress = *pPeer;
  pDiscovery->ePunchState = NET_PUNCH_SUCCEEDED;
  return 1;
}

static void NetDiscoveryDatagram(void *pContext, const tNetAddress *pPeer,
                                 const void *pData, int iLength)
{
  tNetDiscovery *pDiscovery = (tNetDiscovery *)pContext;
  tNetRendezvousPacket packet;
  tNetAddress aPunchCandidates[NET_RVZ_MAX_CANDIDATES];
  int iPunchCandidateCount;
  uint32 uiPunchSessionId;
  uint64 ullPunchNonce;
  if (!pDiscovery)
    return;
  if (!NetDiscoveryAddressEqual(pPeer, &pDiscovery->rendezvous)) {
    NetDiscoveryHandleDirect(pDiscovery, pPeer,
                             (const uint8 *)pData, iLength);
    return;
  }
  if (!NetRendezvousParsePacket(pData, iLength, &packet))
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
  } else if ((packet.byType == NET_RVZ_MSG_PUNCH_OFFER ||
              packet.byType == NET_RVZ_MSG_PUNCH_ANSWER) &&
             NetDiscoveryDecodeCandidateSet(aPunchCandidates,
                                             &iPunchCandidateCount, &packet,
                                             &uiPunchSessionId,
                                             &ullPunchNonce) &&
             ((packet.byType == NET_RVZ_MSG_PUNCH_OFFER &&
               pDiscovery->byRegistered &&
               packet.ullToken == pDiscovery->ullToken &&
               uiPunchSessionId == pDiscovery->uiSessionId &&
               !(pDiscovery->ePunchState == NET_PUNCH_SUCCEEDED &&
                 ullPunchNonce == pDiscovery->ullPunchNonce)) ||
              (packet.byType == NET_RVZ_MSG_PUNCH_ANSWER &&
               packet.ullToken == 0 &&
               pDiscovery->ePunchState == NET_PUNCH_IN_PROGRESS &&
               uiPunchSessionId == pDiscovery->uiPunchSessionId &&
               ullPunchNonce == pDiscovery->ullPunchNonce))) {
    uint64 ullNowMs = NetChannelNowMs(pDiscovery->pChannel);
    pDiscovery->uiPunchSessionId = uiPunchSessionId;
    pDiscovery->ullPunchNonce = ullPunchNonce;
    memcpy(pDiscovery->aPunchCandidates, aPunchCandidates,
           iPunchCandidateCount * sizeof(tNetAddress));
    pDiscovery->iPunchCandidateCount = iPunchCandidateCount;
    pDiscovery->iNextPunchCandidate = 0;
    pDiscovery->ePunchState = NET_PUNCH_IN_PROGRESS;
    pDiscovery->byPunchHasAnswer = 1;
    pDiscovery->ullPunchDeadlineMs = ullNowMs + NET_PUNCH_TIMEOUT_MS;
    pDiscovery->ullNextPunchMs = ullNowMs;
  } else if ((packet.byType == NET_RVZ_MSG_RELAY_OFFER ||
              packet.byType == NET_RVZ_MSG_RELAY_ALLOCATED)) {
    tNetAddress relayPeer;
    uint32 uiSessionId, uiRelayId;
    uint64 ullRelayToken;
    if (!NetDiscoveryDecodeRelayAllocation(&packet, &uiSessionId,
            &uiRelayId, &ullRelayToken, &relayPeer))
      return;
    if (packet.byType == NET_RVZ_MSG_RELAY_OFFER) {
      if (!pDiscovery->byRegistered ||
          packet.ullToken != pDiscovery->ullToken ||
          uiSessionId != pDiscovery->uiSessionId)
        return;
    } else if (packet.ullToken ||
               pDiscovery->ePunchState != NET_PUNCH_RELAY_IN_PROGRESS ||
               uiSessionId != pDiscovery->uiPunchSessionId)
      return;
    if (!NetChannelSetRelayRoute(pDiscovery->pChannel, &relayPeer,
            &pDiscovery->rendezvous, uiRelayId, ullRelayToken))
      return;
    if (packet.byType == NET_RVZ_MSG_RELAY_ALLOCATED) {
      pDiscovery->uiRelayId = uiRelayId;
      pDiscovery->ullRelayToken = ullRelayToken;
      pDiscovery->tPunchAddress = relayPeer;
      pDiscovery->ePunchState = NET_PUNCH_RELAY_SUCCEEDED;
    }
  } else if (packet.byType == NET_RVZ_MSG_RELAY_THROTTLED &&
             !packet.ullToken &&
             packet.unPayloadLength == sizeof(tRvzRelayThrottle) &&
             !packet.pPayload[5] && !packet.pPayload[6] &&
             !packet.pPayload[7] && packet.pPayload[4] <= 1 &&
             (pDiscovery->byHosting ||
              NetDiscoveryRead32(packet.pPayload) ==
                  pDiscovery->uiRelayId)) {
    pDiscovery->ePunchState = NET_PUNCH_RELAY_THROTTLED;
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
  NetChannelClearRelayRoutes(pDiscovery->pChannel);
  NetChannelSetDatagramCallback(pDiscovery->pChannel, NULL, NULL);
  free(pDiscovery);
}

int NetDiscoverySetLocalCandidates(tNetDiscovery *pDiscovery,
                                   const tNetAddress *pCandidates,
                                   int iCount)
{
  tNetAddress aValidated[NET_RVZ_MAX_LOCAL_CANDIDATES];
  uint8 abWire[sizeof(tRvzCandidate)];
  int iCandidate, iPrior;
  if (!pDiscovery || iCount < 0 ||
      iCount > NET_RVZ_MAX_LOCAL_CANDIDATES ||
      (iCount && !pCandidates) || pDiscovery->byHosting)
    return 0;
  for (iCandidate = 0; iCandidate < iCount; ++iCandidate) {
    NetRendezvousEncodeCandidate(abWire, &pCandidates[iCandidate]);
    if (!NetRendezvousDecodeCandidate(&aValidated[iCandidate], abWire))
      return 0;
    for (iPrior = 0; iPrior < iCandidate; ++iPrior)
      if (NetDiscoveryAddressEqual(&aValidated[iPrior],
                                   &aValidated[iCandidate]))
        return 0;
  }
  if (iCount)
    memcpy(pDiscovery->aLocalCandidates, aValidated,
           iCount * sizeof(tNetAddress));
  pDiscovery->iLocalCandidateCount = iCount;
  return 1;
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

int NetDiscoveryPunch(tNetDiscovery *pDiscovery, uint32 uiSessionId)
{
  uint64 ullNowMs;
  if (!pDiscovery || !uiSessionId || pDiscovery->byHosting ||
      !pDiscovery->pRandom(pDiscovery->pRandomContext,
                           &pDiscovery->ullPunchNonce,
                           sizeof(pDiscovery->ullPunchNonce)) ||
      !pDiscovery->ullPunchNonce)
    return 0;
  ullNowMs = NetChannelNowMs(pDiscovery->pChannel);
  NetChannelClearRelayRoutes(pDiscovery->pChannel);
  pDiscovery->uiPunchSessionId = uiSessionId;
  pDiscovery->ePunchState = NET_PUNCH_IN_PROGRESS;
  pDiscovery->byPunchHasAnswer = 0;
  pDiscovery->iPunchCandidateCount = 0;
  pDiscovery->iNextPunchCandidate = 0;
  pDiscovery->ullPunchDeadlineMs = ullNowMs + NET_PUNCH_TIMEOUT_MS;
  pDiscovery->ullNextPunchRequestMs = ullNowMs +
      NET_PUNCH_REQUEST_RETRY_MS;
  NetDiscoverySendPunchRequest(pDiscovery);
  return 1;
}

eNetPunchState NetDiscoveryPunchState(const tNetDiscovery *pDiscovery,
                                      tNetAddress *pAddress)
{
  if (!pDiscovery)
    return NET_PUNCH_IDLE;
  if (pAddress && pDiscovery->ePunchState == NET_PUNCH_SUCCEEDED)
    *pAddress = pDiscovery->tPunchAddress;
  if (pAddress && (pDiscovery->ePunchState == NET_PUNCH_RELAY_SUCCEEDED ||
                   pDiscovery->ePunchState == NET_PUNCH_RELAY_THROTTLED))
    *pAddress = pDiscovery->tPunchAddress;
  return pDiscovery->ePunchState;
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
  if (pDiscovery->ePunchState == NET_PUNCH_IN_PROGRESS) {
    if (ullNowMs >= pDiscovery->ullPunchDeadlineMs) {
      pDiscovery->ePunchState = NET_PUNCH_RELAY_IN_PROGRESS;
      NetDiscoverySendRelayRequest(pDiscovery);
      pDiscovery->ullNextPunchRequestMs = ullNowMs +
          NET_RELAY_REQUEST_RETRY_MS;
    } else if (!pDiscovery->byPunchHasAnswer &&
               ullNowMs >= pDiscovery->ullNextPunchRequestMs) {
      NetDiscoverySendPunchRequest(pDiscovery);
      pDiscovery->ullNextPunchRequestMs = ullNowMs +
          NET_PUNCH_REQUEST_RETRY_MS;
    }
    if (pDiscovery->ePunchState == NET_PUNCH_IN_PROGRESS &&
        pDiscovery->byPunchHasAnswer &&
        pDiscovery->iPunchCandidateCount &&
        ullNowMs >= pDiscovery->ullNextPunchMs) {
      NetDiscoverySendPunchPacket(pDiscovery,
          &pDiscovery->aPunchCandidates[pDiscovery->iNextPunchCandidate],
          NET_PUNCH_PROBE);
      pDiscovery->iNextPunchCandidate =
          (pDiscovery->iNextPunchCandidate + 1) %
          pDiscovery->iPunchCandidateCount;
      pDiscovery->ullNextPunchMs = ullNowMs + NET_PUNCH_RETRY_MS;
    }
  }
  if (pDiscovery->ePunchState == NET_PUNCH_RELAY_IN_PROGRESS &&
      ullNowMs >= pDiscovery->ullNextPunchRequestMs) {
    NetDiscoverySendRelayRequest(pDiscovery);
    pDiscovery->ullNextPunchRequestMs = ullNowMs +
        NET_RELAY_REQUEST_RETRY_MS;
  }
}
