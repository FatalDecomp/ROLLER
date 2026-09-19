#include "net_session.h"

#include <stdlib.h>
#include <string.h>

enum
{
  NET_HOST_SLOT_EMPTY = 0,
  NET_HOST_SLOT_PENDING,
  NET_HOST_SLOT_JOINED,
  NET_HOST_SLOT_REFUSED
};

typedef struct
{
  tNetConnection *pConnection;
  uint64 ullToken;
  uint8 byState, byPlayerIdx, byRefuseReason, byLocalPlayers;
  char szPlayerName[9];
} tNetHostSlot;

struct tNetSessionHost
{
  tNetChannel *pChannel;
  tNetRandomBytesFn pRandom;
  void *pRandomContext;
  tNetSessionHostMessageFn pMessageCallback;
  void *pMessageContext;
  uint64 ullRandomProof;
  uint8 byMaxPlayers, byHasConfig;
  tNetSessionConfig config;
  tNetHostSlot aSlots[NET_SESSION_MAX_PLAYERS];
};

struct tNetSessionClient
{
  tNetConnection *pConnection;
  uint16 unProtocolVersion;
  uint64 ullToken;
  uint8 byLocalPlayers, byGeneration, byPlayerIdx, byRefuseReason;
  uint8 byHasConfig;
  eNetJoinState state;
  tNetSessionClientMessageFn pMessageCallback;
  void *pMessageContext;
  tNetSessionConfig config;
  char szPlayerName[9];
};

static void NetSessionWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetSessionWrite64(uint8 *pData, uint64 ullValue)
{
  int iByte;
  for (iByte = 0; iByte < 8; ++iByte)
    pData[iByte] = (uint8)(ullValue >> (iByte * 8));
}

static uint16 NetSessionRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint64 NetSessionRead64(const uint8 *pData)
{
  uint64 ullValue = 0;
  int iByte;
  for (iByte = 0; iByte < 8; ++iByte)
    ullValue |= (uint64)pData[iByte] << (iByte * 8);
  return ullValue;
}

static int NetSessionIsControlMessage(uint8 byType)
{
  return byType == NET_MSG_JOIN_REQUEST || byType == NET_MSG_JOIN_ACCEPT ||
         byType == NET_MSG_JOIN_REFUSE || byType == NET_MSG_SESSION_CONFIG;
}

static int NetSessionPlayerNameValid(const char *szName)
{
  int iChar, iTerminated = 0;
  if (!szName || !szName[0])
    return 0;
  for (iChar = 0; iChar < 9; ++iChar) {
    uint8 byChar = (uint8)szName[iChar];
    if (iTerminated) {
      if (byChar)
        return 0;
    } else if (!byChar) {
      iTerminated = 1;
    } else if (byChar < 32 || byChar > 126) {
      return 0;
    }
  }
  return iTerminated;
}

static int NetSessionAddressEqual(const tNetAddress *pA,
                                  const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort ||
      (pA->byFamily == NET_ADDR_IPV6 && pA->uiScopeId != pB->uiScopeId))
    return 0;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 :
      (pA->byFamily == NET_ADDR_IPV6 ? 16 : 0);
  return iLength && !memcmp(pA->abAddress, pB->abAddress, (size_t)iLength);
}

static int NetSessionPlayerCount(const tNetSessionHost *pHost)
{
  int iCount = 0, iSlot;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
    iCount += pHost->aSlots[iSlot].byState == NET_HOST_SLOT_JOINED;
  return iCount;
}

static int NetSessionTokenExists(const tNetSessionHost *pHost,
                                 uint64 ullToken)
{
  int iSlot;
  if (!ullToken)
    return 1;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
    if (pHost->aSlots[iSlot].ullToken == ullToken)
      return 1;
  return 0;
}

static int NetSessionCreateToken(tNetSessionHost *pHost, uint64 *pToken)
{
  int iTry;
  for (iTry = 0; iTry < 4; ++iTry) {
    uint64 ullToken = 0;
    if (!pHost->pRandom(pHost->pRandomContext, &ullToken,
                        (int)sizeof(ullToken)))
      return 0;
    if (!NetSessionTokenExists(pHost, ullToken)) {
      *pToken = ullToken;
      return 1;
    }
  }
  return 0;
}

