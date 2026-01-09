#include "func3.h"
#include "3d.h"
#include "sound.h"
#include "frontend.h"
#include "roller.h"
#include "carplans.h"
#include "car.h"
#include "transfrm.h"
#include "graphics.h"
#include "polyf.h"
#include "polytex.h"
#include "drawtrk3.h"
#include "func2.h"
#include "network.h"
#include "colision.h"
#include "control.h"
#include "comms.h"
#include "function.h"
#include "loadtrak.h"
#include "rollercomms.h"
#include <memory.h>
#include <fcntl.h>
#include <math.h>
#include <float.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#define read _read
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif
//-------------------------------------------------------------------------------------------------
//symbols defined by ROLLER
char szMrEvil[10] = "MR EVIL"; //000A23F8

//-------------------------------------------------------------------------------------------------

char save_slots[4][13] =  //000A6234
{
  "champ1.sav",
  "champ2.sav",
  "champ3.sav",
  "champ4.sav"
};
int credit_order[25] =      //000A6268
{ 3, 1, 0, 2, 4, 5, 6, 7, -1, 0, 1, 2, 3, 4, -2 };
char round_pals[8][13] =  //000A62A4
{
  "round1.pal",
  "round2.pal",
  "round3.pal",
  "round4.pal",
  "round5.pal",
  "round6.pal",
  "round7.pal",
  "round8.pal"
};
char round_pics[8][13] =  //000A630C
{
  "round1.bm",
  "round2.bm",
  "round3.bm",
  "round4.bm",
  "round5.bm",
  "round6.bm",
  "round7.bm",
  "round8.bm"
};
char send_buffer[32] = "HELLO. WHAT A LOVELY DAY"; //000A6374
int send_message_to = -1; //000A6394
int rec_status = 0;       //000A6398
char rec_mes_buf[32];     //00188530
char send_mes_buf[32];    //00188550
tSaveStatus save_status[4]; //00188570
int result_lap[16];       //001885D0
int result_order[16];     //00188610
float result_time[16];    //00188650
int result_design[16];    //00188690
float result_best[16];    //001886D0
int result_competing[16]; //00188710
int result_control[16];   //00188750
int result_lives[16];     //00188790
int result_kills[16];     //001887D0
int send_status;          //00188810
char rec_mes_name[12];    //00188814
int restart_net;          //00188828
float BestTime;           //0018882C
int result_p1;            //00188830
int result_p2;            //00188834
int result_p2_pos;        //00188838
int result_p1_pos;        //0018883C

//-------------------------------------------------------------------------------------------------
//00056070
int winner_screen(eCarDesignIndex carDesign, char byFlags)
{
  int iExit; // ebp
  //int iCartexLoopItr; // eax
  eCarType carType; // eax
  eCarType carType_1; // esi
  int iTexturesLoaded; // edx
  int iExistingTexIdx; // ecx
  uint8 *pScrBuf; // edi
  tBlockHeader *pWinnerImage; // esi
  unsigned int uiBufSize; // ecx
  char byBufSizeLow; // al
  unsigned int uiDWordCount; // ecx
  int16 nNewYaw; // ax
  tBlockHeader **ppFrontVgaItr; // edx
  void **ppFrePtr; // eax
  uint8 **ppCartexItr; // edx
  void **ppFrePtr_1; // eax
  int iRetVal; // [esp+0h] [ebp-28h]
  int iOldGfxSize; // [esp+4h] [ebp-24h]
  char byAnimFrame; // [esp+8h] [ebp-20h]
  int iDisplayDuration; // [esp+Ch] [ebp-1Ch]

  tick_on = -1;
  frontend_on = -1;
  iRetVal = -1;
  SVGA_ON = -1;
  iOldGfxSize = gfx_size;
  front_fade = 0;
  init_screen();
  winx = 0;
  winy = 0;
  winw = XMAX;
  mirror = 0;
  winh = YMAX;

  // longer display with music
  if ( MusicVolume && MusicCard )
    iDisplayDuration = 720;
  else
    iDisplayDuration = 180;

  // load graphics
  front_vga[0] = (tBlockHeader *)load_picture("winner.bm");
  front_vga[1] = (tBlockHeader *)load_picture("font3.bm");

  iExit = 0;
  setpal("winner.pal");
  FindShades();

  // init car pos and orientation
  frames = 0;
  Car[0].pos.fX = 0.0;
  Car[0].pos.fY = 0.0;
  Car[0].pos.fZ = 0.0;
  gfx_size = 0;
  Car[0].nYaw = 0;
  Car[0].nRoll = 0;
  Car[0].nPitch = 0;
  set_starts(0);

  for (int i = 0; i < 16; ++i) {
    car_texs_loaded[i] = -1;
  }

  carType = CarDesigns[carDesign].carType;
  carType_1 = carType;
  iTexturesLoaded = 1;
  iExistingTexIdx = car_texs_loaded[carType];

  if ( iExistingTexIdx == -1 )
  {
    // load new car tex if not already loaded
    LoadCarTexture(carType, 1u);
    iTexturesLoaded = 2;
    car_texmap[carDesign] = 1;
    car_texs_loaded[carType_1] = 1;
  }
  else
  {
    // use existing texture
    car_texmap[carDesign] = iExistingTexIdx;
  }

  LoadCarTextures = iTexturesLoaded;
  NoOfTextures = 255;
  if ( SVGA_ON )
    scr_size = 128;
  else
    scr_size = 64;

  // Start winner screen sequence
  ticks = 0;
  frames = 0;
  startmusic(winsong);
  byAnimFrame = byFlags & 1;
  do
  {
    pScrBuf = scrbuf;
    pWinnerImage = front_vga[0];
    if ( SVGA_ON )
      uiBufSize = 256000;
    else
      uiBufSize = 64000;

    // dword copy
    byBufSizeLow = uiBufSize;
    uiDWordCount = uiBufSize >> 2;
    memcpy(scrbuf, front_vga[0], 4 * uiDWordCount);
    memcpy(&pScrBuf[4 * uiDWordCount], &pWinnerImage->iWidth + uiDWordCount, byBufSizeLow & 3);

    DrawCar(scrbuf + 73600, carDesign, 2200.0, 512, byAnimFrame);
    front_text(front_vga[1], driver_names[result_order[0]], font3_ascii, font3_offsets, 320, 120, 0x8Fu, 1u);
    copypic(scrbuf, screen);
    if ( !front_fade )
    {
      front_fade = -1;
      fade_palette(32);
      frames = 0;
    }
    while ( fatkbhit() )
    {
      if ( !(uint8)fatgetch() )
        fatgetch();
      iExit = -1;
      iRetVal = 0;
      UpdateSDL();
    }
    if ( ticks > iDisplayDuration )
      iExit = -1;
    nNewYaw = Car[0].nYaw + 32 * frames;
    nNewYaw &= 0x3FFF;
    Car[0].nYaw = nNewYaw;
    frames = 0;
    UpdateSDL();
  }
  while ( !iExit );

  // cleanup
  ppFrontVgaItr = front_vga;
  do
  {
    ppFrePtr = (void **)ppFrontVgaItr++;
    fre(ppFrePtr);
  }
  while ( ppFrontVgaItr != &front_vga[16] );
  ppCartexItr = cartex_vga;
  do
  {
    ppFrePtr_1 = (void **)ppCartexItr++;
    fre(ppFrePtr_1);
  }
  while ( ppCartexItr != &cartex_vga[16] );
  remove_mapsels();
  gfx_size = iOldGfxSize;
  if ( !iRetVal )
  {
    fade_palette(0);
    front_fade = 0;
  }
  return iRetVal;
}

//-------------------------------------------------------------------------------------------------
//000563E0
void StoreResult()
{
  int iResultP2Pos; // edi
  int iFastestLap; // ebp
  int iMaxOffset_1; // esi
  unsigned int iOffset; // ebx
  int iCarIdx; // edx
  double dResultTime; // st7
  uint8 byCarDesignIdx; // al
  int iResultP1Pos; // esi
  int iCarOrderIdx; // edx
  int iCarResult; // eax
  int iCarOrderIdx_1; // eax
  int iCarResult_1; // edx

  iResultP2Pos = result_p2_pos;

  for (int i = 0; i < numcars; ++i) {
    result_order[i] = carorder[i];
    result_control[i] = human_control[i];
    result_competing[i] = non_competitors[i];
  }
  //if (numcars > 0) {
  //  iMaxOffset = 4 * numcars;
  //  iResultOffset = 0;
  //  do {
  //    iResultOffset += 4;
  //    // offsets into adjacent data
  //    result_lap[iResultOffset / 4 + 15] = nearcall[iResultOffset / 4 + 15];
  //    result_competing[iResultOffset / 4 + 15] = team_wins[iResultOffset / 4 + 15];
  //    LODWORD(result_best[iResultOffset / 4 + 15]) = TrackArrow_variable_1[iResultOffset / 4];
  //  } while ((int)iResultOffset < iMaxOffset);
  //}

  iFastestLap = -1;
  BestTime = 100000000.0;
  if (racers > 0) {
    iMaxOffset_1 = 4 * racers;
    iOffset = 0;
    do {
      iCarIdx = result_order[iOffset / 4];
      if (fabs(Car[iCarIdx].fBestLapTime) > FLT_EPSILON && Car[iCarIdx].fBestLapTime < (double)BestTime) {
        iFastestLap = result_order[iOffset / 4];
        BestTime = Car[iCarIdx].fBestLapTime;
      }
      result_best[iCarIdx] = Car[iCarIdx].fBestLapTime;
      dResultTime = Car[iCarIdx].fTotalRaceTime;
      result_kills[iCarIdx] = Car[iCarIdx].byKills;
      result_lap[iCarIdx] = (char)Car[iCarIdx].byLap;
      result_lives[iCarIdx] = (char)Car[iCarIdx].byLives;
      byCarDesignIdx = Car[iCarIdx].byCarDesignIdx;
      result_time[iCarIdx] = (float)dResultTime;
      iOffset += 4;
      result_design[iCarIdx] = byCarDesignIdx;
    } while ((int)iOffset < iMaxOffset_1);
  }
  iResultP1Pos = 0;
  iCarOrderIdx = 0;
  if (carorder[0] != result_p1) {
    do {
      iCarResult = carorder[++iCarOrderIdx];
      ++iResultP1Pos;
    } while (iCarResult != result_p1);
  }
  if (player_type == 2) {
    iResultP2Pos = 0;
    iCarOrderIdx_1 = 0;
    if (carorder[0] != result_p2) {
      do {
        iCarResult_1 = carorder[++iCarOrderIdx_1];
        ++iResultP2Pos;
      } while (iCarResult_1 != result_p2);
    }
  }
  FastestLap = iFastestLap;
  result_p2_pos = iResultP2Pos;
  result_p1_pos = iResultP1Pos;
}

