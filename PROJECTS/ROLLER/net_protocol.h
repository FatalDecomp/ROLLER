#ifndef ROLLER_NET_PROTOCOL_H
#define ROLLER_NET_PROTOCOL_H
#include "net_types.h"

#define NET_COMMUNITY_TRACK_FILENAME (ROLLER_MAX_PATH - 16)
#define NET_MAX_RAMPS 8
#define NET_SNAPSHOT_RETENTION_MS 600
#define NET_INPUT_HISTORY 256
#define NET_INPUT_QUEUE 64
#define NET_INPUT_HORIZON 48
#define NET_REPLAY_BUDGET_MS 500
/* roller-core advances the legacy simulation at 36 Hz.  Higher negotiated
   rates convert the same 500 ms budget at runtime. */
#define NET_MAX_REPLAY_TICKS 18
#define NET_MSG_RELIABLE 1
#define NET_MSG_ORDERED 2
#pragma pack(push, 1)
#define NET_PROTOCOL_ID      0x524C5231u  /* 'RLR1' */
#define NET_PROTOCOL_VERSION 1
#define NET_MAX_PAYLOAD      1200
#define NET_ACK_BITS         32

typedef struct
{
  uint32 uiProtocolId;
  uint16 unSequence, unAck;
  uint32 uiAckBits;
  uint64 ullSessionToken;
  uint8  byGeneration;     /* incremented per (re)connection; older values dropped */
  uint8  byMessageCount;
} tNetPacketHeader;        /* 22 bytes */

typedef struct
{
  uint8  byType, byFlags;  /* NET_MSG_RELIABLE, NET_MSG_ORDERED */
  uint16 unLength, unReliableSeq;
} tNetMessageHeader;       /* 6 bytes */

typedef enum
{
  NET_MSG_PING = 1, NET_MSG_PONG,
  NET_MSG_JOIN_REQUEST, NET_MSG_JOIN_ACCEPT, NET_MSG_JOIN_REFUSE,
  NET_MSG_SESSION_CONFIG, NET_MSG_PLAYER_LIST, NET_MSG_PLAYER_INFO,
  NET_MSG_READY, NET_MSG_CHAT, NET_MSG_COUNTDOWN,
  NET_MSG_INPUT, NET_MSG_SNAPSHOT, NET_MSG_SNAPSHOT_DELTA,
  NET_MSG_OWN_CAR_STATE, NET_MSG_EVENT, NET_MSG_WORLD_CHANGE,
  NET_MSG_PAUSE, NET_MSG_INPUT_FEEDBACK, NET_MSG_LEAVE,
  NET_MSG_REJOIN_REQUEST, NET_MSG_CHECKPOINT_REQUEST,
  NET_MSG_CHECKPOINT_HEADER, NET_MSG_CHECKPOINT_CARS,
  NET_MSG_CHECKPOINT_PLAYERS, NET_MSG_CHECKPOINT_WORLD,
  NET_MSG_CHECKPOINT_END, NET_MSG_KICK
} eNetMessageType;

typedef enum
{
  NET_JOIN_REFUSE_NONE = 0,
  NET_JOIN_REFUSE_VERSION_MISMATCH,
  NET_JOIN_REFUSE_SERVER_FULL,
  NET_JOIN_REFUSE_CSPRNG_UNAVAILABLE,
  NET_JOIN_REFUSE_INVALID_REQUEST
} eNetJoinRefuseReason;

typedef struct
{
  uint16 unProtocolVersion;
  uint8 byLocalPlayers, byReserved;
  char szPlayerName[9];
} tNetJoinRequest;                         /* 13 bytes */

typedef struct
{
  uint64 ullSessionToken;
  uint8 byGeneration, byPlayerIdx;
  uint8 byPad[2];
} tNetJoinAccept;                          /* 12 bytes */

typedef struct
{
  uint8 byReason, byPad;
  uint16 unExpectedVersion;
} tNetJoinRefuse;                          /* 4 bytes */