static tNetConnection *NetSessionAcceptConnection(void *pContext,
                                                   tNetChannel *pChannel,
                                                   const tNetAddress *pPeer)
{
  tNetSessionHost *pHost = (tNetSessionHost *)pContext;
  int iSlot;
  (void)pChannel;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot) {
    tNetAddress peer;
    if (pHost->aSlots[iSlot].byState &&
        NetConnectionPeer(pHost->aSlots[iSlot].pConnection, &peer) &&
        NetSessionAddressEqual(&peer, pPeer))
      return pHost->aSlots[iSlot].pConnection;
  }
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot) {
    tNetHostSlot *pSlot = &pHost->aSlots[iSlot];
    if (pSlot->byState)
      continue;
    pSlot->pConnection = NetChannelAddConnection(pHost->pChannel, pPeer, 0, 0);
    if (!pSlot->pConnection)
      return NULL;
    pSlot->byState = NET_HOST_SLOT_PENDING;
    return pSlot->pConnection;
  }
  return NULL;
}

static void NetSessionQueueRefuse(tNetHostSlot *pSlot,
                                  eNetJoinRefuseReason reason)
{
  uint8 abRefuse[sizeof(tNetJoinRefuse)] = {0};
  abRefuse[0] = (uint8)reason;
  NetSessionWrite16(abRefuse + 2, NET_PROTOCOL_VERSION);
  if (NetConnectionQueueMessage(pSlot->pConnection, NET_MSG_JOIN_REFUSE,
          NET_MSG_RELIABLE | NET_MSG_ORDERED, abRefuse, sizeof(abRefuse))) {
    pSlot->byState = NET_HOST_SLOT_REFUSED;
    pSlot->byRefuseReason = (uint8)reason;
  }
}

static int NetSessionFindPlayerIndex(const tNetSessionHost *pHost)
{
  int iPlayer, iSlot;
  for (iPlayer = 0; iPlayer < NET_SESSION_MAX_PLAYERS; ++iPlayer) {
    int iUsed = 0;
    for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
      iUsed |= pHost->aSlots[iSlot].byState == NET_HOST_SLOT_JOINED &&
          pHost->aSlots[iSlot].byPlayerIdx == iPlayer;
    if (!iUsed)
      return iPlayer;
  }
  return -1;
}

static int NetSessionQueueConfig(const tNetSessionHost *pHost,
                                 tNetHostSlot *pSlot)
{
  uint8 abConfig[NET_SESSION_CONFIG_WIRE_SIZE];
  if (!pHost->byHasConfig)
    return 1;
  if (!NetSessionConfigEncode(&pHost->config, abConfig, sizeof(abConfig)))
    return 0;
  return NetConnectionQueueMessage(pSlot->pConnection,
      NET_MSG_SESSION_CONFIG, NET_MSG_RELIABLE | NET_MSG_ORDERED,
      abConfig, sizeof(abConfig));
}

static void NetSessionAcceptRequest(tNetSessionHost *pHost,
                                    tNetHostSlot *pSlot,
                                    const uint8 *pData)
{
  uint8 abAccept[sizeof(tNetJoinAccept)] = {0};
  uint16 unVersion = NetSessionRead16(pData);
  int iPlayerIdx;
  if (unVersion != NET_PROTOCOL_VERSION) {
    NetSessionQueueRefuse(pSlot, NET_JOIN_REFUSE_VERSION_MISMATCH);
    return;
  }
  if ((pData[2] != 1 && pData[2] != 2) || pData[3] ||
      !NetSessionPlayerNameValid((const char *)pData + 4)) {
    NetSessionQueueRefuse(pSlot, NET_JOIN_REFUSE_INVALID_REQUEST);
    return;
  }
  if (NetSessionPlayerCount(pHost) >= pHost->byMaxPlayers) {
    NetSessionQueueRefuse(pSlot, NET_JOIN_REFUSE_SERVER_FULL);
    return;
  }
  iPlayerIdx = NetSessionFindPlayerIndex(pHost);
  if (iPlayerIdx < 0 || !NetSessionCreateToken(pHost, &pSlot->ullToken)) {
    NetSessionQueueRefuse(pSlot, NET_JOIN_REFUSE_CSPRNG_UNAVAILABLE);
    return;
  }
  pSlot->byPlayerIdx = (uint8)iPlayerIdx;
  pSlot->byLocalPlayers = pData[2];
  memcpy(pSlot->szPlayerName, pData + 4, sizeof(pSlot->szPlayerName));
  NetSessionWrite64(abAccept, pSlot->ullToken);
  abAccept[8] = 1;
  abAccept[9] = pSlot->byPlayerIdx;
  NetConnectionSetIdentity(pSlot->pConnection, pSlot->ullToken, 1);
  if (NetConnectionQueueMessage(pSlot->pConnection, NET_MSG_JOIN_ACCEPT,
          NET_MSG_RELIABLE | NET_MSG_ORDERED, abAccept, sizeof(abAccept))) {
    pSlot->byState = NET_HOST_SLOT_JOINED;
    NetSessionQueueConfig(pHost, pSlot);
  }
}

