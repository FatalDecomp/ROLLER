#include "net_checkpoint.h"

#include "net_event.h"
#include "net_snapshot.h"

#include <string.h>

static void NetCheckpointWire(uint8 *pOut, const uint8 *pIn, int iSize)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  for (int iByte = 0; iByte < iSize; ++iByte)
    pOut[iByte] = pIn[iSize - 1 - iByte];
#else
  memcpy(pOut, pIn, (size_t)iSize);
#endif
}

static int NetCheckpointNameValid(const char *szName)
{
  int iEnded = 0;
  for (int iChar = 0; iChar < 9; ++iChar) {
    uint8 byChar = (uint8)szName[iChar];
    if (iEnded) {
      if (byChar)
        return 0;
    } else if (!byChar) {
      iEnded = 1;
    } else if (byChar < 32 || byChar > 126) {
      return 0;
    }
  }
  return iEnded;
}

static int NetCheckpointPlayerValid(const tNetPlayerEntry *pPlayer)
{
  if (pPlayer->byState == NET_PLAYER_EMPTY)
    return pPlayer->byCarIdx0 == 255 && pPlayer->byCarIdx1 == 255 &&
        !pPlayer->byHumanControl && !pPlayer->szName[0];
  if (pPlayer->byState < NET_PLAYER_LOBBY ||
      pPlayer->byState > NET_PLAYER_FINISHED ||
      pPlayer->byCarIdx0 >= MAX_CARS ||
      (pPlayer->byCarIdx1 != 255 && pPlayer->byCarIdx1 >= MAX_CARS) ||
      pPlayer->byCarIdx0 == pPlayer->byCarIdx1 ||
      (pPlayer->byHumanControl != 1 && pPlayer->byHumanControl != 2))
    return 0;
  return NetCheckpointNameValid(pPlayer->szName);
}

int NetCheckpointHeaderEncode(const tNetCheckpointHeader *pHeader,
                              uint8 *pBytes, int iCapacity)
{
  int iOffset = 0;
  if (!pHeader || !pBytes || iCapacity < (int)sizeof(*pHeader) ||
      !pHeader->byNumCars || pHeader->byNumCars > MAX_CARS ||
      !pHeader->byNumCarParts || !pHeader->byNumPlayers ||
      pHeader->byNumPlayers > NET_SESSION_MAX_PLAYERS ||
      pHeader->byRaceState > NET_RACE_STOPPED || pHeader->byPaused > 1 ||
      pHeader->context.byRaceStarted > 1 || pHeader->context.byRacing > 1 ||
      pHeader->context.nWarpAngle < 0 ||
      pHeader->context.nWarpAngle > 16383)
    return 0;
  memset(pBytes, 0, sizeof(*pHeader));
  for (int iField = 0; iField < 3; ++iField, iOffset += 4)
    NetCheckpointWire(pBytes + iOffset,
                      (const uint8 *)pHeader + iOffset, 4);
  NetCheckpointWire(pBytes + 12, (const uint8 *)&pHeader->context.iGameFrame, 4);
  NetCheckpointWire(pBytes + 16, (const uint8 *)&pHeader->context.iCountdown, 4);
  pBytes[20] = pHeader->context.byRaceStarted;
  pBytes[21] = pHeader->context.byRacing;
  NetCheckpointWire(pBytes + 22, (const uint8 *)&pHeader->context.nWarpAngle, 2);
  NetCheckpointWire(pBytes + 24, (const uint8 *)&pHeader->unPauseRevision, 2);
  pBytes[26] = pHeader->byRaceState;
  pBytes[27] = pHeader->byPaused;
  for (int iRamp = 0; iRamp < NET_MAX_RAMPS; ++iRamp)
    for (int iField = 0; iField < 3; ++iField)
      NetCheckpointWire(pBytes + 28 + iRamp * 6 + iField * 2,
          (const uint8 *)&pHeader->aRamps[iRamp] + iField * 2, 2);
  memcpy(pBytes + 76, &pHeader->byNumCars, 4);
  return sizeof(*pHeader);
}

