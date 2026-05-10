#ifndef _ROLLER_CDX_H
#define _ROLLER_CDX_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "sound.h"
//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct {
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

#pragma pack(push, 1)
typedef struct {
  uint8 byCommand;
  uint8 byParam0;
  uint8 bySubcommand;
  uint16 unParam1;
  uint8 reserved[8];
  uint8 byPlayFlag;
  uint32 uiStartSector;
  uint32 uiSectorCount;
} tAudioControlParams;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint8 byCommand;
  uint8 byChannelBase;
  uint8 byVolChMaster;
  uint8 byChannelLeft;
  uint8 byVolLeft;
  uint8 byChannelRight;
  uint8 byVolRight;
  uint8 byTerminator;
  uint8 unused;
} tVolumeControl;

//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct {
  uint8 byCommand;
  uint8 byFlags1;
  uint8 bySubCommand;
  uint16 unFlags2;
  uint8 reserved[6];
} tAudioFinished;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

extern int track_playing;
extern int last_audio_track;
extern int numCDdrives;
extern int firstCDdrive;
extern int last_track;
extern int first_track;
extern int sector_size;
extern int tracklengths[99];
extern int track_duration;
extern void *iobuffer;
extern void *cdbuffer;
extern int trackstarts[99];
extern int16 ioselector;
extern int16 cdselector;
extern tAudioControlParams playControl;
extern tIOControlBlock io;
extern DPMI_RMI RMIcd;
extern char volscale[129];
extern int drive;

//-------------------------------------------------------------------------------------------------

int FinishedAudio();
void OpenDoor();
void CloseDoor();
void ResetDrive();
void GetCDStatus();
void WriteIOCTL(uint8 bySubCommand, unsigned int uiSize, void *pBuffer);
void *AllocDOSMemory(int iSizeBytes, int16 *pOutSegment);
void GetAudioInfo();
void PlayTrack(int iTrack);
void PlayTrack4(int iStartTrack);
void RepeatTrack();
void StopTrack();
void SetAudioVolume(int iVolume);
void AudioIOCTL(uint8 bySubCommand);
void FreeDOSMemory(uint16 unSegment);
void intRM(uint8 byInterruptNumber);
void GetFirstCDDrive();
void cdxinit();
void cdxdone();
int cdpresent();
int checkCD(uint8 byDriveIdx);
int criticalhandler();

//-------------------------------------------------------------------------------------------------
#endif
