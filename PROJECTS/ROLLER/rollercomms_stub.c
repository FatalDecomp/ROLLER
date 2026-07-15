/*
 * Browser networking is intentionally unavailable until a WebSocket/WebRTC
 * transport is designed. These stubs preserve the native communications API,
 * report transport initialization failure, and keep all other operations
 * inert so the frontend can follow its normal "no network" path.
 */
#include "rollercomms.h"

#include <SDL3/SDL.h>
#include <string.h>

static int s_iCommsType;
static bool s_bInitFailureLogged;

static void ROLLERCommsClearString(char *szBuf, int iBufLen)
{
  if (szBuf && iBufLen > 0)
    szBuf[0] = '\0';
}

void ROLLERCommsSetLocalPort(uint16_t unPort)
{
  (void)unPort;
}

void ROLLERCommsSetPeer(const char *szIP, uint16_t unPort)
{
  (void)szIP;
  (void)unPort;
}

void ROLLERCommsSetLocalIP(const char *szIP)
{
  (void)szIP;
}

int ROLLERCommsInitSystem(unsigned int uiMaxPackets)
{
  (void)uiMaxPackets;
  if (!s_bInitFailureLogged) {
    SDL_Log("rollercomms: networking is unavailable in browser builds");
    s_bInitFailureLogged = true;
  }
  return 0;
}

void ROLLERCommsUnInitSystem(void)
{
}

void ROLLERCommsSetType(int iType)
{
  s_iCommsType = iType;
}

int ROLLERCommsGetType(void)
{
  return s_iCommsType;
}

void ROLLERCommsUpdateLocalAddrForPeer(const void *pPeerAddress)
{
  (void)pPeerAddress;
}

int ROLLERCommsGetActiveNodes(void)
{
  return 0;
}

int ROLLERCommsGetConsoleNode(void)
{
  return 0;
}

int ROLLERCommsAddNode(const void *pAddress)
{
  (void)pAddress;
  return 2;
}

void ROLLERCommsUpdateNodeTransportAddr(const void *pAddress,
                                        const void *pTransportAddress)
{
  (void)pAddress;
  (void)pTransportAddress;
}

int ROLLERCommsDeleteNode(int iNodeIdx)
{
  (void)iNodeIdx;
  return 1;
}

void ROLLERCommsSortNodes(void)
{
}

int ROLLERCommsNetAddrToNode(const int *pAddress)
{
  (void)pAddress;
  return -1;
}

void ROLLERCommsGetNetworkAddr(int *pAddressOut)
{
  if (pAddressOut)
    memset(pAddressOut, 0, 4 * sizeof(*pAddressOut));
}

void ROLLERCommsGetLastPacketAddr(tROLLERNetAddr *pAddressOut)
{
  if (pAddressOut)
    memset(pAddressOut, 0, sizeof(*pAddressOut));
}

void ROLLERCommsGetNodeAddrStr(int iNode, char *szBuf, int iBufLen)
{
  (void)iNode;
  ROLLERCommsClearString(szBuf, iBufLen);
}

void ROLLERCommsFormatAddr(const tROLLERNetAddr *pAddress,
                           char *szBuf, int iBufLen)
{
  (void)pAddress;
  ROLLERCommsClearString(szBuf, iBufLen);
}

int ROLLERCommsEnumLocalAddrs(tROLLERNetIface *pOut, int iMax)
{
  (void)pOut;
  (void)iMax;
  return 0;
}

int ROLLERCommsSendData(const void *pHeader, int iHeaderSize,
                        const void *pData, int iDataSize, int iDestNode)
{
  (void)pHeader;
  (void)iHeaderSize;
  (void)pData;
  (void)iDataSize;
  (void)iDestNode;
  return 0;
}

int ROLLERCommsQueueSend(const void *pHeader, int iHeaderSize,
                         const void *pData, int iDataSize, int iDestNode)
{
  (void)pHeader;
  (void)iHeaderSize;
  (void)pData;
  (void)iDataSize;
  (void)iDestNode;
  return 0;
}

void ROLLERCommsPumpSendQueue(void)
{
}

int ROLLERCommsSendQueueDepth(int iDestNode)
{
  (void)iDestNode;
  return 0;
}

int ROLLERCommsBroadcastData(const void *pHeader, int iHeaderSize,
                             const void *pData, int iDataSize,
                             uint16_t unPort)
{
  (void)pHeader;
  (void)iHeaderSize;
  (void)pData;
  (void)iDataSize;
  (void)unPort;
  return 0;
}

int ROLLERCommsSendDataToAddr(const void *pHeader, int iHeaderSize,
                              const void *pData, int iDataSize,
                              const void *pAddress)
{
  (void)pHeader;
  (void)iHeaderSize;
  (void)pData;
  (void)iDataSize;
  (void)pAddress;
  return 0;
}

int ROLLERCommsGetHeader(void *pHeaderOut, int iHeaderSize, void **ppDataOut)
{
  (void)pHeaderOut;
  (void)iHeaderSize;
  if (ppDataOut)
    *ppDataOut = NULL;
  return 0;
}

int ROLLERCommsGetBlock(void *pDataIn, void *pDataOut, int iSize)
{
  (void)pDataIn;
  (void)pDataOut;
  (void)iSize;
  return 0;
}

int ROLLERCommsPostListen(void)
{
  return 0;
}

void ROLLERCommsSetCommandBase(int iCommandBase)
{
  (void)iCommandBase;
}

void ROLLERCommsSetComPort(int iPort)
{
  (void)iPort;
}

void ROLLERclrrx(void)
{
}

void ROLLERclrtx(void)
{
}
