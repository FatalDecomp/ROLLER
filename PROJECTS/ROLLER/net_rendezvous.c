#include "net_rendezvous.h"

#include <stdlib.h>
#include <string.h>

#define NET_RVZ_RATE_SOURCES 1024
#define NET_RVZ_RATE_IDLE_MS 60000
#define NET_RVZ_MAX_PUMP_PACKETS 256

typedef struct {
  uint8 byActive;
  tNetAddress source;
  uint64 ullRegistrationNonce, ullToken, ullExpiresMs;
  tRvzSessionInfo info;
  uint8 byCandidateCount;
  tNetAddress aCandidates[NET_RVZ_MAX_LOCAL_CANDIDATES];
} tNetRendezvousSession;

typedef struct {
  uint8 byActive;
  tNetAddress source;
  uint64 ullLastMs, ullLastSeenMs, ullMilliTokens;
} tNetRendezvousRate;

struct tNetRendezvous {
  tNetTransport transport;
  tNetRendezvousRandomFn pRandom;
  void *pRandomContext;
  tNetRendezvousSession aSessions[NET_RVZ_MAX_SESSIONS];
  tNetRendezvousRate aRates[NET_RVZ_RATE_SOURCES];
  tNetRendezvousStats stats;
};

static void NetRvzWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetRvzWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static void NetRvzWrite64(uint8 *pData, uint64 ullValue)
{
  NetRvzWrite32(pData, (uint32)ullValue);
  NetRvzWrite32(pData + 4, (uint32)(ullValue >> 32));
}

static uint16 NetRvzRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetRvzRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static uint64 NetRvzRead64(const uint8 *pData)
{
  return (uint64)NetRvzRead32(pData) |
      ((uint64)NetRvzRead32(pData + 4) << 32);
}

void NetRendezvousEncodeSessionInfo(uint8 *pWire,
                                    const tRvzSessionInfo *pInfo)
{
  NetRvzWrite32(pWire, pInfo->uiSessionId);
  NetRvzWrite16(pWire + 4, pInfo->unPort);
  NetRvzWrite16(pWire + 6, pInfo->unTickRateHz);
  pWire[8] = pInfo->byPlayers;
  pWire[9] = pInfo->byMaxPlayers;
  pWire[10] = pInfo->byFlags;
  memcpy(pWire + 11, pInfo->szName, sizeof(pInfo->szName));
  memcpy(pWire + 43, pInfo->szTrack, sizeof(pInfo->szTrack));
  memcpy(pWire + 69, pInfo->szBuildHash, sizeof(pInfo->szBuildHash));
}

void NetRendezvousDecodeSessionInfo(tRvzSessionInfo *pInfo,
                                    const uint8 *pWire)
{
  memset(pInfo, 0, sizeof(*pInfo));
  pInfo->uiSessionId = NetRvzRead32(pWire);
  pInfo->unPort = NetRvzRead16(pWire + 4);
  pInfo->unTickRateHz = NetRvzRead16(pWire + 6);
  pInfo->byPlayers = pWire[8];
  pInfo->byMaxPlayers = pWire[9];
  pInfo->byFlags = pWire[10];
  memcpy(pInfo->szName, pWire + 11, sizeof(pInfo->szName));
  memcpy(pInfo->szTrack, pWire + 43, sizeof(pInfo->szTrack));
  memcpy(pInfo->szBuildHash, pWire + 69, sizeof(pInfo->szBuildHash));
}

void NetRendezvousEncodeCandidate(uint8 *pWire,
                                  const tNetAddress *pAddress)
{
  memset(pWire, 0, sizeof(tRvzCandidate));
  pWire[0] = pAddress->byFamily;
  NetRvzWrite16(pWire + 2, pAddress->unPort);
  NetRvzWrite32(pWire + 4, pAddress->uiScopeId);
  memcpy(pWire + 8, pAddress->abAddress, 16);
}

int NetRendezvousDecodeCandidate(tNetAddress *pAddress,
                                 const uint8 *pWire)
{
  int iByte;
  if (!pAddress || !pWire || pWire[1] ||
      (pWire[0] != NET_ADDR_IPV4 && pWire[0] != NET_ADDR_IPV6) ||
      !NetRvzRead16(pWire + 2) ||
      (pWire[0] == NET_ADDR_IPV4 && NetRvzRead32(pWire + 4)))
    return 0;
  if (pWire[0] == NET_ADDR_IPV4)
    for (iByte = 12; iByte < 24; ++iByte)
      if (pWire[iByte])
        return 0;
  memset(pAddress, 0, sizeof(*pAddress));
  pAddress->byFamily = pWire[0];
  pAddress->unPort = NetRvzRead16(pWire + 2);
  pAddress->uiScopeId = NetRvzRead32(pWire + 4);
  memcpy(pAddress->abAddress, pWire + 8, 16);
  return 1;
}

