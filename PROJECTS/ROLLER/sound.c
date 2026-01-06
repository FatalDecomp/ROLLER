#include "sound.h"
#include "frontend.h"
#include "moving.h"
#include "cdx.h"
#include "func2.h"
#include "func3.h"
#include "3d.h"
#include "loadtrak.h"
#include "car.h"
#include "roller.h"
#include "control.h"
#include "network.h"
#include "replay.h"
#include "tower.h"
#include "view.h"
#include "transfrm.h"
#include <memory.h>
#include <ctype.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <fcntl.h>
#include <assert.h>
#include <math.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif

//-------------------------------------------------------------------------------------------------

int samplespending = 0;     //000A4690
int writesample = 0;        //000A4694
int readsample = 0;         //000A4698
int lastsample = 0;         //000A469C
int musicon = -1;           //000A46A0
int soundon = -1;           //000A46A4
int allengines = -1;        //000A46A8
int cheat_samples = 0;      //000A46AC
int palette_brightness = 32;//000A46B0
int last_inp[2] = { 0, 0 }; //000A46BC
void *pal_selector = (void *)-1; //000A46B4
char SourcePath[64] = { 0 }; //000A46C4
char DestinationPath[64] = { 0 }; //000A4704
char languagename[32] = { 0 }; //000A4744
int writeptr = 0;           //000A476C
int readptr = 0;            //000A4770
int SoundCard = 0;          //000A4774
int SoundPort = 0;          //000A4778
int SoundIRQ = 0;           //000A477C
int SoundDMA = 0;           //000A4780
int EngineVolume = 32;      //000A4784
int SFXVolume = 56;         //000A4788
int SpeechVolume = 127;     //000A478C
int MusicVolume = 108;      //000A4790
int MusicCard = 0;          //000A4794
int MusicCD = 0;            //000A4798
int MusicPort = 0;          //000A479C
uint8 *SongPtr = NULL;      //000A47A0
int SongHandle = 0;         //000A47A4
int CDSong[20] = { 10, 10, 10, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0 }; //000A47A8
int GMSong[21] = { 0, 1, 2, 3, 4, 5, 6, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 }; //000A47F8
tSampleData SampleData = { NULL, 0u, 0, 0, 2, 32767, 0, 0, 0, 0, 18176, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1701995379 } }; //000A484C
tSampleData SampleFixed = { NULL, 0u, 0, 0, 2, 32767, 0, 0, 0, 0, 256, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }; //000A48C4
tSampleData SamplePanned = { NULL, 0u, 0, 0, 2, 32767, 0, 0, 0, 0, 768, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }; //000A493C
uint8 rud_gr[2] = { 0, 0 }; //000A4A08
uint8 rud_strat[2] = { 0, 0 }; //000A4A0A
int Joy1used = 0;           //000A4A0C
int Joy2used = 0;           //000A4A0C
int fraction = 0;           //000A4A14
int x1ok = 0;               //000A4A30
int y1ok = 0;               //000A4A34
int x2ok = 0;               //000A4A38
int y2ok = 0;               //000A4A3C
int bitaccept = 0;          //000A4A40
uint8 *frontendspeechptr = NULL;//000A4A44
int frontendspeechhandle = -1;  //000A4A48
int holdmusic = 0;          //000A4A4C
int drivertype = -1;        //000A4A50
uint8 unmangleinbuf[1024];  //00149EF0
uint8 *musicbuffer;         //0014EBF8
int lastvolume[16];         //001603F8
int lastpitch[16];          //00160438
int lastpan[16];            //00160478
int net_time[16];           //001604B8
int joyvalue[8];            //001604F8
tJoyPos rud_Joy_pos;        //00160518
int rud_turn[2];            //00160538
int rud_swheel[2];          //00160540
int rud_steer[2];           //00160548
uint32 SampleLen[120];      //00160560
uint8 *SamplePtr[120];      //00160750
tSampleHandleCar SampleHandleCar[120]; //00160930
char Sample[120][15];       //00162730
char lang[16][32];          //00162E38
int TrackMap[32];           //00163038
char TextExt[64];           //001630CA
char SampleExt[64];         //0016310A
int Pending[16];            //0016314C
tSamplePending SamplePending[16][20];  //0016318C
int HandleCar[32];          //00163B8C
int HandleSample[32];       //00163C0C
tCarSoundData enginedelay[16]; //00163C8C
int car_to_player[16];    //0016748C
int player_to_car[16];      //001674CC
tSpeechInfo speechinfo[16]; //0016750C
int load_times[16];         //0016760C
tCopyData copy_multiple[512][16]; //0016764C
int unmangleinpoff;         //0016F64C
uint8 *unmangledst;         //0016F650
int unmangleoverflow;       //0016F654
FILE *unmanglefile;         //0016F658
int unmanglebufpos;         //0016F65C
volatile int s7;            //0016F660
int network_timeout;        //0016F668
void *MT32Data;             //0016F66C
void *FMDrums;              //0016F670
void *FMInstruments;        //0016F674
int network_sync_error;     //0016F678
int ticks_received;         //0016F680
int network_limit;          //0016F684
int MIDIHandle;             //0016F68C
int DIGIHandle;             //0016F690
volatile int frames;        //0016F694
char Song[20][15];          //0016F708
uint32 tickhandle;          //0016F834
DPMI_RMI RMI;               //0016F838
tColor *pal_addr;           //0016F86C
int user_inp;               //0016F87C
int nummusictracks;         //0016F8A8
int winchampsong;           //0016F8AC
int winsong;                //0016F8B0
int delaywritex;            //0016F8B4
int delayreadx;             //0016F8B8
int leaderboardsong;        //0016F8BC
int optionssong;            //0016F8C0
int titlesong;              //0016F8C4
int delaywrite;             //0016F8C8
int delayread;              //0016F8CC
int numsamples;             //0016F8D0
int cheatsample;            //0016F8D4
int languages;              //0016F8D8
int net_loading;            //0016F8DC
int already_quit;           //0016F8E0
int network_error;          //0017C97C

//-------------------------------------------------------------------------------------------------
//000394C0
void realmode(uint8 byRealModeInterrupt)
{
  /*union REGS regs; // [esp+0h] [ebp-34h] BYREF
  struct SREGS sregs; // [esp+1Ch] [ebp-18h] BYREF

  // 49 is DPMI interrupt
  // 768 Simulate Real Mode Interrupt
  // bx is being set to the real mode interrupt to simulate
  // all calls to this function set it to 0x10
  //   to simulate a BIOS video interrupt in real mode
  // __DS__ represents the current value of the DS (Data Segment)
  //   register in your program's segment context
  // By setting sregs.es = __DS__; and regs.x.edi = (unsigned int)&RMI;,
  //   we are telling the DPMI host that the RMI structure is located
  //   at offset &RMI in the segment __DS__
  memset(&sregs, 0, sizeof(sregs));
  regs.w.bx = byRealModeInterrupt;
  regs.w.ax = 768;
  regs.w.cx = 0;
  sregs.es = __DS__;
  regs.x.edi = (unsigned int)&RMI;
  int386x(49, &regs, &regs, &sregs);*/
}

