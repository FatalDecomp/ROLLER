#include "net_test_socket.h"
#include "net_transport.h"

int main(int iArgc, char **ppArgv)
{
  tNetSocket listenSocket, controlSocket, udpSocket;
  tNetTransportSim *pSim;
  tNetTransport aEndpoints[NET_SIM_MAX_ENDPOINTS];
  tNetSimLink link = {100, 20, 50, 0, 0};
  tNetSimLink aaLinks[NET_SIM_MAX_ENDPOINTS][NET_SIM_MAX_ENDPOINTS];
  int aiPorts[NET_SIM_MAX_ENDPOINTS] = {0}, iQuit = 0;
  uint8 abyFramed[NET_SIM_MAX_ENDPOINTS] = {0};
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
  for (int iEndpoint = 0; iEndpoint < NET_SIM_MAX_ENDPOINTS; ++iEndpoint) {
    aEndpoints[iEndpoint] = NetTransportSimEndpoint(pSim, iEndpoint);
    NetTransportSimSetLink(pSim, iEndpoint, &link);
    for (int iDestination = 0; iDestination < NET_SIM_MAX_ENDPOINTS;
         ++iDestination)
      aaLinks[iEndpoint][iDestination] = link;
  }
  printf("{\"control\":%d,\"udp\":%d}\n", NetSocketPort(listenSocket), NetSocketPort(udpSocket));
  fflush(stdout);
  if (!NetSocketReadable(listenSocket, 10000))
    return 1;
  controlSocket = accept(listenSocket, NULL, NULL);
  while (!iQuit && NetSocketLine(controlSocket, szLine, sizeof(szLine))) {
    int iEndpoint, iPort, iExpected, iRouteDestination;
    unsigned iLatency, iJitter, iLoss, iDuplicate, iReorder;
    unsigned long long ullTime;
    strcpy(szReply, "{\"error\":\"invalid command\"}\n");
    if (sscanf(szLine, "peer %d %d", &iEndpoint, &iPort) == 2 &&
        iEndpoint >= 0 && iEndpoint < NET_SIM_MAX_ENDPOINTS &&
        iPort > 0 && iPort <= 65535) {
      aiPorts[iEndpoint] = iPort;
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (sscanf(szLine, "link %d %u %u %u %u %u", &iEndpoint,
                      &iLatency, &iJitter, &iLoss, &iDuplicate,
                      &iReorder) == 6 &&
               iEndpoint >= 0 && iEndpoint < NET_SIM_MAX_ENDPOINTS &&
               iLatency <= 60000 && iJitter <= 60000 && iLoss <= 1000 &&
               iDuplicate <= 1000 && iReorder <= 1000) {
      tNetSimLink configured = {(uint32)iLatency, (uint32)iJitter,
                                (uint16)iLoss, (uint16)iDuplicate,
                                (uint16)iReorder};
      int iConfigured = NetTransportSimSetLink(pSim, iEndpoint, &configured);
      if (iConfigured)
        for (iRouteDestination = 0;
             iRouteDestination < NET_SIM_MAX_ENDPOINTS;
             ++iRouteDestination)
          aaLinks[iEndpoint][iRouteDestination] = configured;
      snprintf(szReply, sizeof(szReply), "{\"ok\":%s}\n",
          iConfigured ? "true" : "false");
    } else if (sscanf(szLine, "route %d %d %u %u %u %u %u", &iEndpoint,
                      &iRouteDestination, &iLatency, &iJitter, &iLoss,
                      &iDuplicate, &iReorder) == 7 &&
               iEndpoint >= 0 && iEndpoint < NET_SIM_MAX_ENDPOINTS &&
               iRouteDestination >= 0 &&
               iRouteDestination < NET_SIM_MAX_ENDPOINTS &&
               iEndpoint != iRouteDestination && iLatency <= 60000 &&
               iJitter <= 60000 && iLoss <= 1000 && iDuplicate <= 1000 &&
               iReorder <= 1000) {
      aaLinks[iEndpoint][iRouteDestination].uiLatencyMs = (uint32)iLatency;
      aaLinks[iEndpoint][iRouteDestination].uiJitterMs = (uint32)iJitter;
      aaLinks[iEndpoint][iRouteDestination].unLossPermille = (uint16)iLoss;
      aaLinks[iEndpoint][iRouteDestination].unDuplicatePermille =
          (uint16)iDuplicate;
      aaLinks[iEndpoint][iRouteDestination].unReorderPermille =
          (uint16)iReorder;
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (sscanf(szLine, "advance %llu", &ullTime) == 1) {
      int iReceived = 0, aiDelivered[NET_SIM_MAX_ENDPOINTS] = {0};
      int iError = 0;
      while (NetSocketReadable(udpSocket, 0)) {
        struct sockaddr_in from;
        tNetSocketLength iFromSize = sizeof(from);
        uint8 abDatagram[NET_MAX_PAYLOAD + 4];
        const uint8 *pPacket = abDatagram;
        int iLength = recvfrom(udpSocket, (char *)abDatagram,
            sizeof(abDatagram), 0, (struct sockaddr *)&from, &iFromSize);
        int iSource = -1, iDestination = -1;
        for (iEndpoint = 0; iEndpoint < NET_SIM_MAX_ENDPOINTS; ++iEndpoint)
          if (ntohs(from.sin_port) == aiPorts[iEndpoint]) {
            iSource = iEndpoint;
            break;
          }
        if (iLength > 4 && abDatagram[0] == 'R' && abDatagram[1] == 'H') {
          iDestination = abDatagram[3];
          if (abDatagram[2] != iSource)
            iSource = -1;
          pPacket += 4;
          iLength -= 4;
        }
        if (iSource < 0 || iDestination < 0 ||
            iDestination >= NET_SIM_MAX_ENDPOINTS || !aiPorts[iDestination]) {
          iError = 1;
          break;
        }
        abyFramed[iSource] = 1;
        abyFramed[iDestination] = 1;
        {
          tNetAddress destination = {0};
          destination.abAddress[0] = 127;
          destination.abAddress[3] = 1;
          destination.unPort = (uint16)iDestination;
          destination.byFamily = NET_ADDR_IPV4;
          if (!NetTransportSimSetLink(pSim, iSource,
                                      &aaLinks[iSource][iDestination]) ||
              aEndpoints[iSource].pSend(aEndpoints[iSource].pContext,
                                        &destination, pPacket,
                                        iLength) != iLength) {
            iError = 1;
            break;
          }
        }
        ++iReceived;
      }
      if (!NetTransportSimAdvance(pSim, (uint64)ullTime))
        iError = 1;
      for (iEndpoint = 0; iEndpoint < NET_SIM_MAX_ENDPOINTS; ++iEndpoint) {
        uint8 abPacket[NET_MAX_PAYLOAD];
        uint8 abDatagram[NET_MAX_PAYLOAD + 4];
        int iLength;
        tNetAddress source;
        if (!aiPorts[iEndpoint])
          continue;
        while ((iLength = aEndpoints[iEndpoint].pReceive(
                    aEndpoints[iEndpoint].pContext, &source, abPacket,
                    sizeof(abPacket))) > 0) {
          struct sockaddr_in destination = NetSocketAddress(aiPorts[iEndpoint]);
          const uint8 *pSend = abPacket;
          int iSendLength = iLength;
          if (abyFramed[iEndpoint]) {
            abDatagram[0] = 'R';
            abDatagram[1] = 'H';
            abDatagram[2] = (uint8)source.unPort;
            abDatagram[3] = (uint8)iEndpoint;
            memcpy(abDatagram + 4, abPacket, (size_t)iLength);
            pSend = abDatagram;
            iSendLength += 4;
          }
          if (sendto(udpSocket, (const char *)pSend, iSendLength, 0,
                     (struct sockaddr *)&destination, sizeof(destination)) !=
              iSendLength)
            iError = 1;
          ++aiDelivered[iEndpoint];
        }
      }
      snprintf(szReply, sizeof(szReply),
          "{\"ok\":%s,\"received\":%d,\"delivered\":[%d,%d,%d,%d,%d,%d,%d,%d]}\n",
          iError ? "false" : "true", iReceived, aiDelivered[0],
          aiDelivered[1], aiDelivered[2], aiDelivered[3], aiDelivered[4],
          aiDelivered[5], aiDelivered[6], aiDelivered[7]);
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
        iEndpoint = ntohs(from.sin_port) == aiPorts[0] ? 0 :
            ntohs(from.sin_port) == aiPorts[1] ? 1 : -1;
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
