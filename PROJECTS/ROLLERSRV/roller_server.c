#include "net_dedicated.h"
#include "net_harness.h"
#include "net_headless.h"
#include "net_transport.h"
#include "3d.h"
#include "frontend.h"
#include "loadtrak.h"

#include <SDL3/SDL_timer.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROLLER_SERVER_DEFAULT_PORT 7777
#define ROLLER_SERVER_RESULT_DRAIN_MS 1000u

typedef struct
{
  const char *szTrackPath;
  const char *szAssetsPath;
  uint16 unPort;
  uint32 uiSeed;
  int iTrackIndex;
  int iPlayers;
  int iCars;
  int iLaps;
} tRollerServerOptions;

static volatile sig_atomic_t s_iStopRequested;

static void RollerServerSignal(int iSignal)
{
  (void)iSignal;
  s_iStopRequested = 1;
}

static int RollerServerUnsigned(const char *szText, uint64 ullMaximum,
                                uint64 *pullValue)
{
  char *szEnd;
  unsigned long long ullValue;
  if (!szText || !*szText || *szText == '-')
    return 0;
  errno = 0;
  ullValue = strtoull(szText, &szEnd, 10);
  if (errno || *szEnd || ullValue > ullMaximum)
    return 0;
  *pullValue = (uint64)ullValue;
  return 1;
}

static int RollerServerAsciiEqual(char a, char b)
{
  if (a >= 'a' && a <= 'z')
    a = (char)(a - ('a' - 'A'));
  if (b >= 'a' && b <= 'z')
    b = (char)(b - ('a' - 'A'));
  return a == b;
}

static int RollerServerTrackIndex(const char *szPath)
{
  const char *szName = szPath;
  const char *pScan;
  uint64 ullTrack = 0;
  size_t uiLength;
  if (!szPath)
    return 0;
  for (pScan = szPath; *pScan; ++pScan)
    if (*pScan == '/' || *pScan == '\\')
      szName = pScan + 1;
  uiLength = strlen(szName);
  if (uiLength < 10 ||
      !RollerServerAsciiEqual(szName[0], 'T') ||
      !RollerServerAsciiEqual(szName[1], 'R') ||
      !RollerServerAsciiEqual(szName[2], 'A') ||
      !RollerServerAsciiEqual(szName[3], 'C') ||
      !RollerServerAsciiEqual(szName[4], 'K') ||
      szName[uiLength - 4] != '.' ||
      !RollerServerAsciiEqual(szName[uiLength - 3], 'T') ||
      !RollerServerAsciiEqual(szName[uiLength - 2], 'R') ||
      !RollerServerAsciiEqual(szName[uiLength - 1], 'K'))
    return 0;
  for (size_t iChar = 5; iChar < uiLength - 4; ++iChar) {
    if (szName[iChar] < '0' || szName[iChar] > '9')
      return 0;
    ullTrack = ullTrack * 10u + (uint64)(szName[iChar] - '0');
  }
  return ullTrack >= NET_SESSION_FIRST_STOCK_TRACK &&
      ullTrack < NET_SESSION_COMMUNITY_TRACK ? (int)ullTrack : 0;
}

static void RollerServerUsage(const char *szProgram)
{
  fprintf(stderr,
      "usage: %s --track-path FILE --assets-path DIR [options]\n"
      "\n"
      "options:\n"
      " --port N          UDP port (default 7777; 0 selects an ephemeral port)\n"
      " --track-index N   stock track index; inferred from TRACKn.TRK\n"
      " --players N       remote player slots, 1-16 (default 2)\n"
      " --cars N          competitors: 2, 8, or 16 (default 2)\n"
      " --laps N          laps, 1-127 (default 3)\n"
      " --seed N          unsigned 32-bit race seed (default 12345)\n"
      " -h, --help        show this help\n",
      szProgram);
}

