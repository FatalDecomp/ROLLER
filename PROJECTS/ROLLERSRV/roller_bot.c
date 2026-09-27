#include "net_bot.h"
#include "net_headless.h"
#include "net_transport.h"
#include "loadtrak.h"

#include <SDL3/SDL_timer.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROLLER_BOT_DEFAULT_PORT 7777
#define ROLLER_BOT_DEFAULT_TIMEOUT_MS 120000u
#define ROLLER_BOT_INPUT_LEAD 4u
#define ROLLER_BOT_FINISH_DRAIN_MS 3000u

typedef struct
{
  const char *szHost;
  const char *szTrackPath;
  const char *szAssetsPath;
  const char *szName;
  uint16 unPort;
  uint32 uiTimeoutMs;
  int iCar;
  int iCars;
} tRollerBotOptions;

static int RollerBotUnsigned(const char *szText, uint64 ullMaximum,
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

static void RollerBotUsage(const char *szProgram)
{
  fprintf(stderr,
      "usage: %s --track-path FILE --assets-path DIR --car N [options]\n"
      "\n"
      "options:\n"
      " --host ADDRESS    numeric server address (default 127.0.0.1)\n"
      " --port N          server UDP port (default 7777)\n"
      " --name TEXT       player name (default BOT)\n"
      " --cars N          competitors: 2, 8, or 16 (default 2)\n"
      " --timeout-ms N    process timeout (default 120000)\n"
      " -h, --help        show this help\n",
      szProgram);
}

static int RollerBotParse(int iArgc, char **ppArgv,
                          tRollerBotOptions *pOptions)
{
  uint64 ullValue;
  memset(pOptions, 0, sizeof(*pOptions));
  pOptions->szHost = "127.0.0.1";
  pOptions->szName = "BOT";
  pOptions->unPort = ROLLER_BOT_DEFAULT_PORT;
  pOptions->uiTimeoutMs = ROLLER_BOT_DEFAULT_TIMEOUT_MS;
  pOptions->iCar = -1;
  pOptions->iCars = 2;
  for (int iArg = 1; iArg < iArgc; ++iArg) {
    const char *szOption = ppArgv[iArg];
    if (!strcmp(szOption, "-h") || !strcmp(szOption, "--help"))
      return -1;
    if (iArg + 1 >= iArgc)
      return 0;
    if (!strcmp(szOption, "--host"))
      pOptions->szHost = ppArgv[++iArg];
    else if (!strcmp(szOption, "--track-path"))
      pOptions->szTrackPath = ppArgv[++iArg];
    else if (!strcmp(szOption, "--assets-path"))
      pOptions->szAssetsPath = ppArgv[++iArg];
    else if (!strcmp(szOption, "--name"))
      pOptions->szName = ppArgv[++iArg];
    else if (!strcmp(szOption, "--port")) {
      if (!RollerBotUnsigned(ppArgv[++iArg], 65535, &ullValue) || !ullValue)
        return 0;
      pOptions->unPort = (uint16)ullValue;
    } else if (!strcmp(szOption, "--car")) {
      if (!RollerBotUnsigned(ppArgv[++iArg], 15, &ullValue))
        return 0;
      pOptions->iCar = (int)ullValue;
    } else if (!strcmp(szOption, "--cars")) {
      if (!RollerBotUnsigned(ppArgv[++iArg], 16, &ullValue) ||
          (ullValue != 2 && ullValue != 8 && ullValue != 16))
        return 0;
      pOptions->iCars = (int)ullValue;
    } else if (!strcmp(szOption, "--timeout-ms")) {
      if (!RollerBotUnsigned(ppArgv[++iArg], UINT32_MAX, &ullValue) ||
          !ullValue)
        return 0;
      pOptions->uiTimeoutMs = (uint32)ullValue;
    } else {
      return 0;
    }
  }
  return pOptions->szTrackPath && pOptions->szAssetsPath &&
      pOptions->szName && pOptions->szName[0] &&
      pOptions->iCar >= 0 && pOptions->iCar < pOptions->iCars;
}

static int RollerBotMain(int iArgc, char **ppArgv)
{
  tRollerBotOptions options;
  tNetAddress hostAddress;
  tNetTransportUdp *pUdp = NULL;
  tNetChannel *pChannel = NULL;
  tNetConnection *pConnection;
  tNetBot *pBot = NULL;
  tNetBotStats stats;
  eNetBotState previousState = NET_BOT_ERROR;
  uint64 ullStartMs, ullRaceStartMs = 0, ullFinishedMs = 0;
  char szError[512];
  int iParse = RollerBotParse(iArgc, ppArgv, &options);
  int iResult = 1;
  if (iParse <= 0) {
    RollerBotUsage(ppArgv[0]);
    return iParse < 0 ? 0 : 2;
  }
  if (!NetHeadlessInit(options.szTrackPath, options.szAssetsPath,
                       options.iCars, 0, szError, sizeof(szError))) {
    fprintf(stderr, "roller-bot: %s\n", szError);
    return 1;
  }
  if (!NetAddressParse(&hostAddress, options.szHost, options.unPort)) {
    fputs("roller-bot: invalid numeric server address\n", stderr);
    return 2;
  }
  pUdp = NetTransportUdpCreate(0);
  if (!pUdp || !(pChannel = NetChannelCreate(NetTransportUdpEndpoint(pUdp))))
    goto cleanup;
  pConnection = NetChannelAddConnection(pChannel, &hostAddress, 0, 0);
  if (!pConnection)
    goto cleanup;
  pBot = NetBotCreate(pConnection, options.szName, (uint8)options.iCar, 1,
                      community_track_crc(options.szTrackPath));
  if (!pBot || !NetBotStart(pBot))
    goto cleanup;

  memset(&stats, 0, sizeof(stats));
  ullStartMs = NetChannelNowMs(pChannel);
  while (NetChannelNowMs(pChannel) - ullStartMs < options.uiTimeoutMs) {
    eNetBotState state;
    uint64 ullNowMs;
    NetChannelPump(pChannel);
    NetBotPump(pBot);
    state = NetBotState(pBot);
    ullNowMs = NetChannelNowMs(pChannel);
    if (state != previousState) {
      printf("Bot %s state %d\n", options.szName, (int)state);
      fflush(stdout);
      previousState = state;
    }
    if (state == NET_BOT_REFUSED) {
      fprintf(stderr, "roller-bot: join refused: %s\n",
              NetJoinRefuseReasonString(NetBotRefuseReason(pBot)));
      goto cleanup;
    }
    if (state == NET_BOT_ERROR) {
      fputs("roller-bot: protocol error\n", stderr);
      goto cleanup;
    }
    if (!NetBotStats(pBot, &stats))
      goto cleanup;
    if (state == NET_BOT_RACING) {
      uint32 uiDesiredTick;
      if (!ullRaceStartMs)
        ullRaceStartMs = ullNowMs;
      uiDesiredTick = stats.uiStartTick + ROLLER_BOT_INPUT_LEAD +
          (uint32)(((ullNowMs - ullRaceStartMs) * 36u) / 1000u);
      while ((int32)(uiDesiredTick - NetBotNextTick(pBot)) >= 0)
        if (!NetBotTick(pBot, NetBotNextTick(pBot), NULL))
          goto cleanup;
    }
    if (stats.byFinished) {
      if (!ullFinishedMs) {
        ullFinishedMs = ullNowMs;
        printf("Bot %s finished after %u inputs\n",
               options.szName, stats.uiInputsSent);
        fflush(stdout);
      } else if (ullNowMs - ullFinishedMs >= ROLLER_BOT_FINISH_DRAIN_MS) {
        if (stats.uiRejectedMessages) {
          fputs("roller-bot: rejected protocol messages\n", stderr);
          goto cleanup;
        }
        printf("Bot %s complete: %u snapshots, %u laps, 0 rejected\n",
               options.szName, stats.uiSnapshots, stats.uiLapCompletions);
        iResult = 0;
        break;
      }
    }
    SDL_Delay(1);
  }
  if (iResult)
    fputs("roller-bot: timed out before finishing\n", stderr);

cleanup:
  NetBotDestroy(pBot);
  NetChannelDestroy(pChannel);
  NetTransportUdpDestroy(pUdp);
  return iResult;
}

int main(int iArgc, char **ppArgv)
{
  return RollerBotMain(iArgc, ppArgv);
}
