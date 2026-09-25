#ifndef ROLLER_NET_DISCOVERY_H
#define ROLLER_NET_DISCOVERY_H

#include "net_channel.h"
#include "net_rendezvous.h"

typedef struct tNetDiscovery tNetDiscovery;

#define NET_LAN_PROTOCOL_ID 0x4C4E4431u /* 'LND1' */
#define NET_LAN_PROTOCOL_VERSION 1
#define NET_LAN_QUERY_INTERVAL_MS 2000
#define NET_LAN_SESSION_TIMEOUT_MS 6000

typedef enum {
  NET_LAN_MSG_QUERY = 1,
  NET_LAN_MSG_ADVERTISE
} eNetLanMessageType;

#pragma pack(push, 1)
typedef struct {
  uint32 uiProtocolId;
  uint8 byVersion, byType, byFilterBuild, byPad;
  char szBuildHash[16];
} tNetLanQuery;

typedef struct {
  uint32 uiProtocolId;
  uint8 byVersion, byType;
  uint8 byPad[2];
  tRvzSessionInfo info;
} tNetLanAdvertisement;
#pragma pack(pop)

_Static_assert(sizeof(tNetLanQuery) == 24, "LAN query wire size");
_Static_assert(sizeof(tNetLanAdvertisement) == 93,
               "LAN advertisement wire size");

typedef enum {
  NET_PUNCH_IDLE = 0,
  NET_PUNCH_IN_PROGRESS,
  NET_PUNCH_SUCCEEDED,
  NET_PUNCH_TIMED_OUT,
  NET_PUNCH_RELAY_IN_PROGRESS,
  NET_PUNCH_RELAY_SUCCEEDED,
  NET_PUNCH_RELAY_THROTTLED
} eNetPunchState;

tNetDiscovery *NetDiscoveryCreate(tNetChannel *pChannel,
                                  const tNetAddress *pRendezvous,
                                  tNetRendezvousRandomFn pRandom,
                                  void *pRandomContext);
int NetDiscoveryEnableLan(tNetDiscovery *pDiscovery, uint16 unPort);
void NetDiscoveryDestroy(tNetDiscovery *pDiscovery);
int NetDiscoverySetLocalCandidates(tNetDiscovery *pDiscovery,
                                   const tNetAddress *pCandidates,
                                   int iCount);
int NetDiscoveryHostStart(tNetDiscovery *pDiscovery,
                          const tRvzSessionInfo *pInfo);
void NetDiscoveryHostUpdate(tNetDiscovery *pDiscovery,
                            const tRvzSessionInfo *pInfo);
void NetDiscoveryHostStop(tNetDiscovery *pDiscovery);
int NetDiscoveryHostRegistered(const tNetDiscovery *pDiscovery,
                               uint32 *puiSessionId);
int NetDiscoveryList(tNetDiscovery *pDiscovery, const char *szBuildHash);
int NetDiscoveryListReady(const tNetDiscovery *pDiscovery);
int NetDiscoverySessionCount(const tNetDiscovery *pDiscovery);
int NetDiscoverySession(const tNetDiscovery *pDiscovery, int iIndex,
                        tRvzSessionInfo *pInfo);
int NetDiscoveryResolve(tNetDiscovery *pDiscovery, uint32 uiSessionId);
int NetDiscoveryResolved(tNetDiscovery *pDiscovery, uint32 uiSessionId,
                         tNetAddress *pAddress);
int NetDiscoveryPunch(tNetDiscovery *pDiscovery, uint32 uiSessionId);
eNetPunchState NetDiscoveryPunchState(const tNetDiscovery *pDiscovery,
                                      tNetAddress *pAddress);
void NetDiscoveryPump(tNetDiscovery *pDiscovery);

#endif