static int RollerServerParse(int iArgc, char **ppArgv,
                             tRollerServerOptions *pOptions)
{
  uint64 ullValue;
  memset(pOptions, 0, sizeof(*pOptions));
  pOptions->unPort = ROLLER_SERVER_DEFAULT_PORT;
  pOptions->uiSeed = 12345;
  pOptions->iPlayers = 2;
  pOptions->iCars = 2;
  pOptions->iLaps = 3;
  for (int iArg = 1; iArg < iArgc; ++iArg) {
    const char *szOption = ppArgv[iArg];
    if (!strcmp(szOption, "-h") || !strcmp(szOption, "--help"))
      return -1;
    if (iArg + 1 >= iArgc)
      return 0;
    if (!strcmp(szOption, "--track-path"))
      pOptions->szTrackPath = ppArgv[++iArg];
    else if (!strcmp(szOption, "--assets-path"))
      pOptions->szAssetsPath = ppArgv[++iArg];
    else if (!strcmp(szOption, "--port")) {
      if (!RollerServerUnsigned(ppArgv[++iArg], 65535, &ullValue))
        return 0;
      pOptions->unPort = (uint16)ullValue;
    } else if (!strcmp(szOption, "--track-index")) {
      if (!RollerServerUnsigned(ppArgv[++iArg],
                                NET_SESSION_COMMUNITY_TRACK - 1,
                                &ullValue) ||
          ullValue < NET_SESSION_FIRST_STOCK_TRACK)
        return 0;
      pOptions->iTrackIndex = (int)ullValue;
    } else if (!strcmp(szOption, "--players")) {
      if (!RollerServerUnsigned(ppArgv[++iArg], NET_SESSION_MAX_PLAYERS,
                                &ullValue) || !ullValue)
        return 0;
      pOptions->iPlayers = (int)ullValue;
    } else if (!strcmp(szOption, "--cars")) {
      if (!RollerServerUnsigned(ppArgv[++iArg], 16, &ullValue) ||
          (ullValue != 2 && ullValue != 8 && ullValue != 16))
        return 0;
      pOptions->iCars = (int)ullValue;
    } else if (!strcmp(szOption, "--laps")) {
      if (!RollerServerUnsigned(ppArgv[++iArg], 127, &ullValue) || !ullValue)
        return 0;
      pOptions->iLaps = (int)ullValue;
    } else if (!strcmp(szOption, "--seed")) {
      if (!RollerServerUnsigned(ppArgv[++iArg], UINT32_MAX, &ullValue))
        return 0;
      pOptions->uiSeed = (uint32)ullValue;
    } else {
      return 0;
    }
  }
  if (!pOptions->szTrackPath || !pOptions->szAssetsPath ||
      pOptions->iPlayers > pOptions->iCars)
    return 0;
  if (!pOptions->iTrackIndex)
    pOptions->iTrackIndex = RollerServerTrackIndex(pOptions->szTrackPath);
  return pOptions->iTrackIndex != 0;
}

static int RollerServerConfig(const tRollerServerOptions *pOptions,
                              tNetSessionConfig *pConfig)
{
  tNetSessionConfigOptions defaults;
  memset(pConfig, 0, sizeof(*pConfig));
  NetSessionConfigOptionsDefault(&defaults);
  pConfig->unProtocolVersion = NET_PROTOCOL_VERSION;
  pConfig->unTickRateHz = 36;
  pConfig->bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  pConfig->byMaxPlayers = (uint8)pOptions->iPlayers;
  pConfig->byHostIsDedicated = 1;
  pConfig->iTrackLoad = pOptions->iTrackIndex;
  pConfig->iGameType = 0;
  pConfig->iManualControl = 1;
  pConfig->iCompetitors = pOptions->iCars;
  pConfig->iDamageLevel = 1;
  pConfig->iTextureMode = 1;
  pConfig->uiRandomSeed = pOptions->uiSeed;
  pConfig->uiTrackCRC = community_track_crc(pOptions->szTrackPath);
  memcpy(pConfig->szBuildHash, defaults.szBuildHash,
         sizeof(pConfig->szBuildHash));
  return NetSessionConfigValidate(pConfig);
}

