#ifndef ROLLER_NET_CLIENT_H
#define ROLLER_NET_CLIENT_H

#include "net_lobby.h"
#include "net_sim_seam.h"

/* All three rings (4.5) are this deep and indexed by client tick. */
#define NET_CLIENT_HISTORY NET_INPUT_HISTORY
/* Received snapshots and own-car states; 600 ms is at most 60 ticks. */
#define NET_CLIENT_SNAPSHOT_BUFFER 64
/* Time dilation is the only lead adjustment (4.4). */
#define NET_CLIENT_TICK_SCALE_MIN 0.9f
#define NET_CLIENT_TICK_SCALE_MAX 1.1f

typedef struct tNetClient tNetClient;

typedef struct
{
  uint32 uiTicks;              /* client ticks simulated */
  uint32 uiBatchesSent;        /* one NET_MSG_INPUT per client tick */
  uint32 uiSnapshots, uiOwnCarStates, uiFeedback;
  uint32 uiRejectedMessages;   /* race messages that failed to decode */
  uint32 uiStaleMessages;      /* decoded, but older than what was held */
  uint32 uiNewestSnapshotTick; /* uiLastDecodedSnapshotTick on the wire */
  uint32 uiSnapshotAgeMs;
  uint32 uiRampTick;           /* the tick ramps stand at (4.6) */
  /* The host's cumulative counts from the newest input feedback. */
  uint32 uiHostLateInputs, uiHostFutureInputs;
  int16 nArrivalMarginTicks;
  uint8 byHasHostEstimate;
  int iLeadTicks, iLeadBias;
  float fHostTick;             /* estimated host tick now, minus uiStartTick */
  float fLeadErrorTicks;       /* timeline position minus (host + lead) */
  float fTickScale, fRttMs, fJitterMs, fFrameMs;
} tNetClientStats;

/* Registers for race traffic on pLobby.  One client per session. */
tNetClient *NetClientCreate(tNetSessionClient *pSession,
                            tNetLobbyClient *pLobby);
void NetClientDestroy(tNetClient *pClient);

/* Call once the lobby has released the race.  Takes the rollback group (this
   player's cars) and human_control[] from the roster (4.11), clears the
   rings, and puts the timeline just before the start tick.  Requires
   net_mode == NET_MODE_MODERN. */
int NetClientBeginRace(tNetClient *pClient);

/* Frame loop, after the session pump: clock sync, lead control, and the
   dilated accumulator (4.4).  The accumulator owns the client's ticks: the
   frame runs exactly NetClientTicksDue() calls to NetClientTick. */
void NetClientPump(tNetClient *pClient);
int NetClientTicksDue(const tNetClient *pClient);

/* One client tick N = NetClientCurrentTick() + 1 (4.3 steps 5 to 7): the
   group's inputs (one per car, as readuserdata produced them) are
   canonicalised, recorded and sent, the tick runs under
   NET_AUTHORITY_REMOTE, then the prediction and context rings record the
   post-tick state.  Fails when no tick is due, and when the simulation's
   input ring will not take the tick, which is terminal for the race: the
   caller must stop rather than retry, since the tick was not simulated and
   the timeline cannot advance past it. */
int NetClientTick(tNetClient *pClient, const tCarInputData *pLocalInputs);

/* The newest simulated tick; uiStartTick - 1 before the first. */
uint32 NetClientCurrentTick(const tNetClient *pClient);
int NetClientGroup(const tNetClient *pClient, uint8 *pbyCars);
int NetClientStats(const tNetClient *pClient, tNetClientStats *pStats);

/* Ring readers.  Each fails for a tick outside the last NET_CLIENT_HISTORY
   simulated ticks or one never recorded. */
int NetClientInputAt(const tNetClient *pClient, uint32 uiTick,
                     tCarInputData *pInputs);
int NetClientPredictionAt(const tNetClient *pClient, int iMember,
                          uint32 uiTick, tNetCarFullState *pState);
int NetClientContextAt(const tNetClient *pClient, uint32 uiTick,
                       tNetSimTickContext *pContext);

/* Received host state, retained for NET_SNAPSHOT_RETENTION_MS (4.6).
   Own-car extras are in rollback-group order. */
int NetClientSnapshotAt(const tNetClient *pClient, uint32 uiTick,
                        tNetSnapshot *pSnapshot);
int NetClientOwnCarStateAt(const tNetClient *pClient, uint32 uiTick,
                           tNetCarExtra *pExtras);

#endif
