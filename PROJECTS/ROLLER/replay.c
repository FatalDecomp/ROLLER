#include "replay.h"
#include "sound.h"
#include "car.h"
#include "3d.h"
#include "moving.h"
#include "func2.h"
#include "roller.h"
#include "loadtrak.h"
#include "control.h"
#include "view.h"
#include "function.h"
#include <string.h>
#include <errno.h>
#include <math.h>
#ifdef IS_WINDOWS
#include <io.h>
#else
#define _GNU_SOURCE
#include <dirent.h>
#include <fnmatch.h>
#endif
//-------------------------------------------------------------------------------------------------
//symbol names added by ROLLER
char g_szGss[5] = ".GSS";         //000A253C
char g_szNewEdit[10] = "NEW EDIT:"; //000A274C
char g_szEdit[6] = "EDIT:";         //000A2758

//-------------------------------------------------------------------------------------------------

int disciconpressed = 0;  //000A63AC
int rotpoint = 0;         //000A63B0
int replaypanel = -1;     //000A63B4
int controlicon = 9;      //000A63B8
int replayspeeds[9] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 }; //000A63BC
char *replayname[9] = {   //000A63E0
    "1/16",
    "1/8",
    "1/4",
    "1/2",
    "1",
    "2",
    "4",
    "8",
    "16"
};
int lastintro = -1;       //000A6404
int filingmenu = 0;       //000A6408
int filefile = 0;         //000A640C
int filefiles = 0;        //000A6410
int replaysetspeed = 0;   //000A6414
int replaydirection = 0;  //000A6418
int lastfile = -1;        //000A641C
int lastautocut = -1;     //000A6420
int pend_view_init = -1;  //000A6424
int replayedit = 0;       //000A6428
char replayfilename[32] = "INTRO1.GSS"; //000A642C
char *views[8] = {        //000A644C
  "IN CAR",
  "CHASE",
  "MIRROR",
  "BEHIND",
  "CAMERA",
  "ABOVE",
  "BACK",
  "TEAM"
};
int loading_replay = 0;   //000A6470
tRIcon ricon[26] =        //000A6474
{
  { 0, 0, NULL },
  { 7, 5, &Rframeminus },
  { 29, 5, &Rframeplus },
  { 51, 5, &Rreverseplay },
  { 73, 5, &Rplay },
  { 95, 5, &Rspeedminus },
  { 117, 5, &Rspeedplus },
  { 7, 21, &Rstart },
  { 29, 21, NULL },
  { 51, 21, &DoRstop },
  { 95, 21, NULL },
  { 117, 21, &Rend },
  { 7, 37, &carminus },
  { 29, 37, &carplus },
  { 51, 37, &viewminus },
  { 73, 37, &viewplus },
  { 95, 37, &discmenu },
  { 117, 37, &rtoggleedit },
  { 140, 37, &rstartblock },
  { 162, 37, &rselectblock },
  { 184, 37, &rdeleteblock },
  { 206, 37, &rstoreview },
  { 228, 37, &rremoveview },
  { 250, 37, &rpreviouscut },
  { 272, 37, &rnextcut },
  { 294, 37, &rstartassemble }
};
char *replayhelp[26] = {  //000A6544
  "",
  "FRAME|REVERSE",
  "FRAME|FORWARD",
  "REVERSE PLAY",
  "PLAY",
  "DECREASE|PLAY SPEED",
  "INCREASE|PLAY SPEED",
  "GO TO START",
  "REWIND",
  "STOP",
  "FAST FORWARD",
  "GO TO END",
  "SELECT|PREVIOUS CAR",
  "SELECT|NEXT CAR",
  "SELECT|PREVIOUS VIEW",
  "SELECT|NEXT VIEW",
  "FILE|OPERATIONS",
  "SELECT|EDIT MODE",
  "START|BLOCK",
  "SELECT|BLOCK",
  "DESELECT|BLOCK",
  "STORE|NEW CUT",
  "REMOVE|CUT",
  "GO TO|PREVIOUS CUT",
  "GO TO|NEXT CUT",
  "ASSEMBLE|REPLAY"
};
int lsdsel = 0;           //000A646C
tPoint rrotate[8] =       //000A65AC
{
  { 194, 11 },
  { 199, 11 },
  { 203, 15 },
  { 203, 20 },
  { 199, 24 },
  { 194, 24 },
  { 190, 20 },
  { 190, 15 }
};
tReplayCamera camera[100];//00189980
int disabled[4096];       //00189BD8
char temp_names[16][9];   //0018DBD8
char newrepsample[16];    //0018DC68
char repsample[16];       //0018DC78
char repvolume[16];       //0018DC88
int topfile;              //0018DC98
char filename[500][9];    //0018DC9C
int oldtrack;             //0018EE30
int oldtextures;          //0018EE34
int replayheader;         //0018EE38
FILE *replayfile;         //0018EE3C
int replayspeed;          //0018EE40
int oldcars;              //0018EE44
int replayframes;         //0018EE48
int discfull;             //0018EE4C
int replayblock;          //0018EE50
int currentreplayframe;   //0018EE54
int lastreplayframe;      //0018EE58
int introfiles;           //0018EE70
int newreplayframe;       //0018EE5C
int replayselect;         //0018EE60
int slowing;              //0018EE64
int rewinding;            //0018EE68
int forwarding;           //0018EE6C
int replaystart;          //0018EE74
int cuts;                 //0018EE78
char selectfilename[30];  //0018EE80
char rememberfilename[34];//0018EE9E

//-------------------------------------------------------------------------------------------------
//00063DB0
void setreplaytrack()
{
  char *pszRememberFilename; // edi
  char *pszReplayFilename; // esi
  char c1; // al
  char c2; // al
  int iRandValue1; // eax
  int iIntroFileNum1; // eax
  FILE *pFile; // edi
  int iRandValue2; // eax
  int iIntroFileNum2; // eax
  //uint32 iCheatFlags1; // ecx
  //uint32 iCheatFlags2; // ebx
  int *iNonCompetitor; // esi
  int *pReplayNonCompetitor; // eax
  int iByteOffset; // ebp
  int iCarIndex; // esi
  uint8 iCarDesign; // al
  int iNumCarsTemp; // ecx
  int iCarCounter; // [esp+0h] [ebp-20h]
  uint8 buffer[28]; // [esp+4h] [ebp-1Ch] BYREF

  if (replaytype == 2) {
    oldtrack = TrackLoad;                       // Save current game state before loading replay
    oldcars = numcars;
    oldtextures = textures_off;
    if (intro || replayfilename[0] == 73) {
      pszRememberFilename = rememberfilename;   // Copy current filename to remember buffer
      pszReplayFilename = replayfilename;
      do {
        c1 = *pszReplayFilename;
        *pszRememberFilename = *pszReplayFilename;
        if (!c1)
          break;
        c2 = pszReplayFilename[1];
        pszReplayFilename += 2;
        pszRememberFilename[1] = c2;
        pszRememberFilename += 2;
      } while (c2);
      do {
        iRandValue1 = rand();                   // Generate random intro file number (1 to introfiles)
        iIntroFileNum1 = GetHighOrderRand(introfiles, iRandValue1) + 1;
      } while (iIntroFileNum1 == lastintro && introfiles != 1);
      lastintro = iIntroFileNum1;
      sprintf(replayfilename, "INTRO%d.GSS", iIntroFileNum1);
    }
    pFile = fopen(replayfilename, "rb");        // Open the selected intro GSS file
    if (!pFile) {
      do {
        iRandValue2 = rand();                   // Fallback: try different intro file if first failed to open
        iIntroFileNum2 = GetHighOrderRand(introfiles, iRandValue2) + 1;
      } while (iIntroFileNum2 == lastintro);
      lastintro = iIntroFileNum2;
      sprintf(replayfilename, "INTRO%d.GSS", iIntroFileNum2);
      pFile = fopen(replayfilename, "rb");
    }
    if (pFile) {
      fread(buffer, 1u, 1u, pFile);             // Read track number from GSS file
      TrackLoad = buffer[0];
      game_track = buffer[0];
      fread(buffer, 1u, 1u, pFile);             // Read game flags byte from GSS file

      if ((buffer[0] & 0x20) != 0)            // Process texture flag (bit 5 = 0x20)
        textures_off |= TEX_OFF_ADVANCED_CARS;
      else
        textures_off &= ~TEX_OFF_ADVANCED_CARS;
      cheat_mode = 0;

      if ((buffer[0] & 0x40) != 0)            // Process cheat mode flags (bit 6 = 0x40, bit 7 = 0x80)
      {
        cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
        //iCheatFlags1 = cheat_mode;
        //BYTE1(iCheatFlags1) = BYTE1(cheat_mode) | 0x10;
        //cheat_mode = iCheatFlags1;
      } else {
        cheat_mode &= ~CHEAT_MODE_DOUBLE_TRACK;
        //iCheatFlags2 = cheat_mode;
        //BYTE1(iCheatFlags2) = BYTE1(cheat_mode) & 0xEF;
        //cheat_mode = iCheatFlags2;
      }

      if ((buffer[0] & 0x80u) == 0)
        cheat_mode &= ~CHEAT_MODE_TINY_CARS;
      else
        cheat_mode |= CHEAT_MODE_TINY_CARS;

      iNonCompetitor = non_competitors;
      numcars = buffer[0] & 0x1F;               // Extract number of cars from lower 5 bits (0x1F mask)
      fseek(pFile, 605, 0);                     // Seek to non-competitors data at offset 605
      do {
        pReplayNonCompetitor = iNonCompetitor++;// Read 16 non-competitor entries (4 bytes each)
        fread(pReplayNonCompetitor, 4u, 1u, pFile);
      } while (iNonCompetitor != &non_competitors[16]);
      fread(&racers, 4u, 1u, pFile);            // Read racers data (4 bytes)
      iCarCounter = 0;
      if (numcars > 0) {
        iByteOffset = 0;
        iCarIndex = 0;
        do {
          fread(buffer, 1u, 1u, pFile);         // Read car design index for each car
          iCarDesign = buffer[0];
          Car[iCarIndex].byCarDesignIdx = buffer[0];
          iNumCarsTemp = numcars;
          iByteOffset += 4;
          Drivers_Car[iCarIndex] = iCarDesign;
          ++iCarIndex;
          //*(_DWORD *)&car_texture_names[10][iByteOffset + 252] = iCarDesign;// offset into Drivers_Car
          ++iCarCounter;
        } while (iCarCounter < iNumCarsTemp);
      }
    }
    if (pFile)
      fclose(pFile);                            // Close the GSS file
  }
}

