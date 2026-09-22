#ifndef ROLLER_NET_SESSION_H
#define ROLLER_NET_SESSION_H

#include "net_channel.h"
#include "net_config.h"

typedef int (*tNetRandomBytesFn)(void *pContext, void *pData, int iLength);

typedef enum
{
  NET_JOIN_IDLE = 0,
  NET_JOIN_WAITING,
  NET_JOIN_ACCEPTED,
  NET_JOIN_REFUSED
} eNetJoinState;

typedef struct tNetSessionHost tNetSessionHost;
typedef struct tNetSessionClient tNetSessionClient;
typedef void (*tNetSessionHostMessageFn)(void *pContext,
                                         uint8 byPlayerIdx,
                                         const tNetMessage *pMessage);
typedef void (*tNetSessionClientMessageFn)(void *pContext,
                                           const tNetMessage *pMessage);
typedef eNetJoinRefuseReason (*tNetSessionHostRejoinFn)(void *pContext,
                                                        uint8 byPlayerIdx,
                                                        uint64 ullNowMs);

/* Creation verifies the random provider immediately.  A host is never
   advertised or allowed to accept joins without a working CSPRNG. */
tNetSessionHost *NetSessionHostCreate(tNetChannel *pChannel,
                                      uint8 byMaxPlayers,
                                      tNetRandomBytesFn pRandom,
                                      void *pRandomContext);
void NetSessionHostDestroy(tNetSessionHost *pHost);
void NetSessionHostPump(tNetSessionHost *pHost);
/* Configuration must be installed before the first player joins. */
int NetSessionHostSetConfig(tNetSessionHost *pHost,
                            const tNetSessionConfig *pConfig);
int NetSessionHostGetConfig(const tNetSessionHost *pHost,
                            tNetSessionConfig *pConfig);
void NetSessionHostSetMessageCallback(tNetSessionHost *pHost,
                                      tNetSessionHostMessageFn pCallback,
                                      void *pContext);
void NetSessionHostSetRejoinCallback(tNetSessionHost *pHost,
                                     tNetSessionHostRejoinFn pCallback,
                                     void *pContext);
int NetSessionHostPlayerCount(const tNetSessionHost *pHost);
tNetConnection *NetSessionHostPlayerConnection(const tNetSessionHost *pHost,
                                                uint8 byPlayerIdx);
uint64 NetSessionHostPlayerToken(const tNetSessionHost *pHost,
                                 uint8 byPlayerIdx);
const char *NetSessionHostPlayerName(const tNetSessionHost *pHost,
                                     uint8 byPlayerIdx);
uint8 NetSessionHostPlayerLocalPlayers(const tNetSessionHost *pHost,
                                       uint8 byPlayerIdx);
int NetSessionHostRefusePlayer(tNetSessionHost *pHost, uint8 byPlayerIdx,
                               eNetJoinRefuseReason reason);
uint64 NetSessionHostNowMs(const tNetSessionHost *pHost);

tNetSessionClient *NetSessionClientCreate(tNetConnection *pConnection,
                                          uint16 unProtocolVersion,
                                          uint8 byLocalPlayers,
                                          const char *szPlayerName);
void NetSessionClientDestroy(tNetSessionClient *pClient);
int NetSessionClientStart(tNetSessionClient *pClient);
/* Reuses the accepted session identity on a fresh transport connection and
   increments the generation. */
int NetSessionClientRejoin(tNetSessionClient *pClient,
                           tNetConnection *pConnection);
void NetSessionClientPump(tNetSessionClient *pClient);
eNetJoinState NetSessionClientState(const tNetSessionClient *pClient);
eNetJoinRefuseReason NetSessionClientRefuseReason(
    const tNetSessionClient *pClient);
uint64 NetSessionClientToken(const tNetSessionClient *pClient);
uint8 NetSessionClientGeneration(const tNetSessionClient *pClient);
uint8 NetSessionClientPlayerIndex(const tNetSessionClient *pClient);
tNetConnection *NetSessionClientConnection(const tNetSessionClient *pClient);
int NetSessionClientGetConfig(const tNetSessionClient *pClient,
                              tNetSessionConfig *pConfig);
void NetSessionClientSetMessageCallback(tNetSessionClient *pClient,
                                        tNetSessionClientMessageFn pCallback,
                                        void *pContext);

#endif