int NetCheckpointHeaderDecode(const uint8 *pBytes, int iLength,
                              tNetCheckpointHeader *pHeader)
{
  tNetCheckpointHeader header;
  if (!pBytes || !pHeader || iLength != (int)sizeof(header))
    return 0;
  memset(&header, 0, sizeof(header));
  for (int iField = 0; iField < 3; ++iField)
    NetCheckpointWire((uint8 *)&header + iField * 4,
                      pBytes + iField * 4, 4);
  NetCheckpointWire((uint8 *)&header.context.iGameFrame, pBytes + 12, 4);
  NetCheckpointWire((uint8 *)&header.context.iCountdown, pBytes + 16, 4);
  header.context.byRaceStarted = pBytes[20];
  header.context.byRacing = pBytes[21];
  NetCheckpointWire((uint8 *)&header.context.nWarpAngle, pBytes + 22, 2);
  NetCheckpointWire((uint8 *)&header.unPauseRevision, pBytes + 24, 2);
  header.byRaceState = pBytes[26];
  header.byPaused = pBytes[27];
  for (int iRamp = 0; iRamp < NET_MAX_RAMPS; ++iRamp)
    for (int iField = 0; iField < 3; ++iField)
      NetCheckpointWire((uint8 *)&header.aRamps[iRamp] + iField * 2,
                        pBytes + 28 + iRamp * 6 + iField * 2, 2);
  memcpy(&header.byNumCars, pBytes + 76, 4);
  if (!NetCheckpointHeaderEncode(&header, (uint8[sizeof(header)]){0},
                                 sizeof(header)))
    return 0;
  *pHeader = header;
  return 1;
}

int NetCheckpointCarsEncode(uint8 byFirstCar,
                            const tNetCarFullState *pCars, int iCount,
                            uint8 *pBytes, int iCapacity)
{
  int iLength = 2 + iCount * (int)sizeof(*pCars);
  if (!pCars || !pBytes || iCount < 1 ||
      iCount > NET_CHECKPOINT_CARS_PER_MESSAGE ||
      byFirstCar >= MAX_CARS || byFirstCar + iCount > MAX_CARS ||
      iLength > iCapacity)
    return 0;
  pBytes[0] = byFirstCar;
  pBytes[1] = (uint8)iCount;
  for (int iCar = 0; iCar < iCount; ++iCar)
    if (!NetSnapshotEncodeCarFullWire(&pCars[iCar],
            pBytes + 2 + iCar * sizeof(*pCars), sizeof(*pCars)))
      return 0;
  return iLength;
}

int NetCheckpointCarsDecode(const uint8 *pBytes, int iLength,
                            uint8 *pbyFirstCar, tNetCarFullState *pCars,
                            int *piCount)
{
  int iCount;
  if (!pBytes || !pbyFirstCar || !pCars || !piCount || iLength < 2)
    return 0;
  iCount = pBytes[1];
  if (iCount < 1 || iCount > NET_CHECKPOINT_CARS_PER_MESSAGE ||
      pBytes[0] >= MAX_CARS || pBytes[0] + iCount > MAX_CARS ||
      iLength != 2 + iCount * (int)sizeof(*pCars))
    return 0;
  for (int iCar = 0; iCar < iCount; ++iCar)
    if (!NetSnapshotDecodeCarFullWire(
            pBytes + 2 + iCar * sizeof(*pCars), sizeof(*pCars),
            &pCars[iCar]))
      return 0;
  *pbyFirstCar = pBytes[0];
  *piCount = iCount;
  return 1;
}

int NetCheckpointPlayersEncode(const tNetPlayerEntry *pPlayers, int iCount,
                               uint8 *pBytes, int iCapacity)
{
  int iLength = iCount * (int)sizeof(*pPlayers);
  if (!pPlayers || !pBytes || iCount < 1 ||
      iCount > NET_SESSION_MAX_PLAYERS || iLength > iCapacity)
    return 0;
  for (int iPlayer = 0; iPlayer < iCount; ++iPlayer) {
    uint8 *pOut = pBytes + iPlayer * sizeof(*pPlayers);
    if (!NetCheckpointPlayerValid(&pPlayers[iPlayer]))
      return 0;
    pOut[0] = pPlayers[iPlayer].byState;
    pOut[1] = pPlayers[iPlayer].byCarIdx0;
    pOut[2] = pPlayers[iPlayer].byCarIdx1;
    pOut[3] = pPlayers[iPlayer].byHumanControl;
    memcpy(pOut + 4, pPlayers[iPlayer].szName, 9);
  }
  return iLength;
}

