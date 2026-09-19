#include "net_lobby.h"

#include <stdlib.h>
#include <string.h>

struct tNetLobbyHost
{
  tNetSessionHost *pSession;
  tNetSessionConfig config;
  tNetPlayerEntry aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetChat lastChat;
  uint64 ullStartMs;
  uint32 uiStartTick;
  uint16 unRevision, unStartRevision, unRaceRevision;
  uint8 abyRaceLoaded[NET_SESSION_MAX_PLAYERS];
  uint8 byHasStart, byRaceReleased, byHasChat;
  tNetSessionHostMessageFn pRaceCallback;
  void *pRaceContext;
};

struct tNetLobbyClient
{
  tNetSessionClient *pSession;
  tNetPlayerEntry aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetChat lastChat;
  uint32 uiStartTick;
  uint16 unRevision, unStartRevision, unRaceRevision;
  uint8 byPlayerSlots, byHasStart, byRaceReleased, byRaceLoadedSent,
      byHasChat;
  tNetSessionClientMessageFn pRaceCallback;
  void *pRaceContext;
};

static uint16 NetLobbyRead16(const uint8 *pData)
{
  return (uint16)((uint16)pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetLobbyRead32(const uint8 *pData)
{
  return (uint32)pData[0] |
         ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) |
         ((uint32)pData[3] << 24);
}

static void NetLobbyWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetLobbyWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

/* Lobby messages are reliable-ordered.  Anything else that reaches the
   lobby belongs to the race and goes to the race callback. */
static int NetLobbyIsLobbyMessage(uint8 byType)
{
  return byType == NET_MSG_PLAYER_LIST || byType == NET_MSG_PLAYER_INFO ||
         byType == NET_MSG_READY || byType == NET_MSG_CHAT ||
         byType == NET_MSG_COUNTDOWN;
}

static int NetLobbyReliableOrdered(const tNetMessage *pMessage)
{
  return (pMessage->byFlags & (NET_MSG_RELIABLE | NET_MSG_ORDERED)) ==
         (NET_MSG_RELIABLE | NET_MSG_ORDERED);
}

static int NetLobbyRevisionNewer(uint16 unA, uint16 unB)
{
  return (int16)(unA - unB) > 0;
}

static int NetLobbyStringValid(const char *szValue, int iCapacity,
                               int iRequireValue)
{
  int iChar, iLength = -1;
  if (!szValue || iCapacity <= 0)
    return 0;
  for (iChar = 0; iChar < iCapacity; ++iChar) {
    uint8 byChar = (uint8)szValue[iChar];
    if (iLength >= 0) {
      if (byChar)
        return 0;
    } else if (!byChar) {
      iLength = iChar;
    } else if (byChar < 32 || byChar > 126) {
      return 0;
    }
  }
  return iLength >= 0 && (!iRequireValue || iLength > 0);
}

static void NetLobbyClearPlayer(tNetPlayerEntry *pPlayer)
{
  memset(pPlayer, 0, sizeof(*pPlayer));
  pPlayer->byCarIdx0 = NET_LOBBY_NO_PLAYER;
  pPlayer->byCarIdx1 = NET_LOBBY_NO_PLAYER;
}

static int NetLobbyPlayerValid(const tNetPlayerEntry *pPlayer)
{
  if (pPlayer->byState > NET_PLAYER_FINISHED)
    return 0;
  if (pPlayer->byState == NET_PLAYER_EMPTY)
    return pPlayer->byCarIdx0 == NET_LOBBY_NO_PLAYER &&
        pPlayer->byCarIdx1 == NET_LOBBY_NO_PLAYER &&
        !pPlayer->byHumanControl && !pPlayer->szName[0];
  if (pPlayer->byState == NET_PLAYER_JOINING)
    return 0;
  if (pPlayer->byCarIdx0 >= NET_SESSION_MAX_PLAYERS ||
      (pPlayer->byCarIdx1 != NET_LOBBY_NO_PLAYER &&
       pPlayer->byCarIdx1 >= NET_SESSION_MAX_PLAYERS) ||
      pPlayer->byCarIdx0 == pPlayer->byCarIdx1 ||
      (pPlayer->byHumanControl != 1 && pPlayer->byHumanControl != 2))
    return 0;
  return NetLobbyStringValid(pPlayer->szName, sizeof(pPlayer->szName), 1);
}

static uint16 NetLobbyNextRevision(uint16 unRevision)
{
  ++unRevision;
  return unRevision ? unRevision : 1;
}

static int NetLobbyHostQueueAll(tNetLobbyHost *pLobby, uint8 byType,
                                const void *pData, uint16 unLength)
{
  int iQueued = 0;
  int iPlayer;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    tNetConnection *pConnection =
        NetSessionHostPlayerConnection(pLobby->pSession, (uint8)iPlayer);
    /* An expired connection accepts nothing; it must not block the rest. */
    if (pConnection && !NetConnectionIsExpired(pConnection)) {
      if (!NetConnectionQueueMessage(pConnection, byType,
              NET_MSG_RELIABLE | NET_MSG_ORDERED, pData, unLength))
        return 0;
      ++iQueued;
    }
  }
  return iQueued > 0;
}

