#ifndef ROLLER_NET_SNAPSHOT_H
#define ROLLER_NET_SNAPSHOT_H
#include "net_protocol.h"
#include "car.h"

int NetSnapshotBuild(tNetSnapshot *pSnapshot, uint32 uiTick, uint32 uiLastEventSeq,
                     uint8 byRaceState, uint8 byPaused);
int NetSnapshotEncodeCarFull(int iCar, tNetCarFullState *pState);
int NetSnapshotDecodeCarFull(int iCar, const tNetCarFullState *pState);
int NetSnapshotInterpolate(const tNetCarState *pOlder, const tNetCarState *pNewer,
                           float fFraction, tNetCarState *pResult);
int NetSnapshotEncode(const tNetSnapshot *pSnapshot, uint8 *pBytes, int iCapacity);
int NetSnapshotDecode(const uint8 *pBytes, int iLength, tNetSnapshot *pSnapshot);
int NetSnapshotEncodeDelta(const tNetSnapshot *pBase, const tNetSnapshot *pCurrent,
                           uint8 *pBytes, int iCapacity);
int NetSnapshotDecodeDelta(const tNetSnapshot *pBase, const uint8 *pBytes, int iLength,
                           tNetSnapshot *pResult);
#endif
