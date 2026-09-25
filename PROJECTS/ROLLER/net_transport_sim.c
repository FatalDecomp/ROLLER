#include "net_transport.h"
#include <stdlib.h>
#include <string.h>

#define NET_SIM_PACKETS 4096
typedef struct {
  uint64 ullDueMs, ullOrder;
  int iLength, iSource, iDestination;
  uint8 abData[NET_MAX_PAYLOAD];
} tNetSimPacket;
typedef struct {
  struct tNetTransportSim *pSim;
  int iIndex;
  tNetAddress address;
} tNetSimEndpoint;
struct tNetTransportSim {
  uint64 ullNowMs, ullOrder;
  uint32 uiRandom;
  tNetSimLink aLinks[NET_SIM_MAX_ENDPOINTS];
  tNetSimEndpoint aEndpoints[NET_SIM_MAX_ENDPOINTS];
  tNetSimPacket aPackets[NET_SIM_PACKETS];
};

static int NetSimAddressEqual(const tNetAddress *pA,
                              const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort ||
      (pA->byFamily == NET_ADDR_IPV6 && pA->uiScopeId != pB->uiScopeId))
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 :
      (pA->byFamily == NET_ADDR_IPV6 ? 16 : 0);
  return iLength && !memcmp(pA->abAddress, pB->abAddress, (size_t)iLength);
}

static uint32 NetSimRandom(tNetTransportSim *pSim)
{
  uint32 uiValue = pSim->uiRandom;
  uiValue ^= uiValue << 13;
  uiValue ^= uiValue >> 17;
  uiValue ^= uiValue << 5;
  return pSim->uiRandom = uiValue;
}

static int NetSimSend(void *pContext, const tNetAddress *pTo, const void *pData, int iLength)
{
  tNetSimEndpoint *pEndpoint = pContext;
  tNetTransportSim *pSim = pEndpoint->pSim;
  const tNetSimLink *pLink = &pSim->aLinks[pEndpoint->iIndex];
  int iCopies, iDestination, iFirstDestination, iLastDestination;
  int aiFree[NET_SIM_MAX_ENDPOINTS * 2], iFree = 0, iNeeded = 0;
  int iBroadcast = pTo && pTo->byFamily == NET_ADDR_IPV4 &&
      pTo->abAddress[0] == 255 && pTo->abAddress[1] == 255 &&
      pTo->abAddress[2] == 255 && pTo->abAddress[3] == 255;
  if (!pData || iLength < 1 || iLength > NET_MAX_PAYLOAD)
    return -1;
  if (iBroadcast) {
    iFirstDestination = 0;
    iLastDestination = NET_SIM_MAX_ENDPOINTS;
  } else if (!pTo) {
    if (pEndpoint->iIndex > 1)
      return -1;
    iDestination = 1 - pEndpoint->iIndex;
    iFirstDestination = iDestination;
    iLastDestination = iDestination + 1;
  } else {
    for (iDestination = 0; iDestination < NET_SIM_MAX_ENDPOINTS;
         ++iDestination)
      if (NetSimAddressEqual(pTo, &pSim->aEndpoints[iDestination].address))
        break;
    iFirstDestination = iDestination;
    iLastDestination = iDestination + 1;
  }
  if (!iBroadcast && (iDestination == NET_SIM_MAX_ENDPOINTS ||
      iDestination == pEndpoint->iIndex))
    return -1;
  if (NetSimRandom(pSim) % 1000 < pLink->unLossPermille)
    return iLength;
  iCopies = 1 + (NetSimRandom(pSim) % 1000 < pLink->unDuplicatePermille);
  for (iDestination = iFirstDestination; iDestination < iLastDestination;
       ++iDestination)
    if (iDestination != pEndpoint->iIndex &&
        (!iBroadcast || pSim->aEndpoints[iDestination].address.unPort ==
                           pTo->unPort))
      iNeeded += iCopies;
  if (!iNeeded)
    return iLength;
  for (int iPacket = 0; iPacket < NET_SIM_PACKETS && iFree < iNeeded; ++iPacket)
    if (!pSim->aPackets[iPacket].iLength)
      aiFree[iFree++] = iPacket;
  if (iFree < iNeeded)
    return -1;
  iFree = 0;
  for (iDestination = iFirstDestination; iDestination < iLastDestination;
       ++iDestination) {
    if (iDestination == pEndpoint->iIndex ||
        (iBroadcast && pSim->aEndpoints[iDestination].address.unPort !=
                           pTo->unPort))
      continue;
    for (int iCopy = 0; iCopy < iCopies; ++iCopy) {
      tNetSimPacket *pPacket = &pSim->aPackets[aiFree[iFree++]];
      int64 llDelay = (int64)pLink->uiLatencyMs;
      if (pLink->uiJitterMs)
        llDelay += (int64)(NetSimRandom(pSim) %
            (2 * pLink->uiJitterMs + 1)) - pLink->uiJitterMs;
      if (NetSimRandom(pSim) % 1000 < pLink->unReorderPermille)
        llDelay += pLink->uiLatencyMs + pLink->uiJitterMs + 1;
      pPacket->ullDueMs = pSim->ullNowMs +
          (uint64)(llDelay > 0 ? llDelay : 0);
      pPacket->ullOrder = pSim->ullOrder++;
      pPacket->iSource = pEndpoint->iIndex;
      pPacket->iDestination = iDestination;
      pPacket->iLength = iLength;
      memcpy(pPacket->abData, pData, (size_t)iLength);
    }
  }
  return iLength;
}

