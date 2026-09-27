#ifndef ROLLER_NET_CHECKPOINT_H
#define ROLLER_NET_CHECKPOINT_H

#include "net_event.h"
#include "net_config.h"

#define NET_CHECKPOINT_MAX_WORLD_PARTS \
  ((MAX_TRACK_CHUNKS + NET_WORLD_CHANGE_MAX_ENTRIES - 1) / \
   NET_WORLD_CHANGE_MAX_ENTRIES)

int NetCheckpointHeaderEncode(const tNetCheckpointHeader *pHeader,
                              uint8 *pBytes, int iCapacity);
int NetCheckpointHeaderDecode(const uint8 *pBytes, int iLength,
                              tNetCheckpointHeader *pHeader);
int NetCheckpointCarsEncode(uint8 byFirstCar,
                            const tNetCarFullState *pCars, int iCount,
                            uint8 *pBytes, int iCapacity);
int NetCheckpointCarsDecode(const uint8 *pBytes, int iLength,
                            uint8 *pbyFirstCar, tNetCarFullState *pCars,
                            int *piCount);
int NetCheckpointPlayersEncode(const tNetPlayerEntry *pPlayers, int iCount,
                               uint8 *pBytes, int iCapacity);
int NetCheckpointPlayersDecode(const uint8 *pBytes, int iLength,
                               tNetPlayerEntry *pPlayers, int iCapacity,
                               int *piCount);
int NetCheckpointWorldEncode(const tNetWorldChangeEntry *pEntries, int iCount,
                             uint8 *pBytes, int iCapacity);
int NetCheckpointWorldDecode(const uint8 *pBytes, int iLength, int iTrackLen,
                             tNetWorldChangeEntry *pEntries, int iCapacity,
                             int *piCount);
int NetCheckpointEndEncode(uint32 uiTick, uint8 *pBytes, int iCapacity);
int NetCheckpointEndDecode(const uint8 *pBytes, int iLength,
                           uint32 *puiTick);

#endif
