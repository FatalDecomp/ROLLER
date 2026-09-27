#ifndef _ROLLER_ROLLERCOMMS_H
#define _ROLLER_ROLLERCOMMS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <stdint.h>
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

#ifdef ROLLERCOMMS_LEGACY_IMPLEMENTATION
#define ROLLERCommsSetLocalPort NetLegacyImpl_ROLLERCommsSetLocalPort
#define ROLLERCommsSetPeer NetLegacyImpl_ROLLERCommsSetPeer
#define ROLLERCommsSetLocalIP NetLegacyImpl_ROLLERCommsSetLocalIP
#define ROLLERCommsInitSystem NetLegacyImpl_ROLLERCommsInitSystem
#define ROLLERCommsUnInitSystem NetLegacyImpl_ROLLERCommsUnInitSystem
#define ROLLERCommsSetType NetLegacyImpl_ROLLERCommsSetType
#define ROLLERCommsGetType NetLegacyImpl_ROLLERCommsGetType
#define ROLLERCommsUpdateLocalAddrForPeer \
  NetLegacyImpl_ROLLERCommsUpdateLocalAddrForPeer
#define ROLLERCommsGetActiveNodes NetLegacyImpl_ROLLERCommsGetActiveNodes
#define ROLLERCommsGetConsoleNode NetLegacyImpl_ROLLERCommsGetConsoleNode
#define ROLLERCommsAddNode NetLegacyImpl_ROLLERCommsAddNode
#define ROLLERCommsUpdateNodeTransportAddr \
  NetLegacyImpl_ROLLERCommsUpdateNodeTransportAddr
#define ROLLERCommsDeleteNode NetLegacyImpl_ROLLERCommsDeleteNode
#define ROLLERCommsSortNodes NetLegacyImpl_ROLLERCommsSortNodes
#define ROLLERCommsNetAddrToNode NetLegacyImpl_ROLLERCommsNetAddrToNode
#define ROLLERCommsGetNetworkAddr NetLegacyImpl_ROLLERCommsGetNetworkAddr
#define ROLLERCommsGetLastPacketAddr NetLegacyImpl_ROLLERCommsGetLastPacketAddr
#define ROLLERCommsGetNodeAddrStr NetLegacyImpl_ROLLERCommsGetNodeAddrStr
#define ROLLERCommsFormatAddr NetLegacyImpl_ROLLERCommsFormatAddr
#define ROLLERCommsEnumLocalAddrs NetLegacyImpl_ROLLERCommsEnumLocalAddrs
#define ROLLERCommsSendData NetLegacyImpl_ROLLERCommsSendData
#define ROLLERCommsQueueSend NetLegacyImpl_ROLLERCommsQueueSend
#define ROLLERCommsPumpSendQueue NetLegacyImpl_ROLLERCommsPumpSendQueue
#define ROLLERCommsSendQueueDepth NetLegacyImpl_ROLLERCommsSendQueueDepth
#define ROLLERCommsBroadcastData NetLegacyImpl_ROLLERCommsBroadcastData
#define ROLLERCommsSendDataToAddr NetLegacyImpl_ROLLERCommsSendDataToAddr
#define ROLLERCommsGetHeader NetLegacyImpl_ROLLERCommsGetHeader
#define ROLLERCommsGetBlock NetLegacyImpl_ROLLERCommsGetBlock
#define ROLLERCommsPostListen NetLegacyImpl_ROLLERCommsPostListen
#define ROLLERCommsSetCommandBase NetLegacyImpl_ROLLERCommsSetCommandBase
#define ROLLERCommsSetComPort NetLegacyImpl_ROLLERCommsSetComPort
#define ROLLERclrrx NetLegacyImpl_ROLLERclrrx
#define ROLLERclrtx NetLegacyImpl_ROLLERclrtx
#endif
#define ROLLER_MAX_NODES 16
#define ROLLER_DEFAULT_PORT 7777
#define ROLLER_MAX_PACKET_SIZE 2048
#define ROLLER_MAX_IFACES 8
#define SEND_QUEUE_DEPTH 64

typedef struct
{
  char szIP[16];   // dotted-decimal IPv4
  char szName[48]; // interface / adapter friendly name
} tROLLERNetIface;
//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint32 uiIPAddress;      // IPv4 address in network byte order
  uint16 unPort;           // Port number
  uint16 unPadding;
  uint64 ullReserved;    // Must be zero - pads to 16 bytes to match _NETNOW_NODE_ADDR
} tROLLERNetAddr;

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint16 unHeaderSize;
  uint16 unDataSize;
  uint8 abPacket[ROLLER_MAX_PACKET_SIZE];
} tSendQueueEntry;

//-------------------------------------------------------------------------------------------------
// Pre-init configuration (call before InitSystem)
void ROLLERCommsSetLocalPort(uint16_t unPort);
void ROLLERCommsSetPeer(const char *szIP, uint16_t unPort);
void ROLLERCommsSetLocalIP(const char *szIP); // NULL or "" = auto-detect

// Init/shutdown
int ROLLERCommsInitSystem(unsigned int uiMaxPackets);
void ROLLERCommsUnInitSystem(void);
void ROLLERCommsSetType(int iType); // 0 = IPX emulation, 1 = Serial emulation
int ROLLERCommsGetType(void);
void ROLLERCommsUpdateLocalAddrForPeer(const void *pPeerAddress);

// Node management
int ROLLERCommsGetActiveNodes(void);
int ROLLERCommsGetConsoleNode(void);
int ROLLERCommsAddNode(const void *pAddress);
void ROLLERCommsUpdateNodeTransportAddr(const void *pAddress, const void *pTransportAddress);
int ROLLERCommsDeleteNode(int iNodeIdx);
void ROLLERCommsSortNodes(void);
int ROLLERCommsNetAddrToNode(const int *pAddress);

// Address
void ROLLERCommsGetNetworkAddr(int *pAddressOut);
void ROLLERCommsGetLastPacketAddr(tROLLERNetAddr *pAddressOut);
void ROLLERCommsGetNodeAddrStr(int iNode, char *szBuf, int iBufLen);
void ROLLERCommsFormatAddr(const tROLLERNetAddr *pAddress, char *szBuf, int iBufLen);
int  ROLLERCommsEnumLocalAddrs(tROLLERNetIface *pOut, int iMax);

// Data transmission
int ROLLERCommsSendData(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    int iDestNode);
int ROLLERCommsQueueSend(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    int iDestNode);
void ROLLERCommsPumpSendQueue(void);
int ROLLERCommsSendQueueDepth(int iDestNode);
int ROLLERCommsBroadcastData(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    uint16_t unPort);
int ROLLERCommsSendDataToAddr(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    const void *pAddress);

// Data reception
int ROLLERCommsGetHeader(void *pHeaderOut, int iHeaderSize, void **ppDataOut);
int ROLLERCommsGetBlock(void *pDataIn, void *pDataOut, int iSize);
int ROLLERCommsPostListen(void);

// Serial port emulation (for compatibility)
void ROLLERCommsSetCommandBase(int iCommandBase);
void ROLLERCommsSetComPort(int iPort);
void ROLLERCommsSetComPort(int iPort);

// Buffer management
void ROLLERclrrx(void); // Clear receive buffer
void ROLLERclrtx(void); // Clear transmit buffer

// Modem emulation (for compatibility)

//-------------------------------------------------------------------------------------------------
#endif // _ROLLER_ROLLERCOMMS_H
