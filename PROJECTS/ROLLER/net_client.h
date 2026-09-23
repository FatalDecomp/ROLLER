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
/* World-space puppet buffering and visual-only stall extrapolation (4.6). */
#define NET_CLIENT_INTERPOLATION_MIN_MS 50.0f
#define NET_CLIENT_INTERPOLATION_MAX_MS 250.0f
#define NET_CLIENT_EXTRAPOLATION_MAX_MS 100.0f
/* Reconciliation thresholds, in wire/world units (4.5). */
#define NET_CLIENT_POSITION_TOLERANCE 0.5f
#define NET_CLIENT_SPEED_TOLERANCE 1.0f
#define NET_CLIENT_ANGLE_TOLERANCE 91 /* two degrees in the 14-bit circle */
#define NET_CLIENT_CORRECTION_TICKS 8
/* The session tick rate converts this time budget to 16..64 ticks (4.16). */
#define NET_CLIENT_REPLAY_BUDGET_MS 500
#define NET_CLIENT_REPLAY_PRESSURE_MAX 3
#define NET_CLIENT_HIGH_RTT_MS 2000u
#define NET_CLIENT_LOW_RTT_MS 3000u
#define NET_CLIENT_RTT_HYSTERESIS 0.85f

typedef struct tNetClient tNetClient;

typedef enum
{
  NET_RECOVERY_RACING = 0,
  NET_RECOVERY_INSTALLING,
  NET_RECOVERY_RESYNCING
} eNetRecoveryState;

typedef struct
{
  uint32 uiTicks;              /* client ticks simulated */
  uint32 uiBatchesSent;        /* one NET_MSG_INPUT per client tick */
  uint32 uiSnapshots, uiFullSnapshots, uiDeltaSnapshots, uiDroppedDeltas;
  uint32 uiOwnCarStates, uiFeedback;
  uint32 uiEvents, uiWorldChanges, uiCommitsApplied;
  uint32 uiPauseChanges;
  uint32 uiLastAppliedEventSeq, uiCommitWatermark;
  uint32 uiRejectedMessages;   /* race messages that failed to decode */
  uint32 uiStaleMessages;      /* decoded, but older than what was held */
  uint32 uiNewestSnapshotTick; /* uiLastDecodedSnapshotTick on the wire */
  uint32 uiSnapshotAgeMs;
  uint32 uiRampTick;           /* the tick ramps stand at (4.6) */
  uint32 uiRampCorrections;
  uint32 uiCorrections, uiDeferredCorrections, uiReplayTicksTotal;
  uint32 uiTimeDegradedMs;
  uint32 uiPuppetHookCalls, uiPuppetApplications;
  uint32 uiInterpolationUnderruns, uiInterpolationExtrapolations;
  /* The host's cumulative counts from the newest input feedback. */
  uint32 uiHostLateInputs, uiHostFutureInputs;
  int16 nArrivalMarginTicks;
  uint8 byHasHostEstimate;
  uint8 byStalled;
  int iLeadTicks, iLeadBias;
  int iReplayDepth, iReplayBudgetTicks, iReplayPressure;
  int iPredictionMode, iPredictionTransitions;
  int iRaceState, iPaused, iResultsPublished;
  int iResultFinishers, iResultHumanFinishers;
  uint16 unPauseRevision;
  float fHostTick;             /* estimated host tick now, minus uiStartTick */
  float fLeadErrorTicks;       /* timeline position minus (host + lead) */
  float fTickScale, fRttMs, fJitterMs, fFrameMs;
  float fInterpolationDelayMs;
  float fRenderTick, fAppliedRenderTick;
  float fCorrectionMagnitude, fReplayMsTotal, fReplayMsWorst;
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
int NetClientBeginRejoin(tNetClient *pClient,
                         tNetConnection *pConnection);
eNetRecoveryState NetClientRecoveryState(const tNetClient *pClient);
/* Empty during ordinary racing; otherwise the highest-priority non-blocking
   recovery or prediction-mode indicator for the in-race HUD. */
const char *NetClientStatus(const tNetClient *pClient);

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
int NetClientPaused(const tNetClient *pClient);
uint16 NetClientPauseRevision(const tNetClient *pClient);
eNetRaceState NetClientRaceState(const tNetClient *pClient);
int NetClientResults(const tNetClient *pClient, int *piFinishers,
                     int *piHumanFinishers);

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