static int NetLobbyHostBroadcastPlayers(tNetLobbyHost *pLobby,
                                        int iAdvanceRevision)
{
  uint8 abData[sizeof(tNetPlayerListHeader) +
               NET_SESSION_MAX_PLAYERS * sizeof(tNetPlayerEntry)] = {0};
  int iPlayer;
  if (iAdvanceRevision)
    pLobby->unRevision = NetLobbyNextRevision(pLobby->unRevision);
  NetLobbyWrite16(abData, pLobby->unRevision);
  abData[2] = pLobby->config.byMaxPlayers;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    const tNetPlayerEntry *pPlayer = &pLobby->aPlayers[iPlayer];
    uint8 *pEntry = abData + sizeof(tNetPlayerListHeader) +
        iPlayer * sizeof(tNetPlayerEntry);
    pEntry[0] = pPlayer->byState;
    pEntry[1] = pPlayer->byCarIdx0;
    pEntry[2] = pPlayer->byCarIdx1;
    pEntry[3] = pPlayer->byHumanControl;
    memcpy(pEntry + 4, pPlayer->szName, sizeof(pPlayer->szName));
  }
  return NetLobbyHostQueueAll(pLobby, NET_MSG_PLAYER_LIST, abData,
      (uint16)(sizeof(tNetPlayerListHeader) +
               pLobby->config.byMaxPlayers * sizeof(tNetPlayerEntry)));
}

static int NetLobbyHostCarAvailable(const tNetLobbyHost *pLobby,
                                    uint8 byPlayerIdx, uint8 byCarIdx)
{
  int iPlayer;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    const tNetPlayerEntry *pPlayer = &pLobby->aPlayers[iPlayer];
    if (iPlayer != byPlayerIdx && pPlayer->byState != NET_PLAYER_EMPTY &&
        (pPlayer->byCarIdx0 == byCarIdx || pPlayer->byCarIdx1 == byCarIdx))
      return 0;
  }
  return 1;
}

static int NetLobbyHostTryReleaseRace(tNetLobbyHost *pLobby)
{
  uint8 abCountdown[sizeof(tNetCountdown)] = {0};
  uint16 unRaceRevision;
  int iPlayers = 0;
  int iPlayer;
  if (!pLobby || !pLobby->byHasStart || pLobby->byRaceReleased)
    return 0;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    if (pLobby->aPlayers[iPlayer].byState == NET_PLAYER_EMPTY ||
        pLobby->aPlayers[iPlayer].byState == NET_PLAYER_DROPPED)
      continue;
    if (pLobby->aPlayers[iPlayer].byState != NET_PLAYER_RACING ||
        !pLobby->abyRaceLoaded[iPlayer])
      return 0;
    ++iPlayers;
  }
  if (!iPlayers)
    return 0;

  unRaceRevision = NetLobbyNextRevision(pLobby->unRevision);
  NetLobbyWrite32(abCountdown, pLobby->uiStartTick);
  NetLobbyWrite16(abCountdown + 4, unRaceRevision);
  abCountdown[6] = NET_PLAYER_RACING;
  abCountdown[7] = NET_COUNTDOWN_RELEASE;
  if (!NetLobbyHostQueueAll(pLobby, NET_MSG_COUNTDOWN, abCountdown,
                            sizeof(abCountdown)))
    return 0;
  pLobby->unRevision = unRaceRevision;
  pLobby->unRaceRevision = unRaceRevision;
  pLobby->byRaceReleased = 1;
  return 1;
}

