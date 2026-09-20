#ifndef ROLLER_NET_HOST_H
#define ROLLER_NET_HOST_H

#include "net_lobby.h"
#include "sound.h"

/* Host snapshot ring (4.8).  64 entries covers the 600 ms retention window
   at every tick rate and snapshot interval the session allows. */
#define NET_HOST_SNAPSHOT_RING 64
#define NET_HOST_FEEDBACK_MS 250

typedef struct tNetHost tNetHost;

typedef struct
{
  uint32 uiLateInputs;      /* ticks simulated with the repeated last input */
  uint32 uiFutureInputs;    /* input ticks at or beyond the 48-tick horizon */
  uint32 uiClampedInputs;   /* accepted inputs the D9 clamp had to change */
  uint32 uiRejectedBatches; /* malformed or wrongly shaped NET_MSG_INPUT */
  /* The client's timeline as the host sees it (4.4).  Batches carry
     consecutive uiFirstTick values, so over any span
     uiInputBatches + uiBatchTickGaps - uiBatchReorders is exactly
     uiNewestFirstTick - uiFirstBatchTick + 1, and the gaps a reordered
     batch did not fill are the batches that were lost. */
  uint32 uiInputBatches;
  uint32 uiBatchTickGaps;   /* uiFirstTick values skipped as the newest rose */
  uint32 uiBatchReorders;   /* arrived with uiFirstTick at or below the newest */
  uint32 uiFirstBatchTick, uiNewestFirstTick;
  uint32 uiLastDecodedSnapshotTick;
  int16 nArrivalMarginTicks;
  uint8 byCarCount, abyCars[2];
} tNetHostPlayerStats;

/* Registers for race traffic on pLobby.  One host per session. */
tNetHost *NetHostCreate(tNetSessionHost *pSession, tNetLobbyHost *pLobby);
void NetHostDestroy(tNetHost *pHost);

/* Call once the lobby has released the race.  Assigns human_control[] from
   the roster (4.11): each racing player's cars get its byHumanControl, every
   other car 0.  Requires net_mode == NET_MODE_MODERN. */
int NetHostBeginRace(tNetHost *pHost);

/* Frame loop, after the session and lobby pumps: input feedback. */
void NetHostPump(tNetHost *pHost);

/* One host tick (4.3).  uiTick must be NetHostNextTick(): ticks are
   consecutive and every labelled tick is simulated. */
int NetHostTick(tNetHost *pHost, uint32 uiTick);
uint32 NetHostNextTick(const tNetHost *pHost);

/* Replaces the simulation step of NetHostTick (NetSimWriteTickInputs, then
   control_one_tick).  NULL restores it.  A process holds one world (D13); a
   test whose world belongs to a client uses this to keep the host's queues,
   feedback and snapshot cadence running without a second world.  Snapshots
   are then built from whatever world the process holds. */
typedef int (*tNetHostSimulateFn)(void *pContext, uint32 uiTick,
                                  const tCopyData *pInputs, int iNumCars);
void NetHostSetSimulation(tNetHost *pHost, tNetHostSimulateFn pSimulate,
                          void *pContext);

int NetHostSnapshotAt(const tNetHost *pHost, uint32 uiTick,
                      tNetSnapshot *pSnapshot);
int NetHostPlayerStats(const tNetHost *pHost, uint8 byPlayerIdx,
                       tNetHostPlayerStats *pStats);

#endif
