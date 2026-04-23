#include "rollercomms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef IS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tROLLERNetAddr address;
  SOCKET socket;
  bool bActive;
} tNodeInfo;

static struct
{
  bool bInitialized;
  int iNetType;              // 0 = IPX-style, 1 = Serial-style
  int iConsoleNode;
  int iActiveNodes;
  SOCKET listenSocket;
  tNodeInfo nodes[ROLLER_MAX_NODES];
  tROLLERNetAddr myAddress;

  // Receive buffer
  uint8 receiveBuffer[ROLLER_MAX_PACKET_SIZE];
  int iReceiveSize;
  void *pCurrentPacketData;

} g_commsState;

// Pre-init configuration (survives ROLLERCommsUnInitSystem)
static uint16_t s_unLocalPort = ROLLER_DEFAULT_PORT;
static bool s_bHasPeer = false;
static tROLLERNetAddr s_peerAddr;

//-------------------------------------------------------------------------------------------------

static int InitializeSockets(void)
{
#ifdef IS_WINDOWS
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult);
    return 0;
  }
#endif
  return 1;
}

//-------------------------------------------------------------------------------------------------

static void CleanupSockets(void)
{
#ifdef IS_WINDOWS
  WSACleanup();
#endif
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetLocalPort(uint16_t unPort)
{
  s_unLocalPort = unPort;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetPeer(const char *szIP, uint16_t unPort)
{
  struct in_addr addr;
  if (inet_pton(AF_INET, szIP, &addr) != 1) {
    printf("ROLLERCommsSetPeer: invalid IP address '%s'\n", szIP);
    return;
  }
  s_peerAddr.uiIPAddress = addr.s_addr;
  s_peerAddr.unPort = unPort;
  s_peerAddr.unPadding = 0;
  s_peerAddr.ullReserved = 0;
  s_bHasPeer = true;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsInitSystem(unsigned int uiMaxPackets)
{
  if (g_commsState.bInitialized) {
    return 1;
  }

  if (!InitializeSockets()) {
    return 0;
  }

  memset(&g_commsState, 0, sizeof(g_commsState));
  g_commsState.listenSocket = INVALID_SOCKET;

  g_commsState.listenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_commsState.listenSocket == INVALID_SOCKET) {
    CleanupSockets();
    return 0;
  }

#ifdef IS_WINDOWS
  u_long iMode = 1;
  ioctlsocket(g_commsState.listenSocket, FIONBIO, &iMode);
#else
  int iFlags = fcntl(g_commsState.listenSocket, F_GETFL, 0);
  fcntl(g_commsState.listenSocket, F_SETFL, iFlags | O_NONBLOCK);
#endif

  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = INADDR_ANY;
  bindAddr.sin_port = htons(s_unLocalPort);

  if (bind(g_commsState.listenSocket, (struct sockaddr *)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
    printf("ROLLERCommsInitSystem: bind to port %u failed\n", (unsigned)s_unLocalPort);
    closesocket(g_commsState.listenSocket);
    CleanupSockets();
    return 0;
  }

  // Phase 2: replace with getifaddrs() to pick a real LAN IP
  g_commsState.myAddress.uiIPAddress = htonl(INADDR_LOOPBACK);
  g_commsState.myAddress.unPort = s_unLocalPort;
  g_commsState.myAddress.unPadding = 0;

  g_commsState.bInitialized = 1;
  g_commsState.iNetType = 0;
  g_commsState.iConsoleNode = 0;
  g_commsState.iActiveNodes = 0;

  // Add ourselves as node 0 so ROLLERCommsNetAddrToNode(myAddress) returns 0
  ROLLERCommsAddNode(&g_commsState.myAddress);
  g_commsState.iConsoleNode = 0;

  // Pre-configured peer (from --peer CLI arg)
  if (s_bHasPeer) {
    ROLLERCommsAddNode(&s_peerAddr);
  }

  return 1;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsUnInitSystem()
{
  if (!g_commsState.bInitialized) {
    return;
  }

  if (g_commsState.listenSocket != INVALID_SOCKET) {
    closesocket(g_commsState.listenSocket);
    g_commsState.listenSocket = INVALID_SOCKET;
  }

  CleanupSockets();
  memset(&g_commsState, 0, sizeof(g_commsState));
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsSendData(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    int iDestNode)
{
  if (!g_commsState.bInitialized) {
    return 0;
  }

  // Special case: node 21 means broadcast
  if (iDestNode == 21) {
      // Broadcast to all active nodes
    int iSuccess = 1;
    for (int i = 0; i < g_commsState.iActiveNodes; i++) {
      if (i != g_commsState.iConsoleNode && g_commsState.nodes[i].bActive) {
        if (!ROLLERCommsSendData(pHeader, iHeaderSize, pData, iDataSize, i)) {
          iSuccess = 0;
        }
      }
    }
    return iSuccess;
  }

  if (iDestNode < 0 || iDestNode >= ROLLER_MAX_NODES || !g_commsState.nodes[iDestNode].bActive) {
    return 0;
  }

  if (iDestNode == g_commsState.iConsoleNode) {
    return 1;  // Don't send to ourselves
  }

  // Combine header and data into single packet
  uint8_t packetBuffer[ROLLER_MAX_PACKET_SIZE];
  int iTotalSize = iHeaderSize + iDataSize;

  if (iTotalSize > ROLLER_MAX_PACKET_SIZE) {
    return 0;
  }

  memcpy(packetBuffer, pHeader, iHeaderSize);
  if (iDataSize > 0) {
    memcpy(packetBuffer + iHeaderSize, pData, iDataSize);
  }

  // Send via UDP
  struct sockaddr_in destAddr;
  memset(&destAddr, 0, sizeof(destAddr));
  destAddr.sin_family = AF_INET;
  destAddr.sin_addr.s_addr = g_commsState.nodes[iDestNode].address.uiIPAddress;
  destAddr.sin_port = htons(g_commsState.nodes[iDestNode].address.unPort);

  int iBytesSent = sendto(g_commsState.listenSocket, (const char *)packetBuffer, iTotalSize, 0,
                          (struct sockaddr *)&destAddr, sizeof(destAddr));

  return (iBytesSent == iTotalSize) ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsGetHeader(void *pHeaderOut, int iHeaderSize, void **ppDataOut)
{
  if (!g_commsState.bInitialized) {
    return 0;
  }

  struct sockaddr_in fromAddr;
  socklen_t fromLen = sizeof(fromAddr);

  int iBytesReceived = recvfrom(g_commsState.listenSocket, (char *)g_commsState.receiveBuffer,
                                 ROLLER_MAX_PACKET_SIZE, 0, (struct sockaddr *)&fromAddr, &fromLen);

  if (iBytesReceived <= 0) {
    return 0; // No data available
  }

  if (iBytesReceived < iHeaderSize) {
    return 0; // Packet too small
  }

  // Copy header
  memcpy(pHeaderOut, g_commsState.receiveBuffer, iHeaderSize);

  // Set data pointer
  g_commsState.iReceiveSize = iBytesReceived - iHeaderSize;
  g_commsState.pCurrentPacketData = g_commsState.receiveBuffer + iHeaderSize;
  *ppDataOut = g_commsState.pCurrentPacketData;

  return 1;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsGetBlock(void *pDataIn, void *pDataOut, int iSize)
{
  if (pDataIn && pDataOut && iSize > 0) {
    memcpy(pDataOut, pDataIn, iSize);
  }
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsPostListen(void)
{
  // In non-blocking mode, this is a no-op
  // Return 1 if there might be more data, 0 otherwise
  return 0;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsGetActiveNodes(void)
{
  return g_commsState.iActiveNodes;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsGetConsoleNode(void)
{
  return g_commsState.iConsoleNode;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsAddNode(const void *pAddress)
{
  // Idempotent: if this address is already in the table, do nothing
  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    if (memcmp(&g_commsState.nodes[i].address, pAddress, sizeof(tROLLERNetAddr)) == 0) {
      return 1;  // Already present (not an error — > 1 is the failure sentinel)
    }
  }

  if (g_commsState.iActiveNodes >= ROLLER_MAX_NODES) {
    return 2;  // Too many nodes
  }

  int iNodeIdx = g_commsState.iActiveNodes;
  memcpy(&g_commsState.nodes[iNodeIdx].address, pAddress, sizeof(tROLLERNetAddr));
  g_commsState.nodes[iNodeIdx].bActive = true;
  g_commsState.iActiveNodes++;

  return 0;  // Success
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsDeleteNode(int iNodeIdx)
{
  if (iNodeIdx < 0 || iNodeIdx >= g_commsState.iActiveNodes) {
    return 1;  // Invalid node
  }

  g_commsState.nodes[iNodeIdx].bActive = false;
  return 0;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSortNodes(void)
{
    // Compact the node list by removing inactive nodes
  int iWriteIdx = 0;
  for (int iReadIdx = 0; iReadIdx < g_commsState.iActiveNodes; iReadIdx++) {
    if (g_commsState.nodes[iReadIdx].bActive) {
      if (iWriteIdx != iReadIdx) {
        g_commsState.nodes[iWriteIdx] = g_commsState.nodes[iReadIdx];
      }
      iWriteIdx++;
    }
  }
  g_commsState.iActiveNodes = iWriteIdx;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetType(int iType)
{
  g_commsState.iNetType = iType;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsGetType(void)
{
  return g_commsState.iNetType;
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetCommandBase(int iCommandBase)
{
// No-op for modern networking
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetComPort(int iPort)
{
// No-op for modern networking
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsSetComBaudRate(int iBaudRate)
{
// No-op for modern networking
}

//-------------------------------------------------------------------------------------------------

int ROLLER16550(int iPort)
{
  return 1;  // Always return success
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsGetNetworkAddr(int *pAddressOut)
{
  if (pAddressOut) {
    memcpy(pAddressOut, &g_commsState.myAddress, sizeof(tROLLERNetAddr));
  }
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsNetAddrToNode(const int *pAddress)
{
  // Find node with matching address
  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    if (memcmp(&g_commsState.nodes[i].address, pAddress, sizeof(tROLLERNetAddr)) == 0) {
      return i;
    }
  }
  return -1;
}

//-------------------------------------------------------------------------------------------------

void ROLLERclrrx(void)
{
// Clear receive buffer - could implement if needed
}

//-------------------------------------------------------------------------------------------------

void ROLLERclrtx(void)
{
// Clear transmit buffer - could implement if needed
}

//-------------------------------------------------------------------------------------------------

// Modem stubs (always succeed immediately for local network)
int ROLLERModemHangUp(void) { return 1; }
int ROLLERModemInit(const char *szInitString, int p1, int p2, int p3) { return 1; }
int ROLLERModemDial(const char *szPhoneNumber, int iToneMode) { return 1; }
int ROLLERModemAnswer(void) { return 1; }
int ROLLERModemCheckResponse(int p1, int p2, int p3, int p4) { return 0; /* Ready */ }

//-------------------------------------------------------------------------------------------------