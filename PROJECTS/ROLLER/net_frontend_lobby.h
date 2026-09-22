#ifndef ROLLER_NET_FRONTEND_LOBBY_H
#define ROLLER_NET_FRONTEND_LOBBY_H

#include "net_protocol.h"
#include "sound.h"

#include <stddef.h>

void NetFrontendSetLocalPort(uint16 unPort);
int NetFrontendSetPeer(const char *szAddress, uint16 unDefaultPort);

int NetFrontendOpen(void);
void NetFrontendClose(void);
int NetFrontendIsOpen(void);
int NetFrontendIsHost(void);
/* Called on the main thread after a mobile app returns to the foreground.
   An active race resumes through the authenticated checkpoint path so wall
   time spent asleep never becomes a client simulation backlog. */
void NetFrontendAppResumed(void);

int NetFrontendLobbyBegin(void);
void NetFrontendPump(void);
int NetFrontendLobbyCanStart(void);
int NetFrontendLobbyRequestStart(uint32 uiStartTick);
int NetFrontendLobbyStartTick(uint32 *puiStartTick);
int NetFrontendRaceSynchronise(void);
int NetFrontendRaceTicksDue(void);
int NetFrontendRaceLocalPlayers(void);
int NetFrontendRaceTick(uint32 uiTick, const tCarInputData *pInputs,
                        int iCount);
int NetFrontendRaceSetPaused(int iPaused);
int NetFrontendRacePaused(void);
eNetRaceState NetFrontendRaceState(void);
int NetFrontendRaceResults(int *piFinishers, int *piHumanFinishers);
int NetFrontendSendStrategy(uint8 byMessage);
const char *NetFrontendLobbyStatus(void);
const char *NetFrontendRaceStatus(void);
int NetFrontendHostNetworkStatus(int iPlayer, char *szStatus,
                                 size_t uiStatusSize);

#endif
