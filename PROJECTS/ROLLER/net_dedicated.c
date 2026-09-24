#include "net_dedicated.h"

#include "car.h"
#include "network.h"

#include <stdlib.h>
#include <string.h>

struct tNetDedicated
{
  tNetChannel *pChannel;
  tNetSessionHost *pSession;
  tNetLobbyHost *pLobby;
  tNetHost *pHost;
  tNetSessionConfig config;
  uint64 ullRaceStartMs;
  uint32 uiTicksSimulated;
  int iFinishers, iHumanFinishers;
  eNetDedicatedState state;
};

static void NetDedicatedFail(tNetDedicated *pDedicated)
{
  if (pDedicated)
    pDedicated->state = NET_DEDICATED_ERROR;
}

tNetDedicated *NetDedicatedCreate(tNetChannel *pChannel,
                                  const tNetSessionConfig *pConfig,
                                  tNetRandomBytesFn pRandom,
                                  void *pRandomContext)
{
  tNetDedicated *pDedicated;
  if (!pChannel || !pConfig || !pRandom ||
      !NetSessionConfigValidate(pConfig) ||
      !pConfig->byHostIsDedicated || pConfig->byPauseAllowed ||
      pConfig->iCompetitors != numcars || pConfig->byMaxPlayers > numcars)
    return NULL;
  pDedicated = (tNetDedicated *)calloc(1, sizeof(*pDedicated));
  if (!pDedicated)
    return NULL;
  pDedicated->pChannel = pChannel;
  pDedicated->config = *pConfig;
  pDedicated->pSession = NetSessionHostCreate(
      pChannel, pConfig->byMaxPlayers, pRandom, pRandomContext);
  if (!pDedicated->pSession ||
      !NetSessionHostSetConfig(pDedicated->pSession, pConfig))
    goto fail;
  pDedicated->pLobby = NetLobbyHostCreate(pDedicated->pSession);
  if (!pDedicated->pLobby)
    goto fail;
  pDedicated->pHost = NetHostCreate(pDedicated->pSession,
                                    pDedicated->pLobby);
  if (!pDedicated->pHost)
    goto fail;
  net_mode = NET_MODE_MODERN;
  pDedicated->state = NET_DEDICATED_WAITING;
  return pDedicated;

fail:
  NetDedicatedDestroy(pDedicated);
  return NULL;
}

void NetDedicatedDestroy(tNetDedicated *pDedicated)
{
  if (!pDedicated)
    return;
  NetHostDestroy(pDedicated->pHost);
  NetLobbyHostDestroy(pDedicated->pLobby);
  NetSessionHostDestroy(pDedicated->pSession);
  free(pDedicated);
}

static uint64 NetDedicatedTicksDue(const tNetDedicated *pDedicated,
                                   uint64 ullNowMs)
{
  uint64 ullElapsed = ullNowMs - pDedicated->ullRaceStartMs;
  uint64 ullRate = pDedicated->config.unTickRateHz;
  return (ullElapsed / 1000u) * ullRate +
      ((ullElapsed % 1000u) * ullRate) / 1000u;
}

int NetDedicatedPump(tNetDedicated *pDedicated)
{
  uint64 ullNowMs;
  int iTicksThisPump = 0;
  if (!pDedicated || pDedicated->state == NET_DEDICATED_ERROR)
    return 0;

  NetChannelPump(pDedicated->pChannel);
  NetSessionHostPump(pDedicated->pSession);
  NetLobbyHostPump(pDedicated->pLobby);
  NetHostPump(pDedicated->pHost);
  ullNowMs = NetChannelNowMs(pDedicated->pChannel);

  if (pDedicated->state == NET_DEDICATED_WAITING &&
      NetLobbyHostPlayerCount(pDedicated->pLobby) ==
          pDedicated->config.byMaxPlayers &&
      NetLobbyHostAllReady(pDedicated->pLobby)) {
    if (!NetLobbyHostStart(pDedicated->pLobby,
                           NET_DEDICATED_START_TICK)) {
      NetDedicatedFail(pDedicated);
      return 0;
    }
    pDedicated->state = NET_DEDICATED_LOADING;
  }

  if (pDedicated->state == NET_DEDICATED_LOADING &&
      NetLobbyHostRaceReleased(pDedicated->pLobby)) {
    if (!NetHostBeginRace(pDedicated->pHost)) {
      NetDedicatedFail(pDedicated);
      return 0;
    }
    pDedicated->ullRaceStartMs = ullNowMs;
    pDedicated->state = NET_DEDICATED_RACING;
  }

  while (pDedicated->state == NET_DEDICATED_RACING &&
         pDedicated->uiTicksSimulated <
             NetDedicatedTicksDue(pDedicated, ullNowMs) &&
         iTicksThisPump < NET_DEDICATED_MAX_TICKS_PER_PUMP) {
    if (!NetHostTick(pDedicated->pHost,
                     NetHostNextTick(pDedicated->pHost))) {
      NetDedicatedFail(pDedicated);
      return 0;
    }
    ++pDedicated->uiTicksSimulated;
    ++iTicksThisPump;
    if (NetHostResults(pDedicated->pHost, &pDedicated->iFinishers,
                       &pDedicated->iHumanFinishers))
      pDedicated->state = NET_DEDICATED_COMPLETE;
  }
  return 1;
}

eNetDedicatedState NetDedicatedState(const tNetDedicated *pDedicated)
{
  return pDedicated ? pDedicated->state : NET_DEDICATED_ERROR;
}

uint32 NetDedicatedNextTick(const tNetDedicated *pDedicated)
{
  return pDedicated && pDedicated->pHost ?
      NetHostNextTick(pDedicated->pHost) : 0;
}

int NetDedicatedStats(const tNetDedicated *pDedicated,
                      tNetDedicatedStats *pStats)
{
  if (!pDedicated || !pStats)
    return 0;
  memset(pStats, 0, sizeof(*pStats));
  pStats->uiTicksSimulated = pDedicated->uiTicksSimulated;
  pStats->uiNextTick = NetDedicatedNextTick(pDedicated);
  pStats->iPlayers = NetLobbyHostPlayerCount(pDedicated->pLobby);
  pStats->iFinishers = pDedicated->iFinishers;
  pStats->iHumanFinishers = pDedicated->iHumanFinishers;
  pStats->state = pDedicated->state;
  return 1;
}

int NetDedicatedPlayerStats(const tNetDedicated *pDedicated,
                            uint8 byPlayerIdx,
                            tNetHostPlayerStats *pStats)
{
  return pDedicated && pDedicated->pHost &&
      NetHostPlayerStats(pDedicated->pHost, byPlayerIdx, pStats);
}
