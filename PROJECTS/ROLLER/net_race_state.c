#include "net_race_state.h"

#include <string.h>

static void NetRaceWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetRaceWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static uint16 NetRaceRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetRaceRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

void NetRaceLifecycleReset(tNetRaceLifecycle *pLifecycle)
{
  if (!pLifecycle)
    return;
  memset(pLifecycle, 0, sizeof(*pLifecycle));
  pLifecycle->byState = NET_RACE_PRE_START;
}

int NetRaceStateValid(uint8 byState)
{
  return byState <= NET_RACE_STOPPED;
}

int NetRaceTransition(tNetRaceLifecycle *pLifecycle, uint8 byState)
{
  if (!pLifecycle || !NetRaceStateValid(byState) ||
      byState != (uint8)(pLifecycle->byState + 1u))
    return 0;
  pLifecycle->byState = byState;
  return 1;
}

int NetRaceApplyPause(tNetRaceLifecycle *pLifecycle,
                      uint16 unRevision, uint8 byPaused)
{
  if (!pLifecycle || !unRevision || byPaused > 1 ||
      (int16)(unRevision - pLifecycle->unPauseRevision) <= 0)
    return 0;
  pLifecycle->unPauseRevision = unRevision;
  pLifecycle->byPaused = byPaused;
  return 1;
}

int NetRacePauseAllowed(const tNetSessionConfig *pConfig)
{
  return pConfig && pConfig->byPauseAllowed && !pConfig->byHostIsDedicated;
}

int NetPauseEncode(const tNetPause *pPause, uint8 *pBytes, int iCapacity)
{
  if (!pPause || !pBytes || iCapacity < (int)sizeof(tNetPause) ||
      !pPause->unPauseRevision || pPause->byPaused > 1 || pPause->byPad)
    return 0;
  memset(pBytes, 0, sizeof(tNetPause));
  NetRaceWrite16(pBytes, pPause->unPauseRevision);
  pBytes[2] = pPause->byPaused;
  NetRaceWrite32(pBytes + 4, pPause->uiTick);
  return sizeof(tNetPause);
}

int NetPauseDecode(const uint8 *pBytes, int iLength, tNetPause *pPause)
{
  tNetPause pause;
  if (!pBytes || !pPause || iLength != (int)sizeof(tNetPause))
    return 0;
  memset(&pause, 0, sizeof(pause));
  pause.unPauseRevision = NetRaceRead16(pBytes);
  pause.byPaused = pBytes[2];
  pause.byPad = pBytes[3];
  pause.uiTick = NetRaceRead32(pBytes + 4);
  if (!pause.unPauseRevision || pause.byPaused > 1 || pause.byPad)
    return 0;
  *pPause = pause;
  return 1;
}
