#include "net_legacy.h"
#include "net_types.h"
#include "network.h"
#include "rollercomms.h"

#include <SDL3/SDL_atomic.h>

#include <assert.h>
#include <stdio.h>

static SDL_AtomicInt s_iLegacyEntryCount;
static SDL_AtomicInt s_iLegacyViolationCount;

static void NetLegacyCallTrap(const char *szEntryPoint)
{
  SDL_AddAtomicInt(&s_iLegacyEntryCount, 1);
  if (net_mode != NET_MODE_MODERN)
    return;

  SDL_AddAtomicInt(&s_iLegacyViolationCount, 1);
#ifndef NDEBUG
  fprintf(stderr, "legacy network entry point called in MODERN mode: %s\n",
          szEntryPoint);
  assert(net_mode != NET_MODE_MODERN);
#else
  (void)szEntryPoint;
#endif
}

void NetLegacyTrapReset(void)
{
  SDL_SetAtomicInt(&s_iLegacyEntryCount, 0);
  SDL_SetAtomicInt(&s_iLegacyViolationCount, 0);
}

int NetLegacyTrapEntryCount(void)
{
  return SDL_GetAtomicInt(&s_iLegacyEntryCount);
}

int NetLegacyTrapViolationCount(void)
{
  return SDL_GetAtomicInt(&s_iLegacyViolationCount);
}

/* network.c implementations, renamed by NET_LEGACY_NETWORK_IMPLEMENTATION. */
void NetLegacyImpl_network_initialise_begin(int iSelectNetSlot);
int NetLegacyImpl_network_initialise_update(void);
int NetLegacyImpl_network_initialise_active(void);
void NetLegacyImpl_close_network(void);
void NetLegacyImpl_send_net_error(void);
void NetLegacyImpl_send_network_sync_error(void);
void NetLegacyImpl_send_resync(int iFrameNumber);
void NetLegacyImpl_send_quit(void);
void NetLegacyImpl_send_ready(void);
void NetLegacyImpl_send_record_to_master(int iRecordIdx);
void NetLegacyImpl_send_record_to_slaves(int iRecordIdx);
void NetLegacyImpl_send_mes(int iNetworkMessageIdx, int iNode);
void NetLegacyImpl_send_seed(int iRandomSeed);
void NetLegacyImpl_send_single(uint32 uiData);
void NetLegacyImpl_send_pause(void);
void NetLegacyImpl_send_slot(void);
void NetLegacyImpl_transmitpausetoslaves(void);
void NetLegacyImpl_send_multiple(void);
int NetLegacyImpl_receive_multiple(void);
void NetLegacyImpl_receive_all_singles(void);
void NetLegacyImpl_do_sync_stuff(void);
int NetLegacyImpl_TransmitInit(void);
void NetLegacyImpl_CheckNewNodes(void);
void NetLegacyImpl_FoundNodes(void);
void NetLegacyImpl_SendPlayerInfo(void);
void NetLegacyImpl_SendAMessage(void);
void NetLegacyImpl_BroadcastNews(void);
void NetLegacyImpl_network_broadcast_wait_start(int iBroadcastMode,
                                                int iRepeatCount);
int NetLegacyImpl_network_broadcast_wait_update(void);
int NetLegacyImpl_network_broadcast_wait_active(void);
void NetLegacyImpl_remove_messages(int iClear);
void NetLegacyImpl_reset_network(int iResetBroadcastMode);
void NetLegacyImpl_clear_network_game(void);
void NetLegacyImpl_reset_net_wait(void);
unsigned int NetLegacyImpl_send_broadcast(unsigned int uiBroadcastMode);

/* rollercomms.c implementations, renamed by
   ROLLERCOMMS_LEGACY_IMPLEMENTATION. */
void NetLegacyImpl_ROLLERCommsSetLocalPort(uint16_t unPort);
void NetLegacyImpl_ROLLERCommsSetPeer(const char *szIP, uint16_t unPort);
void NetLegacyImpl_ROLLERCommsSetLocalIP(const char *szIP);
int NetLegacyImpl_ROLLERCommsInitSystem(unsigned int uiMaxPackets);
void NetLegacyImpl_ROLLERCommsUnInitSystem(void);
void NetLegacyImpl_ROLLERCommsSetType(int iType);
int NetLegacyImpl_ROLLERCommsGetType(void);
void NetLegacyImpl_ROLLERCommsUpdateLocalAddrForPeer(
    const void *pPeerAddress);
int NetLegacyImpl_ROLLERCommsGetActiveNodes(void);
int NetLegacyImpl_ROLLERCommsGetConsoleNode(void);
int NetLegacyImpl_ROLLERCommsAddNode(const void *pAddress);
void NetLegacyImpl_ROLLERCommsUpdateNodeTransportAddr(
    const void *pAddress, const void *pTransportAddress);
int NetLegacyImpl_ROLLERCommsDeleteNode(int iNodeIdx);
void NetLegacyImpl_ROLLERCommsSortNodes(void);
int NetLegacyImpl_ROLLERCommsNetAddrToNode(const int *pAddress);
void NetLegacyImpl_ROLLERCommsGetNetworkAddr(int *pAddressOut);
void NetLegacyImpl_ROLLERCommsGetLastPacketAddr(
    tROLLERNetAddr *pAddressOut);
