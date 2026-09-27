#ifndef ROLLER_NET_BOT_H
#define ROLLER_NET_BOT_H

#include "net_input.h"
#include "net_lobby.h"

typedef struct tNetBot tNetBot;

typedef enum
{
  NET_BOT_JOINING = 0,
  NET_BOT_LOBBY,
  NET_BOT_LOADING,
  NET_BOT_RACING,
  NET_BOT_RECOVERING,
  NET_BOT_REFUSED,
  NET_BOT_ERROR
} eNetBotState;

typedef struct
{
  uint32 uiInputsSent;
  uint32 uiSnapshots;
  uint32 uiDroppedDeltas;
  uint32 uiLapCompletions;
  uint32 uiRejectedMessages;
  uint32 uiCheckpoints;
  uint32 uiRejoins;
  uint32 uiLastDecodedSnapshotTick;
  uint32 uiStartTick;
  uint8 byFinished;
} tNetBotStats;

/* A headless protocol client for harness and dedicated-server races.  It owns
   its session and lobby objects but not pConnection or its channel. */
tNetBot *NetBotCreate(tNetConnection *pConnection, const char *szName,
                      uint8 byCarIdx, uint8 byHumanControl,
                      uint32 uiLocalTrackCRC);
void NetBotDestroy(tNetBot *pBot);
int NetBotStart(tNetBot *pBot);

/* Call after the channel pump.  Automates player info, ready, and the loading
   acknowledgement.  The host remains responsible for starting the lobby. */
void NetBotPump(tNetBot *pBot);
eNetBotState NetBotState(const tNetBot *pBot);
eNetJoinRefuseReason NetBotRefuseReason(const tNetBot *pBot);
uint64 NetBotSessionToken(const tNetBot *pBot);
uint8 NetBotGeneration(const tNetBot *pBot);

/* Reuses the accepted identity on a fresh generation and pauses input until
   a complete checkpoint plus a newer full snapshot have been validated. */
int NetBotBeginRejoin(tNetBot *pBot, tNetConnection *pConnection);

/* Queues one consecutive input tick.  NULL selects the simple E2-S7 driver:
   full throttle and centred steering.  The wire batch redundantly carries up
   to NET_INPUT_REDUNDANCY retained ticks. */
int NetBotTick(tNetBot *pBot, uint32 uiTick,
               const tCarInputData *pInput);
uint32 NetBotNextTick(const tNetBot *pBot);
int NetBotStats(const tNetBot *pBot, tNetBotStats *pStats);

#endif
