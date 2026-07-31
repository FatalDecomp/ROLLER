/*
 * roller-core does not own an audio device. These definitions preserve the
 * legacy sound boundary for rendering/loading translation units while keeping
 * every playback operation inert. The game continues to link sound.c.
 */
#include "sound.h"

#include <SDL3/SDL_atomic.h>
#include <string.h>

volatile uint64 ullLastTickTimeNs;
volatile uint64 ullTickIntervalNs = HZ_TO_NS(36u);
SDL_AtomicInt iTicksPending;

int samplespending;
int writesample;
int readsample;
int lastsample = -1000;
int musicon;
int soundon;
int allengines;
int cheat_samples;
int palette_brightness = 32;
int last_inp[2];
void *pal_selector = (void *)-1;
char SourcePath[64];
char DestinationPath[64];
char languagename[32];
int writeptr;
int readptr;
int SoundCard;
int SoundPort;
int SoundIRQ;
int SoundDMA;
int EngineVolume = 32;
int SFXVolume = 56;
int SpeechVolume = 127;
int MusicVolume = 108;
int MusicCard;
int MusicCD;
int MusicOS;
int MusicOPL;
int MusicPort;
uint8 *SongPtr;
int CDSong[20];
int GMSong[21];
tSampleData SampleData;
tSampleData SampleFixed;
tSampleData SamplePanned;
uint8 rud_gr[2];
uint8 rud_strat[2];
int fraction;
uint8 *frontendspeechptr;
int frontendspeechhandle = -1;
uint32 frontendlen;
int holdmusic;
int drivertype = -1;
uint8 unmangleinbuf[1024];
uint8 *musicbuffer;
int lastvolume[16];
int lastpitch[16];
int lastpan[16];
int net_time[16];
int rud_turn[2];
int rud_swheel[2];
int rud_steer[2];
uint32 SampleLen[120];
uint8 *SamplePtr[120];
tSampleHandleCar SampleHandleCar[120];
char Sample[120][15];
char lang[16][32];
int TrackMap[32];
char TextExt[64];
char SampleExt[64];
int Pending[16];
tSamplePending SamplePending[16][20];
int HandleCar[32];
int HandleSample[32];
tCarSoundData enginedelay[16];
int car_to_player[16];
int player_to_car[16];
tSpeechInfo speechinfo[16];
int load_times[16];
tCopyData copy_multiple[512][16];
int unmangleinpoff;
uint8 *unmangledst;
int unmangleoverflow;
FILE *unmanglefile;
int unmanglebufpos;
volatile int s7;
int network_timeout;
void *MT32Data;
void *FMDrums;
void *FMInstruments;
int network_sync_error;
int ticks_received;
int network_limit;
volatile int frames;
char Song[20][15];
uint32 tickhandle;
tColor *pal_addr;
int user_inp;
int nummusictracks;
int winchampsong;
int winsong;
int delaywritex;
int delayreadx;
int leaderboardsong;
int optionssong;
int titlesong;
int delaywrite;
int delayread;
int numsamples;
int cheatsample;
int languages;
int net_loading;
int already_quit;
int network_error;

static void sound_stub_note_playback(void)
{
  static int iLogged;
  if (!iLogged) {
    fprintf(stderr, "roller-core: audio playback is unavailable\n");
    iLogged = 1;
  }
}

bool loadDOS(const char *szFilename, void **pOutBuffer)
{
  (void)szFilename;
  if (pOutBuffer)
    *pOutBuffer = NULL;
  return false;
}

bool setpal(const char *szFilename)
{
  (void)szFilename;
  return false;
}

void blankpal(void)
{
  palette_brightness = 0;
}

void Initialise_SOS(void)
{
  SoundCard = 0;
  MusicCard = 0;
  MusicCD = 0;
  MusicOS = 0;
  MusicOPL = 0;
  soundon = 0;
  musicon = 0;
}

void readuserdata(int iPlayer)
{
  (void)iPlayer;
}

void tick_clock_step(void)
{
}

#if defined(IS_WASM)
void wasm_tick_clock_suspend(void)
{
}

void wasm_tick_clock_update(void)
{
}
#endif

void game_tick_step(void)
{
}

void DrainEngineDelay(void)
{
}

void reset_tick_input_samples(void)
{
  memset(last_inp, 0, sizeof(last_inp));
}

void claim_ticktimer(unsigned int uiRateHz)
{
  ullTickIntervalNs = HZ_TO_NS(uiRateHz);
}

void release_ticktimer(void)
{
  ullLastTickTimeNs = 0;
}

void Uninitialise_SOS(void)
{
}

void loadsamples(void)
{
}

void loadfatalsample(void)
{
}

void freefatalsample(void)
{
}

void releasesamples(void)
{
}

void stop(void)
{
}

int initgus(void)
{
  return 0;
}

void devicespecificuninit(void)
{
}

void readsoundconfig(void)
{
  Initialise_SOS();
}

char *FindConfigVar(const char *szConfigText, const char *szVarName)
{
  (void)szConfigText;
  (void)szVarName;
  return NULL;
}

void loadfile(const char *szFile, void **pBuffer, unsigned int *puiSize,
              int iIsSound)
{
  (void)szFile;
  (void)iIsSound;
  if (pBuffer)
    *pBuffer = NULL;
  if (puiSize)
    *puiSize = 0;
}

void initsounds(void)
{
}

void stopallsamples(void)
{
}

void pannedsample(int iSampleIdx, int iHandle, int iPan)
{
  (void)iSampleIdx;
  (void)iHandle;
  (void)iPan;
}