tNetSessionHost *NetSessionHostCreate(tNetChannel *pChannel,
                                      uint8 byMaxPlayers,
                                      tNetRandomBytesFn pRandom,
                                      void *pRandomContext)
{
  tNetSessionHost *pHost;
  uint64 ullProof = 0;
  if (!pChannel || !pRandom || !byMaxPlayers ||
      byMaxPlayers > NET_SESSION_MAX_PLAYERS ||
      !pRandom(pRandomContext, &ullProof, (int)sizeof(ullProof)) || !ullProof)
    return NULL;
  pHost = (tNetSessionHost *)calloc(1, sizeof(*pHost));
  if (!pHost)
    return NULL;
  pHost->pChannel = pChannel;
  pHost->pRandom = pRandom;
  pHost->pRandomContext = pRandomContext;
  pHost->ullRandomProof = ullProof;
  pHost->byMaxPlayers = byMaxPlayers;
  NetChannelSetAcceptCallback(pChannel, NetSessionAcceptConnection, pHost);
  return pHost;
}

void NetSessionHostDestroy(tNetSessionHost *pHost)
{
  if (!pHost)
    return;
  NetChannelSetAcceptCallback(pHost->pChannel, NULL, NULL);
  free(pHost);
}

void NetSessionHostPump(tNetSessionHost *pHost)
{
  int iSlot;
  if (!pHost)
    return;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot) {
    tNetHostSlot *pSlot = &pHost->aSlots[iSlot];
    tNetMessage message;
    while (pSlot->byState &&
           NetConnectionReceiveMessage(pSlot->pConnection, &message)) {
      /* Join control is reliable-ordered only.  Other messages keep their
         flags; the callback owner decides what each type requires (race
         traffic such as NET_MSG_INPUT is unreliable by design). */
      if (NetSessionIsControlMessage(message.byType) &&
          (message.byFlags & (NET_MSG_RELIABLE | NET_MSG_ORDERED)) !=
          (NET_MSG_RELIABLE | NET_MSG_ORDERED))
        continue;
      if (message.byType == NET_MSG_JOIN_REQUEST &&
          message.unLength == sizeof(tNetJoinRequest)) {
        if (pSlot->byState == NET_HOST_SLOT_PENDING)
          NetSessionAcceptRequest(pHost, pSlot, message.abData);
        else if (pSlot->byState == NET_HOST_SLOT_REFUSED)
          NetSessionQueueRefuse(pSlot,
              (eNetJoinRefuseReason)pSlot->byRefuseReason);
        else if (pSlot->byState == NET_HOST_SLOT_JOINED) {
          uint8 abAccept[sizeof(tNetJoinAccept)] = {0};
          NetSessionWrite64(abAccept, pSlot->ullToken);
          abAccept[8] = 1;
          abAccept[9] = pSlot->byPlayerIdx;
          NetConnectionQueueMessage(pSlot->pConnection, NET_MSG_JOIN_ACCEPT,
              NET_MSG_RELIABLE | NET_MSG_ORDERED, abAccept, sizeof(abAccept));
        }
      } else if (pSlot->byState == NET_HOST_SLOT_JOINED &&
                 pHost->pMessageCallback) {
        pHost->pMessageCallback(pHost->pMessageContext,
                                pSlot->byPlayerIdx, &message);
      }
    }
  }
}

int NetSessionHostSetConfig(tNetSessionHost *pHost,
                            const tNetSessionConfig *pConfig)
{
  if (!pHost || NetSessionPlayerCount(pHost) ||
      !NetSessionConfigValidate(pConfig) ||
      pConfig->byMaxPlayers != pHost->byMaxPlayers)
    return 0;
  pHost->config = *pConfig;
  pHost->byHasConfig = 1;
  return 1;
}

int NetSessionHostGetConfig(const tNetSessionHost *pHost,
                            tNetSessionConfig *pConfig)
{
  if (!pHost || !pConfig || !pHost->byHasConfig)
    return 0;
  *pConfig = pHost->config;
  return 1;
}

void NetSessionHostSetMessageCallback(tNetSessionHost *pHost,
                                      tNetSessionHostMessageFn pCallback,
                                      void *pContext)
{
  if (!pHost)
    return;
  pHost->pMessageCallback = pCallback;
  pHost->pMessageContext = pContext;
}

