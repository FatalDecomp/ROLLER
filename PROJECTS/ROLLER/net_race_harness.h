#ifndef ROLLER_NET_RACE_HARNESS_H
#define ROLLER_NET_RACE_HARNESS_H

#include "net_test_socket.h"

typedef struct tNetRaceHarness tNetRaceHarness;

tNetRaceHarness *NetRaceHarnessCreate(tNetSocket udpSocket);
void NetRaceHarnessDestroy(tNetRaceHarness *pHarness);

/* Returns non-zero when szLine belongs to the race-scenario command set.
   Every handled command writes one JSON line to szReply. */
int NetRaceHarnessCommand(tNetRaceHarness *pHarness, const char *szLine,
                          char *szReply, int iReplyCapacity);

#endif