void NetLegacyImpl_ROLLERCommsGetNodeAddrStr(int iNode, char *szBuf,
                                            int iBufLen);
void NetLegacyImpl_ROLLERCommsFormatAddr(const tROLLERNetAddr *pAddress,
                                         char *szBuf, int iBufLen);
int NetLegacyImpl_ROLLERCommsEnumLocalAddrs(tROLLERNetIface *pOut, int iMax);
int NetLegacyImpl_ROLLERCommsSendData(const void *pHeader, int iHeaderSize,
                                      const void *pData, int iDataSize,
                                      int iDestNode);
int NetLegacyImpl_ROLLERCommsQueueSend(const void *pHeader, int iHeaderSize,
                                       const void *pData, int iDataSize,
                                       int iDestNode);
void NetLegacyImpl_ROLLERCommsPumpSendQueue(void);
int NetLegacyImpl_ROLLERCommsSendQueueDepth(int iDestNode);
int NetLegacyImpl_ROLLERCommsBroadcastData(
    const void *pHeader, int iHeaderSize, const void *pData, int iDataSize,
    uint16_t unPort);
int NetLegacyImpl_ROLLERCommsSendDataToAddr(
    const void *pHeader, int iHeaderSize, const void *pData, int iDataSize,
    const void *pAddress);
int NetLegacyImpl_ROLLERCommsGetHeader(void *pHeaderOut, int iHeaderSize,
                                       void **ppDataOut);
int NetLegacyImpl_ROLLERCommsGetBlock(void *pDataIn, void *pDataOut,
                                      int iSize);
int NetLegacyImpl_ROLLERCommsPostListen(void);
void NetLegacyImpl_ROLLERCommsSetCommandBase(int iCommandBase);
void NetLegacyImpl_ROLLERCommsSetComPort(int iPort);
void NetLegacyImpl_ROLLERclrrx(void);
void NetLegacyImpl_ROLLERclrtx(void);

