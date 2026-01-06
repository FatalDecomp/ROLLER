#include "cdx.h"
#include "roller.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#if defined(IS_WINDOWS)
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#elif defined(IS_LINUX)
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif
//-------------------------------------------------------------------------------------------------

int track_playing = 0;    //000A7510
int last_audio_track = 0; //000A7514
int numCDdrives = 0;      //000A7518
int firstCDdrive;         //001A1B4C
int last_track;           //001A1B50
int first_track;          //001A1B54
int sector_size;          //001A1B58
int tracklengths[99];     //001A1B5C
int track_duration;       //001A1CE8
void *iobuffer;           //001A1CEC
void *cdbuffer;           //001A1CF0
int trackstarts[99];      //001A1CF8
int16 ioselector;         //001A1E88
int16 cdselector;         //001A1E8C
tAudioControlParams playControl; //001A1EB8 renamed from "play" by ROLLER
tIOControlBlock io;       //001A1ED0
DPMI_RMI RMIcd;           //001A1EEC
char volscale[129];       //001A1F1E
int drive;                //001A1F9F

//-------------------------------------------------------------------------------------------------
//00074C80
int FinishedAudio()
{
  tAudioFinished audioFinished; // [esp+0h] [ebp-14h] BYREF
  audioFinished.byCommand = 12;

  WriteIOCTL(3u, 0xBu, &audioFinished);

  // Check if audio is still playing (bit 1 of status byte)
  if ((io.unFlags2 & 0x200) != 0)
    return 0;

  track_playing = 0;
  return -1;
}

//-------------------------------------------------------------------------------------------------
//00074CC0
void OpenDoor()
{
  uint8 byCommand = 0;  // Open door command
  WriteIOCTL(0x0C, 1, &byCommand);
}

//-------------------------------------------------------------------------------------------------
//00074CF0
void CloseDoor()
{
  uint8 byCommand = 5;  // Close door command
  WriteIOCTL(0xCu, 1u, &byCommand);
  ResetDrive();
}

//-------------------------------------------------------------------------------------------------
//00074D10
void ResetDrive()
{
  ;
}

//-------------------------------------------------------------------------------------------------
//00074D20
void GetCDStatus()
{
  uint8 szBuf[16]; // [esp+0h] [ebp-10h] BYREF
  memset(szBuf, 0, sizeof(szBuf));

  szBuf[0] = 6;
  WriteIOCTL(3u, 5u, szBuf);
  printf("\n\nStatus:\n");
  if ((szBuf[1] & 1) != 0)
    printf("\nDoor Open");
  else
    printf("\nDoor Closed");
  if ((szBuf[1] & 2) != 0)
    printf("\nDoor unlocked");
  else
    printf("\nDoor locked");
  if ((szBuf[1] & 4) != 0)
    printf("\nSupports Cooked and Raw");
  else
    printf("\nSupports only Cooked");
  if ((szBuf[1] & 8) != 0)
    printf("\nRead/Write");
  else
    printf("\nRead only");
  if ((szBuf[1] & 0x10) != 0)
    printf("\nCan play Audio tracks");
  else
    printf("\nCannot play Audio tracks");
  if ((szBuf[1] & 0x20) != 0)
    printf("\nSupports interleaving");
  else
    printf("\nNo interleaving");
  if ((szBuf[1] & 0x80u) == 0)
    printf("\nNo prefetching");
  else
    printf("\nSupprots prefetching");
  if ((szBuf[2] & 1) != 0)
    printf("\nSupports audio channel manipulation");
  else
    printf("\nNo audio channel manipulation");
  if ((szBuf[2] & 2) != 0)
    printf("\nSupports HSG and Red Book addressing");
  else
    printf("\nSupports only HSG addressing, no Red Book");
  fflush(stdout);
}

