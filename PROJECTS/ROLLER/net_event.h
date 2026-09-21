#ifndef ROLLER_NET_EVENT_H
#define ROLLER_NET_EVENT_H

#include "net_protocol.h"

#define NET_EVENT_NO_CAR 255u
#define NET_EVENT_NO_PLAYER 255u
#define NET_WORLD_CHANGE_MAX_ENTRIES 64
#define NET_WORLD_GRIP_COUNT 14

/* Explicit little-endian codecs for host commits.  Decoders validate the
   loaded-world bounds before publishing any output (D23). */
int NetEventEncode(const tNetEvent *pEvent, int iNumCars, int iNumPlayers,
                   uint8 *pBytes, int iCapacity);
int NetEventDecode(const uint8 *pBytes, int iLength,
                   int iNumCars, int iNumPlayers, tNetEvent *pEvent);

/* Captures the complete mutable surface state for one loaded track chunk. */
int NetWorldChangeCapture(int iChunk, tNetWorldChangeEntry *pEntry);
int NetWorldChangeEntryEqual(const tNetWorldChangeEntry *pLeft,
                             const tNetWorldChangeEntry *pRight);

int NetWorldChangeEncode(uint32 uiEventSeq, uint32 uiTick,
                         const tNetWorldChangeEntry *pEntries, int iCount,
                         uint8 *pBytes, int iCapacity);
int NetWorldChangeDecode(const uint8 *pBytes, int iLength, int iTrackLen,
                         tNetWorldChangeHeader *pHeader,
                         tNetWorldChangeEntry *pEntries, int iCapacity);

#endif
