#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "net_transport.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef SOCKET tNetSocket;
typedef int tNetSocketLength;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NetCloseSocket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int tNetSocket;
typedef socklen_t tNetSocketLength;
#define NET_INVALID_SOCKET (-1)
#define NetCloseSocket close
#endif

struct tNetTransportUdp {
  tNetSocket socketHandle;
  uint16 unPort;
  tNetClockFn pClock;
  void *pClockContext;
#ifdef _WIN32
  LARGE_INTEGER clockFrequency;
#endif
};

#ifdef _WIN32
static int NetSocketsStart(void)
{
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

static void NetSocketsStop(void)
{
  WSACleanup();
}
#else
static int NetSocketsStart(void) { return 1; }
static void NetSocketsStop(void) { }
#endif

static int NetAddressLength(const tNetAddress *pAddress)
{
  if (!pAddress)
    return 0;
  if (pAddress->byFamily == NET_ADDR_IPV4)
    return 4;
  if (pAddress->byFamily == NET_ADDR_IPV6)
    return 16;
  return 0;
}

int NetAddressEqual(const tNetAddress *pA, const tNetAddress *pB)
{
  int iLength = NetAddressLength(pA);
  return iLength && pB && pA->byFamily == pB->byFamily &&
         pA->unPort == pB->unPort &&
         (pA->byFamily != NET_ADDR_IPV6 || pA->uiScopeId == pB->uiScopeId) &&
         memcmp(pA->abAddress, pB->abAddress, (size_t)iLength) == 0;
}

static int NetParseUnsigned(const char *szText, uint32 uiMaximum, uint32 *pValue)
{
  char *szEnd;
  unsigned long ulValue;
  if (!szText || !*szText)
    return 0;
  errno = 0;
  ulValue = strtoul(szText, &szEnd, 10);
  if (errno || *szEnd || ulValue > uiMaximum)
    return 0;
  *pValue = (uint32)ulValue;
  return 1;
}

static int NetParseScope(char *szHost, uint32 *pScopeId)
{
  char *szScope = strrchr(szHost, '%');
  uint32 uiScope;
  if (!szScope) {
    *pScopeId = 0;
    return 1;
  }
  *szScope++ = 0;
  if (NetParseUnsigned(szScope, UINT32_MAX, &uiScope)) {
    *pScopeId = uiScope;
    return 1;
  }
  uiScope = if_nametoindex(szScope);
  if (!uiScope)
    return 0;
  *pScopeId = uiScope;
  return 1;
}

int NetAddressParse(tNetAddress *pAddress, const char *szText, uint16 unDefaultPort)
{
  char szHost[NET_ADDRESS_STRING_CAPACITY];
  const char *szPort = NULL;
  size_t iHostLength;
  uint32 uiPort = unDefaultPort, uiScopeId = 0;
  int iColonCount = 0;
  tNetAddress result;

  if (!pAddress || !szText || !*szText || !NetSocketsStart())
    return 0;
  memset(&result, 0, sizeof(result));
  if (*szText == '[') {
    const char *szClose = strchr(szText + 1, ']');
    if (!szClose) {
      NetSocketsStop();
      return 0;
    }
    iHostLength = (size_t)(szClose - (szText + 1));
    if (szClose[1]) {
      if (szClose[1] != ':' || !szClose[2]) {
        NetSocketsStop();
        return 0;
      }
      szPort = szClose + 2;
    }
  } else {
    const char *p;
    const char *szColon = NULL;
    for (p = szText; *p; ++p) {
      if (*p == ':') {
        ++iColonCount;
        szColon = p;
      }
    }
    if (iColonCount == 1) {
      iHostLength = (size_t)(szColon - szText);
      szPort = szColon + 1;
    } else {
      iHostLength = strlen(szText);
    }
  }
  if (!iHostLength || iHostLength >= sizeof(szHost) ||
      (szPort && !NetParseUnsigned(szPort, 65535, &uiPort))) {
    NetSocketsStop();
    return 0;
  }
  memcpy(szHost, *szText == '[' ? szText + 1 : szText, iHostLength);
  szHost[iHostLength] = 0;

  if (strchr(szHost, ':')) {
    if (!NetParseScope(szHost, &uiScopeId) ||
        inet_pton(AF_INET6, szHost, result.abAddress) != 1) {
      NetSocketsStop();
      return 0;
    }
    result.byFamily = NET_ADDR_IPV6;
    result.uiScopeId = uiScopeId;
  } else {
    if (strchr(szHost, '%') || inet_pton(AF_INET, szHost, result.abAddress) != 1) {
      NetSocketsStop();
      return 0;
    }
    result.byFamily = NET_ADDR_IPV4;
  }
  result.unPort = (uint16)uiPort;
  *pAddress = result;
  NetSocketsStop();
  return 1;
}

int NetAddressFormat(const tNetAddress *pAddress, char *szText, int iCapacity)
{
  char szHost[INET6_ADDRSTRLEN];
  char szScoped[INET6_ADDRSTRLEN + 16];
  const char *szResult;
  int iWritten;
  if (!pAddress || !szText || iCapacity < 1 || !NetSocketsStart())
    return 0;
  if (pAddress->byFamily == NET_ADDR_IPV4) {
    szResult = inet_ntop(AF_INET, pAddress->abAddress, szHost, sizeof(szHost));
    iWritten = szResult ? snprintf(szText, (size_t)iCapacity, "%s:%u", szHost,
                                   (unsigned)pAddress->unPort) : -1;
  } else if (pAddress->byFamily == NET_ADDR_IPV6) {
    szResult = inet_ntop(AF_INET6, pAddress->abAddress, szHost, sizeof(szHost));
    if (szResult && pAddress->uiScopeId)
      snprintf(szScoped, sizeof(szScoped), "%s%%%u", szHost,
               (unsigned)pAddress->uiScopeId);
    else if (szResult)
      snprintf(szScoped, sizeof(szScoped), "%s", szHost);
    iWritten = szResult ? snprintf(szText, (size_t)iCapacity, "[%s]:%u", szScoped,
                                   (unsigned)pAddress->unPort) : -1;
  } else {
    iWritten = -1;
  }
  NetSocketsStop();
  if (iWritten < 0 || iWritten >= iCapacity) {
    szText[0] = 0;
    return 0;
  }
  return 1;
}

static int NetAddressFromSockaddr(tNetAddress *pAddress,
                                  const struct sockaddr *pSocketAddress)
{
  tNetAddress result;
  memset(&result, 0, sizeof(result));
  if (pSocketAddress->sa_family == AF_INET) {
    const struct sockaddr_in *pV4 = (const struct sockaddr_in *)pSocketAddress;
    memcpy(result.abAddress, &pV4->sin_addr, 4);
    result.unPort = ntohs(pV4->sin_port);
    result.byFamily = NET_ADDR_IPV4;
  } else if (pSocketAddress->sa_family == AF_INET6) {
    const struct sockaddr_in6 *pV6 = (const struct sockaddr_in6 *)pSocketAddress;
    const uint8 *pBytes = (const uint8 *)&pV6->sin6_addr;
    static const uint8 abMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (memcmp(pBytes, abMappedPrefix, sizeof(abMappedPrefix)) == 0) {
      memcpy(result.abAddress, pBytes + 12, 4);
      result.byFamily = NET_ADDR_IPV4;
    } else {
      memcpy(result.abAddress, pBytes, 16);
      result.byFamily = NET_ADDR_IPV6;
      result.uiScopeId = pV6->sin6_scope_id;
    }
    result.unPort = ntohs(pV6->sin6_port);
  } else {
    return 0;
  }
  *pAddress = result;
  return 1;
}

static int NetAddressToSockaddr(const tNetAddress *pAddress,
                                struct sockaddr_in6 *pSocketAddress)
{
  static const uint8 abMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  memset(pSocketAddress, 0, sizeof(*pSocketAddress));
  pSocketAddress->sin6_family = AF_INET6;
  pSocketAddress->sin6_port = htons(pAddress->unPort);
  if (pAddress->byFamily == NET_ADDR_IPV4) {
    memcpy(&pSocketAddress->sin6_addr, abMappedPrefix, sizeof(abMappedPrefix));
    memcpy((uint8 *)&pSocketAddress->sin6_addr + 12, pAddress->abAddress, 4);
  } else if (pAddress->byFamily == NET_ADDR_IPV6) {
    memcpy(&pSocketAddress->sin6_addr, pAddress->abAddress, 16);
    pSocketAddress->sin6_scope_id = pAddress->uiScopeId;
  } else {
    return 0;
  }
  return 1;
}

static void NetAddressAppend(tNetAddress *pAddresses, int iCapacity, int *pCount,
                             const struct sockaddr *pSocketAddress, uint16 unPort)
{
  tNetAddress address;
  int iAddress;
  if (*pCount >= iCapacity || !NetAddressFromSockaddr(&address, pSocketAddress))
    return;
  address.unPort = unPort;
  for (iAddress = 0; iAddress < *pCount; ++iAddress)
    if (NetAddressEqual(&pAddresses[iAddress], &address))
      return;
  pAddresses[(*pCount)++] = address;
}

int NetAddressEnumerateLocal(tNetAddress *pAddresses, int iCapacity, uint16 unPort)
{
  int iCount = 0;
  if (!pAddresses || iCapacity < 1 || !NetSocketsStart())
    return -1;
#ifdef _WIN32
  ULONG ulSize = 0;
  IP_ADAPTER_ADDRESSES *pAdapters = NULL, *pAdapter;
  ULONG ulResult = GetAdaptersAddresses(AF_UNSPEC,
      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
      NULL, NULL, &ulSize);
  if (ulResult == ERROR_BUFFER_OVERFLOW) {
    pAdapters = (IP_ADAPTER_ADDRESSES *)malloc(ulSize);
    if (pAdapters)
      ulResult = GetAdaptersAddresses(AF_UNSPEC,
          GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
          NULL, pAdapters, &ulSize);
  }
  if (ulResult == NO_ERROR) {
    for (pAdapter = pAdapters; pAdapter && iCount < iCapacity;
         pAdapter = pAdapter->Next) {
      IP_ADAPTER_UNICAST_ADDRESS *pUnicast;
      if (pAdapter->OperStatus != IfOperStatusUp)
        continue;
      for (pUnicast = pAdapter->FirstUnicastAddress;
           pUnicast && iCount < iCapacity; pUnicast = pUnicast->Next)
        NetAddressAppend(pAddresses, iCapacity, &iCount,
                         pUnicast->Address.lpSockaddr, unPort);
    }
  }
  free(pAdapters);
  NetSocketsStop();
  return ulResult == NO_ERROR ? iCount : -1;
#else
  struct ifaddrs *pInterfaces = NULL, *pInterface;
  if (getifaddrs(&pInterfaces)) {
    NetSocketsStop();
    return -1;
  }
  for (pInterface = pInterfaces; pInterface && iCount < iCapacity;
       pInterface = pInterface->ifa_next) {
    if (pInterface->ifa_addr)
      NetAddressAppend(pAddresses, iCapacity, &iCount,
                       pInterface->ifa_addr, unPort);
  }
  freeifaddrs(pInterfaces);
  NetSocketsStop();
  return iCount;
#endif
}

static int NetUdpSend(void *pContext, const tNetAddress *pTo,
                      const void *pData, int iLength)
{
  tNetTransportUdp *pUdp = (tNetTransportUdp *)pContext;
  struct sockaddr_in6 destination;
  int iSent;
  if (!pUdp || !pData || iLength < 1 || iLength > NET_MAX_PAYLOAD ||
      !NetAddressToSockaddr(pTo, &destination))
    return -1;
  iSent = (int)sendto(pUdp->socketHandle, (const char *)pData, iLength, 0,
                      (const struct sockaddr *)&destination,
                      (tNetSocketLength)sizeof(destination));
  return iSent == iLength ? iSent : -1;
}

static int NetSocketWouldBlock(void)
{
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int NetUdpReceive(void *pContext, tNetAddress *pFrom,
                         void *pData, int iCapacity)
{
  tNetTransportUdp *pUdp = (tNetTransportUdp *)pContext;
  struct sockaddr_in6 source;
  tNetSocketLength iSourceLength = (tNetSocketLength)sizeof(source);
  int iReceived;
  if (!pUdp || !pData || iCapacity < 1)
    return -1;
#ifdef _WIN32
  iReceived = (int)recvfrom(pUdp->socketHandle, (char *)pData, iCapacity, 0,
                            (struct sockaddr *)&source, &iSourceLength);
#else
  {
    struct iovec buffer;
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    buffer.iov_base = pData;
    buffer.iov_len = (size_t)iCapacity;
    message.msg_name = &source;
    message.msg_namelen = iSourceLength;
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    iReceived = (int)recvmsg(pUdp->socketHandle, &message, 0);
    iSourceLength = (tNetSocketLength)message.msg_namelen;
    if (iReceived >= 0 && (message.msg_flags & MSG_TRUNC))
      return -1;
  }
#endif
  if (iReceived < 0)
    return NetSocketWouldBlock() ? 0 : -1;
  if (pFrom && !NetAddressFromSockaddr(pFrom, (const struct sockaddr *)&source))
    return -1;
  return iReceived;
}

static uint64 NetPlatformNowMs(tNetTransportUdp *pUdp)
{
#ifdef _WIN32
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (uint64)((now.QuadPart / pUdp->clockFrequency.QuadPart) * 1000 +
      (now.QuadPart % pUdp->clockFrequency.QuadPart) * 1000 /
          pUdp->clockFrequency.QuadPart);
#else
  struct timespec now;
  (void)pUdp;
  if (clock_gettime(CLOCK_MONOTONIC, &now))
    return 0;
  return (uint64)now.tv_sec * 1000u + (uint64)now.tv_nsec / 1000000u;
#endif
}

static uint64 NetUdpNowMs(void *pContext)
{
  tNetTransportUdp *pUdp = (tNetTransportUdp *)pContext;
  return pUdp->pClock ? pUdp->pClock(pUdp->pClockContext) : NetPlatformNowMs(pUdp);
}

tNetTransportUdp *NetTransportUdpCreate(uint16 unPort)
{
  tNetTransportUdp *pUdp;
  struct sockaddr_in6 bindAddress;
  tNetSocketLength iAddressLength;
  int iV6Only = 0;
#ifdef _WIN32
  u_long ulNonBlocking = 1;
#endif
  if (!NetSocketsStart())
    return NULL;
  pUdp = (tNetTransportUdp *)calloc(1, sizeof(*pUdp));
  if (!pUdp) {
    NetSocketsStop();
    return NULL;
  }
  pUdp->socketHandle = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (pUdp->socketHandle == NET_INVALID_SOCKET ||
      setsockopt(pUdp->socketHandle, IPPROTO_IPV6, IPV6_V6ONLY,
                 (const char *)&iV6Only, sizeof(iV6Only)))
    goto fail;
#ifdef _WIN32
  if (ioctlsocket(pUdp->socketHandle, FIONBIO, &ulNonBlocking))
    goto fail;
  if (!QueryPerformanceFrequency(&pUdp->clockFrequency) ||
      pUdp->clockFrequency.QuadPart <= 0)
    goto fail;
#else
  {
    int iFlags = fcntl(pUdp->socketHandle, F_GETFL, 0);
    if (iFlags < 0 || fcntl(pUdp->socketHandle, F_SETFL, iFlags | O_NONBLOCK))
      goto fail;
  }
#endif
  memset(&bindAddress, 0, sizeof(bindAddress));
  bindAddress.sin6_family = AF_INET6;
  bindAddress.sin6_addr = in6addr_any;
  bindAddress.sin6_port = htons(unPort);
  if (bind(pUdp->socketHandle, (const struct sockaddr *)&bindAddress,
           (tNetSocketLength)sizeof(bindAddress)))
    goto fail;
  iAddressLength = (tNetSocketLength)sizeof(bindAddress);
  if (getsockname(pUdp->socketHandle, (struct sockaddr *)&bindAddress,
                  &iAddressLength))
    goto fail;
  pUdp->unPort = ntohs(bindAddress.sin6_port);
  return pUdp;

fail:
  if (pUdp->socketHandle != NET_INVALID_SOCKET)
    NetCloseSocket(pUdp->socketHandle);
  free(pUdp);
  NetSocketsStop();
  return NULL;
}

void NetTransportUdpDestroy(tNetTransportUdp *pUdp)
{
  if (!pUdp)
    return;
  NetCloseSocket(pUdp->socketHandle);
  free(pUdp);
  NetSocketsStop();
}

tNetTransport NetTransportUdpEndpoint(tNetTransportUdp *pUdp)
{
  tNetTransport transport;
  memset(&transport, 0, sizeof(transport));
  if (pUdp) {
    transport.pContext = pUdp;
    transport.pSend = NetUdpSend;
    transport.pReceive = NetUdpReceive;
    transport.pNowMs = NetUdpNowMs;
  }
  return transport;
}

uint16 NetTransportUdpPort(const tNetTransportUdp *pUdp)
{
  return pUdp ? pUdp->unPort : 0;
}

void NetTransportUdpSetClock(tNetTransportUdp *pUdp, tNetClockFn pClock,
                             void *pClockContext)
{
  if (pUdp) {
    pUdp->pClock = pClock;
    pUdp->pClockContext = pClock ? pClockContext : NULL;
  }
}
