#ifndef ROLLER_TESTS_NET_REPLAY_COMPAT_H
#define ROLLER_TESTS_NET_REPLAY_COMPAT_H

#include "replay.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"
#include "roller.h"

#include <stdio.h>
#include <string.h>

static int NetReplayTestWrite(const void *pData, size_t uiSize,
                              size_t uiCount, FILE *pFile)
{
  return fwrite(pData, uiSize, uiCount, pFile) == uiCount;
}

static int NetReplayTestBegin(const char *szFilename)
{
  uint8 byValue;
  int aiEmptyNonCompetitors[MAX_CARS] = {0};
  tReplayCamera aEmptyCameras[100] = {{0}};

  if (!szFilename || replaytype || replayfile || numcars < 1 ||
      numcars > MAX_CARS || racers != numcars)
    return 0;
  replayfile = fopen(szFilename, "wb");
  if (!replayfile)
    return 0;

  byValue = (uint8)TrackLoad;
  if (!NetReplayTestWrite(&byValue, 1, 1, replayfile))
    goto fail;
  byValue = (uint8)numcars;
  if (!NetReplayTestWrite(&byValue, 1, 1, replayfile))
    goto fail;
  byValue = (uint8)ViewType[0];
  if (!NetReplayTestWrite(&byValue, 1, 1, replayfile))
    goto fail;
  byValue = SelectedView[0];
  if (!NetReplayTestWrite(&byValue, 1, 1, replayfile) ||
      !NetReplayTestWrite(aEmptyCameras, sizeof(aEmptyCameras[0]),
                          100, replayfile))
    goto fail;
  byValue = 0;
  if (!NetReplayTestWrite(&byValue, 1, 1, replayfile) ||
      !NetReplayTestWrite(aiEmptyNonCompetitors,
                          sizeof(aiEmptyNonCompetitors[0]), MAX_CARS,
                          replayfile) ||
      !NetReplayTestWrite(&racers, sizeof(racers), 1, replayfile))
    goto fail;
  for (int iCar = 0; iCar < numcars; ++iCar) {
    byValue = Car[iCar].byCarDesignIdx;
    if (!NetReplayTestWrite(&byValue, 1, 1, replayfile))
      goto fail;
  }
  if (!NetReplayTestWrite(driver_names[0], 9, (size_t)numcars, replayfile))
    goto fail;

  replayheader = 673 + 10 * numcars;
  replayblock = 30 * racers + 16;
  discfull = 0;
  memset(newrepsample, 0, sizeof(newrepsample));
  memset(repsample, 0, sizeof(repsample));
  memset(repvolume, 0, sizeof(repvolume));
  replaytype = 1;
  if (ftell(replayfile) == replayheader)
    return 1;

fail:
  fclose(replayfile);
  replayfile = NULL;
  replaytype = 0;
  remove(szFilename);
  return 0;
}

static int NetReplayTestEnd(void)
{
  long lLength;
  int iFrames;

  if (replaytype != 1 || !replayfile || replayblock <= 0 ||
      fflush(replayfile))
    return -1;
  lLength = ftell(replayfile);
  if (lLength < replayheader || (lLength - replayheader) % replayblock)
    return -1;
  iFrames = (int)((lLength - replayheader) / replayblock);
  fclose(replayfile);
  replayfile = NULL;
  replaytype = 0;
  return iFrames;
}

static int NetReplayTestLoad(const char *szFilename, int iExpectedFrames)
{
  int iSavedIntro = intro;
  int iSavedTicks = ticks;
  int iLoaded;

  if (!szFilename || iExpectedFrames < 1 || replaytype || replayfile ||
      strlen(szFilename) >= sizeof(replayfilename))
    return 0;
  strcpy(replayfilename, szFilename);
  replayheader = 673 + 10 * numcars;
  replaytype = 2;
  intro = 1;
  startreplay();
  intro = 0;
  iLoaded = replayfile != NULL && replayframes == iExpectedFrames;
  if (iLoaded) {
    ticks = 0;
    lastreplayframe = -1;
    DoReplayData();
    iLoaded = newreplayframe != 0 && currentreplayframe == 0;
  }
  stopreplay();
  intro = iSavedIntro;
  ticks = iSavedTicks;
  remove(szFilename);
  return iLoaded;
}

#endif