//-------------------------------------------------------------------------------------------------
//00074E60
void WriteIOCTL(uint8 bySubCommand, unsigned int uiSize, void *pBuffer)
{
  io.bySubCommand = bySubCommand;
  io.unFlags2 = 0;
  io.unCount = 0;
  io.byCommand = uiSize + 13;
  io.byFlags1 = 0;
  io.byStatus = 0;
  io.uiLba = 0;
  io.unSector = uiSize;

  // Copy input buffer to CD buffer
  memcpy(cdbuffer, pBuffer, uiSize);

  // segment_of(cdbuffer)
  int32 addr = (int32)(int64)cdbuffer;
  int32 sign_mask = addr >> 31;
  int32 adjustment = sign_mask & 15;
  int32 segment = (addr + adjustment) >> 4;
  io.uiDataOffset = (uint32)segment << 16;

  memcpy(iobuffer, &io, sizeof(tIOControlBlock));

  // Prepare real mode call structure
  memset(&RMIcd, 0, sizeof(RMIcd));
  RMIcd.ecx = (uint8)drive;           // CD drive number
  RMIcd.ebx = 0;
  RMIcd.es = (uint16)(((int64)iobuffer + ((int64)iobuffer < 0 ? 15 : 0)) >> 4); // segment_of(iobuffer)
  RMIcd.eax = 0x1510;                           // (Function: Get CD-ROM Drive Information)
  intRM(0x2Fu);                                 // Get address of the driver for a specific CD-ROM drive
  memcpy(pBuffer, cdbuffer, uiSize);

  memcpy(&io, iobuffer, sizeof(tIOControlBlock));
}

//-------------------------------------------------------------------------------------------------
//00074FA0
void *AllocDOSMemory(int iSizeBytes, int16 *pOutSegment)
{
  pOutSegment = NULL;
  return malloc(iSizeBytes);

  //memset(&sregs, 0, sizeof(sregs));             // clear sregs
  //regs.w.ax = 0x100;                            // allocate DOS memory block
  //regs.w.bx = ((iSizeBytes - (__CFSHL__(iSizeBytes >> 31, 4) + 16 * (iSizeBytes >> 31))) >> 4) + 1;// compute number of 16-byte paragraphs needed to hold iSizeBytes
  //int386x(0x31, &regs, &regs, &sregs);
  //
  //// if carry flag is set, alloc failed
  //if (regs.x.cflag) {
  //  *(_DWORD *)pOutSegment = -1;
  //  return 0;
  //} else {
  //  // alloc succeeded
  //  *(_DWORD *)pOutSegment = regs.w.dx;
  //  return (void *)(16 * regs.w.ax);
  //}
}