static int RollerServerDedicatedMain(int iArgc, char **ppArgv)
{
  tRollerServerOptions options;
  tNetSessionConfig config;
  tNetTransportUdp *pUdp = NULL;
  tNetChannel *pChannel = NULL;
  tNetDedicated *pDedicated = NULL;
  tNetDedicatedStats stats;
  eNetDedicatedState previousState = NET_DEDICATED_ERROR;
  uint64 ullCompleteMs = 0;
  char szError[512];
  int iParse = RollerServerParse(iArgc, ppArgv, &options);
  int iResult = 1;
  if (iParse <= 0) {
    RollerServerUsage(ppArgv[0]);
    return iParse < 0 ? 0 : 2;
  }
  if (!RollerServerConfig(&options, &config) ||
      !NetSessionConfigApply(&config)) {
    fputs("roller-server: invalid dedicated race configuration\n", stderr);
    return 1;
  }
  if (!NetHeadlessInit(options.szTrackPath, options.szAssetsPath,
                       options.iCars, options.uiSeed,
                       szError, sizeof(szError))) {
    fprintf(stderr, "roller-server: %s\n", szError);
    return 1;
  }
  NoOfLaps = options.iLaps;
  pUdp = NetTransportUdpCreate(options.unPort);
  if (!pUdp) {
    fputs("roller-server: could not bind UDP transport\n", stderr);
    goto cleanup;
  }
  pChannel = NetChannelCreate(NetTransportUdpEndpoint(pUdp));
  if (!pChannel)
    goto cleanup;
  pDedicated = NetDedicatedCreate(pChannel, &config,
                                   NetPlatformRandomBytes, NULL);
  if (!pDedicated)
    goto cleanup;

  signal(SIGINT, RollerServerSignal);
  signal(SIGTERM, RollerServerSignal);
  printf("ROLLER dedicated server listening on UDP port %u; "
         "waiting for %d player%s\n",
         (unsigned)NetTransportUdpPort(pUdp), options.iPlayers,
         options.iPlayers == 1 ? "" : "s");
  fflush(stdout);
  while (!s_iStopRequested) {
    if (!NetDedicatedPump(pDedicated) ||
        !NetDedicatedStats(pDedicated, &stats)) {
      fputs("roller-server: dedicated runtime failed\n", stderr);
      goto cleanup;
    }
    if (stats.state != previousState) {
      if (stats.state == NET_DEDICATED_LOADING)
        puts("All players ready; loading race");
      else if (stats.state == NET_DEDICATED_RACING)
        puts("Race started");
      else if (stats.state == NET_DEDICATED_COMPLETE) {
        printf("Race complete: %d finishers, %d human finishers, %u ticks\n",
               stats.iFinishers, stats.iHumanFinishers,
               stats.uiTicksSimulated);
        ullCompleteMs = NetChannelNowMs(pChannel);
      }
      fflush(stdout);
      previousState = stats.state;
    }
    if (stats.state == NET_DEDICATED_COMPLETE &&
        NetChannelNowMs(pChannel) - ullCompleteMs >=
            ROLLER_SERVER_RESULT_DRAIN_MS) {
      iResult = 0;
      break;
    }
    SDL_Delay(1);
  }
  if (s_iStopRequested)
    iResult = 0;

cleanup:
  NetDedicatedDestroy(pDedicated);
  NetChannelDestroy(pChannel);
  NetTransportUdpDestroy(pUdp);
  return iResult;
}

int main(int iArgc, char **ppArgv)
{
  for (int iArg = 1; iArg < iArgc; ++iArg) {
    if (!strcmp(ppArgv[iArg], "--net-harness"))
      return NetHarnessMain(iArgc, (const char **)ppArgv);
  }
  return RollerServerDedicatedMain(iArgc, ppArgv);
}
