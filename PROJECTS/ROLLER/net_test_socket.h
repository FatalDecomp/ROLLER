#ifndef ROLLER_NET_TEST_SOCKET_H
#define ROLLER_NET_TEST_SOCKET_H
/* Only the test harness and proxy include this platform adapter. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET tNetSocket;
#define NET_BAD_SOCKET INVALID_SOCKET
#define NetSocketClose closesocket
typedef int tNetSocketLength;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int tNetSocket;
#define NET_BAD_SOCKET (-1)
#define NetSocketClose close
typedef socklen_t tNetSocketLength;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int NetSocketInit(void)
{
#ifdef _WIN32
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return 1;
#endif
}
static struct sockaddr_in NetSocketAddress(int iPort)
{
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons((unsigned short)iPort);
  return address;
}
static tNetSocket NetSocketBind(int iType, int iPort)
{
  struct sockaddr_in address = NetSocketAddress(iPort);
  tNetSocket socketHandle = socket(AF_INET, iType, 0);
  if (socketHandle != NET_BAD_SOCKET && bind(socketHandle, (struct sockaddr *)&address, sizeof(address))) {
    NetSocketClose(socketHandle);
    return NET_BAD_SOCKET;
  }
  return socketHandle;
}
static int NetSocketPort(tNetSocket socketHandle)
{
  struct sockaddr_in address;
  tNetSocketLength iSize = sizeof(address);
  return getsockname(socketHandle, (struct sockaddr *)&address, &iSize) ? 0 : ntohs(address.sin_port);
}
static int NetSocketReadable(tNetSocket socketHandle, int iTimeoutMs)
{
  fd_set readSet;
  struct timeval timeout;
  FD_ZERO(&readSet);
  FD_SET(socketHandle, &readSet);
  timeout.tv_sec = iTimeoutMs / 1000;
  timeout.tv_usec = (iTimeoutMs % 1000) * 1000;
  return select((int)socketHandle + 1, &readSet, NULL, NULL, &timeout) > 0;
}
static int NetSocketLine(tNetSocket socketHandle, char *szLine, int iCapacity)
{
  for (int iChar = 0; iChar < iCapacity - 1; ++iChar) {
    if (!NetSocketReadable(socketHandle, 10000) || recv(socketHandle, szLine + iChar, 1, 0) != 1)
      return 0;
    if (szLine[iChar] == '\n') {
      szLine[iChar] = 0;
      return 1;
    }
  }
  return 0;
}
static int NetSocketReply(tNetSocket socketHandle, const char *szReply)
{
  int iLength = (int)strlen(szReply), iSent = 0;
  while (iSent < iLength) {
    int iCount = send(socketHandle, szReply + iSent, iLength - iSent, 0);
    if (iCount <= 0)
      return 0;
    iSent += iCount;
  }
  return 1;
}
#endif