/* The race-start barrier gives up on a racing player that has not reported
   loaded once its connection has expired (it can never deliver the message),
   or once the rejoin grace window has passed since the loading countdown.
   Handing the car to the AI at the start line needs E5-S2's ownership
   bookkeeping, so for now the player is dropped: its session is refused and
   the roster keeps it as DROPPED, which leaves its car reserved. */
static void NetLobbyHostDropUnloaded(tNetLobbyHost *pLobby)
{
  int iDeadline, iDropped = 0;
  int iPlayer;
  if (!pLobby->byHasStart || pLobby->byRaceReleased)
    return;
  iDeadline = NetSessionHostNowMs(pLobby->pSession) - pLobby->ullStartMs >=
      NET_REJOIN_GRACE_MS;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    tNetPlayerEntry *pPlayer = &pLobby->aPlayers[iPlayer];
    tNetConnection *pConnection;
    if (pPlayer->byState != NET_PLAYER_RACING ||
        pLobby->abyRaceLoaded[iPlayer])
      continue;
    pConnection = NetSessionHostPlayerConnection(pLobby->pSession,
                                                 (uint8)iPlayer);
    if (!iDeadline && pConnection && !NetConnectionIsExpired(pConnection))
      continue;
    /* Fails harmlessly when the connection has expired. */
    NetSessionHostRefusePlayer(pLobby->pSession, (uint8)iPlayer,
                               NET_JOIN_REFUSE_LOAD_TIMEOUT);
    pPlayer->byState = NET_PLAYER_DROPPED;
    iDropped = 1;
  }
  if (iDropped)
    NetLobbyHostBroadcastPlayers(pLobby, 1);
}

static int NetLobbyDecodeStrategy(tNetChat *pChat, const tNetMessage *pMessage,
                                  int iFromClient)
{
  int iChar;
  if (pMessage->unLength != sizeof(tNetChat))
    return 0;
  memcpy(pChat, pMessage->abData, sizeof(*pChat));
  if (pChat->byKind != NET_CHAT_STRATEGY ||
      pChat->byValue >= NET_LOBBY_STRATEGY_COUNT || pChat->szText[0])
    return 0;
  for (iChar = 0; iChar < (int)sizeof(pChat->szText); ++iChar)
    if (pChat->szText[iChar])
      return 0;
  if (iFromClient)
    return pChat->bySenderPlayerIdx == NET_LOBBY_NO_PLAYER;
  return pChat->bySenderPlayerIdx < NET_SESSION_MAX_PLAYERS;
}

