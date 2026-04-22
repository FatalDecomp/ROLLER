#ifndef _ROLLER_SOUND_H
#define _ROLLER_SOUND_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#ifdef IS_WINDOWS
#include <windows.h>
#endif
//-------------------------------------------------------------------------------------------------

#define SOUND_SAMPLE_ENGINE 0
#define SOUND_SAMPLE_ENGINE2 1
#define SOUND_SAMPLE_BIGCRASH 2
#define SOUND_SAMPLE_SKID1 3
#define SOUND_SAMPLE_GO 4
#define SOUND_SAMPLE_GRSHIFT 5
#define SOUND_SAMPLE_EXPLO 6
#define SOUND_SAMPLE_FENDER 7
#define SOUND_SAMPLE_FENDER2 8
#define SOUND_SAMPLE_LANDSKID 9
#define SOUND_SAMPLE_LIGHTLAN 10
#define SOUND_SAMPLE_BANK 11
#define SOUND_SAMPLE_WALL1 12
#define SOUND_SAMPLE_ROOF 13
#define SOUND_SAMPLE_FATALITY 14
#define SOUND_SAMPLE_KEEPGO 15
#define SOUND_SAMPLE_FINAL1 16
#define SOUND_SAMPLE_FINAL2 17
#define SOUND_SAMPLE_FINAL3 18
#define SOUND_SAMPLE_YEAHHAH 19
#define SOUND_SAMPLE_AAARG 20
#define SOUND_SAMPLE_GOTONE 21
#define SOUND_SAMPLE_WINNER 22
#define SOUND_SAMPLE_HARDER 23
#define SOUND_SAMPLE_RUBBISH 24
#define SOUND_SAMPLE_POOR 25
#define SOUND_SAMPLE_REVERSE 26
#define SOUND_SAMPLE_BLOWTIME 27
#define SOUND_SAMPLE_ESTART 28
#define SOUND_SAMPLE_FALSE 29
#define SOUND_SAMPLE_RACEOVER 30
#define SOUND_SAMPLE_YEAHLOOP 31
#define SOUND_SAMPLE_YEAHCORK 32
#define SOUND_SAMPLE_LOOPCOME 33
#define SOUND_SAMPLE_CORKCOME 34
#define SOUND_SAMPLE_TOOSLOW 35
#define SOUND_SAMPLE_TOOFAST 36
#define SOUND_SAMPLE_RECORD 37
#define SOUND_SAMPLE_SMASH 38
#define SOUND_SAMPLE_BEST 39
#define SOUND_SAMPLE_FAST 40
#define SOUND_SAMPLE_MENDING 41
#define SOUND_SAMPLE_FIXED 42
#define SOUND_SAMPLE_2LEFT 43
#define SOUND_SAMPLE_1LEFT 44
#define SOUND_SAMPLE_0LEFT 45
#define SOUND_SAMPLE_PITIN 46
#define SOUND_SAMPLE_HURTME 47
#define SOUND_SAMPLE_HURTYOU 48
#define SOUND_SAMPLE_ENGINES 49
#define SOUND_SAMPLE_DRIVERS 50
#define SOUND_SAMPLE_LAPPED 51
#define SOUND_SAMPLE_UNLAPPED 52
#define SOUND_SAMPLE_2LAPS 53
#define SOUND_SAMPLE_3LAPS 54
#define SOUND_SAMPLE_4LAPS 55
#define SOUND_SAMPLE_5LAPS 56
#define SOUND_SAMPLE_WIN 57
#define SOUND_SAMPLE_BLOCK 58
#define SOUND_SAMPLE_SLOW 59
#define SOUND_SAMPLE_BASH 60
#define SOUND_SAMPLE_WIN2 61
#define SOUND_SAMPLE_BLOCK2 62
#define SOUND_SAMPLE_SLOW2 63
#define SOUND_SAMPLE_BASH2 64
#define SOUND_SAMPLE_REJECT1 65
#define SOUND_SAMPLE_REJECT2 66
#define SOUND_SAMPLE_TGONE 67
#define SOUND_SAMPLE_TDAMAGE 68
#define SOUND_SAMPLE_REVERST 69
#define SOUND_SAMPLE_TPITS 70
#define SOUND_SAMPLE_ARIEL 71
#define SOUND_SAMPLE_DESILVA 72
#define SOUND_SAMPLE_PULSE 73
#define SOUND_SAMPLE_GLOBAL 74
#define SOUND_SAMPLE_MILLION 75
#define SOUND_SAMPLE_MISSION 76
#define SOUND_SAMPLE_ZIZIN 77
#define SOUND_SAMPLE_REISE 78
#define SOUND_SAMPLE_BRP 79
#define SOUND_SAMPLE_UP 80
#define SOUND_SAMPLE_BOING 81
#define SOUND_SAMPLE_BLOP 82
#define SOUND_SAMPLE_BUTTON 83
#define SOUND_SAMPLE_CARIN 84
#define SOUND_SAMPLE_CAROUT 85
#define SOUND_SAMPLE_TRACK 86
#define SOUND_SAMPLE_START 87
#define SOUND_SAMPLE_FATAL 88
#define SOUND_SAMPLE_1RACE 89
#define SOUND_SAMPLE_2RACE 90
#define SOUND_SAMPLE_3RACE 91
#define SOUND_SAMPLE_4RACE 92
#define SOUND_SAMPLE_5RACE 93
#define SOUND_SAMPLE_6RACE 94
#define SOUND_SAMPLE_7RACE 95
#define SOUND_SAMPLE_8RACE 96
#define SOUND_SAMPLE_ONE 97
#define SOUND_SAMPLE_TWO 98
#define SOUND_SAMPLE_THREE 99
#define SOUND_SAMPLE_FOUR 100
#define SOUND_SAMPLE_FIVE 101
#define SOUND_SAMPLE_SIX 102
#define SOUND_SAMPLE_SEVEN 103
#define SOUND_SAMPLE_EIGHT 104
#define SOUND_SAMPLE_NINE 105
#define SOUND_SAMPLE_TEN 106
#define SOUND_SAMPLE_ELEVEN 107
#define SOUND_SAMPLE_TWELVE 108
#define SOUND_SAMPLE_THIRTEEN 109
#define SOUND_SAMPLE_FOURTEEN 110
#define SOUND_SAMPLE_FIFTEEN 111
#define SOUND_SAMPLE_SIXTEEN 112
#define SOUND_SAMPLE_FATLOTS 113
#define SOUND_SAMPLE_WON 114
#define SOUND_SAMPLE_STAT 115
#define SOUND_SAMPLE_NEWLAP 116
#define SOUND_SAMPLE_NEWFAST 117
#define SOUND_SAMPLE_CONGRAT 118

