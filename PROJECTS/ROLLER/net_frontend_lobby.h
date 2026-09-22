#ifndef ROLLER_NET_FRONTEND_LOBBY_H
#define ROLLER_NET_FRONTEND_LOBBY_H

#include "net_protocol.h"
#include "sound.h"

void NetFrontendSetLocalPort(uint16 unPort);
int NetFrontendSetPeer(const char *szAddress, uint16 unDefaultPort);

int NetFrontendOpen(void);
void NetFrontendClose(void);
int NetFrontendIsOpen(void);
int NetFrontendIsHost(void);

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

#endif
