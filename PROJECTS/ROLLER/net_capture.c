#include "net_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_CAPTURE_FILE_HEADER_SIZE 16
#define NET_CAPTURE_RECORD_HEADER_SIZE 36
#define NET_CAPTURE_VERSION 1
#define NET_CAPTURE_SEND 1
#define NET_CAPTURE_RECEIVE 2
#define NET_CAPTURE_MAX_FILE_SIZE (64u * 1024u * 1024u)

static const uint8 s_abNetCaptureMagic[8] = {
  'R', 'L', 'R', 'P', 'C', 'A', 'P', '1'
};

typedef struct {
  uint64 ullTimeMs;
  tNetAddress address;
  uint16 unLength;
  uint8 byDirection;
  const uint8 *pData;
} tNetCaptureRecord;

struct tNetPacketCapture {
  tNetTransport transport;
  FILE *pFile;
  size_t iBytes;
  int iOk;
};

struct tNetPacketPlayback {
  uint8 *pFileData;
  size_t iFileSize, iOffset;
  uint64 ullNowMs, ullFirstMs, ullLastMs, ullEndMs;
  int iOk, iComplete;
  tNetCaptureRecord next;
};

static void NetCaptureWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetCaptureWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static void NetCaptureWrite64(uint8 *pData, uint64 ullValue)
{
  NetCaptureWrite32(pData, (uint32)ullValue);
  NetCaptureWrite32(pData + 4, (uint32)(ullValue >> 32));
}

static uint16 NetCaptureRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetCaptureRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static uint64 NetCaptureRead64(const uint8 *pData)
{
  return (uint64)NetCaptureRead32(pData) |
      ((uint64)NetCaptureRead32(pData + 4) << 32);
}

static int NetCaptureAddressValid(const tNetAddress *pAddress,
                                  int iAllowEmpty)
{
  return pAddress &&
      ((iAllowEmpty && pAddress->byFamily == 0) ||
       pAddress->byFamily == NET_ADDR_IPV4 ||
       pAddress->byFamily == NET_ADDR_IPV6);
}

static int NetCaptureAddressEqual(const tNetAddress *pA,
                                  const tNetAddress *pB)
{
  int iLength;
  if (!pA || !pB || pA->byFamily != pB->byFamily ||
      pA->unPort != pB->unPort || pA->uiScopeId != pB->uiScopeId)
    return 0;
  if (!pA->byFamily)
    return 1;
  iLength = pA->byFamily == NET_ADDR_IPV4 ? 4 : 16;
  return !memcmp(pA->abAddress, pB->abAddress, (size_t)iLength);
}

static int NetPacketCaptureWrite(tNetPacketCapture *pCapture,
                                 uint8 byDirection,
                                 const tNetAddress *pAddress,
                                 const void *pData, int iLength)
{
  uint8 abHeader[NET_CAPTURE_RECORD_HEADER_SIZE] = {0};
  tNetAddress empty = {0};
  if (!pCapture->iOk || !pData || iLength < 1 ||
      iLength > NET_MAX_PAYLOAD)
    return 0;
  if (pCapture->iBytes > NET_CAPTURE_MAX_FILE_SIZE -
          NET_CAPTURE_RECORD_HEADER_SIZE - (size_t)iLength) {
    pCapture->iOk = 0;
    return 0;
  }
  if (!pAddress)
    pAddress = &empty;
  NetCaptureWrite64(abHeader, pCapture->transport.pNowMs(
      pCapture->transport.pContext));
  NetCaptureWrite16(abHeader + 8, (uint16)iLength);
  abHeader[10] = byDirection;
  abHeader[11] = pAddress->byFamily;
  NetCaptureWrite16(abHeader + 12, pAddress->unPort);
  NetCaptureWrite32(abHeader + 16, pAddress->uiScopeId);
  memcpy(abHeader + 20, pAddress->abAddress, sizeof(pAddress->abAddress));
  if (fwrite(abHeader, 1, sizeof(abHeader), pCapture->pFile) !=
          sizeof(abHeader) ||
      fwrite(pData, 1, (size_t)iLength, pCapture->pFile) != (size_t)iLength) {
    pCapture->iOk = 0;
    return 0;
  }
  pCapture->iBytes += sizeof(abHeader) + (size_t)iLength;
  return 1;
}