int NetRendezvousBuildPacket(uint8 *pPacket, int iCapacity,
                             uint16 unSequence, uint64 ullToken,
                             uint8 byType, const void *pPayload,
                             uint16 unPayloadLength)
{
  int iLength = (int)sizeof(tNetPacketHeader) +
      (int)sizeof(tNetMessageHeader) + unPayloadLength;
  if (!pPacket || iCapacity < iLength || iLength > NET_MAX_PAYLOAD ||
      (!pPayload && unPayloadLength) || !byType)
    return 0;
  memset(pPacket, 0, (size_t)iLength);
  NetRvzWrite32(pPacket, NET_RVZ_PROTOCOL_ID);
  NetRvzWrite16(pPacket + 4, unSequence);
  NetRvzWrite64(pPacket + 12, ullToken);
  pPacket[20] = NET_RVZ_PROTOCOL_VERSION;
  pPacket[21] = 1;
  pPacket[22] = byType;
  NetRvzWrite16(pPacket + 24, unPayloadLength);
  if (unPayloadLength)
    memcpy(pPacket + 28, pPayload, unPayloadLength);
  return iLength;
}

int NetRendezvousParsePacket(const void *pData, int iLength,
                             tNetRendezvousPacket *pDecoded)
{
  const uint8 *pPacket = (const uint8 *)pData;
  uint16 unPayloadLength;
  if (!pPacket || !pDecoded ||
      iLength < (int)(sizeof(tNetPacketHeader) + sizeof(tNetMessageHeader)) ||
      iLength > NET_MAX_PAYLOAD ||
      NetRvzRead32(pPacket) != NET_RVZ_PROTOCOL_ID ||
      pPacket[20] != NET_RVZ_PROTOCOL_VERSION || pPacket[21] != 1 ||
      NetRvzRead16(pPacket + 6) || NetRvzRead32(pPacket + 8) ||
      !pPacket[22] || pPacket[23] || NetRvzRead16(pPacket + 26))
    return 0;
  unPayloadLength = NetRvzRead16(pPacket + 24);
  if (iLength != (int)(sizeof(tNetPacketHeader) +
                       sizeof(tNetMessageHeader) + unPayloadLength))
    return 0;
  pDecoded->unSequence = NetRvzRead16(pPacket + 4);
  pDecoded->ullToken = NetRvzRead64(pPacket + 12);
  pDecoded->byType = pPacket[22];
  pDecoded->pPayload = pPacket + 28;
  pDecoded->unPayloadLength = unPayloadLength;
  return 1;
}

static int NetRvzIpEqual(const tNetAddress *pA, const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily)
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 :
      (pA->byFamily == NET_ADDR_IPV6 ? 16 : 0);
  return iLength && memcmp(pA->abAddress, pB->abAddress,
                           (size_t)iLength) == 0;
}

static int NetRvzTextValid(const char *szText, int iLength, int iAllowFull)
{
  int iChar, iTerminated = 0;
  if (!szText || !iLength || !szText[0])
    return 0;
  for (iChar = 0; iChar < iLength; ++iChar) {
    unsigned char byChar = (unsigned char)szText[iChar];
    if (!byChar) {
      iTerminated = 1;
      continue;
    }
    if (iTerminated || byChar < 32 || byChar > 126)
      return 0;
  }
  return iAllowFull || iTerminated;
}

static int NetRvzInfoValid(const tRvzSessionInfo *pInfo,
                           int iRegistration)
{
  if (!pInfo || (iRegistration && pInfo->uiSessionId) ||
      (pInfo->unTickRateHz != 36 && pInfo->unTickRateHz != 50 &&
       pInfo->unTickRateHz != 100) ||
      !pInfo->byMaxPlayers || pInfo->byMaxPlayers > MAX_CARS ||
      !pInfo->byPlayers || pInfo->byPlayers > pInfo->byMaxPlayers ||
      (pInfo->byFlags & ~NET_RVZ_SESSION_FLAGS) ||
      !NetRvzTextValid(pInfo->szName, sizeof(pInfo->szName), 0) ||
      !NetRvzTextValid(pInfo->szTrack, sizeof(pInfo->szTrack), 0) ||
      !NetRvzTextValid(pInfo->szBuildHash, sizeof(pInfo->szBuildHash), 1))
    return 0;
  return 1;
}

