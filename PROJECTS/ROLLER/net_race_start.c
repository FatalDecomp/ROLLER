#include "net_race_start.h"

#include <string.h>

static tNetRaceStartClock s_raceClock;

void NetRaceClockReset(tNetRaceStartClock *pClock)
{
  if (pClock)
    memset(pClock, 0, sizeof(*pClock));
}

int NetRaceClockSchedule(tNetRaceStartClock *pClock, uint32 uiStartTick)
{
  if (!pClock || pClock->ePhase != NET_RACE_START_IDLE)
    return 0;
  pClock->uiStartTick = uiStartTick;
  pClock->ePhase = NET_RACE_START_LOADING;
  return 1;
}

int NetRaceClockRelease(tNetRaceStartClock *pClock, uint32 uiStartTick)
{
  if (!pClock || pClock->ePhase != NET_RACE_START_LOADING ||
      pClock->uiStartTick != uiStartTick)
    return 0;
  pClock->uiNextTick = uiStartTick;
  pClock->uiCurrentTick = uiStartTick;
  pClock->ePhase = NET_RACE_START_PRE_START;
  return 1;
}

int NetRaceClockBeginTick(tNetRaceStartClock *pClock, uint32 *puiTick)
{
  if (!pClock || !puiTick ||
      (pClock->ePhase != NET_RACE_START_PRE_START &&
       pClock->ePhase != NET_RACE_START_RUNNING))
    return 0;
  pClock->uiCurrentTick = pClock->uiNextTick++;
  *puiTick = pClock->uiCurrentTick;
  return 1;
}

void NetRaceClockEndTick(tNetRaceStartClock *pClock, int iGameFrame)
{
  if (!pClock || pClock->ePhase != NET_RACE_START_PRE_START ||
      iGameFrame != 145)
    return;
  pClock->uiRunningTick = pClock->uiCurrentTick;
  pClock->ePhase = NET_RACE_START_RUNNING;
}

eNetRaceStartPhase NetRaceClockPhase(const tNetRaceStartClock *pClock)
{
  return pClock ? pClock->ePhase : NET_RACE_START_IDLE;
}

uint32 NetRaceClockRunningTick(const tNetRaceStartClock *pClock)
{
  return pClock ? pClock->uiRunningTick : 0;
}

void NetRaceStartReset(void)
{
  NetRaceClockReset(&s_raceClock);
}

int NetRaceStartSchedule(uint32 uiStartTick)
{
  return NetRaceClockSchedule(&s_raceClock, uiStartTick);
}

int NetRaceStartRelease(uint32 uiStartTick)
{
  return NetRaceClockRelease(&s_raceClock, uiStartTick);
}

int NetRaceStartBeginTick(uint32 *puiTick)
{
  return NetRaceClockBeginTick(&s_raceClock, puiTick);
}

void NetRaceStartEndTick(int iGameFrame)
{
  NetRaceClockEndTick(&s_raceClock, iGameFrame);
}

eNetRaceStartPhase NetRaceStartPhase(void)
{
  return NetRaceClockPhase(&s_raceClock);
}

uint32 NetRaceStartRunningTick(void)
{
  return NetRaceClockRunningTick(&s_raceClock);
}