//-------------------------------------------------------------------------------------------------
//00075020
void GetAudioInfo()
{
  ROLLERGetAudioInfo();
  //uint8 buffer[7];
  //uint32 track_sectors[100];  // Temporary storage for track sectors (including lead-out)
  //
  //// Read TOC header to get first/last tracks and lead-out position
  //buffer[0] = 0x0A;
  //WriteIOCTL(3, 7, buffer);
  //
  //uint32 uiLeadOutPacked = *(uint32 *)&buffer[3];  // Packed MSF format
  //
  //// Store first/last track numbers in global variables
  //first_track = buffer[1];
  //last_track = buffer[2];
  //if (last_track >= 99)
  //  return;
  //
  //// Process audio tracks
  //if (first_track <= last_track) {
  //  for (uint8 byTrack = first_track; byTrack <= last_track; byTrack++) {
  //    // Read track start position
  //    buffer[0] = 0x0B;
  //    buffer[1] = byTrack;
  //    WriteIOCTL(3, 7, buffer);
  //    uint32 uiTrackPacked = *(uint32 *)&buffer[2];  // Packed MSF format
  //
  //    // Convert MSF to sector number (minutes/seconds/frames)
  //    uint32 uiMinutes = (uiTrackPacked >> 16) & 0xFF;
  //    uint32 uiSeconds = (uiTrackPacked >> 8) & 0xFF;
  //    uint32 uiFrames = uiTrackPacked & 0xFF;
  //    uint32 uiSector = (uiMinutes * 4500) + (uiSeconds * 75) + uiFrames - 150;
  //
  //    // Store in global trackstarts array (indexed by track number)
  //    trackstarts[byTrack] = uiSector;
  //    // Cache sector in local array for length calculation
  //    track_sectors[byTrack] = uiSector;
  //  }
  //}
  //
  //// Convert lead-out MSF to sectors
  //uint32 uiLeadMin = (uiLeadOutPacked >> 16) & 0xFF;
  //uint32 uiLeadSec = (uiLeadOutPacked >> 8) & 0xFF;
  //uint32 uiLeadFrame = uiLeadOutPacked & 0xFF;
  //uint32 uiLeadOutSector = (uiLeadMin * 4500) + (uiLeadSec * 75) + uiLeadFrame - 150;
  //track_sectors[last_track + 1] = uiLeadOutSector;  // Store after last track
  //
  //// Calculate track lengths
  //if (first_track <= last_track) {
  //  for (uint8 byTrack = first_track; byTrack <= last_track; byTrack++) {
  //    // Track length = next start position - current start position
  //    uint32 uiLength = track_sectors[byTrack + 1] - track_sectors[byTrack];
  //    // Store in global tracklengths array (indexed by track number)
  //    tracklengths[byTrack] = uiLength;
  //  }
  //}
}

//-------------------------------------------------------------------------------------------------
//000751A0
void PlayTrack(int iTrack)
{
  ROLLERPlayTrack(iTrack);
  g_bRepeat = true;
  //// Prepare audio control structure
  //playControl.byPlayFlag = 1;
  //playControl.uiStartSector = trackstarts[iTrack];
  //playControl.uiSectorCount = tracklengths[iTrack];
  //
  //// Execute audio command
  //AudioIOCTL(0x84u);
  
  // Update global state
  track_playing = -1;
  last_audio_track = iTrack;
  track_duration = tracklengths[iTrack];
}

//-------------------------------------------------------------------------------------------------
//000751F0
void PlayTrack4(int iStartTrack)
{
  ROLLERPlayTrack4(iStartTrack);
  // Calculate total duration of four tracks
  //uint32 uiTotalDuration = tracklengths[iStartTrack] +
  //  tracklengths[iStartTrack + 1] + tracklengths[iStartTrack + 2] + tracklengths[iStartTrack + 3];
  //
  //// Prepare audio control structure
  //playControl.byPlayFlag = 1;  // Play command flag
  //playControl.uiStartSector = trackstarts[iStartTrack];  // Start sector
  //playControl.uiSectorCount = uiTotalDuration;  // Sector count
  //
  //// Execute audio command
  //AudioIOCTL(0x84);  // 0x84 = Play Audio command
  //
  //// Update global state
  //track_duration = uiTotalDuration;
  track_playing = -1; // Indicate track is starting
  last_audio_track = iStartTrack;
}

//-------------------------------------------------------------------------------------------------
//00075260
void RepeatTrack()
{
  g_bRepeat = true;
  //playControl.byPlayFlag = 1;
  //playControl.uiStartSector = trackstarts[last_audio_track];
  //playControl.uiSectorCount = track_duration;
  //AudioIOCTL(0x84u);
  track_playing = -1;
}

//-------------------------------------------------------------------------------------------------
//000752A0
void StopTrack()
{
  ROLLERStopTrack();
  //AudioIOCTL(0x85); //stop track
  track_playing = 0;
}