//-------------------------------------------------------------------------------------------------
//00064040
void startreplay()
{
  char *pDestStr; // edi
  const char *pSrcStr; // esi
  char cChar1; // al
  char cChar2; // al
  FILE *pWriteFile; // ecx
  int *pNonCompetitorsWrite; // esi
  int *pCurrentNonCompetitor; // eax
  int iCarWriteCounter; // edi
  int iCarWriteIndex; // esi
  int iDriverNameCounter; // edi
  char *pDriverNamesWrite; // esi
  char *pDriverNamesSrc; // edx
  char *pTempNamesDest; // ebx
  char *pTempPtr1; // edi
  char *pTempPtr2; // esi
  char cTempChar1; // al
  char cTempChar2; // al
  //int iFileHandle; // eax
  //int iFileHandleCopy; // edx
  FILE *pReadFile; // ecx
  //uint32 iCheatFlags1; // edx
  //uint32 iCheatFlags2; // eax
  //uint32 iCheatFlags3; // ecx
  //uint32 iCheatFlags4; // ebx
  int *pNonCompetitorsRead; // esi
  int *pCurrentNonCompetitorRead; // eax
  int iDriverIndex; // ebp
  int iNonCompetitorIndex; // edi
  int iCarArrayIndex; // esi
  uint8 byCarDesignIdx; // al
  int iNonCompetitorValue; // ebx
  double dZCoordinate; // st7
  int iDriverNameIndex; // edi
  char *pDriverNamesRead; // esi
  int iReplayDataIndex; // esi
  char cReplayDataByte; // al
  int iNumCarsLocal; // ebp
  //signed int iCarByteOffset; // eax
  //signed int iTotalCarBytes; // edx
  tReplayData replayData; // [esp+0h] [ebp-3Ch] BYREF
  uint8 buffer[28]; // [esp+20h] [ebp-1Ch] BYREF

  oldcars = numcars;                            // Initialize replay state variables
  lastreplayframe = -1;
  currentreplayframe = 0;
  lastautocut = -1;
  forwarding = 0;
  rewinding = 0;
  slowing = 0;
  replayedit = 0;
  replaysetspeed = 4;
  replaydirection = 1;
  replaypanel = -1;
  controlicon = 9;
  memset(disabled, 255, sizeof(disabled));
  memset(camera, 0, sizeof(camera));
  cuts = 0;
  if (replaytype) {                                             // RECORDING MODE: Set up replay file for writing
    if ((unsigned int)replaytype <= 1) {
      pDestStr = replayfilename;
      pSrcStr = "..\\REPLAYS\\REPLAY.TMP";      // Copy replay temp filename
      discfull = 0;
      do {
        cChar1 = *pSrcStr;
        *pDestStr = *pSrcStr;
        if (!cChar1)
          break;
        cChar2 = pSrcStr[1];
        pSrcStr += 2;
        pDestStr[1] = cChar2;
        pDestStr += 2;
      } while (cChar2);
      pWriteFile = ROLLERfopen(replayfilename, "wb");// Open replay file for writing
      replayfile = pWriteFile;
      if (pWriteFile) {
        buffer[0] = TrackLoad;                  // Write track number to replay file
        fwrite(buffer, 1, 1, pWriteFile);
        buffer[0] = numcars;                    // Write game flags (numcars + texture/cheat flags)
        // TEX_OFF_ADVANCED_CARS
        if ((textures_off & 0x10000) != 0)
          buffer[0] = numcars | 0x20;
        // CHEAT_MODE_DOUBLE_TRACK
        if ((cheat_mode & 0x1000) != 0)
          buffer[0] |= 0x40u;
        // CHEAT_MODE_TINY_CARS
        if ((cheat_mode & 0x8000) != 0)
          buffer[0] |= 0x80u;
        fwrite(buffer, 1, 1, replayfile);
        buffer[0] = (uint8)ViewType[0];                // Write view type and selected view to replay file
        fwrite(buffer, 1, 1, replayfile);
        buffer[0] = SelectedView[0];
        fwrite(buffer, 1, 1, replayfile);
        pNonCompetitorsWrite = non_competitors;
        fwrite(camera, 6, 100, replayfile);     // Write camera data and cuts to replay file
        buffer[0] = cuts;
        fwrite(buffer, 1, 1, replayfile);
        do {
          pCurrentNonCompetitor = pNonCompetitorsWrite++;// Write non-competitors and racers data
          fwrite(pCurrentNonCompetitor, 4, 1, replayfile);
        } while (pNonCompetitorsWrite != &non_competitors[16]);
        fwrite(&racers, 4, 1, replayfile);
        iCarWriteCounter = 0;
        if (numcars > 0) {
          iCarWriteIndex = 0;
          do {
            buffer[0] = Car[iCarWriteIndex].byCarDesignIdx;// Write car design indices for each car
            ++iCarWriteCounter;
            fwrite(buffer, 1, 1, replayfile);
            ++iCarWriteIndex;
          } while (iCarWriteCounter < numcars);
        }
        iDriverNameCounter = 0;
        if (numcars > 0) {
          pDriverNamesWrite = driver_names[0];
          do {
            fwrite(pDriverNamesWrite, 9, 1, replayfile);
            ++iDriverNameCounter;
            pDriverNamesWrite += 9;
          } while (iDriverNameCounter < numcars);
        }
      }
    } else if (replaytype == 2) {                                           // PLAYBACK MODE: Load replay data from GSS file
      if (!intro)
        rev_vga[15] = (tBlockHeader *)load_picture("replaysc.bm");// Load replay screen image if not in intro mode

      int iCounter = 0; //added by ROLLER
      pDriverNamesSrc = driver_names[0];        // Backup current driver names to temp storage
      pTempNamesDest = temp_names[0];
      do {
        pTempPtr1 = pTempNamesDest;
        pTempPtr2 = pDriverNamesSrc;
        do {
          cTempChar1 = *pTempPtr2;
          *pTempPtr1 = *pTempPtr2;
          if (!cTempChar1)
            break;
          cTempChar2 = pTempPtr2[1];
          pTempPtr2 += 2;
          pTempPtr1[1] = cTempChar2;
          pTempPtr1 += 2;
        } while (cTempChar2);
        pDriverNamesSrc += 9;
        pTempNamesDest += 9;

        ++iCounter;
      } while (iCounter < 16);

      replayframes = ROLLERfilelength(replayfilename);
      //iFileHandle = open(replayfilename, 0x200);// Get replay file size to calculate frame count
      //iFileHandleCopy = iFileHandle;
      //if (iFileHandle != -1) {
      //  replayframes = filelength(iFileHandle);
      //  close(iFileHandleCopy);
      //}

      pReadFile = ROLLERfopen(replayfilename, "rb");  // Open replay file for reading
      replayfile = pReadFile;
      if (pReadFile) {
        fread(buffer, 1u, 1u, pReadFile);       // Read track number and game flags from replay file
        TrackLoad = buffer[0];
        game_track = buffer[0];
        fread(buffer, 1u, 1u, replayfile);

        // TEX_OFF_ADVANCED_CARS
        if ((buffer[0] & 0x20) != 0)
          textures_off |= TEX_OFF_ADVANCED_CARS;
        else
          textures_off &= ~TEX_OFF_ADVANCED_CARS;

        cheat_mode = 0;

        // CHEAT_MODE_DOUBLE_TRACK
        if ((buffer[0] & 0x40) != 0) {
          cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
          //iCheatFlags1 = cheat_mode;
          //BYTE1(iCheatFlags1) = BYTE1(cheat_mode) | 0x10;
          //cheat_mode = iCheatFlags1;
        } else {
          cheat_mode &= ~CHEAT_MODE_DOUBLE_TRACK;
          //iCheatFlags2 = cheat_mode;
          //BYTE1(iCheatFlags2) = BYTE1(cheat_mode) & 0xEF;
          //cheat_mode = iCheatFlags2;
        }

        // CHEAT_MODE_TINY_CARS
        if ((buffer[0] & 0x80u) == 0) {
          cheat_mode &= ~CHEAT_MODE_TINY_CARS;
          //iCheatFlags4 = cheat_mode;
          //BYTE1(iCheatFlags4) = BYTE1(cheat_mode) & 0x7F;
          //cheat_mode = iCheatFlags4;
        } else {
          cheat_mode |= CHEAT_MODE_TINY_CARS;
          //iCheatFlags3 = cheat_mode;
          //BYTE1(iCheatFlags3) = BYTE1(cheat_mode) | 0x80;
          //cheat_mode = iCheatFlags3;
        }

        numcars = buffer[0] & 0x1F;
        fread(buffer, 1u, 1u, replayfile);
        ViewType[0] = buffer[0];
        fread(buffer, 1u, 1u, replayfile);
        SelectedView[0] = buffer[0];
        fread(camera, 6u, 0x64u, replayfile);
        fread(buffer, 1u, 1u, replayfile);
        pNonCompetitorsRead = non_competitors;
        cuts = buffer[0];
        do {
          pCurrentNonCompetitorRead = pNonCompetitorsRead++;
          fread(pCurrentNonCompetitorRead, 4u, 1u, replayfile);
        } while (pNonCompetitorsRead != &non_competitors[16]);
        fread(&racers, 4u, 1u, replayfile);
        iDriverIndex = 0;
        if (numcars > 0) {
          iNonCompetitorIndex = 0;
          iCarArrayIndex = 0;
          do {
            fread(buffer, 1u, 1u, replayfile);  // Initialize each car from replay data
            byCarDesignIdx = buffer[0];
            Car[iCarArrayIndex].iDriverIdx = iDriverIndex;
            Car[iCarArrayIndex].byCarDesignIdx = byCarDesignIdx;
            Car[iCarArrayIndex].byDamageToggle = 0;
            iNonCompetitorValue = non_competitors[iNonCompetitorIndex];
            Car[iCarArrayIndex].byLastDamageToggle = 20;
            if (iNonCompetitorValue)          // Position non-competitor cars off-track
            {
              Car[iCarArrayIndex].pos.fX = -localdata[0].pointAy[3].fX;
              Car[iCarArrayIndex].pos.fY = -localdata[0].pointAy[3].fY;
              dZCoordinate = -localdata[0].pointAy[3].fZ;
              Car[iCarArrayIndex].iControlType = 0;
              Car[iCarArrayIndex].byLives = -1;
              Car[iCarArrayIndex].fFinalSpeed = 0.0;
              Car[iCarArrayIndex].fSpeedOverflow = 0.0;
              finished_car[iNonCompetitorIndex] = -1;
              Car[iCarArrayIndex].nReferenceChunk = Car[iCarArrayIndex].nCurrChunk;
              Car[iCarArrayIndex].nCurrChunk = -1;
              Car[iCarArrayIndex].pos.fZ = (float)dZCoordinate + 1000.0f;
            }
            ++iNonCompetitorIndex;
            ++iDriverIndex;
            ++iCarArrayIndex;
          } while (iDriverIndex < numcars);
        }
        iDriverNameIndex = 0;
        if (numcars > 0) {
          pDriverNamesRead = driver_names[0];   // Read driver names from replay file
          do {
            fread(pDriverNamesRead, 9u, 1u, replayfile);// Write driver names (9 bytes each)
            ++iDriverNameIndex;
            pDriverNamesRead += 9;
          } while (iDriverNameIndex < numcars);
        }
        fseek(replayfile, replayheader, 0);     // Seek to replay frame data header
        iReplayDataIndex = 0;
        if (numcars > 0) {
          do {
            fread(&replayData, 0x1Eu, 1u, replayfile);// Read initial replay frame data (30 bytes per car)
            ++iReplayDataIndex;
            cReplayDataByte = GET_HIBYTE(replayData.iPackedPosX);
            iNumCarsLocal = numcars;

//TODO: ensure this is correct
            newrepsample[iReplayDataIndex - 1] = GET_HIBYTE(replayData.iPackedPosX);
            repsample[iReplayDataIndex - 1] = cReplayDataByte;
            //temp_names[15][iReplayDataIndex + 8] = replayData[3];
            //newrepsample[iReplayDataIndex + 15] = cReplayDataByte;
          } while (iReplayDataIndex < iNumCarsLocal);
        }
        replayheader = 10 * numcars + 673;      // Calculate replay header size and frame block size
        replayblock = 30 * racers + 16;
        replayframes = (int)(replayframes - replayheader) / (30 * racers + 16);
      }

      //TODO: ensure this is correct
      for (int iCarIndex = 0; iCarIndex < numcars; iCarIndex++)
      {
        // Initialize car physics data for replay
        Car[iCarIndex].iControlType = 8;           // was the DWORD assignment
        Car[iCarIndex].fHorizontalSpeed = 0.0f;    // was hitboxAy[14][2].fZ
        Car[iCarIndex].direction.fX = 0.0f;        // was hitboxAy[14][3].fY  
        Car[iCarIndex].direction.fY = 0.0f;        // was hitboxAy[14][1].fY
        Car[iCarIndex].direction.fZ = 0.0f;        // was hitboxAy[14][1].fZ
      }
      //if (numcars > 0) {
      //  iCarByteOffset = 0;
      //  iTotalCarBytes = sizeof(tCar) * numcars;
      //  do {
      //    iCarByteOffset += sizeof(tCar);       // Initialize car physics data for replay
      //    *(_DWORD *)((char *)&CarBox.hitboxAy[15][6].fY + iCarByteOffset) = 8;// offset into Car
      //    *(float *)((char *)&CarBox.hitboxAy[14][2].fZ + iCarByteOffset) = 0.0;
      //    *(float *)((char *)&CarBox.hitboxAy[14][3].fY + iCarByteOffset) = 0.0;
      //    *(float *)((char *)&CarBox.hitboxAy[14][1].fY + iCarByteOffset) = 0.0;
      //    *(float *)((char *)&CarBox.hitboxAy[14][1].fZ + iCarByteOffset) = 0.0;
      //  } while (iCarByteOffset < iTotalCarBytes);
      //}

      replayspeed = 256;                        // Set default replay speed and check for intro mode
      if (replayfilename[0] == 73 && !intro)
        filingmenu = 1;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00064730
void stopreplay()
{
  char *szTempNamePtr; // edx
  char *szDriverNamePtr; // ebx
  char *szDstPtr; // edi
  char *szSrcPtr; // esi
  char byChar1; // al
  char byChar2; // al
  //int uiAvailBytes; // edx
  //int iFileHandle; // eax
  //int iFileHandleCopy; // ebx
  //int iNewFileSize; // edx
  char *szReplayDstPtr; // edi
  char *szRememberSrcPtr; // esi
  char byReplayChar1; // al
  char byReplayChar2; // al
  //_diskfree_t diskfree; // [esp-2h] [ebp-1Ch] BYREF

  if (replaytype) {                            // Close replay file if replay is active
    if (replayfile) //check added by ROLLER
      fclose(replayfile);
  }
  if (replaytype == 2)                        // If replay type is 2 (recording/playback mode), restore driver names
  {
    int iCounter = 0;
    szTempNamePtr = temp_names[0];              // Copy temp driver names back to original driver names array
    szDriverNamePtr = driver_names[0];
    do {
      szDstPtr = szDriverNamePtr;
      szSrcPtr = szTempNamePtr;
      do {
        byChar1 = *szSrcPtr;
        *szDstPtr = *szSrcPtr;
        if (!byChar1)
          break;
        byChar2 = szSrcPtr[1];
        szSrcPtr += 2;
        szDstPtr[1] = byChar2;
        szDstPtr += 2;
      } while (byChar2);
      szTempNamePtr += 9;
      szDriverNamePtr += 9;

      ++iCounter;
    } while (iCounter < 16);
    TrackLoad = oldtrack;                       // Restore original track, car count and texture settings
    numcars = oldcars;
    textures_off = oldtextures;
  }
  if (replaytype == 1)                        // If replay type is 1 (recording mode), manage disk space
  {
    //dos_getdiskfree(0, &diskfree);              // Get disk free space information
    //uiAvailBytes = HIWORD(diskfree.total_clusters) * LOWORD(diskfree.avail_clusters) * HIWORD(diskfree.avail_clusters);// Calculate available disk space in bytes
    //if (uiAvailBytes < 0x100000)              // Check if available space is less than required threshold
    //{
    //  iFileHandle = open(replayfilename, 0x202);// Open replay file for trimming
    //  iFileHandleCopy = iFileHandle;
    //  if (iFileHandle != -1) {
    //    iNewFileSize = filelength(iFileHandle) - (0x100000 - uiAvailBytes);// Calculate new file size to free up disk space
    //    if (iNewFileSize >= 20480)            // If new size >= 20KB, truncate file; otherwise delete it
    //    {
    //      chsize(iFileHandleCopy, iNewFileSize);
    //      close(iFileHandleCopy);
    //    } else {
    //      close(iFileHandleCopy);
    //      remove(replayfilename);
    //    }
    //  }
    //}
  }
  replaytype = 0;                               // Reset replay type to inactive (0)
  if (intro)                                  // If in intro mode, restore original filename
  {
    szReplayDstPtr = replayfilename;            // Copy remembered filename back to replay filename
    szRememberSrcPtr = rememberfilename;
    do {
      byReplayChar1 = *szRememberSrcPtr;
      *szReplayDstPtr = *szRememberSrcPtr;
      if (!byReplayChar1)
        break;
      byReplayChar2 = szRememberSrcPtr[1];
      szRememberSrcPtr += 2;
      szReplayDstPtr[1] = byReplayChar2;
      szReplayDstPtr += 2;
    } while (byReplayChar2);
  }
}

//-------------------------------------------------------------------------------------------------
//00064880
void DoReplayData()
{                                               // Check replay type: 0=disabled, 1=recording, 2=playback
  int iCarIndex; // esi
  tCar *pCar; // edi
  int iCarArrayIndex; // ebp
  char byNewRepSample; // ah
  char byAdjustedSample; // al
  int16 nPitchWithOffsets; // ax
  int16 nRollWithOffsets; // ax
  tStuntData **ppRamp; // esi
  int i; // edi
  //tStuntData *pRampData; // eax
  tCar *pReplayCar; // ebp
  int iCarIndexForReplay; // edx
  int16 nChunkFromReplay; // dx
  int16 nNegativeChunk; // ax
  int iStatusAndSpeed; // edx
  int iLivesFromReplay; // edx
  int iStunnedDirection; // ecx
  int iWheelAnimFrame; // eax
  int iDamageState_1; // edi
  int iTrackSegmentIdx; // ebx
  int iGroundColorIdx; // ecx
  float fNearestDistance; // eax
  int iCarSpray; // eax
  int iSprayIdx; // ecx
  tCarSpray *pCarSpray; // ebx
  int iDamageIntensity; // edi
  int iRandomValue; // eax
  tStuntData **ppRampReplay; // esi
  int j; // ebp
  int iFrameDifference; // ebx
  int iDelayWriteNew; // esi
  int iInterpolationSteps; // ecx
  int iNextDelaySlot; // edx
  unsigned int uiSoundOffset; // edi
  int iEngineVol; // eax
  int iEngineStepCounter; // ebp
  int iEngineVolDelta; // edi
  int iEnginePitchOld; // eax
  int iEnginePitchNew; // edi
  int iEnginePitchDelta; // ebp
  int iEngine2DelaySlot; // edx
  unsigned int uiEngine2SoundOffset; // edi
  int iEngine2StepIdx; // edi
  int iEngine2Vol; // eax
  int iEngine2VolCurrent; // ebp
  int iEngine2PitchOld; // eax
  int iEngine2PitchNew; // ebp
  int iEngine2PitchStep; // edi
  int iEngine2PitchDelta; // ebp
  int iSkidDelaySlot; // edx
  unsigned int uiSkidSoundOffset; // edi
  int iSkid1Vol; // eax
  int iSkidVolCurrent; // ebp
  int iSkidVolDelta; // edi
  int iSkidPitchOld; // eax
  int iSkidPitchNew; // edi
  int iSkidPitchStep; // ebp
  int iSkidPitchDelta; // edi
  int iPanStepIdx; // edi
  int iPan; // eax
  int iPanDelta; // ebp
  int iPanOld; // eax
  int iPanNew; // edi
  int iPanStepCounter; // ebp
  int iCurrentCut; // ecx
  int iCutIndex; // eax
  int iCutArrayIndex; // edx
  int iUpdateSteps; // ecx
  int iSmokeCarIndex; // esi
  tCar *pSmokeCar; // edi
  int iStepCounter; // edx
  int iTexCleanupIdx; // ebx
  int iTexOffsetStep; // ecx
  int iCarTexIdx; // esi
  unsigned int uiTexArrayOffset; // eax
  tReplayData replayData;
  int iPanNewCopy; // [esp+20h] [ebp-114h]
  int iPanDeltaStep; // [esp+24h] [ebp-110h]
  int iPanOldCopy; // [esp+28h] [ebp-10Ch]
  unsigned int uiDelayOldOffset; // [esp+2Ch] [ebp-108h]
  int iPanStepCount; // [esp+30h] [ebp-104h]
  int iSkidStepCount; // [esp+34h] [ebp-100h]
  int iEnginePitchStepCount; // [esp+38h] [ebp-FCh]
  float fDistanceBuffer; // [esp+40h] [ebp-F4h]
  int iDelayIdxForEngine; // [esp+50h] [ebp-E4h]
  int iDelayWriteOld; // [esp+54h] [ebp-E0h]
  int bReadDisabled; // [esp+58h] [ebp-DCh]
  int iDamageState; // [esp+5Ch] [ebp-D8h]
  int iReplayCarIndex; // [esp+60h] [ebp-D4h]
  int iNearestChunk; // [esp+64h] [ebp-D0h] BYREF
  unsigned int uiEnginePitchOffset; // [esp+68h] [ebp-CCh]
  unsigned int uiEngine2PitchOffset; // [esp+6Ch] [ebp-C8h]
  unsigned int uiSkidPitchOffset; // [esp+70h] [ebp-C4h]
  unsigned int uiDelayOldByteOffset; // [esp+74h] [ebp-C0h]
  unsigned int uiDelayNewByteOffset; // [esp+78h] [ebp-BCh]
  unsigned int uiPanSoundOffset; // [esp+7Ch] [ebp-B8h]
  int iNextDelayIndex; // [esp+80h] [ebp-B4h]
  unsigned int uiEngineCalcOffset; // [esp+84h] [ebp-B0h]
  unsigned int uiEngine2CalcOffset; // [esp+88h] [ebp-ACh]
  unsigned int uiSkidCalcOffset; // [esp+8Ch] [ebp-A8h]
  unsigned int uiPanCalcOffset; // [esp+90h] [ebp-A4h]
  unsigned int uiCarOffset; // [esp+94h] [ebp-A0h]
  int iEngineVolStep; // [esp+98h] [ebp-9Ch]
  int iEngineVolIncrement; // [esp+9Ch] [ebp-98h]
  int iEngineVolBase; // [esp+A0h] [ebp-94h]
  int iEnginePitchStep; // [esp+A4h] [ebp-90h]
  int iEnginePitchBase; // [esp+A8h] [ebp-8Ch]
  int iEngine2VolIncrement; // [esp+ACh] [ebp-88h]
  int iEngine2VolDelta; // [esp+B0h] [ebp-84h]
  int iEngine2VolBase; // [esp+B4h] [ebp-80h]
  int iEngine2PitchWorking; // [esp+B8h] [ebp-7Ch]
  int iEngine2PitchIncrement; // [esp+BCh] [ebp-78h]
  int iEngine2PitchBase; // [esp+C0h] [ebp-74h]
  int iSkidVolIncrement; // [esp+C4h] [ebp-70h]
  int iSkidVolBase; // [esp+C8h] [ebp-6Ch]
  int iSkidPitchWorking; // [esp+CCh] [ebp-68h]
  int iSkidPitchIncrement; // [esp+D0h] [ebp-64h]
  int iSkidPitchBase; // [esp+D4h] [ebp-60h]
  int iPanStep; // [esp+D8h] [ebp-5Ch]
  int iPanIncrement; // [esp+DCh] [ebp-58h]
  int iPanBase; // [esp+E0h] [ebp-54h]
  int iCarLoopIndex; // [esp+E4h] [ebp-50h]
  unsigned int uiSoundOffsetCopy; // [esp+E8h] [ebp-4Ch]
  unsigned int uiSoundDataOffset; // [esp+ECh] [ebp-48h]
  int iTotalCarDataSize; // [esp+F0h] [ebp-44h]
  float fBestDistance; // [esp+F4h] [ebp-40h]
  int iPanLoopStep; // [esp+F8h] [ebp-3Ch]
  int iEngine2LoopStep; // [esp+FCh] [ebp-38h]
  int iSkidPitchDelayIdx; // [esp+100h] [ebp-34h]
  int iSkidVolDelayIdx; // [esp+104h] [ebp-30h]
  int iEngine2PitchDelayIdx; // [esp+108h] [ebp-2Ch]
  int iEnginePitchDelayIdx; // [esp+10Ch] [ebp-28h]
  int iConversionBuffer; // [esp+110h] [ebp-24h]
  tStuntData *pRampData_1; // [esp+114h] [ebp-20h] BYREF
  int iTempCalcBuffer; // [esp+118h] [ebp-1Ch]

  if (replaytype) {
    if ((unsigned int)replaytype <= 1) {                                           // Recording mode: write replay data to file
      if (replayfile && !discfull) {
        iCarIndex = 0;

        // Loop through all cars to record their state
        if (numcars > 0) {
          pCar = Car;
          iCarArrayIndex = 0;
          do {
            if (!non_competitors[iCarArrayIndex]) {
              byNewRepSample = newrepsample[iCarIndex];
              //_EDX = pCar;
              if (byNewRepSample) {
                if (repsample[iCarIndex] > 0)
                  byAdjustedSample = -byNewRepSample - 1;
                else
                  byAdjustedSample = byNewRepSample + 1;
                repsample[iCarIndex] = byAdjustedSample;
              }

              replayData.iPackedPosX = PACK_24_WITH_TAG((int32)pCar->pos.fX, repsample[iCarIndex]);
              replayData.iPackedPosY = PACK_24_WITH_TAG((int32)pCar->pos.fY, repvolume[iCarIndex]);
              replayData.iPackedPosZ = PACK_24_WITH_TAG((int32)pCar->pos.fZ, (uint8)pCar->iSteeringInput);
              //__asm { fld     dword ptr[edx]; Load Real }
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //iPackedPosX = (repsample[iCarIndex] << 24) | iConversionBuffer & 0xFFFFFF;// Pack X position with sample data in high byte
              //__asm { fld     dword ptr[edx + 4]; Load Real }
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //iPackedPosY = ((unsigned __int8)repvolume[iCarIndex] << 24) | iConversionBuffer & 0xFFFFFF;// Pack Y position with volume data in high byte
              //__asm { fld     dword ptr[edx + 8]; Load Real }
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //iPackedPosZ = (pCar->iSteeringInput << 24) | iConversionBuffer & 0xFFFFFF;// Pack Z position with steering input in high byte

              if (pCar->nCurrChunk == -1)
                replayData.nCurrChunk = -pCar->nReferenceChunk;
              else
                replayData.nCurrChunk = pCar->nCurrChunk;
              replayData.nDesiredYaw = pCar->nYaw;
              replayData.nActualYaw = pCar->nActualYaw;
              nPitchWithOffsets = (int16)(pCar->iPitchCameraOffset) + (int16)(pCar->iPitchDynamicOffset) + pCar->nPitch;
              nPitchWithOffsets &= 0x3FFF;
              //HIBYTE(nPitchWithOffsets) &= 0x3Fu;
              replayData.nPitchPacked = nPitchWithOffsets;
              nRollWithOffsets = (int16)(pCar->iRollCameraOffset) + (int16)(pCar->iRollDynamicOffset) + pCar->nRoll;
              nRollWithOffsets &= 0x3FFF;
              //HIBYTE(nRollWithOffsets) &= 0x3Fu;
              replayData.nRollPacked = nRollWithOffsets;

              // Convert car speed (take absolute value) and pack with lives/status data
              //_CHP();
              iConversionBuffer = (int)fabs(pCar->fFinalSpeed);
              replayData.nSpeedAndStatus = iConversionBuffer | (((char)pCar->byLives + 2) << 10);
              // Pack status flags into high byte
              if ( (pCar->byStatusFlags & 2) != 0 )
                replayData.nSpeedAndStatus |= 0x2000u;
                //HIBYTE(nSpeedAndStatus) |= 0x20u;
              if ( (pCar->byStatusFlags & 4) != 0 )
                replayData.nSpeedAndStatus |= 0x4000u;
                //HIBYTE(nSpeedAndStatus) |= 0x40u;
              // Convert RPM ratio with scaling factor
              //_CHP();
              iConversionBuffer = (int)(pCar->fRPMRatio * 255.0);
              replayData.byRPMPacked = iConversionBuffer;
              // Convert health value and pack with stunned flag
              //_CHP();
              iConversionBuffer = (int)pCar->fHealth;
              replayData.byHealthAndStunned = iConversionBuffer | ((uint8)(pCar->iStunned) << 7); //TODO: check on this
              //__asm
              //{
              //  fld     dword ptr[edx + 18h]; Load Real
              //  fabs; Absolute value
              //}
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //nSpeedAndStatus = iConversionBuffer | (((char)pCar->byLives + 2) << 10);// Pack speed and lives data into replay format
              //if ((pCar->byStatusFlags & 2) != 0)
              //  HIBYTE(nSpeedAndStatus) |= 0x20u;
              //if ((pCar->byStatusFlags & 4) != 0)
              //  HIBYTE(nSpeedAndStatus) |= 0x40u;
              //__asm
              //{
              //  fld     dword ptr[edx + 78h]; Load Real
              //  fmul    replay_c_variable_15; Multiply Real
              //}
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //byRPMPacked = iConversionBuffer;
              //__asm { fld     dword ptr[edx + 1Ch]; Load Real }
              //_CHP();
              //__asm { fistp[esp + 134h + var_24]; Store Integer and Pop }
              //byHealthAndStunned = iConversionBuffer | (LOBYTE(pCar->iStunned) << 7);

              replayData.byMiscCarData = (pCar->byDamageToggle << 7) + 8 * (uint8)(pCar->iDamageState) + pCar->byGearAyMax + 2 + 16 * pCar->byWheelAnimationFrame;
              replayData.byLap = pCar->byLap;
              if (pCar->nDeathTimer >= 0)
                replayData.byDeathTimer = (char)pCar->nDeathTimer;
              else
                replayData.byDeathTimer = -1;
              replayData.byDamageIntensity = pCar->byDamageIntensity;
              if (!fwrite(&replayData.iPackedPosX, 30, 1, replayfile))
                discfull = -1;
            }
            ++iCarArrayIndex;
            ++iCarIndex;
            ++pCar;
          } while (iCarIndex < numcars);
        }

        // Record stunt ramp timing data (8 ramps max)
        ppRamp = ramp;
        for (i = 0; i < 8; ++i) {
          int16 nRampTiming;
          if (*ppRamp) {
              // Ramp exists - get its activation timing
            nRampTiming = (*ppRamp)->iTickStartIdx;
          } else {
              // No ramp in this slot - write 0
            nRampTiming = 0;
          }
          // Write 2 bytes (16-bit timing value) to replay file
          if (!fwrite(&nRampTiming, 2, 1, replayfile))
            discfull = -1;
          ++ppRamp;  // Move to next ramp slot
        }
        //ppRamp = ramp;
        //for (i = 0; i < 8; ++i) {
        //  pRampData = *ppRamp;
        //  if (*ppRamp) {
        //    LOWORD(pRampData) = pRampData->iTickStartIdx;
        //    ++ppRamp;
        //  } else {
        //    pRampData = 0;
        //  }
        //  pRampData_1 = pRampData;
        //  if (!fwrite(&pRampData_1, 2, 1, replayfile))
        //    discfull = -1;
        //}
      }
    } else if (replaytype == 2) {                                           // Playback mode: read and apply replay data
      if (replayspeed != 256)
        stopallsamples();
      newreplayframe = 0;
      if (replayfile) {
        currentreplayframe = ticks;
        bReadDisabled = 0;
        if (ticks >= replayframes)
          currentreplayframe = replayframes - 1;
        if (currentreplayframe < 0)
          currentreplayframe = 0;
        if (!rewinding && !forwarding && readdisable(currentreplayframe)) {
          if (replayspeed <= 0) {
            if (replayspeed < 0)
              findlastvalid();
          } else {
            findnextvalid();
          }
          bReadDisabled = -1;
        }
        if (replayframes - 1 == currentreplayframe && replayspeed > 0 && replaytype == 2) {
          //_disable();
          replayspeed = 0;
          fraction = 0;
          replaydirection = 0;
          ticks = currentreplayframe;
          //_enable();
        }
        if (!currentreplayframe && replayspeed < 0 && replaytype == 2) {
          //_disable();
          replayspeed = currentreplayframe;
          fraction = currentreplayframe;
          replaydirection = currentreplayframe;
          ticks = currentreplayframe;
          //_enable();
        }
        if (lastreplayframe == currentreplayframe) {
          newreplayframe = 0;
        } else {
          newreplayframe = -1;
          fseek(replayfile, replayblock * currentreplayframe + replayheader, 0);// Seek to current replay frame in file
          iReplayCarIndex = 0;

          // Read car state data from replay file
          if (numcars > 0) {
            pReplayCar = Car;
            uiCarOffset = 0;
            do {
              if (!non_competitors[uiCarOffset / 4]) {
                fread(&replayData, 0x1Eu, 1u, replayfile);

                // Unpack X position: extract lower 24 bits and convert to float
                pReplayCar->pos.fX = (float)SIGN_EXTEND_24(replayData.iPackedPosX);
                pReplayCar->pos.fY = (float)SIGN_EXTEND_24(replayData.iPackedPosY);
                pReplayCar->pos.fZ = (float)SIGN_EXTEND_24(replayData.iPackedPosZ);
                //iConversionBuffer = iPackedPosX << 8 >> 8;
                //__asm
                //{
                //  fild[esp + 134h + var_24]; Load Integer
                //  fstp    dword ptr[ebp + 0]; Store Real and Pop
                //}
                //pReplayCar->pos.fX = _ET1;
                //iConversionBuffer = iPackedPosY << 8 >> 8;
                //__asm
                //{
                //  fild[esp + 134h + var_24]; Load Integer
                //  fstp    dword ptr[ebp + 4]; Store Real and Pop
                //}
                //pReplayCar->pos.fY = _ET1;
                //iConversionBuffer = iPackedPosZ << 8 >> 8;
                //__asm
                //{
                //  fild[esp + 134h + var_24]; Load Integer
                //  fstp    dword ptr[ebp + 8]; Store Real and Pop
                //}
                //pReplayCar->pos.fZ = _ET1;

                iCarIndexForReplay = iReplayCarIndex;
                newrepsample[iReplayCarIndex] = GET_HIBYTE(replayData.iPackedPosX);
                repvolume[iCarIndexForReplay] = GET_HIBYTE(replayData.iPackedPosY);
                pReplayCar->iSteeringInput = replayData.iPackedPosZ >> 24;
                nChunkFromReplay = replayData.nCurrChunk;
                //_ESI = pReplayCar;
                if (replayData.nCurrChunk >= 0) {
                  pReplayCar->iControlType = 3;
                  pReplayCar->nCurrChunk = nChunkFromReplay;
                  pReplayCar->nReferenceChunk = nChunkFromReplay;
                  pReplayCar->iLastValidChunk = nChunkFromReplay;
                } else {
                  pReplayCar->nCurrChunk = -1;
                  nNegativeChunk = replayData.nCurrChunk;
                  pReplayCar->iControlType = 0;
                  pReplayCar->nReferenceChunk = nNegativeChunk;
                  pReplayCar->nReferenceChunk = -nNegativeChunk;
                  findnearsection(pReplayCar, &iNearestChunk);
                }
                if (pReplayCar->iControlType == 3) {
                  tData *pData = &localdata[pReplayCar->nCurrChunk];
                  // Compare car's Y position with track lane boundary
                  if ( pReplayCar->pos.fY < pData->fTrackHalfWidth || isnan(pReplayCar->pos.fY) )
                  {
                    // Check if car is in the opposite lane boundary  
                    if ( pReplayCar->pos.fY > -pData->fTrackHalfWidth || isnan(pReplayCar->pos.fY) )
                      pReplayCar->iLaneType = 1;    // Left lane or boundary area
                    else  
                      pReplayCar->iLaneType = 2;    // Far left outside track
                  }
                  else
                  {
                    pReplayCar->iLaneType = 0;        // Right lane or center
                  }
                  //__asm { fld     dword ptr[esi + 4]; Load Real }
                  //_EDX = &localdata[pReplayCar->nCurrChunk];
                  //__asm
                  //{
                  //  fcomp   dword ptr[edx + 34h]; Compare Real and Pop
                  //  fnstsw  ax; Store Status Word(no wait)
                  //}
                  //if ((_AX & 0x100) != 0 || (_AX & 0x4000) != 0) {
                  //  __asm
                  //  {
                  //    fld     dword ptr[edx + 34h]; Load Real
                  //    fchs; Change Sign
                  //    fcomp   dword ptr[esi + 4]; Compare Real and Pop
                  //    fnstsw  ax; Store Status Word(no wait)
                  //  }
                  //  if ((_AX & 0x100) != 0 || (_AX & 0x4000) != 0)
                  //    pReplayCar->iLaneType = 1;
                  //  else
                  //    pReplayCar->iLaneType = 2;
                  //} else {
                  //  pReplayCar->iLaneType = 0;
                  //}
                }
                pReplayCar->nYaw = replayData.nDesiredYaw;
                pReplayCar->nActualYaw = replayData.nActualYaw;
                pReplayCar->nPitch = replayData.nPitchPacked;
                //HIWORD(iStatusAndSpeed) = 0;
                pReplayCar->nRoll = replayData.nRollPacked;
                //LOWORD(iStatusAndSpeed) = nSpeedAndStatus;
                iStatusAndSpeed = replayData.nSpeedAndStatus;

                // Extract speed data from packed format (lower 10 bits) and convert to float
                iConversionBuffer = replayData.nSpeedAndStatus & 0x2FF;  // Extract lower 10 bits containing speed
                pReplayCar->byStatusFlags = 0;
                pReplayCar->fFinalSpeed = (float)iConversionBuffer;
                //iConversionBuffer = nSpeedAndStatus & 0x2FF;
                //__asm { fild[esp + 134h + var_24]; Load Integer }
                //pReplayCar->byStatusFlags = 0;
                //__asm { fstp    dword ptr[esi + 18h]; Store Real and Pop }
                //pReplayCar->fFinalSpeed = _ET1;

                if ((iStatusAndSpeed & 0x2000) != 0)
                  pReplayCar->byStatusFlags = 2;
                if ((iStatusAndSpeed & 0x4000) != 0)
                  pReplayCar->byStatusFlags |= 4u;
                iLivesFromReplay = (iStatusAndSpeed >> 10) & 7;
                if (iLivesFromReplay) {
                  pReplayCar->byLives = iLivesFromReplay - 2;
                } else {
                  pReplayCar->byStatusFlags = 0;
                  pReplayCar->byLives = 3;
                }
                pReplayCar->nDeathTimer = replayData.byDeathTimer;

                // Unpack RPM data: convert packed byte back to floating point ratio
                pReplayCar->fRPMRatio = (float)replayData.byRPMPacked * 0.00392156862745098f;
                //LOWORD(iTempCalcBuffer) = byRPMPacked;
                //__asm
                //{
                //  fild    word ptr[esp + 134h + var_1C]; Load Integer
                //  fmul    replay_c_variable_16; Multiply Real
                //  fstp    dword ptr[esi + 78h]; Store Real and Pop
                //}
                //pReplayCar->fRPMRatio = _ET1;

                // Extract health data from packed format (lower 7 bits) and convert to float
                iTempCalcBuffer = replayData.byHealthAndStunned & 0x7F;  // Extract lower 7 bits containing health
                pReplayCar->fHealth = (float)iTempCalcBuffer;
                //iTempCalcBuffer = byHealthAndStunned & 0x7F;
                //__asm
                //{
                //  fild    word ptr[esp + 134h + var_1C]; Load Integer
                //  fstp    dword ptr[esi + 1Ch]; Store Real and Pop
                //}
                //pReplayCar->fHealth = _ET1;

                iStunnedDirection = (int)replayData.byHealthAndStunned >> 7;
                pReplayCar->iStunned = iStunnedDirection;
                pReplayCar->iStunned = -iStunnedDirection;
                pReplayCar->byGearAyMax = (replayData.byMiscCarData & 7) - 2;
                iDamageState = (replayData.byMiscCarData & 8) >> 3;
                iWheelAnimFrame = (replayData.byMiscCarData & 0x70) >> 4;
                pReplayCar->byWheelAnimationFrame = iWheelAnimFrame;
                if ((uint8)iWheelAnimFrame > 4u)
                  pReplayCar->byWheelAnimationFrame = 4;
                pReplayCar->byDamageToggle = (replayData.byMiscCarData & 0x80) >> 7;
                iDamageState_1 = pReplayCar->iDamageState;
                pReplayCar->byDamageIntensity = replayData.byDamageIntensity;
                if (iDamageState_1 != iDamageState && ViewType[0] == pReplayCar->iDriverIdx) {
                  if (replayData.nCurrChunk < 0) {

                    tData *pData = &localdata[0];
                    fBestDistance = 1.0e10f;  // Very large initial value (appears to be a float constant)
                    iTrackSegmentIdx = 0;
                    if ( TRAK_LEN > 0 )
                    {
                      iGroundColorIdx = 0;
                      do
                      {
                        // Calculate relative position (car position + track segment offset)
                        float fRelativeX = pReplayCar->pos.fX + pData->pointAy[3].fX;  // [edx+24h]
                        float fRelativeY = pReplayCar->pos.fY + pData->pointAy[3].fY;  // [edx+28h] 
                        float fRelativeZ = pReplayCar->pos.fZ + pData->pointAy[3].fZ;  // [edx+2Ch]
                        // Calculate dot product with track normal vector
                        float fDotProduct = (pData->pointAy[0].fZ * fRelativeX) +     // [edx+8]
                                               (pData->pointAy[1].fZ * fRelativeY) +     // [edx+14h]
                                               (pData->pointAy[2].fZ * fRelativeZ);      // [edx+20h]
                            
                        // Calculate distance from car to track segment
                        float fDistanceSquared = (fRelativeX * fRelativeX) + 
                                                    (fRelativeY * fRelativeY) + 
                                                    (fRelativeZ * fRelativeZ) - 
                                                    (fDotProduct * fDotProduct);
                        float fDistance = (float)sqrt(fDistanceSquared) - pData->fTrackHalfLength; // [edx+30h]
                        fDistanceBuffer = fDistance;
                        // Check if this is a valid segment and closer than previous best
                        if ( fDistance >= -400.0 || GroundColour[iGroundColorIdx][2] >= 0 )
                        {
                          if ( fDistance < fBestDistance )
                          {
                            fNearestDistance = fDistanceBuffer;
                            pReplayCar->iLastValidChunk = iTrackSegmentIdx;
                            fBestDistance = fNearestDistance;
                          }
                        }
                        ++pData;
                        ++iTrackSegmentIdx; 
                        ++iGroundColorIdx;
                      }
                      while ( iTrackSegmentIdx < TRAK_LEN );
                    }
                    //_EDX = localdata;
                    //fBestDistance = 1.0e10;
                    //iTrackSegmentIdx = 0;
                    //if (TRAK_LEN > 0) {
                    //  iGroundColorIdx = 0;
                    //  do {
                    //    __asm
                    //    {
                    //      fld     dword ptr[esi]; Load Real
                    //      fadd    dword ptr[edx + 24h]; Add Real
                    //      fld     dword ptr[esi + 4]; Load Real
                    //      fadd    dword ptr[edx + 28h]; Add Real
                    //      fld     dword ptr[esi + 8]; Load Real
                    //      fadd    dword ptr[edx + 2Ch]; Add Real
                    //      fld     dword ptr[edx + 8]; Load Real
                    //      fmul    st, st(3); Multiply Real
                    //      fld     dword ptr[edx + 14h]; Load Real
                    //      fmul    st, st(3); Multiply Real
                    //      faddp   st(1), st; Add Real and Pop
                    //      fld     dword ptr[edx + 20h]; Load Real
                    //      fmul    st, st(2); Multiply Real
                    //      faddp   st(1), st; Add Real and Pop
                    //      fld     st(3); Load Real
                    //      fmulp   st(4), st; Multiply Real and Pop
                    //      fld     st(2); Load Real
                    //      fmulp   st(3), st; Multiply Real and Pop
                    //      fxch    st(2); Exchange Registers
                    //      faddp   st(3), st; Add Real and Pop
                    //      fmul    st, st; Multiply Real
                    //      faddp   st(2), st; Add Real and Pop
                    //      fxch    st(1); Exchange Registers
                    //      fsqrt; Square Root
                    //      fsub    dword ptr[edx + 30h]; Subtract Real
                    //      fstp[esp + 134h + var_F4]; Store Real and Pop
                    //      fcomp   replay_c_variable_17; Compare Real and Pop
                    //      fnstsw  ax; Store Status Word(no wait)
                    //    }
                    //    if ((_AX & 0x100) == 0 || GroundColour[iGroundColorIdx][2] >= 0) {
                    //      __asm
                    //      {
                    //        fld[esp + 134h + var_F4]; Load Real
                    //        fcomp[esp + 134h + var_40]; Compare Real and Pop
                    //        fnstsw  ax; Store Status Word(no wait)
                    //      }
                    //      if ((_AX & 0x100) != 0) {
                    //        fNearestDistance = fDistanceBuffer;
                    //        pReplayCar->iLastValidChunk = iTrackSegmentIdx;
                    //        fBestDistance = fNearestDistance;
                    //      }
                    //    }
                    //    ++_EDX;
                    //    ++iTrackSegmentIdx;
                    //    ++iGroundColorIdx;
                    //  } while (iTrackSegmentIdx < TRAK_LEN);
                    //}
                    findnearsection(pReplayCar, &iNearestChunk);
                  }
                  pend_view_init = ViewType[0];
                }
                if (iDamageState != pReplayCar->iDamageState) {
                  for (iCarSpray = 0; iCarSpray < 32; iCarSpray++)
                    CarSpray[pReplayCar->iDriverIdx][iCarSpray].iTimer = 0;
                  //for (iTexIndex = 0; iTexIndex != 352; car_texs_loaded[352 * pReplayCar->iDriverIdx + 12 + iTexIndex] = 0)
                  //  iTexIndex += 11;
                  pReplayCar->byLastDamageToggle = pReplayCar->byDamageToggle;
                }

                // Initialize damage spray effects when damage state changes
                if (pReplayCar->byDamageToggle != pReplayCar->byLastDamageToggle) {
                  iSprayIdx = 0;
                  pCarSpray = CarSpray[pReplayCar->iDriverIdx];
                  iDamageIntensity = pReplayCar->byDamageIntensity;
                  do {
                    if (pCarSpray->iLifeTime <= 0) {
                      iRandomValue = rand();
                      if (GetHighOrderRand(12, iRandomValue) < iDamageIntensity)
                        pCarSpray->iTimer = -1;
                    }
                    ++iSprayIdx;
                    ++pCarSpray;
                  } while (iSprayIdx < 32);
                }
                pReplayCar->iDamageState = iDamageState;
                pReplayCar->byLastDamageToggle = pReplayCar->byDamageToggle;
                pReplayCar->byLap = replayData.byLap;
              }
              ++pReplayCar;
              uiCarOffset += 4;
              ++iReplayCarIndex;
            } while (iReplayCarIndex < numcars);
          }

          // Read stunt ramp timing data from replay file
          ppRampReplay = ramp;
          for (j = 0; j < 8; ++j) {
            fread(&pRampData_1, 2u, 1u, replayfile);
            if (*ppRampReplay)
              (*ppRampReplay)->iTickStartIdx = (uint16)(uintptr_t)pRampData_1;
            ++ppRampReplay;
          }

          // Interpolate engine sound data between replay frames
          if (replayspeed == 256) {
            iFrameDifference = currentreplayframe - lastreplayframe - 1;
            iDelayWriteOld = ((uint8)delaywrite - 1) & 0x1F;
            delaywrite += iFrameDifference;
            iDelayWriteNew = delaywrite & 0x1F;
            enginesounds(ViewType[0]);
            if (iFrameDifference > 0 && numcars > 0) {
              uiDelayOldByteOffset = 28 * iDelayWriteOld;
              uiDelayNewByteOffset = 28 * iDelayWriteNew;
              iNextDelayIndex = ((uint8)iDelayWriteOld + 1) & 0x1F;
              iInterpolationSteps = iFrameDifference + 1;
              uiSoundOffsetCopy = 28 * iDelayWriteNew;
              uiDelayOldOffset = 28 * iDelayWriteOld;
              iCarLoopIndex = 0;
              uiSoundDataOffset = 0;
              iTotalCarDataSize = 4 * numcars;
              do {
                if (!non_competitors[iCarLoopIndex / 4u]) {
                  // Check if engine sound data needs interpolation or reset
                  if (enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iEngineVol == -1
                    || enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iEngineVol == -1) {
                    iNextDelaySlot = iNextDelayIndex;
                    if (iDelayWriteNew != iNextDelayIndex) {
                      uiSoundOffset = uiSoundDataOffset;
                      do {
                        *(int *)((char *)&enginedelay[0].engineSoundData[iNextDelaySlot].iEngineVol + uiSoundOffset) = -1;
                        iNextDelaySlot = ((uint8)iNextDelaySlot + 1) & 0x1F;
                      } while (iNextDelaySlot != iDelayWriteNew);
                    }
                  } else {
                    iDelayIdxForEngine = iDelayWriteOld;
                    iEngineVol = enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iEngineVol;
                    iEngineVolIncrement = enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iEngineVol;
                    iEngineVolStep = iEngineVolIncrement;
                    iEngineStepCounter = 1;
                    iEngineVolBase = iEngineVol;
                    uiEngineCalcOffset = uiSoundDataOffset;
                    iEngineVolDelta = iEngineVol * -iFrameDifference;
                    do {
                      iDelayIdxForEngine = ((uint8)iDelayIdxForEngine + 1) & 0x1F;
                      iConversionBuffer = 28 * iDelayIdxForEngine;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iDelayIdxForEngine].iEngineVol + uiEngineCalcOffset) = (iEngineVolStep - iEngineVolDelta)
                        / iInterpolationSteps;
                      ++iEngineStepCounter;
                      iEngineVolStep += iEngineVolIncrement;
                      iEngineVolDelta += iEngineVolBase;
                    } while (iEngineStepCounter <= iFrameDifference);
                    iEnginePitchOld = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C].iEnginePitch + uiEngineCalcOffset);
                    iEnginePitchNew = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayNewByteOffset / 0x1C].iEnginePitch + uiEngineCalcOffset);
                    iEnginePitchDelayIdx = iDelayWriteOld;
                    iEnginePitchStepCount = 1;
                    uiEnginePitchOffset = uiSoundDataOffset;
                    iEnginePitchBase = iEnginePitchOld;
                    iEnginePitchStep = iEnginePitchNew;
                    iEnginePitchDelta = iEnginePitchOld * -iFrameDifference;
                    do {
                      iEnginePitchDelayIdx = ((uint8)iEnginePitchDelayIdx + 1) & 0x1F;
                      iConversionBuffer = 28 * iEnginePitchDelayIdx;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iEnginePitchDelayIdx].iEnginePitch + uiEnginePitchOffset) = (iEnginePitchNew - iEnginePitchDelta)
                        / iInterpolationSteps;
                      iEnginePitchNew += iEnginePitchStep;
                      iEnginePitchDelta += iEnginePitchBase;
                      ++iEnginePitchStepCount;
                    } while (iFrameDifference >= iEnginePitchStepCount);
                  }

                  // Begin engine2 sound interpolation (secondary engine channel)
                  if (enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iEngine2Vol == -1
                    || enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iEngine2Vol == -1) {
                    iEngine2DelaySlot = iNextDelayIndex;
                    if (iDelayWriteNew != iNextDelayIndex) {
                      uiEngine2SoundOffset = uiSoundDataOffset;
                      do {
                        *(int *)((char *)&enginedelay[0].engineSoundData[iEngine2DelaySlot].iEngine2Vol + uiEngine2SoundOffset) = -1;
                        iEngine2DelaySlot = ((uint8)iEngine2DelaySlot + 1) & 0x1F;
                      } while (iEngine2DelaySlot != iDelayWriteNew);
                    }
                  } else {
                    iEngine2StepIdx = iDelayWriteOld;
                    //LOBYTE(iEngine2StepIdx) = iDelayWriteOld;
                    iEngine2Vol = enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iEngine2Vol;
                    iEngine2VolCurrent = enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iEngine2Vol;
                    iEngine2LoopStep = 1;
                    uiEngine2CalcOffset = uiSoundDataOffset;
                    iEngine2VolBase = iEngine2Vol;
                    iEngine2VolIncrement = iEngine2VolCurrent;
                    iEngine2VolDelta = iEngine2Vol * -iFrameDifference;
                    do {
                      iEngine2StepIdx = ((uint8)iEngine2StepIdx + 1) & 0x1F;
                      iConversionBuffer = 28 * iEngine2StepIdx;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iEngine2StepIdx].iEngine2Vol + uiEngine2CalcOffset) = (iEngine2VolCurrent - iEngine2VolDelta)
                        / iInterpolationSteps;
                      iEngine2VolCurrent += iEngine2VolIncrement;
                      iEngine2VolDelta += iEngine2VolBase;
                      ++iEngine2LoopStep;
                    } while (iFrameDifference >= iEngine2LoopStep);
                    iEngine2PitchOld = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C].iEngine2Pitch + uiEngine2CalcOffset);
                    iEngine2PitchNew = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayNewByteOffset / 0x1C].iEngine2Pitch + uiEngine2CalcOffset);
                    iEngine2PitchStep = 1;
                    iEngine2PitchDelayIdx = iDelayWriteOld;
                    uiEngine2PitchOffset = uiSoundDataOffset;
                    iEngine2PitchIncrement = iEngine2PitchNew;
                    iEngine2PitchBase = iEngine2PitchOld;
                    iEngine2PitchWorking = iEngine2PitchNew;
                    iEngine2PitchDelta = iEngine2PitchOld * -iFrameDifference;
                    do {
                      iEngine2PitchDelayIdx = ((uint8)iEngine2PitchDelayIdx + 1) & 0x1F;
                      iConversionBuffer = 28 * iEngine2PitchDelayIdx;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iEngine2PitchDelayIdx].iEngine2Pitch + uiEngine2PitchOffset) = (iEngine2PitchWorking - iEngine2PitchDelta)
                        / iInterpolationSteps;
                      ++iEngine2PitchStep;
                      iEngine2PitchWorking += iEngine2PitchIncrement;
                      iEngine2PitchDelta += iEngine2PitchBase;
                    } while (iEngine2PitchStep <= iFrameDifference);
                  }

                  // Begin skid sound interpolation (tire screech effects)
                  if (enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iSkid1Vol == -1
                    || enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iSkid1Vol == -1) {
                    iSkidDelaySlot = iNextDelayIndex;
                    if (iDelayWriteNew != iNextDelayIndex) {
                      uiSkidSoundOffset = uiSoundDataOffset;
                      do {
                        *(int *)((char *)&enginedelay[0].engineSoundData[iSkidDelaySlot].iSkid1Vol + uiSkidSoundOffset) = -1;
                        iSkidDelaySlot = ((uint8)iSkidDelaySlot + 1) & 0x1F;
                      } while (iSkidDelaySlot != iDelayWriteNew);
                    }
                  } else {
                    iSkidVolDelayIdx = iDelayWriteOld;
                    iSkid1Vol = enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C + uiSoundDataOffset / 0x1C].iSkid1Vol;
                    iSkidVolCurrent = enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iSkid1Vol;
                    iSkidStepCount = 1;
                    uiSkidCalcOffset = uiSoundDataOffset;
                    iSkidVolBase = iSkid1Vol;
                    iSkidVolIncrement = iSkidVolCurrent;
                    iSkidVolDelta = iSkid1Vol * -iFrameDifference;
                    do {
                      iSkidVolDelayIdx = ((uint8)iSkidVolDelayIdx + 1) & 0x1F;
                      iConversionBuffer = 28 * iSkidVolDelayIdx;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iSkidVolDelayIdx].iSkid1Vol + uiSkidCalcOffset) = (iSkidVolCurrent - iSkidVolDelta)
                        / iInterpolationSteps;
                      iSkidVolCurrent += iSkidVolIncrement;
                      iSkidVolDelta += iSkidVolBase;
                      ++iSkidStepCount;
                    } while (iFrameDifference >= iSkidStepCount);
                    iSkidPitchOld = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C].iSkid1Pitch + uiSkidCalcOffset);
                    iSkidPitchNew = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayNewByteOffset / 0x1C].iSkid1Pitch + uiSkidCalcOffset);
                    iSkidPitchStep = 1;
                    iSkidPitchDelayIdx = iDelayWriteOld;
                    uiSkidPitchOffset = uiSoundDataOffset;
                    iSkidPitchIncrement = iSkidPitchNew;
                    iSkidPitchBase = iSkidPitchOld;
                    iSkidPitchWorking = iSkidPitchNew;
                    iSkidPitchDelta = iSkidPitchOld * -iFrameDifference;
                    do {
                      iSkidPitchDelayIdx = ((uint8)iSkidPitchDelayIdx + 1) & 0x1F;
                      iConversionBuffer = 28 * iSkidPitchDelayIdx;
                      *(int *)((char *)&enginedelay[0].engineSoundData[iSkidPitchDelayIdx].iSkid1Pitch + uiSkidPitchOffset) = (iSkidPitchWorking - iSkidPitchDelta)
                        / iInterpolationSteps;
                      ++iSkidPitchStep;
                      iSkidPitchWorking += iSkidPitchIncrement;
                      iSkidPitchDelta += iSkidPitchBase;
                    } while (iSkidPitchStep <= iFrameDifference);
                  }
                  iPanStepIdx = iDelayWriteOld;
                  //LOBYTE(iPanStepIdx) = iDelayWriteOld;
                  iPan = enginedelay[0].engineSoundData[uiDelayOldOffset / 0x1C].iPan;
                  iPanIncrement = enginedelay[0].engineSoundData[uiSoundOffsetCopy / 0x1C].iPan;
                  iPanStep = iPanIncrement;
                  iPanBase = iPan;
                  iPanStepCount = 1;
                  uiPanCalcOffset = uiSoundDataOffset;
                  iPanDelta = iPan * -iFrameDifference;
                  do {
                    iPanStepIdx = ((uint8)iPanStepIdx + 1) & 0x1F;
                    iConversionBuffer = 28 * iPanStepIdx;
                    *(int *)((char *)&enginedelay[0].engineSoundData[iPanStepIdx].iPan + uiPanCalcOffset) = (iPanStep - iPanDelta) / iInterpolationSteps;
                    iPanStep += iPanIncrement;
                    iPanDelta += iPanBase;
                    ++iPanStepCount;
                  } while (iFrameDifference >= iPanStepCount);
                  iPanOld = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayOldByteOffset / 0x1C].iPan + uiPanCalcOffset);
                  iPanNew = *(int *)((char *)&enginedelay[0].engineSoundData[uiDelayNewByteOffset / 0x1C].iPan + uiPanCalcOffset);
                  iPanStepCounter = iDelayWriteOld;
                  //LOBYTE(iPanStepCounter) = iDelayWriteOld;
                  iPanLoopStep = 1;
                  uiPanSoundOffset = uiSoundDataOffset;
                  iPanOldCopy = iPanOld;
                  iPanNewCopy = iPanNew;
                  iPanDeltaStep = iPanOld * -iFrameDifference;
                  do {
                    iPanStepCounter = ((uint8)iPanStepCounter + 1) & 0x1F;
                    iConversionBuffer = 28 * iPanStepCounter;
                    *(int *)((char *)&enginedelay[0].engineSoundData[iPanStepCounter].iPan + uiPanSoundOffset) = (iPanNew - iPanDeltaStep) / iInterpolationSteps;
                    iPanNew += iPanNewCopy;
                    iPanDeltaStep += iPanOldCopy;
                    ++iPanLoopStep;
                  } while (iFrameDifference >= iPanLoopStep);
                }
                iCarLoopIndex += 4;
                uiSoundOffsetCopy += 896;
                uiSoundDataOffset += 896;
                uiDelayOldOffset += 896;
              } while (iCarLoopIndex < iTotalCarDataSize);
            }
          }

          // Handle automatic camera cuts during replay
          if (!rewinding && !forwarding && replayspeed) {
            iCurrentCut = -1;
            if (cuts) {
              iCutIndex = 0;
              if (cuts > 0) {
                iCutArrayIndex = 0;
                do {
                  if (currentreplayframe >= camera[iCutArrayIndex].iFrame)
                    iCurrentCut = iCutIndex;
                  ++iCutIndex;
                  ++iCutArrayIndex;
                } while (iCutIndex < cuts);
              }
            }
            if (iCurrentCut != -1
              && iCurrentCut != lastautocut
              && (camera[iCurrentCut].byView != SelectedView[0] || camera[iCurrentCut].byCarIdx != ViewType[0])
              && autoswitch) {
              SelectedView[0] = camera[iCurrentCut].byView;
              ViewType[0] = camera[iCurrentCut].byCarIdx;
              select_view(0);
              lastautocut = iCurrentCut;
              pend_view_init = ViewType[0];
            }
          }
          if (pend_view_init != -1) {
            if (Play_View == 1) {
              ViewType[0] = pend_view_init;
              doteaminit();
            } else {
              initcarview(pend_view_init, 0);
              ViewType[0] = pend_view_init;
            }
            pend_view_init = -1;
          }
          if (lastreplayframe != -1)
            doviewtend(&Car[ViewType[0]], abs(lastreplayframe - currentreplayframe), 0);
          iUpdateSteps = abs(currentreplayframe - lastreplayframe);
          if (iUpdateSteps > 16)
            iUpdateSteps = 16;

          // Update smoke and flame effects for all cars
          iSmokeCarIndex = 0;
          if (numcars > 0) {
            pSmokeCar = Car;
            do {
              for (iStepCounter = 0; iStepCounter < iUpdateSteps; ++iStepCounter)
                updatesmokeandflames(pSmokeCar);
              ++iSmokeCarIndex;
              ++pSmokeCar;
            } while (iSmokeCarIndex < numcars);
          }

          // Clean up car textures when rewinding replay (dead cars)
          if (replayspeed < 0) {
            iTexCleanupIdx = 0;
            if (numcars > 0) {
              iTexOffsetStep = 1408;
              iCarTexIdx = 0;
              do {
                if (Car[iCarTexIdx].nDeathTimer > 126) {
                  uiTexArrayOffset = 1408 * iTexCleanupIdx;
                  do {
                    uiTexArrayOffset += 44;
                    car_texs_loaded[uiTexArrayOffset / 4 + 12] = 0;
                  } while (uiTexArrayOffset != iTexOffsetStep);
                }
                ++iCarTexIdx;
                ++iTexCleanupIdx;
                iTexOffsetStep += 1408;
              } while (iTexCleanupIdx < numcars);
            }
          }
          lastreplayframe = currentreplayframe;
        }
        if (bReadDisabled || forwarding || rewinding)
          memcpy(repsample, newrepsample, sizeof(repsample));
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00065BF0
void Rplay()
{
  if (replaytype == 2) {
    // Play sound effect sample 83 at 50% volume (0x8000)
    // Volume 0x8000 = 32768, which is 50 % of full 16-bit volume
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);

    // Init replay state
    replaydirection = 1;
    lastautocut = -1;

    // Set replay speed
    replayspeed = replayspeeds[replaysetspeed];

    // copy newrepsample into repsample
    memcpy(repsample, newrepsample, sizeof(repsample));

    // Initialize sound timing if playing at normal speed (0x100 is 1.0 in 8.8 fixed float)
    if (replayspeed == 0x100)
      initsoundlag(currentreplayframe);
  }
}