int NetSessionHostPlayerCount(const tNetSessionHost *pHost)
{
  return pHost ? NetSessionPlayerCount(pHost) : 0;
}

tNetConnection *NetSessionHostPlayerConnection(const tNetSessionHost *pHost,
                                                uint8 byPlayerIdx)
{
  int iSlot;
  if (!pHost)
    return NULL;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
    if (pHost->aSlots[iSlot].byState == NET_HOST_SLOT_JOINED &&
        pHost->aSlots[iSlot].byPlayerIdx == byPlayerIdx)
      return pHost->aSlots[iSlot].pConnection;
  return NULL;
}

uint64 NetSessionHostPlayerToken(const tNetSessionHost *pHost,
                                 uint8 byPlayerIdx)
{
  tNetConnection *pConnection =
      NetSessionHostPlayerConnection(pHost, byPlayerIdx);
  return NetConnectionSessionToken(pConnection);
}

const char *NetSessionHostPlayerName(const tNetSessionHost *pHost,
                                     uint8 byPlayerIdx)
{
  int iSlot;
  if (!pHost)
    return NULL;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
    if (pHost->aSlots[iSlot].byState == NET_HOST_SLOT_JOINED &&
        pHost->aSlots[iSlot].byPlayerIdx == byPlayerIdx)
      return pHost->aSlots[iSlot].szPlayerName;
  return NULL;
}

uint8 NetSessionHostPlayerLocalPlayers(const tNetSessionHost *pHost,
                                       uint8 byPlayerIdx)
{
  int iSlot;
  if (!pHost)
    return 0;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot)
    if (pHost->aSlots[iSlot].byState == NET_HOST_SLOT_JOINED &&
        pHost->aSlots[iSlot].byPlayerIdx == byPlayerIdx)
      return pHost->aSlots[iSlot].byLocalPlayers;
  return 0;
}

int NetSessionHostRefusePlayer(tNetSessionHost *pHost, uint8 byPlayerIdx,
                               eNetJoinRefuseReason reason)
{
  int iSlot;
  if (!pHost || reason <= NET_JOIN_REFUSE_NONE ||
      reason > NET_JOIN_REFUSE_LOAD_TIMEOUT)
    return 0;
  for (iSlot = 0; iSlot < NET_SESSION_MAX_PLAYERS; ++iSlot) {
    tNetHostSlot *pSlot = &pHost->aSlots[iSlot];
    if (pSlot->byState == NET_HOST_SLOT_JOINED &&
        pSlot->byPlayerIdx == byPlayerIdx) {
      NetSessionQueueRefuse(pSlot, reason);
      return pSlot->byState == NET_HOST_SLOT_REFUSED;
    }
  }
  return 0;
}

uint64 NetSessionHostNowMs(const tNetSessionHost *pHost)
{
  return pHost ? NetChannelNowMs(pHost->pChannel) : 0;
}

tNetSessionClient *NetSessionClientCreate(tNetConnection *pConnection,
                                          uint16 unProtocolVersion,
                                          uint8 byLocalPlayers,
                                          const char *szPlayerName)
{
  tNetSessionClient *pClient;
  size_t nameLength;
  if (!pConnection || !szPlayerName ||
      (byLocalPlayers != 1 && byLocalPlayers != 2))
    return NULL;
  nameLength = strlen(szPlayerName);
  if (!nameLength || nameLength > 8) {
    return NULL;
  } else {
    size_t iChar;
    for (iChar = 0; iChar < nameLength; ++iChar)
      if ((uint8)szPlayerName[iChar] < 32 ||
          (uint8)szPlayerName[iChar] > 126)
        return NULL;
  }
  pClient = (tNetSessionClient *)calloc(1, sizeof(*pClient));
  if (!pClient)
    return NULL;
  pClient->pConnection = pConnection;
  pClient->unProtocolVersion = unProtocolVersion;
  pClient->byLocalPlayers = byLocalPlayers;
  memcpy(pClient->szPlayerName, szPlayerName, nameLength + 1);
  return pClient;
}

void NetSessionClientDestroy(tNetSessionClient *pClient)
{
  free(pClient);
}

