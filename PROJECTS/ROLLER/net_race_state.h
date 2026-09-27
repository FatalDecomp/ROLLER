#ifndef ROLLER_NET_RACE_STATE_H
#define ROLLER_NET_RACE_STATE_H

#include "net_protocol.h"

typedef struct
{
  uint16 unPauseRevision;
  uint8 byState, byPaused;
} tNetRaceLifecycle;

void NetRaceLifecycleReset(tNetRaceLifecycle *pLifecycle);
int NetRaceStateValid(uint8 byState);
int NetRaceTransition(tNetRaceLifecycle *pLifecycle, uint8 byState);

/* Applies only a newer revision.  Returns 1 when applied, 0 when stale or
   malformed.  Revision comparison is serial-number arithmetic, so wrap is
   well-defined while fewer than 32768 revisions are in flight. */
int NetRaceApplyPause(tNetRaceLifecycle *pLifecycle,
                      uint16 unRevision, uint8 byPaused);
int NetRacePauseAllowed(const tNetSessionConfig *pConfig);

/* Explicit little-endian NET_MSG_PAUSE codec. */
int NetPauseEncode(const tNetPause *pPause, uint8 *pBytes, int iCapacity);
int NetPauseDecode(const uint8 *pBytes, int iLength, tNetPause *pPause);

#endif