//-------------------------------------------------------------------------------------------------
//00065C70
void Rreverseplay()
{
  int iCurrSpeed; // eax

  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    iCurrSpeed = replayspeeds[replaysetspeed];
    replaydirection = -1;
    lastautocut = -1;
    replayspeed = -iCurrSpeed;
  }
}

//-------------------------------------------------------------------------------------------------
//00065CC0
void Rframeplus()
{
  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    //_disable();
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    if (replayframes - 1 < ++ticks)
      ticks = replayframes - 1;
    //_enable();
  }
}

//-------------------------------------------------------------------------------------------------
//00065D20
void Rframeminus()
{
  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    //_disable();
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    if (--ticks < 0)
      ticks = 0;
    //_enable();
  }
}

//-------------------------------------------------------------------------------------------------
//00065D70
void Rspeedplus()
{
  int iNewSpeed; // eax

  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    if (++replaysetspeed == 9)
      replaysetspeed = 8;
    if (replayspeed) {
      if (replayspeed >= 0) {
        iNewSpeed = replayspeeds[replaysetspeed];
      } else {
        replayspeed = replayspeeds[replaysetspeed];
        iNewSpeed = -replayspeed;
      }
      replayspeed = iNewSpeed;
    }
    if (replayspeed == 256)
      initsoundlag(currentreplayframe);
  }
}