//-------------------------------------------------------------------------------------------------
//00056570
void RaceResult()
{
  uint8 *pbyScreenBuffer; // edi
  tBlockHeader *pResultBitmap; // esi
  unsigned int uiScreenSize; // ecx
  char byRemainder; // al
  unsigned int uiDwordCount; // ecx
  int iBytesToCopy; // ebx
  unsigned int uiLoopCounter; // eax
  int iCarDesign; // ebp
  int iKillIconX; // esi
  int iKillIcon; // edi
  int iTotalPoints; // eax
  double dTimeDifference; // st7
  int iTotalRacePoints; // eax
  int iFinishedCount; // [esp+0h] [ebp-4Ch]
  int iSavedScreenSize; // [esp+4h] [ebp-48h]
  float fWinnerTime; // [esp+8h] [ebp-44h]
  char *pszPositionText; // [esp+Ch] [ebp-40h]
  int iY; // [esp+10h] [ebp-3Ch]
  int iTextY; // [esp+14h] [ebp-38h]
  int iCarY; // [esp+1Ch] [ebp-30h]
  int iCurrentRacer; // [esp+20h] [ebp-2Ch]
  int iDriverIndex; // [esp+24h] [ebp-28h]
  int iTextBaseY; // [esp+28h] [ebp-24h]
  int iTimeCentiseconds; // [esp+2Ch] [ebp-20h]
  int iTimeWork; // [esp+2Ch] [ebp-20h]

  // init
  fWinnerTime = 0; //added by ROLLER
  tick_on = 0;
  iSavedScreenSize = scr_size;
  SVGA_ON = -1;
  init_screen();
  setpal("result.pal");
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load resources
  front_vga[2] = (tBlockHeader *)load_picture("result.bm");
  front_vga[3] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[0] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[1] = (tBlockHeader *)load_picture("tabtext.bm");

  // Enable frontend mode and timer
  frontend_on = -1;
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pResultBitmap = front_vga[2];

  // Copy result background bitmap to screen buffer (SVGA or VGA size)
  if (SVGA_ON)
    uiScreenSize = 256000;
  else
    uiScreenSize = 64000;
  byRemainder = uiScreenSize;
  uiDwordCount = uiScreenSize >> 2;
  memcpy(scrbuf, front_vga[2], 4 * uiDwordCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCount], &pResultBitmap->iWidth + uiDwordCount, byRemainder & 3);

  // Display "Result" header text
  iTextBaseY = 49;
  display_block(scrbuf, front_vga[1], 0, 130, 3, -1);

  if (numcars > 0) {
    iBytesToCopy = 4 * numcars;
    uiLoopCounter = 0;
    do {
      non_competitors[uiLoopCounter / 4] = result_competing[uiLoopCounter / 4];
      uiLoopCounter += 4;
      //TrackArrow_variable_1[uiLoopCounter / 4] = LODWORD(result_best[uiLoopCounter / 4 + 15]);// non_competitors[uiLoopCounter / 4] = result_competing[uiLoopCounter / 4];
    } while ((int)uiLoopCounter < iBytesToCopy);
  }
  iFinishedCount = 0;

  // display each racer's results
  if (racers > 0) {
    iY = 44;                                    // Initialize Y positions and text pointer for racer display
    iTextY = 45;
    iCurrentRacer = 0;
    pszPositionText = race_posn[0];
    iCarY = 46;
    do {
      iDriverIndex = result_order[iCurrentRacer];
      if (result_control[iDriverIndex])       // Show small human player icon if this racer is human controlled
        display_block(scrbuf, front_vga[0], 0, 13, iY, 0);
      sprintf(buffer, "%s", pszPositionText);   // Display position text (1st, 2nd, etc.)
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 33, iTextBaseY, 0x8Fu, 0);
      sprintf(buffer, "%s", driver_names[iDriverIndex]);// Display driver name
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 85, iTextBaseY, 0x8Fu, 0);
      sprintf(buffer, "%s", CompanyNames[result_design[iDriverIndex]]);// Display car manufacturer name
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 218, iTextBaseY, 0x8Fu, 0);
      iCarDesign = result_design[iDriverIndex];

      // Display car sprite or CHEAT text
      if (iCarDesign >= 8) {
        front_text(front_vga[3], "CHEAT", font2_ascii, font2_offsets, 165, iTextBaseY, 0x8Fu, 0);
      } else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
        display_block(scrbuf, front_vga[0], smallcars[1][iCarDesign], 165, iCarY, 0);
      } else {
        display_block(scrbuf, front_vga[0], smallcars[0][iCarDesign], 165, iCarY, 0);
      }

      // Display kill count: either as number (>3) or individual icons (<=3)
      if (result_kills[iDriverIndex] > 3) {
        display_block(scrbuf, front_vga[0], 9, 356, iTextY, 0);
        sprintf(buffer, "%i", result_kills[iDriverIndex]);
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 376, iTextBaseY, 0x8Fu, 0);
      } else {
        iKillIconX = 356;
        for (iKillIcon = 0; iKillIcon < result_kills[iDriverIndex]; iKillIconX += 18) {
          ++iKillIcon;
          display_block(scrbuf, front_vga[0], 9, iKillIconX, iTextY, 0);
        }
      }

      // Show fastest lap icon if this driver achieved fastest lap
      if (iDriverIndex == FastestLap && FastestLap >= 0)
        display_block(scrbuf, front_vga[0], 10, 428, iCarY, 0);

      // Display race time or lap status if racer is still alive
      if (result_lives[iDriverIndex] > 0) {
        iTotalPoints = result_lap[iDriverIndex];
        if (iTotalPoints > NoOfLaps) {
          // Format and display race time (MM:SS:CS format)
          if (iFinishedCount) {
            dTimeDifference = result_time[iDriverIndex] - fWinnerTime;
          } else {
            fWinnerTime = result_time[iDriverIndex];
            dTimeDifference = fWinnerTime;
          }
          //_CHP();
          iTimeCentiseconds = (int)(dTimeDifference * 100.0);
          if (iTimeCentiseconds > 599999)
            iTimeCentiseconds = 599999;
          buffer[1] = iTimeCentiseconds % 10 + 48;
          iTimeWork = iTimeCentiseconds / 10;
          buffer[0] = iTimeWork % 10 + 48;
          buffer[2] = 0;
          front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 492, iTextBaseY, 0x8Fu, 0);
          front_text(front_vga[3], ":", font2_ascii, font2_offsets, 467, iTextBaseY, 0x8Fu, 0);
          iTimeWork /= 10;
          buffer[1] = iTimeWork % 10 + 48;
          iTimeWork /= 10;
          buffer[0] = iTimeWork % 6 + 48;
          buffer[2] = 0;
          front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 471, iTextBaseY, 0x8Fu, 0);
          front_text(front_vga[3], ":", font2_ascii, font2_offsets, 488, iTextBaseY, 0x8Fu, 0);
          iTimeWork /= 6;
          buffer[1] = iTimeWork % 10 + 48;
          buffer[0] = iTimeWork / 10 % 10 + 48;
          buffer[2] = 0;
          front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 450, iTextBaseY, 0x8Fu, 0);
        } else {
          if (iTotalPoints == NoOfLaps)
            sprintf(buffer, "1 %s", &language_buffer[256]);
          else
            sprintf(buffer, "%i %s", NoOfLaps - result_lap[iDriverIndex] + 1, &language_buffer[320]);
          front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 450, iTextBaseY, 0x8Fu, 0);
        }
      } else {
        front_text(front_vga[3], &language_buffer[1920], font2_ascii, font2_offsets, 450, iTextBaseY, 0x8Fu, 0);
      }

      // Calculate total points (position + kills + fastest lap bonus)
      if (iDriverIndex == FastestLap)
        iTotalRacePoints = result_kills[iDriverIndex] + points[iCurrentRacer] + 1;
      else
        iTotalRacePoints = result_kills[iDriverIndex] + points[iCurrentRacer];
      sprintf(buffer, "%2i", iTotalRacePoints);
      pszPositionText += 5;
      ++iCurrentRacer;
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 560, iTextBaseY, 0x8Fu, 0);

      // Move to next racer: increment Y positions and text pointer
      iTextBaseY += 22;
      iY += 22;
      iTextY += 22;
      iCarY += 22;
      ++iFinishedCount;
    } while (iFinishedCount < racers);
  }

  // Display completed result screen and wait for input
  copypic(scrbuf, screen);
  fade_palette(32);
  ticks = 0;
  while (!fatkbhit() && ticks < 2160)
    UpdateSDL();

  // cleanup
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  scr_size = iSavedScreenSize;
  holdmusic = -1;
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//00056D60
void TimeTrials(int iDriverIdx)
{
  uint8 *pbyScreenBuffer; // edi
  tBlockHeader *pResultBitmap; // esi
  unsigned int uiScreenSize; // ecx
  char byRemainder; // al
  unsigned int uiDwordCount; // ecx
  int iCarIdx; // edi
  int iDesignIdx; // ebp
  int iCarDesign; // ecx
  double dBestTime; // st7
  int iLapTextY; // esi
  int iLapNumber; // ebp
  int iTimeOffset; // edi
  double dLapTime; // st7
  int iRecordTextY; // esi
  int iRecordCar; // edi
  double dRecordTime; // st7
  int iFastestDriver; // edi
  int iFastestTextY; // esi
  int iFastestDriverCopy; // edi
  int iFastestCarDesign; // ebp
  double dFastestTime; // st7
  int iRecordHeaderY; // [esp-Ch] [ebp-40h]
  int iSavedScreenSize; // [esp+0h] [ebp-34h]
  int iRecordCentiseconds; // [esp+4h] [ebp-30h]
  int iRecordTimeWork; // [esp+4h] [ebp-30h]
  int iFastestCentiseconds; // [esp+8h] [ebp-2Ch]
  int iFastestTimeWork; // [esp+8h] [ebp-2Ch]
  int iBestCentiseconds; // [esp+Ch] [ebp-28h]
  int iBestTimeWork; // [esp+Ch] [ebp-28h]
  int iCarIndex; // [esp+10h] [ebp-24h]
  int iY; // [esp+14h] [ebp-20h]
  int iLapCentiseconds; // [esp+18h] [ebp-1Ch]
  int iLapTimeWork; // [esp+18h] [ebp-1Ch]

  // init
  tick_on = 0;
  iSavedScreenSize = scr_size;
  SVGA_ON = -1;
  init_screen();
  setpal("result.pal");
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load graphics
  front_vga[3] = (tBlockHeader *)load_picture("result.bm");
  front_vga[2] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[0] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[1] = (tBlockHeader *)load_picture("tabtext.bm");

  frontend_on = -1;
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pResultBitmap = front_vga[3];

  // Copy result background bitmap to screen buffer (SVGA or VGA size)
  if (SVGA_ON)
    uiScreenSize = 256000;
  else
    uiScreenSize = 64000;
  byRemainder = uiScreenSize;
  uiDwordCount = uiScreenSize >> 2;
  memcpy(scrbuf, front_vga[3], 4 * uiDwordCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCount], &pResultBitmap->iWidth + uiDwordCount, byRemainder & 3);

  // Display "Time Trials" header text
  display_block(scrbuf, front_vga[1], 4, 157, 5, -1);

  // Display current driver info: name, company, car sprite
  sprintf(buffer, "%s", driver_names[iDriverIdx]);
  iCarIdx = iDriverIdx;
  iDesignIdx = iDriverIdx;
  front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 85, 49, 0x8Fu, 0);
  sprintf(buffer, "%s", CompanyNames[result_design[iDesignIdx]]);
  front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 218, 49, 0x8Fu, 0);

  // Display car sprite or CHEAT text
  iCarDesign = result_design[iDesignIdx];
  if (iCarDesign >= 8) {
    front_text(front_vga[2], "CHEAT", font2_ascii, font2_offsets, 165, 49, 0x8Fu, 0);
  } else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
    display_block(scrbuf, front_vga[0], smallcars[1][iCarDesign], 165, 46, 0);
  } else {
    display_block(scrbuf, front_vga[0], smallcars[0][iCarDesign], 165, 46, 0);
  }

  // Format and display driver's best time in MM:SS:CS format
  dBestTime = Car[iCarIdx].fBestLapTime * 100.0;
  //_CHP();
  iBestCentiseconds = (int)dBestTime;
  if ((int)dBestTime > 100000)
    iBestCentiseconds = 0;
  buffer[1] = iBestCentiseconds % 10 + 48;
  iBestTimeWork = iBestCentiseconds / 10;
  buffer[0] = iBestTimeWork % 10 + 48;
  buffer[2] = 0;
  front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 492, 49, 0x8Fu, 0);
  front_text(front_vga[2], ":", font2_ascii, font2_offsets, 467, 49, 0x8Fu, 0);
  iBestTimeWork /= 10;
  buffer[1] = iBestTimeWork % 10 + 48;
  iBestTimeWork /= 10;
  buffer[0] = iBestTimeWork % 6 + 48;
  buffer[2] = 0;
  front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 471, 49, 0x8Fu, 0);
  front_text(front_vga[2], ":", font2_ascii, font2_offsets, 488, 49, 0x8Fu, 0);
  iBestTimeWork /= 6;
  buffer[1] = iBestTimeWork % 10 + 48;
  buffer[0] = iBestTimeWork / 10 % 10 + 48;
  buffer[2] = 0;
  front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 450, 49, 0x8Fu, 0);

  // Initialize loop variables for displaying individual lap times
  iLapTextY = 93;
  iLapNumber = 1;
  iCarIndex = iCarIdx;
  iY = 90;
  iTimeOffset = 24 * iCarIdx + 4;

  // Loop through each completed lap and display lap number and time
  while (iLapNumber < (char)Car[iCarIndex].byLap) {
    sprintf(buffer, "%s %i", &language_buffer[256], iLapNumber);// Display lap number text
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 220, iLapTextY, 0x8Fu, 0);

    // Calculate the array index instead of byte offset
    int iTrialIndex = (iTimeOffset - 4) / 4;  // Convert byte offset back to array index
    if (trial_times[iTrialIndex] == Car[iCarIndex].fBestLapTime)  // Show fastest lap icon if this lap matches best time
      display_block(scrbuf, front_vga[0], 10, 428, iY, 0);
    dLapTime = trial_times[iTrialIndex] * 100.0;  // Format and display lap time in MM:SS:CS format
    //if (*(float *)((char *)trial_times + iTimeOffset) == Car[iCarIndex].fResultBestTime)// Show fastest lap icon if this lap matches best time
    //  display_block(scrbuf, front_vga[0], 10, 428, iY, 0);
    //dLapTime = *(float *)((char *)trial_times + iTimeOffset) * 100.0;// Format and display lap time in MM:SS:CS format
    //_CHP();

    iLapCentiseconds = (int)dLapTime;
    if ((int)dLapTime > 100000)
      iLapCentiseconds = 0;
    buffer[1] = iLapCentiseconds % 10 + 48;
    iLapTimeWork = iLapCentiseconds / 10;
    buffer[0] = iLapTimeWork % 10 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 492, iLapTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 467, iLapTextY, 0x8Fu, 0);
    iLapTimeWork /= 10;
    buffer[1] = iLapTimeWork % 10 + 48;
    iLapTimeWork /= 10;
    buffer[0] = iLapTimeWork % 6 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 471, iLapTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 488, iLapTextY, 0x8Fu, 0);
    iLapTimeWork /= 6;
    buffer[1] = iLapTimeWork % 10 + 48;
    buffer[0] = iLapTimeWork / 10 % 10 + 48;
    buffer[2] = 0;
    iTimeOffset += 4;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 450, iLapTextY, 0x8Fu, 0);
    ++iLapNumber;
    iLapTextY += 22;
    iY += 22;
  }

  // Display track record section header
  iRecordHeaderY = iLapTextY + 44;
  iRecordTextY = iLapTextY + 66;
  front_text(front_vga[2], &language_buffer[2752], font2_ascii, font2_offsets, 218, iRecordHeaderY, 0x8Fu, 0);

  // Check if track record exists and display record holder info
  iRecordCar = RecordCars[TrackLoad];
  if (iRecordCar < 0) {
    // Handle case where no track record exists
    sprintf(buffer, "%s", RecordNames[TrackLoad]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 165, iRecordTextY, 0x8Fu, 0);
    front_text(front_vga[2], "00:00:00", font2_ascii, font2_offsets, 450, iRecordTextY, 0x8Fu, 0);
  } else {
    // Display track record holder: name, company, car, and time
    sprintf(buffer, "%s", RecordNames[TrackLoad]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 85, iRecordTextY, 0x8Fu, 0);
    sprintf(buffer, "%s", CompanyNames[iRecordCar & 0xF]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 218, iRecordTextY, 0x8Fu, 0);
    if ((iRecordCar & 0xFu) >= 8)
      front_text(front_vga[2], "CHEAT", font2_ascii, font2_offsets, 165, iRecordTextY, 0x8Fu, 0);
    else
      display_block(scrbuf, front_vga[0], smallcars[0][iRecordCar], 165, iRecordTextY - 3, 0);
    dRecordTime = RecordLaps[TrackLoad] * 100.0;
    //_CHP();
    iRecordCentiseconds = (int)dRecordTime;
    if ((int)dRecordTime > 100000)
      iRecordCentiseconds = 0;
    buffer[1] = iRecordCentiseconds % 10 + 48;
    iRecordTimeWork = iRecordCentiseconds / 10;
    buffer[0] = iRecordTimeWork % 10 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 492, iRecordTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 467, iRecordTextY, 0x8Fu, 0);
    iRecordTimeWork /= 10;
    buffer[1] = iRecordTimeWork % 10 + 48;
    iRecordTimeWork /= 10;
    buffer[0] = iRecordTimeWork % 6 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 471, iRecordTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 488, iRecordTextY, 0x8Fu, 0);
    iRecordTimeWork /= 6;
    buffer[1] = iRecordTimeWork % 10 + 48;
    buffer[2] = 0;
    buffer[0] = iRecordTimeWork / 10 % 10 + 48;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 450, iRecordTextY, 0x8Fu, 0);
  }
  front_text(front_vga[2], &language_buffer[2880], font2_ascii, font2_offsets, 218, iRecordTextY + 44, 0x8Fu, 0);// Display session fastest lap section header
  iFastestDriver = FastestLap;
  iFastestTextY = iRecordTextY + 66;

  // Check if fastest lap exists and display fastest lap holder info
  if (FastestLap >= 0) {
    // Display fastest lap holder: name, company, car, and time
    sprintf(buffer, "%s", driver_names[FastestLap]);
    iFastestDriverCopy = iFastestDriver;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 85, iFastestTextY, 0x8Fu, 0);
    sprintf(buffer, "%s", CompanyNames[result_design[iFastestDriverCopy]]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 218, iFastestTextY, 0x8Fu, 0);
    iFastestCarDesign = result_design[iFastestDriverCopy];
    if (iFastestCarDesign >= 8) {
      front_text(front_vga[2], "CHEAT", font2_ascii, font2_offsets, 165, iFastestTextY, 0x8Fu, 0);
    } else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
      display_block(scrbuf, front_vga[0], smallcars[1][iFastestCarDesign], 165, iFastestTextY - 3, 0);
    } else {
      display_block(scrbuf, front_vga[0], smallcars[0][iFastestCarDesign], 165, iFastestTextY - 3, 0);
    }
    dFastestTime = BestTime * 100.0;
    //_CHP();
    iFastestCentiseconds = (int)dFastestTime;
    if ((int)dFastestTime > 100000)
      iFastestCentiseconds = 0;
    buffer[1] = iFastestCentiseconds % 10 + 48;
    iFastestTimeWork = iFastestCentiseconds / 10;
    buffer[2] = 0;
    buffer[0] = iFastestTimeWork % 10 + 48;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 492, iFastestTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 467, iFastestTextY, 0x8Fu, 0);
    iFastestTimeWork /= 10;
    buffer[1] = iFastestTimeWork % 10 + 48;
    iFastestTimeWork /= 10;
    buffer[0] = iFastestTimeWork % 6 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 471, iFastestTextY, 0x8Fu, 0);
    front_text(front_vga[2], ":", font2_ascii, font2_offsets, 488, iFastestTextY, 0x8Fu, 0);
    iFastestTimeWork /= 6;
    buffer[1] = iFastestTimeWork % 10 + 48;
    buffer[0] = iFastestTimeWork / 10 % 10 + 48;
    buffer[2] = 0;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 450, iFastestTextY, 0x8Fu, 0);
  }

  // Display completed screen, start music, and wait for input
  copypic(scrbuf, screen);
  startmusic(leaderboardsong);
  fade_palette(32);
  ticks = 0;
  while (!fatkbhit() && ticks < 2160)
    UpdateSDL();

  // cleanup
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[2]);
  scr_size = iSavedScreenSize;
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//00057AD0
void ChampionshipStandings()
{
  int iSavedScreenSize; // ebp
  uint8 *pbyScreenBuffer; // edi
  tBlockHeader *pResultBitmap; // esi
  unsigned int uiScreenSize; // ecx
  char byRemainder; // al
  unsigned int uiDwordCount; // ecx
  int iPosition; // ebx
  int iOrderIndex; // edx
  int iDriverIndex; // esi
  int iDriverCopy; // eax
  int iKills; // ecx
  int iTotalKills; // edi
  int iFastestLap; // ecx
  int iTotalFasts; // ecx
  int iSortOuter; // esi
  int iSortIndex; // edi
  int iMaxIndex; // ecx
  int iSortInner; // eax
  int iInnerIndex; // edx
  int iSwapTemp; // edx
  int iRacerCount; // eax
  int iTextBaseY; // esi
  int iCarY; // edi
  int iCarDesign; // ebx
  char *pszPositionText; // [esp+4h] [ebp-34h]
  int iDisplayIndex; // [esp+8h] [ebp-30h]
  int iY; // [esp+Ch] [ebp-2Ch]
  int iIconY; // [esp+10h] [ebp-28h]
  int iDisplayCount; // [esp+14h] [ebp-24h]
  int iCurrentDriver; // [esp+1Ch] [ebp-1Ch]

  // init
  tick_on = 0;
  iSavedScreenSize = scr_size;
  SVGA_ON = -1;
  init_screen();
  setpal("result.pal");
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load graphics
  front_vga[3] = (tBlockHeader *)load_picture("result.bm");
  front_vga[2] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[0] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[1] = (tBlockHeader *)load_picture("tabtext.bm");

  frontend_on = -1;
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pResultBitmap = front_vga[3];

  // Copy result background bitmap to screen buffer (SVGA or VGA size)
  if (SVGA_ON)
    uiScreenSize = 256000;
  else
    uiScreenSize = 64000;
  byRemainder = uiScreenSize;
  uiDwordCount = uiScreenSize >> 2;
  memcpy(scrbuf, front_vga[3], 4 * uiDwordCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCount], &pResultBitmap->iWidth + uiDwordCount, byRemainder & 3);

  // Calculate championship points for each racer based on race results
  iPosition = 0;
  if (racers > 0) {
    iOrderIndex = 0;
    do {
      iDriverIndex = result_order[iOrderIndex];
      // Skip points calculation for single race mode (game_type == 3)
      if (game_type != 3) {
        iDriverCopy = iDriverIndex;
        iKills = result_kills[iDriverIndex];    // Add race kills + position points to championship total
        championship_points[iDriverCopy] = iKills + points[iOrderIndex] + championship_points[iDriverIndex];
        iTotalKills = iKills + total_kills[iDriverIndex];// Update total kills counter
        iFastestLap = FastestLap;
        total_kills[iDriverCopy] = iTotalKills;
        if (iDriverIndex == iFastestLap)      // Award bonus point and increment fastest lap count if this driver had fastest lap
        {
          iTotalFasts = total_fasts[iDriverIndex] + 1;
          ++championship_points[iDriverIndex];
          total_fasts[iDriverIndex] = iTotalFasts;
        }
        if (!iPosition)                       // Increment win count for race winner (position 0)
          ++total_wins[iDriverIndex];
      }
      ++iPosition;
      champorder[iOrderIndex++] = iDriverIndex;
    } while (iPosition < racers);
  }

  // Sort racers by championship points (selection sort algorithm)
  iSortOuter = 0;
  if (racers > 0) {
    iSortIndex = 0;
    do {
      iMaxIndex = iSortOuter;                   // Find racer with highest points in remaining unsorted elements
      iSortInner = iSortOuter + 1;
      iSavedScreenSize = championship_points[champorder[iSortIndex]];
      if (iSortOuter + 1 < racers) {
        iInnerIndex = iSortInner;
        do {                                       // Update max if current racer has more points
          if (championship_points[champorder[iInnerIndex]] > iSavedScreenSize) {
            iMaxIndex = iSortInner;
            iSavedScreenSize = championship_points[champorder[iInnerIndex]];
          }
          ++iSortInner;
          ++iInnerIndex;
        } while (iSortInner < racers);
      }
      ++iSortIndex;
      ++iSortOuter;                             // Swap highest scoring racer to correct position
      iSwapTemp = champorder[iSortIndex - 1];
      champorder[iSortIndex - 1] = champorder[iMaxIndex];
      iRacerCount = racers;
      champorder[iMaxIndex] = iSwapTemp;
    } while (iSortOuter < iRacerCount);
  }

  // Display "Championship Standings" header text
  iTextBaseY = 49;
  display_block(scrbuf, front_vga[1], 1, 127, 4, -1);

  // Initialize display loop variables for showing sorted standings
  iDisplayCount = 0;
  if (racers > 0) {
    iCarY = 46;
    iDisplayIndex = 0;
    pszPositionText = race_posn[0];
    iIconY = 45;
    iY = 44;
    do {
      iCurrentDriver = champorder[iDisplayIndex];
      if (result_control[iCurrentDriver])     // Show human player icon if this driver is human controlled
        display_block(scrbuf, front_vga[0], 0, 13, iY, 0);
      sprintf(buffer, "%s", pszPositionText);   // Display position text (1st, 2nd, etc.)
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 33, iTextBaseY, 0x8Fu, 0);
      sprintf(buffer, "%s", driver_names[iCurrentDriver]);// Display driver name
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 85, iTextBaseY, 0x8Fu, 0);
      sprintf(buffer, "%s", CompanyNames[result_design[iCurrentDriver]]);// Display car manufacturer name
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 218, iTextBaseY, 0x8Fu, 0);
      iCarDesign = result_design[iCurrentDriver];

      // Display car sprite or CHEAT text
      if (iCarDesign >= 8) {
        front_text(front_vga[2], "CHEAT", font2_ascii, font2_offsets, 165, iTextBaseY, 0x8Fu, 0);
      } else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
        display_block(scrbuf, front_vga[0], smallcars[1][iCarDesign], 165, iCarY, 0);
      } else {
        display_block(scrbuf, front_vga[0], smallcars[0][iCarDesign], 165, iCarY, 0);
      }
      display_block(scrbuf, front_vga[0], 9, 356, iIconY, 0);// Display total kills icon and count
      sprintf(buffer, "%i", total_kills[iCurrentDriver]);
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 376, iTextBaseY, 0x8Fu, 0);
      display_block(scrbuf, front_vga[0], 11, 475, iCarY, 0);// Display total wins icon and count
      sprintf(buffer, "%i", total_wins[iCurrentDriver]);
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 500, iTextBaseY, 0x8Fu, 0);
      display_block(scrbuf, front_vga[0], 10, 428, iCarY, 0);// Display total fastest laps icon and count
      sprintf(buffer, "%i", total_fasts[iCurrentDriver]);
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 448, iTextBaseY, 0x8Fu, 0);
      sprintf(buffer, "%3i", championship_points[iCurrentDriver]);// Display total championship points

      // Move to next driver: increment Y positions and text pointer
      iCarY += 22;
      front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 560, iTextBaseY, 0x8Fu, 0);
      iTextBaseY += 22;
      iY += 22;
      iIconY += 22;
      ++iDisplayIndex;
      pszPositionText += 5;
      ++iDisplayCount;
    } while (iDisplayCount < racers);
  }

  // Display completed screen and wait for input
  copypic(scrbuf, screen);
  holdmusic = -1;
  fade_palette(32);
  ticks = 0;

  // Different wait behavior: single race mode waits indefinitely, championship mode waits 2160 ticks
  if (game_type == 3) {
    while (!fatkbhit())
      UpdateSDL();
  } else {
    while (!fatkbhit() && ticks < 2160)
      UpdateSDL();
  }

  // cleanup
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[2]);
  scr_size = iSavedScreenSize;
  holdmusic = -1;
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//00058100
void TeamStandings()
{
  uint8 *pScrBuf; // edi
  tBlockHeader *pResultImage; // esi
  unsigned int uiBufSize; // ecx
  char uiBufSizeLow; // al
  unsigned int uiDWordCount; // ecx
  int iRacerIdx; // edx
  int iResultIdx; // ebx
  int iDriverIdx; // ebp
  int iCarDesignIdx; // esi
  int iTeamIdx; // eax
  int iFastestLapDrvr; // ecx
  int iTeamFastLaps; // ebp
  int iTeamOrderIdx; // eax
  int iInitIdx; // edx
  int iCurrTeamIdx; // edi
  int iSortIdx; // ebp
  int iCompareIdx; // ecx
  int iNextIdx; // eax
  int iBestPoints; // esi
  int iCompareTeamIdx; // edx
  int iBestTeamIdx; // edx
  int iDisplayY; // edi
  int iTeamDisplayIdx; // ebp
  int iDisplayTeamIdx; // edi
  int iBestTeamIdx_1; // ebp
  char *szPosition; // [esp+8h] [ebp-28h]
  int iTeamIter; // [esp+Ch] [ebp-24h]
  int iCarY; // [esp+10h] [ebp-20h]

  // init screen
  tick_on = 0;
  SVGA_ON = -1;
  init_screen();
  setpal("result.pal");
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load graphics
  front_vga[3] = (tBlockHeader *)load_picture("result.bm");
  front_vga[2] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[0] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[1] = (tBlockHeader *)load_picture("tabtext.bm");

  frontend_on = -1;
  tick_on = -1;
  pScrBuf = scrbuf;
  pResultImage = front_vga[3];
  if ( SVGA_ON )
    uiBufSize = 256000;
  else
    uiBufSize = 64000;
  uiBufSizeLow = uiBufSize;
  uiDWordCount = uiBufSize >> 2;
  memcpy(scrbuf, front_vga[3], 4 * uiDWordCount);
  memcpy(&pScrBuf[4 * uiDWordCount], &pResultImage->iWidth + uiDWordCount, uiBufSizeLow & 3);

  // calculate team stats
  if ( game_type != 3 )
  {
    iRacerIdx = 0;
    if ( racers > 0 )
    {
      iResultIdx = 0;
      do
      {
        iDriverIdx = result_order[iResultIdx];
        iCarDesignIdx = result_design[iDriverIdx];
        if ( iCarDesignIdx < 8 )
        {
          iTeamIdx = iCarDesignIdx;
          team_points[iTeamIdx] += result_kills[iDriverIdx] + points[iResultIdx];
          iFastestLapDrvr = FastestLap;
          team_kills[iTeamIdx] = result_kills[iDriverIdx] + team_kills[iCarDesignIdx];
          if ( iDriverIdx == iFastestLapDrvr )
          {
            iTeamFastLaps = team_fasts[iCarDesignIdx] + 1;
            ++team_points[iCarDesignIdx];
            team_fasts[iCarDesignIdx] = iTeamFastLaps;
          }
          if ( !iRacerIdx )
            ++team_wins[iCarDesignIdx];
        }
        ++iRacerIdx;
        ++iResultIdx;
      }
      while ( iRacerIdx < racers );
    }
  }

  // init team order ay
  iTeamOrderIdx = 0;
  iInitIdx = 0;
  do
    teamorder[iInitIdx++] = iTeamOrderIdx++;
  while ( iTeamOrderIdx < 8 );

  // Sort teams by points
  iCurrTeamIdx = 0;
  iSortIdx = 0;
  do
  {
    iCompareIdx = iCurrTeamIdx;
    iNextIdx = iCurrTeamIdx + 1;
    iBestPoints = team_points[teamorder[iSortIdx]];
    if ( iCurrTeamIdx + 1 < 8 )
    {
      iCompareTeamIdx = iNextIdx;
      do
      {
        if ( team_points[teamorder[iCompareTeamIdx]] > iBestPoints )
        {
          iCompareIdx = iNextIdx;
          iBestPoints = team_points[teamorder[iCompareTeamIdx]];
        }
        ++iNextIdx;
        ++iCompareTeamIdx;
      }
      while ( iNextIdx < 8 );
    }

    iBestTeamIdx = teamorder[iSortIdx];
    teamorder[iSortIdx] = teamorder[iCompareIdx];
    ++iSortIdx;
    //iBestTeamIdx = DeathView_variable_1[++iSortIdx];// offset into teamorder
    //DeathView_variable_1[iSortIdx] = teamorder[iCompareIdx];

    ++iCurrTeamIdx;
    teamorder[iCompareIdx] = iBestTeamIdx;
  }
  while ( iCurrTeamIdx < 8 );

  // Display team standings table
  iDisplayY = 49;
  display_block(scrbuf, front_vga[1], 2, 157, 3, -1);// header
  iTeamIter = 0;
  szPosition = race_posn[0];
  do
  {
    iTeamDisplayIdx = teamorder[iTeamIter];

    // display human player indicator if this team has huma players
    if ( result_control[2 * iTeamDisplayIdx + 1] || result_control[2 * iTeamDisplayIdx] )
      display_block(scrbuf, front_vga[0], 0, 13, iDisplayY - 5, 0);

    // Display team position
    sprintf(buffer, (uint8 *)"%s", szPosition);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 33, iDisplayY, 0x8Fu, 0);

    // Display team name
    sprintf(buffer, (uint8 *)"%s", CompanyNames[iTeamDisplayIdx]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 190, iDisplayY, 0x8Fu, 0);

    // display car sprites
    iCarY = iDisplayY - 3;
    if ( (textures_off & TEX_OFF_ADVANCED_CARS) != 0 )
    {
      display_block(scrbuf, front_vga[0], smallcars[1][iTeamDisplayIdx], 340, iCarY, 0);
      display_block(scrbuf, front_vga[0], smallcars[1][iTeamDisplayIdx], 100, iCarY, 0);
    }
    else
    {
      display_block(scrbuf, front_vga[0], smallcars[0][iTeamDisplayIdx], 340, iCarY, 0);
      display_block(scrbuf, front_vga[0], smallcars[0][iTeamDisplayIdx], 100, iCarY, 0);
    }

    // display wins icon and count
    display_block(scrbuf, front_vga[0], 11, 475, iDisplayY - 3, 0);
    sprintf(buffer, (uint8 *)"%i", team_wins[iTeamDisplayIdx]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 500, iDisplayY, 0x8Fu, 0);

    // display fast laps icon and count
    display_block(scrbuf, front_vga[0], 10, 428, iDisplayY - 3, 0);
    sprintf(buffer, (uint8 *)"%i", team_fasts[iTeamDisplayIdx]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 448, iDisplayY, 0x8Fu, 0);

    // display total points
    sprintf(buffer, (uint8 *)"%3i", team_points[iTeamDisplayIdx]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 560, iDisplayY, 0x8Fu, 0);

    // display kills icon and count (line 2)
    display_block(scrbuf, front_vga[0], 9, 428, iDisplayY + 18, 0);
    sprintf(buffer, (uint8 *)"%i", team_kills[iTeamDisplayIdx]);
    iDisplayTeamIdx = iDisplayY + 22;

    // Display both team driver names
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 448, iDisplayTeamIdx, 0x8Fu, 0);
    sprintf(buffer, (uint8 *)"%s", driver_names[2 * iTeamDisplayIdx]);
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 110, iDisplayTeamIdx, 0x8Fu, 0);

    sprintf(buffer, (uint8 *)"%s", driver_names[2 * iTeamDisplayIdx + 1]);
    iBestTeamIdx_1 = 2 * iTeamDisplayIdx;
    front_text(front_vga[2], buffer, font2_ascii, font2_offsets, 310, iDisplayTeamIdx, 0x8Fu, 0);

    // display human player indicator for driver names if applicable
    if ( result_control[iBestTeamIdx_1 + 1] || result_control[iBestTeamIdx_1] )
      display_block(scrbuf, front_vga[0], 0, 13, iDisplayTeamIdx - 5, 0);

    // move to next team display pos
    iDisplayY = iDisplayTeamIdx + 22;
    ++iTeamIter;
    szPosition += 5;
  }
  while ( iTeamIter != 8 );

  // display completed standings screen
  copypic(scrbuf, screen);
  holdmusic = -1;
  fade_palette(32);
  ticks = 0;

  // wait for user input or timeout
  if ( game_type == 3 )
  {
    while ( !fatkbhit() )
      UpdateSDL();
  }
  else
  {
    while ( !fatkbhit() && ticks < 2160 )
      UpdateSDL();
  }

  // cleanup
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[2]);
  scr_size = iBestPoints;
  holdmusic = -1;
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//00058780
void ShowLapRecords()
{
  uint8 *pScrBuf; // edi
  tBlockHeader *pBlockHeader; // esi
  unsigned int uiMemSize; // ecx
  char byMemSize; // al
  unsigned int uiDwordCount; // ecx
  int iTextY; // esi
  int iRecordIdx; // ebp
  int iArrayIdx; // edi
  double dLapTime; // st7
  uint8 *pScrBuf_1; // edi
  tBlockHeader *pBlockHeader_1; // esi
  unsigned int uiMemSize_1; // ecx
  char byMemSize_1; // al
  unsigned int uiDwordCount_1; // ecx
  int iTextY_1; // esi
  int iRecordIdx_1; // ebp
  int iArrayIdx_1; // edi
  double dLapTime_1; // st7
  int iCarY; // [esp+4h] [ebp-44h]
  int iOldScrSize; // [esp+8h] [ebp-40h]
  int iKillIconY_1; // [esp+Ch] [ebp-3Ch]
  int iKillIconY; // [esp+10h] [ebp-38h]
  int iCarY2; // [esp+14h] [ebp-34h]
  int iCarType; // [esp+18h] [ebp-30h]
  int iCarType_1; // [esp+1Ch] [ebp-2Ch]
  const char *szRecordName; // [esp+20h] [ebp-28h]
  const char *szRecordName_1; // [esp+24h] [ebp-24h]
  int iTimeValue_1; // [esp+28h] [ebp-20h]
  int iTimeTemp_1; // [esp+28h] [ebp-20h]
  int iTimeValue; // [esp+2Ch] [ebp-1Ch]
  int iTimeTemp; // [esp+2Ch] [ebp-1Ch]

  // init display settings
  holdmusic = -1;
  tick_on = 0;
  SVGA_ON = -1;
  iOldScrSize = scr_size;
  init_screen();
  setpal("result.pal");

  // set up window params
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load graphics resources
  front_vga[2] = (tBlockHeader *)load_picture("result.bm");
  front_vga[3] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[0] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[1] = (tBlockHeader *)load_picture("tabtext.bm");

  frontend_on = -1;
  tick_on = -1;

  // copy background to screen buffer
  pScrBuf = scrbuf;
  pBlockHeader = front_vga[2];
  if ( SVGA_ON )
    uiMemSize = 256000;
  else
    uiMemSize = 64000;
  byMemSize = uiMemSize;
  uiDwordCount = uiMemSize >> 2;
  memcpy(scrbuf, front_vga[2], 4 * uiDwordCount);
  memcpy(&pScrBuf[4 * uiDwordCount], &pBlockHeader->iWidth + uiDwordCount, byMemSize & 3);

  // Display records
  iTextY = 49;
  iRecordIdx = 1;
  display_block(scrbuf, front_vga[1], 3, 142, 2, -1);
  iArrayIdx = 1;
  szRecordName = RecordNames[1];
  iCarY = 46;
  iKillIconY = 45;

  do
  {
    // display record number
    sprintf(buffer, (uint8 *)"%02i", iRecordIdx);
    front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 33, iTextY, 0x8Fu, 0);

    iCarType = RecordCars[iArrayIdx];
    if ( iCarType < 0 )
    {
      // no record set - display default name and time
      sprintf(buffer, (uint8 *)"%s", szRecordName);
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 165, iTextY, 0x8Fu, 0);
      front_text(front_vga[3], "00:00:00", font2_ascii, font2_offsets, 450, iTextY, 0x8Fu, 0);
    }
    else
    {
      // display record holder name
      sprintf(buffer, (uint8 *)"%s", szRecordName);
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 85, iTextY, 0x8Fu, 0);

      // display car company name
      sprintf(buffer, (uint8 *)"%s", CompanyNames[iCarType & 0xF]);
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 218, iTextY, 0x8Fu, 0);

      // display car
      if ( (iCarType & 0xFu) >= 8 )
        front_text(front_vga[3], "CHEAT", font2_ascii, font2_offsets, 165, iTextY, 0x8Fu, 0);
      else
        display_block(scrbuf, front_vga[0], smallcars[0][iCarType], 165, iCarY, 0);

      // display kill count icon and number
      display_block(scrbuf, front_vga[0], 9, 540, iKillIconY, 0);
      sprintf(buffer, (uint8 *)"%i", RecordKills[iArrayIdx]);
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 560, iTextY, 0x8Fu, 0);

      // display lap time
      dLapTime = RecordLaps[iArrayIdx] * 100.0;
      //_CHP();
      iTimeValue = (int)dLapTime;
      if ( (int)dLapTime > 599999 )
        iTimeValue = 599999;

      // display centiseconds
      buffer[1] = iTimeValue % 10 + 48;
      iTimeTemp = iTimeValue / 10;
      buffer[0] = iTimeTemp % 10 + 48;
      buffer[2] = 0;
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 492, iTextY, 0x8Fu, 0);
      front_text(front_vga[3], ":", font2_ascii, font2_offsets, 467, iTextY, 0x8Fu, 0);

      // display seconds
      iTimeTemp /= 10;
      buffer[1] = iTimeTemp % 10 + 48;
      iTimeTemp /= 10;
      buffer[0] = iTimeTemp % 6 + 48;
      buffer[2] = 0;
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 471, iTextY, 0x8Fu, 0);
      front_text(front_vga[3], ":", font2_ascii, font2_offsets, 488, iTextY, 0x8Fu, 0);

      // display minutes
      iTimeTemp /= 6;
      buffer[1] = iTimeTemp % 10 + 48;
      buffer[0] = iTimeTemp / 10 % 10 + 48;
      buffer[2] = 0;
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 450, iTextY, 0x8Fu, 0);
    }

    // move to next record pos
    iTextY += 22;
    ++iArrayIdx;
    ++iRecordIdx;
    iCarY += 22;
    iKillIconY += 22;
    szRecordName += 9;
  }
  while ( iRecordIdx < 17 );

  // display first page and wait for input
  copypic(scrbuf, screen);
  holdmusic = -1;
  fade_palette(32);
  ticks = 0;
  if ( game_type == 4 )
  {
    while ( !fatkbhit() )
      UpdateSDL();
  }
  else
  {
    while ( !fatkbhit() && ticks < 720 )
      UpdateSDL();
  }
  scr_size = iOldScrSize;

  // display second page
  if ( (textures_off & TEX_OFF_BONUS_CUP_AVAILABLE) != 0 )
  {
    holdmusic = -1;
    fade_palette(0);

    // copy background to screen buffer
    pScrBuf_1 = scrbuf;
    pBlockHeader_1 = front_vga[2];
    if ( SVGA_ON )
      uiMemSize_1 = 256000;
    else
      uiMemSize_1 = 64000;
    byMemSize_1 = uiMemSize_1;
    uiDwordCount_1 = uiMemSize_1 >> 2;
    memcpy(scrbuf, front_vga[2], 4 * uiDwordCount_1);
    memcpy(&pScrBuf_1[4 * uiDwordCount_1], &pBlockHeader_1->iWidth + uiDwordCount_1, byMemSize_1 & 3);

    // display records 17-24
    iTextY_1 = 49;
    iRecordIdx_1 = 17;
    display_block(scrbuf, front_vga[1], 3, 142, 2, -1);
    iArrayIdx_1 = 17;
    szRecordName_1 = RecordNames[17];
    iCarY2 = 46;
    iKillIconY_1 = 45;

    do
    {
      // display record number
      sprintf(buffer, (uint8 *)"%02i", iRecordIdx_1);
      front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 33, iTextY_1, 0x8Fu, 0);

      iCarType_1 = RecordCars[iArrayIdx_1];
      if ( iCarType_1 < 0 )
      {
        // no record set
        sprintf(buffer, (uint8 *)"%s", szRecordName_1);
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 165, iTextY_1, 0x8Fu, 0);
        front_text(front_vga[3], "00:00:00", font2_ascii, font2_offsets, 450, iTextY_1, 0x8Fu, 0);
      }
      else
      {
        // display record holder name
        sprintf(buffer, (uint8 *)"%s", szRecordName_1);
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 85, iTextY_1, 0x8Fu, 0);

        // display car company name
        sprintf(buffer, (uint8 *)"%s", CompanyNames[iCarType_1 & 0xF]);
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 218, iTextY_1, 0x8Fu, 0);

        // display car or cheat indicator
        if ( (iCarType_1 & 0xFu) >= 8 )
          front_text(front_vga[3], "CHEAT", font2_ascii, font2_offsets, 165, iTextY_1, 0x8Fu, 0);
        else
          display_block(scrbuf, front_vga[0], smallcars[0][iCarType_1], 165, iCarY2, 0);

        // display kill count icon and number
        display_block(scrbuf, front_vga[0], 9, 540, iKillIconY_1, 0);
        sprintf(buffer, (uint8 *)"%i", RecordKills[iArrayIdx_1]);
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 560, iTextY_1, 0x8Fu, 0);

        // display lap time
        dLapTime_1 = RecordLaps[iArrayIdx_1] * 100.0;
        //_CHP();
        iTimeValue_1 = (int)dLapTime_1;
        if ( (int)dLapTime_1 > 599999 )
          iTimeValue_1 = 599999;

        // display centiseconds
        buffer[1] = iTimeValue_1 % 10 + 48;
        iTimeTemp_1 = iTimeValue_1 / 10;
        buffer[0] = iTimeTemp_1 % 10 + 48;
        buffer[2] = 0;
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 492, iTextY_1, 0x8Fu, 0);
        front_text(front_vga[3], ":", font2_ascii, font2_offsets, 467, iTextY_1, 0x8Fu, 0);

        // display seconds
        iTimeTemp_1 /= 10;
        buffer[1] = iTimeTemp_1 % 10 + 48;
        iTimeTemp_1 /= 10;
        buffer[0] = iTimeTemp_1 % 6 + 48;
        buffer[2] = 0;
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 471, iTextY_1, 0x8Fu, 0);
        front_text(front_vga[3], ":", font2_ascii, font2_offsets, 488, iTextY_1, 0x8Fu, 0);

        // display minutes
        iTimeTemp_1 /= 6;
        buffer[1] = iTimeTemp_1 % 10 + 48;
        buffer[2] = 0;
        buffer[0] = iTimeTemp_1 / 10 % 10 + 48;
        front_text(front_vga[3], buffer, font2_ascii, font2_offsets, 450, iTextY_1, 0x8Fu, 0);
      }

      // move to next record pos
      iTextY_1 += 22;
      ++iArrayIdx_1;
      ++iRecordIdx_1;
      iCarY2 += 22;
      iKillIconY_1 += 22;
      szRecordName_1 += 9;
    }
    while ( iRecordIdx_1 < 25 );

    // display second page and wait for input
    copypic(scrbuf, screen);
    holdmusic = -1;
    fade_palette(32);
    ticks = 0;
    if ( game_type == 4 )
    {
      while ( !fatkbhit() )
        UpdateSDL();
    }
    else
    {
      while ( !fatkbhit() && ticks < 720 )
        UpdateSDL();
    }
    scr_size = iOldScrSize;
  }

  // cleanup
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  holdmusic = (game_type != 4) - 1;
  fade_palette(0);
  if ( game_type != 4 )
    holdmusic = 0;
}