typedef struct
{
  uint16 unProtocolVersion, unTickRateHz;   /* 36, 50, 100 */
  uint8  bySnapshotInterval, byMaxPlayers, byHostIsDedicated, byPauseAllowed;
  int32  iTrackLoad, iGameType, iManualControl, iLevelFlags;
  int32  iCompetitors, iDamageLevel, iTextureMode, iNetworkChampOn;
  uint32 uiRandomSeed, uiTrackCRC, uiCommunityTrackCRC;
  char   szCommunityTrack[NET_COMMUNITY_TRACK_FILENAME];
  char   szBuildHash[16];
} tNetSessionConfig;

#define NET_INPUT_REDUNDANCY 8

typedef struct
{
  uint32 uiFirstTick, uiLastDecodedSnapshotTick;
  uint8  byCount;                     /* 1..8, consecutive ticks */
  uint8  byLocalPlayers;              /* 1 or 2 */
  /* followed by byCount * byLocalPlayers tCarInputData (4 bytes), [tick][player] */
} tNetInputBatchHeader;               /* 10 bytes */

typedef struct
{
  uint32 uiHostTick;
  uint16 unLateInputs, unFutureInputs;
  int16  nArrivalMarginTicks;
  uint8  byPad[2];
} tNetInputFeedback;                  /* 12 bytes, every 250 ms per client */

/* The per-tick globals a replay must restore that a recovering client cannot
   get from a ring, because recovery clears the rings (4.7). Carried by every
   snapshot and every checkpoint. E0-S10's enumeration may add fields here; if
   it does, the sizes below change with it. */
typedef struct
{
  int32  iGameFrame;        /* the start gate at control.c:813 reads this */
  int32  iCountdown;
  uint8  byRaceStarted;     /* gates lap timing at control.c:3241 */
  uint8  byRacing;
  int16  nWarpAngle;
} tNetSimContextWire;                          /* 12 bytes */

typedef struct
{
  float  fWorldPosX, fWorldPosY, fWorldPosZ;   /* always world (D15) */
  float  fFinalSpeed, fHorizontalSpeed;
  float  fVelX, fVelY, fVelZ;                  /* world velocity (tCar.direction when airborne) */
  int16  nCurrChunk;                           /* -1 = airborne; frame indicator */
  int16  nReferenceChunk, nLastValidChunk;
  int16  nWorldRoll, nWorldPitch, nWorldYaw, nActualYaw;
  int16  nDeathTimer, nJumpMomentum;
  uint8  byHealth, byLives, byLap, byRacePosition;
  uint8  byStatusFlags, byStunned, byDamageIntensity, byDamageState;
  uint8  byWheelAnimationFrame, byGearAyMax;
  uint8  byHumanControl, byControlType, byCheatAmmo, byPad;
} tNetCarState;                                /* 64 bytes, 31 scalars */

typedef struct { int16 nTickStartIdx, nTimingGroup2, nRunningTimer; } tNetRampState;  /* 6 */

typedef struct
{
  uint32 uiTick, uiLastEventSeq, uiRandomState;
  tNetSimContextWire context;
  uint8  byNumCars, byRaceState, byPaused, byNumRamps;
  uint8  byPad[4];
  tNetRampState aRamps[8];
  tNetCarState  aCars[MAX_CARS];
} tNetSnapshot;                                /* 32 + 48 + 1024 = 1104 bytes */

typedef struct
{
  uint32 uiTick, uiBaseTick, uiLastEventSeq, uiRandomState;
  tNetSimContextWire context;   /* full in every delta: no field mask covers it (4.8) */
  uint8  byNumCars, byRaceState, byPaused, byNumRamps;
  uint16 unCarMask, unRampMask;
} tNetSnapshotDeltaHeader;                     /* 36 bytes */

