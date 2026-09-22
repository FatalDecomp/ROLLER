#ifndef ROLLER_NET_LOBBY_H
#define ROLLER_NET_LOBBY_H

#include "net_session.h"

#define NET_LOBBY_NO_PLAYER 255
#define NET_LOBBY_STRATEGY_COUNT 4

typedef struct tNetLobbyHost tNetLobbyHost;
typedef struct tNetLobbyClient tNetLobbyClient;

tNetLobbyHost *NetLobbyHostCreate(tNetSessionHost *pSession);
void NetLobbyHostDestroy(tNetLobbyHost *pLobby);
void NetLobbyHostPump(tNetLobbyHost *pLobby);
int NetLobbyHostStart(tNetLobbyHost *pLobby, uint32 uiStartTick);
int NetLobbyHostPlayerCount(const tNetLobbyHost *pLobby);
int NetLobbyHostPlayer(const tNetLobbyHost *pLobby, uint8 byPlayerIdx,
                       tNetPlayerEntry *pPlayer);
int NetLobbyHostAllReady(const tNetLobbyHost *pLobby);
int NetLobbyHostStartTick(const tNetLobbyHost *pLobby, uint32 *puiStartTick);
int NetLobbyHostRaceReleased(const tNetLobbyHost *pLobby);
int NetLobbyHostLastChat(const tNetLobbyHost *pLobby, tNetChat *pChat);
/* Marks a racing player dropped while retaining its roster entry and cars
   for E5-S3 rejoin.  The updated roster is broadcast to every live player. */
int NetLobbyHostMarkDropped(tNetLobbyHost *pLobby, uint8 byPlayerIdx);
/* Non-lobby messages from a racing player, once the race is released. */
void NetLobbyHostSetRaceCallback(tNetLobbyHost *pLobby,
                                 tNetSessionHostMessageFn pCallback,
                                 void *pContext);

tNetLobbyClient *NetLobbyClientCreate(tNetSessionClient *pSession);
void NetLobbyClientDestroy(tNetLobbyClient *pLobby);
int NetLobbyClientSetPlayerInfo(tNetLobbyClient *pLobby, uint8 byCarIdx0,
                                uint8 byCarIdx1, uint8 byHumanControl);
int NetLobbyClientSetReady(tNetLobbyClient *pLobby, int iReady,
                           uint32 uiLocalTrackCRC);
int NetLobbyClientSendStrategy(tNetLobbyClient *pLobby,
                               uint8 byTargetPlayerIdx,
                               uint8 byStrategy);
int NetLobbyClientPlayerCount(const tNetLobbyClient *pLobby);
int NetLobbyClientPlayer(const tNetLobbyClient *pLobby, uint8 byPlayerIdx,
                         tNetPlayerEntry *pPlayer);
int NetLobbyClientStartTick(const tNetLobbyClient *pLobby,
                            uint32 *puiStartTick);
int NetLobbyClientSetRaceLoaded(tNetLobbyClient *pLobby);
int NetLobbyClientRaceReleased(const tNetLobbyClient *pLobby,
                               uint32 *puiStartTick);
int NetLobbyClientLastChat(const tNetLobbyClient *pLobby, tNetChat *pChat);
/* Non-lobby messages from the host, once the race is released. */
void NetLobbyClientSetRaceCallback(tNetLobbyClient *pLobby,
                                   tNetSessionClientMessageFn pCallback,
                                   void *pContext);

#endif