//-------------------------------------------------------------------------------------------------
//00059220
void show_3dmap(float fZ, int iElevation, int iYaw)
{
  int iChunkIdx; // edx
  double dBoundingBoxExpansionX; // st7
  double dBoundingBoxExpansionY; // st7
  double dZRange; // st7
  double dCenterDivisor; // st7
  double dTempTransform; // st7
  double dTempTransform2; // st7
  double dCorner1Y; // st5
  double dCorner1Z; // st4
  double dCorner1X; // st2
  double dTransformedX1; // st6
  double dTransformedY1; // rt2
  double dTransformedZ1; // st5
  double dDepth1; // st7
  double dViewDistance; // st7
  double dDepthInverse1; // st6
  double dScreenX1; // st5
  double dScreenY1; // st7
  double dCorner2Y; // st6
  double dCorner2Z; // st5
  double dCorner2X; // st3
  double dTransformedX2; // st7
  double dTransformedY2; // rtt
  double dTransformedZ2; // st5
  double dProjX2; // st4
  int iScreenSize; // edi
  double dProjZ2; // st3
  float fProjX1Int; // eax
  double dProjY2Full; // st4
  float fProjY2; // eax
  double dDepth2Full; // st7
  double dViewDistance2Full; // st7
  double dDepthInverse2; // st6
  double dScreenX2; // st5
  double dScreenY2; // st7
  double dCorner3Y; // st6
  double dCorner3Z; // st5
  double dCorner3X; // st3
  double dTransformedX3; // st7
  double fTransformedY3; // rt1
  double dTransformedZ3; // st5
  double dProjX3; // st4
  int iScreenSize2; // edi
  double dProjZ3Full; // st3
  float fProjX2Temp; // eax
  double dProjY3; // st4
  float fProjY2Temp; // eax
  double dDepth3; // st7
  double dViewDistance3; // st7
  double dDepthInverse3; // st6
  double dScreenX3; // st5
  double dScreenY3Full; // st7
  double dCorner4Y; // st6
  double dCorner4Z; // st5
  double dCorner4X; // st3
  double dTransformedX4; // st7
  double dTransformedY4; // rtt
  double dTransformedZ4; // st5
  double dProjX4; // st4
  int iScreenSize3; // edi
  double dProjZ4; // st3
  float fProjX3; // eax
  double dProjY4; // st4
  float fProjY3Full; // eax
  double dDepth4Full; // st7
  double dViewDistance4; // st7
  double dDepthInverse4; // st6
  double dScreenX4; // st5
  double dScreenY4; // st7
  int iScreenSize4; // edi
  int iProjX4Int; // eax
  int iScreenXFinal; // eax
  int iLoopIndex; // ecx
  tTrackScreenXYZ *pTrackScreenXYZ; // ebx
  tVec3 *pTrackPt; // edi
  tGroundPt *pCurrentGroundPt; // eax
  double dTrackPointY; // st6
  double dTrackPointZ; // st5
  double dTrackPointX; // st3
  double dTransformedTrackX; // st7
  double dTransformedTrackY; // rt1
  double dTransformedTrackZ; // st5
  double dViewDistanceTrackFull; // st7
  double dViewDistanceTrack; // st7
  double dDepthInverseTrack; // st6
  double dScreenXTrack; // st5
  double dScreenYTrack; // st7
  int iScreenXTrackInt; // eax
  double dTrackPoint2Y; // st6
  double dTrackPoint2Z; // st5
  double dTrackPoint2X; // st3
  double dTransformedTrack2X; // st7
  double dTransformedTrack2Y; // rtt
  double dTransformedTrack2Z; // st5
  double dDepthTrack2Full; // st7
  double dViewDistanceTrack2; // st7
  double dDepthInverseTrack2; // st6
  double dScreenXTrack2; // st5
  double dScreenYTrack2; // st7
  int iScreenSize5; // ebp
  int iScreenXCalc; // esi
  int iTrackSegmentLoop; // ecx
  tTrackScreenXYZ *pTrackScreenXYZ_1; // edi
  int iZOrderIndex; // esi
  int iNextSegmentIdx; // edx
  tTrackScreenXYZ *pNextTrackScreenXYZ; // edx
  float fMaxDepthCurrent; // eax
  float fDepthForZOrderFinal; // eax
  float fDepthForZOrder; // eax
  signed int iRenderLoopIndex; // ebp
  int iColorGradient; // ecx
  int iCurrentSegmentIdx; // ebx
  tTrackScreenXYZ *pCurrentSegmentScreenXYZ; // esi
  double dHeightColorCalc; // st7
  tTrackScreenXYZ *pNextTrackScreenXYZ_1; // edi
  //int iSurfaceColor_1; // eax
  tPoint pointTemp; // kr00_8
  float fBaseY; // [esp+34h] [ebp-B0h]
  float fBaseX; // [esp+38h] [ebp-ACh]
  float fTransformTemp; // [esp+3Ch] [ebp-A8h]
  float fDepthTemp1; // [esp+40h] [ebp-A4h]
  float fDepthTemp2; // [esp+40h] [ebp-A4h]
  float fDepth1; // [esp+40h] [ebp-A4h]
  float fDepth2; // [esp+40h] [ebp-A4h]
  float fProjectedX1; // [esp+44h] [ebp-A0h]
  float fProjectedX2; // [esp+44h] [ebp-A0h]
  float fProjectedX3; // [esp+44h] [ebp-A0h]
  float fProjectedX4; // [esp+44h] [ebp-A0h]
  float fProjectedY1; // [esp+48h] [ebp-9Ch]
  float fProjectedY2; // [esp+48h] [ebp-9Ch]
  float fProjectedY3; // [esp+48h] [ebp-9Ch]
  float fProjectedY4; // [esp+48h] [ebp-9Ch]
  float fProjectedZ1; // [esp+4Ch] [ebp-98h]
  float fProjectedZ2; // [esp+4Ch] [ebp-98h]
  float fProjectedZ3; // [esp+4Ch] [ebp-98h]
  float fProjectedZ4; // [esp+4Ch] [ebp-98h]
  float fZRangeForColor; // [esp+50h] [ebp-94h]
  float fTransform33; // [esp+58h] [ebp-8Ch]
  float fTransform21; // [esp+5Ch] [ebp-88h]
  float fTransform31; // [esp+60h] [ebp-84h]
  float fTransform32; // [esp+64h] [ebp-80h]
  float fTransform23; // [esp+68h] [ebp-7Ch]
  float fTransform22; // [esp+6Ch] [ebp-78h]
  float fTransform12; // [esp+70h] [ebp-74h]
  float fTransform11; // [esp+74h] [ebp-70h]
  float fTransform13; // [esp+78h] [ebp-6Ch]
  float fMaxDepthForZOrder; // [esp+8Ch] [ebp-58h]
  signed int iTrackSegmentCount; // [esp+90h] [ebp-54h]
  float fTrackDepth1; // [esp+94h] [ebp-50h]
  float fTrackDepth2; // [esp+94h] [ebp-50h]
  float fMinY; // [esp+98h] [ebp-4Ch]
  float fBoundingMinY; // [esp+98h] [ebp-4Ch]
  float fMinX; // [esp+9Ch] [ebp-48h]
  float fBoundingMinX; // [esp+9Ch] [ebp-48h]
  float fY; // [esp+A0h] [ebp-44h]
  float fBoundingMaxY; // [esp+A0h] [ebp-44h]
  float fX; // [esp+A4h] [ebp-40h]
  float fBoundingMaxX; // [esp+A4h] [ebp-40h]
  float fAccumulatedX; // [esp+A8h] [ebp-3Ch]
  float fTrackCenterX; // [esp+A8h] [ebp-3Ch]
  float fAccumulatedY; // [esp+ACh] [ebp-38h]
  float fTrackCenterY; // [esp+ACh] [ebp-38h]
  float fAccumulatedZ; // [esp+B0h] [ebp-34h]
  float fTrackCenterZ; // [esp+B0h] [ebp-34h]
  float fMinZ; // [esp+B4h] [ebp-30h]
  float fBoundingMinZ; // [esp+B4h] [ebp-30h]
  float fBoundingMaxZ; // [esp+B8h] [ebp-2Ch]
  float fTrackProjY1; // [esp+BCh] [ebp-28h]
  float fTrackProjY2; // [esp+BCh] [ebp-28h]
  float fTrackProjX1; // [esp+C0h] [ebp-24h]
  float fTrackProjX2; // [esp+C0h] [ebp-24h]
  float fTrackProjZ1; // [esp+C4h] [ebp-20h]
  float fTrackProjZ2; // [esp+C4h] [ebp-20h]
  int iNextSegmentIdx_1; // [esp+C8h] [ebp-1Ch]
  int iSurfaceColor; // [esp+C8h] [ebp-1Ch]

  // Initialize base screen coordinates
  fBaseX = (float)xbase;
  fBaseY = (float)ybase;

  // Set world camera position based on elevation and distance
  worldx = -fZ * tcos[iElevation];
  worldz = fZ * tsin[iElevation];
  worldy = 0.0;

  // Initialize bounding box coordinates (min/max values)
  fMinZ = 9.9999998e17f;
  fMinY = 9.9999998e17f;
  fMinX = 9.9999998e17f;
  fBoundingMaxZ = -9.9999998e17f;
  fY = -9.9999998e17f;
  fX = -9.9999998e17f;
  vdirection = 0;
  vtilt = 0;
  velevation = -iElevation & 0x3FFF;
  fAccumulatedX = 0.0;
  xbase += 40;
  calculatetransform(-1, 0, -iElevation & 0x3FFF, 0, worldx, 0.0, worldz, 0.0, 0.0, 0.0);
  fAccumulatedY = 0.0;
  worlddirn = vdirection;
  fAccumulatedZ = 0.0;
  worldelev = velevation;
  worldtilt = vtilt;
  if (TRAK_LEN > 0) {
    // Calculate track bounding box and center point
    iChunkIdx = 0;
    do {
      // Accumulate track point coordinates for center calculation
      fAccumulatedX = TrakPt[iChunkIdx].pointAy[0].fX + TrakPt[iChunkIdx].pointAy[4].fX + fAccumulatedX;
      fAccumulatedY = TrakPt[iChunkIdx].pointAy[0].fY + TrakPt[iChunkIdx].pointAy[4].fY + fAccumulatedY;
      fAccumulatedZ = TrakPt[iChunkIdx].pointAy[0].fZ + TrakPt[iChunkIdx].pointAy[4].fZ + fAccumulatedZ;
      // Update minimum bounding box coordinates
      if (TrakPt[iChunkIdx].pointAy[0].fX < (double)fMinX)
        fMinX = TrakPt[iChunkIdx].pointAy[0].fX;
      if (TrakPt[iChunkIdx].pointAy[4].fX < (double)fMinX)
        fMinX = TrakPt[iChunkIdx].pointAy[4].fX;
      if (TrakPt[iChunkIdx].pointAy[0].fY < (double)fMinY)
        fMinY = TrakPt[iChunkIdx].pointAy[0].fY;
      if (TrakPt[iChunkIdx].pointAy[4].fY < (double)fMinY)
        fMinY = TrakPt[iChunkIdx].pointAy[4].fY;
      if (TrakPt[iChunkIdx].pointAy[0].fZ < (double)fMinZ)
        fMinZ = TrakPt[iChunkIdx].pointAy[0].fZ;
      if (TrakPt[iChunkIdx].pointAy[4].fZ < (double)fMinZ)
        fMinZ = TrakPt[iChunkIdx].pointAy[4].fZ;
      // Update maximum bounding box coordinates
      if (TrakPt[iChunkIdx].pointAy[0].fX > (double)fX)
        fX = TrakPt[iChunkIdx].pointAy[0].fX;
      if (TrakPt[iChunkIdx].pointAy[4].fX > (double)fX)
        fX = TrakPt[iChunkIdx].pointAy[4].fX;
      if (TrakPt[iChunkIdx].pointAy[0].fY > (double)fY)
        fY = TrakPt[iChunkIdx].pointAy[0].fY;
      if (TrakPt[iChunkIdx].pointAy[4].fY > (double)fY)
        fY = TrakPt[iChunkIdx].pointAy[4].fY;
      if (TrakPt[iChunkIdx].pointAy[0].fZ > (double)fBoundingMaxZ)
        fBoundingMaxZ = TrakPt[iChunkIdx].pointAy[0].fZ;
      if (TrakPt[iChunkIdx].pointAy[4].fZ > (double)fBoundingMaxZ)
        fBoundingMaxZ = TrakPt[iChunkIdx].pointAy[4].fZ;
      ++iChunkIdx;
    } while (iChunkIdx < TRAK_LEN);
  }

  // Calculate expanded bounding box (min/max coordinates with 10% margin)
  dBoundingBoxExpansionX = (fX - fMinX) * 0.1;
  fBoundingMinX = fMinX - (float)dBoundingBoxExpansionX;
  fBoundingMaxX = (float)dBoundingBoxExpansionX + fX;
  dBoundingBoxExpansionY = (fY - fMinY) * 0.1;
  fBoundingMinY = fMinY - (float)dBoundingBoxExpansionY;
  fBoundingMaxY = (float)dBoundingBoxExpansionY + fY;
  dZRange = fBoundingMaxZ - fMinZ;
  fZRangeForColor = (float)dZRange;
  fBoundingMinZ = fMinZ - (float)(dZRange * 0.4);

  // Calculate track center point from accumulated coordinates
  dCenterDivisor = 1.0 / (double)(2 * TRAK_LEN);
  fTrackCenterX = -fAccumulatedX * (float)dCenterDivisor;
  fTrackCenterY = -fAccumulatedY * (float)dCenterDivisor;
  fTrackCenterZ = (float)dCenterDivisor * -fAccumulatedZ;

  // Build 3D rotation transformation matrix
  fTransform11 = tcos[iYaw] * tcos[0];
  fTransform12 = tsin[iYaw] * tcos[0];
  fTransform13 = tsin[0];
  dTempTransform = tcos[iYaw] * tsin[0];
  fTransformTemp = (float)dTempTransform;
  fTransform21 = (float)dTempTransform * tsin[0] - fTransform12;
  dTempTransform2 = tsin[iYaw] * tsin[0];
  fTransform22 = (float)dTempTransform2 * tsin[0] + fTransform11;
  fTransform31 = -tsin[0] * tcos[0];
  fTransform32 = -tcos[iYaw] * tsin[0] * tcos[0] - (float)dTempTransform2;
  fTransform23 = -tsin[iYaw] * tsin[0] * tcos[0] + fTransformTemp;
  fTransform33 = tcos[0] * tcos[0];

  // Transform corner 1 (min X, min Y) of bounding box to screen coordinates
  dCorner1Y = fBoundingMinY + fTrackCenterY;
  dCorner1Z = fBoundingMinZ + fTrackCenterZ;
  dCorner1X = fBoundingMinX + fTrackCenterX;
  dTransformedX1 = dCorner1X * fTransform11 + dCorner1Y * fTransform21 + dCorner1Z * fTransform32 - viewx;
  dTransformedY1 = dCorner1X * fTransform12 + dCorner1Y * fTransform22 + dCorner1Z * fTransform23 - viewy;
  dTransformedZ1 = tsin[0] * dCorner1X + dCorner1Y * fTransform31 + dCorner1Z * fTransform33 - viewz;
  fProjectedX1 = (float)dTransformedX1 * vk1 + (float)dTransformedY1 * vk4 + (float)dTransformedZ1 * vk7;
  fProjectedY1 = (float)dTransformedX1 * vk2 + (float)dTransformedY1 * vk5 + (float)dTransformedZ1 * vk8;
  dDepth1 = (float)dTransformedX1 * vk3 + (float)dTransformedY1 * vk6 + (float)dTransformedZ1 * vk9;
  fProjectedZ1 = (float)dDepth1;
  fDepthTemp1 = fProjectedZ1;
  if (dDepth1 < 80.0) {
    fProjectedZ1 = 80.0;
    ++TrackScreenXYZ[0].iClipCount;
  }
  dViewDistance = (double)VIEWDIST;
  dDepthInverse1 = 1.0 / fProjectedZ1;
  dScreenX1 = dViewDistance * fProjectedX1 * dDepthInverse1 + (double)xbase;
  //_CHP();
  xp = (int)dScreenX1;
  dScreenY1 = dDepthInverse1 * (dViewDistance * fProjectedY1) + (double)ybase;
  //_CHP();
  yp = (int)dScreenY1;

  // Transform corner 2 (max X, min Y) of bounding box to screen coordinates
  dCorner2Y = fBoundingMinY + fTrackCenterY;
  dCorner2Z = fBoundingMinZ + fTrackCenterZ;
  dCorner2X = fBoundingMaxX + fTrackCenterX;
  dTransformedX2 = dCorner2X * fTransform11 + dCorner2Y * fTransform21 + dCorner2Z * fTransform32 - viewx;
  dTransformedY2 = dCorner2X * fTransform12 + dCorner2Y * fTransform22 + dCorner2Z * fTransform23 - viewy;
  dTransformedZ2 = dCorner2X * fTransform13 + dCorner2Y * fTransform31 + dCorner2Z * fTransform33 - viewz;
  dProjX2 = dTransformedX2 * vk1 + dTransformedY2 * vk4;
  iScreenSize = scr_size;
  dProjZ2 = dTransformedZ2 * vk7;
  TrackScreenXYZ[0].screenPtAy[0].screen.x = (scr_size * xp) >> 6;
  TrackScreenXYZ[0].screenPtAy[0].screen.y = (iScreenSize * (199 - yp)) >> 6;
  fProjX1Int = fProjectedX1;
  fProjectedX2 = (float)(dProjX2 + dProjZ2);
  dProjY2Full = dTransformedX2 * vk2 + dTransformedY2 * vk5 + dTransformedZ2 * vk8;
  TrackScreenXYZ[0].screenPtAy[0].projected.fX = fProjX1Int;
  fProjY2 = fProjectedY1;
  fProjectedY2 = (float)dProjY2Full;
  dDepth2Full = dTransformedX2 * vk3 + dTransformedY2 * vk6 + dTransformedZ2 * vk9;
  fProjectedZ2 = (float)dDepth2Full;
  TrackScreenXYZ[0].screenPtAy[0].projected.fY = fProjY2;
  TrackScreenXYZ[0].screenPtAy[0].projected.fZ = fDepthTemp1;
  fDepthTemp2 = fProjectedZ2;
  if (dDepth2Full < 80.0) {
    fProjectedZ2 = 80.0;
    ++TrackScreenXYZ[0].iClipCount;
  }
  dViewDistance2Full = (double)VIEWDIST;
  dDepthInverse2 = 1.0 / fProjectedZ2;
  dScreenX2 = dViewDistance2Full * fProjectedX2 * dDepthInverse2 + (double)xbase;
  //_CHP();
  xp = (int)dScreenX2;
  dScreenY2 = dDepthInverse2 * (dViewDistance2Full * fProjectedY2) + (double)ybase;
  //_CHP();
  yp = (int)dScreenY2;

  // Transform corner 3 (max X, max Y) of bounding box to screen coordinates
  dCorner3Y = fBoundingMaxY + fTrackCenterY;
  dCorner3Z = fBoundingMinZ + fTrackCenterZ;
  dCorner3X = fBoundingMaxX + fTrackCenterX;
  dTransformedX3 = dCorner3X * fTransform11 + dCorner3Y * fTransform21 + dCorner3Z * fTransform32 - viewx;
  fTransformedY3 = dCorner3X * fTransform12 + dCorner3Y * fTransform22 + dCorner3Z * fTransform23 - viewy;
  dTransformedZ3 = dCorner3X * fTransform13 + dCorner3Y * fTransform31 + dCorner3Z * fTransform33 - viewz;
  dProjX3 = dTransformedX3 * vk1 + fTransformedY3 * vk4;
  iScreenSize2 = scr_size;
  dProjZ3Full = dTransformedZ3 * vk7;
  TrackScreenXYZ[0].screenPtAy[1].screen.x = (scr_size * xp) >> 6;
  TrackScreenXYZ[0].screenPtAy[1].screen.y = (iScreenSize2 * (199 - yp)) >> 6;
  fProjX2Temp = fProjectedX2;
  fProjectedX3 = (float)(dProjX3 + dProjZ3Full);
  dProjY3 = dTransformedX3 * vk2 + fTransformedY3 * vk5 + dTransformedZ3 * vk8;
  TrackScreenXYZ[0].screenPtAy[1].projected.fX = fProjX2Temp;
  fProjY2Temp = fProjectedY2;
  fProjectedY3 = (float)dProjY3;
  dDepth3 = dTransformedX3 * vk3 + fTransformedY3 * vk6 + dTransformedZ3 * vk9;
  fProjectedZ3 = (float)dDepth3;
  TrackScreenXYZ[0].screenPtAy[1].projected.fY = fProjY2Temp;
  TrackScreenXYZ[0].screenPtAy[1].projected.fZ = fDepthTemp2;
  fDepth1 = fProjectedZ3;
  if (dDepth3 < 80.0) {
    fProjectedZ3 = 80.0;
    ++TrackScreenXYZ[0].iClipCount;
  }
  dViewDistance3 = (double)VIEWDIST;
  dDepthInverse3 = 1.0 / fProjectedZ3;
  dScreenX3 = dViewDistance3 * fProjectedX3 * dDepthInverse3 + (double)xbase;
  //_CHP();
  xp = (int)dScreenX3;
  dScreenY3Full = dDepthInverse3 * (dViewDistance3 * fProjectedY3) + (double)ybase;
  //_CHP();
  yp = (int)dScreenY3Full;

  // Transform corner 4 (min X, max Y) of bounding box to screen coordinates
  dCorner4Y = fBoundingMaxY + fTrackCenterY;
  dCorner4Z = fBoundingMinZ + fTrackCenterZ;
  dCorner4X = fBoundingMinX + fTrackCenterX;
  dTransformedX4 = dCorner4X * fTransform11 + dCorner4Y * fTransform21 + dCorner4Z * fTransform32 - viewx;
  dTransformedY4 = dCorner4X * fTransform12 + dCorner4Y * fTransform22 + dCorner4Z * fTransform23 - viewy;
  dTransformedZ4 = dCorner4X * fTransform13 + dCorner4Y * fTransform31 + dCorner4Z * fTransform33 - viewz;
  dProjX4 = dTransformedX4 * vk1 + dTransformedY4 * vk4;
  iScreenSize3 = scr_size;
  dProjZ4 = dTransformedZ4 * vk7;
  TrackScreenXYZ[0].screenPtAy[2].screen.x = (scr_size * xp) >> 6;
  TrackScreenXYZ[0].screenPtAy[2].screen.y = (iScreenSize3 * (199 - yp)) >> 6;
  fProjX3 = fProjectedX3;
  fProjectedX4 = (float)(dProjX4 + dProjZ4);
  dProjY4 = dTransformedX4 * vk2 + dTransformedY4 * vk5 + dTransformedZ4 * vk8;
  TrackScreenXYZ[0].screenPtAy[2].projected.fX = fProjX3;
  fProjY3Full = fProjectedY3;
  fProjectedY4 = (float)dProjY4;
  dDepth4Full = dTransformedX4 * vk3 + dTransformedY4 * vk6 + dTransformedZ4 * vk9;
  fProjectedZ4 = (float)dDepth4Full;
  TrackScreenXYZ[0].screenPtAy[2].projected.fY = fProjY3Full;
  TrackScreenXYZ[0].screenPtAy[2].projected.fZ = fDepth1;
  fDepth2 = fProjectedZ4;
  if (dDepth4Full < 80.0) {
    fProjectedZ4 = 80.0;
    ++TrackScreenXYZ[0].iClipCount;
  }
  dViewDistance4 = (double)VIEWDIST;
  dDepthInverse4 = 1.0 / fProjectedZ4;
  dScreenX4 = dViewDistance4 * fProjectedX4 * dDepthInverse4 + (double)xbase;
  //_CHP();
  iProjX4Int = scr_size * (int)dScreenX4; //lost line, decompiler artifact
  xp = (int)dScreenX4;
  dScreenY4 = dDepthInverse4 * (dViewDistance4 * fProjectedY4) + (double)ybase;
  iScreenSize4 = scr_size;
  //_CHP();
  yp = (int)dScreenY4;
  TrackScreenXYZ[0].screenPtAy[3].screen.x = iProjX4Int >> 6;
  TrackScreenXYZ[0].screenPtAy[3].screen.y = (iScreenSize4 * (199 - yp)) >> 6;
  TrackScreenXYZ[0].screenPtAy[3].projected.fX = fProjectedX4;
  TrackScreenXYZ[0].screenPtAy[3].projected.fY = fProjectedY4;
  TrackScreenXYZ[0].screenPtAy[3].projected.fZ = fDepth2;
  RoadPoly.vertices[0] = TrackScreenXYZ[0].screenPtAy[0].screen;
  RoadPoly.vertices[1] = TrackScreenXYZ[0].screenPtAy[1].screen;
  RoadPoly.vertices[2] = TrackScreenXYZ[0].screenPtAy[2].screen;
  iScreenXFinal = TrackScreenXYZ[0].screenPtAy[3].screen.x;
  RoadPoly.uiNumVerts = 4;
  RoadPoly.vertices[3].x = iScreenXFinal;
  RoadPoly.vertices[3].y = TrackScreenXYZ[0].screenPtAy[3].screen.y;
  RoadPoly.iSurfaceType = SURFACE_FLAG_TRANSPARENT | SURFACE_FLAG_FLIP_BACKFACE | 0x2;

  // Render background polygon for track area
  POLYFLAT(scrbuf, &RoadPoly);
  iLoopIndex = 0;
  if (TRAK_LEN > 0) {
    // Transform all track points to screen coordinates
    pTrackScreenXYZ = TrackScreenXYZ;
    pTrackPt = &TrakPt[0].pointAy[4];
    do {
      pCurrentGroundPt = &TrakPt[iLoopIndex];
      pTrackScreenXYZ->iClipCount = 0;

      // Transform first track point (pointAy[0]) to screen coordinates
      dTrackPointY = pCurrentGroundPt->pointAy[0].fY + fTrackCenterY;
      dTrackPointZ = pCurrentGroundPt->pointAy[0].fZ + fTrackCenterZ;
      dTrackPointX = pCurrentGroundPt->pointAy[0].fX + fTrackCenterX;
      // Apply 3D transformation matrix to track point
      dTransformedTrackX = dTrackPointX * fTransform11 + dTrackPointY * fTransform21 + dTrackPointZ * fTransform32 - viewx;
      dTransformedTrackY = dTrackPointX * fTransform12 + dTrackPointY * fTransform22 + dTrackPointZ * fTransform23 - viewy;
      dTransformedTrackZ = dTrackPointX * fTransform13 + dTrackPointY * fTransform31 + dTrackPointZ * fTransform33 - viewz;
      fTrackProjX1 = (float)dTransformedTrackX * vk1 + (float)dTransformedTrackY * vk4 + (float)dTransformedTrackZ * vk7;
      fTrackProjY1 = (float)dTransformedTrackX * vk2 + (float)dTransformedTrackY * vk5 + (float)dTransformedTrackZ * vk8;
      dViewDistanceTrackFull = (float)dTransformedTrackX * vk3 + (float)dTransformedTrackY * vk6 + (float)dTransformedTrackZ * vk9;
      fTrackProjZ1 = (float)dViewDistanceTrackFull;
      fTrackDepth1 = fTrackProjZ1;
      if (dViewDistanceTrackFull < 80.0) {
        fTrackProjZ1 = 80.0;
        ++pTrackScreenXYZ->iClipCount;
      }
      dViewDistanceTrack = (double)VIEWDIST;
      // Project 3D coordinates to 2D screen coordinates
      dDepthInverseTrack = 1.0 / fTrackProjZ1;
      dScreenXTrack = dViewDistanceTrack * fTrackProjX1 * dDepthInverseTrack + (double)xbase;
      //_CHP();
      iScreenXTrackInt = scr_size * (int)dScreenXTrack; //lost line, decompiler artifact
      xp = (int)dScreenXTrack;
      dScreenYTrack = dDepthInverseTrack * (dViewDistanceTrack * fTrackProjY1) + (double)ybase;
      //_CHP();
      yp = (int)dScreenYTrack;
      pTrackScreenXYZ->screenPtAy[1].screen.x = iScreenXTrackInt >> 6;
      pTrackScreenXYZ->screenPtAy[1].screen.y = (scr_size * (199 - yp)) >> 6;
      pTrackScreenXYZ->screenPtAy[1].projected.fX = fTrackProjX1;
      pTrackScreenXYZ->screenPtAy[1].projected.fY = fTrackProjY1;
      pTrackScreenXYZ->screenPtAy[1].projected.fZ = fTrackDepth1;

      // Transform second track point (pointAy[4]) to screen coordinates
      dTrackPoint2Y = pTrackPt->fY + fTrackCenterY;
      dTrackPoint2Z = pTrackPt->fZ + fTrackCenterZ;
      dTrackPoint2X = pTrackPt->fX + fTrackCenterX;
      dTransformedTrack2X = dTrackPoint2X * fTransform11 + dTrackPoint2Y * fTransform21 + dTrackPoint2Z * fTransform32 - viewx;
      dTransformedTrack2Y = dTrackPoint2X * fTransform12 + dTrackPoint2Y * fTransform22 + dTrackPoint2Z * fTransform23 - viewy;
      dTransformedTrack2Z = dTrackPoint2X * fTransform13 + dTrackPoint2Y * fTransform31 + dTrackPoint2Z * fTransform33 - viewz;
      fTrackProjX2 = (float)dTransformedTrack2X * vk1 + (float)dTransformedTrack2Y * vk4 + (float)dTransformedTrack2Z * vk7;
      fTrackProjY2 = (float)dTransformedTrack2X * vk2 + (float)dTransformedTrack2Y * vk5 + (float)dTransformedTrack2Z * vk8;
      dDepthTrack2Full = (float)dTransformedTrack2X * vk3 + (float)dTransformedTrack2Y * vk6 + (float)dTransformedTrack2Z * vk9;
      fTrackProjZ2 = (float)dDepthTrack2Full;
      fTrackDepth2 = fTrackProjZ2;
      if (dDepthTrack2Full < 80.0) {
        fTrackProjZ2 = 80.0;
        ++pTrackScreenXYZ->iClipCount;
      }
      dViewDistanceTrack2 = (double)VIEWDIST;
      dDepthInverseTrack2 = 1.0 / fTrackProjZ2;
      dScreenXTrack2 = dViewDistanceTrack2 * fTrackProjX2 * dDepthInverseTrack2 + (double)xbase;
      //_CHP();
      xp = (int)dScreenXTrack2;
      dScreenYTrack2 = dDepthInverseTrack2 * (dViewDistanceTrack2 * fTrackProjY2) + (double)ybase;
      iScreenSize5 = scr_size;
      iScreenXCalc = scr_size * (int)dScreenXTrack2;
      //_CHP();
      yp = (int)dScreenYTrack2;
      pTrackScreenXYZ->screenPtAy[0].screen.x = iScreenXCalc >> 6;
      pTrackScreenXYZ->screenPtAy[0].screen.y = (iScreenSize5 * (199 - yp)) >> 6;
      pTrackScreenXYZ->screenPtAy[0].projected.fX = fTrackProjX2;
      pTrackPt += 12;
      pTrackScreenXYZ->screenPtAy[0].projected.fY = fTrackProjY2;
      iLoopIndex += 2;
      pTrackScreenXYZ->screenPtAy[0].projected.fZ = fTrackDepth2;
      pTrackScreenXYZ += 2;
    } while (iLoopIndex < TRAK_LEN);
  }
  iTrackSegmentLoop = 0;
  iTrackSegmentCount = 0;

  // Build Z-order list for depth sorting
  if (TRAK_LEN > 0) {
    pTrackScreenXYZ_1 = TrackScreenXYZ;
    iZOrderIndex = 0;
    do {
      // Determine maximum depth for Z-order sorting
      iNextSegmentIdx = iTrackSegmentLoop + 2;
      if (iTrackSegmentLoop + 2 >= TRAK_LEN)
        iNextSegmentIdx = 0;
      CarZOrder[iZOrderIndex].iPolygonIndex = iTrackSegmentLoop;
      pNextTrackScreenXYZ = &TrackScreenXYZ[iNextSegmentIdx];
      if (pTrackScreenXYZ_1->screenPtAy[1].projected.fZ <= (double)pTrackScreenXYZ_1->screenPtAy[0].projected.fZ)
        fMaxDepthCurrent = pTrackScreenXYZ_1->screenPtAy[0].projected.fZ;
      else
        fMaxDepthCurrent = pTrackScreenXYZ_1->screenPtAy[1].projected.fZ;
      fMaxDepthForZOrder = fMaxDepthCurrent;
      if (pNextTrackScreenXYZ->screenPtAy[1].projected.fZ <= (double)pNextTrackScreenXYZ->screenPtAy[0].projected.fZ)
        fDepthForZOrderFinal = pNextTrackScreenXYZ->screenPtAy[0].projected.fZ;
      else
        fDepthForZOrderFinal = pNextTrackScreenXYZ->screenPtAy[1].projected.fZ;
      if (fMaxDepthForZOrder <= (double)fDepthForZOrderFinal) {
        if (pNextTrackScreenXYZ->screenPtAy[1].projected.fZ <= (double)pNextTrackScreenXYZ->screenPtAy[0].projected.fZ)
          fDepthForZOrder = pNextTrackScreenXYZ->screenPtAy[0].projected.fZ;
        else
          fDepthForZOrder = pNextTrackScreenXYZ->screenPtAy[1].projected.fZ;
      } else if (pTrackScreenXYZ_1->screenPtAy[1].projected.fZ <= (double)pTrackScreenXYZ_1->screenPtAy[0].projected.fZ) {
        fDepthForZOrder = pTrackScreenXYZ_1->screenPtAy[0].projected.fZ;
      } else {
        fDepthForZOrder = pTrackScreenXYZ_1->screenPtAy[1].projected.fZ;
      }

      CarZOrder[iZOrderIndex].fZDepth = fDepthForZOrder;

      ++iZOrderIndex;
      pTrackScreenXYZ_1 += 2;
      iTrackSegmentLoop += 2;
      //*(float *)((char *)&CarPt[127].view.fZ + iZOrderIndex * 12) = fDepthForZOrder;// offset into CarZOrder if placed above loop inc
      ++iTrackSegmentCount;
    } while (iTrackSegmentLoop < TRAK_LEN);
  }

  // Sort track segments by depth (Z-order)
  qsort(CarZOrder, iTrackSegmentCount, 0xCu, carZcmp);

  if (iTrackSegmentCount > 0) {
    // Render track segments in back-to-front order
    iRenderLoopIndex = 0;
    iColorGradient = 15 * iTrackSegmentCount;
    do {
      iCurrentSegmentIdx = CarZOrder[iRenderLoopIndex].iPolygonIndex;
      if (iCurrentSegmentIdx >= 0
        && ((TrakColour[iCurrentSegmentIdx][TRAK_COLOUR_LEFT_LANE] & SURFACE_FLAG_SKIP_RENDER) == 0
            || (TrakColour[iCurrentSegmentIdx][TRAK_COLOUR_CENTER] & SURFACE_FLAG_SKIP_RENDER) == 0
            || (TrakColour[iCurrentSegmentIdx][TRAK_COLOUR_RIGHT_LANE] & SURFACE_FLAG_SKIP_RENDER) == 0)) {
        iNextSegmentIdx_1 = iCurrentSegmentIdx + 2;
        pCurrentSegmentScreenXYZ = &TrackScreenXYZ[iCurrentSegmentIdx];
        if (iCurrentSegmentIdx + 2 >= TRAK_LEN)
          iNextSegmentIdx_1 = 0;
        RoadPoly.uiNumVerts = 4;
        // Calculate color based on height (elevation mapping)
        dHeightColorCalc = (fBoundingMaxZ - TrakPt[iCurrentSegmentIdx].pointAy[2].fZ) * 15.0 / fZRangeForColor + (double)(iColorGradient / iTrackSegmentCount);
        //_CHP();
        pNextTrackScreenXYZ_1 = &TrackScreenXYZ[iNextSegmentIdx_1];
        iSurfaceColor = 143 - (int)dHeightColorCalc;
        if (iSurfaceColor > 139)
          iSurfaceColor = 139;
        if (!iCurrentSegmentIdx)
          iSurfaceColor = 143;
        //iSurfaceColor = iSurfaceColor;
        //BYTE1(uiSurfaceColor) = BYTE1(iSurfaceColor) | 0x60;

        // Render road segment with vert orders for both front and back faces
        RoadPoly.iSurfaceType = iSurfaceColor | SURFACE_FLAG_CONCAVE | SURFACE_FLAG_FLIP_BACKFACE;
        RoadPoly.vertices[0] = pNextTrackScreenXYZ_1->screenPtAy[0].screen;
        RoadPoly.vertices[1] = pNextTrackScreenXYZ_1->screenPtAy[1].screen;
        RoadPoly.vertices[2] = pCurrentSegmentScreenXYZ->screenPtAy[1].screen;
        RoadPoly.vertices[3] = pCurrentSegmentScreenXYZ->screenPtAy[0].screen;
        POLYFLAT(scrbuf, &RoadPoly);
        pointTemp = RoadPoly.vertices[0];
        RoadPoly.vertices[0] = RoadPoly.vertices[1];
        RoadPoly.vertices[1] = RoadPoly.vertices[2];
        RoadPoly.vertices[2] = RoadPoly.vertices[3];
        RoadPoly.vertices[3] = pointTemp;
        POLYFLAT(scrbuf, &RoadPoly);
      }
      ++iRenderLoopIndex;
      iColorGradient -= 15;
    } while (iRenderLoopIndex < iTrackSegmentCount);
  }

  // Restore original base coordinates
  //_CHP();
  xbase = (int)fBaseX;
  //_CHP();
  ybase = (int)fBaseY;
}