static int NetRvzSourceValid(const tNetAddress *pSource)
{
  return pSource && pSource->unPort &&
      (pSource->byFamily == NET_ADDR_IPV4 ||
       pSource->byFamily == NET_ADDR_IPV6);
}

static int NetRvzAddressEqual(const tNetAddress *pA,
                              const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort || pA->uiScopeId != pB->uiScopeId)
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 : 16;
  return memcmp(pA->abAddress, pB->abAddress, (size_t)iLength) == 0;
}

static int NetRvzDecodeCandidates(tNetAddress *pCandidates, int iCapacity,
                                  const uint8 *pWire, int iCount)
{
  int iCandidate, iPrior;
  if (iCount < 0 || iCount > iCapacity)
    return 0;
  for (iCandidate = 0; iCandidate < iCount; ++iCandidate) {
    if (!NetRendezvousDecodeCandidate(&pCandidates[iCandidate],
          pWire + iCandidate * sizeof(tRvzCandidate)))
      return 0;
    for (iPrior = 0; iPrior < iCandidate; ++iPrior)
      if (NetRvzAddressEqual(&pCandidates[iPrior],
                             &pCandidates[iCandidate]))
        return 0;
  }
  return 1;
}

static int NetRvzAppendCandidate(tNetAddress *pCandidates, int iCount,
                                 int iCapacity,
                                 const tNetAddress *pCandidate)
{
  int iCandidate;
  for (iCandidate = 0; iCandidate < iCount; ++iCandidate)
    if (NetRvzAddressEqual(&pCandidates[iCandidate], pCandidate))
      return iCount;
  if (iCount < iCapacity)
    pCandidates[iCount++] = *pCandidate;
  return iCount;
}

static void NetRvzDeactivateSession(tNetRendezvous *pRendezvous,
                                    tNetRendezvousSession *pSession)
{
  if (!pSession->byActive)
    return;
  memset(pSession, 0, sizeof(*pSession));
  --pRendezvous->stats.iActiveSessions;
}

static void NetRvzExpire(tNetRendezvous *pRendezvous, uint64 ullNowMs)
{
  int iSession;
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession) {
    tNetRendezvousSession *pSession = &pRendezvous->aSessions[iSession];
    if (pSession->byActive && ullNowMs >= pSession->ullExpiresMs) {
      NetRvzDeactivateSession(pRendezvous, pSession);
      ++pRendezvous->stats.ullExpiredSessions;
    }
  }
}

static int NetRvzRateAllow(tNetRendezvous *pRendezvous,
                           const tNetAddress *pSource, uint64 ullNowMs)
{
  tNetRendezvousRate *pRate = NULL, *pFree = NULL, *pOldest = NULL;
  int iRate;
  for (iRate = 0; iRate < NET_RVZ_RATE_SOURCES; ++iRate) {
    tNetRendezvousRate *pCandidate = &pRendezvous->aRates[iRate];
    if (pCandidate->byActive && NetRvzIpEqual(&pCandidate->source, pSource)) {
      pRate = pCandidate;
      break;
    }
    if (!pCandidate->byActive && !pFree)
      pFree = pCandidate;
    if (pCandidate->byActive &&
        (!pOldest || pCandidate->ullLastSeenMs < pOldest->ullLastSeenMs))
      pOldest = pCandidate;
  }
  if (!pRate) {
    pRate = pFree;
    if (!pRate && pOldest &&
        ullNowMs - pOldest->ullLastSeenMs >= NET_RVZ_RATE_IDLE_MS)
      pRate = pOldest;
    if (!pRate)
      return 0;
    if (!pRate->byActive) {
      ++pRendezvous->stats.iRateSources;
      if (pRendezvous->stats.iRateSources >
          pRendezvous->stats.iRateSourceHighWater)
        pRendezvous->stats.iRateSourceHighWater =
            pRendezvous->stats.iRateSources;
    }
    memset(pRate, 0, sizeof(*pRate));
    pRate->byActive = 1;
    pRate->source = *pSource;
    pRate->ullLastMs = ullNowMs;
    pRate->ullMilliTokens = NET_RVZ_CONTROL_BURST * 1000ull;
  } else if (ullNowMs > pRate->ullLastMs) {
    uint64 ullAdded = (ullNowMs - pRate->ullLastMs) *
        NET_RVZ_CONTROL_PACKETS_PER_SECOND;
    pRate->ullMilliTokens += ullAdded;
    if (pRate->ullMilliTokens > NET_RVZ_CONTROL_BURST * 1000ull)
      pRate->ullMilliTokens = NET_RVZ_CONTROL_BURST * 1000ull;
    pRate->ullLastMs = ullNowMs;
  }
  pRate->ullLastSeenMs = ullNowMs;
  if (pRate->ullMilliTokens < 1000) {
    ++pRendezvous->stats.ullRateLimitedPackets;
    return 0;
  }
  pRate->ullMilliTokens -= 1000;
  return 1;
}

