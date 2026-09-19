#include "net_transport.h"
#include <stdlib.h>
#include <string.h>

#define NET_SIM_PACKETS 4096
typedef struct {
  uint64 ullDueMs, ullOrder;
  int iLength, iDestination;
  uint8 abData[NET_MAX_PAYLOAD];
} tNetSimPacket;
typedef struct {
  struct tNetTransportSim *pSim;
  int iIndex;
} tNetSimEndpoint;
struct tNetTransportSim {
  uint64 ullNowMs, ullOrder;
  uint32 uiRandom;
  tNetSimLink aLinks[2];
  tNetSimEndpoint aEndpoints[2];
  tNetSimPacket aPackets[NET_SIM_PACKETS];
};

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
  int iCopies;
  int aiFree[2], iFree = 0;
  (void)pTo;
  if (!pData || iLength < 1 || iLength > NET_MAX_PAYLOAD)
    return -1;
  if (NetSimRandom(pSim) % 1000 < pLink->unLossPermille)
    return iLength;
  iCopies = 1 + (NetSimRandom(pSim) % 1000 < pLink->unDuplicatePermille);
  for (int iPacket = 0; iPacket < NET_SIM_PACKETS && iFree < iCopies; ++iPacket)
    if (!pSim->aPackets[iPacket].iLength)
      aiFree[iFree++] = iPacket;
  if (iFree < iCopies)
    return -1;
  for (int iCopy = 0; iCopy < iCopies; ++iCopy) {
    tNetSimPacket *pPacket = &pSim->aPackets[aiFree[iCopy]];
    int64 llDelay = (int64)pLink->uiLatencyMs;
    if (pLink->uiJitterMs)
      llDelay += (int64)(NetSimRandom(pSim) % (2 * pLink->uiJitterMs + 1)) - pLink->uiJitterMs;
    if (NetSimRandom(pSim) % 1000 < pLink->unReorderPermille)
      llDelay += pLink->uiLatencyMs + pLink->uiJitterMs + 1;
    pPacket->ullDueMs = pSim->ullNowMs + (uint64)(llDelay > 0 ? llDelay : 0);
    pPacket->ullOrder = pSim->ullOrder++;
    pPacket->iDestination = 1 - pEndpoint->iIndex;
    pPacket->iLength = iLength;
    memcpy(pPacket->abData, pData, (size_t)iLength);
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
  if (pFrom) {
    memset(pFrom, 0, sizeof(*pFrom));
    pFrom->abAddress[0] = 127;
    pFrom->abAddress[3] = 1;
    pFrom->unPort = (uint16)(1 - pEndpoint->iIndex);
    pFrom->byFamily = NET_ADDR_IPV4;
  }
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
    for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint) {
      pSim->aEndpoints[iEndpoint].pSim = pSim;
      pSim->aEndpoints[iEndpoint].iIndex = iEndpoint;
    }
  }
  return pSim;
}

void NetTransportSimDestroy(tNetTransportSim *pSim) { free(pSim); }

tNetTransport NetTransportSimEndpoint(tNetTransportSim *pSim, int iEndpoint)
{
  tNetTransport transport = {0};
  if (pSim && iEndpoint >= 0 && iEndpoint < 2) {
    transport.pContext = &pSim->aEndpoints[iEndpoint];
    transport.pSend = NetSimSend;
    transport.pReceive = NetSimReceive;
    transport.pNowMs = NetSimNowMs;
  }
  return transport;
}

int NetTransportSimSetLink(tNetTransportSim *pSim, int iSender, const tNetSimLink *pLink)
{
  if (!pSim || !pLink || iSender < 0 || iSender > 1 ||
      pLink->unLossPermille > 1000 || pLink->unDuplicatePermille > 1000 ||
      pLink->unReorderPermille > 1000 || pLink->uiJitterMs > 60000 || pLink->uiLatencyMs > 60000)
    return 0;
  pSim->aLinks[iSender] = *pLink;
  return 1;
}

int NetTransportSimAdvance(tNetTransportSim *pSim, uint64 ullNowMs)
{
  if (!pSim || ullNowMs < pSim->ullNowMs || ullNowMs > UINT64_MAX - 180001)
    return 0;
  pSim->ullNowMs = ullNowMs;
  return 1;
}