//-------------------------------------------------------------------------------------------------

#define BUTTON_FLAG_ACCEL       0x0001
#define BUTTON_FLAG_BRAKE       0x0002
#define BUTTON_FLAG_UPGEAR      0x0004
#define BUTTON_FLAG_DOWNGEAR    0x0008
#define BUTTON_FLAG_SPECIAL     0x0020
#define BUTTON_FLAG_F1          0x0040
#define BUTTON_FLAG_F2          0x0080
#define BUTTON_FLAG_F3          0x0100
#define BUTTON_FLAG_F4          0x0200
#define BUTTON_FLAG_SLAVE_QUIT  0x0400
#define BUTTON_FLAG_MASTER_QUIT 0x0800

//-------------------------------------------------------------------------------------------------

#define REPLAY_BUFFER_SIZE  512
#define DELAY_BUFFER_SIZE   32

//-------------------------------------------------------------------------------------------------

// Flag constants for copy_multiple data
#define FLAG_DISCONNECT      0x0400
#define FLAG_MASTER_CHANGE   0x0800
#define FLAG_FINISHED        0x1000

//-------------------------------------------------------------------------------------------------

#define SAMPLE_FLAG_PLAYING   0x0020
#define SAMPLE_FLAG_LOOP      0x0040
#define SAMPLE_FLAG_ACTIVE    0x0080

//-------------------------------------------------------------------------------------------------

