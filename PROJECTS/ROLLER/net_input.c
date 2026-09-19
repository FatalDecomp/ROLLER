#include "net_input.h"
#include "car.h"
#include "engines.h"

#include <string.h>

static void NetInputWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetInputWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static uint16 NetInputRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetInputRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static int NetInputBatchShapeValid(uint8 byCount, uint8 byLocalPlayers)
{
  return byCount >= 1 && byCount <= NET_INPUT_REDUNDANCY &&
         byLocalPlayers >= 1 && byLocalPlayers <= NET_INPUT_MAX_LOCAL_PLAYERS;
}

int NetInputBatchEncode(const tNetInputBatch *pBatch, uint8 *pBytes,
                        int iCapacity)
{
  int iLength, iOffset = sizeof(tNetInputBatchHeader);
  if (!pBatch || !pBytes ||
      !NetInputBatchShapeValid(pBatch->byCount, pBatch->byLocalPlayers))
    return 0;
  iLength = iOffset + pBatch->byCount * pBatch->byLocalPlayers * 4;
  if (iLength > iCapacity)
    return 0;
  NetInputWrite32(pBytes, pBatch->uiFirstTick);
  NetInputWrite32(pBytes + 4, pBatch->uiLastDecodedSnapshotTick);
  pBytes[8] = pBatch->byCount;
  pBytes[9] = pBatch->byLocalPlayers;
  for (int iTick = 0; iTick < pBatch->byCount; ++iTick)
    for (int iPlayer = 0; iPlayer < pBatch->byLocalPlayers; ++iPlayer) {
      NetInputWrite16(pBytes + iOffset, pBatch->aInputs[iTick][iPlayer].unInput);
      NetInputWrite16(pBytes + iOffset + 2, pBatch->aInputs[iTick][iPlayer].unFlags);
      iOffset += 4;
    }
  return iLength;
}

int NetInputBatchDecode(const uint8 *pBytes, int iLength,
                        tNetInputBatch *pBatch)
{
  tNetInputBatch batch;
  int iOffset = sizeof(tNetInputBatchHeader);
  if (!pBytes || !pBatch || iLength < iOffset)
    return 0;
  memset(&batch, 0, sizeof(batch));
  batch.uiFirstTick = NetInputRead32(pBytes);
  batch.uiLastDecodedSnapshotTick = NetInputRead32(pBytes + 4);
  batch.byCount = pBytes[8];
  batch.byLocalPlayers = pBytes[9];
  if (!NetInputBatchShapeValid(batch.byCount, batch.byLocalPlayers) ||
      iLength != iOffset + batch.byCount * batch.byLocalPlayers * 4)
    return 0;
  for (int iTick = 0; iTick < batch.byCount; ++iTick)
    for (int iPlayer = 0; iPlayer < batch.byLocalPlayers; ++iPlayer) {
      batch.aInputs[iTick][iPlayer].unInput = NetInputRead16(pBytes + iOffset);
      batch.aInputs[iTick][iPlayer].unFlags = NetInputRead16(pBytes + iOffset + 2);
      iOffset += 4;
    }
  *pBatch = batch;
  return 1;
}

int NetInputFeedbackEncode(const tNetInputFeedback *pFeedback, uint8 *pBytes,
                           int iCapacity)
{
  if (!pFeedback || !pBytes || iCapacity < (int)sizeof(tNetInputFeedback))
    return 0;
  memset(pBytes, 0, sizeof(tNetInputFeedback));
  NetInputWrite32(pBytes, pFeedback->uiHostTick);
  NetInputWrite16(pBytes + 4, pFeedback->unLateInputs);
  NetInputWrite16(pBytes + 6, pFeedback->unFutureInputs);
  NetInputWrite16(pBytes + 8, (uint16)pFeedback->nArrivalMarginTicks);
  return sizeof(tNetInputFeedback);
}

int NetInputFeedbackDecode(const uint8 *pBytes, int iLength,
                           tNetInputFeedback *pFeedback)
{
  tNetInputFeedback feedback;
  if (!pBytes || !pFeedback || iLength != (int)sizeof(tNetInputFeedback) ||
      pBytes[10] || pBytes[11])
    return 0;
  memset(&feedback, 0, sizeof(feedback));
  feedback.uiHostTick = NetInputRead32(pBytes);
  feedback.unLateInputs = NetInputRead16(pBytes + 4);
  feedback.unFutureInputs = NetInputRead16(pBytes + 6);
  feedback.nArrivalMarginTicks = (int16)NetInputRead16(pBytes + 8);
  *pFeedback = feedback;
  return 1;
}

int NetInputClamp(tCarInputData *pInput, int iCar)
{
  int iDesign, iLimit, iSteering;
  int iChanged = 0;
  if (!pInput)
    return 0;
  if (pInput->unFlags & ~NET_INPUT_ALLOWED_FLAGS) {
    pInput->unFlags &= NET_INPUT_ALLOWED_FLAGS;
    iChanged = 1;
  }
  /* readuserdata clamps rud_swheel to +/- iSteeringSensitivity << 8 before
     storing it in the low word of user_inp. */
  iLimit = 0;
  if (iCar >= 0 && iCar < MAX_CARS) {
    iDesign = Car[iCar].byCarDesignIdx;
    if (iDesign < (int)(sizeof(CarEngines.engines) / sizeof(CarEngines.engines[0])))
      iLimit = CarEngines.engines[iDesign].iSteeringSensitivity << 8;
  }
  if (iLimit < 0)
    iLimit = 0;
  if (iLimit > 32767)
    iLimit = 32767;
  iSteering = (int16)pInput->unInput;
  if (iSteering > iLimit || iSteering < -iLimit) {
    iSteering = iSteering > 0 ? iLimit : -iLimit;
    pInput->unInput = (uint16)(int16)iSteering;
    iChanged = 1;
  }
  return iChanged;
}
