#include "rollercomms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifdef IS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tROLLERNetAddr address;
  tROLLERNetAddr transportAddress;
  SOCKET socket;
  bool bActive;
} tNodeInfo;

typedef struct
{
  tROLLERNetAddr address;
  tROLLERNetAddr transportAddress;
} tPendingTransport;

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
  tROLLERNetAddr lastPacketAddress;

} g_commsState;

// Pre-init configuration (survives ROLLERCommsUnInitSystem)
static uint16_t s_unLocalPort = ROLLER_DEFAULT_PORT;
static bool s_bHasPeer = false;
static tROLLERNetAddr s_peerAddr;
static uint32_t s_uiLocalIPOverride = 0; // 0 = auto-detect
static tPendingTransport s_pendingTransports[ROLLER_MAX_NODES];
static int s_iPendingTransports = 0;

//-------------------------------------------------------------------------------------------------

int ROLLERCommsEnumLocalAddrs(tROLLERNetIface *pOut, int iMax)
{
  if (!pOut || iMax <= 0) return 0;
  int iCount = 0;
#ifdef IS_WINDOWS
  ULONG uiBufLen = 15000;
  IP_ADAPTER_ADDRESSES *pBuf = (IP_ADAPTER_ADDRESSES *)malloc(uiBufLen);
  if (!pBuf) return 0;
  if (GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL, pBuf, &uiBufLen) == ERROR_BUFFER_OVERFLOW) {
    free(pBuf);
    pBuf = (IP_ADAPTER_ADDRESSES *)malloc(uiBufLen);
    if (!pBuf) return 0;
    GetAdaptersAddresses(AF_INET,
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
      NULL, pBuf, &uiBufLen);
  }
  for (IP_ADAPTER_ADDRESSES *pAA = pBuf; pAA && iCount < iMax; pAA = pAA->Next) {
    if (pAA->OperStatus != IfOperStatusUp) continue;
    if (pAA->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (IP_ADAPTER_UNICAST_ADDRESS *pUA = pAA->FirstUnicastAddress; pUA; pUA = pUA->Next) {
      if (pUA->Address.lpSockaddr->sa_family != AF_INET) continue;
      struct sockaddr_in *pSin = (struct sockaddr_in *)pUA->Address.lpSockaddr;
      inet_ntop(AF_INET, &pSin->sin_addr, pOut[iCount].szIP, sizeof(pOut[iCount].szIP));
      WideCharToMultiByte(CP_UTF8, 0, pAA->FriendlyName, -1,
                          pOut[iCount].szName, sizeof(pOut[iCount].szName), NULL, NULL);
      iCount++;
      break; // one entry per adapter
    }
  }
  free(pBuf);
#else
  struct ifaddrs *pList = NULL;
  if (getifaddrs(&pList) == 0) {
    for (struct ifaddrs *pIfa = pList; pIfa && iCount < iMax; pIfa = pIfa->ifa_next) {
      if (!pIfa->ifa_addr || pIfa->ifa_addr->sa_family != AF_INET) continue;
      struct sockaddr_in *pSin = (struct sockaddr_in *)pIfa->ifa_addr;
      if (pSin->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;
      inet_ntop(AF_INET, &pSin->sin_addr, pOut[iCount].szIP, sizeof(pOut[iCount].szIP));
      strncpy(pOut[iCount].szName, pIfa->ifa_name, sizeof(pOut[iCount].szName) - 1);
      pOut[iCount].szName[sizeof(pOut[iCount].szName) - 1] = '\0';
      iCount++;
    }
    freeifaddrs(pList);
  }
#endif
  return iCount;
}

static uint32_t DetectLocalIPv4(void)
{
  tROLLERNetIface iface;
  if (ROLLERCommsEnumLocalAddrs(&iface, 1) < 1)
    return htonl(INADDR_LOOPBACK);
  struct in_addr addr;
  inet_pton(AF_INET, iface.szIP, &addr);
  return addr.s_addr;
}

static uint32_t DetectRouteLocalIPv4(uint32_t uiRemoteIP, uint16_t unRemotePort)
{
  if (uiRemoteIP == 0)
    return 0;

  SOCKET routeSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (routeSocket == INVALID_SOCKET)
    return 0;

  struct sockaddr_in remoteAddr;
  memset(&remoteAddr, 0, sizeof(remoteAddr));
  remoteAddr.sin_family = AF_INET;
  remoteAddr.sin_addr.s_addr = uiRemoteIP;
  remoteAddr.sin_port = htons(unRemotePort ? unRemotePort : ROLLER_DEFAULT_PORT);

  uint32_t uiLocalIP = 0;
  if (connect(routeSocket, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr)) != SOCKET_ERROR) {
    struct sockaddr_in localAddr;
    socklen_t localLen = sizeof(localAddr);
    memset(&localAddr, 0, sizeof(localAddr));
    if (getsockname(routeSocket, (struct sockaddr *)&localAddr, &localLen) != SOCKET_ERROR) {
      uiLocalIP = localAddr.sin_addr.s_addr;
    }
  }

  closesocket(routeSocket);
  return uiLocalIP;
}

