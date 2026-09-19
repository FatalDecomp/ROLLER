#ifndef ROLLER_NET_FRONTEND_LOBBY_H
#define ROLLER_NET_FRONTEND_LOBBY_H

#include "types.h"

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
const char *NetFrontendLobbyStatus(void);

#endif