static int NetRvzSend(tNetRendezvous *pRendezvous,
                      const tNetAddress *pDestination, uint16 unSequence,
                      uint64 ullToken, uint8 byType, const void *pPayload,
                      uint16 unPayloadLength)
{
  uint8 abPacket[NET_MAX_PAYLOAD];
  int iLength = NetRendezvousBuildPacket(abPacket, sizeof(abPacket),
      unSequence, ullToken, byType, pPayload, unPayloadLength);
  if (!iLength || pRendezvous->transport.pSend(
          pRendezvous->transport.pContext, pDestination, abPacket,
          iLength) != iLength)
    return 0;
  ++pRendezvous->stats.ullPacketsSent;
  return 1;
}

static void NetRvzSendError(tNetRendezvous *pRendezvous,
                            const tNetAddress *pDestination,
                            uint16 unSequence, uint8 byReason)
{
  uint8 abError[sizeof(tRvzError)] = {byReason, 0, 0, 0};
  NetRvzSend(pRendezvous, pDestination, unSequence, 0,
             NET_RVZ_MSG_ERROR, abError, sizeof(abError));
}

static tNetRendezvousSession *NetRvzFindSession(
    tNetRendezvous *pRendezvous, uint32 uiSessionId)
{
  int iSession;
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession)
    if (pRendezvous->aSessions[iSession].byActive &&
        pRendezvous->aSessions[iSession].info.uiSessionId == uiSessionId)
      return &pRendezvous->aSessions[iSession];
  return NULL;
}

static tNetRendezvousSession *NetRvzFindRegistration(
    tNetRendezvous *pRendezvous, const tNetAddress *pSource,
    uint64 ullRegistrationNonce)
{
  int iSession;
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession) {
    tNetRendezvousSession *pSession = &pRendezvous->aSessions[iSession];
    if (pSession->byActive &&
        pSession->ullRegistrationNonce == ullRegistrationNonce &&
        NetRvzIpEqual(&pSession->source, pSource))
      return pSession;
  }
  return NULL;
}

static int NetRvzSessionsForIp(const tNetRendezvous *pRendezvous,
                               const tNetAddress *pSource,
                               const tNetRendezvousSession *pExclude)
{
  int iSession, iCount = 0;
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession)
    if (pRendezvous->aSessions[iSession].byActive &&
        &pRendezvous->aSessions[iSession] != pExclude &&
        NetRvzIpEqual(&pRendezvous->aSessions[iSession].source, pSource))
      ++iCount;
  return iCount;
}

static int NetRvzRandomIdentity(tNetRendezvous *pRendezvous,
                                uint32 *pSessionId, uint64 *pToken)
{
  int iAttempt;
  for (iAttempt = 0; iAttempt < 16; ++iAttempt) {
    uint8 abRandom[12];
    if (!pRendezvous->pRandom(pRendezvous->pRandomContext, abRandom,
                              sizeof(abRandom)))
      return 0;
    *pSessionId = NetRvzRead32(abRandom);
    *pToken = NetRvzRead64(abRandom + 4);
    if (*pSessionId && *pToken &&
        !NetRvzFindSession(pRendezvous, *pSessionId))
      return 1;
  }
  return 0;
}

static void NetRvzSendAck(tNetRendezvous *pRendezvous,
                          const tNetAddress *pDestination,
                          uint16 unSequence, uint64 ullToken,
                          uint8 byType, uint32 uiSessionId,
                          uint32 uiLeaseMs)
{
  uint8 abAck[sizeof(tRvzAck)];
  NetRvzWrite32(abAck, uiSessionId);
  NetRvzWrite32(abAck + 4, uiLeaseMs);
  NetRvzSend(pRendezvous, pDestination, unSequence, ullToken, byType,
             abAck, sizeof(abAck));
}

