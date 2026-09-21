#include "net_event.h"
#include "3d.h"
#include "loadtrak.h"

#include <string.h>

static void NetEventWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetEventWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static uint16 NetEventRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetEventRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static int NetEventValid(const tNetEvent *pEvent,
                         int iNumCars, int iNumPlayers)
{
  if (!pEvent || !pEvent->uiEventSeq ||
      pEvent->byType < NET_EV_LAP_COMPLETE ||
      pEvent->byType > NET_EV_ABILITY_USED ||
      iNumCars < 1 || iNumCars > MAX_CARS ||
      iNumPlayers < 1 || iNumPlayers > MAX_CARS ||
      (pEvent->byCarIdx != NET_EVENT_NO_CAR &&
       pEvent->byCarIdx >= iNumCars) ||
      (pEvent->byPlayerIdx != NET_EVENT_NO_PLAYER &&
       pEvent->byPlayerIdx >= iNumPlayers) || pEvent->byPad)
    return 0;
  switch (pEvent->byType) {
    case NET_EV_LAP_COMPLETE:
      return pEvent->byCarIdx != NET_EVENT_NO_CAR &&
             pEvent->iArg0 >= 1 && pEvent->iArg0 <= 127 &&
             pEvent->iArg1 >= 0;
    case NET_EV_FINISHED:
      return pEvent->byCarIdx != NET_EVENT_NO_CAR &&
             pEvent->iArg0 >= 0 && pEvent->iArg0 < iNumCars &&
             pEvent->iArg1 >= 1 && pEvent->iArg1 <= iNumCars;
    case NET_EV_DESTROYED:
      return pEvent->byCarIdx != NET_EVENT_NO_CAR &&
             pEvent->iArg0 >= 0 && pEvent->iArg0 < iNumCars &&
             pEvent->iArg1 >= 1 && pEvent->iArg1 <= iNumCars;
    case NET_EV_KILL:
      return pEvent->byCarIdx != NET_EVENT_NO_CAR &&
             pEvent->iArg0 >= -1 && pEvent->iArg0 < iNumCars &&
             pEvent->iArg1 >= 1 && pEvent->iArg1 <= 255;
    default:
      return 1;
  }
}

int NetEventEncode(const tNetEvent *pEvent, int iNumCars, int iNumPlayers,
                   uint8 *pBytes, int iCapacity)
{
  if (!pBytes || iCapacity < (int)sizeof(tNetEvent) ||
      !NetEventValid(pEvent, iNumCars, iNumPlayers))
    return 0;
  memset(pBytes, 0, sizeof(tNetEvent));
  NetEventWrite32(pBytes, pEvent->uiEventSeq);
  NetEventWrite32(pBytes + 4, pEvent->uiTick);
  pBytes[8] = pEvent->byType;
  pBytes[9] = pEvent->byCarIdx;
  pBytes[10] = pEvent->byPlayerIdx;
  NetEventWrite32(pBytes + 12, (uint32)pEvent->iArg0);
  NetEventWrite32(pBytes + 16, (uint32)pEvent->iArg1);
  return sizeof(tNetEvent);
}

int NetEventDecode(const uint8 *pBytes, int iLength,
                   int iNumCars, int iNumPlayers, tNetEvent *pEvent)
{
  tNetEvent event;
  if (!pBytes || !pEvent || iLength != (int)sizeof(tNetEvent))
    return 0;
  memset(&event, 0, sizeof(event));
  event.uiEventSeq = NetEventRead32(pBytes);
  event.uiTick = NetEventRead32(pBytes + 4);
  event.byType = pBytes[8];
  event.byCarIdx = pBytes[9];
  event.byPlayerIdx = pBytes[10];
  event.byPad = pBytes[11];
  event.iArg0 = (int32)NetEventRead32(pBytes + 12);
  event.iArg1 = (int32)NetEventRead32(pBytes + 16);
  if (!NetEventValid(&event, iNumCars, iNumPlayers))
    return 0;
  *pEvent = event;
  return 1;
}

static int NetWorldChangeEntryValid(const tNetWorldChangeEntry *pEntry,
                                    int iTrackLen)
{
  return pEntry && iTrackLen > 0 && iTrackLen <= MAX_TRACK_CHUNKS &&
         pEntry->nChunk >= 0 && pEntry->nChunk < iTrackLen &&
         pEntry->byCenterGrip < NET_WORLD_GRIP_COUNT &&
         pEntry->byLeftShoulderGrip < NET_WORLD_GRIP_COUNT &&
         pEntry->byRightShoulderGrip < NET_WORLD_GRIP_COUNT &&
         !pEntry->byPad;
}

int NetWorldChangeCapture(int iChunk, tNetWorldChangeEntry *pEntry)
{
  tNetWorldChangeEntry entry;
  if (!pEntry || TRAK_LEN <= 0 || TRAK_LEN > MAX_TRACK_CHUNKS ||
      iChunk < 0 || iChunk >= TRAK_LEN)
    return 0;
  if (localdata[iChunk].iCenterGrip < 0 ||
      localdata[iChunk].iCenterGrip >= NET_WORLD_GRIP_COUNT ||
      localdata[iChunk].iLeftShoulderGrip < 0 ||
      localdata[iChunk].iLeftShoulderGrip >= NET_WORLD_GRIP_COUNT ||
      localdata[iChunk].iRightShoulderGrip < 0 ||
      localdata[iChunk].iRightShoulderGrip >= NET_WORLD_GRIP_COUNT)
    return 0;
  memset(&entry, 0, sizeof(entry));
  entry.nChunk = (int16)iChunk;
  entry.byCenterGrip = (uint8)localdata[iChunk].iCenterGrip;
  entry.byLeftShoulderGrip = (uint8)localdata[iChunk].iLeftShoulderGrip;
  entry.byRightShoulderGrip = (uint8)localdata[iChunk].iRightShoulderGrip;
  for (int iLane = 0; iLane < 3; ++iLane)
    entry.auiTrakColour[iLane] = (uint32)TrakColour[iChunk][iLane];
  *pEntry = entry;
  return 1;
}