static uint32_t GetConfiguredLocalIPv4(void)
{
  if (s_uiLocalIPOverride)
    return s_uiLocalIPOverride;
  if (s_bHasPeer && s_peerAddr.uiIPAddress == htonl(INADDR_LOOPBACK))
    return htonl(INADDR_LOOPBACK);
  if (s_bHasPeer) {
    uint32_t uiRouteIP = DetectRouteLocalIPv4(s_peerAddr.uiIPAddress, s_peerAddr.unPort);
    if (uiRouteIP)
      return uiRouteIP;
  }
  return DetectLocalIPv4();
}

static void ApplyLocalAddressIPv4(uint32_t uiLocalIP, const char *szReason,
                                  const tROLLERNetAddr *pPeerAddress)
{
  if (!g_commsState.bInitialized || uiLocalIP == 0)
    return;

  tROLLERNetAddr newAddress;
  memset(&newAddress, 0, sizeof(newAddress));
  newAddress.uiIPAddress = uiLocalIP;
  newAddress.unPort = s_unLocalPort;

  if (memcmp(&g_commsState.myAddress, &newAddress, sizeof(newAddress)) == 0)
    return;

  tROLLERNetAddr oldAddress = g_commsState.myAddress;
  g_commsState.myAddress = newAddress;
  if (g_commsState.iActiveNodes == 0) {
    g_commsState.iActiveNodes = 1;
  }

  int iSelfNode = g_commsState.iConsoleNode;
  if (iSelfNode < 0 || iSelfNode >= g_commsState.iActiveNodes ||
      memcmp(&g_commsState.nodes[iSelfNode].address, &oldAddress, sizeof(oldAddress)) != 0) {
    iSelfNode = -1;
    for (int i = 0; i < g_commsState.iActiveNodes; i++) {
      if (memcmp(&g_commsState.nodes[i].address, &oldAddress, sizeof(oldAddress)) == 0) {
        iSelfNode = i;
        break;
      }
    }
  }

  if (iSelfNode < 0)
    iSelfNode = 0;

  g_commsState.nodes[iSelfNode].address = newAddress;
  g_commsState.nodes[iSelfNode].transportAddress = newAddress;
  g_commsState.nodes[iSelfNode].bActive = true;
  g_commsState.iConsoleNode = iSelfNode;

  char szAddr[32];
  ROLLERCommsFormatAddr(&newAddress, szAddr, sizeof(szAddr));
  if (pPeerAddress) {
    char szPeer[32];
    ROLLERCommsFormatAddr(pPeerAddress, szPeer, sizeof(szPeer));
    SDL_Log("[NET-DISCOVERY] selected local address %s for %s via %s",
            szAddr, szPeer, szReason ? szReason : "route");
  } else {
    SDL_Log("[NET-DISCOVERY] selected local address %s", szAddr);
  }

  ROLLERCommsSortNodes();
}

static void ApplyLocalIPOverrideIfIdle(void)
{
  if (!g_commsState.bInitialized || g_commsState.iActiveNodes > 1)
    return;

  ApplyLocalAddressIPv4(GetConfiguredLocalIPv4(), "configuration", NULL);
}

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