void speechonly(int iSampleIdx, int iVolume, int iDelay, int iCarIdx)
{
  (void)iSampleIdx;
  (void)iVolume;
  (void)iDelay;
  (void)iCarIdx;
}

void speechsample(int iSampleIdx, int iVolume, int iDelay, int iCarIdx)
{
  (void)iSampleIdx;
  (void)iVolume;
  (void)iDelay;
  (void)iCarIdx;
  sound_stub_note_playback();
}

void analysespeechsamples(void)
{
}

void dospeechsample(int iSampleIdx, int iVolume)
{
  (void)iSampleIdx;
  (void)iVolume;
}

void loadfrontendsample(char *szFilename)
{
  (void)szFilename;
}

int frontendsample(int iVolume)
{
  (void)iVolume;
  return -1;
}

void remove_frontendspeech(void)
{
}

int sfxplaying(int iSampleIdx)
{
  (void)iSampleIdx;
  return 0;
}

int cheatsampleok(int iCarIdx)
{
  (void)iCarIdx;
  return 0;
}

void sfxsample(int iSampleIdx, int iVolume)
{
  (void)iSampleIdx;
  (void)iVolume;
  sound_stub_note_playback();
}

void sample2(int iCarIdx, int iSampleIdx, int iVolume, int iPitch, int iPan,
             int iByteOffset)
{
  (void)iCarIdx;
  (void)iSampleIdx;
  (void)iVolume;
  (void)iPitch;
  (void)iPan;
  (void)iByteOffset;
}

void sfxpend(int iSampleIdx, int iDriverIdx, int iVolume)
{
  (void)iSampleIdx;
  (void)iDriverIdx;
  (void)iVolume;
}

void enginesounds2(int iPlayer1Car, int iPlayer2Car)
{
  (void)iPlayer1Car;
  (void)iPlayer2Car;
}

void enginesounds(int iFocusCarIdx)
{
  (void)iFocusCarIdx;
}

void loopsample(int iCarIdx, int iSampleIdx, int iVolume, int iPitch, int iPan)
{
  (void)iCarIdx;
  (void)iSampleIdx;
  (void)iVolume;
  (void)iPitch;
  (void)iPan;
}

void enginesound(int iCarIdx, float fListenerDopplerVel, float fCarDopplerVel,
                 float fDistance, int iStereoVolume)
{
  (void)iCarIdx;
  (void)fListenerDopplerVel;
  (void)fCarDopplerVel;
  (void)fDistance;
  (void)iStereoVolume;
}

void startmusic(int iSong)
{
  (void)iSong;
  sound_stub_note_playback();
}

void stopmusic(void)
{
}

bool MusicBackendAvailable(void)
{
  return false;
}

void MusicSetMasterVolume(int iVolume)
{
  MusicVolume = iVolume;
}

void load_language_map(void)
{
}

void initmusic(void)
{
  musicon = 0;
}

void SOSTimerCallbackS7(void)
{
  ++s7;
}

void fade_palette_begin(int iPaletteBrightness)
{
  palette_brightness = iPaletteBrightness;
}

int fade_palette_update(void)
{
  return -1;
}

int fade_palette_active(void)
{
  return 0;
}

void fade_audio_restore(void)
{
}

void palette_sync_pal_addr(void)
{
}

void fade_palette_finish(void)
{
}

void fade_music_finish(int iTargetBrightness)
{
  (void)iTargetBrightness;
}

void set_palette(int iBrightness)
{
  if (iBrightness < 0)
    iBrightness = 0;
  if (iBrightness > 31)
    iBrightness = 31;
  palette_brightness = iBrightness;
}

void convertname(char *szFilename)
{
  (void)szFilename;
}

void decode(uint8 *pData, int iLength, uint32 uiStep, uint32 uiOffset)
{
  int i;
  for (i = 0; i < iLength; ++i) {
    uint8 byOriginal = pData[i];
    uint32 uiNextOffset = uiOffset + uiStep;
    pData[i] = (uint8)uiNextOffset ^ byOriginal;
    uiStep = uiOffset;
    uiOffset = uiNextOffset;
  }
}

void loadasample(int iIndex)
{
  (void)iIndex;
}

void select8bitdriver(void)
{
}

void resetsamplearray(void)
{
  int i;
  int j;
  for (i = 0; i < 32; ++i) {
    HandleCar[i] = -1;
    HandleSample[i] = -1;
  }
  for (i = 0; i < 120; ++i)
    for (j = 0; j < 16; ++j)
      SampleHandleCar[i].handles[j] = -1;
}

void reinitmusic(void)
{
  musicon = 0;
}

int getcompactedfilelength(const char *szFilename)
{
  (void)szFilename;
  return -1;
}

int initmangle(const char *szFilename)
{
  (void)szFilename;
  return 0;
}

int uninitmangle(void)
{
  return 0;
}

int loadcompactedfile(const char *szFilename, uint8 *pBuffer)
{
  (void)szFilename;
  (void)pBuffer;
  return -1;
}

void readmangled(uint8 *pBuffer, int iLength)
{
  (void)pBuffer;
  (void)iLength;
}

void loadcompactedfilepart(uint8 *pDestination, uint32 uiDestinationLength)
{
  (void)pDestination;
  (void)uiDestinationLength;
}

uint8 *unmangleGet(unsigned int uiPosition, unsigned int uiCount)
{
  (void)uiPosition;
  (void)uiCount;
  return NULL;
}