static void NetRvzRegister(tNetRendezvous *pRendezvous,
                           const tNetAddress *pSource,
                           const tNetRendezvousPacket *pPacket,
                           uint64 ullNowMs)
{
  tNetRendezvousSession *pSession = NULL;
  tRvzSessionInfo info;
  tNetAddress aCandidates[NET_RVZ_MAX_LOCAL_CANDIDATES];
  uint64 ullNonce;
  uint8 byCandidateCount;
  int iSession, iPad, iByte;
  if (pPacket->ullToken ||
      pPacket->unPayloadLength != sizeof(tRvzRegisterRequest)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  ullNonce = NetRvzRead64(pPacket->pPayload);
  NetRendezvousDecodeSessionInfo(&info, pPacket->pPayload + 8);
  byCandidateCount = pPacket->pPayload[8 + sizeof(tRvzSessionInfo)];
  for (iPad = 1; iPad < 4; ++iPad)
    if (pPacket->pPayload[8 + sizeof(tRvzSessionInfo) + iPad])
      byCandidateCount = NET_RVZ_MAX_LOCAL_CANDIDATES + 1;
  for (iByte = 8 + (int)sizeof(tRvzSessionInfo) + 4 +
           byCandidateCount * (int)sizeof(tRvzCandidate);
       byCandidateCount <= NET_RVZ_MAX_LOCAL_CANDIDATES &&
       iByte < (int)sizeof(tRvzRegisterRequest); ++iByte)
    if (pPacket->pPayload[iByte])
      byCandidateCount = NET_RVZ_MAX_LOCAL_CANDIDATES + 1;
  if (!ullNonce || !NetRvzInfoValid(&info, 1) ||
      byCandidateCount > NET_RVZ_MAX_LOCAL_CANDIDATES ||
      !NetRvzDecodeCandidates(aCandidates, NET_RVZ_MAX_LOCAL_CANDIDATES,
          pPacket->pPayload + 8 + sizeof(tRvzSessionInfo) + 4,
          byCandidateCount)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  pSession = NetRvzFindRegistration(pRendezvous, pSource, ullNonce);
  if (pSession) {
    pSession->ullExpiresMs = ullNowMs + NET_RVZ_SESSION_LEASE_MS;
    NetRvzSendAck(pRendezvous, pSource, pPacket->unSequence,
                  pSession->ullToken, NET_RVZ_MSG_REGISTERED,
                  pSession->info.uiSessionId, NET_RVZ_SESSION_LEASE_MS);
    return;
  }
  if (NetRvzSessionsForIp(pRendezvous, pSource, NULL) >=
      NET_RVZ_MAX_SESSIONS_PER_IP) {
    ++pRendezvous->stats.ullCapacityRejects;
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_SOURCE_LIMIT);
    return;
  }
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession)
    if (!pRendezvous->aSessions[iSession].byActive) {
      pSession = &pRendezvous->aSessions[iSession];
      break;
    }
  if (!pSession) {
    ++pRendezvous->stats.ullCapacityRejects;
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_FULL);
    return;
  }
  memset(pSession, 0, sizeof(*pSession));
  if (!NetRvzRandomIdentity(pRendezvous, &info.uiSessionId,
                            &pSession->ullToken)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_RANDOM_UNAVAILABLE);
    return;
  }
  info.unPort = pSource->unPort;
  pSession->byActive = 1;
  pSession->source = *pSource;
  pSession->ullRegistrationNonce = ullNonce;
  pSession->ullExpiresMs = ullNowMs + NET_RVZ_SESSION_LEASE_MS;
  pSession->info = info;
  pSession->byCandidateCount = byCandidateCount;
  if (byCandidateCount)
    memcpy(pSession->aCandidates, aCandidates,
           byCandidateCount * sizeof(tNetAddress));
  ++pRendezvous->stats.iActiveSessions;
  if (pRendezvous->stats.iActiveSessions >
      pRendezvous->stats.iSessionHighWater)
    pRendezvous->stats.iSessionHighWater =
        pRendezvous->stats.iActiveSessions;
  NetRvzSendAck(pRendezvous, pSource, pPacket->unSequence,
                pSession->ullToken, NET_RVZ_MSG_REGISTERED,
                info.uiSessionId, NET_RVZ_SESSION_LEASE_MS);
}