static void NetLobbyHostMessage(void *pContext, uint8 byPlayerIdx,
                                const tNetMessage *pMessage)
{
  tNetLobbyHost *pLobby = (tNetLobbyHost *)pContext;
  tNetPlayerEntry *pPlayer;
  if (!pLobby || byPlayerIdx >= pLobby->config.byMaxPlayers)
    return;
  pPlayer = &pLobby->aPlayers[byPlayerIdx];

  if (!NetLobbyIsLobbyMessage(pMessage->byType)) {
    /* Race traffic only from a player the roster has in the race. */
    if (pLobby->pRaceCallback && pLobby->byRaceReleased &&
        pPlayer->byState == NET_PLAYER_RACING)
      pLobby->pRaceCallback(pLobby->pRaceContext, byPlayerIdx, pMessage);
    return;
  }
  if (!NetLobbyReliableOrdered(pMessage))
    return;

  if (pMessage->byType == NET_MSG_READY &&
      pMessage->unLength == sizeof(tNetReady) &&
      pMessage->abData[4] <= 1 && !pMessage->abData[5] &&
      !pMessage->abData[6] && !pMessage->abData[7]) {
    uint32 uiTrackCRC = NetLobbyRead32(pMessage->abData);
    if (uiTrackCRC != pLobby->config.uiTrackCRC) {
      if (NetSessionHostRefusePlayer(pLobby->pSession, byPlayerIdx,
                                     NET_JOIN_REFUSE_TRACK_CRC_MISMATCH)) {
        NetLobbyClearPlayer(pPlayer);
        NetLobbyHostBroadcastPlayers(pLobby, 1);
      }
      return;
    }
    if (!pLobby->byHasStart &&
        (pPlayer->byState == NET_PLAYER_LOBBY ||
         pPlayer->byState == NET_PLAYER_READY)) {
      pPlayer->byState = pMessage->abData[4] ?
          NET_PLAYER_READY : NET_PLAYER_LOBBY;
      NetLobbyHostBroadcastPlayers(pLobby, 1);
    } else if (pLobby->byHasStart && !pLobby->byRaceReleased &&
               pPlayer->byState == NET_PLAYER_RACING &&
               pMessage->abData[4]) {
      pLobby->abyRaceLoaded[byPlayerIdx] = 1;
      NetLobbyHostTryReleaseRace(pLobby);
    }
  } else if (pMessage->byType == NET_MSG_PLAYER_INFO &&
             pMessage->unLength == sizeof(tNetPlayerInfo) &&
             !pLobby->byHasStart &&
             (pPlayer->byState == NET_PLAYER_LOBBY ||
              pPlayer->byState == NET_PLAYER_READY)) {
    uint8 byCarIdx0 = pMessage->abData[0];
    uint8 byCarIdx1 = pMessage->abData[1];
    uint8 byHumanControl = pMessage->abData[2];
    uint8 byLocalPlayers = NetSessionHostPlayerLocalPlayers(
        pLobby->pSession, byPlayerIdx);
    if (pMessage->abData[3] || byCarIdx0 >= NET_SESSION_MAX_PLAYERS ||
        (byCarIdx1 != NET_LOBBY_NO_PLAYER &&
         byCarIdx1 >= NET_SESSION_MAX_PLAYERS) ||
        byCarIdx0 == byCarIdx1 ||
        (byHumanControl != 1 && byHumanControl != 2) ||
        (byLocalPlayers == 1 && byCarIdx1 != NET_LOBBY_NO_PLAYER) ||
        (byLocalPlayers == 2 && byCarIdx1 == NET_LOBBY_NO_PLAYER) ||
        !NetLobbyHostCarAvailable(pLobby, byPlayerIdx, byCarIdx0) ||
        (byCarIdx1 != NET_LOBBY_NO_PLAYER &&
         !NetLobbyHostCarAvailable(pLobby, byPlayerIdx, byCarIdx1)))
      return;
    pPlayer->byCarIdx0 = byCarIdx0;
    pPlayer->byCarIdx1 = byCarIdx1;
    pPlayer->byHumanControl = byHumanControl;
    NetLobbyHostBroadcastPlayers(pLobby, 1);
  } else if (pMessage->byType == NET_MSG_CHAT) {
    tNetChat chat;
    if (pPlayer->byState == NET_PLAYER_EMPTY ||
        !NetLobbyDecodeStrategy(&chat, pMessage, 1) ||
        (chat.byTargetPlayerIdx != NET_LOBBY_NO_PLAYER &&
         (chat.byTargetPlayerIdx >= pLobby->config.byMaxPlayers ||
          pLobby->aPlayers[chat.byTargetPlayerIdx].byState ==
              NET_PLAYER_EMPTY)))
      return;
    chat.bySenderPlayerIdx = byPlayerIdx;
    pLobby->lastChat = chat;
    pLobby->byHasChat = 1;
    NetLobbyHostQueueAll(pLobby, NET_MSG_CHAT, &chat, sizeof(chat));
  }
}

tNetLobbyHost *NetLobbyHostCreate(tNetSessionHost *pSession)
{
  tNetLobbyHost *pLobby;
  int iPlayer;
  if (!pSession)
    return NULL;
  pLobby = (tNetLobbyHost *)calloc(1, sizeof(*pLobby));
  if (!pLobby)
    return NULL;
  pLobby->pSession = pSession;
  if (!NetSessionHostGetConfig(pSession, &pLobby->config)) {
    free(pLobby);
    return NULL;
  }
  for (iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer)
    NetLobbyClearPlayer(&pLobby->aPlayers[iPlayer]);
  NetSessionHostSetMessageCallback(pSession, NetLobbyHostMessage, pLobby);
  return pLobby;
}

