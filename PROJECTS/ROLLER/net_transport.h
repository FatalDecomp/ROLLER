#ifndef ROLLER_NET_TRANSPORT_H
#define ROLLER_NET_TRANSPORT_H
#include "net_protocol.h"

/* Transport owns no simulation state. All clocks are supplied by the host. */
typedef struct {
  uint8 abAddress[16];
  uint16 unPort;
  uint8 byFamily;
} tNetAddress;

typedef struct {
  void *pContext;
  int (*pSend)(void *pContext, const tNetAddress *pTo, const void *pData, int iLength);
  int (*pReceive)(void *pContext, tNetAddress *pFrom, void *pData, int iCapacity);
  uint64 (*pNowMs)(void *pContext);
} tNetTransport;

typedef struct {
  uint32 uiLatencyMs, uiJitterMs;
  uint16 unLossPermille, unDuplicatePermille, unReorderPermille;
} tNetSimLink;
typedef struct tNetTransportSim tNetTransportSim;

/* Channel tests only: two endpoints, independent directional link settings. */
tNetTransportSim *NetTransportSimCreate(uint32 uiSeed);
void NetTransportSimDestroy(tNetTransportSim *pSim);
tNetTransport NetTransportSimEndpoint(tNetTransportSim *pSim, int iEndpoint);
int NetTransportSimSetLink(tNetTransportSim *pSim, int iSender, const tNetSimLink *pLink);
int NetTransportSimAdvance(tNetTransportSim *pSim, uint64 ullNowMs);

#endif
