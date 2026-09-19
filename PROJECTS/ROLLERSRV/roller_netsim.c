#include "net_test_socket.h"
#include "net_transport.h"

int main(int iArgc, char **ppArgv)
{
  tNetSocket listenSocket, controlSocket, udpSocket;
  tNetTransportSim *pSim;
  tNetTransport aEndpoints[2];
  tNetSimLink link = {100, 20, 50, 0, 0};
  int aiPorts[2] = {0}, iQuit = 0;
  uint32 uiSeed = 12345;
  char szLine[512], szReply[256];
  if (iArgc == 2)
    uiSeed = (uint32)strtoul(ppArgv[1], NULL, 10);
  if (!NetSocketInit() || !(pSim = NetTransportSimCreate(uiSeed)))
    return 1;
  listenSocket = NetSocketBind(SOCK_STREAM, 0);
  udpSocket = NetSocketBind(SOCK_DGRAM, 0);
  if (listenSocket == NET_BAD_SOCKET || udpSocket == NET_BAD_SOCKET || listen(listenSocket, 1))
    return 1;
  for (int iEndpoint = 0; iEndpoint < 2; ++iEndpoint) {
    aEndpoints[iEndpoint] = NetTransportSimEndpoint(pSim, iEndpoint);
    NetTransportSimSetLink(pSim, iEndpoint, &link);
  }
  printf("{\"control\":%d,\"udp\":%d}\n", NetSocketPort(listenSocket), NetSocketPort(udpSocket));
  fflush(stdout);
  if (!NetSocketReadable(listenSocket, 10000))
    return 1;
  controlSocket = accept(listenSocket, NULL, NULL);
  while (!iQuit && NetSocketLine(controlSocket, szLine, sizeof(szLine))) {
    int iEndpoint, iPort, iExpected;
    unsigned long long ullTime;
    strcpy(szReply, "{\"error\":\"invalid command\"}\n");
    if (sscanf(szLine, "peer %d %d", &iEndpoint, &iPort) == 2 &&
        iEndpoint >= 0 && iEndpoint <= 1 && iPort > 0 && iPort <= 65535) {
      aiPorts[iEndpoint] = iPort;
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (sscanf(szLine, "pump %llu %d", &ullTime, &iExpected) == 2 && iExpected >= 0 && iExpected <= 10000) {
      int iReceived = 0, aiDelivered[2] = {0}, iError = 0;
      /* The previous virtual instant timestamps ingress. Explicit barriers
       * eliminate scheduler-dependent ordering between the two processes. */
      for (; iReceived < iExpected; ++iReceived) {
        struct sockaddr_in from;
        tNetSocketLength iFromSize = sizeof(from);
        char abPacket[NET_MAX_PAYLOAD];
        int iLength;
        if (!NetSocketReadable(udpSocket, 1000)) { iError = 1; break; }
        iLength = recvfrom(udpSocket, abPacket, sizeof(abPacket), 0, (struct sockaddr *)&from, &iFromSize);
        iEndpoint = ntohs(from.sin_port) == aiPorts[0] ? 0 : ntohs(from.sin_port) == aiPorts[1] ? 1 : -1;
        if (iLength <= 0 || iEndpoint < 0 || aEndpoints[iEndpoint].pSend(aEndpoints[iEndpoint].pContext, NULL, abPacket, iLength) != iLength) {
          iError = 1;
          break;
        }
      }
      if (!NetTransportSimAdvance(pSim, (uint64)ullTime))
        iError = 1;
      for (iEndpoint = 0; iEndpoint < 2; ++iEndpoint) {
        char abPacket[NET_MAX_PAYLOAD];
        int iLength;
        struct sockaddr_in destination = NetSocketAddress(aiPorts[iEndpoint]);
        while ((iLength = aEndpoints[iEndpoint].pReceive(aEndpoints[iEndpoint].pContext, NULL, abPacket, sizeof(abPacket))) > 0) {
          if (sendto(udpSocket, abPacket, iLength, 0, (struct sockaddr *)&destination, sizeof(destination)) != iLength)
            iError = 1;
          ++aiDelivered[iEndpoint];
        }
      }
      snprintf(szReply, sizeof(szReply), "{\"ok\":%s,\"received\":%d,\"delivered\":[%d,%d]}\n", iError ? "false" : "true", iReceived, aiDelivered[0], aiDelivered[1]);
    } else if (!strcmp(szLine, "quit")) {
      iQuit = 1;
      strcpy(szReply, "{\"ok\":true}\n");
    }
    if (!NetSocketReply(controlSocket, szReply))
      break;
  }
  NetSocketClose(controlSocket);
  NetSocketClose(listenSocket);
  NetSocketClose(udpSocket);
  NetTransportSimDestroy(pSim);
  return iQuit ? 0 : 1;
}
