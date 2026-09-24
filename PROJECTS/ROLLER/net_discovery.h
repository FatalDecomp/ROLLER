#ifndef ROLLER_NET_DISCOVERY_H
#define ROLLER_NET_DISCOVERY_H

#include "net_channel.h"
#include "net_rendezvous.h"

typedef struct tNetDiscovery tNetDiscovery;

tNetDiscovery *NetDiscoveryCreate(tNetChannel *pChannel,
                                  const tNetAddress *pRendezvous,
                                  tNetRendezvousRandomFn pRandom,
                                  void *pRandomContext);
void NetDiscoveryDestroy(tNetDiscovery *pDiscovery);
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
void NetDiscoveryPump(tNetDiscovery *pDiscovery);

#endif