//-------------------------------------------------------------------------------------------------
//00039520
bool loadDOS(const char *szFilename, void **out_buffer)
{
  FILE *file = ROLLERfopen(szFilename, "rb");
  if (!file) {
    *out_buffer = NULL;
    return false;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long iFileSize = ftell(file);
  rewind(file);

  if (iFileSize <= 0) {
    fclose(file);
    *out_buffer = NULL;
    return false;
  }

  // Allocate memory
  void *pBuffer = malloc(iFileSize);
  if (!pBuffer) {
    fclose(file);
    *out_buffer = NULL;
    return false;
  }

  // Read file into buffer
  int iReadBytes = (int)fread(pBuffer, 1, iFileSize, file);
  fclose(file);

  if (iReadBytes != iFileSize) {
    free(pBuffer);
    *out_buffer = NULL;
    return false;
  }

  *out_buffer = pBuffer;
  return true;
  /*
  union REGS regs;
  struct SREGS sregs;

  int handle = open_(szFilename, 0x200); // read-only
  if (handle == -1) {
    *out_segment = (void *)-1;
    return false;
  }

  long file_size = filelength_(handle);
  if (file_size <= 0) {
    close_(handle);
    *out_segment = (void *)-1;
    return false;
  }

  // Allocate a DOS memory block using int 31h AX=0100h
  unsigned short paragraphs = (file_size + 15) / 16; // round up to paragraphs
  memset(&sregs, 0, sizeof(sregs));
  regs.w.ax = 0x0100;
  regs.w.bx = paragraphs;
  int386x(0x31, &regs, &regs, &sregs);

  if (regs.x.cflag || regs.w.ax == 0) {
    close_(handle);
    *out_segment = (void *)-1;
    return false;
  }

  unsigned short seg = regs.w.ax;
  *out_segment = (void *)(uintptr_t)seg;

  // Convert segment:offset to linear address and read into it
  void *dst = (void *)((uintptr_t)seg << 4);
  read_(handle, dst, (unsigned int)file_size);
  close_(handle);

  return true;*/
}

//-------------------------------------------------------------------------------------------------
//000395D0
int claimDOS(int iSizeParagraphs, uint32 *uiSelectorOut)
{
  //int iRequestedParagraphs; // ecx
  //uint16 ax; // dx
  //union REGS regs; // [esp+0h] [ebp-34h] BYREF
  //struct SREGS sregs; // [esp+1Ch] [ebp-18h] BYREF
  //
  //// Calculate adjusted size in paragraphs
  //iRequestedParagraphs = ((iSizeParagraphs - (__CFSHL__(iSizeParagraphs >> 31, 4) + 16 * (iSizeParagraphs >> 31))) >> 4)
  //  + 1;
  //memset(&sregs, 0, sizeof(sregs));
  //regs.w.bx = iRequestedParagraphs;
  //regs.w.ax = 0x100;                            // AX = 0x100 (Allocate DOS Memory Block)
  //int386x(0x31, &regs, &regs, &sregs);
  //
  //// // Check if allocation failed (carry flag set)
  //if (regs.x.cflag) {
    *uiSelectorOut = -1;                        // Return error indicator
    return 0;                                   // Failed allocation
  //} else {
  //  ax = regs.w.ax;
  //  *uiSelectorOut = regs.w.dx;                 // Store real-mode segment
  //  return 16 * ax;                             // Return linear address (real-mode segment * 16)
  //}
}

//-------------------------------------------------------------------------------------------------
//00039650
void releaseDOS(uint16 nSegment)
{
  //union REGS regs; // [esp+0h] [ebp-34h] BYREF
  //struct SREGS sregs; // [esp+1Ch] [ebp-18h] BYREF
  //
  //memset(&sregs, 0, sizeof(sregs));
  //regs.w.dx = nSegment;
  //regs.w.ax = 0x101;                            // EAX = 0x101 (Function: Free DOS Memory Block)
  //int386x(0x31, &regs, &regs, &sregs);
}

//-------------------------------------------------------------------------------------------------
//00039690
bool setpal(const char *szFilename)
{
  void *pFileData = NULL;

  // Free old palette memory, if it was set
  if ((int64)pal_selector >= 0 && pal_addr) {
    free(pal_addr);
    pal_addr = NULL;
    pal_selector = (void *)-1;
  }

  g_bPaletteSet = false; //added by ROLLER
  /*if ( (int)pal_selector >= 0 )
  {
    v4 = (unsigned __int16)pal_selector;
    memset(&sregs, 0, sizeof(sregs));
    regs.w.dx = v4;
    regs.w.ax = 257;
    int386x(0x31, &regs, &regs, &sregs);
    pal_selector = (void *)-1;
  }*/

  // Load palette file into memory
  if (!loadDOS(szFilename, &pFileData)) {
    fprintf(stderr, "Failed to load palette file: %s\n", szFilename);
    return false;
  }

  // Assign palette globals
  pal_addr = pFileData;
  pal_selector = pFileData; // For compatibility

  uint8 *pRaw = (uint8 *)pFileData;

  // Copy RGB triplets into the structured palette array
  for (int i = 0; i < 256; ++i) {
    palette[i].byR = pRaw[i * 3 + 0];
    palette[i].byG = pRaw[i * 3 + 1];
    palette[i].byB = pRaw[i * 3 + 2];
  }

  // If cheat mode flag is set, apply grayscale filter
  if (cheat_mode & CHEAT_MODE_GRAYSCALE) {
    for (int i = 0; i < 256; ++i) {
      uint8 byAvg = (palette[i].byR + palette[i].byB + palette[i].byG) / 3;
      palette[i].byR = byAvg;
      palette[i].byB = byAvg;
      palette[i].byG = byAvg;
    }
  }

  palette_brightness = 0;
  return true;
}

//-------------------------------------------------------------------------------------------------
//000397B0
void blankpal()
{
  if (!pal_addr)
    return;

  memset(pal_addr, 0, sizeof(tColor) * 256);

  /*
  unsigned int uiLinearAddress; // ecx
  unsigned __int16 unDx; // si
  union REGS regs; // [esp+0h] [ebp-64h] BYREF
  REGS regs2; // [esp+1Ch] [ebp-48h] BYREF
  struct SREGS sregs; // [esp+38h] [ebp-2Ch] BYREF
  SREGS sregs2; // [esp+44h] [ebp-20h] BYREF

  memset(&sregs, 0, sizeof(sregs));
  regs.w.ax = 256;
  regs.w.bx = 48;

  // allocate 48 LDT selectors
  // * If successful, AX contains the first selector allocated.
  // * If failed, the carry flag is set and AX contains an error code.
  int386x(49, &regs, &regs, &sregs);

  // Multiplying by 16 (* 16) converts a segment
  // selector value to a linear address in real
  // mode (segment:offset addressing), because in
  // real mode, the physical address is
  // segment * 16 + offset.
  uiLinearAddress = 16 * regs.w.ax;
  unDx = regs.w.dx;
  memset((void *)uiLinearAddress, 0, 0x300u);
  RMI.ecx = 256;
  RMI.eax = 4114;
  RMI.ebx = 0;
  RMI.edx = 0;

  // Shifting the address right by 4 (>> 4) converts
  // the linear address back to a segment value
  // (since segment = address / 16).
  RMI.es = uiLinearAddress >> 4;
  realmode(0x10u);
  memset(&sregs2, 0, sizeof(sregs2));
  regs2.w.dx = unDx;
  regs2.w.ax = 257;
  int386x(49, &regs2, &regs2, &sregs2);
  */
  palette_brightness = 0;
}

//-------------------------------------------------------------------------------------------------
//00039890
void resetpal()
{
  /*
  RMI.eax = 4114;
  RMI.ebx = 0;
  RMI.ecx = 256;
  RMI.es = (unsigned int)pal_addr >> 4;
  RMI.edx = 0;
  realmode(0x10u);
  */

  // Clean up heap-allocated palette memory
  if (pal_addr) {
    free(pal_addr);
    pal_addr = NULL;
  }
}

//-------------------------------------------------------------------------------------------------
//000398F0
void Initialise_SOS()
{
  //sosTIMERInitSystem(0xFF00, 0);                // 0xFF00 maybe a special case for a slow rate timer?
  if (MusicCD)
    SetAudioVolume(MusicVolume);
  if (MusicCard) {
    if (MusicCard == 40970)
      initgus();
    printf("Initialising MIDI device %4X...", MusicCard);
    fflush(stdout);
    memset(TrackMap, 255, 0x20 * sizeof(unsigned int));
    //sosMIDIInitSystem(0, 0);
    //MIDIInitDriver_variable_1 = 0;
    //MIDIHardware = MusicPort;
    //MIDIInitDriver_variable_2 = 0;
    //if (sosMIDIInitDriver(
    //  MusicCard,
    //  0,
    //  (unsigned int)&MIDIHardware,
    //  __DS__,
    //  (int)&MIDIInitDriver,
    //  (unsigned __int16)__DS__,
    //  (unsigned int)&MIDIHandle,
    //  (unsigned __int16)__DS__)) {
    //  MusicCard = 0;
    //  sosMIDIUnInitSystem();
    //  printf("Failed\n");
    //} else {
    //  printf("OK\n");
    //  devicespecificinit();
    //  sosMIDISetMasterVolume(MusicVolume);
    //}
  }
  if (SoundCard) {
    printf("Initialising DIGI device %4X...", SoundCard);
    fflush(stdout);
    //sosDIGIInitSystem(0, 0);
    printf("OK\n");
  }
  select8bitdriver();
  ticks = 0;
  frames = 0;

  // start 36Hz timer that calls tickhandler
  claim_ticktimer(36u);
}

//-------------------------------------------------------------------------------------------------
//00039A40
void updatejoy()
{
  int iX1Scaled; // eax
  int iX1Clamped2; // eax
  int iX1Clamped; // eax
  int iY1Scaled; // eax
  int iY1Clamped2; // eax
  int iY1Clamped; // eax
  int iX2Scaled; // eax
  int iX2Clamped2; // eax
  int iX2Clamped; // eax
  int iY2Scaled; // eax
  int iY2Clamped2; // eax
  int iY2Clamped; // eax

  memset(joyvalue, 0, sizeof(joyvalue));
  if (Joy1used || Joy2used)
    ReadJoys(&rud_Joy_pos);
  if (Joy1used) {
    keys[128] = rud_Joy_pos.iJ1Button1;
    keys[129] = rud_Joy_pos.iJ1Button2;
    iX1Scaled = ((2 * rud_Joy_pos.iJ1XAxis - JAXmax - JAXmin) << 10) / (JAXmax - JAXmin);
    //apply 100-unit deadzone
    if (iX1Scaled >= 0) {
      iX1Clamped = iX1Scaled - 100;
      if (iX1Clamped < 0)
        iX1Clamped = 0;
      joyvalue[1] = iX1Clamped;
    } else {
      iX1Clamped2 = iX1Scaled + 100;
      if (iX1Clamped2 > 0)
        iX1Clamped2 = 0;
      joyvalue[0] = -iX1Clamped2;
    }
    iY1Scaled = ((2 * rud_Joy_pos.iJ1YAxis - JAYmax - JAYmin) << 10) / (JAYmax - JAYmin);
    //apply 100-unit deadzone
    if (iY1Scaled >= 0) {
      iY1Clamped = iY1Scaled - 100;
      if (iY1Clamped < 0)
        iY1Clamped = 0;
      joyvalue[3] = iY1Clamped;
    } else {
      iY1Clamped2 = iY1Scaled + 100;
      if (iY1Clamped2 > 0)
        iY1Clamped2 = 0;
      joyvalue[2] = -iY1Clamped2;
    }
  }
  if (Joy2used) {
    keys[130] = rud_Joy_pos.iJ2Button1;
    keys[131] = rud_Joy_pos.iJ2Button2;
    iX2Scaled = ((2 * rud_Joy_pos.iJ2XAxis - JBXmax - JBXmin) << 10) / (JBXmax - JBXmin);
    //apply 100-unit deadzone
    if (iX2Scaled >= 0) {
      iX2Clamped = iX2Scaled - 100;
      if (iX2Clamped < 0)
        iX2Clamped = 0;
      joyvalue[5] = iX2Clamped;
    } else {
      iX2Clamped2 = iX2Scaled + 100;
      if (iX2Clamped2 > 0)
        iX2Clamped2 = 0;
      joyvalue[4] = -iX2Clamped2;
    }
    iY2Scaled = ((2 * rud_Joy_pos.iJ2YAxis - JBYmax - JBYmin) << 10) / (JBYmax - JBYmin);
    //apply 100-unit deadzone
    if (iY2Scaled >= 0) {
      iY2Clamped = iY2Scaled - 100;
      if (iY2Clamped < 0)
        iY2Clamped = 0;
      joyvalue[7] = iY2Clamped;
    } else {
      iY2Clamped2 = iY2Scaled + 100;
      if (iY2Clamped2 > 0)
        iY2Clamped2 = 0;
      joyvalue[6] = -iY2Clamped2;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00039C20
void readuserdata(int iPlayer)
{
  int iHalfKeyIndex; // esi
  int iPlayerIdx; // eax
  tCarEngine *pEngine; // ebx
  int iKeyIndex; // esi
  unsigned int uiAccelKey; // eax
  unsigned int uiBrakeKey; // eax
  uint8 byLeftKey; // bl
  int iTurnRate; // ebp
  int iTurnRate_1; // edx
  int iPlayerIdx2; // eax
  int iSteering; // ebx
  int iMaxSteering; // edi
  int iTurnRate_2; // edx
  int iSteering_1; // ebx
  int iMinSteering_1; // edx
  int iSteering_2; // eax
  int iSteering_4; // edx
  int iSteering_3; // edx
  unsigned int iJoyLeft; // eax
  unsigned int iJoyRight; // ebx
  int iLeftEffect; // eax
  int iMinSteering; // eax
  int16 nButtonFlags_1; // bx
  int16 nButtonFlags; // ax
  unsigned int uiGearUpKey; // edx
  int iGearChange; // eax
  unsigned int uiGearDownKey; // edx
  int iStrategyFlags; // eax
  int iNode; // edx
  int iMessageIdx; // eax
  char iAccelState; // [esp+0h] [ebp-1Ch]

  // Skip processing during countdown phase
  if (countdown >= 140) {
    user_inp = 0;
    return;
  }

  // Calculate key indices for this player (6 keys per player)
  iHalfKeyIndex = 3 * iPlayer;
  iPlayerIdx = iPlayer;
  pEngine = p_eng[iPlayerIdx];
  iKeyIndex = 2 * iHalfKeyIndex;

  // Early exit if no engine
  if (!pEngine) {
    user_inp = 0;
    return;
  }

  // calculate maximum steering value
  rud_steer[iPlayerIdx] = pEngine->iSteeringSensitivity << 8;
  iAccelState = 0;
  // // Update steering state
  rud_swheel[iPlayerIdx] = GET_SLOWORD(last_inp[iPlayerIdx]);

  // Process acceleration and brake inputs
  uiAccelKey = userkey[iKeyIndex + 2];
  if (uiAccelKey > 0x83) {
    //if ((unsigned int)musicbuffer_variable_1[uiAccelKey] <= 0xC8)// joyvalue[uiAccelKey]
    if ((unsigned int)joyvalue[uiAccelKey- 132] <= 0xC8)// 
      goto LABEL_9;
  } else if (!keys[uiAccelKey]) {
    goto LABEL_9;
  }
  iAccelState = 1;
LABEL_9:
  uiBrakeKey = userkey[iKeyIndex + 3];
  if (uiBrakeKey > 0x83) {
    //if ((unsigned int)musicbuffer_variable_1[uiBrakeKey] <= 0xC8)// joyvalue[uiBrakeKey]
    if ((unsigned int)joyvalue[uiBrakeKey- 132] <= 0xC8)// 
      goto LABEL_14;
  } else if (!keys[uiBrakeKey]) {
    goto LABEL_14;
  }
  iAccelState -= 2;
LABEL_14:

  // process steering input
  byLeftKey = userkey[iKeyIndex];
  if (byLeftKey > 0x83u) {
    // joystick steering processing
    //iJoyLeft = (unsigned int)(80 * musicbuffer_variable_1[byLeftKey]) >> 8;// joyvalue[byLeftKey]
    iJoyLeft = (unsigned int)(80 * joyvalue[byLeftKey - 132]) >> 8;// 
    if (iJoyLeft > 0x102)
      iJoyLeft = 0x102;
    //iJoyRight = (unsigned int)(80 * musicbuffer_variable_1[userkey[iKeyIndex + 1]]) >> 8;// joyvalue[byRightKey]
    iJoyRight = (unsigned int)(80 * joyvalue[userkey[iKeyIndex + 1] - 132]) >> 8;// 
    iLeftEffect = (int)(p_joyk1[iPlayer] * ((int)(iJoyLeft * iJoyLeft * iJoyLeft) >> 8) + p_joyk2[iPlayer] * iJoyLeft) >> 16;
    if (iJoyRight > 0x102)
      iJoyRight = 0x102;
    rud_swheel[iPlayer] = iLeftEffect - ((int)(p_joyk1[iPlayer] * ((int)(iJoyRight * iJoyRight * iJoyRight) >> 8) + p_joyk2[iPlayer] * iJoyRight) >> 16);

    // Clamp steering to valid range
    iMinSteering = -rud_steer[iPlayer];
    if (iMinSteering > rud_swheel[iPlayer])
      rud_swheel[iPlayer] = iMinSteering;
    iPlayerIdx2 = iPlayer;
    iMaxSteering = rud_steer[iPlayer];
    if (rud_swheel[iPlayer] > iMaxSteering)
      goto LABEL_43;
  }
  // Keyboard steering processing
  // process left steering
  else if (keys[byLeftKey]) {
    iTurnRate = rud_turn[iPlayer];
    if (iTurnRate >= 0)
      // apply turn decay
      rud_turn[iPlayer] = 32 * p_eng[iPlayer]->iTurnDecayRate + iTurnRate;
    else
      // calculate new turn rate
      rud_turn[iPlayer] = (int)(abs(rud_swheel[iPlayer]) * ((p_eng[iPlayer]->iMaxTurnRate << 8) - 32 * p_eng[iPlayer]->iTurnDecayRate)) / rud_steer[iPlayer];
    iTurnRate_1 = p_eng[iPlayer]->iMaxTurnRate << 8;
    if (iTurnRate_1 < rud_turn[iPlayer])
      rud_turn[iPlayer] = iTurnRate_1;
    iPlayerIdx2 = iPlayer;

    // Apply turn rate and clamp
    iSteering = rud_turn[iPlayer] + rud_swheel[iPlayer];
    iMaxSteering = rud_steer[iPlayer];
    rud_swheel[iPlayer] = iSteering;
    if (iSteering > iMaxSteering)
      LABEL_43:
    rud_swheel[iPlayerIdx2] = iMaxSteering;
  }
  // process right steering
  else if (keys[userkey[iKeyIndex + 1]]) {
    if (rud_turn[iPlayer] >= 0)
      rud_turn[iPlayer] += 32 * p_eng[iPlayer]->iTurnDecayRate;
    else
      rud_turn[iPlayer] = (int)(abs(rud_swheel[iPlayer]) * ((p_eng[iPlayer]->iMaxTurnRate << 8) - 32 * p_eng[iPlayer]->iTurnDecayRate)) / rud_steer[iPlayer];
    iTurnRate_2 = p_eng[iPlayer]->iMaxTurnRate << 8;
    if (iTurnRate_2 < rud_turn[iPlayer])
      rud_turn[iPlayer] = iTurnRate_2;
    iSteering_1 = rud_swheel[iPlayer] - rud_turn[iPlayer];
    iMinSteering_1 = -rud_steer[iPlayer];
    rud_swheel[iPlayer] = iSteering_1;
    if (iMinSteering_1 > iSteering_1)
      rud_swheel[iPlayer] = iMinSteering_1;
  } else {
    // center steering when no input
    iSteering_2 = rud_swheel[iPlayer];
    rud_turn[iPlayer] = -1;
    if (iSteering_2 <= 0) {
      iSteering_3 = (p_eng[iPlayer]->iWheelCenteringRate << 8) + rud_swheel[iPlayer];
      rud_swheel[iPlayer] = iSteering_3;
      if (iSteering_3 > 0)
        rud_swheel[iPlayer] = 0;
    } else {
      iSteering_4 = rud_swheel[iPlayer] - (p_eng[iPlayer]->iWheelCenteringRate << 8);
      rud_swheel[iPlayer] = iSteering_4;
      if (iSteering_4 < 0)
        rud_swheel[iPlayer] = 0;
    }
  }

  // Update steering state
  SET_LOWORD(user_inp, rud_swheel[iPlayer]);
  nButtonFlags_1 = iAccelState > 0;
  if (iAccelState < 0)
    nButtonFlags_1 |= 2;
  nButtonFlags &= 0x00FF;
  //  LOBYTE(nButtonFlags_1) = nButtonFlags_1 | 2;
  //HIBYTE(nButtonFlags) = 0;

  // Process special buttons
  if (iPlayer) {
    if (userkey[13] > 0x83u)                  // USERKEY_P2CHEAT
    {
      //if ((unsigned int)musicbuffer_variable_1[userkey[13]] <= 0xC8)// joyvalue[userkey[USERKEY_P2CHEAT]]
      if ((unsigned int)joyvalue[userkey[USERKEY_P2CHEAT]- 132] <= 0xC8)// 
        goto LABEL_57;
    } else if (!keys[userkey[13]])              // USERKEY_P2CHEAT
    {
      goto LABEL_57;
    }
  } else if (userkey[12] > 0x83u)               // USERKEY_P2DOWNGEAR
  {
    //if ((unsigned int)musicbuffer_variable_1[userkey[12]] <= 0xC8)// joyvalue[userkey[USERKEY_P2DOWNGEAR]]
    if ((unsigned int)joyvalue[userkey[USERKEY_P2DOWNGEAR]- 132] <= 0xC8)// 
      goto LABEL_57;
  } else if (!keys[userkey[12]])                // USERKEY_P2DOWNGEAR
  {
    goto LABEL_57;
  }
  //TODO look a this again
  //LOBYTE(nButtonFlags) = nButtonFlags_1 | 0x20;   // special action flag
  //nButtonFlags_1 = nButtonFlags;
  nButtonFlags_1 |= 0x20;
LABEL_57:
  // process gear shifting
  uiGearUpKey = userkey[iKeyIndex + 4];
  iGearChange = 0;
  if (uiGearUpKey > 0x83) {
    //if ((unsigned int)musicbuffer_variable_1[uiGearUpKey] <= 0xC8)// joyvalue[uiGearUpKey]
    if ((unsigned int)joyvalue[uiGearUpKey- 132] <= 0xC8)// 
      goto LABEL_62;
  } else if (!keys[uiGearUpKey]) {
    goto LABEL_62;
  }
  iGearChange = 1;
LABEL_62:
  uiGearDownKey = userkey[iKeyIndex + 5];
  if (uiGearDownKey > 0x83) {
    //if ((unsigned int)musicbuffer_variable_1[uiGearDownKey] <= 0xC8)// joyvalue[uiGearDownKey]
    if ((unsigned int)joyvalue[uiGearDownKey- 132] <= 0xC8)// 
      goto LABEL_67;
  } else if (!keys[uiGearDownKey]) {
    goto LABEL_67;
  }
  iGearChange = -1;
LABEL_67:
  if (!iGearChange)
    rud_gr[iPlayer] = 0;
  if (rud_gr[iPlayer])
    iGearChange = 0;

  // Update gear state
  if (iGearChange > 0) {
    //LOBYTE(nButtonFlags_1) = nButtonFlags_1 | 4;    // gear up flag
    nButtonFlags_1 |= 4;    // gear up flag
    rud_gr[iPlayer] = -1;                       // gear changed
  }
  if (iGearChange < 0) {
    //LOBYTE(nButtonFlags_1) = nButtonFlags_1 | 8;    // gear down flag
    nButtonFlags_1 |= 8;    // gear down flag
    rud_gr[iPlayer] = -1;                       // gear changed
  }

  // Process strategy buttons (F1-F4)
  if (lastsample >= 0)
    goto LABEL_107;
  iStrategyFlags = 0;
  if (keys[63])                               // F1
  {
    iStrategyFlags = 0x40;
  } else if (keys[64])                          // F2
  {
    iStrategyFlags = 0x80;
  } else if (keys[65])                          // F3
  {
    iStrategyFlags = 0x100;
  } else if (keys[66])                          // F4
  {
    iStrategyFlags = 0x200;
  }
  if (!iStrategyFlags)
    rud_strat[iPlayer] = 0;
  if (rud_strat[iPlayer])
    iStrategyFlags = 0;
  if (!iStrategyFlags)
    goto LABEL_107;

  // Handle network messages for strategy buttons
  if (network_on) {
    message_node = network_mes_mode;
    // Validate target node for messaging
    if (network_mes_mode >= 0) {
      if (human_control[message_node])
        message_node = car_to_player[message_node];
      else
        message_node = -2;                      // invalid target
    }

    // Send message if valid target and player hasn't finished
    if (message_node > -2 && !finished_car[player1_car]) {

      // Map strategy flags to message types
      if ((iStrategyFlags & 0x40) != 0) {
        iNode = message_node;
        iMessageIdx = 0;
      } else if ((iStrategyFlags & 0x80u) == 0) {
        if ((iStrategyFlags & 0x100) != 0) {
          iNode = message_node;
          iMessageIdx = 2;
        } else {
          if ((iStrategyFlags & 0x200) == 0) {
          LABEL_105:
            iStrategyFlags &= 0xFFFF0000;
            //LOWORD(iStrategyFlags) = 0;
            goto LABEL_106;
          }
          iNode = message_node;
          iMessageIdx = 3;
        }
      } else {
        iNode = message_node;
        iMessageIdx = 1;
      }
      send_mes(iMessageIdx, iNode);
      goto LABEL_105;
    }
  }
LABEL_106:
  nButtonFlags_1 |= iStrategyFlags;               // add strategy buttons to buttons state
  rud_strat[iPlayer] = -1;                      // mark strategy button pressed
LABEL_107:
  // Handle quit request
  if (I_Want_Out && !already_quit) {
    I_Want_Out = 0;
    already_quit = -1;
    if (wConsoleNode == master)
      //HIBYTE(nButtonFlags_1) |= 8u;               // master quit flag
      nButtonFlags_1 |= 0x0800u;               // master quit flag
    else
      //HIBYTE(nButtonFlags_1) |= 4u;               // client quit flag
      nButtonFlags_1 |= 0x0400u;               // client quit flag
  }

  // store final input state
  SET_HIWORD(user_inp, nButtonFlags_1);
}

//-------------------------------------------------------------------------------------------------
//0003A270
void tickhandler()
{
  if (network_on && syncleft) {
    do_sync_stuff();
    return;
  }

  // Initialize network state
  if (network_on) {
    net_loading = 0;
  } else {
    broadcast_mode = 0;
  }
  frames++;

  // Replay speed control
  if (replaytype == 2) {
    if (forwarding) {
      if (slowing) {
        replayspeed = (4 * replayspeed) / 5;
        if (replayspeed < 256) {
          ROldStatus();
          slowing = 0;
        }
      } else {
        replayspeed = (17 * replayspeed) / 16;
        if (replayspeed > REPLAY_SPEED_MAX) {
          replayspeed = REPLAY_SPEED_MAX;
        }
      }
    }

    if (rewinding) {
      if (slowing) {
        replayspeed = (4 * replayspeed) / 5;
        if (replayspeed > -256) {
          ROldStatus();
          slowing = 0;
        }
      } else {
        replayspeed = (17 * replayspeed) / 16;
        if (replayspeed < REPLAY_SPEED_MIN) {
          replayspeed = REPLAY_SPEED_MIN;
        }
      }
    }

    // Calculate tick advancement
    int iTotal = replayspeed + fraction;
    int iTickAdvance = iTotal / 256;
    fraction = iTotal % 256;

    // Update and clamp tick position
    ticks += iTickAdvance;
    if (ticks < 0) ticks = 0;
    if (ticks >= replayframes) ticks = replayframes - 1;

    // Handle boundary conditions
    if (ticks == 0 && fraction < 0) fraction = 0;
    if (ticks == replayframes - 1 && fraction > 0) fraction = 0;
  }
  // Handle normal game mode
  else {
    ticks++;
    if (!paused && !frontend_on) {
      updatejoy();
    }
  }

  // Network message processing
  if (tick_on) {
    if (network_on && (replaytype == 2 || game_type > 2)) {
      CheckNewNodes();
      SendAMessage();
      BroadcastNews();
    } else if (replaytype != 2 && game_type < 3) {
      if (frontend_on) {
        if (network_on) {
          CheckNewNodes();
          SendAMessage();
          BroadcastNews();
        }
      } else {
        if (network_on && winner_mode) {
          CheckNewNodes();
          SendAMessage();
          BroadcastNews();
        }
        // Single player mode
        if (!network_on || winner_mode) {
          if (start_race && !paused) {

            // Process player 1 input
            readuserdata(0);
            last_inp[0] = user_inp;
            copy_multiple[writeptr][player1_car].uiFullData = user_inp;

            // Process player 2 input if present
            if (player_type == 2) {
              readuserdata(1);
              last_inp[1] = user_inp;
              copy_multiple[writeptr][player2_car].uiFullData = user_inp;
            }

            // Update write pointer
            writeptr = (writeptr + 1) % REPLAY_BUFFER_SIZE;

            // Process sound if enabled
            if (soundon && !paused) {
              if (delayread < delaywrite) {
                delayreadx = delayread % DELAY_BUFFER_SIZE;

                if (player_type == 2) {
                  // Process both players
                  tEngineSoundData *pSound = &enginedelay[ViewType[0]].engineSoundData[delayreadx];
                  if (pSound->iEngineVol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
                  if (pSound->iEngine2Vol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
                  if (pSound->iSkid1Vol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
                  pSound = &enginedelay[ViewType[1]].engineSoundData[delayreadx];
                  if (pSound->iEngineVol >= 0)
                    loopsample(ViewType[1], SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
                  if (pSound->iEngine2Vol >= 0)
                    loopsample(ViewType[1], SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
                  if (pSound->iSkid1Vol >= 0)
                    loopsample(ViewType[1], SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
                } else if (allengines) {
                  // Process all cars
                  for (int i = 0; i < numcars; i++) {
                    tEngineSoundData *pSound = &enginedelay[i].engineSoundData[delayreadx];
                    if (pSound->iEngineVol >= 0)
                      loopsample(i, SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
                    if (pSound->iEngine2Vol >= 0)
                      loopsample(i, SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
                    if (pSound->iSkid1Vol >= 0)
                      loopsample(i, SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
                  }
                } else {
                  // Process only player 1
                  tEngineSoundData *pSound = &enginedelay[ViewType[0]].engineSoundData[delayreadx];
                  if (pSound->iEngineVol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
                  if (pSound->iEngine2Vol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
                  if (pSound->iSkid1Vol >= 0)
                    loopsample(ViewType[0], SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
                }

                // Advance buffer
                if (delaywrite - delayread >= 6) {
                  delayread++;
                }
              }
            }
          }
        } else {
          // Handle multiplayer mode
          ticks_received = 0;

          // Handle master node
          if (wConsoleNode == master) {
            // Check player timeouts
            for (int i = 0; i < network_on; i++) {
              if (paused) {
                net_time[i] = frames;
              }
              if (i != master && net_players[i] &&
                  (frames > net_time[i] + network_limit) &&
                  !send_finished) {
                network_error = 222;
              }
            }

            // Handle network errors
            if (network_error && network_error != 666) {
              send_net_error();
            }

            // Full player count reached
            if (network_on - 1 == start_multiple) {
              // Copy previous frame data
              memcpy(copy_multiple[writeptr],
                     copy_multiple[(writeptr - 1 + REPLAY_BUFFER_SIZE) % REPLAY_BUFFER_SIZE],
                     sizeof(tCopyData) * MAX_CARS);

              // Reset connection flags
              for (int i = 0; i < numcars; i++) {
                uint16 unFlags = copy_multiple[writeptr][i].data.unFlags;

                if (copy_multiple[writeptr][i].uiFullData & 0x4000000) {
                  unFlags ^= FLAG_DISCONNECT;
                }
                if (copy_multiple[writeptr][i].uiFullData & 0x8000000) {
                  unFlags = (unFlags ^ FLAG_MASTER_CHANGE) | FLAG_DISCONNECT;
                }
                copy_multiple[writeptr][i].data.unFlags = unFlags;
              }

              // Process player input
              readuserdata(0);
              copy_multiple[writeptr][player1_car].uiFullData = user_inp;
              last_inp[0] = user_inp;

              receive_all_singles();
            }

            // Countdown handling
            if (countdown >= 0) {
              active_nodes = 0;
              for (int i = 0; i < network_on; i++) {
                if (player_ready[i]) active_nodes++;
                if (player_ready[i] == 666 && net_players[i]) {
                  copy_multiple[writeptr][player_to_car[i]].data.unFlags |= FLAG_DISCONNECT;
                  net_players[i] = 0;
                }
              }
            }

            // Send data when all nodes are ready
            if (active_nodes == network_on) {
              net_loading = 0;
              if (network_on - 1 != start_multiple || !paused) {
                if (start_race && !paused && send_finished && numcars > 0) {
                  for (int i = 0; i < numcars; i++) {
                    copy_multiple[writeptr][i].data.unFlags |= FLAG_FINISHED;
                  }
                }
                send_multiple();
                
                // Handle master transition
                if (network_on - 1 == start_multiple) {
                  for (int i = 0; i < numcars; i++) {
                    if (copy_multiple[writeptr][i].data.unFlags & FLAG_DISCONNECT) {
                      net_players[car_to_player[i]] = 0;
                      network_timeout = frames;
                    }
                  }

                  if (copy_multiple[writeptr][player1_car].data.unFlags & FLAG_MASTER_CHANGE) {
                    // Reset network state
                    read_check = -1;
                    write_check = -1;
                    memset(player_checks, -1, sizeof(player_checks));

                    // Find new master
                    net_players[master] = 0;
                    while (!net_players[master] && master < MAX_PLAYERS) {
                      master++;
                    }

                    // Reset timers
                    for (int i = 0; i < network_on; i++) {
                      net_time[i] = frames;
                    }

                    // Update flags
                    uint16 unFlags = copy_multiple[writeptr][player1_car].data.unFlags;
                    unFlags ^= FLAG_DISCONNECT;
                    copy_multiple[(writeptr + 1) % REPLAY_BUFFER_SIZE][player1_car].data.unFlags = unFlags;

                    network_timeout = frames;
                  }

                  writeptr = (writeptr + 1) % REPLAY_BUFFER_SIZE;
                  ticks_received++;
                }
              }
            }
          } else if (wConsoleNode > master && net_players[wConsoleNode]) {
            if (paused) {
              network_timeout = frames;
            }
            if (frames > network_timeout + network_limit && human_finishers < players) {
              network_error = 123;
            }

            readuserdata(0);
            send_single(user_inp);
            last_inp[0] = user_inp;

            receive_multiple();

            // Handle master change
            if (copy_multiple[(writeptr - 1 + REPLAY_BUFFER_SIZE) % REPLAY_BUFFER_SIZE][player_to_car[master]].uiFullData & 0x8000000) {
              read_check = -1;
              write_check = -1;
              memset(player_checks, -1, sizeof(player_checks));

              net_players[master] = 0;
              while (!net_players[master] && master < MAX_PLAYERS) {
                master++;
              }

              active_nodes = network_on;
              start_multiple = network_on - 1;

              for (int i = 0; i < network_on; i++) {
                net_time[i] = frames;
              }
              network_timeout = frames;
            }

            // Check for disconnections
            for (int i = 0; i < numcars; i++) {
              uint32 uiData = copy_multiple[(writeptr - 1 + REPLAY_BUFFER_SIZE) % REPLAY_BUFFER_SIZE][i].uiFullData;

              if (uiData & 0x10000000) {
                network_error = 0;  // Reset error on valid data
              }
              if (uiData & 0x4000000) {
                net_players[car_to_player[i]] = 0;
                network_timeout = frames;
                memset(player_checks, -1, sizeof(player_checks));
                read_check = 0;
                write_check = 0;
              }
            }
          } else {
            // Handle disconnected state
            read_check = -1;
            write_check = -1;

            readuserdata(0);
            last_inp[0] = user_inp;
            copy_multiple[writeptr][player1_car].uiFullData = user_inp;

            // Set disconnect flag
            uint16 unFlags = copy_multiple[writeptr][player1_car].data.unFlags;
            unFlags ;
            copy_multiple[writeptr][player1_car].data.unFlags |= FLAG_DISCONNECT;

            writeptr = (writeptr + 1) % REPLAY_BUFFER_SIZE;
          }

          // Process sound
          if (soundon && !paused) {
            if (delayread >= delaywrite) return;

            delayreadx = delayread % DELAY_BUFFER_SIZE;

            if (allengines) {
              for (int i = 0; i < numcars; i++) {
                tEngineSoundData *pSound = &enginedelay[i].engineSoundData[delayreadx];
                if (pSound->iEngineVol >= 0)
                  loopsample(i, SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
                if (pSound->iEngine2Vol >= 0)
                  loopsample(i, SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
                if (pSound->iSkid1Vol >= 0)
                  loopsample(i, SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
              }
            } else {
              tEngineSoundData *pSound = &enginedelay[ViewType[0]].engineSoundData[delayreadx];
              if (pSound->iEngineVol >= 0)
                loopsample(ViewType[0], SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
              if (pSound->iEngine2Vol >= 0)
                loopsample(ViewType[0], SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
              if (pSound->iSkid1Vol >= 0)
                loopsample(ViewType[0], SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
            }

            // Advance by ticks received
            delayread += ticks_received;
          }
        }
      }
    }
  }

  // Replay sound processing
  if (replaytype == 2 && replayspeed == REPLAY_NORMAL_SPEED) {
    if (delayread < delaywrite) {
      int delayreadx = delayread % DELAY_BUFFER_SIZE;

      for (int i = 0; i < numcars; i++) {
        tEngineSoundData *pSound = &enginedelay[i].engineSoundData[delayreadx];
        if (pSound->iEngineVol >= 0)
          loopsample(i, SOUND_SAMPLE_ENGINE, pSound->iEngineVol, pSound->iEnginePitch, pSound->iPan);
        if (pSound->iEngine2Vol >= 0)
          loopsample(i, SOUND_SAMPLE_ENGINE2, pSound->iEngine2Vol, pSound->iEngine2Pitch, pSound->iPan);
        if (pSound->iSkid1Vol >= 0)
          loopsample(i, SOUND_SAMPLE_SKID1, pSound->iSkid1Vol, pSound->iSkid1Pitch, pSound->iPan);
      }

      if (delaywrite - delayread >= 6) {
        delayread++;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003B0E0
void claim_ticktimer(unsigned int uiRateHz)
{
  /***
  * ADDED BY ROLLER
  ***/
  tickhandle = ROLLERAddTimer(uiRateHz, SDLTickTimerCallback, NULL); //may as well re-use tickhandle, it is also a uint32
  /***
  * END ROLLER CODE
  ***/

  /*
  if (sosTIMERRegisterEvent(
    uiRateHz,
    a2,
    (int)tickhandler,
    __CS__,
    (unsigned int)&tickhandle,
    (unsigned __int16)__DS__)) {
    printf("Unable to set timer\n");
    doexit();
  }*/
}

//-------------------------------------------------------------------------------------------------
//0003B120
void release_ticktimer()
{
  /***
  * ADDED BY ROLLER
  ***/
  ROLLERRemoveTimer(tickhandle); //may as well re-use tickhandle, it is also a uint32
  /***
  * END ROLLER CODE
  ***/

  //sosTIMERRemoveEvent(tickhandle);
}

//-------------------------------------------------------------------------------------------------
//0003B130
void Uninitialise_SOS()
{
  /***
  * ADDED BY ROLLER
  ***/
  ROLLERRemoveTimer(tickhandle); //may as well re-use tickhandle, it is also a uint32
  /***
  * END ROLLER CODE
  ***/

  //sosTIMERRemoveEvent(tickhandle);
  //sosTIMERRemoveEvent(TimerEventHandle);
  //sosTIMERUnInitSystem(0);
  //if (SoundCard) {
  //  sosDIGIUnInitDriver(DIGIHandle, 1, 1);
  //  sosDIGIUnInitSystem();
  //}
  if (MusicCard) {
    devicespecificuninit();
    //sosMIDIUnInitDriver(MIDIHandle, 1);
    //sosMIDIUnInitSystem();
  }
}

//-------------------------------------------------------------------------------------------------
//0003B1A0
void loadsamples()
{
  int iWinnerSample; // edx
  int iDesignCmp; // ecx
  int iSampleIdx; // ebx
  int iKills2; // edx
  int iKillSample; // eax
  int iWins; // eax
  int iKills; // edx

  iWinnerSample = winner_mode;
  if (winner_mode) {
    if (champ_mode) {
      loadasample(0);
      loadasample(1);
      loadasample(3);
      loadasample(9);
      loadasample(10);
      loadasample(11);
      loadasample(12);
      loadasample(6);
      iWins = total_wins[champorder[0]];
      if (iWins > 0)
        loadasample(iWins + 88);
      iKills = total_kills[champorder[0]];
      if (iKills > 0 && iKills < 17)
        loadasample(iKills + 96);
      if (iKills >= 17)
        loadasample(113);
      iKillSample = 118;
    } else {
      loadasample(0);
      loadasample(1);
      loadasample(3);
      loadasample(9);
      loadasample(10);
      loadasample(11);
      loadasample(12);
      loadasample(114);
      loadasample(115);
      loadasample(116);
      loadasample(117);
      iKills2 = result_kills[result_order[0]];
      if (iKills2 > 0 && iKills2 < 17)
        loadasample(iKills2 + 96);
      if (iKills2 < 17)
        goto LABEL_30;
      iKillSample = 113;
    }
    loadasample(iKillSample);
    goto LABEL_30;
  }
  if (numsamples > 0) {
    iDesignCmp = -71;
    iSampleIdx = 0;
    do {
      if (!winner_mode) {
        if (iWinnerSample >= 71 && (iWinnerSample <= 78 || iWinnerSample >= 89)) {
          if (Car[ViewType[0]].byCarDesignIdx != iDesignCmp || SamplePtr[iSampleIdx])
            goto LABEL_13;
        LABEL_12:
          loadasample(iWinnerSample);
          goto LABEL_13;
        }
        if (!SamplePtr[iSampleIdx])
          goto LABEL_12;
      }
    LABEL_13:
      ++iDesignCmp;
      ++iWinnerSample;
      ++iSampleIdx;
    } while (iWinnerSample < numsamples);
  }
LABEL_30:
  samplespending = 0;
  writesample = 0;
  readsample = 0;
  lastsample = -10000;
}

//-------------------------------------------------------------------------------------------------
//0003B3A0
void loadfatalsample()
{
  if (!SamplePtr[SOUND_SAMPLE_FATAL]) // 88 - Fatal sample
    loadasample(SOUND_SAMPLE_FATAL);
  if (!SamplePtr[SOUND_SAMPLE_BUTTON]) // 83 - Button sample
    loadasample(SOUND_SAMPLE_BUTTON);
  if (!SamplePtr[SOUND_SAMPLE_START]) // 87 - Start sample
    loadasample(SOUND_SAMPLE_START);
  if (!SamplePtr[SOUND_SAMPLE_CARIN]) // 84 - Car in sample
    loadasample(SOUND_SAMPLE_CARIN);
  if (!SamplePtr[SOUND_SAMPLE_CAROUT]) // 85 - Car out sample
    loadasample(SOUND_SAMPLE_CAROUT);
  if (!SamplePtr[SOUND_SAMPLE_TRACK]) // 86 - Track sample
    loadasample(SOUND_SAMPLE_TRACK);
  samplespending = 0;
  writesample = 0;
  readsample = 0;
  lastsample = -10000;
}

//-------------------------------------------------------------------------------------------------
//0003B440
void freefatalsample()
{
  fre((void**)&SamplePtr[SOUND_SAMPLE_FATAL]); // 88 - Fatal sample
  fre((void**)&SamplePtr[SOUND_SAMPLE_BUTTON]); // 83 - Button sample
  fre((void**)&SamplePtr[SOUND_SAMPLE_START]); // 87 - Start sample
  fre((void**)&SamplePtr[SOUND_SAMPLE_CARIN]); // 84 - Car in sample
  fre((void**)&SamplePtr[SOUND_SAMPLE_CAROUT]); // 85 - Car out sample
  fre((void**)&SamplePtr[SOUND_SAMPLE_TRACK]); // 86 - Track sample
}

//-------------------------------------------------------------------------------------------------
//0003B480
void releasesamples()
{
  if (SoundCard) {
    for (int i = 0; i < 120; ++i) {
      fre((void**)&SamplePtr[i]);
      SamplePtr[i] = NULL;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003B4D0
void play()
{
  if (musicon) {
    if (SongPtr) {
      MIDIStartSong();

      //InitSong.nSection = __DS__;
      //InitSong.pData = (void *)SongPtr;
      //InitSong.nUnk2 = 0;
      //InitSong.iUnk1 = 0;
      //return sosMIDIStartSong(*(int *)&SongHandle);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003B520
void stop()
{
  if (MusicCard) {
    if (SongPtr) {
      MIDIStopSong();
      //sosMIDIStopSong(SongHandle);
      //sosMIDIResetSong(SongHandle);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003B580
void devicespecificinit()
{
  int i; // esi
  unsigned int uiSize;

  switch (MusicCard) {
    case 40961:
      printf("Resetting SCC-1...");
      fflush(stdout);
      //sosMIDISendMIDIData(MIDIHandle, 11, (int)&SCreset, __DS__);
      printf("OK\n");
      break;
    case 40962:
    case 40969:
      printf("Loading Instrument bank file...");
      fflush(stdout);
      loadfile("melodic.bnk", &FMInstruments, &uiSize, 0);
      if (FMInstruments) {
        printf("OK\n");
        //sosMIDISetInsData(MIDIHandle, 1, (int)FMInstruments, __DS__);
      } else {
        printf("Failed\n");
        MusicCard = 0;
      }
      printf("Loading Drums bank file...");
      fflush(stdout);
      loadfile("drum.bnk", &FMDrums, &uiSize, 0);
      if (FMDrums) {
        printf("OK\n");
        //sosMIDISetInsData(MIDIHandle, 1, (int)FMDrums, __DS__);
      } else {
        printf("Failed\n");
        MusicCard = 0;
      }
      if (!MusicCard) {
        //sosMIDIUnInitDriver(MIDIHandle, 1);
        //sosMIDIUnInitSystem();
      }
      break;
    case 40964:
      printf("Resetting LAPC-1...");
      //sosMIDISendMIDIData(MIDIHandle, 11, (int)&MT32reset, __DS__);
      fflush(stdout);
      loadfile("mt32map.mtx", &MT32Data, &uiSize, 0);
      if (MT32Data) {
        fflush(stdout);
        for (i = 0; i != 1104; i += 138) {
          //sosMIDISendMIDIData(MIDIHandle, 138, (int)MT32Data + i, __DS__);
          printf(".");
          fflush(stdout);
        }
        printf("OK\n");
      } else {
        printf("Failed\n");
      }
      break;
    default:
      return;
  }
}

//-------------------------------------------------------------------------------------------------
//0003B7B0
int initgus()
{
  printf("DownLoading  GRAVIS patches...");
  fflush(stdout);
  system("..\\LOADPATS  -Q -IFATALPAT.INI >NUL");
  return printf("Done\n");
}

//-------------------------------------------------------------------------------------------------
//0003B810
void devicespecificuninit()
{
  switch (MusicCard) {
    case 40961:
      //sosMIDISendMIDIData(MIDIHandle, 11, (int)&SCreset, __DS__);
      break;
    case 40962:
    case 40969:
      fre(&FMInstruments);
      fre(&FMDrums);
      break;
    case 40964:
      //sosMIDISendMIDIData(MIDIHandle, 11, (int)&MT32reset, __DS__);
      fre(&MT32Data);
      break;
  }
}

//-------------------------------------------------------------------------------------------------
//0003B880
void readsoundconfig(void)
{
  FILE *fp = ROLLERfopen("../config.ini", "rb");
  char *pBuffer = NULL;
  long iSize;

  if (!fp) 
  {
    autoselectsoundlanguage(); // Add by ROLLER to auto-select languagename
    return;
  }

  // Determine file size
  fseek(fp, 0, SEEK_END);
  iSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  pBuffer = (char *)getbuffer(iSize + 1);
  if (!pBuffer) {
    fclose(fp);
    return;
  }

  fread(pBuffer, 1, iSize, fp);
  pBuffer[iSize] = '\0'; // Null-terminate for string parsing

  // Parse Language
  char *szVar = FindConfigVar(pBuffer, "Language");
  if (szVar) sscanf(szVar, "%s", languagename);

  // Match language index
  language = 0;
  for (int i = 0; i < languages; i++) {
    if (strcmp(languagename, lang[i]) == 0) {
      language = i;
      break;
    }
  }

  // SourcePath
  szVar = FindConfigVar(pBuffer, "SourcePath");
  if (szVar) sscanf(szVar, "%s", SourcePath);

  // DestinationPath
  szVar = FindConfigVar(pBuffer, "DestinationPath");
  if (szVar) sscanf(szVar, "%s", DestinationPath);

  // Sound settings
  szVar = FindConfigVar(pBuffer, "SoundCard");
  uint16 unTemp = 0;
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    SoundCard = unTemp;
  }

  szVar = FindConfigVar(pBuffer, "SoundPort");
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    SoundPort = unTemp;
  }

  szVar = FindConfigVar(pBuffer, "SoundIRQ");
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    SoundIRQ = unTemp;
  }

  szVar = FindConfigVar(pBuffer, "SoundDMA");
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    SoundDMA = unTemp;
  }

  // Music settings
  szVar = FindConfigVar(pBuffer, "MusicCard");
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    MusicCard = unTemp;
  }

  szVar = FindConfigVar(pBuffer, "MusicPort");
  if (szVar) {
    sscanf(szVar, "%hi", &unTemp);
    MusicPort = unTemp;
  }

  // Cleanup
  fre((void**)&pBuffer);
  fclose(fp);

  // Enable CD Audio
  if (MusicCard == 0xCD) {
    MusicCard = 0;
    MusicCD = -1;
  }
  
  //hack, if CD audio not found force switch to MIDI
  if (g_iNumTracks == 0) {
    MusicCard = -1;
    MusicCD = 0;
  }
  
  //hack the SoundCard to be available too
  if (!SoundCard)
    SoundCard = -1;

  // Set flags
  if (MusicCard == 0 && MusicCD == 0)
    musicon = 0;

  if (SoundCard == 0)
    soundon = 0;
}

//-------------------------------------------------------------------------------------------------
//0003BAF0
char *FindConfigVar(const char *szConfigText, const char *szVarName)
{
  char *szMatch = strstr(szConfigText, szVarName);
  if (!szMatch)
    return NULL;

  if (szMatch != szConfigText) {
    // check that the match is at the start of a line or preceded by whitespace.
    char cPrev = szMatch[-1];
    if (isalpha((uint8)cPrev) || isdigit((uint8)cPrev) || cPrev == '_')
      return NULL;
  }

  // move 'edx' to point past the matched variable name
  char *szEnd = szMatch;
  uint32 uiLen = (uint32)strlen(szVarName) + 1;
  szEnd += uiLen;

  // skip any trailing spaces
  while (isspace((uint8)(*szEnd + 1)))
    szEnd++;

  // make sure it's followed by '='
  if (szEnd[-1] != '=')
    return NULL;

  return szEnd;
}

//-------------------------------------------------------------------------------------------------
//0003BB50
void loadfile(const char *szFile, void **pBuf, unsigned int *uiSize, int iIsSound)
{
  int iFile; // eax
  void *pBuf2; // eax
  uint8 *pUint8Buf; // edx

  *pBuf = 0;
  *uiSize = 0;
  iFile = ROLLERopen(szFile, O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h
  if (iFile == -1) {
    *uiSize = 0;
    *pBuf = 0;
  } else {
    close(iFile);
    *uiSize = getcompactedfilelength(szFile);
    if (!iIsSound || iIsSound == 1 && soundon) {
      pBuf2 = trybuffer(*uiSize);
      *pBuf = pBuf2;
      if (pBuf2) {
        pUint8Buf = (uint8 *)pBuf2;
        initmangle(szFile);
        loadcompactedfilepart(pUint8Buf, 1000000000u);
        uninitmangle();
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003BBD0
void ReadJoys(tJoyPos *pJoy)
{
  // Check joystick 1 (controller 1)
  pJoy->iJ1Button1 = g_rollerJoyPos.iJ1Button1;
  pJoy->iJ1Button2 = g_rollerJoyPos.iJ1Button2;
  pJoy->iJ1XAxis = g_rollerJoyPos.iJ1XAxis;
  pJoy->iJ1YAxis = g_rollerJoyPos.iJ1YAxis;

  // Check joystick 2 (controller 2)
  pJoy->iJ2Button1 = g_rollerJoyPos.iJ2Button1;
  pJoy->iJ2Button2 = g_rollerJoyPos.iJ2Button2;
  pJoy->iJ2XAxis = g_rollerJoyPos.iJ2XAxis;
  pJoy->iJ2YAxis = g_rollerJoyPos.iJ2YAxis;

  // Update presence flags based on controller status
  x1ok = (g_pController1 != NULL);
  y1ok = (g_pController1 != NULL);
  x2ok = (g_pController2 != NULL);
  y2ok = (g_pController2 != NULL);

  // Update global acceptance mask
  bitaccept = (x1ok << 0) | (y1ok << 1) | (x2ok << 2) | (y2ok << 3);

  /*unsigned __int8 byCurrentVal; // al
  int i; // ebx
  int v4; // eax
  int iJ2XAxis; // esi
  int iY1Ok; // ecx
  int iY2Ok; // edi
  int iCurrentVal; // eax
  int iX1Ok2; // ebx
  int iX2Ok; // [esp+4h] [ebp-2Ch]
  int iJ2YAxis; // [esp+8h] [ebp-28h]
  int iX1Ok; // [esp+Ch] [ebp-24h]
  int iJ1XAxis; // [esp+10h] [ebp-20h]
  int iJ1YAxis; // [esp+14h] [ebp-1Ch]

  // Read initial joystick state
  byCurrentVal = __inbyte(0x201u);
  i = 0;
  iJ1XAxis = 0;
  iJ1YAxis = 0;
  v4 = (int)(unsigned __int8)~byCurrentVal >> 4;
  iJ2YAxis = 0;
  pJoyPos->iJ1Button1 = v4 & 1;
  pJoyPos->iJ1Button2 = (v4 & 3) >> 1;
  iJ2XAxis = 0;
  pJoyPos->iJ2Button1 = (v4 >> 2) & 1;
  pJoyPos->iJ2Button2 = ((v4 >> 2) & 3) >> 1;
  _disable();
  __outbyte(0x201u, 0xFFu);

  // Store current axis presence flags
  iX1Ok = x1ok;
  iY1Ok = y1ok;
  iX2Ok = x2ok;
  iY2Ok = y2ok;

  // Main measurement loop
  do {
    iCurrentVal = 0;
    LOBYTE(iCurrentVal) = __inbyte(0x201u);

    // Measure X1 axis
    if (x1ok) {
      iX1Ok = iCurrentVal & 1;
      if ((iCurrentVal & 1) != 0)             // Axis still active
        ++iJ1XAxis;
    }

    // Measure Y1 axis
    if (y1ok) {
      iY1Ok = iCurrentVal & 2;
      if ((iCurrentVal & 2) != 0)             // Axis still active
        ++iJ1YAxis;
    }

    // Measure X2 axis
    if (x2ok) {
      iX2Ok = iCurrentVal & 4;
      if ((iCurrentVal & 4) != 0)             // Axis still active
        ++iJ2XAxis;
    }

    // Measure Y2 axis
    if (y2ok) {
      iY2Ok = iCurrentVal & 8;
      if ((iCurrentVal & 8) != 0)             // Axis still active
        ++iJ2YAxis;
    }
    ++i;
  } while ((iCurrentVal & bitaccept) != 0 && i < 10000);
  // Exit conditions:
  // 1. All axes have discharged
  // 2. Reached maximum loop count (10,000)

  // re-enable interrupts
  iX1Ok2 = iX1Ok;
  _enable();

  // Process results for X1 axis
  if (iX1Ok2) {
    pJoyPos->iJ1XAxis = 0;
    x1ok = 0;                                   // mark axis as unavailable
    bitaccept = y2ok | y1ok | x2ok;             // Update global acceptance mask
  } else {
    pJoyPos->iJ1XAxis = iJ1XAxis;
  }

  // Process results for Y1 axis
  if (iY1Ok) {
    pJoyPos->iJ1YAxis = 0;
    y1ok = 0;                                   // mark axis as unavailable
    bitaccept = y2ok | x1ok | x2ok;             // Update global acceptance mask
  } else {
    pJoyPos->iJ1YAxis = iJ1YAxis;
  }

  // Process results for X2 axis
  if (iX2Ok) {
    pJoyPos->iJ2XAxis = 0;
    x2ok = 0;                                   // Mark axis as unavailable
    bitaccept = y2ok | y1ok | x1ok;             // Update global acceptance mask
  } else {
    pJoyPos->iJ2XAxis = iJ2XAxis;
  }

  // Process results for Y2 axis
  if (iY2Ok) {
    pJoyPos->iJ2YAxis = 0;
    y2ok = 0;                                   // Mark axis as unavailable
    bitaccept = x2ok | y1ok | x1ok;             // Update global acceptance mask
  } else {
    pJoyPos->iJ2YAxis = iJ2YAxis;
  }*/
}

//-------------------------------------------------------------------------------------------------
//0003BDD0
void check_joystickpresence()
{
  //TODO: integrate this with SDL
  int iTimeoutCounter; // ebx
  int iY1Temp; // esi
  int iX2Temp; // ecx
  int iY2Temp; // edi
  int iGameportStatus; // eax
  int iX1Temp; // [esp+0h] [ebp-18h]

  iTimeoutCounter = 0;                          // Initialize timeout counter
  x1ok = 1;                                     // Initialize joystick axis detection flags (bit masks for port 0x201)
  y1ok = 2;
  x2ok = 4;
  y2ok = 8;
  bitaccept = 15;
  JAXmin = 10000;                               // Initialize joystick axis calibration min/max values
  JAXmax = -10000;
  JAYmin = 10000;
  JAYmax = -10000;
  JBXmin = 10000;
  JBXmax = -10000;
  JBYmin = 10000;
  JBYmax = -10000;
  //__outbyte(0x201u, 0xFFu);                     // Trigger joystick measurement by writing 0xFF to gameport 0x201
  iX1Temp = x1ok;
  iY1Temp = y1ok;
  iX2Temp = x2ok;
  iY2Temp = y2ok;
  do {
    iGameportStatus = 0;                        // Main detection loop: read gameport and test axis presence
    //LOBYTE(iGameportStatus) = __inbyte(0x201u); // Read current status from gameport 0x201
    if (x1ok)                                 // Test X1 axis (bit 0) - if enabled, check if still active
      iX1Temp = iGameportStatus & 1;
    if (y1ok)                                 // Test Y1 axis (bit 1) - if enabled, check if still active
      iY1Temp = iGameportStatus & 2;
    if (x2ok)                                 // Test X2 axis (bit 2) - if enabled, check if still active
      iX2Temp = iGameportStatus & 4;
    if (y2ok)                                 // Test Y2 axis (bit 3) - if enabled, check if still active
      iY2Temp = iGameportStatus & 8;
    ++iTimeoutCounter;
  } while ((iGameportStatus & bitaccept) != 0 && iTimeoutCounter < 10000);// Continue loop while any expected axes are still active and timeout not reached
  if (iX1Temp)                                // Disable axes that didn't respond (indicating no joystick connected to that axis)
    x1ok = 0;
  if (iX2Temp)
    x2ok = 0;
  if (iY1Temp)
    y1ok = 0;
  if (iY2Temp)
    y2ok = 0;
  bitaccept = y2ok | y1ok | x1ok | x2ok;        // Update bitaccept mask with only the axes that are actually present
}

//-------------------------------------------------------------------------------------------------
//0003BF20
void initsounds()
{
  //int iNumCars; // ebx
  //int iLoopItr; // eax
  //int i; // ecx
  //int iCar; // edx
  //int SampleHandleCarOffset; // eax
  //int iPendingOffset; // eax
  //
  //iNumCars = numcars;
  //iLoopItr = 2;

  for (int i = 0; i < 32; ++i) {
    HandleSample[i] = -1;
    HandleCar[i] = -1;
  }
  //HandleSample[0] = -1;
  //HandleCar[0] = -1;
  //HandleSample[1] = -1;
  //HandleCar[1] = -1;
  //do {
  //  iLoopItr += 5;
  //  HandleCar[iLoopItr + 27] = -1;              // offset into HandleSample
  //  SamplePending_variable_3[iLoopItr] = -1;    // offset into HandleCar
  //  HandleCar[iLoopItr + 28] = -1;
  //  SamplePending_variable_4[iLoopItr] = -1;
  //  HandleCar[iLoopItr + 29] = -1;
  //  SamplePending_variable_5[iLoopItr] = -1;
  //  HandleCar[iLoopItr + 30] = -1;
  //  SamplePending_variable_6[iLoopItr] = -1;
  //  HandleCar[iLoopItr + 31] = -1;
  //  SamplePending_variable_7[iLoopItr] = -1;
  //} while (iLoopItr != 32);

  for (int i = 0; i < 120; ++i) {
    SampleHandleCar[i].handles[0] = -1;
  }
  //for (i = 0; i != 7680; i += 64) {
  //  iCar = 0;
  //  if (iNumCars > 0) {
  //    SampleHandleCarOffset = i;
  //    do {
  //      SampleHandleCarOffset += 4;
  //      ++iCar;
  //      *(uint8 **)((char *)&SamplePtr[119] + SampleHandleCarOffset) = (uint8 *)-1;// offsets into SampleHandleCar
  //    } while (iCar < iNumCars);
  //  }
  //}

  for (int i = 0; i < numcars; ++i) {
    Pending[i] = 0;
  }
  //if (iNumCars > 0) {
  //  iPendingOffset = 0;
  //  do {
  //    iPendingOffset += 4;
  //    *(_DWORD *)&SampleExt[iPendingOffset + 62] = 0;// offsets into Pending
  //  } while (iPendingOffset < 4 * iNumCars);
  //}
  //numcars = iNumCars;
}

//-------------------------------------------------------------------------------------------------
//0003BFF0
void stopallsamples()
{
  //_disable();

  // Clear/initialize engine sound volume data for all cars
  for (int iCarIndex = 0; iCarIndex < numcars; ++iCarIndex) {
    for (int iEngineDataIdx = 0; iEngineDataIdx < 32; ++iEngineDataIdx) {
      tEngineSoundData *pEngineData = &enginedelay[iCarIndex].engineSoundData[iEngineDataIdx];

      pEngineData->iEngineVol = -1;
      pEngineData->iEngine2Vol = -1;
      pEngineData->iSkid1Vol = -1;
    }
  }

  for (int iSample = 0; iSample < numsamples; ++iSample) {
    for (int iCar = 0; iCar < numcars; ++iCar) {
      int iCurrHandle = SampleHandleCar[iSample].handles[iCar];
      if ( iCurrHandle != -1 )
        DIGIStopSample(iCurrHandle);
        //sosDIGIStopSample(*(int *)&DIGIHandle, iCurrHandle);
      SampleHandleCar[iSample].handles[iCar] = -1;
    }
  }

  for (int iSampleIdx = 0; iSampleIdx < 32; ++iSampleIdx) {
    HandleSample[iSampleIdx] = -1;
    HandleCar[iSampleIdx] = -1;
  }

  lastsample = -1000;
  //_enable();
}

//-------------------------------------------------------------------------------------------------
//0003C110
void pannedsample(int iSampleIdx, int iHandle, int iPan)
{
  int iLocalHandle; // edi
  //int iSampleOffset; // ebp
  int iExistingHandle; // ebx
  uint8 *pSampleData; // eax
  unsigned int iNewHandle; // eax
  unsigned int iOldSampleIdx; // ecx

  iLocalHandle = iHandle;
  if (soundon && SamplePtr[iSampleIdx]) {
    //iSampleOffset = iSampleIdx << 6;
    iExistingHandle = SampleHandleCar[iSampleIdx].handles[0];
    if (iExistingHandle != -1) {
      //_disable();
      iHandle = SampleHandleCar[iSampleIdx].handles[0];
      //if (!sosDIGISampleDone(*(int *)&DIGIHandle, iExistingHandle)) {
      if (!DIGISampleDone(iExistingHandle)) {
        iHandle = iExistingHandle;
        DIGIStopSample(iExistingHandle);
        //sosDIGIStopSample(*(int *)&DIGIHandle, iExistingHandle);
        //*(int *)((char *)SampleHandleCar[0].handles + iSampleOffset) = -1;
        SampleHandleCar[iSampleIdx].handles[0] = -1;
        HandleSample[iExistingHandle] = -1;
      }
      //_enable();
    }
    //SamplePanned.unSegment = __DS__;            // Set up panned sample structure with audio parameters
    SamplePanned.iSampleIndex = iSampleIdx;
    SamplePanned.iVolume = iLocalHandle;
    pSampleData = SamplePtr[iSampleIdx];
    SamplePanned.iPan = iPan;
    SamplePanned.pSample = pSampleData;
    SamplePanned.iLength = SampleLen[iSampleIdx];
    //_disable();                                 // Start playing the new panned sample and get handle
    iNewHandle = DIGISampleStart(&SamplePanned);
    //LOWORD(iNewHandle) = sosDIGIStartSample(*(int *)&DIGIHandle, iHandle, &SamplePanned);
    SampleHandleCar[iSampleIdx].handles[0] = iNewHandle;
    //_enable();
    if (iNewHandle != -1)                     // Update handle tracking tables if sample started successfully
    {                                           // Validate handle is within expected range (< 32)
      //if (iNewHandle >= 0x20)
      //  _assert(0, "s>=0 && s<32", a3, 2071);
      iOldSampleIdx = HandleSample[iNewHandle]; // Clean up any existing handle mappings before assigning new ones
      if (iOldSampleIdx != -1) {
        //if (iOldSampleIdx >= 0x78)
        //  _assert(0, "HandleSample[s]>=0 && HandleSample[s]<MAXSAMPLES", a3, 2075);
        //if ((unsigned int)HandleCar[iNewHandle] >= 16)
        //  _assert(0, "HandleCar[s]>=0 && HandleCar[s]<MAXCARS", a3, 2076);
        SampleHandleCar[HandleSample[iNewHandle]].handles[HandleCar[iNewHandle]] = -1;
      }
      HandleSample[iNewHandle] = iSampleIdx;    // Update handle-to-sample and handle-to-car mapping tables
      HandleCar[iNewHandle] = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003C2B0
void speechonly(int iSampleIdx, int iVolume, int iDelay, int iCarIdx)
{
  if (readsample == writesample && -iDelay > lastsample)
    speechsample(iSampleIdx, iVolume, iDelay, iCarIdx);
}

//-------------------------------------------------------------------------------------------------
//0003C2E0
void speechsample(int iSampleIdx, int iVolume, int iDelay, int iCarIdx)
{
  int iWriteIndex; // edi
  int iGameOverCount; // eax
  int iCurrentWriteIndex; // eax

  iWriteIndex = writesample;                    // Get current write position in speech queue
  if (!disable_messages)                      // Only queue samples if messages are not disabled
  {                                             // Special handling for game over sample (index 30)
    if (iSampleIdx == 30) {
      iGameOverCount = ++game_overs;
      if (player_type != 2 || iGameOverCount == 2)// Disable further messages after 2 game overs in multiplayer or any in single player
        disable_messages = -1;
    }
    if (!winner_mode || iSampleIdx >= 89)     // Only queue certain samples when in winner mode (samples >= 89)
    {
      iCurrentWriteIndex = writesample;         // Queue speech sample data into the circular buffer
      speechinfo[iCurrentWriteIndex].iSampleIdx = iSampleIdx;
      speechinfo[iCurrentWriteIndex].iVolume = iVolume;
      speechinfo[iCurrentWriteIndex].iDelay = iDelay;
      ++iWriteIndex;
      speechinfo[iCurrentWriteIndex].iCarIdx = iCarIdx;
      if (iWriteIndex == 16)                  // Wrap around queue index when it reaches buffer size (16)
        iWriteIndex = 0;
    }
  }
  writesample = iWriteIndex;                    // Update global write position for next sample
}

//-------------------------------------------------------------------------------------------------
//0003C530
void analysespeechsamples()
{
  int iCarIdx; // ecx
  int iCarIdxUpper; // esi
  int iIsPlayerCar; // edx
  const char *pszMessage; // eax
  char *pszText; // eax
  int iLap; // eax
  int iWinCount; // edx
  int iKillCount; // edi
  int iResultKills; // ecx
  int iSampleIdx; // eax
  int iSampleDuration; // eax

  --lastsample;                                 // Decrement sample timer
  if (readsample != writesample && -speechinfo[readsample].iDelay > lastsample)// Check if there are samples to process and timing is right
  {
    iCarIdx = (uint8)speechinfo[readsample].iCarIdx;// Extract car index from speech info
    iCarIdxUpper = speechinfo[readsample].iCarIdx >> 8;
    iIsPlayerCar = player_type == 2 && iCarIdx != player1_car;// Check if this is about player car vs other cars
    switch (speechinfo[readsample].iSampleIdx) {
      case 0xE:
        start_zoom(&language_buffer[1920], iIsPlayerCar);// Case 0xE: Handle crash/damage announcement
        if (Car[iCarIdx].byDamageSourceTimer)
          goto LABEL_13;
        goto LABEL_61;
      case 0xF:
        iLap = Car[iCarIdx].byLap;              // Case 0xF: Handle lap count announcements
        if (NoOfLaps == iLap)
          goto LABEL_15;
        sprintf(buffer, "%i %s", NoOfLaps + 1 - iLap, &language_buffer[3712]);
        pszText = buffer;
        goto LABEL_60;
      case 0x10:
      case 0x11:
      case 0x12:
      LABEL_15:
        pszText = &language_buffer[2240];
        goto LABEL_60;
      case 0x15:
        if ((char)Car[Victim].byLives <= 0)   // Case 0x15: Handle player elimination/death announcement
          pszMessage = &language_buffer[1984];
        else
          pszMessage = &language_buffer[1920];
        start_zoom(pszMessage, iIsPlayerCar);
        sprintf(buffer, "%s %s", &language_buffer[2048], driver_names[Victim]);
        subzoom(buffer);
        goto LABEL_61;
      case 0x16:
        pszText = &language_buffer[2560];
        goto LABEL_60;
      case 0x17:
      case 0x18:
      case 0x19:
        pszText = &language_buffer[64 * Car[iCarIdx].byRacePosition + 384];
        goto LABEL_60;
      case 0x1A:
        pszText = &language_buffer[2624];
        goto LABEL_60;
      case 0x1E:
        pszText = &language_buffer[2112];
        goto LABEL_60;
      case 0x25:
      case 0x26:
        start_zoom(&language_buffer[2752], iIsPlayerCar);// Cases 0x25-0x26: Handle best lap time announcements
        make_time(buffer, Car[iCarIdx].fBestLapTime);
        subzoom(buffer);
        goto LABEL_61;
      case 0x27:
        start_zoom(&language_buffer[2816], iIsPlayerCar);
        make_time(buffer, Car[iCarIdx].fBestLapTime);
        subzoom(buffer);
        goto LABEL_61;
      case 0x28:
        start_zoom(&language_buffer[2880], iIsPlayerCar);
        make_time(buffer, Car[iCarIdx].fBestLapTime);
        subzoom(buffer);
        goto LABEL_61;
      case 0x2B:
        pszText = &language_buffer[2944];
        goto LABEL_60;
      case 0x2C:
        pszText = &language_buffer[3008];
        goto LABEL_60;
      case 0x2D:
        start_zoom(&language_buffer[1984], iIsPlayerCar);
        if (Car[iCarIdx].byDamageSourceTimer) {
        LABEL_13:
          sprintf(buffer, "%s %s", &language_buffer[2176], driver_names[Car[iCarIdx].byAttacker]);
          subzoom(buffer);
        }
        goto LABEL_61;
      case 0x2E:
        pszText = &language_buffer[2688];
        goto LABEL_60;
      case 0x33:
        pszText = &language_buffer[6080];
        goto LABEL_60;
      case 0x34:
        pszText = &language_buffer[6144];
        goto LABEL_60;
      case 0x35:
        pszText = &language_buffer[2496];
        goto LABEL_60;
      case 0x36:
        pszText = &language_buffer[2432];
        goto LABEL_60;
      case 0x37:
        pszText = &language_buffer[2368];
        goto LABEL_60;
      case 0x38:
        pszText = &language_buffer[2304];
        goto LABEL_60;
      case 0x39:
      case 0x3A:
      case 0x3B:
      case 0x3C:
        small_zoom(&language_buffer[4992]);
        subzoom(&language_buffer[64 * speechinfo[readsample].iSampleIdx + 1408]);
        goto LABEL_61;
      case 0x41:
      case 0x42:
        small_zoom(&language_buffer[4928]);
        subzoom(&language_buffer[64 * iCarIdxUpper + 1152]);
        goto LABEL_61;
      case 0x47:
      case 0x48:
      case 0x49:
      case 0x4A:
      case 0x4B:
      case 0x4C:
      case 0x4D:
      case 0x4E:
        small_zoom(&language_buffer[64 * speechinfo[readsample].iSampleIdx + 768]);
        switch (iCarIdxUpper) {
          case '=':
          case '>':
          case '?':
          case '@':
            subzoom(&language_buffer[64 * iCarIdxUpper + 1152]);
            break;
          case 'C':
            subzoom(&language_buffer[5952]);
            break;
          case 'D':
            subzoom(&language_buffer[6016]);
            break;
          case 'E':
            subzoom(&language_buffer[5888]);
            break;
          case 'F':
            subzoom(&language_buffer[5824]);
            break;
          default:
            goto LABEL_61;
        }
        goto LABEL_61;
      case 0x59:
      case 0x5A:
      case 0x5B:
      case 0x5C:
      case 0x5D:
      case 0x5E:
      case 0x5F:
      case 0x60:
        iWinCount = total_wins[champorder[0]];  // Cases 0x59-0x60: Handle championship win count announcements
        if (iWinCount == 1)
          sprintf(buffer, "%s", &config_buffer[6784]);
        else
          sprintf(buffer, "%s %i %s", &config_buffer[6656], iWinCount, &config_buffer[6720]);
        pszText = buffer;
        goto LABEL_59;
      case 0x61:
      case 0x62:
      case 0x63:
      case 0x64:
      case 0x65:
      case 0x66:
      case 0x67:
      case 0x68:
      case 0x69:
      case 0x6A:
      case 0x6B:
      case 0x6C:
      case 0x6D:
      case 0x6E:
      case 0x6F:
      case 0x70:
      case 0x71:
        if (champ_mode)                       // Cases 0x61-0x71: Handle kill count announcements
        {
          iKillCount = total_kills[champorder[0]];
          if (iKillCount == 1)
            sprintf(buffer, "%s", &config_buffer[6464]);
          else
            sprintf(buffer, "%s %i %s", &config_buffer[6528], iKillCount, &config_buffer[6592]);
          pszText = buffer;
        } else {
          iResultKills = result_kills[result_order[0]];
          if (iResultKills != 1) {
            sprintf(buffer, "%s %i %s", &config_buffer[6528], iResultKills, &config_buffer[6592]);
            iIsPlayerCar = 0;
            pszText = buffer;
            goto LABEL_60;
          }
          pszText = &config_buffer[6464];
        }
      LABEL_59:
        iIsPlayerCar = 0;
      LABEL_60:
        start_zoom(pszText, iIsPlayerCar);
      LABEL_61:
        if (soundon)                          // Play the actual audio sample if sound is enabled
        {
          iSampleIdx = speechinfo[readsample].iSampleIdx;
          if (SamplePtr[iSampleIdx])
            dospeechsample(iSampleIdx, speechinfo[readsample].iVolume);
        }
        iSampleDuration = (int)(36 * SampleLen[speechinfo[readsample++].iSampleIdx]) / 11025;// Calculate sample duration and advance to next sample in queue
        lastsample = iSampleDuration;
        if (readsample == 16)
          readsample = 0;
        break;
      case 0x73:
        if (result_kills[result_order[0]] <= 0 && result_order[0] != FastestLap)
          goto LABEL_61;
        pszText = &config_buffer[6080];
        goto LABEL_59;
      case 0x74:
        pszText = &config_buffer[6144];
        goto LABEL_59;
      case 0x75:
        pszText = &config_buffer[6208];
        goto LABEL_59;
      default:
        goto LABEL_61;                          // Process different types of speech samples based on sample index
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003CAD0
void dospeechsample(int iSampleIdx, int iVolume)
{
  int iUseVolume;
  int iHandle, iSampleHandle;

  // Check sample pointer is valid
  if (SamplePtr[iSampleIdx] == 0) {
    SDL_Log("dospeechsample: Sample pointer is NULL for sample index %d", iSampleIdx);
    return;
  }

  // Clamp volume to 0x7FFF
  if (iVolume > 0x7FFF)
    iVolume = 0x7FFF;

  // Scale volume by speech volume setting (div 127)
  iUseVolume = (SpeechVolume * iVolume) / 127;

  // Abort if sound is off or paused
  if (!soundon || paused)
    return;

  iSampleHandle = SampleHandleCar[iSampleIdx].handles[0];

  // If a sample is already assigned
  if (iSampleHandle != -1) {
    if (!DIGISampleDone(iSampleHandle)) {
      // Stop previous sample if it's still playing
      DIGIStopSample(iSampleHandle);
      SampleHandleCar[iSampleIdx].handles[0] = -1;
      HandleSample[iSampleHandle] = -1;
    }
  }

  // Setup sample parameters
  //SampleFixed.unSegment = __DS__;
  SampleFixed.iSampleIndex = iSampleIdx;
  SampleFixed.iVolume = iUseVolume;
  SampleFixed.pSample = SamplePtr[iSampleIdx];
  SampleFixed.iLength = SampleLen[iSampleIdx];

  // Start sample
  iHandle = DIGISampleStart(&SampleFixed);
  SampleHandleCar[iSampleIdx].handles[0] = iHandle;

  if (iHandle == -1)
    return;

  int iPrevHandle = HandleSample[iHandle];

  // If that handle was previously used for another sample
  if (iPrevHandle != -1)
    SampleHandleCar[iPrevHandle].handles[HandleCar[iHandle]] = -1;

  HandleSample[iHandle] = iSampleIdx;
  HandleCar[iHandle] = 0;
}

//-------------------------------------------------------------------------------------------------
//0003CC00
int frontendspeechhandle;
uint8 *frontendspeechptr;
uint32 frontendlen;

void loadfrontendsample(char *fileName)
{
  if (!SoundCard)
    return;

  // Clear any existing frontend speech sample
  if (frontendspeechhandle != -1) {
    DIGIStopSample(frontendspeechhandle);
    frontendspeechhandle = -1;
  }

  // Free any existing frontend speech pointer
  if (frontendspeechptr)
    fre((void**)&frontendspeechptr);

  char szFilenameBuf[32];
  // construct sample filename
  char *szSrc = fileName;
  char *szDst = szFilenameBuf;
  while (*szSrc) {
    *szDst++ = *szSrc++;
    if (!*szSrc) break;
    *szDst++ = *szSrc++;
  }
  *szDst = '\0';

  // convert sample name to real path or cheat version
  convertname(szFilenameBuf);
  SDL_Log("Loading track sample: %s\n", szFilenameBuf);
  // load file into memory
  loadfile(szFilenameBuf, (void**)&frontendspeechptr, &frontendlen, 1);

  if (cheatsample && frontendspeechptr) {
    uint8 *pData = frontendspeechptr;
    int iLen = frontendlen;
    uint32 uiSeed = 0x1C73;
    uint32 uiStep = 0x896;
    int i;

    for (i = 0; i < iLen; ++i) {
      uiSeed += uiStep;
      pData[i] ^= (uint8)uiSeed;
    }
  }

  // if using 50Hz timer
  if (frontendspeechptr && (cheat_mode & CHEAT_MODE_50HZ_TIMER)) {
    if (cheat_mode & CHEAT_MODE_100HZ_TIMER) { //100Hz timer
      // halve the sample length, keep only even-indexed bytes
      int iLen = frontendlen;
      uint8 *pData = frontendspeechptr;
      for (int i = 0; i < iLen; i += 2) {
        pData[i / 2] = pData[i];
      }
      frontendlen = iLen / 2;
    } else {
      // remove every third byte
      int iLen = frontendlen;
      uint8 *pData = frontendspeechptr;
      int iNewLen = 0;
      for (int i = 0; i < iLen; ++i) {
        if (i % 3 != 2) {
          pData[iNewLen++] = pData[i];
        }
      }
      frontendlen = iNewLen;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003CD90
int frontendsample(int iVol)
{
  if (iVol > 0x7FFF)
    iVol = 0x7FFF;

  // Scale volume using SpeechVolume
  int iScaledVol = (SpeechVolume * iVol) / 127;

  SampleFixed.iSampleIndex = 0;
  SampleFixed.iVolume = iScaledVol;
  SampleFixed.pSample = frontendspeechptr;
  SampleFixed.iLength = frontendlen;

  frontendspeechhandle = DIGISampleStart(&SampleFixed);

  return frontendspeechhandle;
}

//-------------------------------------------------------------------------------------------------
//0003CE20
void remove_frontendspeech()
{
  // Clear any existing frontend speech sample
  if (frontendspeechhandle != -1) {
    DIGIStopSample(frontendspeechhandle);
    frontendspeechhandle = -1;
  }

// Free any existing frontend speech pointer
  if (frontendspeechptr)
    fre((void**)&frontendspeechptr);
}

//-------------------------------------------------------------------------------------------------
//0003CE70
int sfxplaying(int iSampleIdx)
{
  //int iSampleHandleCarOffset; // ecx
  int iCurrHandle; // ebx
  int iReturn0; // eax

  //iSampleHandleCarOffset = iSampleIdx << 6;
  iCurrHandle = SampleHandleCar[iSampleIdx].handles[0];
  if (iCurrHandle == -1)
    return 0;
  if (!DIGISampleDone(iCurrHandle))
    return -1;
  iReturn0 = 0;
  SampleHandleCar[iSampleIdx].handles[0] = -1;
  HandleSample[iCurrHandle] = -1;
  return iReturn0;
}

//-------------------------------------------------------------------------------------------------
//0003CEC0
int cheatsampleok(int iCarIdx)
{
  if (iCarIdx == player1_car || iCarIdx == player2_car)
    return -1;
  else
    return 0;
}

//-------------------------------------------------------------------------------------------------
//0003CEF0
void sfxsample(int iSample, int iVol)
{
  if (SamplePtr[iSample] == 0) {
    SDL_Log("sfxsample: Sample pointer is NULL for sample index %d", iSample);
    return;
  }

  // Clamp volume to maximum 0x7FFF
  if (iVol > 0x7FFF)
    iVol = 0x7FFF;

  // Scale volume using SFXVolume
  int iScaledVol = (SFXVolume * iVol) / 127;

  // Check if sound is disabled or paused
  if (!soundon || paused)
    return;

  // Stop any currently playing sample for this index
  int iOldHandle = SampleHandleCar[iSample].handles[0];
  if (iOldHandle != -1) {
    // cli(); Disable interrupts
    if (!DIGISampleDone(iOldHandle)) {
      DIGIStopSample(iOldHandle);
      SampleHandleCar[iSample].handles[0] = -1;
      HandleSample[iOldHandle] = -1;
    }
    // sti(); Enable interrupts
  }

  // Prepare sample data
  //SampleFixed.unSegment = ds;
  SampleFixed.iSampleIndex = iSample;
  SampleFixed.iVolume = iScaledVol;
  SampleFixed.pSample = SamplePtr[iSample];
  SampleFixed.iLength = SampleLen[iSample];

  // cli(); Disable interrupts
  // Calculate offset in SampleHandleCar array (64 bytes per element)
  //int iHandleOffset = iSample << 6;
  int iNewHandle = DIGISampleStart(&SampleFixed);
  // Store new handle
  SampleHandleCar[iSample].handles[0] = iNewHandle;
  // sti(); Enable interrupts

  // Clear previous sample association if handle was reused
  if (iNewHandle == -1)
    return;
  if (HandleSample[iNewHandle] != -1) {
    int iPrevCar = HandleCar[iNewHandle];
    SampleHandleCar[HandleSample[iNewHandle]].handles[iPrevCar] = -1;
  }

  // Update handle tracking arrays
  HandleSample[iNewHandle] = iSample;
  HandleCar[iNewHandle] = 0; // Always use first slot in handles array
}


//-------------------------------------------------------------------------------------------------
//0003D020
void sample2(int iCarIndex, int iSampleIndex, int iVolume, int iPitch, int iPan, int iByteOffset)
{
  //int iOffset; // edi
  int iHandle; // eax
  int iOldHandle; // ecx
  uint8 *pSample; // [esp+0h] [ebp-14h]

  if (soundon) {
    if (!paused) {
      // Only proceed if no sample is currently playing in this slot
      //iOffset = 4 * iCarIndex + (iSampleIndex << 6);
      if (SampleHandleCar[iSampleIndex].handles[iCarIndex] == -1)// SampleHandleCar[iSampleIndex].handles[iCarIndex]?
      {
        // Prepare sample data structure
        SampleData.iSampleIndex = iSampleIndex;
        SampleData.iPitch = iPitch;
        pSample = SamplePtr[iSampleIndex];
        SampleData.iLength = SampleLen[iSampleIndex];
        //SampleData.unSegment = __DS__;
        SampleData.iPan = iPan;
        SampleData.pSample = pSample;
        SampleData.iByteOffset = iByteOffset;
        SampleData.iVolume = iVolume;

        // Start sample playback
        iHandle = DIGISampleStart(&SampleData);
        //LOWORD(iHandle) = sosDIGIStartSample(*(int *)&DIGIHandle, iSampleIndex, &SampleData);

        // Store new handle
        SampleHandleCar[iSampleIndex].handles[iCarIndex] = iHandle;// SampleHandleCar[iSampleIndex].handles[iCarIndex]?
        if (iHandle != -1) {
          // Clear any previous assignment of this handle
          iOldHandle = HandleSample[iHandle];
          if (iOldHandle != -1)
            SampleHandleCar[iOldHandle].handles[HandleCar[iHandle]] = -1;

          // Update handle tracking arrays
          HandleSample[iHandle] = iSampleIndex;
          HandleCar[iHandle] = iCarIndex;
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003D110
void sfxpend(int iSampleIdx, int iDriverIdx, int iVolume)
{
  int iClampedVolume; // edi
  int iAdjustedVolume; // edi
  //int iDriverIndex; // ebx
  int bSampleFound; // esi
  //int iArrayIndex; // eax
  //int iLoopCounter; // edx
  //int iPendingOffset; // eax
  //int iNewPendingCount; // ecx

  iClampedVolume = iVolume;                     // Clamp volume to maximum allowed value (32767)
  if (iVolume > 0x7FFF)
    iClampedVolume = 0x7FFF;
  if (replaytype != 2)                        // Store replay information if not in replay playback mode
  {
    newrepsample[iDriverIdx] = iSampleIdx;
    repvolume[iDriverIdx] = iClampedVolume / 256;
    //repvolume[iDriverIdx] = (unsigned __int16)(iClampedVolume - (__CFSHL__(iClampedVolume >> 31, 8) + ((unsigned __int16)(iClampedVolume >> 31) << 8))) >> 8;
  }
  iAdjustedVolume = iClampedVolume * SFXVolume / 127;// Calculate final volume based on global SFX volume setting
  if (soundon && Pending[iDriverIdx] != 5)    // Only queue sample if sound is enabled and pending queue isn't full
  {

    // Check if this sample is already pending for this driver
    bSampleFound = 0;
    for (int iSampleIndex = 0; iSampleIndex < Pending[iDriverIdx]; iSampleIndex++) {
      if (iSampleIdx == SamplePending[iDriverIdx][iSampleIndex].iHandle) {
        bSampleFound = -1;
        break;  // No need to continue once found
      }
    }
    //iDriverIndex = iDriverIdx;
    //bSampleFound = 0;                           // Initialize duplicate detection flag and array indexing
    //iArrayIndex = 40 * iDriverIdx;
    //for (iLoopCounter = 0; iLoopCounter < Pending[iDriverIndex]; iArrayIndex += 2)// Check if this sample is already pending for this driver
    //{
    //  if (iSampleIdx == SamplePending[0][iArrayIndex])
    //    bSampleFound = -1;
    //  ++iLoopCounter;
    //}

    if (!bSampleFound) {
    // Add sample to pending queue if it's not a duplicate
      int iNextSlot = Pending[iDriverIdx];
      SamplePending[iDriverIdx][iNextSlot].iHandle = iSampleIdx;
      SamplePending[iDriverIdx][iNextSlot].iVolume = iAdjustedVolume;
      Pending[iDriverIdx]++;  // Increment pending count for this driver
    }
    //if (!bSampleFound)                        // Add sample to pending queue if it's not a duplicate
    //{
    //  iPendingOffset = 160 * iDriverIdx + 8 * Pending[iDriverIndex];// Calculate offset in pending array and store sample data
    //  *(int *)((char *)SamplePending[0] + iPendingOffset) = iSampleIdx;
    //  iNewPendingCount = Pending[iDriverIndex] + 1;
    //  *(int *)((char *)&SamplePending[0][1] + iPendingOffset) = iAdjustedVolume;
    //  Pending[iDriverIndex] = iNewPendingCount; // Increment pending count for this driver
    //}
  }
}

//-------------------------------------------------------------------------------------------------
//0003D200
void enginesounds2(int iPlayer1Car, int iPlayer2Car)
{
  int iEngine1Offset; // ecx
  int iEngine2Offset; // ecx

  if (soundon) {
    delaywritex = delaywrite & 0x1F;
    iEngine1Offset = 32 * iPlayer1Car;
    enginedelay[0].engineSoundData[(delaywrite & 0x1F) + iEngine1Offset].iEngineVol = -1;
    enginedelay[0].engineSoundData[delaywritex + iEngine1Offset].iEngine2Vol = -1;
    enginedelay[0].engineSoundData[delaywritex + iEngine1Offset].iSkid1Vol = -1;
    iEngine2Offset = 32 * iPlayer2Car;
    enginedelay[0].engineSoundData[delaywritex + iEngine2Offset].iEngineVol = -1;
    enginedelay[0].engineSoundData[delaywritex + iEngine2Offset].iEngine2Vol = -1;
    enginedelay[0].engineSoundData[delaywritex + iEngine2Offset].iSkid1Vol = -1;
    enginesound(iPlayer1Car, 0.0, 0.0, 2000.0, 0x8000);
    enginesound(iPlayer2Car, 0.0, 0.0, 2000.0, 0x8000);
    ++delaywrite;
  }
}

//-------------------------------------------------------------------------------------------------
//0003D310
void enginesounds(int iFocusCarIndex)
{
  //unsigned int uiInitOffset; // eax
  //int iTotalOffset; // ebp
  tCar *pFocusCar; // eax
  int iCurrChunk; // edx
  tData *pChunkData; // edx
  int iCarYaw; // ebx
  double dSpeedCos; // st7
  double dSpeedSin; // st6
  int iChunkIdx; // ebp
  double dCosRoll; // st7
  int iCarIndex; // ecx
  tCar *pCurrentCar; // ebx
  int iEnableSound; // eax
  int iCarChunk; // esi
  int iChunkDiff; // esi
  int iCurrentCarChunk; // eax
  tData *pCarChunkData; // eax
  int iYaw3; // esi
  double dCurrentCarSpeedCos; // st7
  double dCurrentCarSpeedSin; // st6
  int iHorizontalAngle; // edx
  int iVerticalAngle; // eax
  double dVolumeCalc; // st7
  float fTempVolumeCalc; // [esp+0h] [ebp-9Ch]
  int iRoll; // [esp+4h] [ebp-98h] BYREF
  int iYaw; // [esp+8h] [ebp-94h] BYREF
  int iPitch; // [esp+10h] [ebp-8Ch] BYREF
  float fHorizontalDistance; // [esp+14h] [ebp-88h]
  float fTransformedX; // [esp+18h] [ebp-84h]
  float fOrientMatrix_XX; // [esp+20h] [ebp-7Ch]
  float fOrientMatrix_ZX; // [esp+24h] [ebp-78h]
  float fOrientMatrix_ZZ; // [esp+28h] [ebp-74h]
  float fOrientMatrix_YX; // [esp+2Ch] [ebp-70h]
  float fOrientMatrix_YY; // [esp+30h] [ebp-6Ch]
  float fOrientMatrix_XY; // [esp+34h] [ebp-68h]
  float fListenerDirY; // [esp+38h] [ebp-64h]
  float fListenerDirX; // [esp+3Ch] [ebp-60h]
  float fListenerDirZ; // [esp+40h] [ebp-5Ch]
  float fZ; // [esp+44h] [ebp-58h]
  float fX; // [esp+48h] [ebp-54h]
  float fY; // [esp+4Ch] [ebp-50h]
  float fListenerDopplerVel; // [esp+50h] [ebp-4Ch]
  float fCarDopplerVel; // [esp+54h] [ebp-48h]
  float fCarVelY; // [esp+58h] [ebp-44h]
  float fCarPosX; // [esp+5Ch] [ebp-40h]
  float fCarVelZ; // [esp+60h] [ebp-3Ch]
  float fCarPosZ; // [esp+64h] [ebp-38h]
  float fTotalDistance; // [esp+68h] [ebp-34h]
  int iStereoVolume; // [esp+6Ch] [ebp-30h]
  float fRelativeY; // [esp+70h] [ebp-2Ch]
  float fRelativeZ; // [esp+74h] [ebp-28h]
  float fRelativeX; // [esp+78h] [ebp-24h]
  float fCarPosY; // [esp+7Ch] [ebp-20h]
  float fCarVelX; // [esp+80h] [ebp-1Ch]

  if (soundon)                                // Check if sound is enabled globally
  {
    delaywritex = delaywrite & 0x1F;            // Get current delay buffer write index (0-31)
    

    for (int iCar = 0; iCar < numcars; ++iCar) {
      enginedelay[iCar].engineSoundData[delaywritex].iEngineVol = -1;
      enginedelay[iCar].engineSoundData[delaywritex].iEngine2Vol = -1;
      enginedelay[iCar].engineSoundData[delaywritex].iSkid1Vol = -1;
    }
    //if (numcars > 0)                          // Initialize engine sound data for all cars
    //{
    //  uiInitOffset = 0;
    //  iTotalOffset = 896 * numcars;
    //  do {
    //    enginedelay[0].engineSoundData[delaywritex + uiInitOffset / 0x1C].iEngineVol = -1;
    //    enginedelay[0].engineSoundData[delaywritex + uiInitOffset / 0x1C].iEngine2Vol = -1;
    //    uiInitOffset += 896;
    //    SamplePending[12][7 * delaywritex + 5 + uiInitOffset / 4] = -1;
    //  } while ((int)uiInitOffset < iTotalOffset);
    //}
    if (DriveView[0] == 3 || DriveView[0] == 6)// Check for external camera view (3) or tower view (6)
    {                                           // Setup listener position for tower view camera
      if (DriveView[0] == 6) {
        fX = -localdata[2].pointAy[3].fX;
        fY = -localdata[2].pointAy[3].fY;
        iChunkIdx = 2;
        fZ = 1024.0f - localdata[2].pointAy[3].fZ;
      } else {
        iChunkIdx = TowerBase[NearTow].iChunkIdx;
        fX = TowerGx;
        fY = TowerGy;
        fZ = TowerGz;
      }
      fListenerDirX = 0.0;
      fListenerDirY = 0.0;
      fListenerDirZ = 0.0;
    } else {
      enginesound(iFocusCarIndex, 0.0, 0.0, 2000.0, 0x8000);// Process engine sound for focus car (in-car view)
      pFocusCar = &Car[iFocusCarIndex];
      iCurrChunk = pFocusCar->nCurrChunk;
      if (iCurrChunk == -1) {
        fX = pFocusCar->pos.fX;
        fY = pFocusCar->pos.fY;
        fZ = pFocusCar->pos.fZ;
        fListenerDirX = pFocusCar->direction.fX;
        fListenerDirY = pFocusCar->direction.fY;
        fListenerDirZ = pFocusCar->direction.fZ;
      } else {
        pChunkData = &localdata[iCurrChunk];    // Transform car position from chunk coordinates to world coordinates
        fX = pChunkData->pointAy[0].fY * pFocusCar->pos.fY
          + pChunkData->pointAy[0].fX * pFocusCar->pos.fX
          + pChunkData->pointAy[0].fZ * pFocusCar->pos.fZ
          - pChunkData->pointAy[3].fX;
        fY = pChunkData->pointAy[1].fY * pFocusCar->pos.fY
          + pChunkData->pointAy[1].fX * pFocusCar->pos.fX
          + pChunkData->pointAy[1].fZ * pFocusCar->pos.fZ
          - pChunkData->pointAy[3].fY;
        fZ = pChunkData->pointAy[2].fY * pFocusCar->pos.fY
          + pChunkData->pointAy[2].fX * pFocusCar->pos.fX
          + pChunkData->pointAy[2].fZ * pFocusCar->pos.fZ
          - pChunkData->pointAy[3].fZ;
        iCarYaw = pFocusCar->nActualYaw;
        dSpeedCos = pFocusCar->fFinalSpeed * tcos[iCarYaw];
        dSpeedSin = pFocusCar->fFinalSpeed * tsin[iCarYaw];
        fListenerDirX = (float)(pChunkData->pointAy[0].fY * dSpeedSin + pChunkData->pointAy[0].fX * dSpeedCos);
        fListenerDirY = (float)(pChunkData->pointAy[1].fX * dSpeedCos + pChunkData->pointAy[1].fY * dSpeedSin);
        fListenerDirZ = (float)(dSpeedCos * pChunkData->pointAy[2].fX + dSpeedSin * pChunkData->pointAy[2].fY);
      }
      iChunkIdx = pFocusCar->nCurrChunk;
      if (iChunkIdx == -1)
        iChunkIdx = pFocusCar->nReferenceChunk;
    }
    if (DriveView[0] != 3 && DriveView[0] != 6) {
      if (Car[iFocusCarIndex].iControlType == 3) {
        getworldangles(Car[iFocusCarIndex].nYaw, Car[iFocusCarIndex].nPitch, Car[iFocusCarIndex].nRoll, Car[iFocusCarIndex].nCurrChunk, &iYaw, &iPitch, &iRoll);
      } else {
        iYaw = Car[iFocusCarIndex].nYaw;
        iPitch = Car[iFocusCarIndex].nPitch;
        iRoll = Car[iFocusCarIndex].nRoll;
      }
      fOrientMatrix_XX = tcos[iYaw] * tcos[iPitch];// Calculate camera orientation matrix from yaw/pitch/roll angles
      fOrientMatrix_XY = tcos[iYaw] * tsin[iPitch] * tsin[iRoll] - tsin[iYaw] * tcos[iRoll];
      fOrientMatrix_YX = tsin[iYaw] * tcos[iPitch];
      fOrientMatrix_YY = tsin[iYaw] * tsin[iPitch] * tsin[iRoll] + tcos[iYaw] * tcos[iRoll];
      dCosRoll = -tsin[iRoll] * tcos[iPitch];
      fOrientMatrix_ZZ = tsin[iPitch];
      fOrientMatrix_ZX = (float)dCosRoll;
    }
    iCarIndex = 0;                              // Main loop: Process engine sounds for each car
    if (numcars > 0) {
      pCurrentCar = Car;
      do {
        iEnableSound = -1;
        if ((Car[iCarIndex].byLives & 0x80u) != 0)// Check if car is alive (bit 7 of byLives indicates death)
          iEnableSound = 0;
        if (DriveView[0] == 3 || DriveView[0] == 6) {
          if (iCarIndex == iFocusCarIndex || allengines)
            goto LABEL_34;
        } else if (iCarIndex != iFocusCarIndex && allengines) {
          goto LABEL_34;
        }
        iEnableSound = 0;
      LABEL_34:
        if (iEnableSound) {
          iCarChunk = Car[iCarIndex].nCurrChunk;
          if (iCarChunk == -1)
            iCarChunk = Car[iCarIndex].nReferenceChunk;
          iChunkDiff = iCarChunk - iChunkIdx;
          if (iChunkDiff < 0)
            iChunkDiff += TRAK_LEN;
          if (iChunkDiff > TRAK_LEN / 2)
            iChunkDiff -= TRAK_LEN;
          if ((int)abs(iChunkDiff) >= 40)   // Cull car sounds if more than 40 chunks away
          {
            enginesound(iCarIndex, 0.0, 0.0, 1048576.0, 0x8000);
          } else {
            iCurrentCarChunk = pCurrentCar->nCurrChunk;
            if (iCurrentCarChunk == -1) {
              fCarPosX = pCurrentCar->pos.fX;
              fCarPosY = pCurrentCar->pos.fY;
              fCarPosZ = pCurrentCar->pos.fZ;
              fCarVelX = pCurrentCar->direction.fX;
              fCarVelY = pCurrentCar->direction.fY;
              fCarVelZ = pCurrentCar->direction.fZ;
            } else {
              pCarChunkData = &localdata[iCurrentCarChunk];
              fCarPosX = pCarChunkData->pointAy[0].fY * pCurrentCar->pos.fY
                + pCarChunkData->pointAy[0].fX * pCurrentCar->pos.fX
                + pCarChunkData->pointAy[0].fZ * pCurrentCar->pos.fZ
                - pCarChunkData->pointAy[3].fX;
              fCarPosY = pCarChunkData->pointAy[1].fY * pCurrentCar->pos.fY
                + pCarChunkData->pointAy[1].fX * pCurrentCar->pos.fX
                + pCarChunkData->pointAy[1].fZ * pCurrentCar->pos.fZ
                - pCarChunkData->pointAy[3].fY;
              fCarPosZ = pCarChunkData->pointAy[2].fY * pCurrentCar->pos.fY
                + pCarChunkData->pointAy[2].fX * pCurrentCar->pos.fX
                + pCarChunkData->pointAy[2].fZ * pCurrentCar->pos.fZ
                - pCarChunkData->pointAy[3].fZ;
              iYaw3 = pCurrentCar->nActualYaw;
              dCurrentCarSpeedCos = pCurrentCar->fFinalSpeed * tcos[iYaw3];
              dCurrentCarSpeedSin = pCurrentCar->fFinalSpeed * tsin[iYaw3];
              fCarVelX = (float)(pCarChunkData->pointAy[0].fY * dCurrentCarSpeedSin + pCarChunkData->pointAy[0].fX * dCurrentCarSpeedCos);
              fCarVelY = (float)(pCarChunkData->pointAy[1].fX * dCurrentCarSpeedCos + pCarChunkData->pointAy[1].fY * dCurrentCarSpeedSin);
              fCarVelZ = (float)(dCurrentCarSpeedCos * pCarChunkData->pointAy[2].fX + dCurrentCarSpeedSin * pCarChunkData->pointAy[2].fY);
            }
            fRelativeX = fCarPosX - fX;         // Calculate relative position vector from listener to car
            fRelativeY = fCarPosY - fY;
            fRelativeZ = fCarPosZ - fZ;
            fHorizontalDistance = fRelativeX * fRelativeX + fRelativeY * fRelativeY;// Calculate horizontal and total 3D distances
            fTotalDistance = fRelativeZ * fRelativeZ + fHorizontalDistance;
            fHorizontalDistance = (float)sqrt(fHorizontalDistance);
            fTotalDistance = (float)sqrt(fTotalDistance);
            iHorizontalAngle = getangle(fRelativeX, fRelativeY);// Calculate horizontal and vertical angles for 3D positioning
            iVerticalAngle = getangle(fHorizontalDistance, fRelativeZ);
            fListenerDopplerVel = (fListenerDirX * tcos[iHorizontalAngle] + fListenerDirY * tsin[iHorizontalAngle]) * tcos[iVerticalAngle]
              + fListenerDirZ * tsin[iVerticalAngle];// Calculate Doppler velocities for listener and car
            fCarDopplerVel = -(fCarVelX * tcos[iHorizontalAngle] + fCarVelY * tsin[iHorizontalAngle]) * tcos[iVerticalAngle] + fCarVelZ * tsin[iVerticalAngle];
            if (DriveView[0] == 3 || DriveView[0] == 6) {
              iStereoVolume = 0x8000;
            } else {
              fTransformedX = fRelativeX * fOrientMatrix_XX + fRelativeY * fOrientMatrix_YX + fRelativeZ * fOrientMatrix_ZZ;// Calculate stereo position using camera orientation matrix
              fTempVolumeCalc = fRelativeX * fOrientMatrix_XY + fRelativeY * fOrientMatrix_YY + fRelativeZ * fOrientMatrix_ZX;
              dVolumeCalc = (1.0 - tsin[getangle(fTransformedX, fTempVolumeCalc)]) * 32768.0;
              //_CHP();
              iStereoVolume = (int)dVolumeCalc;
              if ((int)dVolumeCalc >= 0x10000)
                iStereoVolume = 0xFFFF;
            }
            enginesound(iCarIndex, fListenerDopplerVel, fCarDopplerVel, fTotalDistance, iStereoVolume);// Output final engine sound with calculated parameters
          }
        }
        ++iCarIndex;
        ++pCurrentCar;
      } while (iCarIndex < numcars);
    }
    ++delaywrite;                               // Advance to next delay buffer frame for next iteration
  }
}

//-------------------------------------------------------------------------------------------------
//0003D990
void loopsample(int iCarIdx, int iSample, int iVolume, int iPitch, int iPan)
{
  //int iCarOffset; // ebx
  //int iCarOffset2; // ebx
  unsigned int iSampleHandle; // edx
  int iVolumeFixed; // esi
  int iPitchFixed; // ecx
  int iCarIdx2; // ebp
  int iPanFixed; // [esp+14h] [ebp+4h]

  if (soundon && SamplePtr[iSample]) {
    //iCarOffset = 4 * iCarIdx + (iSample << 6);
    // *(int *)((char *)SampleHandleCar[0].handles + iCarOffset) = SampleHandleCar[iSampleIdx].handles[iCarIdx];
    if (SampleHandleCar[iSample].handles[iCarIdx] == -1 || iVolume) {
      //iCarOffset2 = (iSample << 6) + 4 * iCarIdx;
      iSampleHandle = SampleHandleCar[iSample].handles[iCarIdx];
      if (iSampleHandle == -1) {
        // Start new sample playback
        if (iVolume)
          sample2(iCarIdx, iSample, iVolume, iPitch, iPan, SampleHandleCar[iSample].handles[iCarIdx]);
      } else if (iVolume) {
        // Clamp pitch value between 0x800 and 0x80000
        if (iPitch < 0x800)
          iPitch = 0x800;
        if (iPitch > 0x80000)
          iPitch = 0x80000;

        // Convert parameters to fixed-point format
        iVolumeFixed = iVolume >> 10;           // convert to 10.6 fixed-point
        iPitchFixed = iPitch >> 10;             // convert to 10.6 fixed-point
        iPanFixed = iPan >> 12;                 // convert to 4.12 fixed-point

        // Update volume if changed
        if (iVolumeFixed != lastvolume[iCarIdx]) {
          DIGISetSampleVolume(SampleHandleCar[iSample].handles[iCarIdx], (int16)iVolumeFixed << 10);
          lastvolume[iCarIdx] = iVolumeFixed;
        }

        // Update pitch if changed
        if (iPitchFixed != lastpitch[iCarIdx]) {
          DIGISetPitch(SampleHandleCar[iSample].handles[iCarIdx], iPitchFixed << 10);
          lastpitch[iCarIdx] = iPitchFixed;
        }

        // Update pan if changed
        iCarIdx2 = iCarIdx;
        if (iPanFixed != lastpan[iCarIdx2]) {
          DIGISetPanLocation(SampleHandleCar[iSample].handles[iCarIdx2], (int16)iPanFixed << 12);
          lastpan[iCarIdx2] = iPanFixed;
        }
      } else {
        DIGIStopSample(iSampleHandle);
        HandleSample[SampleHandleCar[iSample].handles[iCarIdx]] = -1;
        SampleHandleCar[iSample].handles[iCarIdx] = -1;
      }
    } else {
      // Stop playing sample
      DIGIStopSample(SampleHandleCar[iSample].handles[iCarIdx]);
      HandleSample[SampleHandleCar[iSample].handles[iCarIdx]] = -1;
      SampleHandleCar[iSample].handles[iCarIdx] = -1;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003DB40
void enginesound(int iCarIdx, float fListenerDopplerVel, float fCarDopplerVel, float fDistance, int iStereoVolume)
{
  double dVolumeCalc; // st7
  double dBasePitch; // st7
  double dWheelSpinPitch; // st7
  double dFinalEnginePitch; // st7
  int iCurrChunk; // edx
  int iPrevChunkIdx; // edx
  int iNextChunkIdx; // edx
  double dTunnelDistanceTemp; // st7
  float fMinTunnelDistance; // eax
  int iCarOffset; // edx
  int iYaw; // edx
  int iYaw3; // ebx
  int iYawDifference; // eax
  int iAbsYawDifference; // eax
  double dSkidVolumeBase; // st7
  double dSkidVolumeWithSpeed; // st7
  double dFinalSkidVolume; // st7
  //int iCarStructOffset; // eax
  double dSkidPitchCalc; // st7
  //int iPendingSampleOffset; // ecx
  //int iCarIndexCopy; // ebp
  //int iPendingCounter; // esi
  //int i; // ecx
  //double dSampleVolume; // st7
  float fTunnelDistance; // [esp+Ch] [ebp-4Ch]
  float fDopplerFactor; // [esp+10h] [ebp-48h]
  int iTunnelFlag; // [esp+14h] [ebp-44h]
  int iLaneType; // [esp+18h] [ebp-40h]
  float fHealthFactor; // [esp+20h] [ebp-38h]
  int iBaseEngineVolume; // [esp+24h] [ebp-34h]
  int iCalculatedEngineVolume; // [esp+24h] [ebp-34h]
  int iFinalEngineVolume; // [esp+24h] [ebp-34h]
  int iSkidBaseVolume; // [esp+28h] [ebp-30h]
  int iSkidVolumeWithSpeed; // [esp+28h] [ebp-30h]
  int iFinalSkidVolume; // [esp+28h] [ebp-30h]
  int iFinalPitch; // [esp+2Ch] [ebp-2Ch]
  float fDistanceAttenuation; // [esp+30h] [ebp-28h]
  float fTunnelDistanceBack; // [esp+34h] [ebp-24h]
  float fTunnelDistanceForward; // [esp+38h] [ebp-20h]
  //int iPendingSampleVolume; // [esp+3Ch] [ebp-1Ch]

  if (soundon) {
    fDopplerFactor = (fListenerDopplerVel + 1476.0f) / (1476.0f - fCarDopplerVel);// Calculate Doppler effect factor (sound speed / (sound speed - source velocity))
    fDistanceAttenuation = 65536000.0f / (fDistance * fDistance + 65536000.0f);// Calculate distance attenuation factor (inverse square law with minimum)
    if (Car[iCarIdx].fRPMRatio >= 0.1)        // Calculate base engine volume based on RPM ratio
    {
      iBaseEngineVolume = 258 * EngineVolume;
    } else {
      dVolumeCalc = Car[iCarIdx].fRPMRatio * 258.0 * 10.0 * (double)EngineVolume;
      //_CHP();
      iBaseEngineVolume = (int)dVolumeCalc;
    }


    //********************************
    //temporary hack added by ROLLER to enable engine sounds during replays until we can figure out
    //how iEngineState is set during replays
    //iCalculatedEngineVolume = (Car[iCarIdx].iEngineState * iBaseEngineVolume) / 8; //original line
    if (replaytype == 2) {
      iCalculatedEngineVolume = iBaseEngineVolume;
    } else {
      iCalculatedEngineVolume = (Car[iCarIdx].iEngineState * iBaseEngineVolume) / 8; //original line
    }
    //********************************

    
    //iCalculatedEngineVolume = (Car[iCarIdx].iEngineState * iBaseEngineVolume
    //                         - (__CFSHL__((Car[iCarIdx].iEngineState * iBaseEngineVolume) >> 31, 3)
    //                            + 8 * ((Car[iCarIdx].iEngineState * iBaseEngineVolume) >> 31))) >> 3;// Apply engine state modifier to volume (divide by 8)
    if (iCalculatedEngineVolume > 0x7FFF)
      iCalculatedEngineVolume = 0x7FFF;
    //_CHP();
    iFinalEngineVolume = (int)((double)iCalculatedEngineVolume * fDistanceAttenuation);
    if (iFinalEngineVolume < 256)
      iFinalEngineVolume = 0;
    if (fDistance >= 1048576.0)
      iFinalEngineVolume = 0;
    dBasePitch = Car[iCarIdx].fRPMRatio * 100000.0 + 8192.0;// Calculate engine pitch: base RPM + wheel spin effect
    //_CHP();
    dWheelSpinPitch = Car[iCarIdx].fWheelSpinAccumulation * 50000.0 / (Car[iCarIdx].fWheelSpinAccumulation + 2000.0) + (double)(int)dBasePitch;
    //_CHP();
    fHealthFactor = (Car[iCarIdx].fHealth + 34.0f) * 0.01f;// Calculate health factor for engine roughness (damaged cars sound rougher)
    if (fHealthFactor > 1.0)
      fHealthFactor = 1.0;
    dFinalEnginePitch = tsin[Car[iCarIdx].iEngineVibrateOffset] * (1.0 - fHealthFactor) * 10000.0 + (double)(6 * Car[iCarIdx].iPitchCameraOffset + (int)dWheelSpinPitch);// Add engine vibration/roughness based on health and camera offset
    //_CHP();
    //_CHP();
    iTunnelFlag = 0;
    iCurrChunk = Car[iCarIdx].nCurrChunk;
    iLaneType = Car[iCarIdx].iLaneType;
    iFinalPitch = (int)(dFinalEnginePitch * fDopplerFactor);
    if (iCurrChunk != -1 && (TrakColour[iCurrChunk][iLaneType] & SURFACE_FLAG_ECHO) != 0)// Check if car is in tunnel section (bit 29 of TrakColour)
    {
      iTunnelFlag = -1;
      fTunnelDistanceBack = localdata[iCurrChunk].fTrackHalfLength + Car[iCarIdx].pos.fX;// Calculate tunnel distance going backward through track
      iPrevChunkIdx = iCurrChunk - 1;
      if (!iPrevChunkIdx)
        iPrevChunkIdx = TRAK_LEN - 1;

      //iPrevChunkIdx check added by ROLLER
      while (iPrevChunkIdx >= 0 && (TrakColour[iPrevChunkIdx][iLaneType] & SURFACE_FLAG_ECHO) != 0 && fTunnelDistanceBack < 6400.0) {
        fTunnelDistanceBack = 2.0f * localdata[iPrevChunkIdx--].fTrackHalfLength + fTunnelDistanceBack;
        if (!iPrevChunkIdx)
          iPrevChunkIdx = TRAK_LEN - 1;
      }
      iNextChunkIdx = Car[iCarIdx].nCurrChunk + 1;// Calculate tunnel distance going forward through track
      fTunnelDistanceForward = localdata[Car[iCarIdx].nCurrChunk].fTrackHalfLength - Car[iCarIdx].pos.fX;
      if (iNextChunkIdx == TRAK_LEN)
        iNextChunkIdx ^= TRAK_LEN;
      while ((TrakColour[iNextChunkIdx][iLaneType] & SURFACE_FLAG_ECHO) != 0 && fTunnelDistanceForward < 6400.0) {
        dTunnelDistanceTemp = 2.0 * localdata[iNextChunkIdx++].fTrackHalfLength + fTunnelDistanceForward;
        fTunnelDistanceForward = (float)dTunnelDistanceTemp;
        if (iNextChunkIdx == TRAK_LEN)
          iNextChunkIdx ^= TRAK_LEN;
      }
      if (fTunnelDistanceForward >= (double)fTunnelDistanceBack)
        fMinTunnelDistance = fTunnelDistanceBack;
      else
        fMinTunnelDistance = fTunnelDistanceForward;
      fTunnelDistance = fMinTunnelDistance;
      if (fMinTunnelDistance > 6400.0)
        fTunnelDistance = 6400.0;
    }
    iCarOffset = 32 * iCarIdx;
    enginedelay[iCarIdx].engineSoundData[delaywritex].iPan = iStereoVolume;// Write engine sound data to delay buffer
    enginedelay[iCarIdx].engineSoundData[delaywritex].iEnginePitch = iFinalPitch;
    enginedelay[iCarIdx].engineSoundData[delaywritex].iEngineVol = iFinalEngineVolume;
    enginedelay[iCarIdx].engineSoundData[delaywritex].iEngine2Pitch = iFinalPitch;
    if (iTunnelFlag) {
      //_CHP();                                   // Apply tunnel reverb effect to secondary engine sound
      enginedelay[iCarIdx].engineSoundData[delaywritex].iEngine2Vol = (int)(fTunnelDistance * 0.6 * 0.00015625 * (double)iFinalEngineVolume);
    } else {
      enginedelay[iCarIdx].engineSoundData[delaywritex].iEngine2Vol = 0;
    }
    if (Car[iCarIdx].iControlType != 3 || (iYaw = Car[iCarIdx].nYaw, iYaw3 = Car[iCarIdx].nActualYaw, iYaw == iYaw3)) {
      iFinalSkidVolume = 0;
    } else {
      iYawDifference = iYaw - iYaw3 + (iYaw - iYaw3 < 0 ? 0x4000 : 0);// Calculate angular difference between input yaw and actual yaw
      if (iYawDifference > 0x2000)
        iYawDifference -= 0x4000;
      iAbsYawDifference = abs(iYawDifference);
      if (iAbsYawDifference >= 200)           // Calculate skid volume based on angular mismatch (understeer/oversteer)
      {
        dSkidVolumeBase = (double)(iAbsYawDifference - 400) * 65536.0 * 4.0 * 0.00006103515625;
        //_CHP();
        iSkidBaseVolume = (int)dSkidVolumeBase;
      } else {
        iSkidBaseVolume = 0;
      }
      dSkidVolumeWithSpeed = (double)iSkidBaseVolume * Car[iCarIdx].fFinalSpeed * 0.0049999999;// Apply speed factor to skid volume (louder at higher speeds)
      //_CHP();
      iSkidVolumeWithSpeed = (int)dSkidVolumeWithSpeed;
      if ((int)dSkidVolumeWithSpeed > 0x7FFF)
        iSkidVolumeWithSpeed = 0x7FFF;
      dFinalSkidVolume = (double)(SFXVolume * iSkidVolumeWithSpeed / 127) * fDistanceAttenuation;
      //_CHP();
      iFinalSkidVolume = (int)dFinalSkidVolume;
      if ((int)dFinalSkidVolume < 256)
        iFinalSkidVolume = 0;
      //_CHP();                                   // DECOMPILER BUG: Missing iCarStructOffset = iCarIdx * 0x134 (sizeof(tCar)=308)
      dSkidPitchCalc = (Car[iCarIdx].fFinalSpeed + 6400.0) * 0.00015625 * (double)(int)(fDopplerFactor * 65536.0);
      //dSkidPitchCalc = (*(float *)((char *)&Car[0].fFinalSpeed + iCarStructOffset) + 6400.0) * 0.00015625 * (double)(int)(fDopplerFactor * 65536.0);
      //_CHP();
      iFinalPitch = (int)dSkidPitchCalc;
    }
    enginedelay[iCarIdx].engineSoundData[delaywritex].iSkid1Pitch = iFinalPitch;// Write tire skid sound data to delay buffer
    enginedelay[iCarIdx].engineSoundData[delaywritex].iSkid1Vol = iFinalSkidVolume;

    // Process pending sound effects for this car
    int iPendingCount = Pending[iCarIdx];
    // Process each pending sample for this car
    for (int iSampleIndex = 0; iSampleIndex < iPendingCount; iSampleIndex++) {
      int iSampleHandle = SamplePending[iCarIdx][iSampleIndex].iHandle;
      int iSampleVolume = SamplePending[iCarIdx][iSampleIndex].iVolume;
      // Clamp volume to maximum
      if (iSampleVolume > 0x8000) {
        iSampleVolume = 0x8000;
      }
      // Apply distance attenuation
      double dAttenuatedVolume = (double)iSampleVolume * fDistanceAttenuation;
      // Output sample if loud enough
      if ((int)dAttenuatedVolume >= 256) {
        pannedsample(iSampleHandle, (int)dAttenuatedVolume, iStereoVolume);
      }
    }
    // Clear pending sample count for next frame
    Pending[iCarIdx] = 0;

    //iPendingSampleOffset = 5 * iCarIdx;         // Process pending sound effects for this car
    //iCarIndexCopy = iCarIdx;
    //iPendingCounter = 0;
    //for (i = 8 * iPendingSampleOffset; iPendingCounter < Pending[iCarIndexCopy]; i += 2) {
    //  iPendingSampleVolume = SamplePending[0][i + 1];
    //  if (iPendingSampleVolume > 0x8000)
    //    iPendingSampleVolume = 0x8000;
    //  dSampleVolume = (double)iPendingSampleVolume * fDistanceAttenuation;// Apply distance attenuation to sample and output if loud enough
    //  //_CHP();
    //  if ((int)dSampleVolume >= 256)
    //    pannedsample(SamplePending[0][i], (int)dSampleVolume, iStereoVolume);
    //  ++iPendingCounter;
    //}
    //Pending[iCarIndexCopy] = 0;                 // Clear pending sample count for next frame
  }
}

//-------------------------------------------------------------------------------------------------
//0003E1C0
void startmusic(int iSong)
{
  int musicOrTitle = 0;
  if (MusicCD && track_playing) {
    StopTrack();
  } else if (MusicCard && SongPtr) {
    stop();
    // sosMIDIUnInitSong(SongHandle);
    SongPtr = 0;
  }

  if (musicon)
    MIDISetMasterVolume(MusicVolume);

  if (MusicCD)
    SetAudioVolume(MusicVolume);

  int iMusic; // Index of the music track to play
  if (iSong >= 0) {
    musicOrTitle = -1;
    iMusic = (nummusictracks + iSong - 1) % nummusictracks;
  } else {
    iMusic = -iSong;
  }

  // int audioInfo;
  int cdSongId; // Track ID for CD audio
  if (MusicCD && musicon) {
    ResetDrive();
    GetAudioInfo();
    if (cd_cheat) {
      cdSongId = cd_cheat;
    } else {
      if (musicOrTitle) {
        PlayTrack4(CDSong[iMusic]);
        start_cd = frames;
        return;
      }
      cdSongId = CDSong[iMusic];
    }
    PlayTrack(cdSongId);
    start_cd = frames;
    return;
  }

  if (musicon) {
    uint32_t musiclength;
    loadfile((const char *)&Song[GMSong[iMusic]], (void *)&musicbuffer, &musiclength, 0);
    SongPtr = musicbuffer;
    if (&musicbuffer) {
      // Init song in the MIDI system
      tInitSong InitSong = {
        .pData = (void *)musicbuffer,
        .iLength = musiclength,
      };
      MIDIInitSong(&InitSong);
      fre((void **)&musicbuffer);

      // Play the song in the MIDI system
      MIDIStartSong();
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003E370
void stopmusic()
{
  if (MusicCD && track_playing) {
    StopTrack();
  } else if (MusicCard && SongPtr) {
    if (MusicCard) {
      if (SongPtr) {
        //sosMIDIStopSong(SongHandle);
        //sosMIDIResetSong(SongHandle);
        MIDIStopSong();
      }
    }
    //sosMIDIUnInitSong(*(unsigned int *)&SongHandle);
    SongPtr = 0;
  }
}

//-------------------------------------------------------------------------------------------------
//0003E3E0
void load_language_map()
{
  FILE *pFile;
  int i;

  // open file
  pFile = ROLLERfopen("LANGUAGE.INI", "r");
  if (!pFile) return;

  // read number of languages into languages
  readline(pFile, "i", &languages);

  // check if languages is valid
  if (languages <= 0) {
    fclose(pFile);
    return;
  }

  // Set up pointers to global buffers
  char *szLangPtr = lang[0];
  char *szTextExtPtr = TextExt;
  char *szSampleExtPtr = SampleExt;

  // loop through each language and read settings
  for (i = 0; i < languages; i++) {
    readline(pFile, "ccc", szLangPtr, szTextExtPtr, szSampleExtPtr);

    // Move pointers forward to next slots
    szLangPtr += 32;        // Each lang entry is 32 bytes
    szTextExtPtr += 4;       // Each extension is 4 bytes
    szSampleExtPtr += 4;
  }

  // close the file
  fclose(pFile);
}

//-------------------------------------------------------------------------------------------------
//0003E460
void initmusic()
{
  FILE *pFile = ROLLERfopen("SOUND.INI", "r");
  if (!pFile) return;

  readline(pFile, "i", &numsamples);
  int iSongCount = 0;
  readline(pFile, "i", &iSongCount);
  readline(pFile, "i", &nummusictracks);

  // read sample filenames
  for (int i = 0; i < numsamples; i++) {
    char szBuf[20];
    readline(pFile, "c", szBuf);

    // add extension ".RAW"
    char *szEnd = &szBuf[strlen(szBuf)];
    if (szEnd) strcpy(szEnd, ".RAW");

    strncpy(Sample[i], szBuf, 15);
    Sample[i][14] = '\0';
  }

  // read song filenames
  for (int i = 0; i < iSongCount; i++) {
    char szBuf[20];
    readline(pFile, "c", szBuf);

    // add extension ".HMP"
    char *szEnd = &szBuf[strlen(szBuf)];
    if (szEnd) strcpy(szEnd, ".HMP");

    strncpy(Song[i], szBuf, 15);
    Song[i][14] = '\0';
  }

  // read GMSong/CDSong values for each track (+5 entries)
  for (int i = 0; i < nummusictracks + 5; i++) {
    readline(pFile, "ii", &GMSong[i], &CDSong[i]);
  }

  // set special track indices (stored as negative values)
  titlesong = -nummusictracks;
  optionssong = -(nummusictracks + 1);
  leaderboardsong = -(nummusictracks + 2);
  winsong = -(nummusictracks + 3);
  winchampsong = -(nummusictracks + 4);

  fclose(pFile);
}

//-------------------------------------------------------------------------------------------------
//0003E670
void SOSTimerCallbackS7()
{
  ++s7;
}

//-------------------------------------------------------------------------------------------------
//0003E680
void fade_palette(int iTargetBrightness)
{
  int iOriginalTickOn = tick_on;
  int iOriginalTicks = ticks;
  uint32 uiTimerHandle = 0;

  if (iTargetBrightness == 0)
    disable_keyboard();

  if (iTargetBrightness == 32 && soundon) {
    //sosDIGISetMasterVolume(DIGIHandle, 0x7FFF);
    DIGISetMasterVolume(0x7FFF); // Set max volume for sound effects
  }

  int iCurrentBrightness = palette_brightness;

  if (iTargetBrightness == iCurrentBrightness)
    return;

  tick_on = 0;
  s7 = 0;

  if (current_mode != 0) {
    uiTimerHandle = ROLLERAddTimer(70, SDLS7TimerCallback, NULL); //added by ROLLER
  }

  if (iTargetBrightness > iCurrentBrightness) {
      // FADE IN LOOP
    for (int iStep = iCurrentBrightness; iStep <= iTargetBrightness; iStep++) {
      for (int i = 0; i < 256; i++) {
        pal_addr[i].byR = (palette[i].byR * iStep) >> 5;
        pal_addr[i].byB = (palette[i].byB * iStep) >> 5;
        pal_addr[i].byG = (palette[i].byG * iStep) >> 5;
      }

      if (current_mode != 0) {
        //could also do this instead of SDL timer
        //uint32 uiStartTime = SDL_GetTicks();
        //while (SDL_GetTicks() - uiStartTime < 70) {
        //  SDL_Delay(1);
        //}
        int iPrev = s7;
        while (s7 == iPrev); // Wait for timer tick
      } else {
        //wait for retrace
        //aka VSYNC for DOS
      }

      //set dac palette
      g_bPaletteSet = true;
      UpdateSDL();
      UpdateSDLWindow();
    }
  } else {
      // FADE OUT LOOP
    int iVolumeStep = (iTargetBrightness << 15) - iTargetBrightness;

    for (int iStep = iCurrentBrightness; iStep >= iTargetBrightness; iStep--) {
      if (iTargetBrightness == 0 && !holdmusic) {
        if (musicon) {
          //sosMIDISetMasterVolume(((MusicVolume * iStep) >> 5) & 0xFF);
          MIDISetMasterVolume(((MusicVolume * iStep) >> 5) & 0xFF);
        }

        if (soundon) {
          //sosDIGISetMasterVolume(DIGIHandle, (iVolumeStep >> 5));
          DIGISetMasterVolume(iVolumeStep >> 5);
        }

        if (MusicCD) {
          SetAudioVolume(((MusicVolume * iStep) >> 5) & 0xFF);
        }
      }

      for (int i = 0; i < 256; i++) {
        pal_addr[i].byR = (palette[i].byR * iStep) >> 5;
        pal_addr[i].byB = (palette[i].byB * iStep) >> 5;
        pal_addr[i].byG = (palette[i].byG * iStep) >> 5;
      }

      if (current_mode != 0) {
        int iPrevS7 = s7;
        while (s7 == iPrevS7); // Wait for timer tick
      } else {
        //wait for retrace
        //aka VSYNC for DOS
      }

      //set dac palette
      g_bPaletteSet = true;
      UpdateSDL();
      UpdateSDLWindow();

      iVolumeStep -= 0x7FFF;
    }
  }

  if (current_mode != 0) {
    ROLLERRemoveTimer(uiTimerHandle); //added by ROLLER
  }

  //memcpy(pal_addr, palette, 768); //REMOVED by ROLLER (why is this here? causes palette change flicker)
  
  palette_brightness = iTargetBrightness;
  tick_on = iOriginalTickOn;
  ticks = iOriginalTicks;

  if (iTargetBrightness == 0)
    enable_keyboard();

  if (iTargetBrightness == 0 && !holdmusic) {
    if (MusicCD && track_playing)
      StopTrack();
    else if (MusicCard && SongPtr) {
      stop();
      //sosMIDIUnInitSong(SongHandle);
      SongPtr = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003EA00
void set_palette(int iBrightness)
{
  if (iBrightness == palette_brightness)
    return;

  if (iBrightness < 0) iBrightness = 0;
  if (iBrightness > 31) iBrightness = 31;

  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = (palette[i].byR * iBrightness) / 31;
    pal_addr[i].byG = (palette[i].byG * iBrightness) / 31;
    pal_addr[i].byB = (palette[i].byB * iBrightness) / 31;
  }

  // Update the SDL palette
  //SDL_Color sdl_colors[256];
  //
  //for (int i = 0; i < 256; ++i) {
  //  sdl_colors[i].r = pal_addr[i].r;
  //  sdl_colors[i].g = pal_addr[i].g;
  //  sdl_colors[i].b = pal_addr[i].b;
  //  sdl_colors[i].a = 255;
  //}
  //SDL_SetPaletteColors(sdl_palette, sdl_colors, 0, 256);

  palette_brightness = iBrightness;
}

//-------------------------------------------------------------------------------------------------
//0003EAB0
void check_joystick_usage()
{
  int i; // eax
  uint8 byKey1; // dl
  uint8 byKey2; // cl

  Joy1used = 0;
  Joy2used = 0;
  for (i = 0; i < 12; ++i) {
    byKey1 = userkey[i];
    if (byKey1 == 0x80 || byKey1 == 0x81 || byKey1 >= 0x84u && byKey1 <= 0x87u)
      Joy1used = -1;
    byKey2 = userkey[i];
    if (byKey2 == 0x82 || byKey2 == 0x83 || byKey2 >= 0x88u && byKey2 <= 0x8Bu)
      Joy2used = -1;
  }
}

//-------------------------------------------------------------------------------------------------
//0003EB20
void convertname(char *szFilename)
{
  char szTemp[32];
  char *p = szTemp;
  char *s = szFilename;

  cheatsample = 0;

  // copy filename to temp buffer (word-wise copy, handling 2 bytes at a time)
  while (1) {
    *p++ = *s;
    if (*s++ == '\0') break;
    *p++ = *s;
    if (*s++ == '\0') break;
  }

  // check for cheat sample conversion
  if (cheat_samples && language == 0) {
    char *ext = strstr(szFilename, ".RAW");
    if (ext) {
      ext[1] = 'K';   // Change to ".KC"
      ext[2] = 'C';
      ext[3] = '\0';

      FILE *fp = ROLLERfopen(szFilename, "rb");
      if (fp) {
        cheatsample = -1;
        fclose(fp);
        return;
      }
    }
  }

  // language-specific sample override
  if (language != 0) {
    char *szExt = strstr(szFilename, ".RAW");
    if (szExt) {
      const char *szLangExt = (const char *)SampleExt + language * 4;
      strcpy(szExt + 1, szLangExt);

      FILE *fp = ROLLERfopen(szFilename, "rb");
      if (fp) {
        fclose(fp);
        return;
      }
    }
  }

  // restore original filename from temp if all else fails
  s = szTemp;
  p = szFilename;
  while (1) {
    *p++ = *s;
    if (*s++ == '\0') break;
    *p++ = *s;
    if (*s++ == '\0') break;
  }
}

//-------------------------------------------------------------------------------------------------
//0003EC40
void decode(uint8 *pData, int iLength, uint32 uiStep, uint32 uiOffset)
{
  int i; // edx
  uint8 byOriginal; // bh
  uint8 byXorValue; // bl
  uint32 uiNextOffset; // [esp+0h] [ebp-Ch]

  for (i = 0; i < iLength; *(pData - 1) = byXorValue ^ byOriginal) {
    ++i;
    byOriginal = *pData;
    uiNextOffset = uiOffset + uiStep;
    ++pData;
    byXorValue = uiOffset + uiStep;
    uiStep = uiOffset;
    uiOffset = uiNextOffset;
  }
}

//-------------------------------------------------------------------------------------------------
//0003EC70
void loadasample(int iIndex)
{
  char szFilenameBuf[32];

  // construct sample filename
  char *szSrc = Sample[iIndex];
  char *szDst = szFilenameBuf;
  while (*szSrc) {
    *szDst++ = *szSrc++;
    if (!*szSrc) break;
    *szDst++ = *szSrc++;
  }
  *szDst = '\0';

  // convert sample name to real path or cheat version
  convertname(szFilenameBuf);
  SDL_Log("loadasample: %s\n", szFilenameBuf);
  // load file into memory
  loadfile(szFilenameBuf, (void**)&SamplePtr[iIndex], &SampleLen[iIndex], 1);

  // check if cheat sample flag is set and process if needed
  if (cheatsample && SamplePtr[iIndex]) {
    uint8 *pData = SamplePtr[iIndex];
    int iLen = SampleLen[iIndex];
    uint32 uiSeed = 0x1C73;
    uint32 uiStep = 0x896;
    int i;

    for (i = 0; i < iLen; ++i) {
      uiSeed += uiStep;
      pData[i] ^= (uint8)uiSeed;
    }
  }

  // if the index is between 79 and 82 apply a second xor pass
  if (iIndex >= 79 && iIndex <= 82 && SamplePtr[iIndex]) {
    uint8 *pData = SamplePtr[iIndex];
    int iLen = SampleLen[iIndex];
    uint32 uiSeed = 0x4D;
    uint32 uiStep = 0x57;

    for (int i = 0; i < iLen; ++i) {
      uiSeed += uiStep;
      pData[i] ^= (uint8)uiSeed;
    }
  }

  // if using 50Hz timer
  if (SamplePtr[iIndex] && (cheat_mode & CHEAT_MODE_50HZ_TIMER)) {
    if (cheat_mode & CHEAT_MODE_100HZ_TIMER) { //100Hz timer
      // halve the sample length, keep only even-indexed bytes
      int iLen = SampleLen[iIndex];
      uint8 *pData = SamplePtr[iIndex];
      for (int i = 0; i < iLen; i += 2) {
        pData[i / 2] = pData[i];
      }
      SampleLen[iIndex] = iLen / 2;
    } else {
      // remove every third byte
      int iLen = SampleLen[iIndex];
      uint8 *pData = SamplePtr[iIndex];
      int iNewLen = 0;
      for (int i = 0; i < iLen; ++i) {
        if (i % 3 != 2) {
          pData[iNewLen++] = pData[i];
        }
      }
      SampleLen[iIndex] = iNewLen;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003EE40
void select8bitdriver()
{
  //TODO: integrate with SDL?
  if (SoundCard) {                                             // If previous driver was type 1, clean up existing audio resources
    if (drivertype == 1) {
      //sosTIMERRemoveEvent(TimerEventHandle);    // Remove existing timer event from SOS system
      //sosDIGIUnInitDriver(DIGIHandle, 1, 1);    // Uninitialize existing digital audio driver
    }
    resetsamplearray();                         // Reset internal sample array/buffer management
    if (drivertype)                           // If any driver type was active, proceed with 8-bit driver initialization
    {
      drivertype = 0;                           // Set driver type to 0 (8-bit mode) and initialize driver configuration structure
      //memset(&InitDriver, 0, sizeof(InitDriver));
      //InitDriver.dwBufferSize = 4096;           // Configure audio parameters: 4096 buffer size, auto-detect (-1), 11025 Hz sample rate
      //InitDriver.dwParam1 = -1;
      //InitDriver.dwSampleRateHz = 11025;
      //Hardware.dwPort = SoundPort;              // Configure hardware settings: I/O port, IRQ, and DMA channel from global settings
      //Hardware.dwIRQ = SoundIRQ;
      //Hardware.dwDMA = SoundDMA;
      //InitDriver.iUnk7 = 0;
      //if (sosDIGIInitDriver(
      //  SoundCard,
      //  0,
      //  (unsigned int)&Hardware,
      //  __DS__,
      //  (unsigned int)&InitDriver,
      //  (unsigned __int16)__DS__,
      //  (unsigned int)&DIGIHandle,
      //  (unsigned __int16)__DS__))        // Initialize SOS digital audio driver with configured parameters
      //{
      //  sosDIGIUnInitSystem();                  // If driver initialization failed, cleanup and disable sound card
      //  SoundCard = 0;
      //} else {
      //  sosTIMERRegisterEvent(300u, 0x7FFF, (int)InitDriver.lpDriverMemoryCS, (__int16)InitDriver.lpTimerMemory, (unsigned int)&TimerEventHandle, (unsigned __int16)__DS__);// Driver initialized successfully: register timer event and set maximum volume
      //  sosDIGISetMasterVolume(DIGIHandle, 0x7FFFu);// Set digital audio master volume to maximum (0x7FFF)
      //}
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003EF50
void resetsamplearray()
{
  //int iNumCars; // ebx
  //int i; // ecx
  //int iCar; // edx
  //int iEngineDelayOffset; // eax
  //int iSampleOffset; // eax
  //unsigned int iSampleHandleCarOffset; // esi
  //int j; // ecx
  //int iHandleOffset; // eax

  for (int i = 0; i < numcars; ++i) {
    for (int j = 0; j < 32; ++j) {
      enginedelay[i].engineSoundData[j].iEngineVol = -1;
      enginedelay[i].engineSoundData[j].iEngine2Vol = -1;
      enginedelay[i].engineSoundData[j].iSkid1Vol = -1;
    }
  }
  //iNumCars = numcars;
  //for (i = 0; i != sizeof(tCarSoundData); i += sizeof(tEngineSoundData)) {
  //  iCar = 0;
  //  if (iNumCars > 0) {
  //    iEngineDelayOffset = i;
  //    do {
  //      *(int *)((char *)&enginedelay[0].engineSoundData[0].iEngineVol + iEngineDelayOffset) = -1;
  //      *(int *)((char *)&enginedelay[0].engineSoundData[0].iEngine2Vol + iEngineDelayOffset) = -1;
  //      *(int *)((char *)&enginedelay[0].engineSoundData[0].iSkid1Vol + iEngineDelayOffset) = -1;
  //      ++iCar;
  //      iEngineDelayOffset += sizeof(tCarSoundData);
  //    } while (iCar < iNumCars);
  //  }
  //}

  for (int i = 0; i < 32; ++i) {
    HandleSample[i] = -1;
    HandleCar[i] = -1;
  }
  //iSampleOffset = 2;
  //HandleSample[0] = -1;
  //HandleCar[0] = -1;
  //HandleSample[1] = -1;
  //HandleCar[1] = -1;
  //do {
  //  iSampleOffset += 5;
  //  HandleCar[iSampleOffset + 27] = -1;         // offset into HandleSample
  //  SamplePending[15][iSampleOffset + 35] = -1; // offset into HandleCar
  //  HandleCar[iSampleOffset + 28] = -1;
  //  SamplePending[15][iSampleOffset + 36] = -1;
  //  HandleCar[iSampleOffset + 29] = -1;
  //  SamplePending[15][iSampleOffset + 37] = -1;
  //  HandleCar[iSampleOffset + 30] = -1;
  //  SamplePending[15][iSampleOffset + 38] = -1;
  //  HandleCar[iSampleOffset + 31] = -1;
  //  SamplePending[15][iSampleOffset + 39] = -1;
  //} while (iSampleOffset != 32);

  for (int i = 0; i < 120; ++i) {
    for (int j = 0; j < 16; ++j) {
      SampleHandleCar[i].handles[j] = -1;
    }
  }
  //iSampleHandleCarOffset = sizeof(tSampleHandleCar);
  //for (j = 0; j < 120; ++j) {
  //  iHandleOffset = j << 6;
  //  do {
  //    iHandleOffset += 4;
  //    *(uint8 **)((char *)&SamplePtr[119] + iHandleOffset) = (uint8 *)-1;// offset into SampleHandleCar
  //  } while (iHandleOffset != iSampleHandleCarOffset);
  //  iSampleHandleCarOffset += sizeof(tSampleHandleCar);
  //}
  //numcars = iNumCars;
}

//-------------------------------------------------------------------------------------------------
//0003F050
void reinitmusic()
{
  int iSong; // eax

  if (musicon) {
    if (frontend_on) {
      iSong = optionssong;
    } else if (replaytype == 2) {
      iSong = titlesong;
    } else {
      iSong = TrackLoad;
    }
    startmusic(iSong);
  } else if (MusicCD && track_playing) {
    StopTrack();
  } else if (MusicCard) {
    if (SongPtr) {
      stop();
      //sosMIDIUnInitSong(*(unsigned int *)&SongHandle);
      SongPtr = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003F0D0
void waitsampledone(int iSampleIdx)
{
  if (!soundon)
    return;

  // Check sample pointer is valid
  if (SamplePtr[iSampleIdx] == 0)
    return;

  for (int i = 0; i < 16; i++) {
    int iSampleHandle = SampleHandleCar[iSampleIdx].handles[i];
    while (!DIGISampleDone(iSampleHandle)) {
      UpdateSDL(); //added by ROLLER
    }
  }

  return;
}

//-------------------------------------------------------------------------------------------------
//0003F110
int getcompactedfilelength(const char *szFile)
{
  FILE *pFile; // esi
  int iLength; // [esp+0h] [ebp-14h] BYREF

  pFile = ROLLERfopen(szFile, "rb");
  if (!pFile) ErrorBoxExit("Could not open file %s", szFile);
  fread(&iLength, 1u, 4u, pFile);
  fclose(pFile);
  return iLength;
}

//-------------------------------------------------------------------------------------------------
//0003F150
int initmangle(const char *szFile)
{
  unmangleinpoff = 4;
  unmanglebufpos = 4;
  unmangleoverflow = 0;
  unmanglefile = ROLLERfopen(szFile, "rb");
  if (!unmanglefile) ErrorBoxExit("Could not open file %s", szFile);
  fseek(unmanglefile, unmanglebufpos, 0);
  return (int)fread(unmangleinbuf, 1u, 0x400u, unmanglefile);
}

//-------------------------------------------------------------------------------------------------
//0003F1B0
int uninitmangle()
{
  return fclose(unmanglefile);
}

//-------------------------------------------------------------------------------------------------
//0003F1C0
int loadcompactedfile(const char *szFile, uint8 *pBuf)
{
  initmangle(szFile);
  loadcompactedfilepart(pBuf, 1000000000u);
  return fclose(unmanglefile);
}

//-------------------------------------------------------------------------------------------------
//0003F1E0
void readmangled(uint8 *pBufRet, int iLength)
{
  int iLengthToRead; // ebp
  uint8 *pBufUnit8; // ebx
  int iOverflow; // ecx
  int iDelta; // esi

  iLengthToRead = iLength;
  pBufUnit8 = pBufRet + 40000;
  if (unmangleoverflow) {
    memcpy(pBufUnit8, pBufRet, unmangleoverflow);
    iOverflow = unmangleoverflow;
    unmangleoverflow = 0;
    pBufUnit8 += iOverflow;
    iLengthToRead = iLength - iOverflow;
  }
  if (iLengthToRead > 0) {
    loadcompactedfilepart(pBufUnit8, iLengthToRead);
    iDelta = (int)(unmangledst - pBufUnit8);
    if (unmangledst - pBufUnit8 > iLengthToRead) {
      unmangleoverflow = iDelta - iLengthToRead;
      memcpy(pBufRet, &pBufUnit8[iLengthToRead], iDelta - iLengthToRead);
    }
    memcpy(pBufRet + 20000, &pBufUnit8[iLengthToRead - 20000], 0x4E20u);
  }
}

//-------------------------------------------------------------------------------------------------
//0003F290
void loadcompactedfilepart(uint8 *pDest, uint32 uiDestLength)
{
  int c;
  int u, v;
  unsigned char *pt;;

  // Do the unmangle
  unmangledst = pDest;
  for (;;) {
    if (unmangledst - pDest >= (int)uiDestLength)
      break;

    c = *unmangleGet(unmangleinpoff++, 1);
    if (!c) break;		// done

    if (c & 128) {
            // Bit 7 : 1 : BLOCK
      if (c & 64) {
              // Bit 6 : 1 Block size
        if (c & 32) {
                // Bit 5 : 1 Long
          pt = unmangledst - ((c & 31) << 8) - *unmangleGet(unmangleinpoff++, 1) - 3;	// offset
          c = *unmangleGet(unmangleinpoff++, 1) + 5;						// size

          while (c--)
            *unmangledst++ = *pt++;
        } else {
        // Bit 5 : 0 Medium
          pt = unmangledst - ((c & 3) << 8) - *unmangleGet(unmangleinpoff++, 1) - 3;	// offset
          c = ((c >> 2) & 7) + 4;					// size
          while (c--)
            *unmangledst++ = *pt++;

        }
      } else {
      // Bit 6 : 0 : Short
        pt = unmangledst - (c & 63) - 3;
        *unmangledst = *pt;
        *(unmangledst + 1) = *(pt + 1);
        *(unmangledst + 2) = *(pt + 2);
        unmangledst += 3;
      }
    } else {
    // Bit 7 : 0
      if (c & 64) {
              // Bit 6 : 1 Seq/Diff or String
        if (c & 32) {
                // Bit 5 : 1 : Sequence
          if (c & 16) {
          // Bit 4 : 1 : Word sequence
            c = (c & 15) + 2;	// bits 3-0 = len 2->17
            v = *(short *)(unmangledst - 2);
            while (c--) {
              *(short *)unmangledst = (short)v;
              unmangledst += 2;
            }
          } else {
          // Bit 4 : 0 : Byte sequence
            c = (c & 15) + 3;	// bits 3-0 = len 3->18
            v = *(unmangledst - 1);
            while (c--)
              *unmangledst++ = (signed char)v;
          }
        } else {
        // Bit 5 : 0 : Difference
          if (c & 16) {
          // Bit 4 : 1 : Word difference
            c = (c & 15) + 2;			// bits 3-0 = len 2->17
            u = *(short *)(unmangledst - 2);		// start word
            v = u - *(short *)(unmangledst - 4);	// dif
            while (c--) {
              u += v;
              *(short *)unmangledst = (short)u;
              unmangledst += 2;
            }
          } else {
          // Bit 4 : 0 : Byte difference
            c = (c & 15) + 3;	// bits 3-0 = len 3->18
            u = *(unmangledst - 1);		// start byte
            v = u - *(unmangledst - 2);	// dif

            while (c--) {
              u += v;
              *unmangledst++ = (char)u;
            }
          }
        }
      } else {
      // Bit 6 : 0 : String
        c &= 63;	// len
        memcpy(unmangledst, unmangleGet(unmangleinpoff, c), c);
        unmangledst += c;
        unmangleinpoff += c;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0003F550
uint8 *unmangleGet(unsigned int uiPos, unsigned int uiLookahead)
{
  // if what we want to read is not currently in the
  // buffer load it into the buffer from the file
  if (uiPos < (uint32)unmanglebufpos || uiPos + uiLookahead >(uint32)unmanglebufpos + 1024) {
    unmanglebufpos = uiPos;
    fseek(unmanglefile, uiPos, 0);
    fread(unmangleinbuf, 1u, 0x400u, unmanglefile);
  }
  return &unmangleinbuf[uiPos - (uint32)unmanglebufpos];// return new pos
}

//-------------------------------------------------------------------------------------------------