//-------------------------------------------------------------------------------------------------
//00065E00
void Rspeedminus()
{
  int iNewSpeed; // eax

  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    if (--replaysetspeed < 0)
      replaysetspeed = 0;
    if (replayspeed) {
      if (replayspeed >= 0) {
        iNewSpeed = replayspeeds[replaysetspeed];
      } else {
        replayspeed = replayspeeds[replaysetspeed];
        iNewSpeed = -replayspeed;
      }
      replayspeed = iNewSpeed;
    }
    if (replayspeed == 256)
      initsoundlag(currentreplayframe);
  }
}

//-------------------------------------------------------------------------------------------------
//00065E90
void DoRstop()
{
  if (replaytype == 2) {
    //_disable();
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    ticks = currentreplayframe;
    //_enable();
  }
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00065ED0
void Rstop()
{
  if (replaytype == 2) {
    //_disable();
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    ticks = currentreplayframe;
    //_enable();
  }
}

//-------------------------------------------------------------------------------------------------
//00065F00
void Rrewindstart()
{
  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    if (replayspeed >= 0)
      replayspeed = -64;
    rewinding = -1;
  }
}

//-------------------------------------------------------------------------------------------------
//00065F40
void Rforwardstart()
{
  if (replaytype == 2) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    if (replayspeed <= 0)
      replayspeed = 64;
    forwarding = -1;
  }
}

//-------------------------------------------------------------------------------------------------
//00065F80
void ROldStatus()
{
  if (replaytype == 2) {
    // Calculate effective replay speed (direction * base speed)
    replayspeed = replaydirection * replayspeeds[replaysetspeed];

    //cli(); //disable interrupts
    rewinding = 0;
    forwarding = 0;

    // Synchronize timing system with replay position
    ticks = currentreplayframe;
    fraction = 0;

    //if playing at normal speed (0x100 is 1.0 in 8.8 fixed float)
    if (replayspeed == 0x100)
      Rplay(); //Initialize replay playback
    //sti() //enable interrupts
  }
}