static int NetSimReceive(void *pContext, tNetAddress *pFrom, void *pData, int iCapacity)
{
  tNetSimEndpoint *pEndpoint = pContext;
  tNetTransportSim *pSim = pEndpoint->pSim;
  tNetSimPacket *pBest = NULL;
  int iLength;
  for (int iPacket = 0; iPacket < NET_SIM_PACKETS; ++iPacket) {
    tNetSimPacket *pPacket = &pSim->aPackets[iPacket];
    if (pPacket->iLength && pPacket->iDestination == pEndpoint->iIndex &&
        pPacket->ullDueMs <= pSim->ullNowMs &&
        (!pBest || pPacket->ullDueMs < pBest->ullDueMs ||
         (pPacket->ullDueMs == pBest->ullDueMs && pPacket->ullOrder < pBest->ullOrder)))
      pBest = pPacket;
  }
  if (!pBest)
    return 0;
  if (!pData || iCapacity < pBest->iLength)
    return -1;
  iLength = pBest->iLength;
  memcpy(pData, pBest->abData, (size_t)iLength);
  if (pFrom)
    *pFrom = pSim->aEndpoints[pBest->iSource].address;
  pBest->iLength = 0;
  return iLength;
}

static uint64 NetSimNowMs(void *pContext)
{
  return ((tNetSimEndpoint *)pContext)->pSim->ullNowMs;
}

tNetTransportSim *NetTransportSimCreate(uint32 uiSeed)
{
  tNetTransportSim *pSim = calloc(1, sizeof(*pSim));
  if (pSim) {
    pSim->uiRandom = uiSeed ? uiSeed : 1;
    for (int iEndpoint = 0; iEndpoint < NET_SIM_MAX_ENDPOINTS; ++iEndpoint) {
      pSim->aEndpoints[iEndpoint].pSim = pSim;
      pSim->aEndpoints[iEndpoint].iIndex = iEndpoint;
      pSim->aEndpoints[iEndpoint].address.abAddress[0] = 127;
      pSim->aEndpoints[iEndpoint].address.abAddress[3] = 1;
      pSim->aEndpoints[iEndpoint].address.unPort = (uint16)iEndpoint;
      pSim->aEndpoints[iEndpoint].address.byFamily = NET_ADDR_IPV4;
    }
  }
  return pSim;
}

void NetTransportSimDestroy(tNetTransportSim *pSim) { free(pSim); }

tNetTransport NetTransportSimEndpoint(tNetTransportSim *pSim, int iEndpoint)
{
  tNetTransport transport = {0};
  if (pSim && iEndpoint >= 0 && iEndpoint < NET_SIM_MAX_ENDPOINTS) {
    transport.pContext = &pSim->aEndpoints[iEndpoint];
    transport.pSend = NetSimSend;
    transport.pReceive = NetSimReceive;
    transport.pNowMs = NetSimNowMs;
  }
  return transport;
}

int NetTransportSimSetLink(tNetTransportSim *pSim, int iSender, const tNetSimLink *pLink)
{
  if (!pSim || !pLink || iSender < 0 ||
      iSender >= NET_SIM_MAX_ENDPOINTS ||
      pLink->unLossPermille > 1000 || pLink->unDuplicatePermille > 1000 ||
      pLink->unReorderPermille > 1000 || pLink->uiJitterMs > 60000 || pLink->uiLatencyMs > 60000)
    return 0;
  pSim->aLinks[iSender] = *pLink;
  return 1;
}

int NetTransportSimSetEndpointAddress(tNetTransportSim *pSim, int iEndpoint,
                                      const tNetAddress *pAddress)
{
  if (!pSim || !pAddress || iEndpoint < 0 ||
      iEndpoint >= NET_SIM_MAX_ENDPOINTS ||
      (pAddress->byFamily != NET_ADDR_IPV4 &&
       pAddress->byFamily != NET_ADDR_IPV6))
    return 0;
  pSim->aEndpoints[iEndpoint].address = *pAddress;
  return 1;
}

int NetTransportSimAdvance(tNetTransportSim *pSim, uint64 ullNowMs)
{
  if (!pSim || ullNowMs < pSim->ullNowMs || ullNowMs > UINT64_MAX - 180001)
    return 0;
  pSim->ullNowMs = ullNowMs;
  return 1;
}