int NetSessionClientStart(tNetSessionClient *pClient)
{
  uint8 abRequest[sizeof(tNetJoinRequest)] = {0};
  if (!pClient || pClient->state != NET_JOIN_IDLE)
    return 0;
  NetSessionWrite16(abRequest, pClient->unProtocolVersion);
  abRequest[2] = pClient->byLocalPlayers;
  memcpy(abRequest + 4, pClient->szPlayerName,
         sizeof(pClient->szPlayerName));
  if (!NetConnectionQueueMessage(pClient->pConnection, NET_MSG_JOIN_REQUEST,
          NET_MSG_RELIABLE | NET_MSG_ORDERED, abRequest, sizeof(abRequest)))
    return 0;
  pClient->state = NET_JOIN_WAITING;
  return 1;
}

void NetSessionClientPump(tNetSessionClient *pClient)
{
  tNetMessage message;
  if (!pClient)
    return;
  while (NetConnectionReceiveMessage(pClient->pConnection, &message)) {
    if (NetSessionIsControlMessage(message.byType) &&
        (message.byFlags & (NET_MSG_RELIABLE | NET_MSG_ORDERED)) !=
        (NET_MSG_RELIABLE | NET_MSG_ORDERED))
      continue;
    if (pClient->state == NET_JOIN_WAITING &&
        message.byType == NET_MSG_JOIN_ACCEPT &&
        message.unLength == sizeof(tNetJoinAccept)) {
      uint64 ullToken = NetSessionRead64(message.abData);
      uint8 byGeneration = message.abData[8];
      uint8 byPlayerIdx = message.abData[9];
      if (!ullToken || !byGeneration || byPlayerIdx >= NET_SESSION_MAX_PLAYERS ||
          message.abData[10] || message.abData[11])
        continue;
      pClient->ullToken = ullToken;
      pClient->byGeneration = byGeneration;
      pClient->byPlayerIdx = byPlayerIdx;
      pClient->state = NET_JOIN_ACCEPTED;
      NetConnectionSetIdentity(pClient->pConnection, ullToken, byGeneration);
    } else if ((pClient->state == NET_JOIN_WAITING ||
                pClient->state == NET_JOIN_ACCEPTED) &&
               message.byType == NET_MSG_JOIN_REFUSE &&
               message.unLength == sizeof(tNetJoinRefuse) &&
               message.abData[0] > NET_JOIN_REFUSE_NONE &&
               message.abData[0] <= NET_JOIN_REFUSE_LOAD_TIMEOUT &&
               !message.abData[1] &&
               NetSessionRead16(message.abData + 2) == NET_PROTOCOL_VERSION) {
      pClient->byRefuseReason = message.abData[0];
      pClient->state = NET_JOIN_REFUSED;
    } else if (pClient->state == NET_JOIN_ACCEPTED &&
               message.byType == NET_MSG_SESSION_CONFIG) {
      tNetSessionConfig config;
      if (NetSessionConfigDecode(&config, message.abData, message.unLength)) {
        pClient->config = config;
        pClient->byHasConfig = 1;
      }
    } else if (pClient->state == NET_JOIN_ACCEPTED &&
               pClient->pMessageCallback) {
      pClient->pMessageCallback(pClient->pMessageContext, &message);
    }
  }
}

eNetJoinState NetSessionClientState(const tNetSessionClient *pClient)
{
  return pClient ? pClient->state : NET_JOIN_IDLE;
}

eNetJoinRefuseReason NetSessionClientRefuseReason(
    const tNetSessionClient *pClient)
{
  return pClient ? (eNetJoinRefuseReason)pClient->byRefuseReason :
      NET_JOIN_REFUSE_NONE;
}

uint64 NetSessionClientToken(const tNetSessionClient *pClient)
{
  return pClient ? pClient->ullToken : 0;
}

uint8 NetSessionClientGeneration(const tNetSessionClient *pClient)
{
  return pClient ? pClient->byGeneration : 0;
}

uint8 NetSessionClientPlayerIndex(const tNetSessionClient *pClient)
{
  return pClient ? pClient->byPlayerIdx : 0xff;
}

tNetConnection *NetSessionClientConnection(const tNetSessionClient *pClient)
{
  return pClient ? pClient->pConnection : NULL;
}

int NetSessionClientGetConfig(const tNetSessionClient *pClient,
                              tNetSessionConfig *pConfig)
{
  if (!pClient || !pConfig || !pClient->byHasConfig)
    return 0;
  *pConfig = pClient->config;
  return 1;
}

void NetSessionClientSetMessageCallback(tNetSessionClient *pClient,
                                        tNetSessionClientMessageFn pCallback,
                                        void *pContext)
{
  if (!pClient)
    return;
  pClient->pMessageCallback = pCallback;
  pClient->pMessageContext = pContext;
}