//-------------------------------------------------------------------------------------------------
//00065FE0
void Rstart()
{
  if (ticks) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      replaydirection = 0;
      fraction = 0;
      ticks = 0;
      //_enable();
      pend_view_init = ViewType[0];
      sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
      resetsmoke();
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00066040
void Rend()
{
  if (replayframes - 1 != ticks && replaytype == 2) {
    //_disable();
    replayspeed = 0;
    replaydirection = 0;
    fraction = 0;
    ticks = replayframes - 1;
    //_enable();
    pend_view_init = ViewType[0];
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    resetsmoke();
  }
}

//-------------------------------------------------------------------------------------------------
//000660A0
unsigned int readdisable(int iFrame)
{
  char byBitPosition; // bl
  int iArrayIndex; // eax

  byBitPosition = iFrame;
  if (!replayedit || iFrame > 0x1FFFF)
    return 0;
  iArrayIndex = iFrame / 32;  // Calculate array index: divide by 32 to get DWORD index
  //iArrayIndex = (iFrame - (__CFSHL__(iFrame >> 31, 5) + 32 * (iFrame >> 31))) >> 5;
  return ((unsigned int)disabled[iArrayIndex] >> (byBitPosition - 32 * iArrayIndex)) & 1;
}

//-------------------------------------------------------------------------------------------------
//000660F0
void cleardisable(int iFrame)
{
  char byBitPosition; // bl
  int iArrayIndex; // eax

  byBitPosition = iFrame;                 // Store lower 8 bits of replay index for bit position calculation
  if (iFrame < 0x20000)                 // Check if replay index is within valid range (0-131071)
  {
    iArrayIndex = iFrame / 32;  // Calculate array index: divide by 32 to get DWORD index
    //iArrayIndex = (iFrame - (__CFSHL__(iFrame >> 31, 5) + 32 * (iFrame >> 31))) >> 5;// Calculate array index: divide by 32 to get DWORD index
    disabled[iArrayIndex] &= ~(1 << (byBitPosition - 32 * iArrayIndex));// Clear the specific bit using AND with inverted bit mask
  }
}

//-------------------------------------------------------------------------------------------------
//00066130
void setdisable(int iFrame)
{
  char byBitPosition; // bl
  int iArrayIndex; // eax

  byBitPosition = iFrame;
  if (iFrame < 0x20000) {
    iArrayIndex = iFrame / 32;  // Calculate array index: divide by 32 to get DWORD index
    //iArrayIndex = (iFrame - (__CFSHL__(iFrame >> 31, 5) + 32 * (iFrame >> 31))) >> 5;
    disabled[iArrayIndex] |= 1 << (byBitPosition - 32 * iArrayIndex);
  }
}

//-------------------------------------------------------------------------------------------------
//00066170
void deleteframes(int iStartFrame, int iEndFrame)
{
  int iEndFrameFixed; // esi
  int iTemp; // ebx
  int iCurrentFrame; // ebx
  int iArrayIndex; // eax

  iEndFrameFixed = iEndFrame;
  if (replaytype == 2)                        // Check if we're in replay mode (type 2)
  {                                             // Ensure start frame <= end frame by swapping if necessary
    if (iEndFrame < iStartFrame) {
      iTemp = iStartFrame;
      iStartFrame = iEndFrame;
      iEndFrameFixed = iTemp;
    }
    for (iCurrentFrame = iStartFrame; iCurrentFrame <= iEndFrameFixed; ++iCurrentFrame)// Loop through all frames in the range to mark as disabled
    {                                           // Check frame is within valid range (< 128K frames max)
      if (iCurrentFrame < 0x20000) {
        iArrayIndex = iCurrentFrame / 32;  // Calculate array index: divide by 32 to get DWORD index
        //iArrayIndex = (iCurrentFrame - (__CFSHL__(iCurrentFrame >> 31, 5) + 32 * (iCurrentFrame >> 31))) >> 5;// Calculate array index: iCurrentFrame / 32
        disabled[iArrayIndex] |= 1 << (iCurrentFrame - 32 * iArrayIndex);// Set bit in disabled[] bitfield to mark frame as disabled
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000661E0
void undeleteframes(int iStartFrame, int iEndFrame)
{
  int iEndFrameFixed; // ebx
  int iTemp; // edx
  int iCurrentFrame; // edx
  int iFrameToUndelete; // eax

  iEndFrameFixed = iEndFrame;
  if (replaytype == 2)                        // Check if we're in replay mode (type 2)
  {                                             // Ensure start frame <= end frame by swapping if necessary
    if (iEndFrame < iStartFrame) {
      iTemp = iStartFrame;
      iStartFrame = iEndFrameFixed;
      iEndFrameFixed = iTemp;
    }
    for (iCurrentFrame = iStartFrame; iCurrentFrame <= iEndFrameFixed; ++iCurrentFrame)// Loop through all frames in the range to undelete them
    {
      iFrameToUndelete = iCurrentFrame;
      cleardisable(iFrameToUndelete);           // Clear the disabled bit for this frame
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00066210
void findnextvalid()
{
  int iOriginalFrame; // ebx
  int iTestFrame; // edx
  unsigned int uiDisabledStatus; // eax
  int iBackwardFrame; // edx
  unsigned int uiBackwardDisabled; // eax

  iOriginalFrame = currentreplayframe;          // Store original frame position for potential fallback
  for (iTestFrame = currentreplayframe + 1; currentreplayframe + 1 < replayframes; iTestFrame = currentreplayframe + 1)// Search forward for next valid (non-disabled) frame
  {
    currentreplayframe = iTestFrame;
    uiDisabledStatus = readdisable(iTestFrame); // Check if current frame is disabled
    iTestFrame = currentreplayframe;
    if (!uiDisabledStatus)
      break;                                    // If frame is enabled (not disabled), we found our target
  }
  if (iTestFrame == replayframes)             // If reached end without finding valid frame, search backward
  {
    iBackwardFrame = iOriginalFrame - 1;
    if (iOriginalFrame - 1 >= 0)              // Search backward from original position for valid frame
    {
      do {
        currentreplayframe = iBackwardFrame;
        uiBackwardDisabled = readdisable(iBackwardFrame);// Check if backward frame is disabled
        iBackwardFrame = currentreplayframe;
        if (!uiBackwardDisabled)
          break;
        iBackwardFrame = currentreplayframe - 1;
      } while (currentreplayframe - 1 >= 0);
    }
    if (iBackwardFrame < 0)                   // If no valid frame found, fallback to original position
      iBackwardFrame = iOriginalFrame;
    if (replaytype == 2)                      // If in replay mode, stop playback and reset state
    {
      currentreplayframe = iBackwardFrame;      // Disable interrupts for atomic update
      //_disable();
      replayspeed = 0;                          // Stop replay playback
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
      iBackwardFrame = currentreplayframe;
    }
  } else {
    currentreplayframe = iTestFrame;
    //_disable();                                 // Found valid frame forward - update timing atomically
    ticks = currentreplayframe;
    fraction = 0;
    //_enable();
    iBackwardFrame = currentreplayframe;
    if (iOriginalFrame != currentreplayframe) // If frame changed, trigger view refresh
      pend_view_init = ViewType[0];
  }
  currentreplayframe = iBackwardFrame;
}

//-------------------------------------------------------------------------------------------------
//00066300
void findlastvalid()
{
  int iOriginalFrame; // ebx
  int iTestFrame; // edx
  unsigned int uiDisabledStatus; // eax
  int iForwardFrame; // edx
  unsigned int uiForwardDisabled; // eax

  iOriginalFrame = currentreplayframe;          // Store original frame position for potential fallback
  for (iTestFrame = currentreplayframe - 1; currentreplayframe - 1 >= 0; iTestFrame = currentreplayframe - 1)// Search backward for previous valid (non-disabled) frame
  {
    currentreplayframe = iTestFrame;
    uiDisabledStatus = readdisable(iTestFrame); // Check if current frame is disabled
    iTestFrame = currentreplayframe;
    if (!uiDisabledStatus)
      break;                                    // If frame is enabled (not disabled), we found our target
  }
  if (iTestFrame >= 0)                        // If found valid frame backward, use it
  {
    currentreplayframe = iTestFrame;
    //_disable();                                 // Found valid frame backward - update timing atomically
    ticks = currentreplayframe;
    fraction = 0;
    //_enable();
    iForwardFrame = currentreplayframe;
    if (iOriginalFrame != currentreplayframe) // If frame changed, trigger view refresh
      pend_view_init = ViewType[0];
  } else {
    iForwardFrame = iOriginalFrame + 1;         // No valid frame backward, search forward from original position
    if (iOriginalFrame + 1 < replayframes)    // Search forward from original position for valid frame
    {
      do {
        currentreplayframe = iForwardFrame;
        uiForwardDisabled = readdisable(iForwardFrame);// Check if forward frame is disabled
        iForwardFrame = currentreplayframe;
        if (!uiForwardDisabled)
          break;
        iForwardFrame = currentreplayframe + 1;
      } while (currentreplayframe + 1 < replayframes);
    }
    if (iForwardFrame == replayframes)        // If no valid frame found, fallback to original position
      iForwardFrame = iOriginalFrame;
    if (replaytype == 2)                      // If in replay mode, stop playback and reset state
    {
      currentreplayframe = iForwardFrame;
      //_disable();                               // Disable interrupts for atomic update
      replayspeed = 0;                          // Stop replay playback
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
      iForwardFrame = currentreplayframe;
    }
  }
  currentreplayframe = iForwardFrame;
}

//-------------------------------------------------------------------------------------------------
//000663F0
void Rassemble()
{
  int iCarIndex; // eax
  int iNumCars; // ebp
  int iCarCounter; // edx
  char *szSourcePtr; // esi
  char *szDestPtr; // edi
  char byChar1; // al
  char byChar2; // al
  char *szExtSourcePtr; // esi
  char *szExtDestPtr; // edi
  char byExtChar1; // al
  char byExtChar2; // al
  FILE *pOutputFile; // esi
  int iErrorCode1; // eax
  int iFrame; // ebp
  int iValidFrames; // esi
  int iCutIndex; // edi
  int i; // edx
  int iSeekOffset; // edx
  int iErrorCode2; // eax
  tReplayCamera *pCamera; // eax
  FILE *pWriteFile; // ecx
  int iErrorCode3; // eax
  int iCutsTotal; // edx
  tReplayCamera *pNextCamera; // ebx
  int iCutCounter; // ecx
  int iPaddingCount; // ebp
  int j; // edi
  int iErrorCode4; // eax
  int iErrorCode5; // eax
  uint8 *szDataBuffer; // edi
  unsigned int uiBytesRead; // eax
  uint8 *szCarDataPtr; // ebx
  int iCarLoop1; // edx
  int iCarIndex1; // ecx
  uint8 *szCarDataPtr2; // ebx
  int iCarLoop2; // edx
  int iCarIndex2; // ecx
  char byCarData; // al
  uint8 byDirection; // ah
  uint8 byProcessedData; // al
  int iCarCount; // esi
  int iErrorCode6; // eax
  int iHasData; // esi
  int aiCarDirectionFlags[32]; // [esp+0h] [ebp-134h]
  char szFilename[32]; // [esp+80h] [ebp-B4h] BYREF
  uint8 carVelocityData[32]; // [esp+A0h] [ebp-94h]
  uint8 emptyCameraData[16]; // [esp+C0h] [ebp-74h] BYREF
  int iCutsWritten; // [esp+D0h] [ebp-64h]
  int iLastError; // [esp+D4h] [ebp-60h]
  int iErrorFlag; // [esp+D8h] [ebp-5Ch]
  int iTempData; // [esp+DCh] [ebp-58h]
  int iFrameBackup; // [esp+E0h] [ebp-54h]
  unsigned int uiSize; // [esp+E4h] [ebp-50h]
  void *pData; // [esp+E8h] [ebp-4Ch]
  tReplayCamera *pCameraPtr; // [esp+ECh] [ebp-48h]
  int iOffset; // [esp+F0h] [ebp-44h]
  int iPrevValidFrame; // [esp+F4h] [ebp-40h]
  int iCutProcessed; // [esp+F8h] [ebp-3Ch]
  int iDataWritten; // [esp+FCh] [ebp-38h]
  signed int k; // [esp+100h] [ebp-34h]
  FILE *fp; // [esp+104h] [ebp-30h]
  int iInitFlag; // [esp+108h] [ebp-2Ch]
  int iFrameCounter; // [esp+10Ch] [ebp-28h]
  signed int iBlockCounter; // [esp+110h] [ebp-24h]
  uint8 tempBuffer[32]; // [esp+114h] [ebp-20h] BYREF

  iDataWritten = 0;
  iErrorFlag = 0;
  if (replaytype == 2)                        // Only process if replay type is 2 (assembly mode)
  {
    memset(emptyCameraData, 0, sizeof(emptyCameraData));// Initialize empty camera data buffer and setup car processing arrays
    pData = scrbuf;
    iCarIndex = 0;
    if (numcars > 0)                          // Initialize car data arrays for all cars in replay
    {
      iNumCars = numcars;
      iCarCounter = 0;
      do {
        ++iCarCounter;
        carVelocityData[iCarIndex++] = 1;
        aiCarDirectionFlags[iCarCounter + 15] = 1;
      } while (iCarIndex < iNumCars);
    }
    //_disable();                                 // Disable interrupts and reset replay state for assembly
    iInitFlag = -1;
    replayspeed = 0;
    replaydirection = 0;
    fraction = 0;
    ticks = currentreplayframe;
    //_enable();
    strcpy(szFilename, "..\\REPLAYS\\");        // Build output filename by concatenating path, selected name, and .GSS extension
    szSourcePtr = selectfilename;
    szDestPtr = &szFilename[strlen(szFilename)];
    do {
      byChar1 = *szSourcePtr;
      *szDestPtr = *szSourcePtr;
      if (!byChar1)
        break;
      byChar2 = szSourcePtr[1];
      szSourcePtr += 2;
      szDestPtr[1] = byChar2;
      szDestPtr += 2;
    } while (byChar2);
    szExtSourcePtr = g_szGss;
    szExtDestPtr = &szFilename[strlen(szFilename)];
    do {
      byExtChar1 = *szExtSourcePtr;
      *szExtDestPtr = *szExtSourcePtr;
      if (!byExtChar1)
        break;
      byExtChar2 = szExtSourcePtr[1];
      szExtSourcePtr += 2;
      szExtDestPtr[1] = byExtChar2;
      szExtDestPtr += 2;
    } while (byExtChar2);
    if (!strcmp(szFilename, replayfilename))  // Check if trying to overwrite current replay file
    {
      lastfile = 0;
      screenready = 0;
      filingmenu = 9;
    } else {
      pOutputFile = ROLLERfopen(szFilename, "wb");    // Open output file and copy replay header from source file
      fp = pOutputFile;
      if (pOutputFile) {
        fseek(replayfile, 0, 0);
        fread(pData, 1u, replayheader, replayfile);
        if (fwrite(pData, 1, replayheader, pOutputFile) != replayheader) {
          iErrorCode1 = errno;
          iErrorFlag = -1;
          iLastError = iErrorCode1;
        }
        iFrame = 0;                             // Initialize frame processing and camera cut handling
        iPrevValidFrame = -1;
        iValidFrames = 0;
        uiSize = 64000 / replayblock;
        iCutsWritten = 0;
        fseek(fp, 4, 0);
        iCutProcessed = 0;
        if (cuts > 0)                         // Process each camera cut, counting valid frames and writing camera data
        {
          pCameraPtr = camera;
          iOffset = -4;
          iCutIndex = 0;
          do {                                     // Count valid frames up to current camera cut
            for (i = iFrame; i < camera[iCutIndex].iFrame; ++i) {
              if (!readdisable(i))
                ++iValidFrames;
            }
            iFrame = camera[iCutIndex].iFrame;
            iFrameBackup = iFrame;
            if (iPrevValidFrame == -1 || iValidFrames != iPrevValidFrame)// Write camera cut data - either new entry or update existing duplicate frame
            {
              pCamera = pCameraPtr;
              pWriteFile = fp;
              camera[iCutIndex].iFrame = iValidFrames;
              if (fwrite(pCamera, 1, 6, pWriteFile) != 6) {
                iErrorCode3 = errno;
                iErrorFlag = -1;
                iLastError = iErrorCode3;
              }
              iPrevValidFrame = iValidFrames;
              iOffset += 6;
              ++iCutsWritten;
            } else {
              iSeekOffset = iOffset;
              camera[iCutIndex].iFrame = iPrevValidFrame;
              fseek(fp, iSeekOffset, 0);
              if (fwrite(&camera[iCutIndex], 1, 6, fp) != 6) {
                iErrorCode2 = errno;
                iErrorFlag = -1;
                iLastError = iErrorCode2;
              }
            }
            iCutsTotal = cuts;
            ++iCutIndex;
            pNextCamera = pCameraPtr + 1;
            iCutCounter = iCutProcessed + 1;
            //TODO ensure this is accurate
            camera[iCutIndex - 1].iFrame = iFrameBackup;
            //*(int *)((char *)&yp_variable_1 + iCutIndex * 6) = iFrameBackup;// reference into adjacent data camera
            pCameraPtr = pNextCamera;
            iCutProcessed = iCutCounter;
          } while (iCutCounter < iCutsTotal);
        }
        iPaddingCount = 100 - iCutsWritten;     // Fill remaining camera slots with empty data (max 100 cuts)
        for (j = 0; j < iPaddingCount; ++j) {
          if (fwrite(emptyCameraData, 1, 6, fp) != 6) {
            iErrorCode4 = errno;
            iErrorFlag = -1;
            iLastError = iErrorCode4;
          }
        }
        tempBuffer[0] = iCutsWritten;           // Write total number of camera cuts to file
        if (fwrite(tempBuffer, 1, 1, fp) != 1) {
          iErrorCode5 = errno;
          iErrorFlag = -1;
          iLastError = iErrorCode5;
        }
        fseek(fp, replayheader, 0);             // Main replay data processing - read blocks and filter/process car data
        iFrameCounter = 0;
        do {
          szDataBuffer = (uint8 *)pData;
          uiBytesRead = (uint32)fread(pData, replayblock, uiSize, replayfile);
          iBlockCounter = 0;
          for (k = uiBytesRead; iBlockCounter < k; ++iBlockCounter) {                                     // Skip disabled frames, process valid frames with car data filtering
            if (readdisable(iFrameCounter)) {
              iInitFlag = -1;
            } else {                                   // First-time setup of car direction tracking arrays
              if (iInitFlag) {
                szCarDataPtr = szDataBuffer;
                iCarLoop1 = 0;
                if (numcars > 0) {
                  iCarIndex1 = 0;
                  do {
                    aiCarDirectionFlags[iCarIndex1] = (((int)szCarDataPtr[26] >> 3) & 1) == aiCarDirectionFlags[iCarIndex1 + 16];
                    iTempData = szCarDataPtr[3];
                    //iTempData = (uint8)HIBYTE(*(_DWORD *)szCarDataPtr);
                    tempBuffer[4] = iTempData;
                    if ((char)iTempData * (char)carVelocityData[iCarLoop1] >= 0)
                      carVelocityData[iCarLoop1 + 16] = 1;
                    else
                      carVelocityData[iCarLoop1 + 16] = -1;
                    szCarDataPtr += 30;
                    ++iCarLoop1;
                    ++iCarIndex1;
                  } while (iCarLoop1 < numcars);
                }
                iInitFlag = 0;
              }
              szCarDataPtr2 = szDataBuffer;     // Process car data - flip direction bits and apply velocity corrections
              iCarLoop2 = 0;
              if (numcars > 0) {
                iCarIndex2 = 0;
                do {
                  szCarDataPtr2[26] ^= 8 * (uint8)(aiCarDirectionFlags[iCarIndex2]);
                  aiCarDirectionFlags[iCarIndex2 + 16] = ((int)szCarDataPtr2[26] >> 3) & 1;
                  iTempData = szCarDataPtr2[3];
                  //iTempData = (uint8)HIBYTE(*(_DWORD *)szCarDataPtr2);
                  byCarData = iTempData;
                  byDirection = carVelocityData[iCarLoop2 + 16];
                  carVelocityData[iCarLoop2] = iTempData;
                  byProcessedData = byDirection * byCarData;
                  szCarDataPtr2 += 30;
                  ++iCarIndex2;
                  carVelocityData[iCarLoop2++] = byProcessedData;
                  iCarCount = numcars;
                  *(int *)(szCarDataPtr2 - 30) = ((char)byProcessedData << 24) | *(int *)(szCarDataPtr2 - 30) & 0xFFFFFF;
                } while (iCarLoop2 < iCarCount);
              }
              iDataWritten = -1;
              if (fwrite(szDataBuffer, 1, replayblock, fp) != replayblock) {
                iErrorCode6 = errno;
                iErrorFlag = -1;
                iLastError = iErrorCode6;
              }
            }
            szDataBuffer += replayblock;
            ++iFrameCounter;
          }
        } while (k == uiSize && !iErrorFlag);
        iHasData = iDataWritten;                // Close file and handle errors - delete incomplete file on failure
        fclose(fp);
        if (!iHasData) {
          errno = -1000;
          iErrorFlag = -1;
        }
        if (iErrorFlag)
          ROLLERremove(szFilename);
      }
      filingmenu = 0;
      lastfile = 0;
      screenready = 0;
    }
  }
  if (iErrorFlag)                             // Set appropriate error dialog based on failure type
  {
    if (iLastError == 12) {
      filingmenu = 6;
    } else if (iLastError == -1000) {
      filingmenu = 8;
    } else {
      filingmenu = 7;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00066AF0
void storecut()
{
  int iNumCuts; // esi
  int iTargetFrame; // edi
  int iInsertIndex; // ecx
  int iCutCounter; // eax
  int iCameraIndex; // edx
  int iStoreIndex; // eax

  iNumCuts = cuts;                              // Initialize working variables
  iTargetFrame = currentreplayframe;
  if (replaytype == 2 && cuts < 100)          // Only allow cut creation in edit mode and under limit (100 cuts max)
  {
    iInsertIndex = -1;                          // Initialize insertion index to -1 (no position found yet)
    if (cuts)                                 // Search for correct insertion position if cuts exist
    {
      iCutCounter = 0;
      if (cuts > 0) {
        iCameraIndex = 0;                       // Find the last cut that occurs before or at current frame
        do {                                       // Check if this cut frame is <= current frame
          if (camera[iCameraIndex].iFrame <= currentreplayframe)
            iInsertIndex = iCutCounter;
          ++iCutCounter;
          ++iCameraIndex;
        } while (iCutCounter < cuts);
      }
      if (camera[iInsertIndex].iFrame != currentreplayframe || iInsertIndex == -1)// Check if cut already exists at this frame
      {                                         // Calculate insertion position (after last matching cut)
        if (++iInsertIndex != cuts) {
          memmove(&camera[iInsertIndex + 1], &camera[iInsertIndex], 6 * (cuts - iInsertIndex));// Shift existing cuts up to make room for new cut
          iNumCuts = cuts;
        }
        iTargetFrame = currentreplayframe;      // Increment cut count for new cut
        ++iNumCuts;
      }
    } else {
      iNumCuts = 1;                             // First cut - set count to 1 and insert at position 0
      iInsertIndex = 0;
    }
    iStoreIndex = iInsertIndex;                 // Store cut data at calculated index
    camera[iStoreIndex].byView = SelectedView[0];// Store current view settings
    camera[iStoreIndex].iFrame = iTargetFrame;  // Store current frame number
    camera[iStoreIndex].byCarIdx = (uint8)ViewType[0]; // Store current car index for view
  }
  currentreplayframe = iTargetFrame;            // Update global state
  cuts = iNumCuts;
}

//-------------------------------------------------------------------------------------------------
//00066BE0
void removecut()
{
  int iNumCuts; // esi
  int iCutIndex; // ecx
  int iCameraIndex; // edi

  iNumCuts = cuts;                              // Get current number of cuts
  if (replaytype == 2)                        // Only allow cut removal in edit mode (replaytype == 2)
  {
    iCutIndex = 0;
    if (cuts > 0)                             // Search through existing cuts to find match
    {
      iCameraIndex = 0;
      do {                                         // Check if current replay frame matches this cut frame
        if (currentreplayframe == camera[iCameraIndex].iFrame) {                                       // If not the last cut, shift remaining cuts down to fill gap
          if (iCutIndex != iNumCuts - 1) {
            cuts = iNumCuts;
            memmove(&camera[iCameraIndex], &camera[iCameraIndex + 1], 6 * (iNumCuts - iCutIndex - 1));// Move remaining cuts down by 6 bytes each (camera struct size)
            iNumCuts = cuts;
          }
          --iNumCuts;                           // Decrement total cut count
        }
        ++iCutIndex;
        ++iCameraIndex;
      } while (iCutIndex < iNumCuts);
    }
  }
  cuts = iNumCuts;                              // Update global cuts count
}

//-------------------------------------------------------------------------------------------------
//00066C50
int readcut()
{
  int iLastValidCut; // ecx
  int iCutIndex; // eax
  int iArrayIndex; // edx

  iLastValidCut = -1;                           // Initialize to -1 (no valid cut found)
  if (cuts)                                   // Check if any camera cuts exist
  {
    iCutIndex = 0;
    if (cuts > 0)                             // Iterate through all camera cuts to find the active one
    {
      iArrayIndex = 0;
      do {                                         // Check if this cut's frame is at or before current replay frame
        if (camera[iArrayIndex].iFrame <= currentreplayframe)
          iLastValidCut = iCutIndex;            // Update to this cut index (keeps the latest valid cut)
        ++iCutIndex;
        ++iArrayIndex;
      } while (iCutIndex < cuts);
    }
  }
  return iLastValidCut;                         // Return index of most recent valid cut, or -1 if none found
}

//-------------------------------------------------------------------------------------------------
//00066CC0
void displayreplay()
{                                               // Apply scanline effect for disabled replay frames (type 2)
  uint8 *pScreenBuffer; // edi
  int iWindowWidth; // esi
  int iHeightLoop; // ecx
  int iWidthLoop; // eax
  uint8 *pPixelPtr; // edx
  int iRowIndex; // ecx
  uint8 *pRowPtr; // eax

  if (replaytype == 2 && readdisable(currentreplayframe)) {
    pScreenBuffer = scrbuf;                     // Get screen buffer and window dimensions for scanline processing
    iWindowWidth = winw;
    for (iHeightLoop = 0; iHeightLoop < winh; iHeightLoop += 2)// First pass: Set every other pixel to black on even rows (scanlines)
    {
      for (iWidthLoop = 0; iWidthLoop < iWindowWidth; pPixelPtr[iWindowWidth * iHeightLoop] = 0) {
        pPixelPtr = &pScreenBuffer[iWidthLoop];
        iWidthLoop += 2;
      }
    }
    iRowIndex = 1;                              // Second pass: Clear entire odd rows to create horizontal scanline effect
    winw = iWindowWidth;
    scrbuf = pScreenBuffer;
    while (iRowIndex < winh) {
      pRowPtr = &scrbuf[winw * iRowIndex];
      iRowIndex += 2;
      memset(pRowPtr, 0, winw);
    }
  }
  if (filingmenu)                             // Handle file menu dialogs for replay management
  {
    switch (filingmenu) {
      case 1:
        fileselect(10, 10, 310, 125, 160, 20, &language_buffer[3136], "..\\REPLAYS\\*.GSS", 1);// Load replay dialog
        break;
      case 2:
        fileselect(10, 10, 310, 140, 160, 20, &language_buffer[3200], "..\\REPLAYS\\*.GSS", 2);// Save replay dialog
        break;
      case 3:
        fileselect(10, 10, 310, 125, 160, 20, &language_buffer[3264], "..\\REPLAYS\\*.GSS", 3);// Delete replay dialog
        break;
      case 4:
        fileselect(10, 10, 310, 140, 160, 20, &language_buffer[3392], "..\\REPLAYS\\*.GSS", 4);// Assemble replay dialog
        break;
      case 5:
        lsd(40, 37, 280, 107);                  // Show LSD (replay statistics) dialog
        break;
      case 6:
        warning(122, 55, 198, 85, &language_buffer[3456]);// Show warning dialogs for various replay operations
        break;
      case 7:
        warning(112, 55, 208, 85, &language_buffer[3520]);
        break;
      case 8:
        warning(82, 55, 238, 85, &language_buffer[3584]);
        break;
      case 9:
        warning(40, 55, 280, 85, &language_buffer[3648]);
        break;
      default:
        break;
    }
  }
  if (replaypanel)                            // Validate replay panel display based on screen mode and size
  {
    if ((!SVGA_ON || scr_size != 128) && (SVGA_ON || scr_size != 64)) {
      replaypanel = 0;
      controlicon = 9;
    }
  }
  if (replaypanel)                            // Display control panel if replay panel is active
    displaycontrolpanel();
}

//-------------------------------------------------------------------------------------------------
//00066F60
int compare(const void *szStr1, const void *szStr2)
{
  return strcmp((const char *)szStr1, (const char *)szStr2);
}

//-------------------------------------------------------------------------------------------------
//00066F70
void warning(int iX1, int iY1, int iX2, int iY2, char *szWarning)
{
  int iX1Scaled; // esi
  int iY1_1; // edi
  tPolyParams poly; // [esp+0h] [ebp-38h] BYREF

  iX1Scaled = iX1;
  iY1_1 = iY1;
  if (SVGA_ON) {
    iX2 *= 2;
    iY1 *= 2;
    iX1Scaled = 2 * iX1;
    iY2 *= 2;
  }
  poly.vertices[0].x = iX2;
  poly.vertices[0].y = iY1;
  poly.vertices[1].x = iX1Scaled;
  poly.vertices[1].y = iY1;
  poly.vertices[2].x = iX1Scaled;
  poly.vertices[2].y = iY2;
  poly.vertices[3].x = iX2;
  poly.vertices[3].y = iY2;
  poly.iSurfaceType = SURFACE_FLAG_TRANSPARENT | 0x3;// 0x200003;
  poly.uiNumVerts = 4;
  POLYFLAT(scrbuf, &poly);
  prt_centrecol(rev_vga[1], szWarning, (iX1Scaled + iX2) / 2, iY1_1 + 10, 231);
  if (fatkbhit()) {
    while (fatkbhit())
      fatgetch();
    filingmenu = 0;
    disciconpressed = 0;
  }
}

//-------------------------------------------------------------------------------------------------
//00067020
void lsd(int iX1, int iY1, int iX2, int iY2)
{
  int iOriginalY1; // esi
  uint8 byKey; // al
  uint8 byExtendedKey; // al
  int iMenuOption; // ebp
  char *pDestBuffer; // edi
  char *pSourceText; // esi
  char byChar1; // al
  char byChar2; // al
  char byTextColor; // [esp-4h] [ebp-40h]
  tPolyParams poly; // [esp+0h] [ebp-3Ch] BYREF
  int iTextY; // [esp+2Ch] [ebp-10h]

  iOriginalY1 = iY1;                            // Save original Y1 coordinate before SVGA scaling
  if (SVGA_ON)                                // Scale coordinates by 2x for SVGA mode
  {
    iX2 *= 2;
    iY1 *= 2;
    iX1 *= 2;
    iY2 *= 2;
  }
  poly.vertices[0].x = iX2;                     // Set up polygon vertices for background rectangle
  poly.vertices[0].y = iY1;
  poly.vertices[1].x = iX1;
  poly.vertices[1].y = iY1;
  poly.vertices[2].x = iX1;
  poly.vertices[2].y = iY2;
  poly.vertices[3].x = iX2;
  poly.vertices[3].y = iY2;
  poly.iSurfaceType = 0x200003;                 // = SURFACE_FLAG_TRANSPARENT | 0x3;
  poly.uiNumVerts = 4;
  POLYFLAT(scrbuf, &poly);
  prt_centrecol(rev_vga[1], &language_buffer[3072], 160, iOriginalY1 + 10, 231);// Display menu title text centered
  while (fatkbhit())                          // Main input loop - process keyboard input
  {
    byKey = fatgetch();                         // Get key press
    if (byKey)                                // Check if regular ASCII key or extended key (0 = extended)
    {                                           // Check for ENTER key (0x0D)
      if (byKey >= 0xDu) {
        if (byKey <= 0xDu) {
          sfxsample(83, 0x8000);                // SOUND_SAMPLE_BUTTON
          filingmenu = lsdsel + 1;
        } else if (byKey == 0x1B)               // Check for ESCAPE key (0x1B)
        {
          filingmenu = 0;                       // ESCAPE: cancel menu and reset state
          disciconpressed = 0;
          rotpoint = currentreplayframe;
        }
      }
    } else {
      byExtendedKey = fatgetch();               // Extended key: get second byte of scancode
      if (byExtendedKey >= 0x48u)             // Check for UP arrow key (0x48)
      {
        if (byExtendedKey <= 0x48u) {                                       // UP arrow: move selection up if not at top
          if (lsdsel)
            --lsdsel;
        } else if (byExtendedKey == 0x50 && lsdsel < 2)// Check for DOWN arrow key (0x50)
        {
          ++lsdsel;                             // DOWN arrow: move selection down if not at bottom
        }
      }
    }
  }
  iMenuOption = 0;                              // Begin menu text rendering loop
  iTextY = iOriginalY1 + 30;                    // Calculate starting Y position for menu text
  do {                                             // Select appropriate menu text based on option index
    if (iMenuOption) {
      if ((unsigned int)iMenuOption <= 1) {
        pDestBuffer = buffer;
        pSourceText = &language_buffer[3200];
      } else {
        if (iMenuOption != 2)
          goto RENDER_MENU_OPTION;
        pDestBuffer = buffer;
        pSourceText = &language_buffer[3264];
      }
    } else {
      pDestBuffer = buffer;
      pSourceText = &language_buffer[3136];
    }
    do {
      byChar1 = *pSourceText;                   // String copy loop: copy 2 bytes at a time until null terminator
      *pDestBuffer = *pSourceText;
      if (!byChar1)
        break;
      byChar2 = pSourceText[1];
      pSourceText += 2;
      pDestBuffer[1] = byChar2;
      pDestBuffer += 2;
    } while (byChar2);
  RENDER_MENU_OPTION:
    if (iMenuOption == lsdsel)                // Set text color based on selection: highlighted vs normal
      byTextColor = 0x8F;                       // Selected item color (0x8F = white)
    else
      byTextColor = 0x83;                       // Normal item color (0x83 = grey)
    prt_centrecol(rev_vga[1], buffer, 160, iTextY, byTextColor);// Draw menu option text centered
    ++iMenuOption;                              // Advance to next menu option and Y position
    iTextY += 10;
  } while (iMenuOption < 3);                    // Continue until all 3 menu options are drawn
}

//-------------------------------------------------------------------------------------------------
//000671D0
void scandirectory(const char *szPattern)
{
  int iFileCount = 0;

#ifdef IS_WINDOWS
  struct _finddata_t fileinfo;
  intptr_t handle = _findfirst(szPattern, &fileinfo);
  if (handle == -1) {
    filefiles = 0;
    return;
  }

  do {
    if (iFileCount >= 500)
      break;

    // Extract filename without extension (stop at first dot)
    const char *szFileNamePtr = fileinfo.name;
    int iCharIndex = 0;
    while (*szFileNamePtr && *szFileNamePtr != '.' && iCharIndex < 8) {
      filename[iFileCount][iCharIndex++] = *szFileNamePtr++;
    }
    filename[iFileCount][iCharIndex] = '\0';

    iFileCount++;
  } while (_findnext(handle, &fileinfo) == 0);

  _findclose(handle);

#else
  // For Unix-like systems, we need to extract directory and pattern from szPattern
  char szDirectory[256];
  char szFilePattern[256];

  // Find the last directory separator
  const char *szLastSlash = strrchr(szPattern, '/');
  const char *szLastBackslash = strrchr(szPattern, '\\');
  const char *szSeparator = (szLastSlash > szLastBackslash) ? szLastSlash : szLastBackslash;

  if (szSeparator) {
    // Extract directory path
    size_t nDirLen = szSeparator - szPattern + 1;
    strncpy(szDirectory, szPattern, nDirLen);
    szDirectory[nDirLen] = '\0';
    // Extract pattern
    strcpy(szFilePattern, szSeparator + 1);
  } else {
    // No directory specified, use current directory
    strcpy(szDirectory, ".");
    strcpy(szFilePattern, szPattern);
  }

  DIR *dir = opendir(szDirectory);
  if (!dir) {
    filefiles = 0;
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && iFileCount < 500) {
    // Check if filename matches pattern (case-insensitive)
    if (fnmatch(szFilePattern, entry->d_name, FNM_CASEFOLD) == 0) {
      // Extract filename without extension (stop at first dot)
      const char *szFileNamePtr = entry->d_name;
      int iCharIndex = 0;
      while (*szFileNamePtr && *szFileNamePtr != '.' && iCharIndex < 8) {
        filename[iFileCount][iCharIndex++] = *szFileNamePtr++;
      }
      filename[iFileCount][iCharIndex] = '\0';

      iFileCount++;
    }
  }

  closedir(dir);
#endif

  // Update global file count
  filefiles = iFileCount;

  // Sort filenames alphabetically using compare function
  if (iFileCount > 0) {
    qsort(filename, iFileCount, 9, compare);
  }
}

//-------------------------------------------------------------------------------------------------
//000672C0
void fileselect(int iBoxX0, int iBoxY0, int iBoxX1, int iBoxY1, int iTextX, int iTextY, const char *szText, const char *szPattern, int iFileIdx)
{
  int iCurrentFile; // ebp
  char *szDestPtr; // edi
  char *szSourcePtr; // esi
  char byChar1; // al
  char byChar2; // al
  char byKeyCode; // al
  char byProcessedKey; // dl
  int iLeftNavFile; // ebp
  char *szLeftDestPtr; // edi
  char *szLeftSourcePtr; // esi
  char byLeftChar1; // al
  char byLeftChar2; // al
  int iRightNavFile; // ebp
  char *szRightDestPtr; // edi
  char *szRightSourcePtr; // esi
  char byRightChar1; // al
  char byRightChar2; // al
  int iUpNavFile; // ebp
  char *szUpDestPtr; // edi
  char *szUpSourcePtr; // esi
  char byUpChar1; // al
  char byUpChar2; // al
  int iDownNavFile; // ebp
  char *szDownDestPtr; // edi
  char *szDownSourcePtr; // esi
  char byDownChar1; // al
  char byDownChar2; // al
  unsigned int uiDelStrLen; // kr08_4
  unsigned int uiBackspaceLen; // kr0C_4
  int iInputStrLen; // ecx
  int iDisplayFile; // ebp
  int iLoopCounter; // edi
  int iDisplayX; // ebx
  int iDisplayY; // ecx
  const char *szDisplayName; // edx
  unsigned int uiCursorLen; // kr14_4
  unsigned int uiCursorPos; // ebx
  int iScreenWidth; // esi
  int iUpArrowX; // ecx
  int iDownArrowX; // ecx
  tPolyParams params; // [esp+0h] [ebp-48h] BYREF
  int iRightEdge; // [esp+2Ch] [ebp-1Ch]
  int iEditMode; // [esp+30h] [ebp-18h]
  int iLeftEdge; // [esp+34h] [ebp-14h]
  int iBottomEdge; // [esp+38h] [ebp-10h]

  iLeftEdge = iBoxX0;                           // Store left edge coordinate in local variable
  if (filingmenu == 2 || filingmenu == 4)     // Check if in save mode (2) or assemble mode (4) to enable text editing
    iEditMode = -1;
  else
    iEditMode = 0;
  if (SVGA_ON)                                // Scale coordinates by 2 if SVGA mode is enabled
  {
    iBoxX1 *= 2;
    iBoxY0 *= 2;
    iBoxY1 *= 2;
    iLeftEdge *= 2;
  }
  params.vertices[0].x = iBoxX1;                // Set up polygon vertices for file selection background rectangle
  params.vertices[0].y = iBoxY0;
  params.vertices[1].y = iBoxY0;
  params.vertices[2].y = iBoxY1;
  params.vertices[3].x = iBoxX1;
  params.vertices[3].y = iBoxY1;
  params.iSurfaceType = 2097155;
  params.uiNumVerts = 4;
  params.vertices[1].x = iLeftEdge;
  params.vertices[2].x = iLeftEdge;
  POLYFLAT(scrbuf, &params);
  prt_centrecol(rev_vga[1], szText, iTextX, iTextY, 231);
  if (iFileIdx != lastfile)                   // If different directory selected, rescan files and reset selection
  {
    topfile = 0;
    filefile = 0;
    scandirectory(szPattern);
    iCurrentFile = filefile;
    lastfile = iFileIdx;
    if (!filefiles)
      iCurrentFile = 0;
    if (iCurrentFile < topfile)
      topfile -= 3;
    if (iCurrentFile >= topfile + 18)
      topfile += 3;
    szDestPtr = selectfilename;
    szSourcePtr = filename[iCurrentFile];
    filefile = iCurrentFile;
    do {
      byChar1 = *szSourcePtr;
      *szDestPtr = *szSourcePtr;
      if (!byChar1)
        break;
      byChar2 = szSourcePtr[1];
      szSourcePtr += 2;
      szDestPtr[1] = byChar2;
      szDestPtr += 2;
    } while (byChar2);
  }
  while (fatkbhit())                          // Main keyboard input processing loop
  {
    byKeyCode = fatgetch();
    byProcessedKey = byKeyCode;
    if (byKeyCode) {
      if ((uint8)byKeyCode < 0xDu) {
        if (byKeyCode == 8) {
          uiBackspaceLen = (uint32)strlen(selectfilename) + 1;
          if ((int)(uiBackspaceLen - 1) > 0)
            selectfilename[strlen(selectfilename) - 1] = 0;
            //filesel_variable_1[uiBackspaceLen - 1] = 0;
        } else {
        LABEL_85:
          if ((uint8)byKeyCode > 0x39u)
            byProcessedKey = byKeyCode & 0xDF;
          iInputStrLen = (uint32)strlen(selectfilename);
          if (iInputStrLen <= 7
            && ((uint8)byProcessedKey >= 0x41u && (uint8)byProcessedKey <= 0x5Au
                || (uint8)byProcessedKey >= 0x30u && (uint8)byProcessedKey <= 0x39u)) {
            selectfilename[iInputStrLen] = byProcessedKey;
            selectfilename[iInputStrLen + 1] = 0;
          }
        }
      } else if ((uint8)byKeyCode <= 0xDu) {
        sfxsample(83, 0x8000);
        switch (filingmenu) {
          case 1:
            loadreplay();
            replayedit = 0;
            replayselect = 0;
            disciconpressed = 0;
            rotpoint = currentreplayframe;
            break;
          case 2:
            if (selectfilename[0]) {
              savereplay();
              disciconpressed = 0;
              rotpoint = currentreplayframe;
            }
            break;
          case 3:
            deletereplay();
            disciconpressed = 0;
            rotpoint = currentreplayframe;
            break;
          case 4:
            Rassemble();
            disciconpressed = 0;
            rotpoint = currentreplayframe;
            break;
          default:
            continue;                           // Execute action based on filing menu mode (1=Load, 2=Save, 3=Delete, 4=Assemble)
        }
      } else {
        if (byKeyCode != 27)
          goto LABEL_85;
        filingmenu = 0;
        disciconpressed = 0;
        rotpoint = currentreplayframe;
      }
    } else {
      switch ((uint8)fatgetch()) {
        case 'H':
          iUpNavFile = filefile - 3;
          if (filefile - 3 < 0)
            iUpNavFile = 0;
          if (!filefiles)
            iUpNavFile = 0;
          if (iUpNavFile < topfile)
            topfile -= 3;
          if (iUpNavFile >= topfile + 18)
            topfile += 3;
          szUpDestPtr = selectfilename;
          szUpSourcePtr = filename[iUpNavFile];
          filefile = iUpNavFile;
          do {
            byUpChar1 = *szUpSourcePtr;
            *szUpDestPtr = *szUpSourcePtr;
            if (!byUpChar1)
              break;
            byUpChar2 = szUpSourcePtr[1];
            szUpSourcePtr += 2;
            szUpDestPtr[1] = byUpChar2;
            szUpDestPtr += 2;
          } while (byUpChar2);
          break;
        case 'K':
          iLeftNavFile = filefile - 1;
          if (filefile - 1 < 0)
            iLeftNavFile = 0;
          if (!filefiles)
            iLeftNavFile = 0;
          if (iLeftNavFile < topfile)
            topfile -= 3;
          if (iLeftNavFile >= topfile + 18)
            topfile += 3;
          szLeftDestPtr = selectfilename;
          szLeftSourcePtr = filename[iLeftNavFile];
          filefile = iLeftNavFile;
          do {
            byLeftChar1 = *szLeftSourcePtr;
            *szLeftDestPtr = *szLeftSourcePtr;
            if (!byLeftChar1)
              break;
            byLeftChar2 = szLeftSourcePtr[1];
            szLeftSourcePtr += 2;
            szLeftDestPtr[1] = byLeftChar2;
            szLeftDestPtr += 2;
          } while (byLeftChar2);
          break;
        case 'M':
          iRightNavFile = filefile + 1;
          if (filefile + 1 >= filefiles)
            iRightNavFile = filefiles - 1;
          if (!filefiles)
            iRightNavFile = 0;
          if (iRightNavFile < topfile)
            topfile -= 3;
          if (iRightNavFile >= topfile + 18)
            topfile += 3;
          szRightDestPtr = selectfilename;
          szRightSourcePtr = filename[iRightNavFile];
          filefile = iRightNavFile;
          do {
            byRightChar1 = *szRightSourcePtr;
            *szRightDestPtr = *szRightSourcePtr;
            if (!byRightChar1)
              break;
            byRightChar2 = szRightSourcePtr[1];
            szRightSourcePtr += 2;
            szRightDestPtr[1] = byRightChar2;
            szRightDestPtr += 2;
          } while (byRightChar2);
          break;
        case 'P':
          iDownNavFile = filefile + 3;
          if (filefile + 3 >= filefiles)
            iDownNavFile = filefiles - 1;
          if (!filefiles)
            iDownNavFile = 0;
          if (iDownNavFile < topfile)
            topfile -= 3;
          if (iDownNavFile >= topfile + 18)
            topfile += 3;
          szDownDestPtr = selectfilename;
          szDownSourcePtr = filename[iDownNavFile];
          filefile = iDownNavFile;
          do {
            byDownChar1 = *szDownSourcePtr;
            *szDownDestPtr = *szDownSourcePtr;
            if (!byDownChar1)
              break;
            byDownChar2 = szDownSourcePtr[1];
            szDownSourcePtr += 2;
            szDownDestPtr[1] = byDownChar2;
            szDownDestPtr += 2;
          } while (byDownChar2);
          break;
        case 'S':
          uiDelStrLen = (uint32)strlen(selectfilename) + 1;
          if ((int)(uiDelStrLen - 1) > 0)
            selectfilename[strlen(selectfilename) - 1] = 0;
            //filesel_variable_1[uiDelStrLen - 1] = 0;// reference into selectfilename
          break;
        default:
          continue;                             // Handle arrow key navigation (H=Up, K=Left, M=Right, P=Down, S=Delete)
      }
    }
  }
  iRightEdge = iLeftEdge + 20;
  iDisplayFile = filefile;
  iLoopCounter = 0;
  iBottomEdge = iTextY + 20;
  do {
    filefile = iDisplayFile;                    // Display up to 18 files in a 3x6 grid layout
    if (iLoopCounter + topfile < filefiles) {
      iDisplayX = 100 * (iLoopCounter % 3) + iRightEdge;
      iDisplayY = iBottomEdge + 10 * (iLoopCounter / 3);
      szDisplayName = &filename[iLoopCounter][9 * topfile];
      if (iLoopCounter == iDisplayFile - topfile)
        prt_string(rev_vga[1], szDisplayName, iDisplayX, iDisplayY);
      else
        prt_stringcol(rev_vga[1], szDisplayName, iDisplayX, iDisplayY, 131);
    }
    ++iLoopCounter;
    iDisplayFile = filefile;
  } while (iLoopCounter < 18);
  if (iEditMode) {
    uiCursorLen = (uint32)strlen(selectfilename) + 1;
    uiCursorPos = uiCursorLen - 1;
    if (frames % 18 >= 9)                     // Animate blinking cursor for filename input (shows underscore every 9 frames)
    {
      selectfilename[uiCursorPos] = 0;
    } else {
      selectfilename[uiCursorPos] = 95;
      selectfilename[uiCursorPos + 1] = 0;
    }
    prt_stringcol(rev_vga[1], selectfilename, iLeftEdge + 20, iTextY + 100, 255);
    selectfilename[uiCursorLen - 1] = 0;
  }
  if (SVGA_ON)
    iScreenWidth = 640;
  else
    iScreenWidth = 320;
  iUpArrowX = iTextX + 128;
  if (topfile)                                // Draw scroll up/down arrows based on current position in file list
    replayicon(scrbuf, rev_vga[15], 79, iUpArrowX, iTextY - 3, iScreenWidth, 255);
  else
    replayicon(scrbuf, rev_vga[15], 78, iUpArrowX, iTextY - 3, iScreenWidth, 255);
  iDownArrowX = iTextX + 128;
  if (topfile + 18 >= filefiles)
    replayicon(scrbuf, rev_vga[15], 80, iDownArrowX, iTextY + 82, iScreenWidth, 255);
  else
    replayicon(scrbuf, rev_vga[15], 81, iDownArrowX, iTextY + 82, iScreenWidth, 255);
  if (!filefiles)                             // Show 'no files found' message if directory is empty
    prt_centrecol(rev_vga[1], &language_buffer[3328], iTextX, iTextY + 44, 143);
}

//-------------------------------------------------------------------------------------------------
//000679E0
void previouscut()
{
  int iSearchFrame; // ebx
  int iCutIndex; // esi
  int iCutCounter; // edx
  int iCameraIndex; // eax
  int iFrame; // edi
  int iTargetFrame; // ebx
  unsigned int uiPrevDisabled; // edx
  unsigned int uiTempDisabled; // eax
  unsigned int uiCurrDisabled; // eax
  unsigned int uiCurrentDisabled; // ecx
  unsigned int uiTempPrevDisabled; // eax

  resetsmoke();                                 // Clear smoke effects for clean transition
  iSearchFrame = currentreplayframe;            // Start search from current frame or previous frame if possible
  if (currentreplayframe > 0)
    iSearchFrame = currentreplayframe - 1;
  iCutIndex = -1;                               // Initialize cut index to -1 (no cut found)
  if (cuts)                                   // Search through camera cuts if any exist
  {
    iCutCounter = 0;
    if (cuts > 0) {
      iCameraIndex = 0;                         // Find the last cut that occurs before or at search frame
      do {                                         // Check if this cut frame is <= search frame
        if (camera[iCameraIndex].iFrame <= iSearchFrame)
          iCutIndex = iCutCounter;
        ++iCutCounter;
        ++iCameraIndex;
      } while (iCutCounter < cuts);
    }
  }
  iFrame = iCutIndex;                           // Set target frame based on found cut
  if (iCutIndex != -1)                        // If cut found, use the cut frame as target
    iFrame = camera[iCutIndex].iFrame;
  if (iSearchFrame >= iFrame)                 // Search backwards from current position to find previous cut boundary
  {                                             // Get disable status of previous and current frames
    if (iSearchFrame) {
      currentreplayframe = iSearchFrame;
      uiTempDisabled = readdisable(iSearchFrame - 1);
      iSearchFrame = currentreplayframe;
      uiPrevDisabled = uiTempDisabled;
    } else {
      uiPrevDisabled = 0;
    }
    currentreplayframe = iSearchFrame;
    uiCurrDisabled = readdisable(iSearchFrame);
    iTargetFrame = currentreplayframe;
    uiCurrentDisabled = uiCurrDisabled;
    while (iTargetFrame > 0)                  // Search backwards until boundary found or target frame reached
    {
      if (!uiCurrentDisabled && uiPrevDisabled)
        break;                                  // Stop if found transition from disabled to enabled frame
      if (iTargetFrame <= iFrame)
        break;                                  // Stop if reached target frame boundary
      --iTargetFrame;
      uiCurrentDisabled = uiPrevDisabled;
      if (iTargetFrame < 1) {
        uiPrevDisabled = 0;
      } else {
        currentreplayframe = iTargetFrame;
        uiTempPrevDisabled = readdisable(iTargetFrame - 1);
        iTargetFrame = currentreplayframe;
        uiPrevDisabled = uiTempPrevDisabled;
      }
    }
  } else {
    iTargetFrame = iFrame;
  }
  ticks = iTargetFrame;                         // Set game timer to target frame
  pend_view_init = ViewType[0];                 // Initialize view system for frame transition
  currentreplayframe = iTargetFrame;
}

//-------------------------------------------------------------------------------------------------
//00067AF0
void nextcut()
{
  int iCutIndex; // esi
  int iCutCounter; // eax
  int iCameraIndex; // edx
  int iSearchFrame; // ebx
  int iNextCutFrame; // esi
  int iTargetFrame; // ebx
  unsigned int uiPrevDisabled; // edx
  unsigned int uiCurrDisabled; // eax

  resetsmoke();                                 // Clear smoke effects for clean transition
  iCutIndex = -1;                               // Initialize cut index to -1 (no cut found)
  if (cuts)                                   // Search through camera cuts if any exist
  {
    iCutCounter = 0;
    if (cuts > 0) {
      iCameraIndex = 0;                         // Find the last cut that occurs before or at current frame
      do {                                         // Check if this cut frame is <= current frame
        if (camera[iCameraIndex].iFrame <= currentreplayframe)
          iCutIndex = iCutCounter;
        ++iCameraIndex;
        ++iCutCounter;
      } while (iCutCounter < cuts);
    }
  }
  iSearchFrame = currentreplayframe;            // Start search from current frame
  if (iCutIndex >= cuts - 1)                  // Determine next cut boundary or end of replay
    iNextCutFrame = replayframes + 1;           // If at last cut, target is end of replay
  else
    iNextCutFrame = camera[iCutIndex + 1].iFrame;// Otherwise target is next cut frame
  if (currentreplayframe < replayframes - 1)  // Advance to next frame if not at end of replay
    iSearchFrame = currentreplayframe + 1;
  if (iSearchFrame <= iNextCutFrame)          // Search forward from current position to find next cut boundary
  {                                             // Get disable status of previous frame for boundary detection
    if (iSearchFrame) {
      currentreplayframe = iSearchFrame;
      uiCurrDisabled = readdisable(iSearchFrame - 1);
      iSearchFrame = currentreplayframe;
      goto SEARCH_LOOP_ENTRY;
    }
    for (uiPrevDisabled = 0; ; uiPrevDisabled = uiCurrDisabled)// Search forward until boundary found or target reached
    {
      currentreplayframe = iSearchFrame;
      uiCurrDisabled = readdisable(iSearchFrame);
      iTargetFrame = currentreplayframe;
      if (currentreplayframe >= replayframes - 1 || !uiCurrDisabled && uiPrevDisabled)
        break;                                  // Stop at end of replay or transition from disabled to enabled
      if (currentreplayframe >= iNextCutFrame)
        break;                                  // Stop if reached next cut frame boundary
      iSearchFrame = currentreplayframe + 1;
    SEARCH_LOOP_ENTRY:
      ;
    }
  } else {
    iTargetFrame = iNextCutFrame;
  }
  ticks = iTargetFrame;                         // Set game timer to target frame
  pend_view_init = ViewType[0];                 // Initialize view system for frame transition
  currentreplayframe = iTargetFrame;
}

//-------------------------------------------------------------------------------------------------
//00067BF0
void loadreplay()
{
  char *szSrc; // esi
  char *szDst; // edi
  char byChar1; // al
  char byChar2; // al
  char *szExt; // esi
  char *szExtDst; // edi
  char byExtChar1; // al
  char byExtChar2; // al

  holdmusic = -1;                               // Set initial flags for replay loading
  loading_replay = -1;
  filingmenu = 0;
  if (filefiles)                              // If files are available for replay loading
  {
    play_game_uninit();
    strcpy(replayfilename, "..\\REPLAYS\\");    // Start building replay file path with base directory
    szSrc = selectfilename;                     // Manually copy selected filename to replay path (2 chars at a time)
    szDst = &replayfilename[strlen(replayfilename)];
    do {
      byChar1 = *szSrc;
      *szDst = *szSrc;
      if (!byChar1)
        break;
      byChar2 = szSrc[1];
      szSrc += 2;
      szDst[1] = byChar2;
      szDst += 2;
    } while (byChar2);
    szExt = g_szGss;                            // Append file extension to complete replay filename
    szExtDst = &replayfilename[strlen(replayfilename)];
    do {
      byExtChar1 = *szExt;
      *szExtDst = *szExt;
      if (!byExtChar1)
        break;
      byExtChar2 = szExt[1];
      szExt += 2;
      szExtDst[1] = byExtChar2;
      szExtDst += 2;
    } while (byExtChar2);
    replaytype = 2;                             // Initialize replay playback parameters
    //_disable();                                 // Disable interrupts while setting time-critical replay state
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    ticks = currentreplayframe;
    //_enable();
    play_game_init();
    pend_view_init = ViewType[0];
  }
  screenready = 0;                              // Clear screen and loading flags to complete initialization
  lagdone = 0;
  holdmusic = 0;
  loading_replay = 0;
}

//-------------------------------------------------------------------------------------------------
//00067CF0
void savereplay()
{
  uint8 *pbyBuffer; // ebp
  char *szSrc1; // esi
  char *szDst1; // edi
  char byChar1_1; // al
  char byChar2_1; // al
  char *szExtSrc1; // esi
  char *szExtDst1; // edi
  char byExtChar1_1; // al
  char byExtChar2_1; // al
  char *szSrc2; // esi
  char *szDst2; // edi
  char byChar1_2; // al
  char byChar2_2; // al
  char *szExtSrc2; // esi
  char *szExtDst2; // edi
  char byExtChar1_2; // al
  char byExtChar2_2; // al
  unsigned int uiBytesRead; // edi
  int iError; // eax
  int iErrorCopy; // edi
  char szTempFilename[32]; // [esp+0h] [ebp-48h] BYREF
  int iFilePos; // [esp+20h] [ebp-28h]
  int iErrorNum; // [esp+24h] [ebp-24h]
  FILE *pFile; // [esp+28h] [ebp-20h]
  int iErrorFlag; // [esp+2Ch] [ebp-1Ch]

  iErrorFlag = 0;                               // Initialize error flag and buffer pointer
  pbyBuffer = scrbuf;
  if (!strcmp(replayfilename, "..\\REPLAYS\\REPLAY.TMP"))// Check if currently working with temporary replay file
  {
    ftell(replayfile);                          // Finalize temp replay: close file and rename to permanent location
    fclose(replayfile);
    strcpy(replayfilename, "..\\REPLAYS\\");    // Build final replay filename: path + selected name + extension
    szSrc1 = selectfilename;                    // Copy selected filename to replay path (2 chars at a time)
    szDst1 = &replayfilename[strlen(replayfilename)];
    do {
      byChar1_1 = *szSrc1;
      *szDst1 = *szSrc1;
      if (!byChar1_1)
        break;
      byChar2_1 = szSrc1[1];
      szSrc1 += 2;
      szDst1[1] = byChar2_1;
      szDst1 += 2;
    } while (byChar2_1);
    szExtSrc1 = g_szGss;                        // Append file extension to complete the filename
    szExtDst1 = &replayfilename[strlen(replayfilename)];
    do {
      byExtChar1_1 = *szExtSrc1;
      *szExtDst1 = *szExtSrc1;
      if (!byExtChar1_1)
        break;
      byExtChar2_1 = szExtSrc1[1];
      szExtSrc1 += 2;
      szExtDst1[1] = byExtChar2_1;
      szExtDst1 += 2;
    } while (byExtChar2_1);
    ROLLERremove(replayfilename);                     // Remove existing file and rename temp to final name
    rename("..\\REPLAYS\\REPLAY.TMP", replayfilename);
    replayfile = ROLLERfopen(replayfilename, "rb");
    fseek(replayfile, 0, 0);
  } else {
    strcpy(szTempFilename, "..\\REPLAYS\\");    // Handle non-temp replay: copy current replay to new filename
    szSrc2 = selectfilename;                    // Build target filename: path + selected name + extension
    szDst2 = &szTempFilename[strlen(szTempFilename)];
    do {
      byChar1_2 = *szSrc2;
      *szDst2 = *szSrc2;
      if (!byChar1_2)
        break;
      byChar2_2 = szSrc2[1];
      szSrc2 += 2;
      szDst2[1] = byChar2_2;
      szDst2 += 2;
    } while (byChar2_2);
    szExtSrc2 = g_szGss;
    szExtDst2 = &szTempFilename[strlen(szTempFilename)];
    do {
      byExtChar1_2 = *szExtSrc2;
      *szExtDst2 = *szExtSrc2;
      if (!byExtChar1_2)
        break;
      byExtChar2_2 = szExtSrc2[1];
      szExtSrc2 += 2;
      szExtDst2[1] = byExtChar2_2;
      szExtDst2 += 2;
    } while (byExtChar2_2);
    if (strcmp(szTempFilename, replayfilename))// Only copy if target filename is different from current
    {
      pFile = ROLLERfopen(szTempFilename, "wb");      // Open target file for writing and prepare for copy operation
      if (pFile) {
        iFilePos = ftell(replayfile);
        fseek(replayfile, 0, 0);
        iErrorFlag = 0;                         // Copy file in chunks with error checking
        do {
          uiBytesRead = (uint32)fread(pbyBuffer, 1u, 0xFA00u, replayfile);
          if (fwrite(pbyBuffer, 1, uiBytesRead, pFile) != uiBytesRead) {
            iError = errno;// *(_DWORD *)_get_errno_ptr();// Handle write error - set error flag and save errno
            iErrorFlag = -1;
            iErrorNum = iError;
          }
        } while (uiBytesRead && !iErrorFlag);
        iErrorCopy = iErrorFlag;
        fclose(pFile);                          // Cleanup: close file, remove on error, restore file position
        if (iErrorCopy)
          ROLLERremove(szTempFilename);
        fseek(replayfile, iFilePos, 0);
        ticks = currentreplayframe;
      }
    }
  }
  filingmenu = 0;                               // Reset menu flags and screen state
  lastfile = 0;
  screenready = 0;
  if (iErrorFlag)                             // Set appropriate error menu state based on error type
  {
    if (iErrorNum == 12)
      filingmenu = 6;
    else
      filingmenu = 7;
  }
}

//-------------------------------------------------------------------------------------------------
//00067F50
void deletereplay()
{
  char *szSrc; // esi
  char *szDst; // edi
  char byChar1; // al
  char byChar2; // al
  char *szExtSrc; // esi
  char *szExtDst; // edi
  char byExtChar1; // al
  char byExtChar2; // al
  char *szTmpDst; // edi
  const char *szTmpSrc; // esi
  char byTmpChar1; // al
  char byTmpChar2; // al
  char szTargetFilename[40]; // [esp-30h] [ebp-34h] BYREF

  strcpy(szTargetFilename, "..\\REPLAYS\\");    // Build target filename: path + selected filename + extension
  szSrc = selectfilename;                       // Copy selected filename to path (2 chars at a time)
  szDst = &szTargetFilename[strlen(szTargetFilename)];
  do {
    byChar1 = *szSrc;
    *szDst = *szSrc;
    if (!byChar1)
      break;
    byChar2 = szSrc[1];
    szSrc += 2;
    szDst[1] = byChar2;
    szDst += 2;
  } while (byChar2);
  szExtSrc = g_szGss;                           // Append file extension to complete target filename
  szExtDst = &szTargetFilename[strlen(szTargetFilename)];
  do {
    byExtChar1 = *szExtSrc;
    *szExtDst = *szExtSrc;
    if (!byExtChar1)
      break;
    byExtChar2 = szExtSrc[1];
    szExtSrc += 2;
    szExtDst[1] = byExtChar2;
    szExtDst += 2;
  } while (byExtChar2);
  if (!strcmp(replayfilename, szTargetFilename))// Check if deleting the currently loaded replay file
  {
    ROLLERremove("..\\REPLAYS\\REPLAY.TMP");          // Deleting current replay: cleanup temp files and swap to temp
    ftell(replayfile);                          // Close current replay file and rename target to temp
    szTmpDst = replayfilename;
    fclose(replayfile);
    szTmpSrc = "..\\REPLAYS\\REPLAY.TMP";       // Copy temp filename to current replayfilename (2 chars at a time)
    ROLLERrename(szTargetFilename, "..\\REPLAYS\\REPLAY.TMP");
    do {
      byTmpChar1 = *szTmpSrc;
      *szTmpDst = *szTmpSrc;
      if (!byTmpChar1)
        break;
      byTmpChar2 = szTmpSrc[1];
      szTmpSrc += 2;
      szTmpDst[1] = byTmpChar2;
      szTmpDst += 2;
    } while (byTmpChar2);
    replayfile = ROLLERfopen(replayfilename, "rb");   // Reopen temp file as current replay
    fseek(replayfile, 0, 0);
  } else {
    ROLLERremove(szTargetFilename);                   // Not current replay: simple file deletion
  }
  ticks = currentreplayframe;                   // Reset replay state and clear menu flags
  filingmenu = 0;
  lastfile = 0;
}

//-------------------------------------------------------------------------------------------------
//00068070
void updatedirectory()
{
  int iNewTopFile; // edx
  char *szDst; // edi
  char *szSrc; // esi
  char byChar1; // al
  char byChar2; // al

  iNewTopFile = topfile;                        // Initialize with current top file position
  if (!filefiles)                             // If no files available, reset current file selection
    filefile = 0;
  if (filefile < topfile)                     // If selected file is above view window, scroll up 3 positions
    iNewTopFile = topfile - 3;
  if (iNewTopFile + 18 <= filefile)           // If selected file is below view window (18 files visible), scroll down 3 positions
    iNewTopFile += 3;
  szDst = selectfilename;                       // Copy currently selected filename to selectfilename buffer (2 chars at a time)
  szSrc = filename[filefile];
  topfile = iNewTopFile;                        // Update top file position for scrolled view
  do {
    byChar1 = *szSrc;
    *szDst = *szSrc;
    if (!byChar1)
      break;
    byChar2 = szSrc[1];
    szSrc += 2;
    szDst[1] = byChar2;
    szDst += 2;
  } while (byChar2);
}

//-------------------------------------------------------------------------------------------------
//000680F0
void findintrofiles()
{
  introfiles = 0;
#ifdef IS_WINDOWS
  struct _finddata_t fileinfo;
  intptr_t handle = _findfirst("INTRO*.GSS", &fileinfo);

  if (handle == -1)
    return;

  do {
    introfiles++;
  } while (_findnext(handle, &fileinfo) == 0);

  _findclose(handle);
#else
  DIR *dir = opendir(".");
  if (!dir)
    return;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (fnmatch("INTRO*.GSS", entry->d_name, FNM_CASEFOLD) == 0) {
      introfiles++;
      continue;
    }
    if (fnmatch("intro*.gss", entry->d_name, FNM_CASEFOLD) == 0) {
      introfiles++;
      continue;
    }
  }

  closedir(dir);
#endif
}

//-------------------------------------------------------------------------------------------------
//00068130
void displaycontrolpanel()
{
  int iScreenWidth; // ebp
  int iIconOffset; // eax
  int iCalculatedFrame; // edi
  int iFrameLoop; // eax
  int iFrameLoop2; // eax
  int iRotateIndex; // eax
  int iRotateLoop; // esi
  int iRotateX; // ecx
  int iCurrentCut; // ebx
  int iCutLoop; // eax
  int iCutIndex; // edx
  const char *pEditString; // eax
  int iRotateY; // [esp-Ch] [ebp-48h]
  int iEndFrame; // [esp+4h] [ebp-38h]
  int iStartFrame; // [esp+8h] [ebp-34h]
  int iRotateLimit; // [esp+18h] [ebp-24h]

  if (SVGA_ON) {
    iScreenWidth = 640;
    replayicon(scrbuf, rev_vga[15], 0, 0, 150, 640, -1);
  } else {
    iScreenWidth = 320;
    replayicon(scrbuf, rev_vga[15], 0, 0, 150, 320, -1);
  }
  if (!replayedit)                            // Draw control panel overlay if not in replay edit mode
    replayicon(scrbuf, rev_vga[15], 77, 117, 187, iScreenWidth, -1);
  if (!keys[WHIP_SCANCODE_RETURN] || paused || (iIconOffset = -1, controlicon == 18) && !replayselect)// Calculate icon offset: -1 if ENTER pressed and conditions met, 0 otherwise
    iIconOffset = 0;
  replayicon(scrbuf, rev_vga[15], controlicon, ricon[controlicon].nX, ricon[controlicon].nY + 150 + iIconOffset, iScreenWidth, -1);// Draw current control icon with calculated offset
  if (replayspeed > 0 && !forwarding && !rewinding)// Draw fast forward indicators when replay speed is positive
  {
    if (controlicon == 4)
      replayicon(scrbuf, rev_vga[15], 4, ricon[4].nX, ricon[4].nY + 149, iScreenWidth, -1);
    else
      replayicon(scrbuf, rev_vga[15], 27, ricon[4].nX, ricon[4].nY + 149, iScreenWidth, -1);
  }
  if (replayspeed < 0 && !forwarding && !rewinding)// Draw rewind indicators when replay speed is negative
  {
    if (controlicon == 3)
      replayicon(scrbuf, rev_vga[15], 3, ricon[3].nX, ricon[3].nY + 149, iScreenWidth, -1);
    else
      replayicon(scrbuf, rev_vga[15], 26, ricon[3].nX, ricon[3].nY + 149, iScreenWidth, -1);
  }
  if (replayedit)                             // Draw edit mode indicator icon
  {
    if (controlicon == 17)
      replayicon(scrbuf, rev_vga[15], 17, ricon[17].nX, ricon[17].nY + 149, iScreenWidth, -1);
    else
      replayicon(scrbuf, rev_vga[15], 28, ricon[17].nX, ricon[17].nY + 149, iScreenWidth, -1);
  }
  if (replayselect)                           // Draw replay selection mode indicator icon
  {
    if (controlicon == 18)
      replayicon(scrbuf, rev_vga[15], 18, ricon[18].nX, ricon[18].nY + 149, iScreenWidth, -1);
    else
      replayicon(scrbuf, rev_vga[15], 29, ricon[18].nX, ricon[18].nY + 149, iScreenWidth, -1);
  }
  if (disciconpressed)                        // Draw disc icon when pressed
    replayicon(scrbuf, rev_vga[15], 16, ricon[16].nX, ricon[17].nY + 149, iScreenWidth, -1);
  replaypanelstring(replayhelp[controlicon], 255, 165, iScreenWidth);// Display help text for current control icon
  if (replayedit)                             // EDIT MODE: Display edit mode information panel
  {
    replaypanelstring("CURRENT:", 164, 165, iScreenWidth);// Show current edit info text
    sprintf(buffer, "CAR %d", ViewType[0] + 1); // Display current car number being viewed
    replaypanelstring(buffer, 199, 165, iScreenWidth);
    replaypanelstring(views[SelectedView[0]], 225, 165, iScreenWidth);// Display current camera view type
    if (cuts)                                 // Process camera cuts if they exist
    {
      iCurrentCut = -1;
      iCutLoop = 0;
      if (cuts > 0) {
        iCutIndex = 0;                          // Find the current active camera cut based on replay frame
        do {
          if (currentreplayframe >= camera[iCutIndex].iFrame)
            iCurrentCut = iCutLoop;
          ++iCutLoop;
          ++iCutIndex;
        } while (iCutLoop < cuts);
      }
      if (iCurrentCut != -1)                  // Display camera cut information if one is active
      {                                         // Show different text for exact frame match vs approximate
        if (camera[iCurrentCut].iFrame == currentreplayframe)
          pEditString = g_szNewEdit;
        else
          pEditString = g_szEdit;
        replaypanelstring(pEditString, 164, 171, iScreenWidth);
        sprintf(buffer, "CAR %d", camera[iCurrentCut].byCarIdx + 1);
        replaypanelstring(buffer, 199, 171, iScreenWidth);
        replaypanelstring(views[camera[iCurrentCut].byView], 225, 171, iScreenWidth);
      }
    }
    if (replayselect)                         // Display selection range if in replay select mode
    {                                           // Order start and end frames correctly for display
      if (replaystart >= currentreplayframe) {
        iStartFrame = currentreplayframe;
        iEndFrame = replaystart;
      } else {
        iStartFrame = replaystart;
        iEndFrame = currentreplayframe;
      }
      replaypanelstring("BLOCK:", 164, 159, iScreenWidth);// Display selection block text and time range
      displaypaneltime(iStartFrame, 189, 159, iScreenWidth);
      displaypaneltime(iEndFrame, 223, 159, iScreenWidth);
      replaypanelstring("-", 218, 159, iScreenWidth);
    } else {
      displaypaneltime(currentreplayframe, 223, 159, iScreenWidth);// Just show current time when not selecting
    }
  } else {
    sprintf(buffer, "X %s", replayname[replaysetspeed]);// NORMAL MODE: Display speed multiplier and status icons
    replaypanelstring(buffer, 229, 165, iScreenWidth);
    displaypaneltime(currentreplayframe, 223, 159, iScreenWidth);// Display current replay time
    if (replayspeed > 0 && !forwarding && !rewinding)// Show fast forward icon when playing forward
      replayicon(scrbuf, rev_vga[15], 73, 164, 161, iScreenWidth, 255);
    if (replayspeed < 0 && !forwarding && !rewinding)// Show rewind icon when playing backward
      replayicon(scrbuf, rev_vga[15], 72, 164, 161, iScreenWidth, 255);
    if (forwarding && replayspeed > 0)        // Show fast forward icon when actively fast forwarding
      replayicon(scrbuf, rev_vga[15], 71, 164, 161, iScreenWidth, 255);
    if (rewinding && replayspeed < 0)         // Show rewind icon when actively rewinding
      replayicon(scrbuf, rev_vga[15], 70, 164, 161, iScreenWidth, 255);
    if (!replayspeed)                         // Show pause icon when replay speed is zero
      replayicon(scrbuf, rev_vga[15], 74, 164, 161, iScreenWidth, 255);
    replayicon(scrbuf, rev_vga[15], 75, 190, 161, iScreenWidth, 255);// Draw progress bar background
    if (currentreplayframe <= rotpoint)       // Calculate position for progress bar indicators
    {                                           // Current frame is before rotation point
      if (rotpoint - currentreplayframe > 24) {
        iCalculatedFrame = rotpoint - 24;
        for (iFrameLoop2 = rotpoint - 152; iFrameLoop2 >= currentreplayframe; iCalculatedFrame -= 64)
          iFrameLoop2 -= 64;
      } else {
        iCalculatedFrame = currentreplayframe;
      }
    } else if (currentreplayframe - rotpoint > 24)// Current frame is after rotation point
    {
      iCalculatedFrame = rotpoint + 24;
      for (iFrameLoop = rotpoint + 152; iFrameLoop <= currentreplayframe; iCalculatedFrame += 64)
        iFrameLoop += 64;
    } else {
      iCalculatedFrame = currentreplayframe;
    }
    iRotateIndex = iCalculatedFrame / 8;  // Calculate rotation index for progress bar markers
    //iRotateIndex = (iCalculatedFrame - (__CFSHL__(iCalculatedFrame >> 31, 3) + 8 * (iCalculatedFrame >> 31))) >> 3;// Calculate rotation index for progress bar markers
    rotpoint = iCalculatedFrame;
    iRotateLoop = iRotateIndex;
    iRotateLimit = iRotateIndex + 3;

    do {
      iRotateY = rrotate[iRotateLoop % 8].y + 150;
      iRotateX = rrotate[iRotateLoop % 8].x;
      ++iRotateLoop;
      replayicon(scrbuf, rev_vga[15], 76, iRotateX, iRotateY, iScreenWidth, 255);
    } while (iRotateLoop < iRotateLimit);
  }
}

//-------------------------------------------------------------------------------------------------
//00068910
void rtoggleedit()
{
  replayedit = replayedit == 0;
  replayselect = 0;
  if (replaytype == 2) {
    //_disable();
    replayspeed = 0;
    fraction = 0;
    replaydirection = 0;
    ticks = currentreplayframe;
    //_enable();
  }
  rotpoint = currentreplayframe;
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00068980
void rstartblock()
{
  bool bReplaySelected; // zf

  if (replayedit) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    bReplaySelected = replayselect != 0;
    replayselect = replayselect == 0;
    if (!bReplaySelected && replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    replaystart = currentreplayframe;
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068A20
void rselectblock()
{                                               // Check if replay editing mode is active
  int iStartFrame; // eax
  int iEndFrame; // ebx
  int iFrameIndex; // edx
  int iCurrentFrame; // eax

  if (replayedit) {                                             // Check if replay type is 2 (selection mode)
    if (replaytype == 2) {
      //_disable();                               // Pause replay playback - disable interrupts
      replayspeed = 0;                          // Stop replay playback
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    if (!replayselect)                        // If no selection active, start new selection at current frame
      replaystart = currentreplayframe;
    iStartFrame = replaystart;                  // Get frame range for selection block operation
    iEndFrame = currentreplayframe;
    if (replaytype == 2) {                                           // Ensure start frame <= end frame for proper range
      if (currentreplayframe < replaystart) {
        iStartFrame = currentreplayframe;
        iEndFrame = replaystart;
      }
      for (iFrameIndex = iStartFrame; iFrameIndex <= iEndFrame; ++iFrameIndex)// Clear disabled status for all frames in selected range
      {
        iCurrentFrame = iFrameIndex;
        cleardisable(iCurrentFrame);
      }
    }
    replayselect = 0;                           // Clear selection flag
    if (replaytype == 2) {
      //_disable();                               // Reset replay state after selection operation
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068AF0
void rdeleteblock()
{                                               // Check if replay editing mode is active
  int iStartFrame; // ecx
  int iEndFrame; // ebx
  int iFrameIndex; // edx
  int iCurrentFrame; // eax

  if (replayedit) {                                             // Check if replay type is 2 (deletion mode)
    if (replaytype == 2) {
      //_disable();                               // Pause replay playback - disable interrupts
      replayspeed = 0;                          // Stop replay playback
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    if (!replayselect)                        // If no selection active, start new selection at current frame
      replaystart = currentreplayframe;
    iStartFrame = replaystart;                  // Get frame range for deletion block operation
    iEndFrame = currentreplayframe;
    if (replaytype == 2) {                                           // Ensure start frame <= end frame for proper range
      if (currentreplayframe < replaystart) {
        iStartFrame = currentreplayframe;
        iEndFrame = replaystart;
      }
      for (iFrameIndex = iStartFrame; iFrameIndex <= iEndFrame; ++iFrameIndex)// Mark all frames in selected range as disabled/deleted
      {
        iCurrentFrame = iFrameIndex;
        setdisable(iCurrentFrame);
      }
    }
    replayselect = 0;                           // Clear selection flag
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068B90
void rstoreview()
{
  if (replayedit) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    storecut();
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068BE0
void rremoveview()
{
  if (replayedit) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    removecut();
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068C30
void rpreviouscut()
{
  if (replayedit) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    previouscut();
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068C80
void rnextcut()
{
  if (replayedit) {
    if (replaytype == 2) {
      //_disable();
      replayspeed = 0;
      fraction = 0;
      replaydirection = 0;
      ticks = currentreplayframe;
      //_enable();
    }
    nextcut();
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
  }
}

//-------------------------------------------------------------------------------------------------
//00068CD0
void rstartassemble()
{
  int iEnabledFramesFound; // ebx
  int iFrameIndex; // edx

  iEnabledFramesFound = 0;                      // Initialize flag - assume no enabled frames found
  if (replaytype == 2 && replayedit)          // Only allow assembly in edit mode with replay editing enabled
  {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                      // SOUND_SAMPLE_BUTTON
    iFrameIndex = 0;                            // Start searching from first frame
    if (replayframes > 0)                     // Search through all replay frames
    {                                           // Skip disabled frames - look for first enabled frame
      while (readdisable(iFrameIndex)) {
        if (++iFrameIndex >= replayframes)
          goto CHECK_RESULTS;
      }
      iEnabledFramesFound = -1;                 // Found at least one enabled frame
    }
  CHECK_RESULTS:
    if (iEnabledFramesFound)                  // Set appropriate menu state based on search results
      filingmenu = 4;                           // No enabled frames - set menu to error/warning state
    else
      filingmenu = 8;                           // Enabled frames found - proceed with assembly menu
  }
}

//-------------------------------------------------------------------------------------------------
//00068D50
void replayicon(uint8 *pDest, tBlockHeader *pBlockHeader, int iBlockIdx, int iX, int iY, int iScreenWidth, int byTransparentColor)
{
  int iScaledDestY; // edx
  uint8 *pDestPixel; // eax
  tBlockHeader *pSpriteHeader; // edx
  uint8 *pSpritePixels; // edx
  int iCurrentX; // esi
  int iPixelX; // ebp
  uint8 byPixelColor; // cl
  int iSvgaCurrentX; // esi
  uint8 bySvgaPixelColor; // cl
  uint8 *pNextLineStart; // ebp
  uint8 *pTempPixel1; // eax
  uint8 *pTempPixel2; // ebp
  uint8 *pTempPixel3; // eax
  uint8 *pTempPixel4; // ebp
  int iHeight; // [esp+Ch] [ebp-2Ch]
  int iScaledDestX; // [esp+10h] [ebp-28h]
  int iCurrentRow; // [esp+14h] [ebp-24h]
  int iRowLoop; // [esp+18h] [ebp-20h]
  int iWidth; // [esp+1Ch] [ebp-1Ch]
  uint8 *pSvgaNextLine; // [esp+20h] [ebp-18h]
  int iSvgaPixelX; // [esp+24h] [ebp-14h]

  iScaledDestX = iX;
  iScaledDestY = iY;
  if (SVGA_ON)                                // If SVGA mode is enabled, scale coordinates by 2x
  {
    iScaledDestY = 2 * iY;
    iScaledDestX = 2 * iX;
  }
  pDestPixel = &pDest[iScaledDestX + iScreenWidth * iScaledDestY];// Calculate destination pixel pointer: buffer + (y * width) + x
  iWidth = pBlockHeader[iBlockIdx].iWidth;      // Get sprite width from sprite data header (offset 0)
  pSpriteHeader = &pBlockHeader[iBlockIdx];
  iHeight = pSpriteHeader->iHeight;             // Get sprite height from sprite data header (offset 4)
  pSpritePixels = (uint8 *)pBlockHeader + pSpriteHeader->iDataOffset;// Get pointer to sprite pixel data (offset 8)

  // Branch to SVGA rendering path (2x2 pixel scaling)
  if (SVGA_ON) {
    iCurrentRow = 0;                            // SVGA mode: outer loop for sprite rows with 2x scaling
    if (iHeight <= 0)
      return;
    while (1) {
      iSvgaCurrentX = iScaledDestX;
      iSvgaPixelX = 0;
      if (iWidth > 0)
        break;
    SVGA_NEXT_ROW:
          // SVGA: Move to next row, skip unused portion of screen line
      pDestPixel += 2 * (iScreenWidth - iWidth);// SVGA_NEXT_ROW: Advance to next sprite row and check completion
      if (++iCurrentRow >= iHeight)
        return;
    }
    while (1)                                 // SVGA mode: inner loop for sprite pixels with 2x2 scaling
    {
      bySvgaPixelColor = *pSpritePixels++;
      if (iSvgaCurrentX < 0 || iSvgaCurrentX >= iScreenWidth)
        break;                                  // SVGA bounds check
      pNextLineStart = &pDestPixel[iScreenWidth];
      pSvgaNextLine = &pDestPixel[iScreenWidth];

      // Transparency check branch for SVGA mode
      if (byTransparentColor >= 0) {
        if (byTransparentColor == bySvgaPixelColor)
          break;

        // SVGA: Draw 2x2 pixel block (with transparency check)
        *pDestPixel = bySvgaPixelColor;
        pTempPixel3 = pDestPixel + 1;
        *pSvgaNextLine = bySvgaPixelColor;
        pTempPixel4 = &pTempPixel3[iScreenWidth];
        *pTempPixel3 = bySvgaPixelColor;
        pDestPixel = pTempPixel3 + 1;
        *pTempPixel4 = bySvgaPixelColor;
      } else {

        // SVGA: Draw 2x2 pixel block (no transparency check)
        *pDestPixel = bySvgaPixelColor;
        pTempPixel1 = pDestPixel + 1;
        *pNextLineStart = bySvgaPixelColor;
        pTempPixel2 = &pTempPixel1[iScreenWidth];
        *pTempPixel1 = bySvgaPixelColor;
        pDestPixel = pTempPixel1 + 1;
        *pTempPixel2 = bySvgaPixelColor;
      }
    SVGA_NEXT_PIXEL:
      iSvgaCurrentX += 2;                       // SVGA_NEXT_PIXEL: Advance to next pixel in current row (2x scaling)
      if (++iSvgaPixelX >= iWidth)
        goto SVGA_NEXT_ROW;
    }
    pDestPixel += 2;
    goto SVGA_NEXT_PIXEL;
  }

  // Standard VGA rendering loop - iterate through sprite rows
  for (iRowLoop = 0; iRowLoop < iHeight; ++iRowLoop) {
    iCurrentX = iScaledDestX;
    // Inner loop - iterate through sprite pixels in current row
    for (iPixelX = 0; iPixelX < iWidth; ++pDestPixel) {
      byPixelColor = *pSpritePixels++;

      // Check bounds and transparency: skip if out of bounds or matches transparent color
      if (iCurrentX >= 0 && iCurrentX < iScreenWidth && (byTransparentColor < 0 || byTransparentColor != byPixelColor))
        *pDestPixel = byPixelColor;
      ++iCurrentX;
      ++iPixelX;
    }

    // Move to next row: advance pointer by (screen_width - sprite_width)
    pDestPixel += iScreenWidth - iWidth;
  }
}

//-------------------------------------------------------------------------------------------------
//00068EF0
void replaypanelletter(char c, int *piX, int *piY, int iScreenWidth)
{
  int iCharBlockIdx; // esi

  iCharBlockIdx = -1;
  if (c >= '0' && c <= '9')
    iCharBlockIdx = c - 18;
  if (c >= 'A' && c <= 'Z')
    iCharBlockIdx = c - 25;
  if (c == '-')
    iCharBlockIdx = 66;
  if (c == ':')
    iCharBlockIdx = 67;
  if (c == '/')
    iCharBlockIdx = 68;
  if (c == '?')
    iCharBlockIdx = 69;
  if (iCharBlockIdx != -1) {
    replayicon(scrbuf, rev_vga[15], iCharBlockIdx, *piX, *piY, iScreenWidth, 255);
    *piX += rev_vga[15][iCharBlockIdx].iWidth + 1;
  }
}

//-------------------------------------------------------------------------------------------------
//00068F80
void replaypanelstring(const char *szStr, int iX, int iY, int iScreenWidth)
{
  const char *szStrItr; // esi
  unsigned int i; // ebp
  int iXPos; // [esp+4h] [ebp-1Ch] BYREF
  int iYPos; // [esp+8h] [ebp-18h] BYREF
  int iScreenWidth_1; // [esp+Ch] [ebp-14h]
  const char *szGetStrLen; // [esp+10h] [ebp-10h]

  szGetStrLen = szStr;
  iXPos = iX;
  iYPos = iY;
  iScreenWidth_1 = iScreenWidth;
  szStrItr = szStr;
  for (i = 0; i < strlen(szGetStrLen); ++i) {
    if (*szStrItr == '|') {
      iXPos = iX;
      iYPos += 6;
    } else if (*szStrItr == ' ') {
      iXPos += 2;
    } else {
      replaypanelletter(*szStrItr, &iXPos, &iYPos, iScreenWidth_1);
    }
    ++szStrItr;
  }
}

//-------------------------------------------------------------------------------------------------
//00069000
void displaypaneltime(int iTime, int iX, int iY, int iScreenWidth)
{
  sprintf(buffer, "%02d:%02d:%02d", iTime / 36 / 60, iTime / 36 % 60, iTime % 36);
  replaypanelstring(buffer, iX, iY, iScreenWidth);
}

//-------------------------------------------------------------------------------------------------
//00069080
void discmenu()
{
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
  lsdsel = 0;
  disciconpressed = -1;
  filingmenu = 5;
}

//-------------------------------------------------------------------------------------------------
//000690C0
void initsoundlag(uint32 uiTicks)
{
  //cli();  // Disable interrupts

  delayread = 0;
  delaywrite = 6;

  int iNumCars = numcars;
  if (iNumCars > 16) iNumCars = 16;

  for (int iCarIdx = 0; iCarIdx < iNumCars; iCarIdx++) {
    // Initialize all 6 entries for this car
    for (int iEntryIdx = 0; iEntryIdx < 6; iEntryIdx++) {
      enginedelay[iCarIdx].engineSoundData[iEntryIdx].iEngineVol = -1;
      enginedelay[iCarIdx].engineSoundData[iEntryIdx].iEngine2Vol = -1;
      enginedelay[iCarIdx].engineSoundData[iEntryIdx].iSkid1Vol = -1;
    }
  }

  // Initialize timing system
  ticks = uiTicks;
  fraction = 0;
  currentreplayframe = uiTicks;
  lastreplayframe = uiTicks;
  replayspeed = 0x100;  // 1.0x speed

  //sti();  // Enable interrupts
}

//-------------------------------------------------------------------------------------------------
//00069160
void resetsmoke()
{
  int iNumCars; // esi
  int iCarIndex; // ebx
  //int iCarSprayEnd; // ecx
  //unsigned int uiOffset; // eax

  iNumCars = numcars;
  iCarIndex = 0;

  for (iCarIndex = 0; iCarIndex < iNumCars; iCarIndex++)
  {
    for (int iSprayIndex = 0; iSprayIndex < 32; iSprayIndex++)
    {
      CarSpray[iCarIndex][iSprayIndex].iLifeTime = 0;
    }
  }
  //if (numcars > 0) {
  //  iCarSprayEnd = 1408;
  //  do {
  //    uiOffset = 1408 * iCarIndex;
  //    do {
  //      uiOffset += sizeof(tCarSpray);
  //      car_texs_loaded[uiOffset / 4 + 12] = 0; // offset into CarSpray
  //    } while (uiOffset != iCarSprayEnd);
  //    ++iCarIndex;
  //    iCarSprayEnd += 1408;
  //  } while (iCarIndex < iNumCars);
  //}

  numcars = iNumCars;
}

//-------------------------------------------------------------------------------------------------