#define NET_WRAP_VOID0(name) \
  void name(void) { NetLegacyCallTrap(#name); NetLegacyImpl_##name(); }
#define NET_WRAP_VOID1(name, type1, arg1) \
  void name(type1 arg1) { NetLegacyCallTrap(#name); NetLegacyImpl_##name(arg1); }
#define NET_WRAP_VOID2(name, type1, arg1, type2, arg2) \
  void name(type1 arg1, type2 arg2) { \
    NetLegacyCallTrap(#name); NetLegacyImpl_##name(arg1, arg2); \
  }
#define NET_WRAP_VOID3(name, type1, arg1, type2, arg2, type3, arg3) \
  void name(type1 arg1, type2 arg2, type3 arg3) { \
    NetLegacyCallTrap(#name); NetLegacyImpl_##name(arg1, arg2, arg3); \
  }
#define NET_WRAP_INT0(name) \
  int name(void) { NetLegacyCallTrap(#name); return NetLegacyImpl_##name(); }
#define NET_WRAP_INT1(name, type1, arg1) \
  int name(type1 arg1) { \
    NetLegacyCallTrap(#name); return NetLegacyImpl_##name(arg1); \
  }
#define NET_WRAP_INT2(name, type1, arg1, type2, arg2) \
  int name(type1 arg1, type2 arg2) { \
    NetLegacyCallTrap(#name); return NetLegacyImpl_##name(arg1, arg2); \
  }
#define NET_WRAP_INT3(name, type1, arg1, type2, arg2, type3, arg3) \
  int name(type1 arg1, type2 arg2, type3 arg3) { \
    NetLegacyCallTrap(#name); return NetLegacyImpl_##name(arg1, arg2, arg3); \
  }
#define NET_WRAP_INT5(name, type1, arg1, type2, arg2, type3, arg3, type4, arg4, type5, arg5) \
  int name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5) { \
    NetLegacyCallTrap(#name); \
    return NetLegacyImpl_##name(arg1, arg2, arg3, arg4, arg5); \
  }

NET_WRAP_VOID1(network_initialise_begin, int, iSelectNetSlot)
NET_WRAP_INT0(network_initialise_update)
NET_WRAP_INT0(network_initialise_active)
NET_WRAP_VOID0(close_network)
NET_WRAP_VOID0(send_net_error)
NET_WRAP_VOID0(send_network_sync_error)
NET_WRAP_VOID1(send_resync, int, iFrameNumber)
NET_WRAP_VOID0(send_quit)
NET_WRAP_VOID0(send_ready)
NET_WRAP_VOID1(send_record_to_master, int, iRecordIdx)
NET_WRAP_VOID1(send_record_to_slaves, int, iRecordIdx)
NET_WRAP_VOID2(send_mes, int, iNetworkMessageIdx, int, iNode)
NET_WRAP_VOID1(send_seed, int, iRandomSeed)
NET_WRAP_VOID1(send_single, uint32, uiData)
NET_WRAP_VOID0(send_pause)
NET_WRAP_VOID0(send_slot)
NET_WRAP_VOID0(transmitpausetoslaves)
NET_WRAP_VOID0(send_multiple)
NET_WRAP_INT0(receive_multiple)
NET_WRAP_VOID0(receive_all_singles)
NET_WRAP_VOID0(do_sync_stuff)
NET_WRAP_INT0(TransmitInit)
NET_WRAP_VOID0(CheckNewNodes)
NET_WRAP_VOID0(FoundNodes)
NET_WRAP_VOID0(SendPlayerInfo)
NET_WRAP_VOID0(SendAMessage)
NET_WRAP_VOID0(BroadcastNews)
NET_WRAP_VOID2(network_broadcast_wait_start, int, iBroadcastMode, int,
               iRepeatCount)
NET_WRAP_INT0(network_broadcast_wait_update)
NET_WRAP_INT0(network_broadcast_wait_active)
NET_WRAP_VOID1(remove_messages, int, iClear)
NET_WRAP_VOID1(reset_network, int, iResetBroadcastMode)
NET_WRAP_VOID0(clear_network_game)
NET_WRAP_VOID0(reset_net_wait)

unsigned int send_broadcast(unsigned int uiBroadcastMode)
{
  NetLegacyCallTrap("send_broadcast");
  return NetLegacyImpl_send_broadcast(uiBroadcastMode);
}

NET_WRAP_VOID1(ROLLERCommsSetLocalPort, uint16_t, unPort)
NET_WRAP_VOID2(ROLLERCommsSetPeer, const char *, szIP, uint16_t, unPort)
NET_WRAP_VOID1(ROLLERCommsSetLocalIP, const char *, szIP)
NET_WRAP_INT1(ROLLERCommsInitSystem, unsigned int, uiMaxPackets)
NET_WRAP_VOID0(ROLLERCommsUnInitSystem)
NET_WRAP_VOID1(ROLLERCommsSetType, int, iType)
NET_WRAP_INT0(ROLLERCommsGetType)
NET_WRAP_VOID1(ROLLERCommsUpdateLocalAddrForPeer, const void *, pPeerAddress)
NET_WRAP_INT0(ROLLERCommsGetActiveNodes)
NET_WRAP_INT0(ROLLERCommsGetConsoleNode)
NET_WRAP_INT1(ROLLERCommsAddNode, const void *, pAddress)
NET_WRAP_VOID2(ROLLERCommsUpdateNodeTransportAddr, const void *, pAddress,
               const void *, pTransportAddress)
NET_WRAP_INT1(ROLLERCommsDeleteNode, int, iNodeIdx)
NET_WRAP_VOID0(ROLLERCommsSortNodes)
NET_WRAP_INT1(ROLLERCommsNetAddrToNode, const int *, pAddress)
NET_WRAP_VOID1(ROLLERCommsGetNetworkAddr, int *, pAddressOut)
NET_WRAP_VOID1(ROLLERCommsGetLastPacketAddr, tROLLERNetAddr *, pAddressOut)
NET_WRAP_VOID3(ROLLERCommsGetNodeAddrStr, int, iNode, char *, szBuf, int,
               iBufLen)
NET_WRAP_VOID3(ROLLERCommsFormatAddr, const tROLLERNetAddr *, pAddress, char *,
               szBuf, int, iBufLen)
NET_WRAP_INT2(ROLLERCommsEnumLocalAddrs, tROLLERNetIface *, pOut, int, iMax)
NET_WRAP_INT5(ROLLERCommsSendData, const void *, pHeader, int, iHeaderSize,
              const void *, pData, int, iDataSize, int, iDestNode)
NET_WRAP_INT5(ROLLERCommsQueueSend, const void *, pHeader, int, iHeaderSize,
              const void *, pData, int, iDataSize, int, iDestNode)
NET_WRAP_VOID0(ROLLERCommsPumpSendQueue)
NET_WRAP_INT1(ROLLERCommsSendQueueDepth, int, iDestNode)
NET_WRAP_INT5(ROLLERCommsBroadcastData, const void *, pHeader, int,
              iHeaderSize, const void *, pData, int, iDataSize, uint16_t,
              unPort)
NET_WRAP_INT5(ROLLERCommsSendDataToAddr, const void *, pHeader, int,
              iHeaderSize, const void *, pData, int, iDataSize, const void *,
              pAddress)
NET_WRAP_INT3(ROLLERCommsGetHeader, void *, pHeaderOut, int, iHeaderSize,
              void **, ppDataOut)
NET_WRAP_INT3(ROLLERCommsGetBlock, void *, pDataIn, void *, pDataOut, int,
              iSize)
NET_WRAP_INT0(ROLLERCommsPostListen)
NET_WRAP_VOID1(ROLLERCommsSetCommandBase, int, iCommandBase)
NET_WRAP_VOID1(ROLLERCommsSetComPort, int, iPort)
NET_WRAP_VOID0(ROLLERclrrx)
NET_WRAP_VOID0(ROLLERclrtx)