static int NetPacketCaptureSend(void *pContext, const tNetAddress *pTo,
                                const void *pData, int iLength)
{
  tNetPacketCapture *pCapture = pContext;
  if (!pData || iLength < 1 || iLength > NET_MAX_PAYLOAD)
    return -1;
  int iSent = pCapture->transport.pSend(pCapture->transport.pContext, pTo,
                                        pData, iLength);
  if (iSent == iLength)
    NetPacketCaptureWrite(pCapture, NET_CAPTURE_SEND, pTo, pData, iLength);
  return iSent;
}

static int NetPacketCaptureReceive(void *pContext, tNetAddress *pFrom,
                                   void *pData, int iCapacity)
{
  tNetPacketCapture *pCapture = pContext;
  tNetAddress from = {0};
  int iLength = pCapture->transport.pReceive(pCapture->transport.pContext,
      &from, pData, iCapacity);
  if (iLength > 0) {
    if (pFrom)
      *pFrom = from;
    NetPacketCaptureWrite(pCapture, NET_CAPTURE_RECEIVE, &from, pData,
                          iLength);
  }
  return iLength;
}

static uint64 NetPacketCaptureNowMs(void *pContext)
{
  tNetPacketCapture *pCapture = pContext;
  return pCapture->transport.pNowMs(pCapture->transport.pContext);
}

tNetPacketCapture *NetPacketCaptureCreate(tNetTransport transport,
                                          const char *szPath)
{
  uint8 abHeader[NET_CAPTURE_FILE_HEADER_SIZE] = {0};
  tNetPacketCapture *pCapture;
  if (!szPath || !transport.pSend || !transport.pReceive ||
      !transport.pNowMs)
    return NULL;
  pCapture = (tNetPacketCapture *)calloc(1, sizeof(*pCapture));
  if (!pCapture)
    return NULL;
  pCapture->pFile = fopen(szPath, "wb");
  if (!pCapture->pFile) {
    free(pCapture);
    return NULL;
  }
  memcpy(abHeader, s_abNetCaptureMagic, sizeof(s_abNetCaptureMagic));
  NetCaptureWrite16(abHeader + 8, NET_CAPTURE_VERSION);
  NetCaptureWrite16(abHeader + 10, NET_CAPTURE_FILE_HEADER_SIZE);
  pCapture->transport = transport;
  pCapture->iOk = fwrite(abHeader, 1, sizeof(abHeader), pCapture->pFile) ==
      sizeof(abHeader);
  pCapture->iBytes = sizeof(abHeader);
  return pCapture;
}

tNetTransport NetPacketCaptureEndpoint(tNetPacketCapture *pCapture)
{
  tNetTransport transport = {0};
  if (pCapture) {
    transport.pContext = pCapture;
    transport.pSend = NetPacketCaptureSend;
    transport.pReceive = NetPacketCaptureReceive;
    transport.pNowMs = NetPacketCaptureNowMs;
  }
  return transport;
}

int NetPacketCaptureClose(tNetPacketCapture *pCapture)
{
  int iOk;
  if (!pCapture)
    return 0;
  iOk = pCapture->iOk && fflush(pCapture->pFile) == 0;
  if (fclose(pCapture->pFile) != 0)
    iOk = 0;
  free(pCapture);
  return iOk;
}