typedef struct
{
  DWORD edi, esi, ebp, reserved, ebx, edx, ecx, eax;
  WORD  flags, es, ds, fs, gs, ip, cs, sp, ss;
} DPMI_RMI;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int handles[16];
} tSampleHandleCar;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  void *pSample;
  uint16 unSegment;
  int iLength;
  int iByteOffset;
  int iUnk1;
  int iVolume;
  int iSampleIndex;
  int iUnk2;
  int iUnk3;
  int iUnk4;
  int iFlags;
  int iUnk5;
  int iUnk6;
  int iUnk7;
  int iPitch;
  int iUnk8;
  int iPan;
  int unknown2[13];
} tSampleData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  void *pData;
  int nUnk2;
  int iUnk1;
  int iLength; // add by ROLLER: length of the data song in bytes
} tInitSong;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iEnginePitch;   //ENGINE.RAW
  int iEngineVol;
  int iEngine2Pitch;  //ENGINE2.RAW
  int iEngine2Vol;
  int iSkid1Pitch;    //SKID1.RAW
  int iSkid1Vol;
  int iPan;
} tEngineSoundData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tEngineSoundData engineSoundData[32];
} tCarSoundData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iJ1Button1;
  int iJ1Button2;
  int iJ1XAxis;
  int iJ1YAxis;
  int iJ2Button1;
  int iJ2Button2;
  int iJ2XAxis;
  int iJ2YAxis;
} tJoyPos;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint16 unInput;
  uint16 unFlags;
} tCarInputData;

typedef union
{
  tCarInputData data;
  uint32 uiFullData;
} tCopyData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iSampleIdx;
  int iVolume;
  int iDelay;
  int iCarIdx;
} tSpeechInfo;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iHandle;
  int iVolume;
} tSamplePending;

//-------------------------------------------------------------------------------------------------

extern int samplespending;
extern int writesample;
extern int readsample;
extern int lastsample;
extern int musicon;
extern int soundon;
extern int allengines;
extern int cheat_samples;
extern int palette_brightness;
extern int last_inp[2];
extern void *pal_selector;
extern char SourcePath[64];
extern char DestinationPath[64];
extern char languagename[32];
extern int writeptr;
extern int readptr;
extern int SoundCard;
extern int SoundPort;
extern int SoundIRQ;
extern int SoundDMA;
extern int EngineVolume;
extern int SFXVolume;
extern int SpeechVolume;
extern int MusicVolume;
extern int MusicCard;
extern int MusicCD;
extern int MusicPort;
extern int g_iCurrentSong;
extern uint8 *SongPtr;
extern int SongHandle;
extern int CDSong[20];
extern int GMSong[21];
extern tSampleData SampleData;
extern tSampleData SampleFixed;
extern tSampleData SamplePanned;
extern uint8 rud_gr[2];
extern uint8 rud_strat[2];
extern int Joy1used;
extern int Joy2used;
extern int fraction;
extern int x1ok;
extern int y1ok;
extern int x2ok;
extern int y2ok;
extern int bitaccept;
extern uint8 *frontendspeechptr;
extern int frontendspeechhandle;
extern int holdmusic;
extern int drivertype;
extern uint8 unmangleinbuf[1024];
extern int lastvolume[16];
extern int lastpitch[16];
extern int lastpan[16];
extern int net_time[16];
extern int joyvalue[8];
extern tJoyPos rud_Joy_pos;
extern int rud_turn[2];
extern int rud_swheel[2];
extern int rud_steer[2];
extern uint32 SampleLen[120];
extern uint8 *SamplePtr[120];
extern tSampleHandleCar SampleHandleCar[120];
extern char Sample[120][15];
extern char lang[16][32];
extern int TrackMap[32];
extern char TextExt[64];
extern char SampleExt[64];
extern int Pending[16];
extern tSamplePending SamplePending[16][20];
extern int HandleCar[32];
extern int HandleSample[32];
extern tCarSoundData enginedelay[16];
extern int car_to_player[16];
extern int player_to_car[16];
extern tSpeechInfo speechinfo[16];
extern int load_times[16];
extern tCopyData copy_multiple[512][16];
extern int unmangleinpoff;
extern uint8 *unmangledst;
extern int unmangleoverflow;
extern FILE *unmanglefile;
extern int unmanglebufpos;
extern volatile int s7;
extern int network_timeout;
extern void *MT32Data;
extern void *FMDrums;
extern void *FMInstruments;
extern int network_sync_error;
extern int ticks_received;
extern int network_limit;
extern int MIDIHandle;
extern int DIGIHandle;
extern volatile int frames;
extern char Song[20][15];
extern uint32 tickhandle;
extern DPMI_RMI RMI;
extern tColor *pal_addr;
extern int user_inp;
extern int nummusictracks;
extern int winchampsong;
extern int winsong;
extern int delaywritex;
extern int delayreadx;
extern int leaderboardsong;
extern int optionssong;
extern int titlesong;
extern int delaywrite;
extern int delayread;
extern int numsamples;
extern int cheatsample;
extern int languages;
extern int net_loading;
extern int already_quit;
extern int network_error;

