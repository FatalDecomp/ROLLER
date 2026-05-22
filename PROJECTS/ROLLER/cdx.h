#ifndef _ROLLER_CDX_H
#define _ROLLER_CDX_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct
{
  uint8 byCommand;
  uint8 byFlags1;
  uint8 bySubCommand;
  uint16 unFlags2;
  uint8 byReserved;
  uint8 reserved2[7];
  uint8 byStatus;
  uint32 uiDataOffset;
  uint16 unSector;
  uint16 unCount;
  uint32 uiLba;
} tIOControlBlock;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

extern int track_playing;
extern int last_audio_track;
extern int numCDdrives;
extern int firstCDdrive;
extern int tracklengths[99];
extern int track_duration;
extern void *iobuffer;
extern void *cdbuffer;
extern int16 ioselector;
extern int16 cdselector;
extern tIOControlBlock io;
extern char volscale[129];
extern int drive;

//-------------------------------------------------------------------------------------------------
void ResetDrive();
void *AllocDOSMemory(int iSizeBytes, int16 *pOutSegment);
void GetAudioInfo();
void PlayTrack(int iTrack);
void PlayTrack4(int iStartTrack);
void StopTrack();
void SetAudioVolume(int iVolume);
void GetFirstCDDrive();
void cdxinit();
void cdxdone();
int cdpresent();

//-------------------------------------------------------------------------------------------------
#endif