void NetLobbyHostDestroy(tNetLobbyHost *pLobby)
{
  if (!pLobby)
    return;
  NetSessionHostSetMessageCallback(pLobby->pSession, NULL, NULL);
  free(pLobby);
}

void NetLobbyHostPump(tNetLobbyHost *pLobby)
{
  int iChanged = 0;
  int iPlayer;
  if (!pLobby)
    return;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    const char *szName = NetSessionHostPlayerName(
        pLobby->pSession, (uint8)iPlayer);
    tNetPlayerEntry *pPlayer = &pLobby->aPlayers[iPlayer];
    if (szName && pPlayer->byState == NET_PLAYER_EMPTY) {
      pPlayer->byState = NET_PLAYER_LOBBY;
      pPlayer->byCarIdx0 = (uint8)iPlayer;
      pPlayer->byCarIdx1 = NET_LOBBY_NO_PLAYER;
      pPlayer->byHumanControl = (uint8)pLobby->config.iManualControl;
      memcpy(pPlayer->szName, szName, sizeof(pPlayer->szName));
      iChanged = 1;
    } else if (!szName && pPlayer->byState != NET_PLAYER_EMPTY &&
               pPlayer->byState != NET_PLAYER_DROPPED) {
      NetLobbyClearPlayer(pPlayer);
      iChanged = 1;
    }
  }
  if (iChanged)
    NetLobbyHostBroadcastPlayers(pLobby, 1);
  NetLobbyHostDropUnloaded(pLobby);
  NetLobbyHostTryReleaseRace(pLobby);
}

int NetLobbyHostStart(tNetLobbyHost *pLobby, uint32 uiStartTick)
{
  uint8 abCountdown[sizeof(tNetCountdown)] = {0};
  int iPlayers = 0;
  int iPlayer;
  if (!pLobby || pLobby->byHasStart)
    return 0;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    if (pLobby->aPlayers[iPlayer].byState == NET_PLAYER_EMPTY)
      continue;
    if (pLobby->aPlayers[iPlayer].byState != NET_PLAYER_READY)
      return 0;
    ++iPlayers;
  }
  if (!iPlayers)
    return 0;

  pLobby->unRevision = NetLobbyNextRevision(pLobby->unRevision);
  pLobby->unStartRevision = pLobby->unRevision;
  NetLobbyWrite32(abCountdown, uiStartTick);
  NetLobbyWrite16(abCountdown + 4, pLobby->unStartRevision);
  abCountdown[6] = NET_PLAYER_RACING;
  abCountdown[7] = NET_COUNTDOWN_LOADING;
  if (!NetLobbyHostQueueAll(pLobby, NET_MSG_COUNTDOWN, abCountdown,
                            sizeof(abCountdown)))
    return 0;
  pLobby->uiStartTick = uiStartTick;
  pLobby->ullStartMs = NetSessionHostNowMs(pLobby->pSession);
  pLobby->byHasStart = 1;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer)
    if (pLobby->aPlayers[iPlayer].byState != NET_PLAYER_EMPTY)
      pLobby->aPlayers[iPlayer].byState = NET_PLAYER_RACING;
  NetLobbyHostBroadcastPlayers(pLobby, 0);
  return 1;
}

int NetLobbyHostPlayerCount(const tNetLobbyHost *pLobby)
{
  int iCount = 0;
  int iPlayer;
  if (!pLobby)
    return 0;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer)
    iCount += pLobby->aPlayers[iPlayer].byState != NET_PLAYER_EMPTY;
  return iCount;
}

int NetLobbyHostPlayer(const tNetLobbyHost *pLobby, uint8 byPlayerIdx,
                       tNetPlayerEntry *pPlayer)
{
  if (!pLobby || !pPlayer || byPlayerIdx >= pLobby->config.byMaxPlayers)
    return 0;
  *pPlayer = pLobby->aPlayers[byPlayerIdx];
  return pPlayer->byState != NET_PLAYER_EMPTY;
}

int NetLobbyHostAllReady(const tNetLobbyHost *pLobby)
{
  int iPlayers = 0;
  int iPlayer;
  if (!pLobby)
    return 0;
  for (iPlayer = 0; iPlayer < pLobby->config.byMaxPlayers; ++iPlayer) {
    if (pLobby->aPlayers[iPlayer].byState == NET_PLAYER_EMPTY)
      continue;
    if (pLobby->aPlayers[iPlayer].byState != NET_PLAYER_READY)
      return 0;
    ++iPlayers;
  }
  return iPlayers > 0;
}

