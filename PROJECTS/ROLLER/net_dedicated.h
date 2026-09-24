#ifndef ROLLER_NET_DEDICATED_H
#define ROLLER_NET_DEDICATED_H

#include "net_host.h"

#define NET_DEDICATED_START_TICK 1u
#define NET_DEDICATED_MAX_TICKS_PER_PUMP 8

typedef struct tNetDedicated tNetDedicated;

typedef enum
{
  NET_DEDICATED_WAITING = 0,
  NET_DEDICATED_LOADING,
  NET_DEDICATED_RACING,
  NET_DEDICATED_COMPLETE,
  NET_DEDICATED_ERROR
} eNetDedicatedState;

typedef struct
{
  uint32 uiTicksSimulated;
  uint32 uiNextTick;
  int iPlayers;
  int iFinishers;
  int iHumanFinishers;
  eNetDedicatedState state;
} tNetDedicatedStats;

/* The channel and its transport remain caller-owned.  The runtime owns the
   session, lobby, and authoritative host layered on that channel. */
tNetDedicated *NetDedicatedCreate(tNetChannel *pChannel,
                                  const tNetSessionConfig *pConfig,
                                  tNetRandomBytesFn pRandom,
                                  void *pRandomContext);
void NetDedicatedDestroy(tNetDedicated *pDedicated);

/* Frame-loop pump.  It starts when every configured player slot is Ready,
   waits for the loading barrier, then advances the host from the channel's
   monotonic clock without ever skipping a labelled simulation tick. */
int NetDedicatedPump(tNetDedicated *pDedicated);
eNetDedicatedState NetDedicatedState(const tNetDedicated *pDedicated);
uint32 NetDedicatedNextTick(const tNetDedicated *pDedicated);
int NetDedicatedStats(const tNetDedicated *pDedicated,
                      tNetDedicatedStats *pStats);
int NetDedicatedPlayerStats(const tNetDedicated *pDedicated,
                            uint8 byPlayerIdx,
                            tNetHostPlayerStats *pStats);

#endif
