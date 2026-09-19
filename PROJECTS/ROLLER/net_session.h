#ifndef ROLLER_NET_SESSION_H
#define ROLLER_NET_SESSION_H

#include "net_channel.h"

#define NET_SESSION_MAX_PLAYERS 16

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

/* Creation verifies the random provider immediately.  A host is never
   advertised or allowed to accept joins without a working CSPRNG. */
tNetSessionHost *NetSessionHostCreate(tNetChannel *pChannel,
                                      uint8 byMaxPlayers,
                                      tNetRandomBytesFn pRandom,
                                      void *pRandomContext);
void NetSessionHostDestroy(tNetSessionHost *pHost);
void NetSessionHostPump(tNetSessionHost *pHost);
int NetSessionHostPlayerCount(const tNetSessionHost *pHost);
tNetConnection *NetSessionHostPlayerConnection(const tNetSessionHost *pHost,
                                                uint8 byPlayerIdx);
uint64 NetSessionHostPlayerToken(const tNetSessionHost *pHost,
                                 uint8 byPlayerIdx);

tNetSessionClient *NetSessionClientCreate(tNetConnection *pConnection,
                                          uint16 unProtocolVersion,
                                          uint8 byLocalPlayers,
                                          const char *szPlayerName);
void NetSessionClientDestroy(tNetSessionClient *pClient);
int NetSessionClientStart(tNetSessionClient *pClient);
void NetSessionClientPump(tNetSessionClient *pClient);
eNetJoinState NetSessionClientState(const tNetSessionClient *pClient);
eNetJoinRefuseReason NetSessionClientRefuseReason(
    const tNetSessionClient *pClient);
uint64 NetSessionClientToken(const tNetSessionClient *pClient);
uint8 NetSessionClientGeneration(const tNetSessionClient *pClient);
uint8 NetSessionClientPlayerIndex(const tNetSessionClient *pClient);

#endif