int NetLobbyHostStartTick(const tNetLobbyHost *pLobby, uint32 *puiStartTick)
{
  if (!pLobby || !puiStartTick || !pLobby->byHasStart)
    return 0;
  *puiStartTick = pLobby->uiStartTick;
  return 1;
}

int NetLobbyHostRaceReleased(const tNetLobbyHost *pLobby)
{
  return pLobby && pLobby->byRaceReleased;
}

void NetLobbyHostSetRaceCallback(tNetLobbyHost *pLobby,
                                 tNetSessionHostMessageFn pCallback,
                                 void *pContext)
{
  if (!pLobby)
    return;
  pLobby->pRaceCallback = pCallback;
  pLobby->pRaceContext = pContext;
}

int NetLobbyHostLastChat(const tNetLobbyHost *pLobby, tNetChat *pChat)
{
  if (!pLobby || !pChat || !pLobby->byHasChat)
    return 0;
  *pChat = pLobby->lastChat;
  return 1;
}

static int NetLobbyClientDecodePlayers(tNetLobbyClient *pLobby,
                                       const tNetMessage *pMessage)
{
  tNetPlayerEntry aPlayers[NET_SESSION_MAX_PLAYERS];
  tNetSessionConfig config;
  uint8 abyCarUsed[NET_SESSION_MAX_PLAYERS] = {0};
  uint16 unRevision;
  uint8 byCount;
  int iPlayer;
  if (pMessage->unLength < sizeof(tNetPlayerListHeader))
    return 0;
  unRevision = NetLobbyRead16(pMessage->abData);
  byCount = pMessage->abData[2];
  if (!NetSessionClientGetConfig(pLobby->pSession, &config) ||
      !unRevision || !byCount || byCount != config.byMaxPlayers ||
      pMessage->abData[3] ||
      pMessage->unLength != sizeof(tNetPlayerListHeader) +
          byCount * sizeof(tNetPlayerEntry))
    return 0;
  if (pLobby->unRevision &&
      !NetLobbyRevisionNewer(unRevision, pLobby->unRevision))
    return 0;
  for (iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer)
    NetLobbyClearPlayer(&aPlayers[iPlayer]);
  for (iPlayer = 0; iPlayer < byCount; ++iPlayer) {
    const uint8 *pEntry = pMessage->abData + sizeof(tNetPlayerListHeader) +
        iPlayer * sizeof(tNetPlayerEntry);
    tNetPlayerEntry *pPlayer = &aPlayers[iPlayer];
    pPlayer->byState = pEntry[0];
    pPlayer->byCarIdx0 = pEntry[1];
    pPlayer->byCarIdx1 = pEntry[2];
    pPlayer->byHumanControl = pEntry[3];
    memcpy(pPlayer->szName, pEntry + 4, sizeof(pPlayer->szName));
    if (!NetLobbyPlayerValid(pPlayer))
      return 0;
    if (pPlayer->byState != NET_PLAYER_EMPTY) {
      if (abyCarUsed[pPlayer->byCarIdx0])
        return 0;
      abyCarUsed[pPlayer->byCarIdx0] = 1;
      if (pPlayer->byCarIdx1 != NET_LOBBY_NO_PLAYER) {
        if (abyCarUsed[pPlayer->byCarIdx1])
          return 0;
        abyCarUsed[pPlayer->byCarIdx1] = 1;
      }
    }
  }
  memcpy(pLobby->aPlayers, aPlayers, sizeof(aPlayers));
  pLobby->byPlayerSlots = byCount;
  pLobby->unRevision = unRevision;
  return 1;
}

