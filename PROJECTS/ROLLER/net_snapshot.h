#ifndef ROLLER_NET_SNAPSHOT_H
#define ROLLER_NET_SNAPSHOT_H
#include "net_protocol.h"
#include "car.h"

int NetSnapshotBuild(tNetSnapshot *pSnapshot, uint32 uiTick, uint32 uiLastEventSeq,
                     uint8 byRaceState, uint8 byPaused);
int NetSnapshotEncodeCarFull(int iCar, tNetCarFullState *pState);
int NetSnapshotDecodeCarFull(int iCar, const tNetCarFullState *pState);
/* Install display-grade snapshot state for a puppet.  The world pose is
   converted against the caller's current ramp geometry; no full-state-only
   field is touched. */
int NetSnapshotApplyPuppet(int iCar, const tNetCarState *pState);
int NetSnapshotInterpolate(const tNetCarState *pOlder, const tNetCarState *pNewer,
                           float fFraction, tNetCarState *pResult);
int NetSnapshotEncode(const tNetSnapshot *pSnapshot, uint8 *pBytes, int iCapacity);
int NetSnapshotDecode(const uint8 *pBytes, int iLength, tNetSnapshot *pSnapshot);
int NetSnapshotEncodeDelta(const tNetSnapshot *pBase, const tNetSnapshot *pCurrent,
                           uint8 *pBytes, int iCapacity);
/* NET_MSG_OWN_CAR_STATE (5.6): header, then per car { byCarIdx, pad[3], extra }. */
#define NET_OWN_CAR_ENTRY_SIZE (4 + (int)sizeof(tNetCarExtra))
int NetSnapshotEncodeOwnCarState(uint32 uiTick, const uint8 *pbyCars,
                                 const tNetCarExtra *pExtras, int iCount,
                                 uint8 *pBytes, int iCapacity);
int NetSnapshotDecodeOwnCarState(const uint8 *pBytes, int iLength, uint32 *puiTick,
                                 uint8 *pbyCars, tNetCarExtra *pExtras, int *piCount);
int NetSnapshotDecodeDelta(const tNetSnapshot *pBase, const uint8 *pBytes, int iLength,
                           tNetSnapshot *pResult);
#endif