static int NetPacketPlaybackReadNext(tNetPacketPlayback *pPlayback)
{
  const uint8 *pHeader;
  size_t iRemaining;
  int iFirstRecord = pPlayback->iOffset == NET_CAPTURE_FILE_HEADER_SIZE;
  if (pPlayback->iOffset == pPlayback->iFileSize) {
    pPlayback->iComplete = 1;
    memset(&pPlayback->next, 0, sizeof(pPlayback->next));
    return 1;
  }
  iRemaining = pPlayback->iFileSize - pPlayback->iOffset;
  if (iRemaining < NET_CAPTURE_RECORD_HEADER_SIZE)
    return pPlayback->iOk = 0;
  pHeader = pPlayback->pFileData + pPlayback->iOffset;
  pPlayback->next.ullTimeMs = NetCaptureRead64(pHeader);
  pPlayback->next.unLength = NetCaptureRead16(pHeader + 8);
  pPlayback->next.byDirection = pHeader[10];
  memset(&pPlayback->next.address, 0, sizeof(pPlayback->next.address));
  pPlayback->next.address.byFamily = pHeader[11];
  pPlayback->next.address.unPort = NetCaptureRead16(pHeader + 12);
  pPlayback->next.address.uiScopeId = NetCaptureRead32(pHeader + 16);
  memcpy(pPlayback->next.address.abAddress, pHeader + 20,
         sizeof(pPlayback->next.address.abAddress));
  if ((pPlayback->next.byDirection != NET_CAPTURE_SEND &&
       pPlayback->next.byDirection != NET_CAPTURE_RECEIVE) ||
      !NetCaptureAddressValid(&pPlayback->next.address,
                              pPlayback->next.byDirection ==
                                  NET_CAPTURE_SEND) ||
      pHeader[14] || pHeader[15] ||
      !pPlayback->next.unLength ||
      pPlayback->next.unLength > NET_MAX_PAYLOAD ||
      iRemaining < NET_CAPTURE_RECORD_HEADER_SIZE +
          (size_t)pPlayback->next.unLength ||
      (pPlayback->iOffset > NET_CAPTURE_FILE_HEADER_SIZE &&
       pPlayback->next.ullTimeMs < pPlayback->ullLastMs))
    return pPlayback->iOk = 0;
  pPlayback->next.pData = pHeader + NET_CAPTURE_RECORD_HEADER_SIZE;
  pPlayback->iOffset += NET_CAPTURE_RECORD_HEADER_SIZE +
      pPlayback->next.unLength;
  if (iFirstRecord)
    pPlayback->ullFirstMs = pPlayback->next.ullTimeMs;
  pPlayback->ullLastMs = pPlayback->next.ullTimeMs;
  return 1;
}

static int NetPacketPlaybackSend(void *pContext, const tNetAddress *pTo,
                                 const void *pData, int iLength)
{
  tNetPacketPlayback *pPlayback = pContext;
  tNetAddress empty = {0};
  if (!pTo)
    pTo = &empty;
  if (!pData || iLength < 1 || !pPlayback->iOk || pPlayback->iComplete ||
      pPlayback->next.byDirection != NET_CAPTURE_SEND ||
      pPlayback->next.ullTimeMs != pPlayback->ullNowMs ||
      iLength != pPlayback->next.unLength ||
      !NetCaptureAddressEqual(pTo, &pPlayback->next.address) ||
      memcmp(pData, pPlayback->next.pData, (size_t)iLength)) {
    pPlayback->iOk = 0;
    return -1;
  }
  if (!NetPacketPlaybackReadNext(pPlayback))
    return -1;
  return iLength;
}

static int NetPacketPlaybackReceive(void *pContext, tNetAddress *pFrom,
                                    void *pData, int iCapacity)
{
  tNetPacketPlayback *pPlayback = pContext;
  int iLength;
  if (!pPlayback->iOk || pPlayback->iComplete ||
      pPlayback->next.byDirection != NET_CAPTURE_RECEIVE ||
      pPlayback->next.ullTimeMs > pPlayback->ullNowMs)
    return 0;
  if (!pData || iCapacity < pPlayback->next.unLength) {
    pPlayback->iOk = 0;
    return -1;
  }
  iLength = pPlayback->next.unLength;
  memcpy(pData, pPlayback->next.pData, (size_t)iLength);
  if (pFrom)
    *pFrom = pPlayback->next.address;
  if (!NetPacketPlaybackReadNext(pPlayback))
    return -1;
  return iLength;
}

static uint64 NetPacketPlaybackNowMs(void *pContext)
{
  return ((tNetPacketPlayback *)pContext)->ullNowMs;
}