static void NetLobbyClientMessage(void *pContext,
                                  const tNetMessage *pMessage)
{
  tNetLobbyClient *pLobby = (tNetLobbyClient *)pContext;
  if (!pLobby)
    return;
  if (!NetLobbyIsLobbyMessage(pMessage->byType)) {
    if (pLobby->pRaceCallback && pLobby->byRaceReleased)
      pLobby->pRaceCallback(pLobby->pRaceContext, pMessage);
    return;
  }
  if (!NetLobbyReliableOrdered(pMessage))
    return;
  if (pMessage->byType == NET_MSG_PLAYER_LIST) {
    NetLobbyClientDecodePlayers(pLobby, pMessage);
  } else if (pMessage->byType == NET_MSG_COUNTDOWN &&
             pMessage->unLength == sizeof(tNetCountdown) &&
             pMessage->abData[6] == NET_PLAYER_RACING &&
             pMessage->abData[7] <= NET_COUNTDOWN_RELEASE) {
    uint16 unRevision = NetLobbyRead16(pMessage->abData + 4);
    uint32 uiStartTick = NetLobbyRead32(pMessage->abData);
    if (pMessage->abData[7] == NET_COUNTDOWN_LOADING && unRevision &&
        (!pLobby->byHasStart ||
         NetLobbyRevisionNewer(unRevision, pLobby->unStartRevision))) {
      pLobby->uiStartTick = uiStartTick;
      pLobby->unStartRevision = unRevision;
      pLobby->byHasStart = 1;
    } else if (pMessage->abData[7] == NET_COUNTDOWN_RELEASE &&
               pLobby->byHasStart && uiStartTick == pLobby->uiStartTick &&
               unRevision &&
               NetLobbyRevisionNewer(unRevision,
                                     pLobby->unStartRevision) &&
               (!pLobby->byRaceReleased ||
                NetLobbyRevisionNewer(unRevision,
                                      pLobby->unRaceRevision))) {
      pLobby->unRaceRevision = unRevision;
      pLobby->byRaceReleased = 1;
    }
  } else if (pMessage->byType == NET_MSG_CHAT) {
    tNetChat chat;
    if (NetLobbyDecodeStrategy(&chat, pMessage, 0) &&
        chat.bySenderPlayerIdx < pLobby->byPlayerSlots &&
        pLobby->aPlayers[chat.bySenderPlayerIdx].byState != NET_PLAYER_EMPTY &&
        (chat.byTargetPlayerIdx == NET_LOBBY_NO_PLAYER ||
         (chat.byTargetPlayerIdx < pLobby->byPlayerSlots &&
          pLobby->aPlayers[chat.byTargetPlayerIdx].byState !=
              NET_PLAYER_EMPTY))) {
      pLobby->lastChat = chat;
      pLobby->byHasChat = 1;
    }
  }
}

tNetLobbyClient *NetLobbyClientCreate(tNetSessionClient *pSession)
{
  tNetLobbyClient *pLobby;
  int iPlayer;
  if (!pSession)
    return NULL;
  pLobby = (tNetLobbyClient *)calloc(1, sizeof(*pLobby));
  if (!pLobby)
    return NULL;
  pLobby->pSession = pSession;
  for (iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer)
    NetLobbyClearPlayer(&pLobby->aPlayers[iPlayer]);
  NetSessionClientSetMessageCallback(pSession, NetLobbyClientMessage, pLobby);
  return pLobby;
}

void NetLobbyClientDestroy(tNetLobbyClient *pLobby)
{
  if (!pLobby)
    return;
  NetSessionClientSetMessageCallback(pLobby->pSession, NULL, NULL);
  free(pLobby);
}

int NetLobbyClientSetPlayerInfo(tNetLobbyClient *pLobby, uint8 byCarIdx0,
                                uint8 byCarIdx1, uint8 byHumanControl)
{
  uint8 abInfo[sizeof(tNetPlayerInfo)] = {0};
  if (!pLobby || NetSessionClientState(pLobby->pSession) !=
      NET_JOIN_ACCEPTED)
    return 0;
  abInfo[0] = byCarIdx0;
  abInfo[1] = byCarIdx1;
  abInfo[2] = byHumanControl;
  return NetConnectionQueueMessage(
      NetSessionClientConnection(pLobby->pSession), NET_MSG_PLAYER_INFO,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, abInfo, sizeof(abInfo));
}

int NetLobbyClientSetReady(tNetLobbyClient *pLobby, int iReady,
                           uint32 uiLocalTrackCRC)
{
  uint8 abReady[sizeof(tNetReady)] = {0};
  if (!pLobby || (iReady != 0 && iReady != 1) ||
      NetSessionClientState(pLobby->pSession) != NET_JOIN_ACCEPTED)
    return 0;
  NetLobbyWrite32(abReady, uiLocalTrackCRC);
  abReady[4] = (uint8)iReady;
  return NetConnectionQueueMessage(
      NetSessionClientConnection(pLobby->pSession), NET_MSG_READY,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, abReady, sizeof(abReady));
}

