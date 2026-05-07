#include "snapshot.h"
#include "png_writer.h"
#include "3d.h"
#include "replay.h"
#include "sound.h"
#include "frontend.h"
#include "roller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//-------------------------------------------------------------------------------------------------

int g_bSnapshotMode = 0;
tSnapshotConfig g_SnapshotConfig = { 0 };

//-------------------------------------------------------------------------------------------------

void SnapshotSetReplay(const char *szReplay)
{
  if (!szReplay) return;
  strncpy(g_SnapshotConfig.szReplayName, szReplay, sizeof(g_SnapshotConfig.szReplayName) - 1);
  g_SnapshotConfig.szReplayName[sizeof(g_SnapshotConfig.szReplayName) - 1] = '\0';
}

//-------------------------------------------------------------------------------------------------

void SnapshotSetOutDir(const char *szOutDir)
{
  if (!szOutDir) return;
  strncpy(g_SnapshotConfig.szOutDir, szOutDir, sizeof(g_SnapshotConfig.szOutDir) - 1);
  g_SnapshotConfig.szOutDir[sizeof(g_SnapshotConfig.szOutDir) - 1] = '\0';
}

//-------------------------------------------------------------------------------------------------

int SnapshotParseFrames(const char *szFramesArg)
{
  if (!szFramesArg || !*szFramesArg) return 1;

  char szBuf[512];
  strncpy(szBuf, szFramesArg, sizeof(szBuf) - 1);
  szBuf[sizeof(szBuf) - 1] = '\0';

  g_SnapshotConfig.iNumFrames = 0;
  g_SnapshotConfig.iMaxFrame = -1;

  char *pSaveptr = NULL;
  char *pTok = strtok_r(szBuf, ",", &pSaveptr);
  while (pTok) {
    while (*pTok == ' ' || *pTok == '\t') ++pTok;
    if (!*pTok) return 1;
    char *pEnd = NULL;
    long lVal = strtol(pTok, &pEnd, 10);
    if (pEnd == pTok || (*pEnd != '\0' && *pEnd != ' ' && *pEnd != '\t') || lVal < 0)
      return 1;
    if (g_SnapshotConfig.iNumFrames >= SNAPSHOT_MAX_FRAMES)
      return 1;
    g_SnapshotConfig.iFramesAy[g_SnapshotConfig.iNumFrames++] = (int)lVal;
    if ((int)lVal > g_SnapshotConfig.iMaxFrame)
      g_SnapshotConfig.iMaxFrame = (int)lVal;
    pTok = strtok_r(NULL, ",", &pSaveptr);
  }

  return g_SnapshotConfig.iNumFrames > 0 ? 0 : 1;
}

//-------------------------------------------------------------------------------------------------

void SnapshotZeroScreen(void)
{
  if (!g_bSnapshotMode || !scrbuf) return;
  size_t bytes = (size_t)(SVGA_ON ? 256000 : 64000);
  memset(scrbuf, 0, bytes);
}

//-------------------------------------------------------------------------------------------------

static void SnapshotBuildPath(char *szOut, size_t uiOutSize, int iFrame)
{
  char szStem[64];
  strncpy(szStem, g_SnapshotConfig.szReplayName, sizeof(szStem) - 1);
  szStem[sizeof(szStem) - 1] = '\0';
  // Strip the directory portion if any (caller may have passed a path).
  char *pSlash = strrchr(szStem, '/');
  char *pBack = strrchr(szStem, '\\');
  char *pBase = pSlash;
  if (pBack && (!pBase || pBack > pBase)) pBase = pBack;
  if (pBase) memmove(szStem, pBase + 1, strlen(pBase + 1) + 1);
  // Strip extension.
  char *pDot = strrchr(szStem, '.');
  if (pDot) *pDot = '\0';
  // Lowercase.
  for (char *p = szStem; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
  }

  size_t uiDirLen = strlen(g_SnapshotConfig.szOutDir);
  int bHasSep = uiDirLen > 0 &&
    (g_SnapshotConfig.szOutDir[uiDirLen - 1] == '/' ||
     g_SnapshotConfig.szOutDir[uiDirLen - 1] == '\\');
  snprintf(szOut, uiOutSize, "%s%s%s_%d.png",
           g_SnapshotConfig.szOutDir,
           bHasSep ? "" : "/",
           szStem,
           iFrame);
}

//-------------------------------------------------------------------------------------------------

void SnapshotPresent(void)
{
  if (!g_bSnapshotMode || !scrbuf) return;
  if (!g_bPaletteSet) return;

  int bMatch = 0;
  for (int i = 0; i < g_SnapshotConfig.iNumFrames; ++i) {
    if (g_SnapshotConfig.iFramesAy[i] == currentreplayframe) {
      bMatch = 1;
      break;
    }
  }

  if (bMatch) {
    char szPath[512];
    SnapshotBuildPath(szPath, sizeof(szPath), currentreplayframe);

    int iRc = RollerWriteIndexedPng(szPath, scrbuf, palette, 640, 400);
    if (iRc != 0) {
      fprintf(stderr, "snapshot: PNG write failed (rc=%d) for '%s'\n", iRc, szPath);
    } else {
      fprintf(stdout, "snapshot: wrote '%s' (frame %d)\n", szPath, currentreplayframe);
      fflush(stdout);
    }
    g_SnapshotConfig.iCapturedCount++;
  }

  if (g_SnapshotConfig.iCapturedCount >= g_SnapshotConfig.iNumFrames ||
      currentreplayframe >= g_SnapshotConfig.iMaxFrame) {
    quit_game = 1;
    racing = 0;
  }
}

//-------------------------------------------------------------------------------------------------

void SnapshotAdvanceTick(void)
{
  if (!g_bSnapshotMode) return;
  tickhandler();
}