static void NetRvzHeartbeat(tNetRendezvous *pRendezvous,
                            const tNetAddress *pSource,
                            const tNetRendezvousPacket *pPacket,
                            uint64 ullNowMs)
{
  tNetRendezvousSession *pSession;
  tRvzSessionInfo info;
  if (!pPacket->ullToken ||
      pPacket->unPayloadLength != sizeof(tRvzSessionInfo)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  NetRendezvousDecodeSessionInfo(&info, pPacket->pPayload);
  pSession = NetRvzFindSession(pRendezvous, info.uiSessionId);
  if (!pSession || pSession->ullToken != pPacket->ullToken) {
    ++pRendezvous->stats.ullAuthRejects;
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_NOT_FOUND);
    return;
  }
  if (!NetRvzInfoValid(&info, 0) ||
      (pSession->source.byFamily != pSource->byFamily) ||
      (!NetRvzIpEqual(&pSession->source, pSource) &&
       NetRvzSessionsForIp(pRendezvous, pSource, pSession) >=
           NET_RVZ_MAX_SESSIONS_PER_IP)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  info.unPort = pSource->unPort;
  pSession->source = *pSource;
  pSession->info = info;
  pSession->ullExpiresMs = ullNowMs + NET_RVZ_SESSION_LEASE_MS;
  NetRvzSendAck(pRendezvous, pSource, pPacket->unSequence,
                pSession->ullToken, NET_RVZ_MSG_ACK, info.uiSessionId,
                NET_RVZ_SESSION_LEASE_MS);
}

static void NetRvzUnregister(tNetRendezvous *pRendezvous,
                             const tNetAddress *pSource,
                             const tNetRendezvousPacket *pPacket)
{
  tNetRendezvousSession *pSession;
  uint32 uiSessionId;
  if (!pPacket->ullToken ||
      pPacket->unPayloadLength != sizeof(tRvzUnregisterRequest)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  uiSessionId = NetRvzRead32(pPacket->pPayload);
  pSession = NetRvzFindSession(pRendezvous, uiSessionId);
  if (!pSession || pSession->ullToken != pPacket->ullToken) {
    ++pRendezvous->stats.ullAuthRejects;
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_NOT_FOUND);
    return;
  }
  NetRvzDeactivateSession(pRendezvous, pSession);
  NetRvzSendAck(pRendezvous, pSource, pPacket->unSequence, 0,
                NET_RVZ_MSG_ACK, uiSessionId, 0);
}

static int NetRvzListMatches(const tNetRendezvousSession *pSession,
                             const tRvzListRequest *pRequest)
{
  return pSession->byActive &&
      (!pRequest->byFilterBuild ||
       memcmp(pSession->info.szBuildHash, pRequest->szBuildHash,
              sizeof(pRequest->szBuildHash)) == 0);
}

static void NetRvzList(tNetRendezvous *pRendezvous,
                       const tNetAddress *pSource,
                       const tNetRendezvousPacket *pPacket)
{
  tRvzListRequest request;
  uint8 abPayload[sizeof(tRvzListPageHeader) +
      NET_RVZ_MAX_PAGE_SESSIONS * sizeof(tRvzSessionInfo)];
  uint16 unPage, unPages, unTotal = 0;
  uint32 uiFirst;
  uint8 byCount = 0;
  int iSession, iMatch = 0;
  if (pPacket->ullToken ||
      pPacket->unPayloadLength != sizeof(tRvzListRequest)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  request.unPage = NetRvzRead16(pPacket->pPayload);
  request.byFilterBuild = pPacket->pPayload[2];
  request.byPad = pPacket->pPayload[3];
  memcpy(request.szBuildHash, pPacket->pPayload + 4,
         sizeof(request.szBuildHash));
  if (request.byFilterBuild > 1 || request.byPad ||
      (request.byFilterBuild &&
       !NetRvzTextValid(request.szBuildHash,
                        sizeof(request.szBuildHash), 1))) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession)
    if (NetRvzListMatches(&pRendezvous->aSessions[iSession], &request))
      ++unTotal;
  unPages = (uint16)((unTotal + NET_RVZ_MAX_PAGE_SESSIONS - 1) /
                     NET_RVZ_MAX_PAGE_SESSIONS);
  unPage = request.unPage;
  uiFirst = (uint32)unPage * NET_RVZ_MAX_PAGE_SESSIONS;
  memset(abPayload, 0, sizeof(abPayload));
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS; ++iSession) {
    if (!NetRvzListMatches(&pRendezvous->aSessions[iSession], &request))
      continue;
    if ((uint32)iMatch >= uiFirst &&
        byCount < NET_RVZ_MAX_PAGE_SESSIONS) {
      NetRendezvousEncodeSessionInfo(abPayload + sizeof(tRvzListPageHeader) +
          byCount * sizeof(tRvzSessionInfo),
          &pRendezvous->aSessions[iSession].info);
      ++byCount;
    }
    ++iMatch;
  }
  NetRvzWrite16(abPayload, unPage);
  NetRvzWrite16(abPayload + 2, unPages);
  NetRvzWrite16(abPayload + 4, unTotal);
  abPayload[6] = byCount;
  abPayload[7] = (uint8)(unPage + 1 < unPages);
  NetRvzSend(pRendezvous, pSource, pPacket->unSequence, 0,
             NET_RVZ_MSG_LIST_PAGE, abPayload,
             (uint16)(sizeof(tRvzListPageHeader) +
                      byCount * sizeof(tRvzSessionInfo)));
}

