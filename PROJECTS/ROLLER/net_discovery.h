#ifndef ROLLER_NET_DISCOVERY_H
#define ROLLER_NET_DISCOVERY_H

#include "net_channel.h"
#include "net_rendezvous.h"

typedef struct tNetDiscovery tNetDiscovery;

typedef enum {
  NET_PUNCH_IDLE = 0,
  NET_PUNCH_IN_PROGRESS,
  NET_PUNCH_SUCCEEDED,
  NET_PUNCH_TIMED_OUT
} eNetPunchState;

tNetDiscovery *NetDiscoveryCreate(tNetChannel *pChannel,
                                  const tNetAddress *pRendezvous,
                                  tNetRendezvousRandomFn pRandom,
                                  void *pRandomContext);
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