int NetWorldChangeEntryEqual(const tNetWorldChangeEntry *pLeft,
                             const tNetWorldChangeEntry *pRight)
{
  return pLeft && pRight &&
         pLeft->nChunk == pRight->nChunk &&
         pLeft->byCenterGrip == pRight->byCenterGrip &&
         pLeft->byLeftShoulderGrip == pRight->byLeftShoulderGrip &&
         pLeft->byRightShoulderGrip == pRight->byRightShoulderGrip &&
         pLeft->auiTrakColour[0] == pRight->auiTrakColour[0] &&
         pLeft->auiTrakColour[1] == pRight->auiTrakColour[1] &&
         pLeft->auiTrakColour[2] == pRight->auiTrakColour[2];
}

int NetWorldChangeEncode(uint32 uiEventSeq, uint32 uiTick,
                         const tNetWorldChangeEntry *pEntries, int iCount,
                         uint8 *pBytes, int iCapacity)
{
  int iLength;
  if (!uiEventSeq || !pEntries || !pBytes || iCount < 1 ||
      iCount > NET_WORLD_CHANGE_MAX_ENTRIES)
    return 0;
  iLength = (int)sizeof(tNetWorldChangeHeader) +
            iCount * (int)sizeof(tNetWorldChangeEntry);
  if (iLength > iCapacity)
    return 0;
  for (int iEntry = 0; iEntry < iCount; ++iEntry) {
    if (!NetWorldChangeEntryValid(&pEntries[iEntry], TRAK_LEN))
      return 0;
    for (int iPrior = 0; iPrior < iEntry; ++iPrior)
      if (pEntries[iPrior].nChunk == pEntries[iEntry].nChunk)
        return 0;
  }
  memset(pBytes, 0, (size_t)iLength);
  NetEventWrite32(pBytes, uiEventSeq);
  NetEventWrite32(pBytes + 4, uiTick);
  pBytes[8] = (uint8)iCount;
  for (int iEntry = 0; iEntry < iCount; ++iEntry) {
    const tNetWorldChangeEntry *pEntry = &pEntries[iEntry];
    uint8 *pOut = pBytes + sizeof(tNetWorldChangeHeader) +
                  iEntry * sizeof(tNetWorldChangeEntry);
    NetEventWrite16(pOut, (uint16)pEntry->nChunk);
    pOut[2] = pEntry->byCenterGrip;
    pOut[3] = pEntry->byLeftShoulderGrip;
    pOut[4] = pEntry->byRightShoulderGrip;
    for (int iLane = 0; iLane < 3; ++iLane)
      NetEventWrite32(pOut + 6 + iLane * 4, pEntry->auiTrakColour[iLane]);
  }
  return iLength;
}

int NetWorldChangeDecode(const uint8 *pBytes, int iLength, int iTrackLen,
                         tNetWorldChangeHeader *pHeader,
                         tNetWorldChangeEntry *pEntries, int iCapacity)
{
  tNetWorldChangeHeader header;
  tNetWorldChangeEntry aEntries[NET_WORLD_CHANGE_MAX_ENTRIES];
  if (!pBytes || !pHeader || !pEntries ||
      iLength < (int)sizeof(tNetWorldChangeHeader))
    return 0;
  memset(&header, 0, sizeof(header));
  header.uiEventSeq = NetEventRead32(pBytes);
  header.uiTick = NetEventRead32(pBytes + 4);
  header.byCount = pBytes[8];
  header.byPad[0] = pBytes[9];
  header.byPad[1] = pBytes[10];
  header.byPad[2] = pBytes[11];
  if (!header.uiEventSeq || header.byCount < 1 ||
      header.byCount > NET_WORLD_CHANGE_MAX_ENTRIES ||
      header.byCount > iCapacity || header.byPad[0] || header.byPad[1] ||
      header.byPad[2] ||
      iLength != (int)sizeof(tNetWorldChangeHeader) +
                 header.byCount * (int)sizeof(tNetWorldChangeEntry))
    return 0;
  memset(aEntries, 0, sizeof(aEntries));
  for (int iEntry = 0; iEntry < header.byCount; ++iEntry) {
    tNetWorldChangeEntry *pEntry = &aEntries[iEntry];
    const uint8 *pIn = pBytes + sizeof(tNetWorldChangeHeader) +
                       iEntry * sizeof(tNetWorldChangeEntry);
    pEntry->nChunk = (int16)NetEventRead16(pIn);
    pEntry->byCenterGrip = pIn[2];
    pEntry->byLeftShoulderGrip = pIn[3];
    pEntry->byRightShoulderGrip = pIn[4];
    pEntry->byPad = pIn[5];
    for (int iLane = 0; iLane < 3; ++iLane)
      pEntry->auiTrakColour[iLane] = NetEventRead32(pIn + 6 + iLane * 4);
    if (!NetWorldChangeEntryValid(pEntry, iTrackLen))
      return 0;
    for (int iPrior = 0; iPrior < iEntry; ++iPrior)
      if (aEntries[iPrior].nChunk == pEntry->nChunk)
        return 0;
  }
  *pHeader = header;
  memcpy(pEntries, aEntries, header.byCount * sizeof(aEntries[0]));
  return 1;
}