static void NetRvzResolve(tNetRendezvous *pRendezvous,
                          const tNetAddress *pSource,
                          const tNetRendezvousPacket *pPacket)
{
  tNetRendezvousSession *pSession;
  uint8 abResolved[sizeof(tRvzResolved)] = {0};
  uint32 uiSessionId;
  if (pPacket->ullToken || pPacket->unPayloadLength != 4) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  uiSessionId = NetRvzRead32(pPacket->pPayload);
  pSession = NetRvzFindSession(pRendezvous, uiSessionId);
  if (!pSession) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_NOT_FOUND);
    return;
  }
  NetRvzWrite32(abResolved, uiSessionId);
  abResolved[4] = pSession->source.byFamily;
  NetRvzWrite16(abResolved + 8, pSession->source.unPort);
  NetRvzWrite32(abResolved + 12, pSession->source.uiScopeId);
  memcpy(abResolved + 16, pSession->source.abAddress, 16);
  NetRvzSend(pRendezvous, pSource, pPacket->unSequence, 0,
             NET_RVZ_MSG_RESOLVED, abResolved, sizeof(abResolved));
}

static int NetRvzDecodeCandidateSet(tNetAddress *pCandidates, int *piCount,
                                    uint32 *puiSessionId,
                                    uint64 *pullPunchNonce,
                                    const tNetRendezvousPacket *pPacket)
{
  int iByte;
  uint8 byCount;
  if (pPacket->unPayloadLength != sizeof(tRvzCandidateSet) ||
      pPacket->pPayload[13] || pPacket->pPayload[14] ||
      pPacket->pPayload[15])
    return 0;
  byCount = pPacket->pPayload[12];
  if (byCount > NET_RVZ_MAX_LOCAL_CANDIDATES ||
      !NetRvzDecodeCandidates(pCandidates, NET_RVZ_MAX_LOCAL_CANDIDATES,
                              pPacket->pPayload + 16, byCount))
    return 0;
  for (iByte = 16 + byCount * (int)sizeof(tRvzCandidate);
       iByte < (int)sizeof(tRvzCandidateSet); ++iByte)
    if (pPacket->pPayload[iByte])
      return 0;
  *piCount = byCount;
  *puiSessionId = NetRvzRead32(pPacket->pPayload);
  *pullPunchNonce = NetRvzRead64(pPacket->pPayload + 4);
  return *puiSessionId && *pullPunchNonce;
}

static void NetRvzEncodeCandidateSet(uint8 *pWire, uint32 uiSessionId,
                                     uint64 ullPunchNonce,
                                     const tNetAddress *pCandidates,
                                     int iCount)
{
  int iCandidate;
  memset(pWire, 0, sizeof(tRvzCandidateSet));
  NetRvzWrite32(pWire, uiSessionId);
  NetRvzWrite64(pWire + 4, ullPunchNonce);
  pWire[12] = (uint8)iCount;
  for (iCandidate = 0; iCandidate < iCount; ++iCandidate)
    NetRendezvousEncodeCandidate(
        pWire + 16 + iCandidate * sizeof(tRvzCandidate),
        &pCandidates[iCandidate]);
}

static void NetRvzPunchRequest(tNetRendezvous *pRendezvous,
                               const tNetAddress *pSource,
                               const tNetRendezvousPacket *pPacket)
{
  tNetRendezvousSession *pSession;
  tNetAddress aClientCandidates[NET_RVZ_MAX_CANDIDATES];
  tNetAddress aHostCandidates[NET_RVZ_MAX_CANDIDATES];
  uint8 abClientSet[sizeof(tRvzCandidateSet)];
  uint8 abHostSet[sizeof(tRvzCandidateSet)];
  uint32 uiSessionId;
  uint64 ullPunchNonce;
  int iClientCount, iHostCount;
  if (pPacket->ullToken || !NetRvzDecodeCandidateSet(
          aClientCandidates, &iClientCount, &uiSessionId,
          &ullPunchNonce, pPacket)) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_INVALID);
    return;
  }
  pSession = NetRvzFindSession(pRendezvous, uiSessionId);
  if (!pSession) {
    NetRvzSendError(pRendezvous, pSource, pPacket->unSequence,
                    NET_RVZ_ERROR_NOT_FOUND);
    return;
  }
  iClientCount = NetRvzAppendCandidate(aClientCandidates, iClientCount,
                                       NET_RVZ_MAX_CANDIDATES, pSource);
  iHostCount = pSession->byCandidateCount;
  if (iHostCount)
    memcpy(aHostCandidates, pSession->aCandidates,
           iHostCount * sizeof(tNetAddress));
  iHostCount = NetRvzAppendCandidate(aHostCandidates, iHostCount,
                                     NET_RVZ_MAX_CANDIDATES,
                                     &pSession->source);
  NetRvzEncodeCandidateSet(abClientSet, uiSessionId, ullPunchNonce,
                           aClientCandidates, iClientCount);
  NetRvzEncodeCandidateSet(abHostSet, uiSessionId, ullPunchNonce,
                           aHostCandidates, iHostCount);
  NetRvzSend(pRendezvous, &pSession->source, pPacket->unSequence,
             pSession->ullToken, NET_RVZ_MSG_PUNCH_OFFER,
             abClientSet, sizeof(abClientSet));
  NetRvzSend(pRendezvous, pSource, pPacket->unSequence, 0,
             NET_RVZ_MSG_PUNCH_ANSWER, abHostSet, sizeof(abHostSet));
}

