#ifndef ROLLER_NET_RACE_START_H
#define ROLLER_NET_RACE_START_H

#include "types.h"

typedef enum
{
  NET_RACE_START_IDLE = 0,
  NET_RACE_START_LOADING,
  NET_RACE_START_PRE_START,
  NET_RACE_START_RUNNING
} eNetRaceStartPhase;

typedef struct
{
  uint32 uiStartTick;
  uint32 uiNextTick;
  uint32 uiCurrentTick;
  uint32 uiRunningTick;
  eNetRaceStartPhase ePhase;
} tNetRaceStartClock;

void NetRaceClockReset(tNetRaceStartClock *pClock);
int NetRaceClockSchedule(tNetRaceStartClock *pClock, uint32 uiStartTick);
int NetRaceClockRelease(tNetRaceStartClock *pClock, uint32 uiStartTick);
int NetRaceClockBeginTick(tNetRaceStartClock *pClock, uint32 *puiTick);
void NetRaceClockEndTick(tNetRaceStartClock *pClock, int iGameFrame);
eNetRaceStartPhase NetRaceClockPhase(const tNetRaceStartClock *pClock);
uint32 NetRaceClockRunningTick(const tNetRaceStartClock *pClock);

void NetRaceStartReset(void);
int NetRaceStartSchedule(uint32 uiStartTick);
int NetRaceStartRelease(uint32 uiStartTick);
int NetRaceStartBeginTick(uint32 *puiTick);
void NetRaceStartEndTick(int iGameFrame);
eNetRaceStartPhase NetRaceStartPhase(void);
uint32 NetRaceStartRunningTick(void);

#endif
