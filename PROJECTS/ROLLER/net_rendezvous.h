#ifndef ROLLER_NET_RENDEZVOUS_H
#define ROLLER_NET_RENDEZVOUS_H

#include "net_transport.h"

#define NET_RVZ_PROTOCOL_ID 0x52565A31u /* 'RVZ1' */
#define NET_RVZ_PROTOCOL_VERSION 1
#define NET_RVZ_DEFAULT_PORT 7778
#define NET_RVZ_MAX_SESSIONS 512
#define NET_RVZ_MAX_SESSIONS_PER_IP 4
#define NET_RVZ_MAX_PAGE_SESSIONS 12
#define NET_RVZ_CONTROL_PACKETS_PER_SECOND 20
#define NET_RVZ_CONTROL_BURST 40
#define NET_RVZ_SESSION_LEASE_MS 30000
#define NET_RVZ_HEARTBEAT_MS 10000

enum {
  NET_RVZ_SESSION_PASSWORD = 1,
  NET_RVZ_SESSION_DEDICATED = 2,
  NET_RVZ_SESSION_IN_RACE = 4,
  NET_RVZ_SESSION_FLAGS = NET_RVZ_SESSION_PASSWORD |
      NET_RVZ_SESSION_DEDICATED | NET_RVZ_SESSION_IN_RACE
};

typedef enum {
  NET_RVZ_MSG_REGISTER = 1,
  NET_RVZ_MSG_REGISTERED,
  NET_RVZ_MSG_HEARTBEAT,
  NET_RVZ_MSG_UNREGISTER,
  NET_RVZ_MSG_LIST,
  NET_RVZ_MSG_LIST_PAGE,
  NET_RVZ_MSG_ACK,
  NET_RVZ_MSG_ERROR,
  NET_RVZ_MSG_RESOLVE,
  NET_RVZ_MSG_RESOLVED
} eNetRendezvousMessageType;

typedef enum {
  NET_RVZ_ERROR_INVALID = 1,
  NET_RVZ_ERROR_FULL,
  NET_RVZ_ERROR_SOURCE_LIMIT,
  NET_RVZ_ERROR_NOT_FOUND,
  NET_RVZ_ERROR_RANDOM_UNAVAILABLE
} eNetRendezvousError;

#pragma pack(push, 1)
typedef struct {
  uint64 ullRegistrationNonce;
  tRvzSessionInfo info;
} tRvzRegisterRequest;

typedef struct {
  uint32 uiSessionId;
  uint32 uiLeaseMs;
} tRvzAck;

typedef struct {
  uint32 uiSessionId;
} tRvzUnregisterRequest;

typedef struct {
  uint16 unPage;
  uint8 byFilterBuild, byPad;
  char szBuildHash[16];
} tRvzListRequest;

typedef struct {
  uint16 unPage, unPages, unTotal;
  uint8 byCount, byMore;
} tRvzListPageHeader;

typedef struct {
  uint8 byReason, byPad;
  uint16 unRetryAfterMs;
} tRvzError;
typedef struct {
  uint32 uiSessionId;
  uint8 byFamily, byPad[3];
  uint16 unPort, unPad;
  uint32 uiScopeId;
  uint8 abAddress[16];
} tRvzResolved;
#pragma pack(pop)

_Static_assert(sizeof(tRvzRegisterRequest) == 93,
               "rendezvous registration wire size");
_Static_assert(sizeof(tRvzAck) == 8, "rendezvous ack wire size");
_Static_assert(sizeof(tRvzUnregisterRequest) == 4,
               "rendezvous unregister wire size");
_Static_assert(sizeof(tRvzListRequest) == 20,
               "rendezvous list request wire size");
_Static_assert(sizeof(tRvzListPageHeader) == 8,
               "rendezvous list header wire size");
_Static_assert(sizeof(tRvzError) == 4, "rendezvous error wire size");
_Static_assert(sizeof(tRvzResolved) == 32, "rendezvous resolve wire size");
_Static_assert(sizeof(tRvzListPageHeader) +
                   NET_RVZ_MAX_PAGE_SESSIONS * sizeof(tRvzSessionInfo) +
                   sizeof(tNetPacketHeader) + sizeof(tNetMessageHeader) <=
                   NET_MAX_PAYLOAD,
               "rendezvous page fits");

typedef int (*tNetRendezvousRandomFn)(void *pContext, void *pData,
                                      int iLength);

typedef struct tNetRendezvous tNetRendezvous;

typedef struct {
  int iActiveSessions, iSessionHighWater;
  int iRateSources, iRateSourceHighWater;
  uint64 ullPacketsReceived, ullPacketsSent, ullMalformedPackets;
  uint64 ullRateLimitedPackets, ullCapacityRejects, ullAuthRejects;
  uint64 ullExpiredSessions;
} tNetRendezvousStats;

typedef struct {
  uint16 unSequence;
  uint64 ullToken;
  uint8 byType;
  const uint8 *pPayload;
  uint16 unPayloadLength;
} tNetRendezvousPacket;

tNetRendezvous *NetRendezvousCreate(tNetTransport transport,
                                    tNetRendezvousRandomFn pRandom,
                                    void *pRandomContext);
void NetRendezvousDestroy(tNetRendezvous *pRendezvous);
int NetRendezvousPump(tNetRendezvous *pRendezvous);
void NetRendezvousGetStats(const tNetRendezvous *pRendezvous,
                           tNetRendezvousStats *pStats);

int NetRendezvousBuildPacket(uint8 *pPacket, int iCapacity,
                             uint16 unSequence, uint64 ullToken,
                             uint8 byType, const void *pPayload,
                             uint16 unPayloadLength);
int NetRendezvousParsePacket(const void *pPacket, int iLength,
                             tNetRendezvousPacket *pDecoded);
void NetRendezvousEncodeSessionInfo(uint8 *pWire,
                                    const tRvzSessionInfo *pInfo);
void NetRendezvousDecodeSessionInfo(tRvzSessionInfo *pInfo,
                                    const uint8 *pWire);

#endif