//-------------------------------------------------------------------------------------------------
//0005A400
void DrawCar(uint8 *pScrBuf, eCarDesignIndex iCarDesignIndex, float fDistance, int iAngle, char byAnimFrame)
{
  int iNumCoords; // ecx
  int iYaw; // eax
  double dCosYaw; // st7
  double dCosPitch; // st7
  double dCosRoll; // st7
  tVec3 *pCarBoxAy; // ebx
  uint32 uiColorTo; // eax
  unsigned int uiVertIdx; // edx
  int iScrSize; // edi
  double dDeltaX; // st7
  double dDeltaY; // st6
  double dDeltaZ; // st5
  double dViewZ; // st7
  double dViewDist; // st7
  double dInvZ; // st6
  double dScreenX; // st5
  double dScreenY; // st7
  double dCarCenterX; // st7
  double dCarCenterY; // st6
  double dCarCenterZ; // st5
  signed int iVisiblePols; // esi
  tPolygon *pPols; // ebp
  int i; // eax
  double dEdge1X; // st7
  double dEdge2X; // st5
  double dEdge2Y; // st4
  double dEdge2Z; // st3
  tPolygon *pPol; // ecx
  int j; // edx
  int byVertIdx; // ebx
  tVec3 *pVertData; // eax
  double dVertX; // st7
  double dVertY; // st5
  int iVertIdx_1; // eax
  double dVertZ; // st6
  double dDeltaX_1; // st7
  double dDeltaY_1; // st6
  double dDeltaZ_1; // st5
  double dViewX; // st7
  double dViewDist_1; // st7
  double dInvZ_1; // st6
  double dProjX; // st5
  double dProjY; // st7
  int iScreenX; // edi
  int iVertIdx_2; // eax
  unsigned int uiZOrderOffset_3; // edx
  int iPolIdx_1; // eax
  float fMinZ34_1; // eax
  float fMinZ12_1; // eax
  float fMaxZ; // eax
  unsigned int uiZOrderOffset_2; // edx
  float fMinZ34; // eax
  float fMinZ12; // eax
  tPolygon *pFirstPol; // eax
  int32 iLinkedPolIdx; // edi
  int iNextPolIdx; // ecx
  int iCurPolIdx; // edi
  int iSearchIdx; // ebx
  signed int k; // eax
  int32 iCheckPolIdx; // edx
  int iDrawIdx; // edi
  int32 iPolToDraw; // esi
  tPolygon *pDrawPol; // edx
  uint32 uiTex; // ecx
  int m; // eax
  double dViewZ_1; // st7
  tCarPt *pMinZVert34; // eax
  tCarPt *pMinZVert12; // eax
  tCarPt *pMinZVert12_1; // eax
  float fMinViewZ; // eax
  tCarPt *pMinZVert34_1; // eax
  int iCartexOffset; // ecx
  int iGfxSize; // [esp+0h] [ebp-19Ch]
  tVec3 *vertDataAy[4]; // [esp+4h] [ebp-198h]
  tCarPt *screenVertAy[4]; // [esp+14h] [ebp-188h]
  double dLocalZ; // [esp+24h] [ebp-178h]
  double dLocalY; // [esp+2Ch] [ebp-170h]
  double dDotProduct; // [esp+34h] [ebp-168h]
  double dLocalX; // [esp+44h] [ebp-158h]
  double dBoxZ; // [esp+4Ch] [ebp-150h]
  double dBoxY; // [esp+54h] [ebp-148h]
  double dBoxX; // [esp+5Ch] [ebp-140h]
  uint32 uiColorFrom; // [esp+64h] [ebp-138h]
  int iNumPols; // [esp+68h] [ebp-134h]
  uint8 *pScreenBuffer; // [esp+6Ch] [ebp-130h]
  float fRotMat01; // [esp+70h] [ebp-12Ch]
  float fClippedZ; // [esp+74h] [ebp-128h]
  float fNearClip; // [esp+78h] [ebp-124h]
  float fViewY; // [esp+7Ch] [ebp-120h]
  float fViewX; // [esp+80h] [ebp-11Ch]
  float fCarPosZ; // [esp+90h] [ebp-10Ch]
  float fCarPosX; // [esp+94h] [ebp-108h]
  float fRotMat22; // [esp+98h] [ebp-104h]
  float fRotMat21; // [esp+9Ch] [ebp-100h]
  float fRotMat11; // [esp+A0h] [ebp-FCh]
  float fMinZ34Temp; // [esp+B0h] [ebp-ECh]
  float fMinZ12Temp; // [esp+B4h] [ebp-E8h]
  float fMaxZTemp_1; // [esp+B8h] [ebp-E4h]
  float fMaxZTemp; // [esp+BCh] [ebp-E0h]
  float fMinZTemp; // [esp+C0h] [ebp-DCh]
  float fMaxZ34; // [esp+C4h] [ebp-D8h]
  float fMaxZ12; // [esp+C8h] [ebp-D4h]
  float fFinalMaxZ; // [esp+CCh] [ebp-D0h]
  float fFinalMaxZ_1; // [esp+D0h] [ebp-CCh]
  float fFinalMinZ; // [esp+D4h] [ebp-C8h]
  tPolygon *pPolAy; // [esp+D8h] [ebp-C4h]
  uint32 uiDesignOffset; // [esp+DCh] [ebp-C0h]
  tPolygon *pLinkedPol; // [esp+E0h] [ebp-BCh]
  float fMinZ; // [esp+E4h] [ebp-B8h]
  float fMinZ_1; // [esp+E8h] [ebp-B4h]
  float fMinZTemp_1; // [esp+ECh] [ebp-B0h]
  float fMinZ12_2; // [esp+F0h] [ebp-ACh]
  float fMinZ34_2; // [esp+F4h] [ebp-A8h]
  int iSubPolType; // [esp+F8h] [ebp-A4h]
  uint32 uiCarDesignIdxTimes4; // [esp+FCh] [ebp-A0h]
  unsigned int uiZOrderOffset; // [esp+104h] [ebp-98h]
  unsigned int uiZOrderOffset_1; // [esp+108h] [ebp-94h]
  int iTotalZOrderBytes; // [esp+10Ch] [ebp-90h]
  float fOriginalZ; // [esp+110h] [ebp-8Ch]
  float fClampedZ; // [esp+114h] [ebp-88h]
  float fTransformedY; // [esp+118h] [ebp-84h]
  float fTransformedX; // [esp+11Ch] [ebp-80h]
  tVec3 *pCoords; // [esp+120h] [ebp-7Ch]
  signed int iProcessedPols; // [esp+124h] [ebp-78h]
  int iPolIdx; // [esp+128h] [ebp-74h]
  eCarDesignIndex carDesign; // [esp+12Ch] [ebp-70h]
  uint32 uiColorTo_1; // [esp+130h] [ebp-6Ch]
  int iIsBack; // [esp+134h] [ebp-68h]
  uint32 uiCarDesignOffset; // [esp+138h] [ebp-64h]
  int iAnimFrame; // [esp+13Ch] [ebp-60h]
  float fCarPosY; // [esp+140h] [ebp-5Ch]
  float fRotMat20; // [esp+144h] [ebp-58h]
  float fRotMat10; // [esp+148h] [ebp-54h]
  float fRotMat12; // [esp+14Ch] [ebp-50h]
  float fRotMat02; // [esp+150h] [ebp-4Ch]
  float fRotMat00; // [esp+154h] [ebp-48h]
  float fCarCenterViewZ; // [esp+15Ch] [ebp-40h]
  float fCarCenterViewY; // [esp+160h] [ebp-3Ch]
  float fCarCenterViewX; // [esp+164h] [ebp-38h]
  float fEdge1YTemp; // [esp+178h] [ebp-24h]
  int iTexturesEnabled; // [esp+180h] [ebp-1Ch]
  tAnimation *pAnms; // [esp+184h] [ebp-18h]

  pScreenBuffer = pScrBuf;
  carDesign = iCarDesignIndex;

  // Calculate world position from angle and distance
  worldx = -fDistance * tcos[iAngle];
  worldz = fDistance * tsin[iAngle];
  iTexturesEnabled = -1;
  worldy = 0.0;
  vdirection = 0;
  velevation = -iAngle & 0x3FFF;
  vtilt = 0;

  // Set up view transformation
  calculatetransform(-1, 0, -iAngle & 0x3FFF, 0, worldx, 0.0, worldz, 0.0, 0.0, 0.0);
  worlddirn = vdirection;
  worldelev = velevation;
  worldtilt = vtilt;

  // Get car design data
  iNumCoords = CarDesigns[iCarDesignIndex].byNumCoords;
  iNumPols = CarDesigns[iCarDesignIndex].byNumPols;
  pAnms = CarDesigns[iCarDesignIndex].pAnms;

  // Build rotation matrix from car orientation
  iYaw = Car[0].nYaw;
  fRotMat00 = tcos[iYaw] * tcos[Car[0].nPitch];
  fRotMat01 = tsin[iYaw] * tcos[Car[0].nPitch];
  dCosYaw = tcos[iYaw];
  fRotMat02 = tsin[Car[0].nPitch];
  fRotMat11 = (float)dCosYaw * fRotMat02 * tsin[Car[0].nRoll] - tsin[iYaw] * tcos[Car[0].nRoll];
  fRotMat12 = tsin[iYaw] * fRotMat02 * tsin[Car[0].nRoll] + tcos[iYaw] * tcos[Car[0].nRoll];
  fRotMat21 = -tsin[Car[0].nRoll] * tcos[Car[0].nPitch];
  fRotMat10 = -tcos[iYaw] * fRotMat02 * tcos[Car[0].nRoll] - tsin[iYaw] * tsin[Car[0].nRoll];
  fRotMat22 = -tsin[iYaw] * fRotMat02 * tcos[Car[0].nRoll] + tcos[iYaw] * tsin[Car[0].nRoll];
  dCosPitch = tcos[Car[0].nPitch];
  fCarPosX = Car[0].pos.fX;
  fCarPosY = Car[0].pos.fY;
  dCosRoll = dCosPitch * tcos[Car[0].nRoll];
  fCarPosZ = Car[0].pos.fZ;

  // Get car bounding box and color remap info
  pCarBoxAy = CarBox.hitboxAy[iCarDesignIndex];
  uiColorTo = car_flat_remap[iCarDesignIndex].uiColorTo;
  uiColorFrom = car_flat_remap[iCarDesignIndex].uiColorFrom;
  uiColorTo_1 = uiColorTo;
  uiVertIdx = 0;
  iScrSize = scr_size;
  fRotMat20 = (float)dCosRoll;

  // Transform and project car bounding box vertices (first 4 vertices)
  do {
    // Get bounding box vert
    dBoxX = pCarBoxAy->fX;
    dBoxY = pCarBoxAy->fY;
    dBoxZ = pCarBoxAy->fZ;

    // Transform to world space
    CarPt[uiVertIdx / 8].world.fX = (float)(fRotMat00 * dBoxX + fRotMat11 * dBoxY + fRotMat10 * dBoxZ + fCarPosX);
    CarPt[uiVertIdx / 8].world.fY = (float)(fRotMat01 * dBoxX + fRotMat12 * dBoxY + fRotMat22 * dBoxZ + fCarPosY);
    CarPt[uiVertIdx / 8].world.fZ = (float)(fRotMat02 * dBoxX + fRotMat21 * dBoxY + fRotMat20 * dBoxZ + fCarPosZ);

    // Transform to view space
    dDeltaX = CarPt[uiVertIdx / 8].world.fX - viewx;
    dDeltaY = CarPt[uiVertIdx / 8].world.fY - viewy;
    dDeltaZ = CarPt[uiVertIdx / 8].world.fZ - viewz;
    fViewX = (float)(dDeltaX * vk1 + dDeltaY * vk4 + dDeltaZ * vk7);
    fViewY = (float)(dDeltaX * vk2 + dDeltaY * vk5 + dDeltaZ * vk8);
    dViewZ = dDeltaX * vk3 + dDeltaY * vk6 + dDeltaZ * vk9;
    fNearClip = (float)dViewZ;
    ++pCarBoxAy;
    fClippedZ = fNearClip;

    // Apply near clipping plane
    if (dViewZ < 80.0)
      fNearClip = 80.0;

    // Project to screen space
    dViewDist = (double)VIEWDIST;
    dInvZ = 1.0 / fNearClip;
    dScreenX = dViewDist * fViewX * dInvZ + (double)xbase;
    //_CHP
    xp = (int)dScreenX;
    dScreenY = dInvZ * (dViewDist * fViewY) + (double)ybase;
    //_CHP
    yp = (int)dScreenY;

    // Store screen coords (scaled by 64 for fixed-point math)
    CarPt[uiVertIdx / 8].screen.x = (xp * iScrSize) >> 6;
    CarPt[uiVertIdx / 8].screen.y = (iScrSize * (199 - yp)) >> 6;

    // Store view space coords
    CarPt[uiVertIdx / 8].view.fX = fViewX;
    CarPt[uiVertIdx / 8].view.fY = fViewY;
    CarPt[uiVertIdx / 8].view.fZ = fClippedZ;

    uiVertIdx += 8;
  } while (uiVertIdx != 32);

  // Draw car shadow
  CarPol.vertices[0] = CarPt[0].screen;
  CarPol.vertices[1] = CarPt[1].screen;
  CarPol.vertices[2] = CarPt[2].screen;
  CarPol.vertices[3] = CarPt[3].screen;
  CarPol.iSurfaceType = SURFACE_FLAG_FLIP_BACKFACE | SURFACE_FLAG_TRANSPARENT | 2; //0x202002
  CarPol.uiNumVerts = 4;
  POLYFLAT(pScreenBuffer, &CarPol);

  // Calculate car center position in view space
  dCarCenterX = fCarPosX - viewx;
  dCarCenterY = fCarPosY - viewy;
  dCarCenterZ = fCarPosZ - viewz;
  fCarCenterViewX = (float)(fRotMat00 * dCarCenterX + fRotMat01 * dCarCenterY + fRotMat02 * dCarCenterZ);
  fCarCenterViewY = (float)(fRotMat11 * dCarCenterX + fRotMat12 * dCarCenterY + fRotMat21 * dCarCenterZ);
  iVisiblePols = 0;
  pCoords = CarDesigns[carDesign].pCoords;
  pPols = CarDesigns[carDesign].pPols;
  fCarCenterViewZ = (float)(dCarCenterX * fRotMat10 + dCarCenterY * fRotMat22 + dCarCenterZ * fRotMat20);

  // Clear vertex processing flags
  memset(car_persps, 0, 4 * iNumCoords);
  iPolIdx = 0;

  // Process all pols for visibility
  if (iNumPols > 0) {
    uiZOrderOffset = 0;
    do {
      // Get pointers to the 3D vertices for all 4 corners of this polygon
      for ( i = 0; i < 4; i++ )
      {
        // Get vertex index from polygon definition
        uint8 vertexIndex = pPols->verts[i];
        
        // Store pointer to the 3D vertex coordinates
        vertDataAy[i] = &pCoords[vertexIndex];
      }

      // Calcualte normal vector for backface culling
      dEdge1X = vertDataAy[0]->fX - vertDataAy[2]->fX;
      fEdge1YTemp = vertDataAy[0]->fY - vertDataAy[2]->fY;
      dEdge2X = vertDataAy[1]->fX - vertDataAy[3]->fX;
      dEdge2Y = vertDataAy[1]->fY - vertDataAy[3]->fY;
      dEdge2Z = vertDataAy[1]->fZ - vertDataAy[3]->fZ;
      dDotProduct = ((vertDataAy[0]->fX + vertDataAy[1]->fX + vertDataAy[2]->fX + vertDataAy[3]->fX) * 0.25
                   + fCarCenterViewX)
        * (fEdge1YTemp * dEdge2Z - (vertDataAy[0]->fZ - vertDataAy[2]->fZ) * dEdge2Y)
        + ((vertDataAy[0]->fY + vertDataAy[1]->fY + vertDataAy[2]->fY + vertDataAy[3]->fY) * 0.25
         + fCarCenterViewY)
        * ((vertDataAy[0]->fZ - vertDataAy[2]->fZ) * dEdge2X - dEdge2Z * dEdge1X)
        + (0.25 * (vertDataAy[0]->fZ + vertDataAy[1]->fZ + vertDataAy[2]->fZ + vertDataAy[3]->fZ)
         + fCarCenterViewZ)
        * (dEdge2Y * dEdge1X - dEdge2X * fEdge1YTemp);

      // Process visible pols
      if ((pPols->uiTex & SURFACE_FLAG_FLIP_BACKFACE) != 0 || dDotProduct <= 0.0)
      {
        // Transform verts if not already done
        pPol = pPols;
        for (j = 0; j != 4; ++j) {
          byVertIdx = pPol->verts[j];
          if (!car_persps[byVertIdx])         // vert not yet transformed
          {
            car_persps[byVertIdx] = -1;         // Mark as processed
            pVertData = vertDataAy[j];
            dLocalX = pVertData->fX;
            dLocalY = pVertData->fY;
            dLocalZ = pVertData->fZ;

            // scale for tinycars
            if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0)
            {
              dLocalX = dLocalX * 0.25;
              dLocalY = 0.25 * dLocalY;
            }

            // Transform to world space
            dVertX = dLocalX;
            dVertY = dLocalY;
            iVertIdx_1 = byVertIdx;
            dVertZ = dLocalZ;
            CarPt[iVertIdx_1].world.fX = (float)(fRotMat00 * dLocalX + fRotMat11 * dLocalY + fRotMat10 * dLocalZ + fCarPosX);
            CarPt[iVertIdx_1].world.fY = (float)(fRotMat01 * dVertX + fRotMat12 * dVertY + fRotMat22 * dVertZ + fCarPosY);
            CarPt[iVertIdx_1].world.fZ = (float)(dVertX * fRotMat02 + dVertY * fRotMat21 + dVertZ * fRotMat20 + fCarPosZ);

            // Transform to view space
            dDeltaX_1 = CarPt[byVertIdx].world.fX - viewx;
            dDeltaY_1 = CarPt[byVertIdx].world.fY - viewy;
            dDeltaZ_1 = CarPt[byVertIdx].world.fZ - viewz;
            fTransformedX = (float)(dDeltaX_1 * vk1 + dDeltaY_1 * vk4 + dDeltaZ_1 * vk7);
            fTransformedY = (float)(dDeltaX_1 * vk2 + dDeltaY_1 * vk5 + dDeltaZ_1 * vk8);
            dViewX = dDeltaX_1 * vk3 + dDeltaY_1 * vk6 + dDeltaZ_1 * vk9;
            fClampedZ = (float)dViewX;
            fOriginalZ = fClampedZ;

            // Apply near clipping
            if (dViewX < 80.0)
              fClampedZ = 80.0;

            // Project to screen
            dViewDist_1 = (double)VIEWDIST;
            dInvZ_1 = 1.0 / fClampedZ;
            dProjX = dViewDist_1 * fTransformedX * dInvZ_1 + (double)xbase;
            //_CHP
            xp = (int)dProjX;
            dProjY = dInvZ_1 * (dViewDist_1 * fTransformedY) + (double)ybase;
            iScreenX = (int)dProjX * scr_size;
            //_CHP
            yp = (int)dProjY;

            // Store transformed data
            iVertIdx_2 = byVertIdx;
            CarPt[iVertIdx_2].screen.x = iScreenX >> 6;
            CarPt[iVertIdx_2].screen.y = (scr_size * (199 - yp)) >> 6;
            CarPt[iVertIdx_2].view.fX = fTransformedX;
            CarPt[iVertIdx_2].view.fY = fTransformedY;
            CarPt[iVertIdx_2].view.fZ = fOriginalZ;
          }
        }

        // Determine pol index and facing dir
        if (dDotProduct > 0.0 && (pPols->uiTex & SURFACE_FLAG_BACK) != 0)
        {
          uiZOrderOffset_3 = uiZOrderOffset;
          iPolIdx_1 = -iPolIdx - 1;             // negative index
        } else {
          uiZOrderOffset_3 = uiZOrderOffset;
          iPolIdx_1 = iPolIdx;
        }

        // Store pol data for depth sorting
        CarZOrder[uiZOrderOffset_3 / sizeof(tCarZOrderEntry)].iPolygonIndex = iPolIdx_1;
        CarZOrder[uiZOrderOffset / sizeof(tCarZOrderEntry)].iPolygonLink = pPols->nNextPolIdx;

        // Calculate max z val for depth sorting
        if ((pPols->uiTex & CAR_FLAG_ANMS_LIVERY) == 0)
        {
          // Find max Z among verts (furthest from camera)
          if (CarPt[pPols->verts[2]].view.fZ <= (double)CarPt[pPols->verts[3]].view.fZ)
            fMinZ34 = CarPt[pPols->verts[3]].view.fZ;
          else
            fMinZ34 = CarPt[pPols->verts[2]].view.fZ;
          fMaxZ34 = fMinZ34;
          if (CarPt[pPols->verts[0]].view.fZ <= (double)CarPt[pPols->verts[1]].view.fZ)
            fMinZ12 = CarPt[pPols->verts[1]].view.fZ;
          else
            fMinZ12 = CarPt[pPols->verts[0]].view.fZ;
          fMaxZ12 = fMinZ12;
          if (fMinZ12 <= (double)fMaxZ34) {
            if (CarPt[pPols->verts[2]].view.fZ <= (double)CarPt[pPols->verts[3]].view.fZ)
              fMaxZ = CarPt[pPols->verts[3]].view.fZ;
            else
              fMaxZ = CarPt[pPols->verts[2]].view.fZ;
            fFinalMinZ = fMaxZ;
          } else {
            if (CarPt[pPols->verts[0]].view.fZ <= (double)CarPt[pPols->verts[1]].view.fZ)
              fMaxZ = CarPt[pPols->verts[1]].view.fZ;
            else
              fMaxZ = CarPt[pPols->verts[0]].view.fZ;
            fFinalMaxZ_1 = fMaxZ;
          }
          fFinalMaxZ = fMaxZ;
          uiZOrderOffset_2 = uiZOrderOffset;
        } else {
          // Find minimum Z among vertices (closest to camera)
          if (CarPt[pPols->verts[2]].view.fZ >= (double)CarPt[pPols->verts[3]].view.fZ)
            fMinZ34_1 = CarPt[pPols->verts[3]].view.fZ;
          else
            fMinZ34_1 = CarPt[pPols->verts[2]].view.fZ;
          fMinZ34Temp = fMinZ34_1;
          if (CarPt[pPols->verts[0]].view.fZ >= (double)CarPt[pPols->verts[1]].view.fZ)
            fMinZ12_1 = CarPt[pPols->verts[1]].view.fZ;
          else
            fMinZ12_1 = CarPt[pPols->verts[0]].view.fZ;
          fMinZ12Temp = fMinZ12_1;
          if (fMinZ12_1 >= (double)fMinZ34Temp) {
            if (CarPt[pPols->verts[2]].view.fZ >= (double)CarPt[pPols->verts[3]].view.fZ)
              fMaxZ = CarPt[pPols->verts[3]].view.fZ;
            else
              fMaxZ = CarPt[pPols->verts[2]].view.fZ;
            fMinZTemp = fMaxZ;
          } else {
            if (CarPt[pPols->verts[0]].view.fZ >= (double)CarPt[pPols->verts[1]].view.fZ)
              fMaxZ = CarPt[pPols->verts[1]].view.fZ;
            else
              fMaxZ = CarPt[pPols->verts[0]].view.fZ;
            fMaxZTemp = fMaxZ;
          }
          fMaxZTemp_1 = fMaxZ;
          uiZOrderOffset_2 = uiZOrderOffset;
        }

        // Store Z value for sorting
        CarZOrder[uiZOrderOffset_2 / sizeof(tCarZOrderEntry)].fZDepth = fMaxZ;
        ++iVisiblePols;
        uiZOrderOffset += 12;
      }
      ++pPols;
      ++iPolIdx;
    } while (iPolIdx < iNumPols);
  }

  // Render visible pols
  if (iVisiblePols > 0) {
    iProcessedPols = 0;
    uiDesignOffset = 28 * carDesign;
    pFirstPol = CarDesigns[carDesign].pPols;
    uiZOrderOffset_1 = 0;
    pPolAy = pFirstPol;

    // Build depth hierarchy based on polygon links
    do {
      iLinkedPolIdx = CarZOrder[uiZOrderOffset_1 / 0xC].iPolygonIndex;
      if (iLinkedPolIdx < 0)
        iLinkedPolIdx = -1 - iLinkedPolIdx;     // Convert to negative index

      // Follow polygon dependency chain
      iNextPolIdx = pPolAy[iLinkedPolIdx].nNextPolIdx;
      iCurPolIdx = iProcessedPols;
      if (iNextPolIdx >= 0) {
        pLinkedPol = CarDesigns[uiDesignOffset / 0x1C].pPols;
        do {
          iSearchIdx = -1;

          // Find pol in Z-order list
          for (k = 0; k < iVisiblePols; ++k) {
            iCheckPolIdx = CarZOrder[k].iPolygonIndex;
            if (iCheckPolIdx < 0)
              iCheckPolIdx = -1 - iCheckPolIdx;
            if (iNextPolIdx == iCheckPolIdx) {
              iSearchIdx = k;
              k = iVisiblePols;                 // exit loop
            }
          }

          // Adjust Z value to ensure proper ordering
          if (iSearchIdx >= 0)
            CarZOrder[iSearchIdx].fZDepth = CarZOrder[iCurPolIdx].fZDepth + -1.0f;

          iNextPolIdx = pLinkedPol[iNextPolIdx].nNextPolIdx;
          iCurPolIdx = iSearchIdx;
        } while (iNextPolIdx >= 0);
      }
      uiZOrderOffset_1 += 12;
      ++iProcessedPols;
    } while (iVisiblePols > iProcessedPols);

    // Sort pols by depth
    qsort(CarZOrder, iVisiblePols, 0xCu, carZcmp);

    // Prepare for rendering
    iSubPolType = carDesign + 3;
    uiCarDesignIdxTimes4 = 4 * carDesign;
    uiCarDesignOffset = 28 * carDesign;
    iAnimFrame = byAnimFrame & 1;
    iDrawIdx = 0;
    iTotalZOrderBytes = 12 * iVisiblePols;

    // Draw pols in depth order
    do {
      // Get pol to draw
      iPolToDraw = CarZOrder[iDrawIdx / 0xCu].iPolygonIndex;
      if (iPolToDraw >= 0) {
        iIsBack = 0;
      } else {
        iIsBack = -1;
        iPolToDraw = -1 - iPolToDraw;
      }

      pDrawPol = &CarDesigns[uiCarDesignOffset / 0x1C].pPols[iPolToDraw];
      uiTex = pDrawPol->uiTex;

      // Get screen verts for pol
      for ( m = 0; m < 4; m++ )
      {
        // Get screen coordinates for this vertex
        screenVertAy[m] = &CarPt[pDrawPol->verts[m]];
      }

      // Find min Z for near plane clipping check
      dViewZ_1 = screenVertAy[2]->view.fZ;
      CarPol.uiNumVerts = 4;
      if (dViewZ_1 >= screenVertAy[3]->view.fZ)
        pMinZVert34 = screenVertAy[3];
      else
        pMinZVert34 = screenVertAy[2];
      fMinZ = pMinZVert34->view.fZ;

      if (screenVertAy[0]->view.fZ >= (double)screenVertAy[1]->view.fZ)
        pMinZVert12 = screenVertAy[1];
      else
        pMinZVert12 = screenVertAy[0];
      fMinZ_1 = pMinZVert12->view.fZ;

      if (fMinZ_1 >= (double)fMinZ) {
        if (screenVertAy[2]->view.fZ >= (double)screenVertAy[3]->view.fZ)
          pMinZVert34_1 = screenVertAy[3];
        else
          pMinZVert34_1 = screenVertAy[2];
        fMinZ34_2 = pMinZVert34_1->view.fZ;
        fMinViewZ = fMinZ34_2;
      } else {
        if (screenVertAy[0]->view.fZ >= (double)screenVertAy[1]->view.fZ)
          pMinZVert12_1 = screenVertAy[1];
        else
          pMinZVert12_1 = screenVertAy[0];
        fMinZ12_2 = pMinZVert12_1->view.fZ;
        fMinViewZ = fMinZ12_2;
      }
      fMinZTemp_1 = fMinViewZ;

      // Copy screen coords to pol struct
      CarPol.vertices[0] = screenVertAy[0]->screen;
      CarPol.vertices[1] = screenVertAy[1]->screen;
      CarPol.vertices[2] = screenVertAy[2]->screen;
      CarPol.vertices[3] = screenVertAy[3]->screen;

      // Handle backs
      if (iIsBack) {
        uiTex = CarDesigns[uiCarDesignOffset / 0x1C].pBacks[iPolToDraw];
        uiTex |= SURFACE_FLAG_FLIP_BACKFACE; 
      } else if ((uiTex & CAR_FLAG_ANMS_LOOKUP) != 0)
      {
        if ((uint8)uiTex >= 4u)
          uiTex = pAnms[(uint8)uiTex].framesAy[iAnimFrame];
        else
          uiTex = pAnms[(uint8)uiTex].framesAy[0];
      }

      // Apply color remapping if textures are disabled
      if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0 && (uiTex & SURFACE_FLAG_APPLY_TEXTURE) == 0 && (uint8)uiTex == uiColorFrom)
        uiTex = uiColorTo_1;

      CarPol.iSurfaceType = uiTex;

      // Render pol
      if (fMinViewZ >= 1.0) {
        if ((uiTex & SURFACE_FLAG_APPLY_TEXTURE) != 0 && iTexturesEnabled) {
          iCartexOffset = car_texmap[uiCarDesignIdxTimes4 / 4];
          iGfxSize = gfx_size;
          POLYTEX(cartex_vga[iCartexOffset - 1], pScreenBuffer, &CarPol, iCartexOffset, gfx_size);
        } else                                    // flat color pol
        {
          POLYFLAT(pScreenBuffer, &CarPol);
        }
      } else                                      // near plane clipping required
      {
        iGfxSize = gfx_size;
        // Subdivide pol for proper clipping
        subdivide(
          pScreenBuffer,
          &CarPol,
          screenVertAy[0]->view.fX,
          screenVertAy[0]->view.fY,
          screenVertAy[0]->view.fZ,
          screenVertAy[1]->view.fX,
          screenVertAy[1]->view.fY,
          screenVertAy[1]->view.fZ,
          screenVertAy[2]->view.fX,
          screenVertAy[2]->view.fY,
          screenVertAy[2]->view.fZ,
          screenVertAy[3]->view.fX,
          screenVertAy[3]->view.fY,
          screenVertAy[3]->view.fZ,
          iSubPolType,
          gfx_size);
      }
      iDrawIdx += 12;
    } while (iDrawIdx < iTotalZOrderBytes);
  }
}