typedef struct
{
  float  fRunningLapTime, fBestLapTime, fPreviousLapTime, fTotalRaceTime;   /* 16 */
  float  fBaseSpeed, fSpeedOverflow, fPower, fDurability, fRPMRatio;        /* 20 */
  int32  iRollMomentum, iRollMotion, iPitchMotion, iYawMotion, iEngineState;/* 20 */
  int32  iSteeringInput, iBankingSteerOffset;                              /* 8 */
  int16  nTargetChunk, nChangeMateCooldown;                                 /* 4 */
  uint8  byKills, byAttacker, byLapNumber, byFinishPosition;                /* 4 */
  uint8  byDamageToggle, byCheatCooldown, byEngineStartTimer, byPad;        /* 4 */
  uint8  byThrottlePressed, byAccelerating, byAIThrottleControl, byPitLaneActiveFlag; /* 4 */
  uint8  byCollisionTimer, byPad2[3];                                       /* 4 */
  /* Exact local angles remove the one-unit ambiguity in the integer
     world/local transforms before a predicted car is replayed. */
  int16  nLocalYaw, nLocalPitch, nLocalRoll, nLocalActualYaw;                /* 8 */
} tNetCarExtra;                                /* 92 bytes */

typedef struct { tNetCarState state; tNetCarExtra extra; } tNetCarFullState;  /* 156 */

typedef struct
{
  uint32 uiTick;
  uint8  byCount;           /* 1 or 2: one entry per rollback-group car */
  uint8  byPad[3];
  /* followed by byCount x { uint8 byCarIdx; uint8 byPad2[3]; tNetCarExtra extra; } (96 each) */
} tNetOwnCarStateHeader;    /* 8 bytes; 104 for one car, 200 for two */

typedef enum
{
  NET_EV_LAP_COMPLETE = 1, NET_EV_FINISHED, NET_EV_DESTROYED, NET_EV_RESPAWN,
  NET_EV_RECORD_LAP, NET_EV_PLAYER_JOINED, NET_EV_PLAYER_LEFT,
  NET_EV_AI_TAKEOVER, NET_EV_PLAYER_REJOINED, NET_EV_RACE_STATE, NET_EV_RESULTS,
  NET_EV_KILL, NET_EV_ABILITY_USED
} eNetEventType;

typedef struct
{
  uint32 uiEventSeq;   /* monotonic per session, shared with world changes */
  uint32 uiTick;
  uint8  byType, byCarIdx, byPlayerIdx, byPad;
  int32  iArg0, iArg1;
} tNetEvent;           /* 20 bytes */

typedef struct
{
  uint16 unPauseRevision;
  uint8  byPaused, byPad;
  uint32 uiTick;
} tNetPause;           /* 8 bytes */

typedef struct
{
  uint32 uiTick;            /* C */
  uint32 uiLastEventSeq, uiRandomState;
  tNetSimContextWire context;    /* what Phase 1 bootstraps the context ring from */
  uint16 unPauseRevision;
  uint8  byRaceState, byPaused;
  tNetRampState aRamps[8];
  uint8  byNumCars, byNumCarParts, byNumPlayers, byNumWorldParts;
} tNetCheckpointHeader;     /* 80 bytes */

/* NET_MSG_CHECKPOINT_CARS:  uint8 byFirstCar, uint8 byCount, then byCount x tNetCarFullState
                             (<= 7, 1038 bytes payload) */
/* NET_MSG_CHECKPOINT_WORLD: uint8 byCount, uint8 byPad[3], then byCount x tNetWorldChangeEntry
                             (<= 64, 1156 bytes payload) */
/* NET_MSG_CHECKPOINT_END:   uint32 uiTick */

typedef struct
{
  uint8  byState;           /* eNetPlayerState */
  uint8  byCarIdx0, byCarIdx1;   /* 255 = none */
  uint8  byHumanControl;
  char   szName[9];
} tNetPlayerEntry;          /* 13 bytes; up to 16 per message */

typedef struct
{
  int16  nChunk;
  uint8  byCenterGrip, byLeftShoulderGrip, byRightShoulderGrip, byPad;
  uint32 auiTrakColour[3];  /* full 32-bit TrakColour[nChunk][0..2] */
} tNetWorldChangeEntry;     /* 18 bytes */