int NetLobbyClientSendStrategy(tNetLobbyClient *pLobby,
                               uint8 byTargetPlayerIdx,
                               uint8 byStrategy)
{
  tNetChat chat;
  if (!pLobby || byStrategy >= NET_LOBBY_STRATEGY_COUNT ||
      (byTargetPlayerIdx != NET_LOBBY_NO_PLAYER &&
       byTargetPlayerIdx >= NET_SESSION_MAX_PLAYERS) ||
      NetSessionClientState(pLobby->pSession) != NET_JOIN_ACCEPTED)
    return 0;
  memset(&chat, 0, sizeof(chat));
  chat.bySenderPlayerIdx = NET_LOBBY_NO_PLAYER;
  chat.byTargetPlayerIdx = byTargetPlayerIdx;
  chat.byKind = NET_CHAT_STRATEGY;
  chat.byValue = byStrategy;
  return NetConnectionQueueMessage(
      NetSessionClientConnection(pLobby->pSession), NET_MSG_CHAT,
      NET_MSG_RELIABLE | NET_MSG_ORDERED, &chat, sizeof(chat));
}

int NetLobbyClientPlayerCount(const tNetLobbyClient *pLobby)
{
  int iCount = 0;
  int iPlayer;
  if (!pLobby)
    return 0;
  for (iPlayer = 0; iPlayer < pLobby->byPlayerSlots; ++iPlayer)
    iCount += pLobby->aPlayers[iPlayer].byState != NET_PLAYER_EMPTY;
  return iCount;
}

int NetLobbyClientPlayer(const tNetLobbyClient *pLobby, uint8 byPlayerIdx,
                         tNetPlayerEntry *pPlayer)
{
  if (!pLobby || !pPlayer || byPlayerIdx >= pLobby->byPlayerSlots)
    return 0;
  *pPlayer = pLobby->aPlayers[byPlayerIdx];
  return pPlayer->byState != NET_PLAYER_EMPTY;
}

int NetLobbyClientStartTick(const tNetLobbyClient *pLobby,
                            uint32 *puiStartTick)
{
  if (!pLobby || !puiStartTick || !pLobby->byHasStart)
    return 0;
  *puiStartTick = pLobby->uiStartTick;
  return 1;
}

int NetLobbyClientSetRaceLoaded(tNetLobbyClient *pLobby)
{
  tNetSessionConfig config;
  uint8 abReady[sizeof(tNetReady)] = {0};
  if (!pLobby || !pLobby->byHasStart || pLobby->byRaceLoadedSent ||
      NetSessionClientState(pLobby->pSession) != NET_JOIN_ACCEPTED ||
      !NetSessionClientGetConfig(pLobby->pSession, &config))
    return 0;
  NetLobbyWrite32(abReady, config.uiTrackCRC);
  abReady[4] = 1;
  if (!NetConnectionQueueMessage(
          NetSessionClientConnection(pLobby->pSession), NET_MSG_READY,
          NET_MSG_RELIABLE | NET_MSG_ORDERED, abReady, sizeof(abReady)))
    return 0;
  pLobby->byRaceLoadedSent = 1;
  return 1;
}

int NetLobbyClientRaceReleased(const tNetLobbyClient *pLobby,
                               uint32 *puiStartTick)
{
  if (!pLobby || !puiStartTick || !pLobby->byRaceReleased)
    return 0;
  *puiStartTick = pLobby->uiStartTick;
  return 1;
}

void NetLobbyClientSetRaceCallback(tNetLobbyClient *pLobby,
                                   tNetSessionClientMessageFn pCallback,
                                   void *pContext)
{
  if (!pLobby)
    return;
  pLobby->pRaceCallback = pCallback;
  pLobby->pRaceContext = pContext;
}

int NetLobbyClientLastChat(const tNetLobbyClient *pLobby, tNetChat *pChat)
{
  if (!pLobby || !pChat || !pLobby->byHasChat)
    return 0;
  *pChat = pLobby->lastChat;
  return 1;
}