static int BuildPacket(uint8_t *pPacketBuffer,
                       int *pTotalSizeOut,
                       const void *pHeader,
                       int iHeaderSize,
                       const void *pData,
                       int iDataSize)
{
  int iTotalSize = iHeaderSize + iDataSize;

  if (!pPacketBuffer || !pTotalSizeOut || !pHeader || iHeaderSize < 0 || iDataSize < 0)
    return 0;
  if (iTotalSize > ROLLER_MAX_PACKET_SIZE)
    return 0;

  memcpy(pPacketBuffer, pHeader, iHeaderSize);
  if (iDataSize > 0 && pData)
    memcpy(pPacketBuffer + iHeaderSize, pData, iDataSize);

  *pTotalSizeOut = iTotalSize;
  return 1;
}

//-------------------------------------------------------------------------------------------------

static int SendPacketToIPv4(uint32_t uiIPAddress, uint16_t unPort, const uint8_t *pPacket, int iPacketSize)
{
  struct sockaddr_in destAddr;
  memset(&destAddr, 0, sizeof(destAddr));
  destAddr.sin_family = AF_INET;
  destAddr.sin_addr.s_addr = uiIPAddress;
  destAddr.sin_port = htons(unPort);

  int iBytesSent = sendto(g_commsState.listenSocket, (const char *)pPacket, iPacketSize, 0,
                          (struct sockaddr *)&destAddr, sizeof(destAddr));

  return (iBytesSent == iPacketSize) ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------

static int IsLoopbackIPv4(uint32_t uiIPAddress)
{
  return (ntohl(uiIPAddress) & 0xFF000000u) == 0x7F000000u;
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

void ROLLERCommsSetLocalIP(const char *szIP)
{
  if (!szIP || szIP[0] == '\0') {
    s_uiLocalIPOverride = 0;
    ApplyLocalIPOverrideIfIdle();
    return;
  }
  struct in_addr addr;
  if (inet_pton(AF_INET, szIP, &addr) == 1) {
    s_uiLocalIPOverride = addr.s_addr;
    ApplyLocalIPOverrideIfIdle();
  } else {
    printf("ROLLERCommsSetLocalIP: invalid IP address '%s'\n", szIP);
  }
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
  if (g_commsState.bInitialized && !s_uiLocalIPOverride) {
    uint32_t uiRouteIP = DetectRouteLocalIPv4(s_peerAddr.uiIPAddress, s_peerAddr.unPort);
    ApplyLocalAddressIPv4(uiRouteIP, "peer", &s_peerAddr);
  }
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsUpdateLocalAddrForPeer(const void *pPeerAddress)
{
  if (!g_commsState.bInitialized || s_uiLocalIPOverride || !pPeerAddress)
    return;

  tROLLERNetAddr peerAddress;
  memset(&peerAddress, 0, sizeof(peerAddress));
  memcpy(&peerAddress, pPeerAddress, sizeof(peerAddress));
  peerAddress.unPadding = 0;
  peerAddress.ullReserved = 0;

  if (peerAddress.uiIPAddress == 0 || peerAddress.unPort == 0)
    return;

  uint32_t uiRouteIP = DetectRouteLocalIPv4(peerAddress.uiIPAddress, peerAddress.unPort);
  ApplyLocalAddressIPv4(uiRouteIP, "route", &peerAddress);
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
  s_iPendingTransports = 0;
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

  int iBroadcast = 1;
  if (setsockopt(g_commsState.listenSocket, SOL_SOCKET, SO_BROADCAST,
                 (const char *)&iBroadcast, sizeof(iBroadcast)) == SOCKET_ERROR) {
    SDL_Log("[NET-DISCOVERY] SO_BROADCAST setup failed; manual peers may still work");
  }

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

  g_commsState.myAddress.uiIPAddress = GetConfiguredLocalIPv4();
  g_commsState.myAddress.unPort = s_unLocalPort;
  g_commsState.myAddress.unPadding = 0;
  g_commsState.myAddress.ullReserved = 0;

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

static void RememberPendingTransport(const tROLLERNetAddr *pAddress,
                                     const tROLLERNetAddr *pTransportAddress)
{
  if (!pAddress || !pTransportAddress)
    return;
  if (memcmp(pAddress, pTransportAddress, sizeof(tROLLERNetAddr)) == 0)
    return;

  for (int i = 0; i < s_iPendingTransports; i++) {
    if (memcmp(&s_pendingTransports[i].address, pAddress, sizeof(tROLLERNetAddr)) == 0) {
      s_pendingTransports[i].transportAddress = *pTransportAddress;
      return;
    }
  }

  if (s_iPendingTransports >= ROLLER_MAX_NODES)
    return;

  s_pendingTransports[s_iPendingTransports].address = *pAddress;
  s_pendingTransports[s_iPendingTransports].transportAddress = *pTransportAddress;
  s_iPendingTransports++;
}

static void ApplyPendingTransport(tNodeInfo *pNode)
{
  if (!pNode)
    return;

  for (int i = 0; i < s_iPendingTransports; i++) {
    if (memcmp(&s_pendingTransports[i].address, &pNode->address, sizeof(tROLLERNetAddr)) == 0) {
      pNode->transportAddress = s_pendingTransports[i].transportAddress;
      char szAddress[32];
      char szTransport[32];
      ROLLERCommsFormatAddr(&pNode->address, szAddress, sizeof(szAddress));
      ROLLERCommsFormatAddr(&pNode->transportAddress, szTransport, sizeof(szTransport));
      SDL_Log("[NET-DISCOVERY] route %s via %s", szAddress, szTransport);
      return;
    }
  }
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
  s_iPendingTransports = 0;
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
  int iTotalSize = 0;
  if (!BuildPacket(packetBuffer, &iTotalSize, pHeader, iHeaderSize, pData, iDataSize)) {
    return 0;
  }
  return SendPacketToIPv4(g_commsState.nodes[iDestNode].transportAddress.uiIPAddress,
                          g_commsState.nodes[iDestNode].transportAddress.unPort,
                          packetBuffer,
                          iTotalSize);
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsBroadcastData(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    uint16_t unPort)
{
  if (!g_commsState.bInitialized) {
    return 0;
  }

  uint8_t packetBuffer[ROLLER_MAX_PACKET_SIZE];
  int iTotalSize = 0;
  if (!BuildPacket(packetBuffer, &iTotalSize, pHeader, iHeaderSize, pData, iDataSize)) {
    return 0;
  }

  int iSuccess = 0;
  if (SendPacketToIPv4(htonl(INADDR_BROADCAST), unPort, packetBuffer, iTotalSize)) {
    SDL_Log("[NET-DISCOVERY] broadcast init to 255.255.255.255:%u", (unsigned)unPort);
    iSuccess = 1;
  }

  if (s_unLocalPort != unPort &&
      SendPacketToIPv4(htonl(INADDR_BROADCAST), s_unLocalPort, packetBuffer, iTotalSize)) {
    SDL_Log("[NET-DISCOVERY] broadcast init to 255.255.255.255:%u", (unsigned)s_unLocalPort);
    iSuccess = 1;
  }

  return iSuccess;
}

//-------------------------------------------------------------------------------------------------

int ROLLERCommsSendDataToAddr(
    const void *pHeader,
    int iHeaderSize,
    const void *pData,
    int iDataSize,
    const void *pAddress)
{
  if (!g_commsState.bInitialized || !pAddress) {
    return 0;
  }

  tROLLERNetAddr address;
  memset(&address, 0, sizeof(address));
  memcpy(&address, pAddress, sizeof(address));
  address.unPadding = 0;
  address.ullReserved = 0;

  if (address.uiIPAddress == 0 || address.unPort == 0) {
    return 0;
  }

  uint8_t packetBuffer[ROLLER_MAX_PACKET_SIZE];
  int iTotalSize = 0;
  if (!BuildPacket(packetBuffer, &iTotalSize, pHeader, iHeaderSize, pData, iDataSize)) {
    return 0;
  }

  return SendPacketToIPv4(address.uiIPAddress, address.unPort, packetBuffer, iTotalSize);
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

  memset(&g_commsState.lastPacketAddress, 0, sizeof(g_commsState.lastPacketAddress));
  g_commsState.lastPacketAddress.uiIPAddress = fromAddr.sin_addr.s_addr;
  g_commsState.lastPacketAddress.unPort = ntohs(fromAddr.sin_port);

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
  tROLLERNetAddr address;
  if (!pAddress)
    return 2;

  memset(&address, 0, sizeof(address));
  memcpy(&address, pAddress, sizeof(address));
  address.unPadding = 0;
  address.ullReserved = 0;

  if (g_commsState.iActiveNodes > 0 &&
      address.uiIPAddress == g_commsState.myAddress.uiIPAddress &&
      address.unPort != g_commsState.myAddress.unPort &&
      !IsLoopbackIPv4(address.uiIPAddress)) {
    char szAddr[32];
    ROLLERCommsFormatAddr(&address, szAddr, sizeof(szAddr));
    SDL_Log("[NET-DISCOVERY] ignored local-address peer %s", szAddr);
    return 2;
  }

  // Idempotent: if this address is already in the table, do nothing
  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    if (memcmp(&g_commsState.nodes[i].address, &address, sizeof(tROLLERNetAddr)) == 0) {
      return 1;  // Already present (not an error — > 1 is the failure sentinel)
    }
  }

  if (g_commsState.iActiveNodes >= ROLLER_MAX_NODES) {
    return 2;  // Too many nodes
  }

  int iNodeIdx = g_commsState.iActiveNodes;
  memcpy(&g_commsState.nodes[iNodeIdx].address, &address, sizeof(tROLLERNetAddr));
  memcpy(&g_commsState.nodes[iNodeIdx].transportAddress, &address, sizeof(tROLLERNetAddr));
  ApplyPendingTransport(&g_commsState.nodes[iNodeIdx]);
  g_commsState.nodes[iNodeIdx].bActive = true;
  g_commsState.iActiveNodes++;

  return 0;  // Success
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsUpdateNodeTransportAddr(const void *pAddress, const void *pTransportAddress)
{
  tROLLERNetAddr address;
  tROLLERNetAddr transportAddress;

  if (!g_commsState.bInitialized || !pAddress || !pTransportAddress)
    return;

  memset(&address, 0, sizeof(address));
  memcpy(&address, pAddress, sizeof(address));
  address.unPadding = 0;
  address.ullReserved = 0;

  memset(&transportAddress, 0, sizeof(transportAddress));
  memcpy(&transportAddress, pTransportAddress, sizeof(transportAddress));
  transportAddress.unPadding = 0;
  transportAddress.ullReserved = 0;

  if (transportAddress.uiIPAddress == 0 || transportAddress.unPort == 0)
    return;

  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    if (memcmp(&g_commsState.nodes[i].address, &address, sizeof(address)) == 0) {
      if (memcmp(&g_commsState.nodes[i].transportAddress, &transportAddress,
                 sizeof(transportAddress)) != 0) {
        char szAddress[32];
        char szTransport[32];
        ROLLERCommsFormatAddr(&address, szAddress, sizeof(szAddress));
        ROLLERCommsFormatAddr(&transportAddress, szTransport, sizeof(szTransport));
        SDL_Log("[NET-DISCOVERY] route %s via %s", szAddress, szTransport);
        g_commsState.nodes[i].transportAddress = transportAddress;
      }
      return;
    }
  }

  RememberPendingTransport(&address, &transportAddress);
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
  // Compact: remove inactive nodes
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

  // Sort by (uiIPAddress, unPort) ascending so every node agrees on the same
  // ordering and the master always lands at index 0.
  for (int i = 0; i < g_commsState.iActiveNodes - 1; i++) {
    for (int j = 0; j < g_commsState.iActiveNodes - 1 - i; j++) {
      tROLLERNetAddr *pA = &g_commsState.nodes[j].address;
      tROLLERNetAddr *pB = &g_commsState.nodes[j + 1].address;
      int bSwap = 0;
      uint32_t uiA = ntohl(pA->uiIPAddress);
      uint32_t uiB = ntohl(pB->uiIPAddress);
      if (uiA > uiB)
        bSwap = 1;
      else if (uiA == uiB && pA->unPort > pB->unPort)
        bSwap = 1;
      if (bSwap) {
        tNodeInfo tmp = g_commsState.nodes[j];
        g_commsState.nodes[j] = g_commsState.nodes[j + 1];
        g_commsState.nodes[j + 1] = tmp;
      }
    }
  }

  // Update iConsoleNode to our address's new position after the sort.
  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    if (memcmp(&g_commsState.nodes[i].address, &g_commsState.myAddress,
               sizeof(tROLLERNetAddr)) == 0) {
      g_commsState.iConsoleNode = i;
      break;
    }
  }

  // Diagnostic: dump the sorted node table so we can verify iConsoleNode is correct on each machine.
  SDL_Log("[NET] SortNodes: %d active nodes, iConsoleNode=%d\n",
         g_commsState.iActiveNodes, g_commsState.iConsoleNode);
  for (int i = 0; i < g_commsState.iActiveNodes; i++) {
    char szIP[INET_ADDRSTRLEN];
    char szTransport[32];
    inet_ntop(AF_INET, &g_commsState.nodes[i].address.uiIPAddress, szIP, sizeof(szIP));
    if (memcmp(&g_commsState.nodes[i].address, &g_commsState.nodes[i].transportAddress,
               sizeof(tROLLERNetAddr)) != 0) {
      ROLLERCommsFormatAddr(&g_commsState.nodes[i].transportAddress,
                            szTransport, sizeof(szTransport));
      SDL_Log("[NET]   node[%d]: %s:%u via %s%s\n", i, szIP,
             (unsigned)g_commsState.nodes[i].address.unPort, szTransport,
             i == g_commsState.iConsoleNode ? " <-- self" : "");
    } else {
      SDL_Log("[NET]   node[%d]: %s:%u%s\n", i, szIP,
             (unsigned)g_commsState.nodes[i].address.unPort,
             i == g_commsState.iConsoleNode ? " <-- self" : "");
    }
  }
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

void ROLLERCommsGetLastPacketAddr(tROLLERNetAddr *pAddressOut)
{
  if (pAddressOut) {
    memcpy(pAddressOut, &g_commsState.lastPacketAddress, sizeof(tROLLERNetAddr));
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

void ROLLERCommsGetLocalAddrStr(char *szBuf, int iBufLen)
{
  if (!szBuf || iBufLen <= 0) return;
  uint32_t uiIP;
  uint16_t unPort;
  if (g_commsState.bInitialized) {
    uiIP   = g_commsState.myAddress.uiIPAddress;
    unPort = g_commsState.myAddress.unPort;
  } else {
    uiIP   = GetConfiguredLocalIPv4();
    unPort = s_unLocalPort;
  }
  char szIP[INET_ADDRSTRLEN];
  struct in_addr addr;
  addr.s_addr = uiIP;
  inet_ntop(AF_INET, &addr, szIP, sizeof(szIP));
  snprintf(szBuf, iBufLen, "%s:%u", szIP, (unsigned)unPort);
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsGetNodeAddrStr(int iNode, char *szBuf, int iBufLen)
{
  if (!szBuf || iBufLen <= 0) return;
  if (!g_commsState.bInitialized || iNode < 0 || iNode >= g_commsState.iActiveNodes) {
    snprintf(szBuf, iBufLen, "---");
    return;
  }
  char szIP[INET_ADDRSTRLEN];
  struct in_addr addr;
  addr.s_addr = g_commsState.nodes[iNode].address.uiIPAddress;
  inet_ntop(AF_INET, &addr, szIP, sizeof(szIP));
  if (memcmp(&g_commsState.nodes[iNode].address, &g_commsState.nodes[iNode].transportAddress,
             sizeof(tROLLERNetAddr)) != 0) {
    char szTransport[32];
    ROLLERCommsFormatAddr(&g_commsState.nodes[iNode].transportAddress,
                          szTransport, sizeof(szTransport));
    snprintf(szBuf, iBufLen, "%s:%u via %s", szIP,
             (unsigned)g_commsState.nodes[iNode].address.unPort, szTransport);
  } else {
    snprintf(szBuf, iBufLen, "%s:%u", szIP, (unsigned)g_commsState.nodes[iNode].address.unPort);
  }
}

//-------------------------------------------------------------------------------------------------

void ROLLERCommsFormatAddr(const tROLLERNetAddr *pAddress, char *szBuf, int iBufLen)
{
  if (!szBuf || iBufLen <= 0) return;
  if (!pAddress) {
    snprintf(szBuf, iBufLen, "---");
    return;
  }

  char szIP[INET_ADDRSTRLEN];
  struct in_addr addr;
  addr.s_addr = pAddress->uiIPAddress;
  inet_ntop(AF_INET, &addr, szIP, sizeof(szIP));
  snprintf(szBuf, iBufLen, "%s:%u", szIP, (unsigned)pAddress->unPort);
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