//-------------------------------------------------------------------------------------------------
//000752E0
void SetAudioVolume(int iVolume)
{
  int iUseVolume; // eax
  //tVolumeControl volCtrl; // [esp+0h] [ebp-14h] BYREF
  
  // Double the volume level (range expansion)
  iUseVolume = 2 * iVolume;
  
  // Clamp volume to [1, 255]
  if (iUseVolume < 1)
    iUseVolume = 1;
  if (iUseVolume > 255)
    iUseVolume = 255;
  
  ROLLERSetAudioVolume(iUseVolume);

  //// Prepare volume control struct
  //volCtrl.byVolChMaster = iUseVolume;
  //volCtrl.byVolLeft = iUseVolume;
  //volCtrl.byVolRight = iUseVolume;
  //volCtrl.unused = iUseVolume;                  // set but ignored?
  //volCtrl.byCommand = 3;
  //volCtrl.byChannelBase = 0;
  //volCtrl.byChannelLeft = 1;
  //volCtrl.byChannelRight = 2;
  //volCtrl.byTerminator = 3;
  //
  //// Send volume command
  //WriteIOCTL(0xCu, 9u, &volCtrl);
}

//-------------------------------------------------------------------------------------------------
//00075340
void AudioIOCTL(uint8 bySubcommand)
{
  // Prepare audio control structure
  playControl.byCommand = 0x16;            // Audio command group
  playControl.bySubcommand = bySubcommand; // Specific audio function
  playControl.byParam0 = 0;                // Clear parameter 1
  playControl.unParam1 = 0;                // Clear parameter 2

  // Copy control structure to I/O buffer
  memcpy(iobuffer, &playControl, sizeof(tAudioControlParams));

  // Prepare DPMI request structure
  memset(&RMIcd, 0, sizeof(DPMI_RMI));
  RMIcd.eax = 0x1510;                  // CD-ROM IOCTL function
  RMIcd.ecx = drive;                   // Current CD-ROM drive

  // Convert linear address to segment:offset format
  uint32 uiLinearAddr = (uint32)(uint64)iobuffer;
  uint16 unSegment = (uiLinearAddr >> 4) & 0xFFFF;  // Segment calculation
  RMIcd.es = unSegment;                      // Set segment register
  RMIcd.ebx = 0;                           // Offset = 0

  // Execute real-mode interrupt
  intRM(0x2F);  // CD-ROM driver interrupt

  // Copy results back from I/O buffer
  memcpy(&playControl, iobuffer, sizeof(tAudioControlParams));
}

//-------------------------------------------------------------------------------------------------
//00075400
void FreeDOSMemory(uint16 unSegment)
{
  //memset(&sregs, 0, sizeof(sregs));
  //regs.w.dx = nSegment;
  //regs.w.ax = 0x101;
  //int386x(0x31, &regs, &regs, &sregs);
}

//-------------------------------------------------------------------------------------------------
//00075450
void intRM(uint8 byInterruptNumber)
{
  /*
  regs.w.ax = 768;                              // DPMI: Simulate Real Mode Interrupt
  regs.w.bx = byInterruptNumber;                // Interrupt number to simulate
  regs.w.cx = 0;                                // Reserved
  sregs.es = __DS__;                            // Segment of RMIcd
  regs.x.edi = (unsigned int)&RMIcd;            // Offset of RMIcd (real mode register set)
  int386x(0x31, &regs, &regs, &sregs);*/
}