//-------------------------------------------------------------------------------------------------
//0005B490
void championship_winner()
{
  uint8 *pbyScreenBuffer; // edi
  tBlockHeader *pChampImageData; // esi
  int iCurrentFrame; // ebp
  unsigned int uiBufferSize; // ecx
  char byBufferSizeRemainder; // al
  unsigned int uiDwordCopyCount; // ecx
  int iFrameTimer; // ebx
  uint8 *pbyAnimScreenBuf; // edi
  char *pszCurrentFrameData; // esi
  unsigned int uiAnimBufferSize; // ecx
  char byAnimRemainder; // al
  unsigned int uiAnimDwordCount; // ecx
  int iNumAnimFrames; // [esp+0h] [ebp-20h]
  int iDuration; // [esp+4h] [ebp-1Ch]

  SVGA_ON = -1;                                 // Initialize SVGA mode and full screen window for championship victory
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  setpal("champ.pal");                          // Set championship palette and load victory image
  front_vga[0] = (tBlockHeader *)try_load_picture("champ.bm");// Try to load animated championship image, fallback to static
  if (front_vga[0]) {
    iNumAnimFrames = 12;                        // Animated version has 12 frames
  } else {
    front_vga[0] = (tBlockHeader *)load_picture("chump.bm");// Static fallback version has 1 frame
    iNumAnimFrames = 1;
  }
  if (iNumAnimFrames != 1 && MusicVolume && MusicCard)// Set display duration: longer for animated with music, shorter otherwise
    iDuration = 720;
  else
    iDuration = 180;
  pbyScreenBuffer = scrbuf;
  pChampImageData = front_vga[0];
  iCurrentFrame = 0;                            // Copy first frame to screen buffer (optimized memory copy)
  if (SVGA_ON)
    uiBufferSize = 256000;
  else
    uiBufferSize = 64000;
  byBufferSizeRemainder = uiBufferSize;
  uiDwordCopyCount = uiBufferSize >> 2;
  memcpy(scrbuf, front_vga[0], 4 * uiDwordCopyCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCopyCount], &pChampImageData->iWidth + uiDwordCopyCount, byBufferSizeRemainder & 3);
  copypic(scrbuf, screen);                      // Display initial frame and start championship music
  iFrameTimer = 0;
  startmusic(winchampsong);
  enable_keyboard();
  fade_palette(32);                             // Enable input, fade in display, and initialize animation timing
  front_fade = -1;
  ticks = 0;
  frames = 1;
  do {                                             // Main animation loop - exit on keyboard input
    if (fatkbhit())
      break;
    iFrameTimer -= frames;                      // Update frame timer based on game frame rate
    frames = 0;
    if (iFrameTimer < 0)                      // Time to advance to next animation frame
    {
      pbyAnimScreenBuf = scrbuf;
      pszCurrentFrameData = (char *)front_vga[0] + 256000 * iCurrentFrame;// Calculate pointer to current animation frame data
      if (SVGA_ON)
        uiAnimBufferSize = 256000;
      else
        uiAnimBufferSize = 64000;
      byAnimRemainder = uiAnimBufferSize;
      uiAnimDwordCount = uiAnimBufferSize >> 2;
      memcpy(scrbuf, pszCurrentFrameData, 4 * uiAnimDwordCount);// Copy current frame to screen buffer and display
      memcpy(&pbyAnimScreenBuf[4 * uiAnimDwordCount], &pszCurrentFrameData[4 * uiAnimDwordCount], byAnimRemainder & 3);
      copypic(scrbuf, screen);
      do {                                         // Advance to next frame, wrap around at end of animation
        if (++iCurrentFrame == iNumAnimFrames)
          iCurrentFrame ^= iNumAnimFrames;      // Reset to frame 0 when reaching end of animation cycle
        iFrameTimer += 2;                       // Add 2 ticks to frame timer for next frame timing
        UpdateSDL();
      } while (iFrameTimer < 0);
    }
    UpdateSDL();
  } while (ticks < iDuration);                  // Continue animation until timeout or user input
  fre((void **)front_vga);                      // Clean up championship image resources
}

//-------------------------------------------------------------------------------------------------
//0005B660
void print_mem_used(const char *szMsg)
{
  printf("%s\n", szMsg);
  printf("hi mem used: %i\n", mem_used);
  printf("lo mem used: %i\n\n", mem_used_low);
}

//-------------------------------------------------------------------------------------------------
//0005B6A0
uint8 *try_load_picture(const char *szFile)
{
  uint8 *pBuf2; // ebx
  int iFile; // eax
  uint32 iLength; // eax
  uint8 *pBuf; // ecx

  pBuf2 = 0;
  iFile = ROLLERopen(szFile, O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h
  if (iFile != -1) {
    close(iFile);
    iLength = getcompactedfilelength(szFile);
    pBuf = (uint8 *)trybuffer(iLength);
    pBuf2 = pBuf;
    if (pBuf)
      loadcompactedfile(szFile, pBuf);
  }
  return pBuf2;
}

//-------------------------------------------------------------------------------------------------
//0005B6F0
void save_champ(int iSlot)
{
  char *pbySaveBuffer; // eax
  char *pBufPlus1; // eax
  uint8 byCompetitorsFlags; // dl
  char *pbyCurrentPos; // eax
  uint8 *pbyPlayerData; // eax
  int iPlayerIndex; // edx
  int iCarWithInvul; // ebx
  uint8 *pbyPlayerControlData; // eax
  int iControlType; // ebx
  int iBitMask; // ebx
  int iNonCompetitorsMask; // esi
  int iCarIndex; // edx
  uint8 *pbyAfterNonCompetitors; // eax
  uint8 *pbyAfterNetworkChamp; // eax
  uint8 *pbyAfterNetworkSlot; // eax
  uint8 *pbyAfterHeader; // eax
  int iCarStatsIndex; // esi
  int iCarArrayIndex; // ebx
  uint8 *pbyAfterPoints; // eax
  uint8 *pbyAfterKills; // eax
  uint8 *pbyAfterFasts; // eax
  int i; // ebx
  uint8 *pbyAfterTeamPoints; // eax
  uint8 *pbyAfterTeamKills; // eax
  uint8 *pbyAfterTeamFasts; // eax
  int iTeamWins; // edx
  int iNameEndIndex; // edi
  int iTeamIndex; // esi
  int iNameStartIndex; // edx
  uint8 *pbyNameChar; // eax
  char byPlayerNameChar; // cl
  uint8 *pbyAfterSerial; // eax
  uint8 *pbyAfterModemPort; // eax
  uint8 *pbyAfterModemCall; // eax
  uint8 *pbyPhoneData; // eax
  int j; // edx
  uint8 *pbyPhoneChar; // eax
  char byPhoneDigit; // bl
  uint8 *pbyEndOfData; // esi
  char byChecksum; // cl
  char *pbyChecksumPos; // eax
  //int iPlayersCarsOffset; // esi
  int64 llPlayersCarsOffset;
  int iNameIndex; // edx
  char byDataByte; // ch
  FILE *pSaveFile; // edi
  char *pbyBufferStart; // [esp+0h] [ebp-1Ch] BYREF

  pbySaveBuffer = (char *)getbuffer(0x800u);    // Allocate 2KB buffer for save data
  pbyBufferStart = pbySaveBuffer;
  *pbySaveBuffer = TrackLoad;
  pBufPlus1 = pbySaveBuffer + 1;
  byCompetitorsFlags = competitors;
  if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
    byCompetitorsFlags = competitors | 0x20;
  if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)                  // CHEAT_MODE_DEATH_MODE
    byCompetitorsFlags |= 0x40u;
  if ((cheat_mode & CHEAT_MODE_KILLER_OPPONENTS) != 0)              // CHEAT_MODE_KILLER_OPPONENTS
    byCompetitorsFlags |= 0x80u;
  pbyCurrentPos = pBufPlus1 + 1;
  *(pbyCurrentPos++ - 1) = byCompetitorsFlags;
  *(pbyCurrentPos++ - 1) = players;
  *(pbyCurrentPos - 1) = level;
  pbyPlayerData = (uint8 *)(pbyCurrentPos + 3);
  *(pbyPlayerData - 3) = damage_level;
  *(pbyPlayerData - 2) = player_type;
  *(pbyPlayerData - 1) = my_number;
  for (iPlayerIndex = 0; iPlayerIndex != 16; ++iPlayerIndex) {
    iCarWithInvul = Players_Cars[iPlayerIndex];
    if (player_invul[iPlayerIndex]) {
      //LOBYTE(iCarWithInvul) = iCarWithInvul | 0x40;
      iCarWithInvul = (iCarWithInvul & 0xFFFFFF00) | ((iCarWithInvul & 0xFF) | 0x40);
    }
    pbyPlayerControlData = pbyPlayerData + 1;
    *(pbyPlayerControlData - 1) = iCarWithInvul;
    pbyPlayerData = pbyPlayerControlData + 1;
    iControlType = manual_control[iPlayerIndex];
    *(pbyPlayerData - 1) = iControlType;
  }
  iBitMask = 1;
  iNonCompetitorsMask = 0;
  if (numcars > 0) {
    iCarIndex = 0;
    do {
      if (non_competitors[iCarIndex])
        iNonCompetitorsMask |= iBitMask;
      ++iCarIndex;
      iBitMask *= 2;
    } while (iCarIndex < numcars);
  }
  pbyAfterNonCompetitors = sav_champ_int(pbyPlayerData, iNonCompetitorsMask);
  pbyAfterNetworkChamp = sav_champ_int(pbyAfterNonCompetitors, network_champ_on);
  pbyAfterNetworkSlot = sav_champ_int(pbyAfterNetworkChamp, network_slot);
  pbyAfterHeader = sav_champ_int(pbyAfterNetworkSlot, net_type);
  iCarStatsIndex = 0;
  if (numcars > 0) {
    iCarArrayIndex = 0;
    do {
      pbyAfterPoints = sav_champ_int(pbyAfterHeader, championship_points[iCarArrayIndex]);
      pbyAfterKills = sav_champ_int(pbyAfterPoints, total_kills[iCarArrayIndex]);
      pbyAfterFasts = sav_champ_int(pbyAfterKills, total_fasts[iCarArrayIndex]);
      ++iCarStatsIndex;
      pbyAfterHeader = sav_champ_int(pbyAfterFasts, total_wins[iCarArrayIndex++]);
    } while (iCarStatsIndex < numcars);
  }
  for (i = 0; i != 8; ++i) {
    pbyAfterTeamPoints = sav_champ_int(pbyAfterHeader, team_points[i]);
    pbyAfterTeamKills = sav_champ_int(pbyAfterTeamPoints, team_kills[i]);
    pbyAfterTeamFasts = sav_champ_int(pbyAfterTeamKills, team_fasts[i]);
    iTeamWins = team_wins[i];
    pbyAfterHeader = sav_champ_int(pbyAfterTeamFasts, iTeamWins);
  }

  iNameEndIndex = 9;
  for (iTeamIndex = 0; iTeamIndex < 16; ++iTeamIndex) {
    iNameStartIndex = 9 * iTeamIndex;
    do {
      pbyNameChar = pbyAfterHeader + 1;
      *(pbyNameChar - 1) = default_names[0][iNameStartIndex];
      pbyAfterHeader = pbyNameChar + 1;
      byPlayerNameChar = player_names[0][iNameStartIndex++];
      *(pbyAfterHeader - 1) = byPlayerNameChar;
    } while (iNameStartIndex != iNameEndIndex);
    iNameEndIndex += 9;
  }

  pbyAfterSerial = sav_champ_int(pbyAfterHeader, serial_port);
  pbyAfterModemPort = sav_champ_int(pbyAfterSerial, modem_port);
  pbyAfterModemCall = sav_champ_int(pbyAfterModemPort, modem_call);
  pbyPhoneData = sav_champ_int(pbyAfterModemCall, modem_baud) + 1;
  *(pbyPhoneData - 1) = modem_phone[0];
  for (j = 1; j <= 50; j += 5) {
    pbyPhoneChar = pbyPhoneData + 1;
    *(pbyPhoneChar++ - 1) = modem_phone[j];
    *(pbyPhoneChar++ - 1) = modem_phone[j + 1];
    *(pbyPhoneChar++ - 1) = modem_phone[j + 2];
    *(pbyPhoneChar - 1) = modem_phone[j + 3];
    pbyPhoneData = pbyPhoneChar + 1;
    byPhoneDigit = modem_phone[j + 4];
    *(pbyPhoneData - 1) = byPhoneDigit;
  }
  pbyEndOfData = pbyPhoneData;
  byChecksum = 0;
  pbyChecksumPos = pbyBufferStart;
  
  llPlayersCarsOffset = pbyEndOfData - (uint8 *)pbyBufferStart;
  for (iNameIndex = 0; iNameIndex < llPlayersCarsOffset; byChecksum += byDataByte) {
    ++iNameIndex;
    byDataByte = *pbyChecksumPos++;
  }
  
  *pbyChecksumPos = byChecksum;
  pSaveFile = ROLLERfopen(save_slots[iSlot - 1], "wb");
  if (pSaveFile)//check added by ROLLER
    fwrite(pbyBufferStart, 1, llPlayersCarsOffset + 1, pSaveFile);
  fclose(pSaveFile);
  fre((void **)&pbyBufferStart);
}