static void NetRvzHandle(tNetRendezvous *pRendezvous,
                         const tNetAddress *pSource, const uint8 *pData,
                         int iLength, uint64 ullNowMs)
{
  tNetRendezvousPacket packet;
  ++pRendezvous->stats.ullPacketsReceived;
  if (!NetRvzSourceValid(pSource) ||
      !NetRvzRateAllow(pRendezvous, pSource, ullNowMs))
    return;
  if (!NetRendezvousParsePacket(pData, iLength, &packet)) {
    ++pRendezvous->stats.ullMalformedPackets;
    return;
  }
  switch (packet.byType) {
    case NET_RVZ_MSG_REGISTER:
      NetRvzRegister(pRendezvous, pSource, &packet, ullNowMs);
      break;
    case NET_RVZ_MSG_HEARTBEAT:
      NetRvzHeartbeat(pRendezvous, pSource, &packet, ullNowMs);
      break;
    case NET_RVZ_MSG_UNREGISTER:
      NetRvzUnregister(pRendezvous, pSource, &packet);
      break;
    case NET_RVZ_MSG_LIST:
      NetRvzList(pRendezvous, pSource, &packet);
      break;
    case NET_RVZ_MSG_RESOLVE:
      NetRvzResolve(pRendezvous, pSource, &packet);
      break;
    case NET_RVZ_MSG_PUNCH_REQUEST:
      NetRvzPunchRequest(pRendezvous, pSource, &packet);
      break;
    default:
      NetRvzSendError(pRendezvous, pSource, packet.unSequence,
                      NET_RVZ_ERROR_INVALID);
      break;
  }
}

tNetRendezvous *NetRendezvousCreate(tNetTransport transport,
                                    tNetRendezvousRandomFn pRandom,
                                    void *pRandomContext)
{
  tNetRendezvous *pRendezvous;
  if (!transport.pSend || !transport.pReceive || !transport.pNowMs ||
      !pRandom)
    return NULL;
  pRendezvous = (tNetRendezvous *)calloc(1, sizeof(*pRendezvous));
  if (!pRendezvous)
    return NULL;
  pRendezvous->transport = transport;
  pRendezvous->pRandom = pRandom;
  pRendezvous->pRandomContext = pRandomContext;
  return pRendezvous;
}

void NetRendezvousDestroy(tNetRendezvous *pRendezvous)
{
  free(pRendezvous);
}

int NetRendezvousPump(tNetRendezvous *pRendezvous)
{
  uint8 abPacket[NET_MAX_PAYLOAD];
  tNetAddress source;
  uint64 ullNowMs;
  int iLength, iPackets = 0;
  if (!pRendezvous)
    return 0;
  ullNowMs = pRendezvous->transport.pNowMs(
      pRendezvous->transport.pContext);
  NetRvzExpire(pRendezvous, ullNowMs);
  while (iPackets < NET_RVZ_MAX_PUMP_PACKETS &&
         (iLength = pRendezvous->transport.pReceive(
              pRendezvous->transport.pContext, &source, abPacket,
              sizeof(abPacket))) > 0) {
    NetRvzHandle(pRendezvous, &source, abPacket, iLength, ullNowMs);
    ++iPackets;
  }
  return iPackets;
}

void NetRendezvousGetStats(const tNetRendezvous *pRendezvous,
                           tNetRendezvousStats *pStats)
{
  if (pStats)
    *pStats = pRendezvous ? pRendezvous->stats :
        (tNetRendezvousStats){0};
}