typedef struct
{
  uint32 uiEventSeq, uiTick;
  uint8  byCount;           /* <= 64 */
  uint8  byPad[3];
} tNetWorldChangeHeader;    /* 12 bytes; 64 entries = 1164 payload, 1192 on the wire */

typedef struct {
  uint32 uiSessionId;
  uint16 unPort, unTickRateHz;
  uint8 byPlayers, byMaxPlayers, byFlags;
  char szName[32], szTrack[26], szBuildHash[16];
} tRvzSessionInfo;
#pragma pack(pop)

_Static_assert(sizeof(tNetPacketHeader) == 22, "tNetPacketHeader wire size");
_Static_assert(sizeof(tNetMessageHeader) == 6, "tNetMessageHeader wire size");
_Static_assert(sizeof(tNetJoinRequest) == 13, "tNetJoinRequest wire size");
_Static_assert(sizeof(tNetJoinAccept) == 12, "tNetJoinAccept wire size");
_Static_assert(sizeof(tNetJoinRefuse) == 4, "tNetJoinRefuse wire size");
_Static_assert(sizeof(tNetInputBatchHeader) == 10, "tNetInputBatchHeader wire size");
_Static_assert(sizeof(tNetInputFeedback) == 12, "tNetInputFeedback wire size");
_Static_assert(sizeof(tNetSimContextWire) == 12, "tNetSimContextWire wire size");
_Static_assert(sizeof(tNetCarState) == 64, "tNetCarState wire size");
_Static_assert(sizeof(tNetRampState) == 6, "tNetRampState wire size");
_Static_assert(sizeof(tNetSnapshot) == 1104, "tNetSnapshot wire size");
_Static_assert(sizeof(tNetSnapshotDeltaHeader) == 36, "tNetSnapshotDeltaHeader wire size");
_Static_assert(sizeof(tNetCarExtra) == 92, "tNetCarExtra wire size");
_Static_assert(sizeof(tNetCarFullState) == 156, "tNetCarFullState wire size");
_Static_assert(sizeof(tNetOwnCarStateHeader) == 8, "tNetOwnCarStateHeader wire size");
_Static_assert(sizeof(tNetEvent) == 20, "tNetEvent wire size");
_Static_assert(sizeof(tNetPause) == 8, "tNetPause wire size");
_Static_assert(sizeof(tNetCheckpointHeader) == 80, "tNetCheckpointHeader wire size");
_Static_assert(sizeof(tNetPlayerEntry) == 13, "tNetPlayerEntry wire size");
_Static_assert(sizeof(tNetWorldChangeEntry) == 18, "tNetWorldChangeEntry wire size");
_Static_assert(sizeof(tNetWorldChangeHeader) == 12, "tNetWorldChangeHeader wire size");
_Static_assert(sizeof(tRvzSessionInfo) == 85, "rendezvous entry wire size");
_Static_assert(sizeof(tNetSnapshot) + 28 <= NET_MAX_PAYLOAD, "snapshot fits");
_Static_assert(8 + 2 * (4 + sizeof(tNetCarExtra)) + 28 <= NET_MAX_PAYLOAD, "own cars fit");
#define NET_CHECKPOINT_CARS_PER_MESSAGE ((NET_MAX_PAYLOAD - 28 - 2) / sizeof(tNetCarFullState))
_Static_assert(NET_CHECKPOINT_CARS_PER_MESSAGE == 7, "checkpoint car count");
_Static_assert(NET_CHECKPOINT_CARS_PER_MESSAGE * sizeof(tNetCarFullState) + 2 + 28 <= NET_MAX_PAYLOAD, "checkpoint cars fit");
_Static_assert(64 * 18 + 12 + 28 <= NET_MAX_PAYLOAD, "world changes fit");
_Static_assert(16 * 13 + 28 <= NET_MAX_PAYLOAD, "players fit");
_Static_assert(12 * 85 + 28 <= NET_MAX_PAYLOAD, "rendezvous page fits");
#endif