//-------------------------------------------------------------------------------------------------

void realmode(uint8 byRealModeInterrupt);
bool loadDOS(const char *szFilename, void **out_buffer);
int claimDOS(int iSizeParagraphs, uint32 *uiSelectorOut);
void releaseDOS(uint16 nSegment);
bool setpal(const char *szFilename);
void blankpal();
void resetpal();
void Initialise_SOS();
void updatejoy();
void readuserdata(int iPlayer);
void tickhandler();
void claim_ticktimer(unsigned int uiRateHz);
void release_ticktimer();
void Uninitialise_SOS();
void loadsamples();
void loadfatalsample();
void freefatalsample();
void releasesamples();
void play();
void stop();
void devicespecificinit();
int initgus();
void devicespecificuninit();
void readsoundconfig();
char *FindConfigVar(const char *szConfigText, const char *szVarName);
void loadfile(const char *szFile, void **pBuf, unsigned int *uiSize, int iIsSound);
void ReadJoys(tJoyPos *pJoy);
void check_joystickpresence();
void initsounds();
void stopallsamples();
void pannedsample(int iSampleIdx, int iHandle, int iPan);
void speechonly(int iSampleIdx, int iVolume, int iDelay, int iCarIdx);
void speechsample(int iSampleIdx, int iVolume, int iDelay, int iCarIdx);
void analysespeechsamples();
void dospeechsample(int iSampleIdx, int iVolume);
void loadfrontendsample(char *fileName);
int frontendsample(int iVol);
void remove_frontendspeech();
int sfxplaying(int iSampleIdx);
int cheatsampleok(int iCarIdx);
void sfxsample(int iSample, int iVol);
void sample2(int iCarIndex, int iSampleIndex, int iVolume, int iPitch, int iPan, int iByteOffset);
void sfxpend(int iSampleIdx, int iDriverIdx, int iVolume);
void enginesounds2(int iPlayer1Car, int iPlayer2Car);
void enginesounds(int iFocusCarIndex);
void loopsample(int iCarIdx, int iSampleIdx, int iVolume, int iPitch, int iPan);
void enginesound(int iCarIdx, float fListenerDopplerVel, float fCarDopplerVel, float fDistance, int iStereoVolume);
void startmusic(int iSong);
void stopmusic();
void load_language_map();
void initmusic();
void SOSTimerCallbackS7();
void fade_palette(int iPaletteBrightness);
void fade_music_start(int iTargetBrightness);
void fade_music_step(int iStep);
void fade_music_finish(int iTargetBrightness);
void set_palette(int iBrightness);
void check_joystick_usage();
void convertname(char *szFilename);
void decode(uint8 *pData, int iLength, uint32 uiStep, uint32 uiOffset);
void loadasample(int iIndex);
void select8bitdriver();
void resetsamplearray();
void reinitmusic();
void waitsampledone(int iSampleIdx);
int getcompactedfilelength(const char *szFile);
int initmangle(const char *szFile);
int uninitmangle();
int loadcompactedfile(const char *szFile, uint8 *pBuf);
void readmangled(uint8 *pBufRet, int iLength);
void loadcompactedfilepart(uint8 *pDestination, uint32 uiDestLength);
uint8 *unmangleGet(unsigned int uiPos, unsigned int uiCount);

//-------------------------------------------------------------------------------------------------
#endif