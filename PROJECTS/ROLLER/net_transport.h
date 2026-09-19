#ifndef ROLLER_NET_TRANSPORT_H
#define ROLLER_NET_TRANSPORT_H
#include "net_protocol.h"

enum {
  NET_ADDR_IPV4 = 4,
  NET_ADDR_IPV6 = 6,
  NET_ADDRESS_STRING_CAPACITY = 80,
  NET_SIM_MAX_ENDPOINTS = 8
};

typedef struct {
  uint8 abAddress[16];
  uint16 unPort;
  uint8 byFamily;
  uint8 byReserved;
  uint32 uiScopeId;
} tNetAddr;
typedef tNetAddr tNetAddress;

typedef struct {
  void *pContext;
  int (*pSend)(void *pContext, const tNetAddress *pTo, const void *pData, int iLength);
  int (*pReceive)(void *pContext, tNetAddress *pFrom, void *pData, int iCapacity);
  uint64 (*pNowMs)(void *pContext);
} tNetTransport;

typedef uint64 (*tNetClockFn)(void *pContext);
typedef struct tNetTransportUdp tNetTransportUdp;

/* Numeric forms are a.b.c.d[:port] and [ipv6%scope][:port].  An omitted
   port uses unDefaultPort.  IPv6 without a port may omit the brackets. */
int NetAddressParse(tNetAddress *pAddress, const char *szText, uint16 unDefaultPort);
int NetAddressFormat(const tNetAddress *pAddress, char *szText, int iCapacity);
int NetAddressEqual(const tNetAddress *pA, const tNetAddress *pB);

/* Returns the number of unique addresses written, or -1 on error. */
int NetAddressEnumerateLocal(tNetAddress *pAddresses, int iCapacity, uint16 unPort);

/* A single non-blocking IPv6 socket with IPv4-mapped addressing.  unPort == 0
   asks the OS for an ephemeral port. */
tNetTransportUdp *NetTransportUdpCreate(uint16 unPort);
void NetTransportUdpDestroy(tNetTransportUdp *pUdp);
tNetTransport NetTransportUdpEndpoint(tNetTransportUdp *pUdp);
uint16 NetTransportUdpPort(const tNetTransportUdp *pUdp);

/* Passing NULL restores the platform monotonic clock. */
void NetTransportUdpSetClock(tNetTransportUdp *pUdp, tNetClockFn pClock,
                             void *pClockContext);

/* Platform cryptographic random source.  The context argument is ignored so
   this function can be passed directly to the session random callback. */
int NetPlatformRandomBytes(void *pContext, void *pData, int iLength);

typedef struct {
  uint32 uiLatencyMs, uiJitterMs;
  uint16 unLossPermille, unDuplicatePermille, unReorderPermille;
} tNetSimLink;
typedef struct tNetTransportSim tNetTransportSim;

/* Channel tests only: deterministic addressed endpoints with independent
   outgoing link settings. */
tNetTransportSim *NetTransportSimCreate(uint32 uiSeed);
void NetTransportSimDestroy(tNetTransportSim *pSim);
tNetTransport NetTransportSimEndpoint(tNetTransportSim *pSim, int iEndpoint);
int NetTransportSimSetLink(tNetTransportSim *pSim, int iSender, const tNetSimLink *pLink);
int NetTransportSimSetEndpointAddress(tNetTransportSim *pSim, int iEndpoint,
                                      const tNetAddress *pAddress);
int NetTransportSimAdvance(tNetTransportSim *pSim, uint64 ullNowMs);

#endif