int NetCheckpointPlayersDecode(const uint8 *pBytes, int iLength,
                               tNetPlayerEntry *pPlayers, int iCapacity,
                               int *piCount)
{
  tNetPlayerEntry decoded[NET_SESSION_MAX_PLAYERS];
  uint8 abyCars[MAX_CARS] = {0};
  int iCount;
  if (!pBytes || !pPlayers || !piCount || iLength <= 0 ||
      iLength % (int)sizeof(tNetPlayerEntry))
    return 0;
  iCount = iLength / (int)sizeof(tNetPlayerEntry);
  if (iCount > iCapacity || iCount > NET_SESSION_MAX_PLAYERS)
    return 0;
  for (int iPlayer = 0; iPlayer < iCount; ++iPlayer) {
    const uint8 *pIn = pBytes + iPlayer * sizeof(tNetPlayerEntry);
    decoded[iPlayer].byState = pIn[0];
    decoded[iPlayer].byCarIdx0 = pIn[1];
    decoded[iPlayer].byCarIdx1 = pIn[2];
    decoded[iPlayer].byHumanControl = pIn[3];
    memcpy(decoded[iPlayer].szName, pIn + 4, 9);
    if (!NetCheckpointPlayerValid(&decoded[iPlayer]))
      return 0;
    if (decoded[iPlayer].byState == NET_PLAYER_EMPTY)
      continue;
    if (abyCars[decoded[iPlayer].byCarIdx0])
      return 0;
    abyCars[decoded[iPlayer].byCarIdx0] = 1;
    if (decoded[iPlayer].byCarIdx1 != 255) {
      if (abyCars[decoded[iPlayer].byCarIdx1])
        return 0;
      abyCars[decoded[iPlayer].byCarIdx1] = 1;
    }
  }
  memcpy(pPlayers, decoded, (size_t)iCount * sizeof(decoded[0]));
  *piCount = iCount;
  return 1;
}

int NetCheckpointWorldEncode(const tNetWorldChangeEntry *pEntries, int iCount,
                             uint8 *pBytes, int iCapacity)
{
  uint8 abEvent[sizeof(tNetWorldChangeHeader) +
                NET_WORLD_CHANGE_MAX_ENTRIES * sizeof(tNetWorldChangeEntry)];
  int iEventLength, iLength;
  if (!pEntries || !pBytes || iCount < 1 ||
      iCount > NET_WORLD_CHANGE_MAX_ENTRIES)
    return 0;
  iLength = 4 + iCount * (int)sizeof(*pEntries);
  if (iLength > iCapacity)
    return 0;
  iEventLength = NetWorldChangeEncode(1, 1, pEntries, iCount,
                                      abEvent, sizeof(abEvent));
  if (!iEventLength)
    return 0;
  memset(pBytes, 0, (size_t)iLength);
  pBytes[0] = (uint8)iCount;
  memcpy(pBytes + 4, abEvent + sizeof(tNetWorldChangeHeader),
         (size_t)iCount * sizeof(*pEntries));
  return iLength;
}

int NetCheckpointWorldDecode(const uint8 *pBytes, int iLength, int iTrackLen,
                             tNetWorldChangeEntry *pEntries, int iCapacity,
                             int *piCount)
{
  uint8 abEvent[sizeof(tNetWorldChangeHeader) +
                NET_WORLD_CHANGE_MAX_ENTRIES * sizeof(tNetWorldChangeEntry)] = {0};
  tNetWorldChangeHeader header;
  int iCount;
  if (!pBytes || !pEntries || !piCount || iLength < 4 ||
      pBytes[1] || pBytes[2] || pBytes[3])
    return 0;
  iCount = pBytes[0];
  if (iCount < 1 || iCount > iCapacity ||
      iCount > NET_WORLD_CHANGE_MAX_ENTRIES ||
      iLength != 4 + iCount * (int)sizeof(*pEntries))
    return 0;
  abEvent[0] = 1;
  abEvent[4] = 1;
  abEvent[8] = (uint8)iCount;
  memcpy(abEvent + sizeof(tNetWorldChangeHeader), pBytes + 4,
         (size_t)iCount * sizeof(*pEntries));
  if (!NetWorldChangeDecode(abEvent,
          sizeof(tNetWorldChangeHeader) + iCount * sizeof(*pEntries),
          iTrackLen, &header, pEntries, iCapacity))
    return 0;
  *piCount = iCount;
  return 1;
}

int NetCheckpointEndEncode(uint32 uiTick, uint8 *pBytes, int iCapacity)
{
  if (!pBytes || iCapacity < 4)
    return 0;
  NetCheckpointWire(pBytes, (const uint8 *)&uiTick, 4);
  return 4;
}

int NetCheckpointEndDecode(const uint8 *pBytes, int iLength,
                           uint32 *puiTick)
{
  if (!pBytes || !puiTick || iLength != 4)
    return 0;
  NetCheckpointWire((uint8 *)puiTick, pBytes, 4);
  return 1;
}