tNetPacketPlayback *NetPacketPlaybackCreate(const char *szPath)
{
  uint8 abHeader[NET_CAPTURE_FILE_HEADER_SIZE];
  tNetPacketPlayback *pPlayback;
  tNetPacketPlayback scan;
  FILE *pFile;
  long iFileSize;
  if (!szPath || !(pFile = fopen(szPath, "rb")))
    return NULL;
  if (fseek(pFile, 0, SEEK_END) || (iFileSize = ftell(pFile)) <
          NET_CAPTURE_FILE_HEADER_SIZE ||
      (unsigned long)iFileSize > NET_CAPTURE_MAX_FILE_SIZE ||
      fseek(pFile, 0, SEEK_SET)) {
    fclose(pFile);
    return NULL;
  }
  pPlayback = (tNetPacketPlayback *)calloc(1, sizeof(*pPlayback));
  if (!pPlayback) {
    fclose(pFile);
    return NULL;
  }
  pPlayback->pFileData = (uint8 *)malloc((size_t)iFileSize);
  if (!pPlayback->pFileData ||
      fread(pPlayback->pFileData, 1, (size_t)iFileSize, pFile) !=
          (size_t)iFileSize) {
    fclose(pFile);
    NetPacketPlaybackDestroy(pPlayback);
    return NULL;
  }
  fclose(pFile);
  pPlayback->iFileSize = (size_t)iFileSize;
  memcpy(abHeader, pPlayback->pFileData, sizeof(abHeader));
  if (memcmp(abHeader, s_abNetCaptureMagic, sizeof(s_abNetCaptureMagic)) ||
      NetCaptureRead16(abHeader + 8) != NET_CAPTURE_VERSION ||
      NetCaptureRead16(abHeader + 10) != NET_CAPTURE_FILE_HEADER_SIZE ||
      NetCaptureRead32(abHeader + 12) != 0) {
    NetPacketPlaybackDestroy(pPlayback);
    return NULL;
  }
  memset(&scan, 0, sizeof(scan));
  scan.pFileData = pPlayback->pFileData;
  scan.iFileSize = pPlayback->iFileSize;
  scan.iOffset = NET_CAPTURE_FILE_HEADER_SIZE;
  scan.iOk = 1;
  while (!scan.iComplete && NetPacketPlaybackReadNext(&scan)) { }
  if (!scan.iOk) {
    NetPacketPlaybackDestroy(pPlayback);
    return NULL;
  }
  pPlayback->iOffset = NET_CAPTURE_FILE_HEADER_SIZE;
  pPlayback->iOk = 1;
  pPlayback->ullEndMs = scan.ullLastMs;
  if (!NetPacketPlaybackReadNext(pPlayback)) {
    NetPacketPlaybackDestroy(pPlayback);
    return NULL;
  }
  pPlayback->ullNowMs = pPlayback->ullFirstMs;
  return pPlayback;
}

void NetPacketPlaybackDestroy(tNetPacketPlayback *pPlayback)
{
  if (pPlayback) {
    free(pPlayback->pFileData);
    free(pPlayback);
  }
}

tNetTransport NetPacketPlaybackEndpoint(tNetPacketPlayback *pPlayback)
{
  tNetTransport transport = {0};
  if (pPlayback) {
    transport.pContext = pPlayback;
    transport.pSend = NetPacketPlaybackSend;
    transport.pReceive = NetPacketPlaybackReceive;
    transport.pNowMs = NetPacketPlaybackNowMs;
  }
  return transport;
}

int NetPacketPlaybackAdvance(tNetPacketPlayback *pPlayback, uint64 ullNowMs)
{
  if (!pPlayback || !pPlayback->iOk || ullNowMs < pPlayback->ullNowMs ||
      ullNowMs > pPlayback->ullLastMs)
    return 0;
  pPlayback->ullNowMs = ullNowMs;
  return 1;
}

uint64 NetPacketPlaybackFirstMs(const tNetPacketPlayback *pPlayback)
{
  return pPlayback ? pPlayback->ullFirstMs : 0;
}

uint64 NetPacketPlaybackLastMs(const tNetPacketPlayback *pPlayback)
{
  return pPlayback ? pPlayback->ullEndMs : 0;
}

int NetPacketPlaybackComplete(const tNetPacketPlayback *pPlayback)
{
  return pPlayback && pPlayback->iComplete;
}

int NetPacketPlaybackOk(const tNetPacketPlayback *pPlayback)
{
  return pPlayback && pPlayback->iOk;
}