//-------------------------------------------------------------------------------------------------
//000754B0
void GetFirstCDDrive()
{
  firstCDdrive = -1;
#if defined(IS_WINDOWS)
  uint32 uiDrives = GetLogicalDrives();
  if (uiDrives == 0)
    return;

  int iCount = 0;
  for (char cDrive = 'A'; cDrive <= 'Z'; cDrive++) {
    if (!(uiDrives & (1 << (cDrive - 'A'))))
      continue;

    char szRootPath[] = { cDrive, ':', '\\', '\0' };
    uint32 uiType = GetDriveTypeA(szRootPath);
    if (uiType == DRIVE_CDROM) {
      if (firstCDdrive == -1) {
        firstCDdrive = cDrive - 'A';
        drive = cDrive - 'A';
      }
      iCount++;
    }
  }

  numCDdrives = iCount;
#elif defined(IS_LINUX)
  const char *szDevPrefix = "/dev/";
  const char *targets[] = { "cdrom", "sr0", "sr1", "sr2", "sr3", NULL };
  int iCount = 0;

  for (int i = 0; targets[i] != NULL; ++i) {
    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s%s", szDevPrefix, targets[i]);
    if (IsCDROMDevice(fullPath)) {
      if (firstCDdrive == -1) {
        firstCDdrive = i;
        drive = i;
      }
      iCount++;
    }
  }

  numCDdrives = iCount;
#endif
  /*
  regs.w.ax = 0x1500;
  regs.w.bx = 0;
  int386(47, &regs, &regs);
  if (regs.w.bx) {
    drive = regs.h.cl;
    firstCDdrive = regs.h.cl;
    numCDdrives = regs.w.bx;
  }*/
}

//-------------------------------------------------------------------------------------------------
//00075520
void cdxinit()
{
  iobuffer = AllocDOSMemory(256, &ioselector);
  cdbuffer = AllocDOSMemory(1024, &cdselector);

  // Initialize the volume scaling table
  for (int i = 0; i < 129; i++) {
    double dPow = pow(i * 127.0 * 127.0, 1.0 / 3.0);

    if (dPow <= 127.0f) {
      float fPow = (float)dPow;
      volscale[i] = (uint8)round(fPow);
    } else {
      volscale[i] = 127;  // Cap to 127 if over the limit
    }
  }
}
//-------------------------------------------------------------------------------------------------
//000755D0
void cdxdone()
{
  free(cdbuffer);
  free(iobuffer);
  cdbuffer = NULL;
  iobuffer = NULL;

  CleanupAudioCD();

  //memset(&sregs, 0, sizeof(sregs));
  //regs.w.dx = cdselector;
  //regs.w.ax = 257;
  //int386x(49, &regs, &regs, &sregs);
  //memset(&sregs, 0, sizeof(sregs));
  //regs.w.dx = ioselector;
  //regs.w.ax = 257;
  //int386x(49, &regs, &regs, &sregs);
}

//-------------------------------------------------------------------------------------------------
//00075660
int cdpresent()
{
  //added by ROLLER
  return (g_iNumTracks > 0) ? -1 : 0;

  //int iSuccess = 0;
  //
  //for (int i = 0; i < numCDdrives; ++i) {
  //  if (!iSuccess)
  //    iSuccess = checkCD(i + firstCDdrive);
  //}
  //return iSuccess;
}

//-------------------------------------------------------------------------------------------------
//00075690
int checkCD(uint8 byDriveIdx)
{
  char szFilename[128];
  char buffer[20];
  FILE *fp;
  uint32 uiBytesRead;

  // Construct filename
  //TODO: linux compatibility
  sprintf(szFilename, "%c:/FATAL.EXE", byDriveIdx + 'A');

  // Try to open the file
  fp = ROLLERfopen(szFilename, "rb");
  if (!fp) {
    //added by ROLLER, check for WHIP.EXE too
    sprintf(szFilename, "%c:/WHIP.EXE", byDriveIdx + 'A');
    fp = ROLLERfopen(szFilename, "rb");
    if (!fp)
      return 0;
  }

  // Read 20 bytes
  uiBytesRead = (int)fread(buffer, 1, 20, fp);
  fclose(fp);

  // Return -1 if read was exactly 20 bytes, else 0
  return (uiBytesRead == 20) ? -1 : 0;
}

//-------------------------------------------------------------------------------------------------
//000756F0
int criticalhandler()
{
  return 3;
  /*
  _BYTE retaddr[8]; // [esp+0h] [ebp+0h]

  // returning 3 is sending a signal "Ignore" to DOS critical error handler
  return MK_FP(*(_WORD *)retaddr, *(_DWORD *)retaddr)(3);*/
}

//-------------------------------------------------------------------------------------------------
