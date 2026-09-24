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

/* Queues one consecutive input tick.  NULL selects the simple E2-S7 driver:
   full throttle and centred steering.  The wire batch redundantly carries up
   to NET_INPUT_REDUNDANCY retained ticks. */
int NetBotTick(tNetBot *pBot, uint32 uiTick,
               const tCarInputData *pInput);
int NetBotStats(const tNetBot *pBot, tNetBotStats *pStats);

#endif
