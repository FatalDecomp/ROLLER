/* Physical CD audio is intentionally unavailable to roller-core. */
#include "cdx.h"

#include <stdlib.h>

int track_playing;
int last_audio_track;
int numCDdrives;
int firstCDdrive;
int tracklengths[99];
int track_duration;
void *iobuffer;
void *cdbuffer;
int16 ioselector;
int16 cdselector;
tIOControlBlock io;
char volscale[129];
int drive;

void ResetDrive(void)
{
}

void *AllocDOSMemory(int iSizeBytes, int16 *pOutSegment)
{
  if (pOutSegment)
    *pOutSegment = 0;
  if (iSizeBytes <= 0)
    return NULL;
  return malloc((size_t)iSizeBytes);
}

void GetAudioInfo(void)
{
}

void PlayTrack(int iTrack)
{
  (void)iTrack;
  track_playing = 0;
}

void PlayTrack4(int iStartTrack)
{
  (void)iStartTrack;
  track_playing = 0;
}

void StopTrack(void)
{
  track_playing = 0;
}

void SetAudioVolume(int iVolume)
{
  (void)iVolume;
}

void GetFirstCDDrive(void)
{
  firstCDdrive = 0;
  numCDdrives = 0;
}

void cdxinit(void)
{
  track_playing = 0;
  numCDdrives = 0;
}

void cdxdone(void)
{
  track_playing = 0;
}

int cdpresent(void)
{
  return 0;
}
