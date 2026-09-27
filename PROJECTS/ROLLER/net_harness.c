#include "net_test_socket.h"
#include "net_harness.h"
#include "net_race_harness.h"
#include "net_headless.h"
#include "net_sim_seam.h"
#include "control.h"
#include "roller.h"
#include <errno.h>

static int NetHarnessPump(tNetSocket socketHandle, int iExpected, uint32 *pCount, uint32 *pHash)
{
  int iDrained = 0;
  while (NetSocketReadable(socketHandle, iDrained < iExpected ? 1000 : 0)) {
    unsigned char abPacket[NET_MAX_PAYLOAD];
    int iLength = recv(socketHandle, (char *)abPacket, sizeof(abPacket), 0);
    if (iLength < 0)
      return 0;
    ++*pCount;
    for (int iByte = 0; iByte < iLength; ++iByte)
      *pHash = (*pHash ^ abPacket[iByte]) * 16777619u;
    ++iDrained;
  }
  return iDrained >= iExpected;
}

int NetHarnessMain(int iArgc, const char **ppArgv)
{
  const char *szTrack = NULL, *szAssets = NULL;
  int iPort = -1, iQuit = 0;
  uint32 uiTicks = 0, uiPackets = 0, uiHash = 2166136261u;
  uint64 ullNowMs = 0;
  tCopyData aInputs[MAX_CARS] = {0};
  char szError[512], szLine[512], szReply[4096];
  tNetSocket listenSocket, controlSocket, udpSocket;
  tNetRaceHarness *pRaceHarness;
  for (int iArg = 1; iArg < iArgc; ++iArg)
    if (!strcmp(ppArgv[iArg], "--net-harness")) {
      char *pEnd;
      long lPort;
      if (++iArg >= iArgc)
        return 2;
      errno = 0;
      lPort = strtol(ppArgv[iArg], &pEnd, 10);
      if (errno || *pEnd || lPort < 0 || lPort > 65535)
        return 2;
      iPort = (int)lPort;
    } else if (!strcmp(ppArgv[iArg], "--track-path") && iArg + 1 < iArgc)
      szTrack = ppArgv[++iArg];
    else if (!strcmp(ppArgv[iArg], "--assets-path") && iArg + 1 < iArgc)
      szAssets = ppArgv[++iArg];
  if (iPort < 0)
    return -1;
  if (!szTrack || !szAssets || !NetSocketInit())
    return 2;
  if (!NetHeadlessInit(szTrack, szAssets, 16, 12345, szError, sizeof(szError))) {
    fprintf(stderr, "%s\n", szError);
    return 1;
  }
  listenSocket = NetSocketBind(SOCK_STREAM, iPort);
  udpSocket = NetSocketBind(SOCK_DGRAM, 0);
  if (listenSocket == NET_BAD_SOCKET || udpSocket == NET_BAD_SOCKET || listen(listenSocket, 1))
    return 1;
  printf("{\"control\":%d,\"udp\":%d}\n", NetSocketPort(listenSocket), NetSocketPort(udpSocket));
  fflush(stdout);
  if (!NetSocketReadable(listenSocket, 10000))
    return 1;
  controlSocket = accept(listenSocket, NULL, NULL);
  pRaceHarness = NetRaceHarnessCreate(udpSocket);
  if (!pRaceHarness)
    return 1;
  while (!iQuit && NetSocketLine(controlSocket, szLine, sizeof(szLine))) {
    int iCount, iCar, iInput, iFlags, iDestination, iValue;
    strcpy(szReply, "{\"error\":\"invalid command or range\"}\n");
    if (NetRaceHarnessCommand(pRaceHarness, szLine, szReply,
                              sizeof(szReply))) {
      /* The race-scenario library owns this command. */
    } else if (sscanf(szLine, "step %d", &iCount) == 1 && iCount >= 0 && iCount <= 10000) {
      for (int iStep = 0; iStep < iCount; ++iStep) {
        NetHarnessPump(udpSocket, 0, &uiPackets, &uiHash);
        NetHeadlessStepInputs(aInputs, numcars);
        ++uiTicks;
        ullNowMs = (uint64)uiTicks * 1000 / 36;
      }
      snprintf(szReply, sizeof(szReply), "{\"tick\":%u,\"now_ms\":%llu}\n", uiTicks, (unsigned long long)ullNowMs);
    } else if (sscanf(szLine, "input %d %d %d", &iCar, &iInput, &iFlags) == 3 &&
               iCar >= 0 && iCar < numcars && iInput >= -32768 && iInput <= 32767 && iFlags >= 0 && iFlags <= 65535) {
      aInputs[iCar].data.unInput = (uint16)iInput;
      aInputs[iCar].data.unFlags = (uint16)iFlags;
      NetSimCanonicaliseInput(&aInputs[iCar].data);
      human_control[iCar] = 1;
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (sscanf(szLine, "set puppet %d %d", &iCar, &iValue) == 2 && iCar >= 0 && iCar < numcars && (iValue == 0 || iValue == 1)) {
      NetSimSetPuppet(iCar, iValue);
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (sscanf(szLine, "set authority %d", &iValue) == 1 && (iValue == 0 || iValue == 1)) {
      net_sim_authority = iValue;
      strcpy(szReply, "{\"ok\":true}\n");
    } else if (!strcmp(szLine, "stats")) {
      NetHarnessPump(udpSocket, 0, &uiPackets, &uiHash);
      snprintf(szReply, sizeof(szReply), "{\"tick\":%u,\"packets\":%u,\"hash\":%u}\n", uiTicks, uiPackets, uiHash);
    } else if (sscanf(szLine, "drain %d", &iCount) == 1 && iCount >= 0 && iCount <= 10000) {
      snprintf(szReply, sizeof(szReply), "{\"ok\":%s,\"packets\":%u}\n", NetHarnessPump(udpSocket, iCount, &uiPackets, &uiHash) ? "true" : "false", uiPackets);
    } else if (sscanf(szLine, "car %d", &iCar) == 1 && iCar >= 0 && iCar < numcars) {
      snprintf(szReply, sizeof(szReply), "{\"chunk\":%d,\"speed\":%.9g,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}\n", Car[iCar].nCurrChunk, Car[iCar].fFinalSpeed, Car[iCar].pos.fX, Car[iCar].pos.fY, Car[iCar].pos.fZ);
    } else if (sscanf(szLine, "worldpose %d", &iCar) == 1 && iCar >= 0 && iCar < numcars) {
      tNetWorldPose pose;
      if (NetSimLegacyToWorld(&Car[iCar], &pose))
        snprintf(szReply, sizeof(szReply), "{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"yaw\":%d}\n", pose.position.fX, pose.position.fY, pose.position.fZ, pose.nYaw);
    } else if (sscanf(szLine, "ramp %d", &iCar) == 1 && iCar >= 0 && iCar < totalramps) {
      snprintf(szReply, sizeof(szReply), "{\"tick\":%d,\"group\":%d,\"timer\":%d}\n", ramp[iCar]->iTickStartIdx, ramp[iCar]->iTimingGroup2, ramp[iCar]->iRunningTimer);
    } else if (!strcmp(szLine, "context")) {
      snprintf(szReply, sizeof(szReply), "{\"game_frame\":%d,\"countdown\":%d,\"race_started\":%d,\"readptr\":%d,\"writeptr\":%d}\n", game_frame, countdown, race_started, readptr, writeptr);
    } else if (!strcmp(szLine, "rng")) {
      snprintf(szReply, sizeof(szReply), "{\"state\":%u,\"draws\":%llu}\n", ROLLERrandStateGet(), (unsigned long long)ROLLERrandDrawCountGet());
    } else if (sscanf(szLine, "send %d %d", &iDestination, &iValue) == 2 && iDestination > 0 && iDestination <= 65535) {
      struct sockaddr_in address = NetSocketAddress(iDestination);
      char szPacket[32];
      int iLength = snprintf(szPacket, sizeof(szPacket), "%d", iValue);
      snprintf(szReply, sizeof(szReply), "{\"ok\":%s}\n", sendto(udpSocket, szPacket, iLength, 0, (struct sockaddr *)&address, sizeof(address)) == iLength ? "true" : "false");
    } else if (!strcmp(szLine, "quit")) {
      iQuit = 1;
      strcpy(szReply, "{\"ok\":true}\n");
    }
    if (!NetSocketReply(controlSocket, szReply))
      break;
  }
  NetRaceHarnessDestroy(pRaceHarness);
  NetSocketClose(controlSocket);
  NetSocketClose(listenSocket);
  NetSocketClose(udpSocket);
  return iQuit ? 0 : 1;
}