//-------------------------------------------------------------------------------------------------
//0005B9A0
int load_champ(int iSlot)
{
  int iFileHandle; // edx
  int iFileLength; // esi
  char *pbyCurrentPos; // eax
  char byChecksum; // cl
  int iChecksumLoop; // edx
  char byCurrentByte; // ch
  int iSavedRacers; // edi
  char byGameSettings; // al
  //uint32 uiTempCheatMode; // edx
  //uint32 uiTempCheatMode2; // ecx
  uint8 *pbyPlayerData; // ebx
  //int i; // eax
  uint8 byPlayerByte; // dl
  //uint8 *pbyNextPlayerByte; // ebx
  int *piDataPointer; // ecx
  int iPlayerSecondByte; // edx
  int iBitFlag; // ebx
  int *piNextData; // edx
  int iNonCompetitorFlags; // eax
  //int iArraySize; // esi
  //int iByteOffset; // eax
  int iFlags = 0; // ebp
  int iFlagCheck; // ecx
  int *piStatsPointer; // edx
  int iNetType; // eax
  int *piTeamStatsPointer; // edx
  int iStatsLoop; // eax
  int *piTotalWinsPtr; // ecx
  int *piTotalFastsPtr; // esi
  int *piTotalKillsPtr; // ebx
  int *piChampionshipPointsPtr; // edi
  int iTeamStatsValue; // ebp
  int *piNextTeamData; // edx
  int iSecondTeamValue; // ebp
  int *piTeamWinsPtr; // ebx
  int *piTeamPointsPtr; // eax
  int *piTeamFastsPtr; // esi
  int *piTeamKillsPtr; // ecx
  int iCurrentTeamValue; // edi
  int *piTeamDataPtr; // edx
  int iTeamKillsValue; // edi
  int iNameEndIndex; // edi
  int iTeamIndex; // ecx
  int iNameIndex; // eax
  char byNameChar; // bl
  uint8 *pbyNamePtr; // edx
  char *pszTempPointer; // ebx
  int iSerialPortValue; // eax
  int *piModemDataPtr; // edx
  int iModemPortValue; // eax
  int iModemCallValue; // eax
  int iModemBaudValue; // eax
  char *pszPhonePtr; // edx
  //int j; // eax
  //char byPhoneChar1; // bl
  //char *pszPhoneCharPtr; // edx
  //char byPhoneChar2; // bl
  //char byPhoneChar3; // bl
  //char byPhoneChar4; // bl
  int iDriverLoop; // esi
  char *pszDriverNamePtr; // ebp
  int iDriverIndex; // edi
  char *pszSourceNamePtr; // edx
  char *pszDefaultNamePtr; // eax
  char byDriverNameChar; // cl
  int iHumanPlayerLoop; // edi
  int iPlayerCarIndex; // esi
  char *pszPlayerNamePtr; // ebp
  int iPlayerCarValue; // eax
  int iCarSlotIndex; // edx
  int iHumanControlLoop; // eax
  int iControlCheck; // ebx
  char *pszTargetNamePtr; // edx
  char *pszSourcePlayerPtr; // eax
  char byPlayerNameChar; // cl
  //int iControlArraySize; // edx
  //unsigned int uiControlLoop; // edi
  int iCompetitorCount; // edi
  signed int iCompetitorCheck; // esi
  //int iOrderSearchStart; // edx
  //unsigned int uiOrderByteOffset; // ecx
  //int k; // eax
  int iHighestPoints; // ecx
  //int iSortCurrentIndex; // ebp
  //int iSortTotalRacers; // edi
  //int iSortSearchIndex; // eax
  //int iSortCompareIndex; // edx
  //int iSwapTempValue; // edx
  //int iNextSortIndex; // eax
  uint8 *pFileBuf; // [esp+0h] [ebp-30h] BYREF
  int iChecksumOk; // [esp+4h] [ebp-2Ch]
  int *piTeamPointsEnd; // [esp+8h] [ebp-28h]
  int iFlags2; // [esp+Ch] [ebp-24h]
  char *pszDefaultNameEnd; // [esp+10h] [ebp-20h]
  signed int iSortIndex; // [esp+14h] [ebp-1Ch]

  iFileLength = ROLLERfilelength(save_slots[iSlot - 1]);

  iFileHandle = ROLLERopen(save_slots[iSlot - 1], O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h
  iChecksumOk = 0;
  if (iFileHandle != -1) {
    pFileBuf = (uint8 *)getbuffer(0x800u);      // Allocate buffer and read save file (expected size: 795 bytes)
    
    //iFileLength = _filelength(iFileHandle);
    if (iFileLength == 795)
      read(iFileHandle, pFileBuf, 795);
    close(iFileHandle);

    if (iFileLength == 795) {
      pbyCurrentPos = (char *)pFileBuf;         // CHECKSUM VALIDATION: Calculate checksum of first 794 bytes
      byChecksum = 0;
      for (iChecksumLoop = 0; iChecksumLoop < 794; ++iChecksumLoop) {
        byCurrentByte = *pbyCurrentPos++;
        byChecksum += byCurrentByte;
      }
      if (*pbyCurrentPos == byChecksum)       // Verify checksum matches byte 795 - if valid, proceed with load
        iChecksumOk = -1;
    }
    if (iChecksumOk) {
      iSavedRacers = racers;                    // NETWORK CLEANUP: Disconnect from network before loading saved state
      broadcast_mode = -666;
      while (broadcast_mode)
        UpdateSDL();
      tick_on = 0;
      TrackLoad = *pFileBuf;                    // BASIC GAME SETTINGS: Load track, competitors, texture/cheat flags
      byGameSettings = pFileBuf[1];
      competitors = byGameSettings & 0x1F;      // Parse game settings byte: bits 0-4=competitors, bit 5=textures, bit 6=cheat, bit 7=network cheat
      if ((byGameSettings & 0x20) != 0)
        textures_off |= TEX_OFF_ADVANCED_CARS;
      else
        textures_off &= ~TEX_OFF_ADVANCED_CARS;
      if ((byGameSettings & 0x40) != 0) {
        cheat_mode |= CHEAT_MODE_DEATH_MODE;
      } else {
        cheat_mode &= ~CHEAT_MODE_DEATH_MODE;
      }
      if (byGameSettings >= 0) {
        //uiTempCheatMode2 = cheat_mode;
        //BYTE1(uiTempCheatMode2) = BYTE1(cheat_mode) & 0xFD;
        cheat_mode &= ~CHEAT_MODE_KILLER_OPPONENTS;// uiTempCheatMode2;
      } else {
        cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
      }
      players = pFileBuf[2];                    // Load player count, difficulty level, damage level, and player type
      level = pFileBuf[3];
      damage_level = pFileBuf[4];
      player_type = pFileBuf[5];
      pbyPlayerData = pFileBuf + 7;
      my_number = pFileBuf[6];

      for (int i = 0; i < 16; i++) {
        byPlayerByte = *pbyPlayerData++;
        iPlayerSecondByte = *pbyPlayerData++;

        // Store player car selection (bits 0-4 of first byte)
        Players_Cars[i] = byPlayerByte & 0x1F;

        // Store player invulnerability status (bit 6: 0=vulnerable, 1=invulnerable)
        player_invul[i] = ((byPlayerByte & 0x40) == 0) - 1;

        // Store manual control flags for this player
        manual_control[i] = iPlayerSecondByte;

        // Update data pointer to current position (as int pointer for next section)
        piDataPointer = (int *)pbyPlayerData;
      }
      //for (i = 0; i != 16; *(int *)((char *)&competitors + i * 4) = iPlayerSecondByte)// Load 16 players' car choices and starting status
      //{
      //  byPlayerByte = *pbyPlayerData;
      //  pbyNextPlayerByte = pbyPlayerData + 1;
      //  *(int *)((char *)&infinite_laps + ++i * 4) = byPlayerByte & 0x1F;
      //  player_started[i + 15] = ((byPlayerByte & 0x40) == 0) - 1;
      //  piDataPointer = (int *)(pbyNextPlayerByte + 1);
      //  iPlayerSecondByte = *pbyNextPlayerByte;
      //  pbyPlayerData = pbyNextPlayerByte + 1;
      //}

      iBitFlag = 1;
      piNextData = piDataPointer + 1;
      iNonCompetitorFlags = *piDataPointer;
      racers = iSavedRacers;
      iFlags2 = iNonCompetitorFlags;
      if (numcars > 0)                        // NON-COMPETITOR FLAGS: Parse bit flags to determine which cars are competitors
      {
        if (numcars > 0) {
          for (int i = 0; i < numcars; i++) {
            // Check if bit i is set in the flags - if clear, car is a non-competitor
            iFlagCheck = ((iFlags & iBitFlag) == 0) - 1;
            non_competitors[i] = iFlagCheck;
            iBitFlag *= 2;  // Move to next bit position
          }
        }
        //iArraySize = 4 * numcars;
        //iByteOffset = 0;
        //iFlags = iFlags2;
        //do {
        //  iByteOffset += 4;
        //  iFlagCheck = ((iFlags & iBitFlag) == 0) - 1;
        //  iBitFlag *= 2;
        //  TrackArrow_variable_1[iByteOffset / 4u] = iFlagCheck;// offset into non_competitors
        //} while (iByteOffset < iArraySize);
      }
      piStatsPointer = piNextData + 1;
      network_champ_on = *(piStatsPointer++ - 1);// NETWORK SETTINGS: Load network championship flag, slot, and type
      network_slot = *(piStatsPointer - 1);
      iNetType = *piStatsPointer;
      piTeamStatsPointer = piStatsPointer + 1;
      net_type = iNetType;
      ROLLERCommsSetType(iNetType);
      iStatsLoop = 0;
      if (numcars > 0)                        // INDIVIDUAL STATISTICS: Load championship points, kills, fastest laps, wins for each car
      {
        piTotalWinsPtr = total_wins;
        piTotalFastsPtr = total_fasts;
        piTotalKillsPtr = total_kills;
        piChampionshipPointsPtr = championship_points;
        do {
          iTeamStatsValue = *piTeamStatsPointer;
          piNextTeamData = piTeamStatsPointer + 1;
          ++piTotalWinsPtr;
          *piChampionshipPointsPtr = iTeamStatsValue;
          ++piTotalKillsPtr;
          ++piTotalFastsPtr;
          *(piTotalKillsPtr - 1) = *piNextTeamData;
          ++piChampionshipPointsPtr;
          iSecondTeamValue = piNextTeamData[1];
          ++piNextTeamData;
          *(piTotalFastsPtr - 1) = iSecondTeamValue;
          ++iStatsLoop;
          *(piTotalWinsPtr - 1) = piNextTeamData[1];
          piTeamStatsPointer = piNextTeamData + 2;
        } while (iStatsLoop < numcars);
      }
      piTeamWinsPtr = team_wins;                // TEAM STATISTICS: Load team points, kills, fastest laps, wins for 8 teams
      piTeamPointsPtr = team_points;
      piTeamFastsPtr = team_fasts;
      piTeamKillsPtr = team_kills;
      piTeamPointsEnd = &team_points[8];
      do {
        iCurrentTeamValue = *piTeamStatsPointer;
        piTeamDataPtr = piTeamStatsPointer + 1;
        *piTeamPointsPtr = iCurrentTeamValue;
        ++piTeamWinsPtr;
        ++piTeamFastsPtr;
        *piTeamKillsPtr++ = *piTeamDataPtr;
        iTeamKillsValue = piTeamDataPtr[1];
        ++piTeamDataPtr;
        *(piTeamFastsPtr - 1) = iTeamKillsValue;
        ++piTeamPointsPtr;
        *(piTeamWinsPtr - 1) = piTeamDataPtr[1];
        piTeamStatsPointer = piTeamDataPtr + 2;
      } while (piTeamPointsPtr != piTeamPointsEnd);

      iNameEndIndex = 9;
      for (iTeamIndex = 0; iTeamIndex < 16; ++iTeamIndex)// PLAYER NAMES: Load 16 players * 9 * 2 character names (288 bytes total)
      {
        iNameIndex = 9 * iTeamIndex;
        do {
          byNameChar = *(uint8 *)piTeamStatsPointer;// Copy name bytes to both default_names and player_names arrays
          pbyNamePtr = (uint8 *)piTeamStatsPointer + 1;
          default_names[0][iNameIndex] = byNameChar;
          //pszTempPointer = (char *)pbyNamePtr;
          ++iNameIndex;
          uint8 byte = *pbyNamePtr;
          piTeamStatsPointer = (int *)(pbyNamePtr + 1);
          player_names[0][iNameIndex - 1] = (char)byte;
        } while (iNameIndex != iNameEndIndex);
        iNameEndIndex += 9;
      }
      iSerialPortValue = *piTeamStatsPointer;   // COMMUNICATION SETTINGS: Load serial port, modem settings, and phone/init strings
      piModemDataPtr = piTeamStatsPointer + 1;
      serial_port = iSerialPortValue;
      iModemPortValue = *piModemDataPtr++;
      modem_port = iModemPortValue;
      iModemCallValue = *piModemDataPtr++;
      modem_call = iModemCallValue;
      iModemBaudValue = *piModemDataPtr++;
      modem_baud = iModemBaudValue;
      iModemBaudValue = *(uint8 *)piModemDataPtr;
      pszPhonePtr = (char *)piModemDataPtr + 1;

      // Load modem phone number and init string (51 chars each, 102 bytes total)
      memcpy(modem_phone, pszPhonePtr, 51);
      pszPhonePtr += 51;
      memcpy(modem_initstring, pszPhonePtr, 51);
      pszPhonePtr += 51;
      //modem_phone[0] = (uint8)iModemBaudValue;
      //for (j = 1; j <= 50; modem_initstring[j + 50] = (char)pszTempPointer)// Load modem phone number and init string (51 chars each, 102 bytes total)
      //{
      //  byPhoneChar1 = *pszPhonePtr;
      //  pszPhoneCharPtr = pszPhonePtr + 1;
      //  modem_phone[j] = byPhoneChar1;
      //  byPhoneChar2 = *pszPhoneCharPtr++;
      //  modem_phone[j + 1] = byPhoneChar2;
      //  byPhoneChar3 = *pszPhoneCharPtr++;
      //  modem_phone[j + 2] = byPhoneChar3;
      //  byPhoneChar4 = *pszPhoneCharPtr++;
      //  modem_phone[j + 3] = byPhoneChar4;
      //  j += 5;
      //  LOBYTE(pszTempPointer) = *pszPhoneCharPtr;
      //  pszPhonePtr = pszPhoneCharPtr + 1;
      //}
      iDriverLoop = 0;
      if (numcars > 0)                        // DRIVER SETUP: Configure AI driver names and human control flags
      {
        pszDriverNamePtr = driver_names[0];
        iDriverIndex = 0;
        pszDefaultNameEnd = default_names[1];
        do {
          pszTempPointer = pszDefaultNameEnd;
          human_control[iDriverIndex] = 0;      // Initialize AI drivers: clear human control, set car design, copy names
          result_design[iDriverIndex] = iDriverLoop / 2;
          pszSourceNamePtr = pszDriverNamePtr;
          pszDefaultNamePtr = default_names[iDriverLoop];
          do {
            ++pszSourceNamePtr;
            byDriverNameChar = *pszDefaultNamePtr++;
            *(pszSourceNamePtr - 1) = byDriverNameChar;
          } while (pszDefaultNamePtr != pszTempPointer);
          pszDriverNamePtr += 9;
          ++iDriverIndex;
          ++iDriverLoop;
          pszDefaultNameEnd += 9;
        } while (iDriverLoop < numcars);
      }
      for (int i = 0; i < numcars; i++) {
        result_control[i] = 0;
      }
      iHumanPlayerLoop = 0;
      if (players > 0)                        // HUMAN PLAYER SETUP: Configure human players and assign them to cars
      {
        iPlayerCarIndex = 0;
        pszPlayerNamePtr = player_names[1];
        do {
          iPlayerCarValue = Players_Cars[iPlayerCarIndex];
          if (iPlayerCarValue < 8)            // Find available car slot for human player or assign to first available AI slot
          {
            iCarSlotIndex = 2 * iPlayerCarValue;
            if (result_control[2 * iPlayerCarValue])
              ++iCarSlotIndex;
          } else {
            iCarSlotIndex = 0;
            iHumanControlLoop = 0;
            if (human_control[0]) {
              do {
                iControlCheck = human_control[++iHumanControlLoop];
                ++iCarSlotIndex;
              } while (iControlCheck);
            }
          }
          pszTempPointer = pszPlayerNamePtr;
          human_control[iCarSlotIndex] = manual_control[iPlayerCarIndex];
          pszTargetNamePtr = driver_names[iCarSlotIndex];
          pszSourcePlayerPtr = player_names[iHumanPlayerLoop];
          do {
            ++pszTargetNamePtr;
            byPlayerNameChar = *pszSourcePlayerPtr++;
            *(pszTargetNamePtr - 1) = byPlayerNameChar;
          } while (pszSourcePlayerPtr != pszPlayerNamePtr);
          pszPlayerNamePtr += 9;
          ++iPlayerCarIndex;
          ++iHumanPlayerLoop;
        } while (iHumanPlayerLoop < players);
      }
      if (numcars > 0)                        // CONTROL SETUP
      {
        for (int i = 0; i < numcars; i++) {
          result_control[i] = human_control[i];
        }
        //iControlArraySize = 4 * numcars;
        //uiControlLoop = 0;
        //do {
        //  uiControlLoop += 4;
        //  result_competing_variable_1[uiControlLoop / 4] = team_wins[uiControlLoop / 4 + 15];// offset into result_control and human_control
        //} while ((int)uiControlLoop < iControlArraySize);
      }
      iCompetitorCount = competitors;           // RACE ORDER SETUP: Build competitor lists and championship standings
      iCompetitorCheck = competitors;
      if (competitors == 2) {
        iCompetitorCount = players;
        if (players < 2)
          iCompetitorCount = competitors;
      }

      // Build competitor order arrays by skipping non-competitors
      int iCompetitorIndex = 0;
      int iCarIndex = 0;
      while (iCompetitorIndex < iCompetitorCount) {
        if (!non_competitors[iCarIndex]) {
          result_order[iCompetitorIndex] = iCarIndex;
          champorder[iCompetitorIndex] = iCarIndex;
          iCompetitorIndex++;
        }
        iCarIndex++;
      }
      //iOrderSearchStart = 0;
      //if (iCompetitorCount > 0) {
      //  iCompetitorCheck = 4 * iCompetitorCount;
      //  uiOrderByteOffset = 0;                  // Find next available competitor slot (skip non-competitors)
      //  do {
      //    for (k = iOrderSearchStart; ; ++k) {
      //      pszTempPointer = (char *)(iOrderSearchStart + 1);
      //      if (!non_competitors[k])
      //        break;
      //      ++iOrderSearchStart;
      //    }
      //    result_order[uiOrderByteOffset / 4] = iOrderSearchStart;
      //    champorder[uiOrderByteOffset / 4] = iOrderSearchStart;
      //    uiOrderByteOffset += 4;
      //    ++iOrderSearchStart;
      //  } while ((int)uiOrderByteOffset < iCompetitorCheck);
      //}
      iHighestPoints = 0;
      racers = iCompetitorCount;
      iSortIndex = 0;
      if (iCompetitorCount > 0)               // CHAMPIONSHIP STANDINGS: Sort players by championship points (bubble sort)
      {

        // Selection sort: Sort racers by championship points (highest to lowest)
        for (int iCurrentIndex = 0; iCurrentIndex < racers - 1; iCurrentIndex++) {
            // Find the racer with the highest points in the remaining unsorted portion
          int iBestIndex = iCurrentIndex;
          int iHighestPoints = championship_points[champorder[iCurrentIndex]];

          for (int iSearchIndex = iCurrentIndex + 1; iSearchIndex < racers; iSearchIndex++) {
            if (championship_points[champorder[iSearchIndex]] > iHighestPoints) {
              iBestIndex = iSearchIndex;
              iHighestPoints = championship_points[champorder[iSearchIndex]];
            }
          }

          // Swap the current position with the highest scoring racer found
          if (iBestIndex != iCurrentIndex) {
            int iTempChamp = champorder[iCurrentIndex];
            champorder[iCurrentIndex] = champorder[iBestIndex];
            champorder[iBestIndex] = iTempChamp;
          }
        }

        //iSortCurrentIndex = 0;
        //do {
        //  iCompetitorCheck = iSortIndex;
        //  iSortTotalRacers = racers;
        //  iSortSearchIndex = iSortIndex + 1;
        //  iHighestPoints = championship_points[champorder[iSortCurrentIndex]];
        //  if (iSortIndex + 1 < racers) {
        //    iSortCompareIndex = iSortSearchIndex;
        //    do {
        //      pszTempPointer = (char *)championship_points[champorder[iSortCompareIndex]];
        //      if ((int)pszTempPointer > iHighestPoints) {
        //        iCompetitorCheck = iSortSearchIndex;
        //        iHighestPoints = championship_points[champorder[iSortCompareIndex]];
        //      }
        //      ++iSortSearchIndex;
        //      ++iSortCompareIndex;
        //    } while (iSortSearchIndex < racers);
        //  }
        //  iSwapTempValue = teamorder[++iSortCurrentIndex + 7];
        //  teamorder[iSortCurrentIndex + 7] = champorder[iCompetitorCheck];
        //  racers = iSortTotalRacers;
        //  iNextSortIndex = iSortIndex + 1;
        //  champorder[iCompetitorCheck] = iSwapTempValue;
        //  iSortIndex = iNextSortIndex;
        //} while (iSortTotalRacers > iNextSortIndex);
      }
      Race = ((uint8)TrackLoad - 1) & 7;        // FINALIZATION: Set race number, enable game timer, configure network
      tick_on = -1;
      if (ROLLERCommsGetType())                  // NETWORK RESTORATION: Reinitialize network connections if saved game used networking
      {
        iHighestPoints = 0;
        ROLLERCommsUnInitSystem();
        network_on = 0;
        net_started = 0;
      }
      if (player_type == 1 && net_type == 1)
        select_comport(3);
      if (player_type == 1 && net_type == 2)
        select_modemstuff(4);
      if (network_on) {
        if (player_type == 1) {
          reset_network(0);
        } else {
          close_network();
          time_to_start = 0;
        }
      } else if (player_type == 1 && net_type != 2) {
        Initialise_Network(0);
      }
    }
    fre((void **)&pFileBuf);                    // Cleanup: Free file buffer and return success/failure status
  }
  return iChecksumOk;
}

//-------------------------------------------------------------------------------------------------
//0005C000
uint8 *lod_champ_char(uint8 *pSrc, int *piValue)
{
  int iValue; // ebx
  uint8 *pNextPos; // eax

  iValue = *pSrc;
  pNextPos = pSrc + 1;
  *piValue = iValue;
  return pNextPos;
}

//-------------------------------------------------------------------------------------------------
//0005C020
// MISLEADING NAME: Actually LOADS a 4-byte integer from buffer (should be named load_champ_int)
uint8 *sav_champ_char(uint8 *pSrc, int *piValue)
{
  int iValue; // ebx
  uint8 *pNextPos; // eax

  iValue = *(int *)pSrc;                     // Read 4-byte integer from buffer at current position
  pNextPos = pSrc + 4;                          // Advance buffer pointer by 4 bytes to next data position
  *piValue = iValue;                            // Store loaded value in output parameter
  return pNextPos;                              // Return advanced buffer pointer for chaining reads
}

//-------------------------------------------------------------------------------------------------
//0005C030
uint8 *sav_champ_int(uint8 *pDest, int iValue)
{
  pDest[0] = (uint8)(iValue);         // Byte 0: bits 0-7
  pDest[1] = (uint8)(iValue >> 8);    // Byte 1: bits 8-15  
  pDest[2] = (uint8)(iValue >> 16);   // Byte 2: bits 16-23
  pDest[3] = (uint8)(iValue >> 24);   // Byte 3: bits 24-31
  return pDest + 4;
}

//-------------------------------------------------------------------------------------------------
//0005C070
void check_saves()
{
  char *pszSaveSlotName; // esi
  int iSlotIndex; // ecx
  int iFileHandle; // edx
  int iFileSize; // edi
  uint8 *pbyFileData; // eax
  uint8 byNetType; // al
  uint8 *pbyReadBuffer; // [esp+0h] [ebp-1Ch] BYREF

  pszSaveSlotName = save_slots[0];              // Get pointer to first save slot filename
  for (iSlotIndex = 0; iSlotIndex != 4; ++iSlotIndex)// Check each of the 4 save slots
  {
    iFileSize = ROLLERfilelength(pszSaveSlotName);
    iFileHandle = ROLLERopen(pszSaveSlotName, O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h
    if (iFileHandle == -1)                    // Check if file open failed
    {
      save_status[iSlotIndex].iSlotUsed = 0;    // Mark slot as empty if file doesn't exist
    } else {
      pbyReadBuffer = (uint8 *)getbuffer(0x800u);// Allocate 2KB buffer for reading save file
      //iFileSize = filelength(iFileHandle);      // Get size of save file
      if (iFileSize == 795)                   // Check if file is exactly 795 bytes (valid save file size)
        read(iFileHandle, pbyReadBuffer, 795);  // Read entire save file into buffer
      close(iFileHandle);                       // Close the save file
      if (iFileSize == 795)                   // Verify file size again before parsing
      {
        pbyFileData = pbyReadBuffer;
        save_status[iSlotIndex].iSlotUsed = -1; // Mark slot as occupied
        save_status[iSlotIndex].iPackedTrack = *pbyFileData;// Extract packed track info from offset 0
        save_status[iSlotIndex].iDifficulty = pbyReadBuffer[3];// Extract difficulty level from offset 3
        save_status[iSlotIndex].iPlayerType = pbyReadBuffer[5];// Extract player type from offset 5
        byNetType = pbyReadBuffer[51];          // Get network type from offset 51
        if (save_status[iSlotIndex].iPlayerType == 1 && byNetType)// Convert network player type based on net_type value
        {
          if (byNetType <= 1u) {
            save_status[iSlotIndex].iPlayerType = 3;// net_type 0 or 1 = Network player (type 3)
          } else if (byNetType == 2) {
            save_status[iSlotIndex].iPlayerType = 4;// net_type 2 = Modem player (type 4)
          }
        }
      } else {
        save_status[iSlotIndex].iSlotUsed = 0;  // Mark slot as empty if file size is invalid
      }
      fre((void **)&pbyReadBuffer);             // Free the read buffer
    }
    pszSaveSlotName += 13;                      // Move to next save slot filename (13 bytes per filename)
  }
}

//-------------------------------------------------------------------------------------------------
//0005C180
void ResultRoundUp()
{
  uint8 *pbyScreenBuffer; // edi
  tBlockHeader *pBackgroundImage; // esi
  unsigned int uiBufferSize; // ecx
  char byBufferRemainder; // al
  unsigned int uiDwordCopyCount; // ecx
  int iCurrentYPos; // edi
  double dBestTimeFloat; // st7
  int iMostKillsDriver; // esi
  int iMaxKillCount; // eax
  int iDriverIndex; // edx
  int iCurrentDriver; // ebx
  int iKillsYPos; // edi
  int iP1HeaderYPos; // edi
  int iPlayer1Id; // ebp
  int iP1DriverId; // esi
  int iP1StatsYPos; // edi
  double dP1BestTime; // st7
  int iP2HeaderYPos; // edi
  int iPlayer2Id; // ebp
  int iP2DriverId; // esi
  int iP2NameYPos; // edi
  int iP2StatsYPos; // edi
  double dP2BestTime; // st7
  int iOriginalScrSize; // [esp+0h] [ebp-28h]
  int iP2Time1; // [esp+4h] [ebp-24h]
  int iP2Time2; // [esp+4h] [ebp-24h]
  int iP1Time1; // [esp+8h] [ebp-20h]
  int iP1Time2; // [esp+8h] [ebp-20h]
  int iLapTime1; // [esp+Ch] [ebp-1Ch]
  int iLapTime2; // [esp+Ch] [ebp-1Ch]

  tick_on = 0;                                  // Initialize race results screen display
  iOriginalScrSize = scr_size;
  SVGA_ON = -1;
  init_screen();
  setpal("resround.pal");
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  front_vga[2] = (tBlockHeader *)load_picture("resround.bm");// Load race results screen resources (background, fonts)
  front_vga[1] = (tBlockHeader *)load_picture("font4.bm");
  front_vga[0] = (tBlockHeader *)load_picture("font5.bm");
  frontend_on = -1;
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pBackgroundImage = front_vga[2];
  if (SVGA_ON)                                // Copy background image to screen buffer (optimized copy)
    uiBufferSize = 256000;
  else
    uiBufferSize = 64000;
  byBufferRemainder = uiBufferSize;
  uiDwordCopyCount = uiBufferSize >> 2;
  memcpy(scrbuf, front_vga[2], 4 * uiDwordCopyCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCopyCount], &pBackgroundImage->iWidth + uiDwordCopyCount, byBufferRemainder & 3);
  front_text(front_vga[1], &language_buffer[2560], font4_ascii, font4_offsets, 320, 8, 0x8Fu, 1u);// Display race winner information
  sprintf(buffer, "%s  %s", driver_names[result_order[0]], CompanyNames[result_design[result_order[0]]]);
  front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, 41, 0x8Fu, 1u);
  iCurrentYPos = 74;
  if (FastestLap >= 0 && BestTime < 5000.0)// Display fastest lap information if valid
  {
    front_text(front_vga[1], &language_buffer[2880], font4_ascii, font4_offsets, 320, 74, 0x8Fu, 1u);
    dBestTimeFloat = BestTime * 100.0;          // Convert fastest lap time to display format (MM:SS:HH)
    //_CHP();
    iLapTime1 = (int)dBestTimeFloat;
    if ((int)dBestTimeFloat > 599999)
      iLapTime1 = 599999;
    fp_buf[8] = 0;
    fp_buf[7] = iLapTime1 % 10 + 48;
    iLapTime2 = iLapTime1 / 10;
    fp_buf[6] = iLapTime2 % 10 + 48;
    fp_buf[5] = 58;
    iLapTime2 /= 10;
    fp_buf[4] = iLapTime2 % 10 + 48;
    iLapTime2 /= 10;
    fp_buf[3] = iLapTime2 % 6 + 48;
    iLapTime2 /= 6;
    fp_buf[1] = iLapTime2 % 10 + 48;
    fp_buf[0] = iLapTime2 / 10 % 10 + 48;
    fp_buf[2] = 58;
    sprintf(buffer, "%s  %s  %s", driver_names[FastestLap], CompanyNames[result_design[FastestLap]], (const char *)fp_buf);
    front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, 107, 0x8Fu, 1u);
    iCurrentYPos = 140;
  }
  iMostKillsDriver = -1;
  iMaxKillCount = 0;                            // Find driver with most kills in the race
  if (racers > 0) {
    iDriverIndex = 0;
    do {
      iCurrentDriver = result_order[iDriverIndex];
      if (iMaxKillCount < result_kills[iCurrentDriver]) {
        iMostKillsDriver = result_order[iDriverIndex];
        iMaxKillCount = result_kills[iCurrentDriver];
      }
      ++iDriverIndex;
    } while (iDriverIndex < racers);
  }
  if (iMostKillsDriver >= 0)                  // Display most kills information if any kills occurred
  {
    front_text(front_vga[1], &language_buffer[4032], font4_ascii, font4_offsets, 320, iCurrentYPos, 0x8Fu, 1u);
    sprintf(buffer, "%s  %s  %s %i", driver_names[iMostKillsDriver], CompanyNames[result_design[iMostKillsDriver]], &language_buffer[3968], result_kills[iMostKillsDriver]);
    iKillsYPos = iCurrentYPos + 33;
    front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, iKillsYPos, 0x8Fu, 1u);
    iCurrentYPos = iKillsYPos + 33;
  }
  iP1HeaderYPos = iCurrentYPos + 4;
  front_text(front_vga[1], &language_buffer[4672], font4_ascii, font4_offsets, 320, iP1HeaderYPos, 0x8Fu, 1u);// Display Player 1 results section
  iPlayer1Id = result_p1;
  iP1DriverId = result_p1;
  sprintf(buffer, "%s  %s", driver_names[result_p1], CompanyNames[result_design[result_p1]]);
  iP1HeaderYPos += 33;
  front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, iP1HeaderYPos, 0x8Fu, 1u);
  iP1StatsYPos = iP1HeaderYPos + 29;
  if (result_lap[iP1DriverId] >= 2)           // Format Player 1's best lap time or show dashes if no valid laps
  {
    dP1BestTime = result_best[iP1DriverId] * 100.0;// Convert P1's best time to display format (MM:SS:HH)
    //_CHP();
    iP1Time1 = (int)dP1BestTime;
    if ((int)dP1BestTime > 599999)
      iP1Time1 = 599999;
    fp_buf[8] = 0;
    fp_buf[7] = iP1Time1 % 10 + 48;
    iP1Time2 = iP1Time1 / 10;
    fp_buf[6] = iP1Time2 % 10 + 48;
    fp_buf[5] = 58;
    iP1Time2 /= 10;
    fp_buf[4] = iP1Time2 % 10 + 48;
    iP1Time2 /= 10;
    fp_buf[3] = iP1Time2 % 6 + 48;
    iP1Time2 /= 6;
    fp_buf[1] = iP1Time2 % 10 + 48;
    fp_buf[2] = 58;
    fp_buf[0] = iP1Time2 / 10 % 10 + 48;
  } else {
    memcpy(fp_buf, "--:--:--", 8);
  }
  if (racers - 1 > result_p1_pos || racers == 1)
    sprintf(buffer, "%s: %s", &language_buffer[1408], &language_buffer[64 * result_p1_pos + 384]);
  else
    sprintf(buffer, "%s: %s", &language_buffer[1408], &language_buffer[1344]);
  sprintf(buffer, "%s  %s %s  %s %i", buffer, &language_buffer[64], (const char *)fp_buf, &language_buffer[3968], result_kills[iPlayer1Id]);
  front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, iP1StatsYPos, 0x8Fu, 1u);
  iP2HeaderYPos = iP1StatsYPos + 37;
  if (player_type == 2) {
    front_text(front_vga[1], &language_buffer[4736], font4_ascii, font4_offsets, 320, iP2HeaderYPos, 0x8Fu, 1u);// Display Player 2 results section (if two-player mode)
    iPlayer2Id = result_p2;
    iP2DriverId = result_p2;
    sprintf(buffer, "%s  %s", driver_names[result_p2], CompanyNames[result_design[result_p2]]);
    iP2NameYPos = iP2HeaderYPos + 33;
    front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, iP2NameYPos, 0x8Fu, 1u);
    iP2StatsYPos = iP2NameYPos + 29;
    if (result_lap[iP2DriverId] >= 2)         // Format Player 2's best lap time or show dashes if no valid laps
    {
      dP2BestTime = result_best[iP2DriverId] * 100.0;// Convert P2's best time to display format (MM:SS:HH)
      //_CHP();
      iP2Time1 = (int)dP2BestTime;
      if ((int)dP2BestTime > 599999)
        iP2Time1 = 599999;
      fp_buf[8] = 0;
      fp_buf[7] = iP2Time1 % 10 + 48;
      iP2Time2 = iP2Time1 / 10;
      fp_buf[5] = 58;
      fp_buf[6] = iP2Time2 % 10 + 48;
      iP2Time2 /= 10;
      fp_buf[4] = iP2Time2 % 10 + 48;
      iP2Time2 /= 10;
      fp_buf[3] = iP2Time2 % 6 + 48;
      fp_buf[2] = 58;
      iP2Time2 /= 6;
      fp_buf[1] = iP2Time2 % 10 + 48;
      fp_buf[0] = iP2Time2 / 10 % 10 + 48;
    } else {
      fp_buf[8] = 0;
      fp_buf[7] = 45;
      fp_buf[6] = 45;
      fp_buf[5] = 58;
      fp_buf[4] = 45;
      fp_buf[3] = 45;
      fp_buf[2] = 58;
      fp_buf[1] = 45;
      fp_buf[0] = 45;
    }
    if (racers - 1 > result_p2_pos || racers == 1)
      sprintf(buffer, "%s: %s", &language_buffer[1408], &language_buffer[64 * result_p2_pos + 384]);
    else
      sprintf(buffer, "%s: %s", &language_buffer[1408], &language_buffer[1344]);
    sprintf(buffer, "%s  %s %s  %s %i", buffer, &language_buffer[64], (const char *)fp_buf, &language_buffer[3968], result_kills[iPlayer2Id]);
    front_text(front_vga[0], buffer, font4_ascii, font5_offsets, 320, iP2StatsYPos, 0x8Fu, 1u);
  }
  copypic(scrbuf, screen);                      // Display results screen and wait for user input
  startmusic(leaderboardsong);
  fade_palette(32);
  ticks = 0;
  while (!fatkbhit() && ticks < 2160)
    UpdateSDL();
  fre((void **)&front_vga[2]);                  // Clean up resources and restore screen settings
  fre((void **)&front_vga[1]);
  fre((void **)front_vga);
  scr_size = iOriginalScrSize;
  holdmusic = -1;
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//0005CB20
void RollCredits()
{
  int iCurrImageIdx; // edi
  int iCreditOrderIdx; // esi
  int iBlockIdx; // ecx
  tBlockHeader *pCurrImage; // ebp
  int64 llBlockHeight; // rax
  int i; // eax

  title_screens();
  ticks = 0;
  frontend_on = -1;
  tick_on = -1;
  front_vga[0] = (tBlockHeader *)load_picture("credit1.bm");
  front_vga[1] = (tBlockHeader *)load_picture("credit2.bm");
  while ( ticks < 108 )
    UpdateSDL();
  fade_palette(0);
  iCurrImageIdx = 0;
  iCreditOrderIdx = 0;
  setpal("credit1.pal");
  do
  {
    iBlockIdx = credit_order[iCreditOrderIdx];
    if ( iBlockIdx < 0 )
    {
      if ( iBlockIdx == -3 )
        --iCurrImageIdx;
      else
        ++iCurrImageIdx;
      iBlockIdx = credit_order[++iCreditOrderIdx];
    }
    memset(scrbuf, 0, 0x3E800u);
    pCurrImage = front_vga[iCurrImageIdx];
    llBlockHeight = pCurrImage[iBlockIdx].iHeight;
    display_block(scrbuf, pCurrImage, iBlockIdx, XMAX / 2 - pCurrImage[iBlockIdx].iWidth / 2, YMAX / 2 - (int)llBlockHeight / 2, -1);
    copypic(scrbuf, screen);
    fade_palette(32);
    ticks = 0;
    do
    {
      while ( fatkbhit() )
      {
        ticks = 74;
        if ( !fatgetch() )
          fatgetch();
        for ( i = iCreditOrderIdx; credit_order[i] != -2; ++i )
          ++iCreditOrderIdx;
        UpdateSDL();
      }
      UpdateSDL();
    }
    while ( ticks < 72 );
    if ( credit_order[iCreditOrderIdx] != -2 )
      ++iCreditOrderIdx;
    if ( credit_order[iCreditOrderIdx] == -2 )
      holdmusic = 0;
    fade_palette(0);
  }
  while ( credit_order[iCreditOrderIdx] != -2 );
  fre((void **)&front_vga[0]);
  fre((void **)&front_vga[1]);
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//0005CCE0
void ChampionshipOver()
{
  signed int iPlayer1Position; // edx
  int iP1SearchIndex; // eax
  int iCurrentChampEntry; // ebx
  signed int iPlayer2Position; // edx
  int iP2SearchIndex; // eax
  int iCurrentP2Entry; // ebx
  uint8 *pbyScreenBuffer; // edi
  char *pszTitleImageData; // esi
  unsigned int uiBufferSize; // ecx
  char byBufferSizeRemainder; // al
  unsigned int uiDwordCopyCount; // ecx
  int id; // [esp+0h] [ebp-20h]
  signed int iBestPos; // [esp+4h] [ebp-1Ch]

  iPlayer1Position = 0;                         // Initialize championship analysis and disable network championship mode
  network_champ_on = 0;
  iP1SearchIndex = 0;
  if (champorder[0] != result_p1)             // Find Player 1's position in championship order
  {
    do {
      iCurrentChampEntry = champorder[++iP1SearchIndex];
      ++iPlayer1Position;
    } while (iCurrentChampEntry != result_p1);
  }
  iBestPos = iPlayer1Position;
  if (player_type == 2)                       // If two-player mode, find Player 2's position and use the better one
  {
    iPlayer2Position = 0;
    iP2SearchIndex = 0;
    if (champorder[0] != result_p2) {
      do {
        iCurrentP2Entry = champorder[++iP2SearchIndex];
        ++iPlayer2Position;
      } while (iCurrentP2Entry != result_p2);
    }
    if (iPlayer2Position < iBestPos)                // Use the better position between both players
      iBestPos = iPlayer2Position;
  }
  if (!iBestPos)                                    // If player won championship (position 0), show victory sequence
  {
    championship_winner();
    champion_race();
  }
  tick_on = 0;                                  // Initialize screen for championship results display
  id = scr_size;
  SVGA_ON = -1;
  init_screen();
  setpal("resround.pal");
  winx = 0;                                     // Set window to full screen and start victory music
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  startmusic(winsong);
  holdmusic = -1;
  title_vga = load_picture("resround.bm");      // Load championship results screen resources
  font_vga = load_picture("font4.bm");
  front_vga[0] = (tBlockHeader *)load_picture("font5.bm");
  frontend_on = -1;
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pszTitleImageData = (char *)title_vga;
  if (SVGA_ON)                                // Copy title background to screen buffer (optimized copy)
    uiBufferSize = 256000;
  else
    uiBufferSize = 64000;
  byBufferSizeRemainder = uiBufferSize;
  uiDwordCopyCount = uiBufferSize >> 2;
  memcpy(scrbuf, title_vga, 4 * uiDwordCopyCount);
  memcpy(&pbyScreenBuffer[4 * uiDwordCopyCount], &pszTitleImageData[4 * uiDwordCopyCount], byBufferSizeRemainder & 3);
  if (iBestPos)                                     // Display different messages based on championship position
  {                                             // Second place - show runner-up messages
    if (iBestPos == 1) {
      front_text((tBlockHeader *)font_vga, &language_buffer[4160], font4_ascii, font4_offsets, 320, 64, 0x8Fu, 1u);
      front_text(front_vga[0], &language_buffer[4288], font4_ascii, font5_offsets, 320, 100, 0x8Fu, 1u);
      front_text((tBlockHeader *)font_vga, &language_buffer[4352], font4_ascii, font4_offsets, 320, 140, 0x8Fu, 1u);
      front_text((tBlockHeader *)font_vga, driver_names[champorder[0]], font4_ascii, font5_offsets, 320, 180, 0x8Fu, 1u);
      if (Race == 8) {
        TrackLoad = (((uint8)TrackLoad - 1) & 7) + 1;
        Race = 0;
      }
    } else {
      front_text((tBlockHeader *)font_vga, &language_buffer[4224], font4_ascii, font4_offsets, 320, 64, 0x8Fu, 1u);// Third place or lower - show completion message
      front_text((tBlockHeader *)font_vga, &language_buffer[4352], font4_ascii, font4_offsets, 320, 140, 0x8Fu, 1u);
      front_text((tBlockHeader *)font_vga, driver_names[champorder[0]], font4_ascii, font4_offsets, 320, 180, 0x8Fu, 1u);
      if (Race == 8) {
        TrackLoad = (((uint8)TrackLoad - 1) & 7) + 1;
        Race = 0;
      }
    }
    goto LABEL_36;
  }
  front_text((tBlockHeader *)font_vga, &language_buffer[4096], font4_ascii, font4_offsets, 320, 64, 0x8Fu, 1u);// Championship winner - display congratulations
  if (Race == 8)                              // Handle completion rewards and progression
  {                                             // Unlock texture quality improvements for winning championship
    if (level < 4)
      textures_off |= TEX_OFF_PREMIER_CUP_AVAILABLE;
    if (level < 2) {
      textures_off |= TEX_OFF_CAR_SET_AVAILABLE;
      //uiTextureSettings = textures_off;
      //BYTE1(uiTextureSettings) = BYTE1(textures_off) | 0x80;
      //textures_off = uiTextureSettings;
    }
  }
  if (TrackLoad < 17 && level < 4)            // Determine next level progression or reset
  {                                             // Continue in same track group at higher difficulties
    if (level > 0) {
      front_text((tBlockHeader *)font_vga, &language_buffer[4480], font4_ascii, font4_offsets, 320, 100, 0x8Fu, 1u);
      goto LABEL_30;
    }
  } else {
    TrackLoad = 17;                             // Reset to first track group and decrease difficulty if at higher levels
    if (level > 0) {
      if (Race == 8)
        --level;
      front_text((tBlockHeader *)font_vga, &language_buffer[4416], font4_ascii, font4_offsets, 320, 100, 0x8Fu, 1u);
      goto LABEL_30;
    }
  }
  front_text((tBlockHeader *)font_vga, &language_buffer[4544], font4_ascii, font4_offsets, 320, 100, 0x8Fu, 1u);// Ultimate completion - show mastery message
LABEL_30:
  if (Race == 8)                              // Reset race counter after completing championship
    Race = 0;
LABEL_36:
  copypic(scrbuf, screen);                      // Display results screen and wait for user input
  fade_palette(32);
  ticks = 0;
  while (!fatkbhit() && ticks < 2160)
    UpdateSDL();
  fre(&title_vga);                              // Clean up resources and show end sequence
  fre(&font_vga);
  fre((void **)front_vga);
  scr_size = id;
  fade_palette(0);
  EndChampSequence();                           // Run championship end sequence and credits
  RollCredits();
  if (TrackLoad >= 17)                        // Reset track selection if at maximum
    TrackLoad = 1;
}

//-------------------------------------------------------------------------------------------------
//0005D180
void EndChampSequence()
{
  int iImageIndex; // esi
  int iRandomValue; // eax
  int iRandomYPosition; // eax

  ticks = 0;                                    // Initialize championship end sequence timing and modes
  frontend_on = -1;
  tick_on = -1;
  iImageIndex = 0;                              // Start with first championship image (index 0)
  front_vga[0] = (tBlockHeader *)load_picture(round_pics[0]);// Load the first championship sequence image
  do {
    setpal(round_pals[iImageIndex]);            // Set palette for current championship image
    memset(scrbuf, 0, 0x3E800u);                // Clear screen buffer (320x200 = 0x3E800 bytes)
    iRandomValue = rand();                      // Generate random position for image display
    iRandomYPosition = 300 * GetHighOrderRand(5, iRandomValue);
    //iRandomYPosition = 300 * ((5 * iRandomValue - (__CFSHL__((5 * iRandomValue) >> 31, 15) + ((5 * iRandomValue) >> 31 << 15))) >> 15);// Calculate random Y position using complex modulo arithmetic
    display_block(scrbuf, front_vga[0], 0, iRandomYPosition / 4, 0, -1);
    //display_block(scrbuf, front_vga[0], 0, (iRandomYPosition - (__CFSHL__(iRandomYPosition >> 31, 2) + 4 * (iRandomYPosition >> 31))) >> 2, 0, -1);// Display championship image at random Y position, centered horizontally
    copypic(scrbuf, screen);                    // Copy buffer to screen and fade in
    fade_palette(32);
    ticks = 0;                                  // Reset timing for image display duration
    if (++iImageIndex < 8)                    // Advance to next image if not at end of sequence
    {
      fre((void **)front_vga);                  // Free current image and load next championship image
      front_vga[0] = (tBlockHeader *)load_picture(round_pics[iImageIndex]);
    }
    do {                                           // Check for user input to skip sequence
      if (fatkbhit()) {
        iImageIndex = 8;                        // Skip to end of sequence and set timeout on key press
        if (!fatgetch())
          fatgetch();
        ticks = 144;                            // Set timeout to 144 ticks for image display
      }
      UpdateSDL();
    } while (ticks < 144);                      // Wait for display timeout (144 ticks) or user input
    fade_palette(0);                            // Fade out current image before next iteration
  } while (iImageIndex < 8);                    // Continue sequence until all 8 championship images shown
  fre((void **)front_vga);                      // Clean up resources and reset fade state
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//0005D2B0
void network_fucked()
{                                               // Check if network is in error state and close if needed
  int iOriginalScreenSize; // ebp
  uint8 *pbyScreenBuffer; // edi
  char *pszTitleImageData; // esi
  unsigned int uiBufferSize; // ecx
  char byBufferSizeRemainder; // al
  unsigned int uiDwordCopyCount; // ecx
  int iInputReceived; // ebx
  int iKeyCode; // eax

  if (network_buggered != 666)
    close_network();
  tick_on = 0;                                  // Disable game ticking
  iOriginalScreenSize = scr_size;               // Save original screen size for restoration later
  SVGA_ON = -1;                                 // Enable SVGA mode for error display
  init_screen();                                // Initialize screen for error display
  setpal("resround.pal");
  winx = 0;                                     // Set window to full screen dimensions
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  title_vga = load_picture("resround.bm");      // Load background image and fonts for error screen
  font_vga = load_picture("font4.bm");
  front_vga[0] = (tBlockHeader *)load_picture("font5.bm");
  frontend_on = -1;                             // Enable frontend mode and re-enable ticking
  tick_on = -1;
  pbyScreenBuffer = scrbuf;
  pszTitleImageData = (char *)title_vga;
  if (SVGA_ON)                                // Determine buffer size based on video mode (SVGA=256K, VGA=64K)
    uiBufferSize = 256000;
  else
    uiBufferSize = 64000;
  byBufferSizeRemainder = uiBufferSize;
  uiDwordCopyCount = uiBufferSize >> 2;
  memcpy(scrbuf, title_vga, 4 * uiDwordCopyCount);// Copy title image to screen buffer (optimized 32-bit copy + remainder)
  memcpy(&pbyScreenBuffer[4 * uiDwordCopyCount], &pszTitleImageData[4 * uiDwordCopyCount], byBufferSizeRemainder & 3);
  if (network_buggered == 666)                // Check if this is a data loss error (network_buggered == 666)
  {
    front_text((tBlockHeader *)font_vga, "ERROR", font4_ascii, font4_offsets, 320, 100, 0x8Fu, 1u);// Display data loss error messages and prompt for restart
    front_text((tBlockHeader *)font_vga, "DATA LOSS", font4_ascii, font4_offsets, 320, 140, 0x8Fu, 1u);
    front_text((tBlockHeader *)font_vga, "PLAY AGAIN?", font4_ascii, font4_offsets, 320, 200, 0x8Fu, 1u);
    front_text((tBlockHeader *)font_vga, "YES OR NO", font4_ascii, font4_offsets, 320, 260, 0x8Fu, 1u);
  } else {
    front_text((tBlockHeader *)font_vga, &language_buffer[6208], font4_ascii, font4_offsets, 320, 192, 0x8Fu, 1u);// Display general network error message from language buffer
  }
  copypic(scrbuf, screen);                      // Display the error screen and start background music
  startmusic(leaderboardsong);
  fade_palette(32);                             // Fade in the error screen
  ticks = 0;
  if (network_buggered == 666)                // Handle user input based on error type
  {
    iInputReceived = 0;                         // Data loss error: wait for Y/N input to restart network
    do {
      while (fatkbhit()) {
        UpdateSDL();
        iKeyCode = fatgetch();
        if (iKeyCode) {                                       // Handle Y/y key: restart network
          if (iKeyCode == 0x79 || iKeyCode == 0x59) {
            iInputReceived = -1;
            restart_net = -1;
          }
          if (iKeyCode == 0x6E || iKeyCode == 0x4E)// Handle N/n key: don't restart network
          {
            iInputReceived = -1;
            restart_net = 0;
          }
        } else {
          fatgetch();
        }
      }
      UpdateSDL();
    } while (!iInputReceived);
  } else {                                             // General network error: wait for any key or timeout (2160 ticks)
    while (!fatkbhit() && ticks < 2160)
      UpdateSDL();
  }
  fre(&title_vga);                              // Clean up loaded images and restore original screen settings
  fre(&font_vga);
  fre((void **)front_vga);
  scr_size = iOriginalScreenSize;
  holdmusic = -1;                               // Hold music and fade out screen
  fade_palette(0);
  if (network_buggered == 666 && !restart_net)// If data loss occurred and user chose not to restart, close network
  {
    broadcast_mode = -666;
    while (broadcast_mode)
      UpdateSDL();
    close_network();
  }
}

//-------------------------------------------------------------------------------------------------
//0005D560
void no_cd()
{
  int iScrSize; // ebp
  uint8 *pScrBuf; // edi
  char *pTitleVga; // esi
  unsigned int uiScreenTotalBytes; // ecx
  char uiRemainderBytes; // al
  unsigned int uiAlignedCopySize; // ecx

  // disable timing to prepare for error screen display
  tick_on = 0;
  iScrSize = scr_size;
  SVGA_ON = -1;

  // show init screen
  init_screen();
  setpal("resround.pal");

  // set viewport
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // load resources
  title_vga = load_picture("resround.bm");
  font_vga = load_picture("font4.bm");
  front_vga[0] = (tBlockHeader *)load_picture("font5.bm");

  // enable frontend and timing
  frontend_on = -1;
  tick_on = -1;

  pScrBuf = scrbuf;
  pTitleVga = (char *)title_vga;

  // determine screen buffer size based on video mode
  if (SVGA_ON)
    uiScreenTotalBytes = 256000;
  else
    uiScreenTotalBytes = 64000;
  uiRemainderBytes = uiScreenTotalBytes;
  uiAlignedCopySize = uiScreenTotalBytes >> 2;

  // Copy 4-bytes at a time then remainder
  memcpy(scrbuf, title_vga, 4 * uiAlignedCopySize);
  memcpy(&pScrBuf[4 * uiAlignedCopySize], &pTitleVga[4 * uiAlignedCopySize], uiRemainderBytes & 3);

  // Display error text
  front_text(font_vga, &language_buffer[6336], font4_ascii, font4_offsets, 320, 192, 0x8Fu, 1u);

  // Copy to scrbuf and fade in
  copypic(scrbuf, screen);
  fade_palette(32);

  // Wait for user input
  ticks = 0;
  while (!fatkbhit() && ticks < 2160)
    UpdateSDL(); //added by ROLLER

  // Cleanup
  fre((void**)&title_vga);
  fre((void**)&font_vga);
  fre((void**)&front_vga[0]);

  // Fade out and exit
  scr_size = iScrSize;
  holdmusic = 0;
  fade_palette(0);
  //__asm { int     10h; -VIDEO - SET VIDEO MODE }
  doexit();
}

//-------------------------------------------------------------------------------------------------
//0005D6D0
int name_cmp(char *szName1, char *szName2)
{
  int iResult; // edx
  int iCharIdx; // eax
  char byChar; // cl

  iResult = -1;
  iCharIdx = 0;
  do {
    if (iCharIdx >= 9)
      break;
    byChar = szName2[iCharIdx];
    if (byChar) {
      if (byChar != szName1[iCharIdx])
        iResult = 0;
      ++iCharIdx;
    } else {
      iCharIdx = 9;
    }
  } while (iResult);
  return iResult;
}

//-------------------------------------------------------------------------------------------------
//0005D710
void name_copy(char *szDest, char *szSrc)
{
  char *pSrcPos; // eax
  char *pSrcEndPos; // ebx
  char byChar; // cl

  pSrcPos = szSrc;
  pSrcEndPos = szSrc + 9;
  do {
    ++szDest;
    byChar = *pSrcPos++;
    *(szDest - 1) = byChar;
  } while (pSrcPos != pSrcEndPos);
}

//-------------------------------------------------------------------------------------------------
//0005D730
void loadtracksample(int track_number)
{
  if (track_number <= 0) track_number = 1;
  snprintf(buffer, sizeof(buffer), "TRACK%02d.RAW", track_number);
  loadfrontendsample(buffer);
}

//-------------------------------------------------------------------------------------------------
//0005D790
void front_letter(tBlockHeader *pFont, uint8 byCharIdx, int *iX, int *iY, const char *szStr, uint8 byColorReplace)
{
  // Get character from string
  uint8 c = szStr[byCharIdx];

  // Handle special character 0xFF (tab)
  if (c == 0xFF) {
    *iX += 8;
    return;
  }

  // Get character data from font
  tBlockHeader *pBlock = &pFont[c];
  uint8 *pPixelData = (uint8 *)pFont + pBlock->iDataOffset;

  // Calculate screen position
  uint8 *pDest = &scrbuf[*iX + *iY * 640];

  // Draw character
  for (int y = 0; y < pBlock->iHeight; y++) {
    for (int x = 0; x < pBlock->iWidth; x++) {
      uint8 byPixel = *pPixelData++;

      if (byPixel != 0) {  // Skip transparent pixels
        if (byPixel == 0x8F) {  // Special color replacement
          *pDest = byColorReplace;
        } else {
          *pDest = byPixel;
        }
      }
      pDest++;
    }
    // Move to next screen row
    pDest += 640 - pBlock->iWidth;
  }

  // Update x position (character width + 1 pixel spacing)
  *iX = *iX + pBlock->iWidth + 1;
}

//-------------------------------------------------------------------------------------------------
//0005D840
void scale_letter(tBlockHeader *pFont, uint8 byChar, int *iCursorX, int *iCursorY, char *mappingTable, char byColorReplace, int iScaleSize)
{
  int byCharIndex; // edx
  int iCharWidth; // ebp
  uint8 *pFontData; // edx
  uint8 *pScreenPos; // edi
  int iScaleFactor; // eax
  int iPixelX; // ebx
  uint8 byPixelColor; // cl
  uint8 *pRowStart; // [esp+0h] [ebp-24h]
  int iCharHeight; // [esp+Ch] [ebp-18h]
  uint8 *pFontRowStart; // [esp+10h] [ebp-14h]
  int iPixelY; // [esp+14h] [ebp-10h]

  byCharIndex = (uint8)mappingTable[byChar];// Get character index from ASCII lookup table
  if (byCharIndex == 255)                     // Character not found (255) - advance cursor by scaled space width
  {
    *iCursorX += (8 * iScaleSize) >> 6;
  } else {
    iCharWidth = pFont[byCharIndex].iWidth;     // Load character dimensions and font data pointer from font header
    iCharHeight = pFont[byCharIndex].iHeight;
    pFontData = (uint8 *)pFont + pFont[byCharIndex].iDataOffset;
    iPixelY = 0;
    for (pScreenPos = &scrbuf[640 * *iCursorY + *iCursorX]; iPixelY < iCharHeight; ++iPixelY)// MAIN RENDER LOOP: Process each row of the character
    {
      pRowStart = pScreenPos;
      pFontRowStart = pFontData;
      iScaleFactor = iScaleSize;
      iPixelX = 0;
      while (iPixelX < iCharWidth)            // PIXEL LOOP: Scale and render each pixel in the current row
      {
        byPixelColor = *pFontData;
        if (*pFontData) {                                       // Special color handling: 0x8F (-113) becomes the specified color parameter
          if (byPixelColor == 0x8F)
            byPixelColor = byColorReplace;
          *pScreenPos = byPixelColor;
        }
        iScaleFactor -= 64;
        ++pScreenPos;
        for (; iScaleFactor <= 0; ++iPixelX)  // Scaling logic: advance source pixel when scale factor reaches threshold
        {
          ++pFontData;
          iScaleFactor += iScaleSize;
        }
      }
      pScreenPos = pRowStart + 640;             // Move to next screen row (640 pixels per row) and next font data row
      pFontData = &pFontRowStart[iCharWidth];
    }
    *iCursorX += (iScaleSize * (iCharWidth + 1)) >> 6;// Advance cursor position by scaled character width plus 1 pixel spacing
  }
}

//-------------------------------------------------------------------------------------------------
//0005D930
void front_text(
    tBlockHeader *pFont,
    const char *szText,
    const uint8 *mappingTable,
    int *pCharVOffsets,
    int iX,
    int iY,
    uint8 byColorReplace,
    int iAlignment)
{
  int iCurrX = iX;
  int iCurrY = iY;
  const char *p = szText;

  // Precompute text width for alignment
  if (iAlignment == 1 || iAlignment == 2) {
    int iTotalWidth = 0;
    const char *q = p;

    while (*q) {
      uint8 c = *q++;
      uint8 byMapped = mappingTable[c];

      if (byMapped == 0xFF) {
        iTotalWidth += 8;  // Tab-like character
      } else {
        // Get character width from font data
        tBlockHeader *pBlock = &pFont[byMapped];
        iTotalWidth += pBlock->iWidth + 1;  // Width + 1px spacing
      }
    }

    // Apply alignment adjustment
    if (iAlignment == 2) {      // Right alignment
      iCurrX -= iTotalWidth;
    } else if (iAlignment == 1) { // Center alignment
      iCurrX -= iTotalWidth / 2;
    }
  }

  // Render each character
  int iContinue = 1;
  while (iContinue) {
    uint8 c = *p;

    if (c == '\0') {  // End of string
      iContinue = -1;  // Set exit flag
    } else if (c == '\n') {  // Newline character
        // Skip to next character (no rendering)
    } else {
      uint8 byMapped = mappingTable[c];

      if (byMapped == 0xFF) {  // Special character (tab-like)
        iCurrX += 8;
      } else {
        // Get vertical offset for this character
        int iVOffset = pCharVOffsets[byMapped];

        // Calculate character-specific Y position
        int iCharY = iCurrY + iVOffset;

        // Draw the character
        front_letter(
            pFont,          // Font data
            c,              // Character to draw
            &iCurrX,        // Current X position (updated)
            &iCharY,        // Character-specific Y position
            mappingTable,   // Character mapping
            byColorReplace  // Color replacement
        );
      }
    }

    p++;  // Move to next character

    // Exit loop if end flag is set
    if (iContinue == -1) {
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0005DA40
void scale_text(tBlockHeader *pFont,
                char *szText,
                const char *mappingTable,
                int *pCharVOffsets,
                int iX,
                int iY,
                char byColorReplace,
                unsigned int uiAlignment,
                int iClipLeft,
                int iClipRight)
{
  int iTextWidth; // esi
  int byCurrentChar; // eax
  int byCharIndex; // eax
  int iTextEndX; // eax
  int iAvailableWidth; // edx
  char *pbyTextPtr; // eax
  int iScaledWidth; // edx
  int iCharIndexTemp; // esi
  uint8 *pRenderPtr; // esi
  int iRightBound; // [esp+0h] [ebp-34h]
  char *szTextStart; // [esp+4h] [ebp-30h]
  int iSavedY; // [esp+8h] [ebp-2Ch]
  int iCursorY; // [esp+Ch] [ebp-28h] BYREF
  int iCursorX; // [esp+10h] [ebp-24h] BYREF
  int iLeftBound; // [esp+14h] [ebp-20h]
  int *pCharVOffsets_1; // [esp+18h] [ebp-1Ch]
  int iScaledSpaceWidth; // [esp+1Ch] [ebp-18h]
  int iFinishedFlag; // [esp+20h] [ebp-14h]
  int iScaleSize; // [esp+24h] [ebp-10h]

  szTextStart = szText;                         // Store parameters and initialize position tracking
  pCharVOffsets_1 = pCharVOffsets;
  iCursorX = iX;
  iTextWidth = 0;
  iCursorY = iY;
  while (*szText)                             // PASS 1: Calculate total text width in pixels for alignment
  {
    byCurrentChar = (uint8)*szText++;
    byCharIndex = (uint8)mappingTable[byCurrentChar];
    if (byCharIndex == 255)
      iTextWidth += 8;
    else
      iTextWidth += pFont[byCharIndex].iWidth + 1;
  }
  if (!uiAlignment)                           // Apply horizontal alignment: 0=left, 1=center, 2=right
  {
    iLeftBound = iCursorX;
    iTextEndX = iTextWidth + iCursorX;
  LABEL_13:
    iRightBound = iTextEndX;
    goto LABEL_14;
  }
  if (uiAlignment > 1) {
    if (uiAlignment != 2)
      goto LABEL_14;
    iLeftBound = iCursorX - iTextWidth;
    iTextEndX = iCursorX;
    goto LABEL_13;
  }
  iLeftBound = iCursorX - iTextWidth / 2;
  iRightBound = iTextWidth / 2 + iCursorX;
LABEL_14:
  if (iClipLeft > iLeftBound || iRightBound > iClipRight)// Check if text extends beyond clipping bounds and calculate scaling factor
  {
    if (iClipLeft <= iLeftBound) {
      iAvailableWidth = iClipRight - iLeftBound;
    } else if (iRightBound <= iClipRight) {
      iAvailableWidth = iRightBound - iClipLeft;
    } else {
      iAvailableWidth = iClipRight - iClipLeft;
    }
    iScaleSize = (iAvailableWidth << 6) / iTextWidth;
  } else {
    iScaleSize = 64;
  }
  pbyTextPtr = szTextStart;                     // PASS 2: Calculate scaled text width for final positioning
  iScaledWidth = 0;
  while (*pbyTextPtr) {
    iCharIndexTemp = (uint8)mappingTable[(uint8)*pbyTextPtr++];
    if (iCharIndexTemp == 255)
      iScaledWidth += (8 * iScaleSize) >> 6;
    else
      iScaledWidth += (iScaleSize * (pFont[iCharIndexTemp].iWidth + 1)) >> 6;
  }
  if (uiAlignment)                            // Adjust starting X position based on alignment and scaled width
  {
    if (uiAlignment <= 1) {
      iCursorX -= iScaledWidth / 2;
    } else if (uiAlignment == 2) {
      iCursorX -= iScaledWidth;
    }
  }
  iFinishedFlag = 0;                            // PASS 3: Render each character with scaling and positioning
  pRenderPtr = (uint8 *)szTextStart;
  iScaledSpaceWidth = (8 * iScaleSize) >> 6;
  do {
    if (*pRenderPtr) {                                           // Handle newline character (ASCII 10) - not implemented
      if (*pRenderPtr != 10) {                                         // Character not in font (index 255) - advance by scaled space width
        if (mappingTable[*pRenderPtr] == -1) {
          iCursorX += iScaledSpaceWidth;
        } else {
          iSavedY = iCursorY;                   // Render valid character: adjust Y position, call scale_letter, restore Y position
          iCursorY += pCharVOffsets_1[(uint8)mappingTable[*pRenderPtr]];
          scale_letter(pFont, *pRenderPtr, &iCursorX, &iCursorY, (char *)mappingTable, byColorReplace, iScaleSize);
          iCursorY = iSavedY;
        }
      }
    } else {
      iFinishedFlag = -1;                       // End of string (null terminator) - set finished flag
    }
    ++pRenderPtr;
  } while (!iFinishedFlag);
}

//-------------------------------------------------------------------------------------------------
//0005DC50
void clear_screen(void *pDest, int iVal)
{
  memset(pDest, iVal, 256000u);
}

//-------------------------------------------------------------------------------------------------
//0005DC60
void display_picture(void *pDest, const void *pSrc)
{
  unsigned int uiSize; // ecx

  if (SVGA_ON)
    uiSize = 256000;
  else
    uiSize = 64000;
  memcpy(pDest, pSrc, uiSize);
}

//-------------------------------------------------------------------------------------------------
//0005DC90
void display_block(uint8 *pDest, tBlockHeader *pSrc, int iBlockIdx, int iX, int iY, int iTransparentColor)
{
  int iBlockWidth; // ebp
  int iPixelDataOffset; // edx
  int iBlockHeight; // ebx
  uint8 *pDestItr; // eax
  uint8 *pPixelData; // esi
  int iX2; // edx
  int j; // ebx
  uint8 byColor; // cl
  int iBlockHeight2; // [esp+8h] [ebp-18h]
  int i; // [esp+Ch] [ebp-14h]

  iBlockWidth = pSrc[iBlockIdx].iWidth;
  iPixelDataOffset = iBlockIdx;
  iBlockHeight = pSrc[iBlockIdx].iHeight;
  pDestItr = &pDest[640 * iY + iX];
  pPixelData = (uint8 *)pSrc + pSrc[iPixelDataOffset].iDataOffset;
  iBlockHeight2 = iBlockHeight;

  // Process each row
  for (i = 0; i < iBlockHeight2; ++i) {
    iX2 = iX;
    // Process each column
    for (j = 0; j < iBlockWidth; ++pDestItr) {
      byColor = *pPixelData++;

      // Skip transparent color if specified and draw pixel if within horizontal screen bounds
      if ((unsigned int)iX2 < 0x280 && (iTransparentColor < 0 || byColor != iTransparentColor))
        *pDestItr = byColor;
      ++iX2;
      ++j;
    }
    pDestItr += 640 - iBlockWidth;
  }
}

//-------------------------------------------------------------------------------------------------
//0005DD40
uint8 *load_picture(const char *szFile)
{
  int iFileHandle; // ebx
  uint32 uiFileLength; // eax
  uint8 *pBuf; // ebx

  iFileHandle = ROLLERopen(szFile, O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h
  if (iFileHandle == -1) {
    ErrorBoxExit("Unable to open texture map data file %s", szFile);
    //printf("Unable to open texture map data file %s\n\n", szFile);
    //doexit();
    return NULL;
  }
  close(iFileHandle);
  uiFileLength = getcompactedfilelength(szFile);
  pBuf = (uint8 *)getbuffer(uiFileLength);
  loadcompactedfile(szFile, pBuf);
  return pBuf;
}

//-------------------------------------------------------------------------------------------------
//0005DDA0
void AllocateCars()
{
  int iCarIdx; // esi
  char *pszNextDefaultNamePtr; // ebp
  int iDriverIdx; // edi
  int iAssignedCarType; // eax
  char *pszDestName; // edx
  char *pszSrcName; // eax
  char byNameChar; // cl
  int iTotalCars; // ecx
  int iPlayerIdx; // edi
  int iPlayersCarIdx; // ebp
  int iSelectedCarType; // ebx
  int iAvailableSlot; // eax
  int iSlotSearchIdx; // edx
  int iHumanCtrlCheck; // ecx
  int iFallbackSearchIdx; // edx
  int iFallbackCtrlCheck; // esi
  char *szDest; // ebx
  char *szPlayerNameItr; // edx
  char c; // cl
  int iConsolePlayerId; // edx
  int iPlayerInvulFlag; // ebx
  int iCarLoopBytes; // edx
  int iLoopCounter; // eax
  int iCompetitorMode; // eax
  int iCarsBytesTotal; // edx
  unsigned int uiRandomCarIdx; // eax
  int iRandResult; // eax
  int iNormalizedCarSlot; // eax
  //int iActiveCompetitors; // edi
  //int v29; // eax
  //int v30; // eax
  int iEvilCarCounter; // edi
  char *pszEvilNameDest; // ebp
  int iEvilCarIdx; // esi
  char *pszEvilNameSrc; // eax
  char *pszEvilNameCharDest; // edx
  char byEvilNameChar; // cl
  int iFinalCaridx; // edx
  int iTotalCarsFinal; // esi
  int iCarSlotIdx; // eax
  char *pszNextPlayerName; // [esp+0h] [ebp-20h]
  char *pszDriverName; // [esp+4h] [ebp-1Ch]

  my_number = -1;                               // Initialize my_number to indicate no player assigned yet
  numcars = 16;                                 // Set total cars to maximum (16)
  ViewType[1] = -1;                             // Reset second player view
  check_cars();                                 // Validate and initialize car data
  iCarIdx = 0;
  if (numcars > 0) {
    pszNextDefaultNamePtr = default_names[1];
    pszDriverName = driver_names[0];
    iDriverIdx = 0;
    do {                                           // Clone mode: all cars use player 1's car type
      if ((cheat_mode & 0x4000) != 0)         // cheat_mode & CHEAT_MODE_CLONES
        iAssignedCarType = Players_Cars[0];
      else
        iAssignedCarType = iCarIdx / 2;         // Normal mode: car type based on position (pairs)
      Drivers_Car[iDriverIdx] = iAssignedCarType;
      pszDestName = pszDriverName;
      pszSrcName = default_names[iCarIdx];
      do {
        ++pszDestName;                          // Copy default driver name
        byNameChar = *pszSrcName++;
        *(pszDestName - 1) = byNameChar;
      } while (pszSrcName != pszNextDefaultNamePtr);
      iTotalCars = numcars;
      pszNextDefaultNamePtr += 9;
      human_control[iDriverIdx] = 0;
      invulnerable[iDriverIdx] = 0;
      ++iDriverIdx;
      ++iCarIdx;
      //team_wins[iDriverIdx + 15] = 0;
      //RecordNames_variable_1[iDriverIdx] = 0;   // offset into invulnerable
      pszDriverName += 9;
    } while (iCarIdx < iTotalCars);
  }
  iPlayerIdx = 0;
  if (players > 0) {
    iPlayersCarIdx = 0;
    pszNextPlayerName = player_names[1];
    do {
      iSelectedCarType = Players_Cars[iPlayersCarIdx];// Get player's selected car type
      if (iPlayerIdx || (iAvailableSlot = my_number, my_number < 0)) {                                         // For cars >= 8 or clone mode, find any available slot
        if (iSelectedCarType >= 8 || (cheat_mode & 0x4000) != 0)// CHEAT_MODE_CLONES
        {
          iAvailableSlot = 0;
          iSlotSearchIdx = 0;
          if (human_control[0]) {
            do {
              iHumanCtrlCheck = human_control[++iSlotSearchIdx];
              ++iAvailableSlot;
            } while (iHumanCtrlCheck);
          }
        } else {
          iAvailableSlot = 2 * iSelectedCarType;// For standard cars (0-7), try preferred slot based on car type
          if (human_control[2 * iSelectedCarType])
            ++iAvailableSlot;
        }
        if (iAvailableSlot >= numcars) {
          iAvailableSlot = 0;
          iFallbackSearchIdx = 0;
          if (human_control[0]) {
            do {
              iFallbackCtrlCheck = human_control[++iFallbackSearchIdx];
              ++iAvailableSlot;
            } while (iFallbackCtrlCheck);
          }
        }
      }
      Drivers_Car[iAvailableSlot] = iSelectedCarType;
      szDest = driver_names[iAvailableSlot];
      szPlayerNameItr = player_names[iPlayerIdx];
      do {
        ++szDest;                               // Copy player name to driver slot
        c = *szPlayerNameItr++;
        *(szDest - 1) = c;
      } while (szPlayerNameItr != pszNextPlayerName);
      iConsolePlayerId = wConsoleNode;
      human_control[iAvailableSlot] = manual_control[iPlayersCarIdx];// Set up control type and player mappings
      iPlayerInvulFlag = player_invul[iPlayersCarIdx];
      car_to_player[iAvailableSlot] = iPlayerIdx;
      invulnerable[iAvailableSlot] = iPlayerInvulFlag;
      if (iPlayerIdx == iConsolePlayerId)     // Configure player 1 camera if this is the console player
      {
        player1_car = iAvailableSlot;
        ViewType[0] = iAvailableSlot;
      }
      if (player_type == 2 && iPlayerIdx == player2_car)// Configure player 2 camera in split-screen mode
      {
        player2_car = iAvailableSlot;
        ViewType[1] = iAvailableSlot;
      }
      ++iPlayersCarIdx;
      ++iPlayerIdx;
      pszNextPlayerName += 9;
    } while (iPlayerIdx < players);
  }
  if (game_type != 1 || !Race)                // Skip competitor setup for racing mode
  {
    if (numcars > 0) {
      iCarLoopBytes = 4 * numcars;
      iLoopCounter = 0;
      do {
        non_competitors[iLoopCounter / 4u] = 0;
        iLoopCounter += 4;                      // Initialize all cars as competitors by default
        //TrackArrow_variable_1[iLoopCounter / 4u] = 0;// offset into non_competitors
      } while (iLoopCounter < iCarLoopBytes);
    }
    iCompetitorMode = competitors;
    iCarsBytesTotal = 4 * numcars;
    if ((unsigned int)competitors < 2) {                                           // Competitor mode 1: Mark AI cars as non-competitors
      if (competitors == 1 && numcars > 0) {
        uiRandomCarIdx = 0;
        do {
          if (!human_control[uiRandomCarIdx / 4])
            non_competitors[uiRandomCarIdx / 4] = -1;
          uiRandomCarIdx += 4;
        } while ((int)uiRandomCarIdx < iCarsBytesTotal);
      }
    } else if ((unsigned int)competitors <= 2) {                                           // Competitor mode 2: Mark AI cars as non-competitors
      if (numcars > 0) {
        for (int i = 0; i < numcars; ++i) {
          if (!human_control[i])
            non_competitors[i] = -1;
        }
        //iCompetitorMode = 0;
        //do {
        //  if (!*(int *)((char *)human_control + iCompetitorMode))
        //    *(int *)((char *)non_competitors + iCompetitorMode) = -1;
        //  iCompetitorMode += 4;
        //} while (iCompetitorMode < iCarsBytesTotal);
      }
      if (players == 1)                       // Single player mode: randomly select one AI as competitor
      {
        do {
          iRandResult = rand();// iCompetitorMode);
          iNormalizedCarSlot = GetHighOrderRand(8, iRandResult);  // Normalize to range [0, 8)

          if (iNormalizedCarSlot == 8)
            iNormalizedCarSlot = 7;
        } while (human_control[iNormalizedCarSlot]);
        non_competitors[iNormalizedCarSlot] = 0;
        //do {
        //  iRandResult = rand(iCompetitorMode);  // Generate random car slot until we find an AI-controlled one
        //  // iNormalizedCarSlot = (8 * iRandResult) / 32768;
        //  iNormalizedCarSlot = (8 * iRandResult
        //                      - (__CFSHL__((8 * iRandResult) >> 31, 15)
        //                         + ((8 * iRandResult) >> 31 << 15))) >> 15;
        //  if (iNormalizedCarSlot == 8)
        //    iNormalizedCarSlot = 7;
        //  iCompetitorMode = 8 * iNormalizedCarSlot;
        //} while (*(int *)((char *)human_control + iCompetitorMode));
        //*(int *)((char *)&non_competitors[1] + iCompetitorMode) = 0;
      }
    } else if (competitors == 8) {                                           // Competitor mode 8: All AI cars marked as non-competitors initially
      if (numcars > 0) {
        for (int i = 0; i < numcars; ++i) {
            if (!human_control[i])
                non_competitors[i] = -1;
        }
        //iCompetitorMode = 0;
        //do {
        //  if (!*(int *)((char *)human_control + iCompetitorMode))
        //    *(int *)((char *)non_competitors + iCompetitorMode) = -1;
        //  iCompetitorMode += 4;
        //} while (iCompetitorMode < iCarsBytesTotal);
      }
      if (players < 8)                        // Add random AI cars as competitors until we have 8 total
      {
        int iActiveCompetitors = players;
        while (1) {
          int randVal = rand();// iCompetitorMode);
          int slot = GetHighOrderRand(8, randVal);
          if (slot == 8)
            slot = 7;

          iCompetitorMode = 8 * slot;

          if (!human_control[slot]) {
            if (non_competitors[slot] != 0) {
              ++iActiveCompetitors;
              non_competitors[slot] = 0;

              if (iActiveCompetitors >= 8)
                break;
            }
          }
        }
        //iActiveCompetitors = players;
        //while (1) {
        //  v29 = rand(iCompetitorMode);
        //  v30 = (8 * v29 - (__CFSHL__((8 * v29) >> 31, 15) + ((8 * v29) >> 31 << 15))) >> 15;
        //  if (v30 == 8)
        //    v30 = 7;
        //  iCompetitorMode = 8 * v30;
        //  if (!*(int *)((char *)human_control + iCompetitorMode)) {
        //    if (*(int *)((char *)&non_competitors[1] + iCompetitorMode)) {
        //      ++iActiveCompetitors;
        //      *(int *)((char *)&non_competitors[1] + iCompetitorMode) = 0;
        //      if (iActiveCompetitors >= 8)
        //        break;
        //    }
        //  }
        //}
      }
    }
  }
  if ((cheat_mode & 0x200) != 0)              // Mr. Evil cheat mode - replace AI drivers with Mr. Evil
  {
    iEvilCarCounter = 0;
    if (numcars > 0) {
      pszEvilNameDest = driver_names[0];
      iEvilCarIdx = 0;
      do {
        if (!human_control[iEvilCarIdx]) {
          pszEvilNameSrc = szMrEvil;
          pszEvilNameCharDest = pszEvilNameDest;
          Drivers_Car[iEvilCarIdx] = 13;        // Set AI car to Mr. Evil's car type (13) and copy name
          do {
            ++pszEvilNameCharDest;
            byEvilNameChar = *pszEvilNameSrc++;
            *(pszEvilNameCharDest - 1) = byEvilNameChar;
          } while (pszEvilNameSrc != &szMrEvil[9]);
        }
        ++iEvilCarIdx;
        ++iEvilCarCounter;
        pszEvilNameDest += 9;
      } while (iEvilCarCounter < numcars);
    }
  }
  iFinalCaridx = 0;                             // Final step: Set up player-to-car mappings for human-controlled cars
  if (numcars > 0) {
    iTotalCarsFinal = numcars;
    iCarSlotIdx = 0;
    do {                                           // Map car slot back to player index for human-controlled cars
      if (human_control[iCarSlotIdx])
        player_to_car[car_to_player[iCarSlotIdx]] = iFinalCaridx;
      ++iFinalCaridx;
      ++iCarSlotIdx;
    } while (iFinalCaridx < iTotalCarsFinal);
  }
}

//-------------------------------------------------------------------------------------------------
//0005E1C0
void check_cars()
{
  //int v0; // ebx
  int iPlayerCount; // eax
  int i; // edx
  int j; // eax
  int iCarId; // edi
  int iCarId2; // ebx

  memset(allocated_cars, 0, sizeof(allocated_cars));
  //_STOSD(allocated_cars, 0, v0, 14u);

  // determine number of players
  iPlayerCount = player_type;
  if (!player_type) {
    players = 1;
    goto LABEL_8;
  }
  if ((unsigned int)player_type <= 1) {
    iPlayerCount = network_on;
    goto LABEL_7;
  }
  if (player_type == 2)
    LABEL_7:
  players = iPlayerCount;
LABEL_8:
  i = 0;
  if (players > 0) {
    j = 0;
    do {
      iCarId = Players_Cars[j];
      if (iCarId >= 0) {
        // If cheat bit CHEAT_MODE_CLONES is not enabled, count the car usage
        if ((cheat_mode & CHEAT_MODE_CLONES) == 0)
          ++allocated_cars[iCarId];

        // Assign player index to car_to_player
        iCarId2 = Players_Cars[j];
        if (iCarId2 <= 7) { //added by ROLLER, ignore cheat cars, bug in original game
          if (allocated_cars[iCarId2] == 1)
            car_to_player[2 * iCarId2] = i;
          else
            car_to_player[2 * iCarId2 + 1] = i;
        }
      }
      ++j;
      ++i;
    } while (i < players);
  }
}

//-------------------------------------------------------------------------------------------------
//0005E300
void select_messages()
{
  uint8 *pScreenBuffer; // edi
  tBlockHeader *pBlockHeader; // esi
  unsigned int uiBufferSize; // ecx
  char byBufferSizeByte; // al
  unsigned int uiQwordCopySize; // ecx
  unsigned int uiCurrentSelection; // edi
  char byTextColor; // al
  int iPlayerIndex; // esi
  char *pPlayerName; // edi
  char byPlayerTextColor; // al
  int iCursorX; // eax
  unsigned int uiKeyCode; // eax
  int iCharCode; // ebx
  unsigned int uiExtendedKey; // eax
  int j; // eax
  int i; // eax
  int iSendConfirmation; // [esp+4h] [ebp-1Ch]
  int iMessageLength; // [esp+8h] [ebp-18h]
  unsigned int uiCurrentMenu; // [esp+Ch] [ebp-14h]
  unsigned int uiPreviousMenu; // [esp+Ch] [ebp-14h]
  int iSelectedPlayer; // [esp+10h] [ebp-10h]
  int iMenuSelection; // [esp+14h] [ebp-Ch]
  int iExitFlag; // [esp+18h] [ebp-8h]
  int iY; // [esp+1Ch] [ebp-4h]

  // Initialize UI state variables
  iExitFlag = 0;
  iSelectedPlayer = 0;
  iMenuSelection = 0;
  uiCurrentMenu = 0;
  send_status = 0;
  send_message_to = -1;
  iSendConfirmation = 0;
  iMessageLength = 0;

  // Calculate current message length in send buffer
  if (send_buffer[0]) {
    while (send_buffer[++iMessageLength])
      ;
  }
MAIN_UI_LOOP:
  if (!iExitFlag)                             // MAIN_UI_LOOP: Main display and input processing loop
  {
    if (iSelectedPlayer < 0 || iSelectedPlayer >= network_on)
      iSelectedPlayer = 0;

    // Setup screen buffer and copy VGA frame buffer
    pScreenBuffer = scrbuf;
    pBlockHeader = front_vga[0];
    if (SVGA_ON)
      uiBufferSize = 256000;
    else
      uiBufferSize = 64000;
    byBufferSizeByte = uiBufferSize;
    uiQwordCopySize = uiBufferSize >> 2;
    memcpy(scrbuf, front_vga[0], 4 * uiQwordCopySize);
    memcpy(&pScreenBuffer[4 * uiQwordCopySize], &pBlockHeader->iWidth + uiQwordCopySize, byBufferSizeByte & 3);

    // Display UI elements: header, message panel, game type indicator
    display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[5], 1, -4, 247, 0);
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    uiCurrentSelection = uiCurrentMenu;
    display_block(scrbuf, front_vga[4], 4, 76, 257, -1);
    if (uiCurrentMenu) {
      uiPreviousMenu = uiCurrentMenu - 1;
      if ((int)(uiCurrentSelection - 1) >= 3) {
        display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
      } else {
        display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
        front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[2 * uiPreviousMenu].x, sel_posns[2 * uiPreviousMenu].y, 0x8Fu, 0);
      }
      uiCurrentMenu = uiPreviousMenu + 1;
    } else if (iMenuSelection >= 3) {
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    } else {
      display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[2 * iMenuSelection].x, sel_posns[2 * iMenuSelection].y, 0x8Fu, 0);
    }
    front_text(front_vga[2], &language_buffer[7168], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &language_buffer[7232], font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &language_buffer[7296], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
    switch (uiCurrentMenu) {
      case 0u:
        // Menu 0: Send to player selection screen
        if (iSelectedPlayer)
          sprintf(buffer, "%s", player_names[iSelectedPlayer]);
        else
          sprintf(buffer, "%s", &language_buffer[7360]);
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 190, 66, 143, 0, 180, 640);
        scale_text(front_vga[15], send_buffer, font1_ascii, font1_offsets, 190, 110, 143, 0, 180, 640);
        if (send_status > 0)
          goto SHOW_SENDING_STATUS;
        if (send_status)
          goto SHOW_SEND_FAILED;
        if (iSendConfirmation) {
          uiCurrentMenu = send_status;
          iSendConfirmation = send_status;
          iMenuSelection = 2;
        }
        goto UPDATE_DISPLAY;
      case 1u:
        // Menu 1: Player list selection screen
        scale_text(front_vga[15], &language_buffer[7424], font2_ascii, font2_offsets, 400, 60, 143, 1u, 200, 640);
        if (iMenuSelection)
          byTextColor = -113;
        else
          byTextColor = -85;
        iPlayerIndex = 1;
        scale_text(front_vga[15], &language_buffer[7360], font2_ascii, font2_offsets, 400, 98, byTextColor, 1u, 200, 640);
        if (network_on > 1) {
          pPlayerName = player_names[1];
          iY = 116;
          do {
            if (iPlayerIndex == iMenuSelection)
              byPlayerTextColor = -85;
            else
              byPlayerTextColor = -113;
            scale_text(front_vga[15], pPlayerName, font2_ascii, font2_offsets, 400, iY, byPlayerTextColor, 1u, 200, 640);
            ++iPlayerIndex;
            pPlayerName += 9;
            iY += 18;
          } while (iPlayerIndex < network_on);
        }
        goto UPDATE_DISPLAY;
      case 2u:
        // Menu 2: Message composition screen with cursor
        scale_text(front_vga[15], &language_buffer[7488], font2_ascii, font2_offsets, 400, 60, 143, 1u, 200, 640);
        if ((frames & 0xFu) < 8) {
          iCursorX = stringwidth(send_buffer) + 190;
          if (iCursorX <= 620)
            scale_text(front_vga[15], "_", font1_ascii, font1_offsets, iCursorX, 110, 171, 0, 180, 640);
          else
            scale_text(front_vga[15], "_", font1_ascii, font1_offsets, 621, 110, 171, 0, 180, 640);
        }
        scale_text(front_vga[15], send_buffer, font1_ascii, font1_offsets, 190, 110, 143, 0, 180, 630);
        goto UPDATE_DISPLAY;
      case 3u:
        // Menu 3: Send confirmation screen
        if (iSelectedPlayer)
          sprintf(buffer, "%s", player_names[iSelectedPlayer]);
        else
          sprintf(buffer, "%s", &language_buffer[7360]);
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 190, 66, 143, 0, 180, 640);
        scale_text(front_vga[15], send_buffer, font1_ascii, font1_offsets, 190, 110, 143, 0, 180, 640);
        scale_text(front_vga[15], &language_buffer[7552], font2_ascii, font2_offsets, 400, 150, 143, 1u, 200, 640);
        if (send_status > 0) {
        SHOW_SENDING_STATUS:
          scale_text(front_vga[15], &language_buffer[7616], font2_ascii, font2_offsets, 400, 180, 231, 1u, 200, 640);// SHOW_SENDING_STATUS: Display "Sending..." message
        } else {
          if (!send_status) {
            if (iSendConfirmation) {
              uiCurrentMenu = send_status;
              iSendConfirmation = send_status;
              iMenuSelection = 2;
            }
            goto UPDATE_DISPLAY;
          }
        SHOW_SEND_FAILED:
          scale_text(front_vga[15], &language_buffer[7680], font2_ascii, font2_offsets, 400, 180, 231, 1u, 200, 640);// SHOW_SEND_FAILED: Display "Send failed" message
        }
        --send_status;
      UPDATE_DISPLAY:
        show_received_mesage();                 // UPDATE_DISPLAY: Show received messages and copy screen buffer
        copypic(scrbuf, screen);
        while (1) {
          UpdateSDL();
          // Main input processing loop
          if (!fatkbhit())
            goto MAIN_UI_LOOP;
          uiKeyCode = fatgetch();
          iCharCode = uiKeyCode;
          if (uiKeyCode < 8) {
            if (uiKeyCode)
              goto PROCESS_CHARACTER_INPUT;
            // Handle extended keys (arrows, function keys)
            uiExtendedKey = fatgetch();
            if (uiExtendedKey >= 0x48) {
              if (uiExtendedKey <= 0x48) {                                 // Up arrow key - move selection up
                if (uiCurrentMenu <= 1 && iMenuSelection > 0)
                  --iMenuSelection;
              } else if (uiExtendedKey == 80) {                                 // Down arrow key - move selection down
                if (uiCurrentMenu) {
                  if (uiCurrentMenu == 1 && network_on - 1 > iMenuSelection)
                    ++iMenuSelection;
                } else if (iMenuSelection < 3) {
                  ++iMenuSelection;
                }
              }
            }
          } else if (uiKeyCode <= 8) {                                     // Backspace key - remove character from message
            if (uiCurrentMenu == 2 && iMessageLength > 0) {
              send_buffer[iMessageLength--] = 0;
              send_buffer[iMessageLength] = 0;
            }
            if (uiCurrentMenu == 3 && !iSendConfirmation) {
              uiCurrentMenu = 0;
              iMenuSelection = 2;
            }
          } else if (uiKeyCode < 0xD) {                                     // PROCESS_CHARACTER_INPUT: Handle regular character input
          PROCESS_CHARACTER_INPUT:
            if (uiCurrentMenu == 3 && !iSendConfirmation) {
              if (uiKeyCode == 121 || uiKeyCode == 89) {
                iSendConfirmation = -1;
                send_message_to = iSelectedPlayer;

                for (i = 0; i < 32; ++i) {
                  send_mes_buf[i] = send_buffer[i];
                }
                //for (i = 0; i < 32; rec_mes_buf[i + 31] = round_pics[7][i + 12])// Fixed loop: Copy send_buffer to send_mes_buf (32 bytes)
                //{
                //  i += 8;
                //  rec_mes_buf[i + 24] = round_pics[7][i + 5];// offset into send_mes_buf and send_buffer
                //  rec_mes_buf[i + 25] = round_pics[7][i + 6];
                //  rec_mes_buf[i + 26] = round_pics[7][i + 7];
                //  rec_mes_buf[i + 27] = round_pics[7][i + 8];
                //  rec_mes_buf[i + 28] = round_pics[7][i + 9];
                //  rec_mes_buf[i + 29] = round_pics[7][i + 10];
                //  rec_mes_buf[i + 30] = round_pics[7][i + 11];
                //}
              } else {
                uiCurrentMenu = 0;
                iMenuSelection = 2;
              }
            }
            if (uiCurrentMenu == 2 && iMessageLength < 30) {                                   // Handle shift key combinations for special characters
              if (keys[WHIP_SCANCODE_LSHIFT] || keys[WHIP_SCANCODE_RSHIFT]) {
                switch (iCharCode) {
                  case '#':
                    iCharCode = 126;
                    break;
                  case '\'':
                    iCharCode = 64;
                    break;
                  case ',':
                    iCharCode = 60;
                    break;
                  case '-':
                    iCharCode = 95;
                    break;
                  case '.':
                    iCharCode = 62;
                    break;
                  case '/':
                    iCharCode = 63;
                    break;
                  case '0':
                    iCharCode = 41;
                    break;
                  case '1':
                    iCharCode = 33;
                    break;
                  case '2':
                    iCharCode = 34;
                    break;
                  case '3':
                    iCharCode = 156;
                    break;
                  case '4':
                    iCharCode = 36;
                    break;
                  case '5':
                    iCharCode = 37;
                    break;
                  case '6':
                    iCharCode = 94;
                    break;
                  case '7':
                    iCharCode = 38;
                    break;
                  case '8':
                    iCharCode = 42;
                    break;
                  case '9':
                    iCharCode = 40;
                    break;
                  case ';':
                    iCharCode = 58;
                    break;
                  case '=':
                    iCharCode = 43;
                    break;
                  default:
                    break;
                }
              }
              if (iCharCode >= 97 && iCharCode <= 122)// Convert lowercase to uppercase for message input
                iCharCode -= 32;
              if (iCharCode != 127) {
                send_buffer[iMessageLength++] = iCharCode;
                send_buffer[iMessageLength] = 0;
              }
            }
          } else if (uiKeyCode <= 0xD) {                                     // Enter key - handle menu navigation and confirmations
            if (uiCurrentMenu <= 3) {
              switch (uiCurrentMenu) {
                case 0u:
                  uiCurrentMenu = iMenuSelection + 1;
                  switch (iMenuSelection) {
                    case 0:
                      iMenuSelection = iSelectedPlayer;
                      break;
                    case 1:
                      iMessageLength = 0;
                      if (send_buffer[0]) {
                        while (send_buffer[++iMessageLength])
                          ;
                      }
                      break;
                    case 2:
                      iSendConfirmation = -1;
                      send_message_to = iSelectedPlayer;


                      for (j = 0; j < 32; ++j) {
                        send_mes_buf[j] = send_buffer[j];
                      }
                      //for (j = 0; j < 32; rec_mes_buf[j + 31] = round_pics[7][j + 12])// Fixed loop: Copy send_buffer to send_mes_buf (32 bytes)
                      //{
                      //  j += 8;
                      //  rec_mes_buf[j + 24] = round_pics[7][j + 5];
                      //  rec_mes_buf[j + 25] = round_pics[7][j + 6];
                      //  rec_mes_buf[j + 26] = round_pics[7][j + 7];
                      //  rec_mes_buf[j + 27] = round_pics[7][j + 8];
                      //  rec_mes_buf[j + 28] = round_pics[7][j + 9];
                      //  rec_mes_buf[j + 29] = round_pics[7][j + 10];
                      //  rec_mes_buf[j + 30] = round_pics[7][j + 11];
                      //}
                      uiCurrentMenu = 0;
                      break;
                    case 3:
                      goto EXIT_FUNCTION;
                    default:
                      continue;
                  }
                  break;
                case 1u:
                  uiCurrentMenu = 0;
                  iSelectedPlayer = iMenuSelection;
                  iMenuSelection = 0;
                  break;
                default:
                  continue;
              }
            }
          } else {
            if (uiKeyCode != 27)
              goto PROCESS_CHARACTER_INPUT;
            if (uiCurrentMenu)                // Escape key - go back or exit
              uiCurrentMenu = 0;
            else
              EXIT_FUNCTION:
            iExitFlag = -1;                   // EXIT_FUNCTION: Set exit flag and return from function
          }
        }
      default:
        goto UPDATE_DISPLAY;                    // Main menu state machine - handle different UI screens
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0005EDE0
void show_received_mesage()
{                                               // Check if there's a message to display and screen is not fading
  int iRecMesWidth; // ebx
  int iBufStrWidth; // eax
  int iWindowLeft; // ecx
  int iWindowRight; // edx
  int iAdjustedLeft; // ecx

  if (rec_status > 0 && front_fade) {
    sprintf(buffer, "%s %s", &language_buffer[7744], rec_mes_name);// Format message header with sender name
    iRecMesWidth = stringwidth(rec_mes_buf);    // Calculate width of the actual message text
    iBufStrWidth = stringwidth(buffer);         // Calculate width of the header (sender info)
    if (iBufStrWidth > iRecMesWidth)          // Use the wider of the two strings for window width
      iRecMesWidth = iBufStrWidth;
    iWindowLeft = 400 - iRecMesWidth / 2;       // Calculate left edge of window (center at x=400)
    if (iWindowLeft < 180)                    // Ensure window doesn't go too far left (min x=180)
      iWindowLeft = 180;
    iWindowRight = iRecMesWidth / 2 + 408;      // Calculate right edge of window
    iAdjustedLeft = iWindowLeft - 8;
    if (iWindowRight > 639)                   // Ensure window doesn't go past screen edge (max x=639)
      iWindowRight = 639;
    blankwindow(iAdjustedLeft / 2, 86, iWindowRight / 2, 118);// Draw message window background (coordinates are halved for some reason)
    scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 400, 180, 143, 1u, 180, 640);// Draw sender info text at y=180
    scale_text(front_vga[15], rec_mes_buf, font1_ascii, font1_offsets, 400, 210, 143, 1u, 180, 640);// Draw message text at y=210
    copypic(scrbuf, screen);                    // Copy rendered screen buffer to display
    rec_status = 0;                             // Clear message received flag
    frames = 0;                                 // Reset frame counter for message display duration
    do {
      if (time_to_start)
        break;
      while (fatkbhit()) {
        if (!fatgetch())
          fatgetch();
      }
    } while (frames < 72);                      // Show message for at least 72 frames
    if (frames >= 72 && !time_to_start)       // After 72 frames, wait for any key press to dismiss
    {
      while (!fatkbhit() && !time_to_start)
        ;
      while (fatkbhit() && !time_to_start) {
        if (!fatgetch())
          fatgetch();
      }
    }
    frames = 0;                                 // Reset frame counter when done
  }
}

//-------------------------------------------------------------------------------------------------
//0005EFB0
int select_netslot()
{
  int iSlot1PlayerCount; // ebx
  int iStringIndex; // esi
  int iCurrentSlot; // ebp
  int iDigitIndex; // edx
  int i; // eax
  //char byDigitChar; // bl
  int iSlotCounter; // edx
  int iSlotIndex; // eax
  uint8 *pScreenBuffer; // edi
  tBlockHeader *pBlockHeader; // esi
  unsigned int uiBufferSize; // ecx
  char byBufferSizeByte; // al
  unsigned int uiQwordCopySize; // ecx
  char *pSlot1Names; // esi
  int iSlot1YPos; // edi
  int iSlot2PlayerIndex; // edi
  char *pSlot2Names; // esi
  char *pSlot3Names; // esi
  int iSlot3YPos; // edi
  int iSlot4PlayerIndex; // edi
  char *pSlot4Names; // esi
  unsigned int uiKeyCode; // eax
  int iNextSlot; // ebx
  unsigned int uiExtendedKey; // eax
  int iPrevSlot; // edx
  int iLeftSearchSlot; // eax
  int iRightSearchSlot; // edx
  int iRightSearchIndex; // eax
  int textColor[4]; // [esp+0h] [ebp-4Ch]
  char szSlotNumberBuffer[12]; // [esp+10h] [ebp-3Ch]
  int iY; // [esp+1Ch] [ebp-30h]
  int iReturnValue; // [esp+20h] [ebp-2Ch]
  int iExitFlag; // [esp+24h] [ebp-28h]
  int iSlot4YPos; // [esp+28h] [ebp-24h]
  int iSlot3PlayerIndex; // [esp+2Ch] [ebp-20h]
  int iSlot1PlayerIndex; // [esp+30h] [ebp-1Ch]

  iExitFlag = 0;                                // Initialize network slot selection variables
  network_slot = -1;
  iReturnValue = -1;
  Initialise_Network(-1);                       // Initialize network with slot -1 (discovery mode)
  if (network_on) {
    iReturnValue = 0;
    iSlot1PlayerCount = 0;
    iStringIndex = 7;
    iCurrentSlot = 0;
    szSlotNumberBuffer[8] = 0;                  // Setup slot number string conversion and text colors
    textColor[1] = 0x83;
    textColor[2] = 0x83;
    textColor[3] = 0x83;
    textColor[0] = 0xAB;
    do {
      szSlotNumberBuffer[iStringIndex--] = iSlot1PlayerCount % 10 + '0';// Convert slot number to ASCII digits (reverse order)
      iSlot1PlayerCount /= 10;
    } while (iSlot1PlayerCount > 0);

    // Copy digits from conversion area to display area of buffer
    iDigitIndex = 0;
    for (i = iStringIndex + 1; i < 8; ++i) {
        szSlotNumberBuffer[iDigitIndex + 3] = szSlotNumberBuffer[i];
        ++iDigitIndex;
    }
    //for (i = iStringIndex + 1; i < 8; *((_BYTE *)&textColor[3] + iDigitIndex + 3) = byDigitChar)// Copy converted digits to buffer in correct order
    //{
    //  ++iDigitIndex;
    //  byDigitChar = szSlotNumberBuffer[i++];
    //}                                           // 
                                                // 
    while (1)                                 // MAIN_DISPLAY_LOOP: Main UI display and input loop
    {
      if (iExitFlag)
        return iReturnValue;

      // Update slot selection colors (highlight current slot)
      iSlotCounter = 0;
      iSlotIndex = 0;
      do {
        if (gamers_playing[iSlotIndex] == 16) {
          if (iSlotCounter == iCurrentSlot)
            textColor[iSlotIndex] = 0xA8;
          else
            textColor[iSlotIndex] = 0x7F;
        }
        ++iSlotCounter;
        ++iSlotIndex;
      } while (iSlotCounter < 4);

      // Setup screen buffer and copy VGA frame
      pScreenBuffer = scrbuf;
      pBlockHeader = front_vga[0];
      if (SVGA_ON)
        uiBufferSize = 256000;
      else
        uiBufferSize = 64000;
      byBufferSizeByte = uiBufferSize;
      uiQwordCopySize = uiBufferSize >> 2;
      memcpy(scrbuf, front_vga[0], 4 * uiQwordCopySize);
      memcpy(&pScreenBuffer[4 * uiQwordCopySize], &pBlockHeader->iWidth + uiQwordCopySize, byBufferSizeByte & 3);

      // Display UI elements: header, panels, game type
      display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);
      display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
      display_block(scrbuf, front_vga[5], 1, -4, 247, 0);
      display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
      display_block(scrbuf, front_vga[4], 4, 76, 257, -1);
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
      scale_text(front_vga[15], &language_buffer[6528], font1_ascii, font1_offsets, 400, 55, 143, 1u, 200, 640);// Display slot selection instructions
      scale_text(front_vga[15], &language_buffer[6592], font1_ascii, font1_offsets, 400, 73, 143, 1u, 200, 640);
      sprintf(buffer, "%s1", &language_buffer[7808]);// Display Slot 1 header and status
      scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 260, 92, textColor[0], 1u, 200, 320);
      if (gamers_playing[0] < 0) {
        if (gamers_playing[0] == -2) {
          scale_text(front_vga[15], &language_buffer[8000], font1_ascii, font1_offsets, 260, 200, textColor[0], 1u, 200, 319);
          goto SLOT2_DISPLAY;
        }
      } else {
        if (gamers_playing[0] <= 0) {
          scale_text(front_vga[15], &language_buffer[7872], font1_ascii, font1_offsets, 260, 200, textColor[0], 1u, 200, 319);
          goto SLOT2_DISPLAY;
        }
        if (gamers_playing[0] == 16) {
          scale_text(front_vga[15], &language_buffer[7936], font1_ascii, font1_offsets, 260, 200, textColor[0], 1u, 200, 319);
          goto SLOT2_DISPLAY;
        }
      }
      iSlot1PlayerIndex = 0;

      // Display Slot 1 player names if slot has players
      if (gamers_playing[0] > 0) {
        pSlot1Names = gamers_names[0];
        iSlot1YPos = 110;
        do {
          scale_text(front_vga[15], pSlot1Names, font1_ascii, font1_offsets, 260, iSlot1YPos, textColor[0], 1u, 200, 319);
          pSlot1Names += 9;
          iSlot1YPos += 18;
          ++iSlot1PlayerIndex;
        } while (iSlot1PlayerIndex < gamers_playing[0]);
      }
    SLOT2_DISPLAY:
          // SLOT2_DISPLAY: Display Slot 2 header and status
      sprintf(buffer, "%s2", &language_buffer[7808]);
      scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 370, 92, textColor[1], 1u, 200, 640);
      if (gamers_playing[1] < 0) {
        if (gamers_playing[1] == -2) {
          scale_text(front_vga[15], &language_buffer[8000], font1_ascii, font1_offsets, 370, 200, textColor[1], 1u, 321, 419);
          goto SLOT3_DISPLAY;
        }
      } else {
        if (gamers_playing[1] <= 0) {
          scale_text(front_vga[15], &language_buffer[7872], font1_ascii, font1_offsets, 370, 200, textColor[1], 1u, 321, 419);
          goto SLOT3_DISPLAY;
        }
        if (gamers_playing[1] == 16) {
          scale_text(front_vga[15], &language_buffer[7936], font1_ascii, font1_offsets, 370, 200, textColor[1], 1u, 321, 419);
          goto SLOT3_DISPLAY;
        }
      }
      iSlot2PlayerIndex = 0;

      // Display Slot 2 player names if slot has players
      if (gamers_playing[1] > 0) {
        pSlot2Names = gamers_names[1];
        iY = 110;
        do {
          scale_text(front_vga[15], pSlot2Names, font1_ascii, font1_offsets, 370, iY, textColor[1], 1u, 321, 419);
          ++iSlot2PlayerIndex;
          pSlot2Names += 9;
          iY += 18;
        } while (iSlot2PlayerIndex < gamers_playing[1]);
      }
    SLOT3_DISPLAY:
          // SLOT3_DISPLAY: Display Slot 3 header and status
      sprintf(buffer, "%s3", &language_buffer[7808]);
      scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 474, 92, textColor[2], 1u, 200, 640);
      if (gamers_playing[2] < 0) {
        if (gamers_playing[2] == -2) {
          scale_text(front_vga[15], &language_buffer[8000], font1_ascii, font1_offsets, 474, 200, textColor[2], 1u, 421, 519);
          goto SLOT4_DISPLAY;
        }
      } else {
        if (gamers_playing[2] <= 0) {
          scale_text(front_vga[15], &language_buffer[7872], font1_ascii, font1_offsets, 474, 200, textColor[2], 1u, 421, 519);
          goto SLOT4_DISPLAY;
        }
        if (gamers_playing[2] == 16) {
          scale_text(front_vga[15], &language_buffer[7936], font1_ascii, font1_offsets, 474, 200, textColor[2], 1u, 421, 519);
          goto SLOT4_DISPLAY;
        }
      }
      iSlot3PlayerIndex = 0;

      // Display Slot 3 player names if slot has players
      if (gamers_playing[2] > 0) {
        pSlot3Names = gamers_names[2];
        iSlot3YPos = 110;
        do {
          scale_text(front_vga[15], pSlot3Names, font1_ascii, font1_offsets, 474, iSlot3YPos, textColor[2], 1u, 421, 519);
          pSlot3Names += 9;
          iSlot3YPos += 18;
          ++iSlot3PlayerIndex;
        } while (iSlot3PlayerIndex < gamers_playing[2]);
      }
    SLOT4_DISPLAY:
          // SLOT4_DISPLAY: Display Slot 4 header and status
      sprintf(buffer, "%s4", &language_buffer[7808]);
      scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 580, 92, textColor[3], 1u, 520, 640);
      if (gamers_playing[3] < 0) {
        if (gamers_playing[3] == -2) {
          scale_text(front_vga[15], &language_buffer[8000], font1_ascii, font1_offsets, 580, 200, textColor[3], 1u, 521, 639);
          goto UPDATE_DISPLAY;
        }
      } else {
        if (gamers_playing[3] <= 0) {
          scale_text(front_vga[15], &language_buffer[7872], font1_ascii, font1_offsets, 580, 200, textColor[3], 1u, 521, 639);
          goto UPDATE_DISPLAY;
        }
        if (gamers_playing[3] == 16) {
          scale_text(front_vga[15], &language_buffer[7936], font1_ascii, font1_offsets, 580, 200, textColor[3], 1u, 521, 639);
          goto UPDATE_DISPLAY;
        }
      }
      iSlot4PlayerIndex = 0;

      // Display Slot 4 player names if slot has players
      if (gamers_playing[3] > 0) {
        pSlot4Names = gamers_names[3];
        iSlot4YPos = 110;
        do {
          scale_text(front_vga[15], pSlot4Names, font1_ascii, font1_offsets, 580, iSlot4YPos, textColor[3], 1u, 521, 639);
          ++iSlot4PlayerIndex;
          pSlot4Names += 9;
          iSlot4YPos += 18;
        } while (iSlot4PlayerIndex < gamers_playing[3]);
      }
    UPDATE_DISPLAY:
          // UPDATE_DISPLAY: Show received messages and update screen
      show_received_mesage();
      copypic(scrbuf, screen);

      // Input processing loop - handle keyboard input
      while (fatkbhit()) {
        uiKeyCode = fatgetch();
        iNextSlot = iCurrentSlot + 1;
        if (uiKeyCode < 0xD) {                                       // Handle extended keys (arrows)
          if (!uiKeyCode) {
            uiExtendedKey = fatgetch();
            if (uiExtendedKey >= WHIP_SCANCODE_LEFT) {
              if (uiExtendedKey <= WHIP_SCANCODE_LEFT) {                                 // Left arrow - move to previous available slot
                if (iCurrentSlot > 0) {
                  textColor[iCurrentSlot] = 131;
                  iPrevSlot = iCurrentSlot - 1;
                  if (iCurrentSlot - 1 > 0) {
                    iLeftSearchSlot = iPrevSlot;
                    do {
                      if (gamers_playing[iLeftSearchSlot] != 16)
                        break;                  // Skip slots with status 16 (unavailable) when going left
                      --iLeftSearchSlot;
                      --iPrevSlot;
                    } while (iLeftSearchSlot > 0);
                  }
                  if (gamers_playing[iPrevSlot] < 16)
                    iCurrentSlot = iPrevSlot;
                  textColor[iCurrentSlot] = 171;
                }
              } else if (uiExtendedKey == WHIP_SCANCODE_RIGHT && iCurrentSlot < 3)// Right arrow - move to next available slot
              {
                textColor[iCurrentSlot] = 131;
                iRightSearchSlot = iCurrentSlot + 1;
                if (iNextSlot < 3) {
                  iRightSearchIndex = iNextSlot;
                  do {
                    if (gamers_playing[iRightSearchIndex] != 16)
                      break;                    // Skip slots with status 16 (unavailable) when going right
                    ++iRightSearchIndex;
                    ++iRightSearchSlot;
                  } while (iRightSearchIndex < 3);
                }
                if (gamers_playing[iRightSearchSlot] < 16)
                  iCurrentSlot = iRightSearchSlot;
                textColor[iCurrentSlot] = 171;
              }
            }
          }
        } else if (uiKeyCode <= 0xD) {                                       // Enter key - select current slot if available
          if ((unsigned int)gamers_playing[iCurrentSlot] < 16) {
            iExitFlag = -1;
            iReturnValue = iCurrentSlot + 1;
          }
        } else if (uiKeyCode == 27) {
          iExitFlag = -1;                       // Escape key - cancel slot selection
          iReturnValue = -2;
        }
      }
    }
  }
  return iReturnValue;
}

//-------------------------------------------------------------------------------------------------