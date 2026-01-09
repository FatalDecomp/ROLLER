#ifndef _ROLLER_ROLLERCOMMS_H
#define _ROLLER_ROLLERCOMMS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <stdint.h>
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------
#define ROLLER_MAX_NODES 16
#define ROLLER_DEFAULT_PORT 7777
#define ROLLER_MAX_PACKET_SIZE 1024
//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint32 uiIPAddress;      // IPv4 address in network byte order
  uint16 unPort;           // Port number
  uint16 unPadding;
} tROLLERNetAddr;

//-------------------------------------------------------------------------------------------------
// Init/shutdown
int ROLLERCommsInitSystem(unsigned int uiMaxPackets);
void ROLLERCommsUnInitSystem(void);
void ROLLERCommsSetType(int iType); // 0 = IPX emulation, 1 = Serial emulation
int ROLLERCommsGetType(void);

// Node management
int ROLLERCommsGetActiveNodes(void);
int ROLLERCommsGetConsoleNode(void);
int ROLLERCommsAddNode(const void *pAddress);
int ROLLERCommsDeleteNode(int iNodeIdx);
void ROLLERCommsSortNodes(void);
int ROLLERCommsNetAddrToNode(const int *pAddress);

// Address
void ROLLERCommsGetNetworkAddr(int *pAddressOut);

// Data transmission
int ROLLERCommsSendData(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    int iDestNode);

// Data reception
int ROLLERCommsGetHeader(void *pHeaderOut, int iHeaderSize, void **ppDataOut);
void ROLLERCommsGetBlock(void *pDataIn, void *pDataOut, int iSize);
int ROLLERCommsPostListen(void);

// Serial port emulation (for compatibility)
void ROLLERCommsSetCommandBase(int iCommandBase);
void ROLLERCommsSetComPort(int iPort);
void ROLLERCommsSetComPort(int iPort);
void ROLLERCommsSetComBaudRate(int iBaudRate);
int ROLLER16550(int iPort); // Always returns true

// Buffer management
void ROLLERclrrx(void); // Clear receive buffer
void ROLLERclrtx(void); // Clear transmit buffer

// Modem emulation (for compatibility)
int ROLLERModemHangUp(void);
int ROLLERModemInit(const char *szInitString, int iParam1, int iParam2, int iParam3);
int ROLLERModemDial(const char *szPhoneNumber, int iToneMode);
int ROLLERModemAnswer(void);
int ROLLERModemCheckResponse(int iParam1, int iParam2, int iParam3, int iParam4);

//-------------------------------------------------------------------------------------------------
#endif // _ROLLER_ROLLERCOMMS_H