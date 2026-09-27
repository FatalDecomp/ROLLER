#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net_rendezvous.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t s_iStop;

static void NetRendezvousSignal(int iSignal)
{
  (void)iSignal;
  s_iStop = 1;
}

static void NetRendezvousSleep(void)
{
#ifdef _WIN32
  Sleep(1);
#else
  struct timespec delay = {0, 1000000};
  nanosleep(&delay, NULL);
#endif
}

static void NetRendezvousUsage(const char *szProgram)
{
  fprintf(stderr, "usage: %s [--port 1..65535]\n", szProgram);
}

int main(int iArgc, char **ppArgv)
{
  tNetTransportUdp *pUdp;
  tNetTransport transport;
  tNetRendezvous *pRendezvous;
  unsigned long ulPort = NET_RVZ_DEFAULT_PORT;
  int iArg;
  for (iArg = 1; iArg < iArgc; ++iArg) {
    char *pEnd = NULL;
    if (!strcmp(ppArgv[iArg], "--help")) {
      NetRendezvousUsage(ppArgv[0]);
      return 0;
    }
    if (strcmp(ppArgv[iArg], "--port") || iArg + 1 >= iArgc) {
      NetRendezvousUsage(ppArgv[0]);
      return 2;
    }
    ulPort = strtoul(ppArgv[++iArg], &pEnd, 10);
    if (!pEnd || *pEnd || !ulPort || ulPort > 65535) {
      NetRendezvousUsage(ppArgv[0]);
      return 2;
    }
  }
  pUdp = NetTransportUdpCreate((uint16)ulPort);
  if (!pUdp) {
    fprintf(stderr, "roller-rendezvous: could not bind UDP port %lu\n",
            ulPort);
    return 1;
  }
  transport = NetTransportUdpEndpoint(pUdp);
  pRendezvous = NetRendezvousCreate(transport, NetPlatformRandomBytes, NULL);
  if (!pRendezvous) {
    NetTransportUdpDestroy(pUdp);
    return 1;
  }
  signal(SIGINT, NetRendezvousSignal);
  signal(SIGTERM, NetRendezvousSignal);
  printf("roller-rendezvous listening on UDP port %u\n",
         (unsigned)NetTransportUdpPort(pUdp));
  fflush(stdout);
  while (!s_iStop) {
    if (!NetRendezvousPump(pRendezvous))
      NetRendezvousSleep();
  }
  NetRendezvousDestroy(pRendezvous);
  NetTransportUdpDestroy(pUdp);
  return 0;
}
