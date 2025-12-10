#ifndef _ROLLER_ROLLER_H
#define _ROLLER_ROLLER_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "sound.h"
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_gamepad.h>
//-------------------------------------------------------------------------------------------------

extern SDL_Gamepad *g_pController1;
extern SDL_Gamepad *g_pController2;
extern tJoyPos g_rollerJoyPos;
extern bool g_bPaletteSet;
extern bool g_bForceMaxDraw;
extern uint8 testbuf[4096];
extern bool g_bRepeat;
extern int g_iNumTracks;

//-------------------------------------------------------------------------------------------------

// functions added by ROLLER
int InitSDL(const char *data_root, const char *midi_root);
void ShutdownSDL();
void UpdateSDL();
void UpdateSDLWindow();
void UpdateSDLAudioEvents(SDL_Event e);

bool MIDI_Init(const char *config_file);
void MIDIDigi_PlayBuffer(uint8 *midi_buffer, uint32 midi_length);
void MIDIDigi_ClearBuffer();
void MIDI_CloseMidiBuffer();
void MIDI_Shutdown();

void MIDIInitSong(tInitSong *data);
void MIDIStartSong();
void MIDIStopSong();

void MIDISetMasterVolume(int8 volume);
int MIDIGetMasterVolume();

int DIGISampleStart(tSampleData *data);
bool DIGISampleDone(int index);
int DIGISampleAvailable(int index);
void DIGIStopSample(int index);
void DIGISetMasterVolume(int volume);
int DIGIGetMasterVolume();
void DIGISetSampleVolume(int iHandle, int iVolume);
void DIGISetPitch(int iHandle, int iPitch);
void DIGISetPanLocation(int iHandle, int iPan);

void PlayAudioSampleWait(int iIndex);
void PlayAudioDataWait(Uint8 *buffer, Uint32 length);
bool ROLLERfexists(const char *szFile);
FILE *ROLLERfopen(const char *szFile, const char *szMode); //tries to open file with both all caps and all lower case
int ROLLERopen(const char *szFile, int iOpenFlags); //tries to open file with both all caps and all lower case
int ROLLERremove(const char *szFile); //tries to remove file with both all caps and all lower case
int ROLLERrename(const char *szOldName, const char *szNewName); //tries to rename file with both all caps and all lower case
uint32 ROLLERAddTimer(Uint32 uiFrequencyHz, SDL_NSTimerCallback callback, void *userdata);
void ROLLERRemoveTimer(uint32 uiHandle);
int ROLLERfilelength(const char *szFile);
int ROLLERrand();
Uint64 SDLTickTimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval);
Uint64 SDLS7TimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval);
int IsCDROMDevice(const char *szPath);
void ReplaceExtension(char *szFilename, const char *szNewExt);
void ErrorBoxExit(const char *szErrorMsgFormat, ...);
void autoselectsoundlanguage();
int GetHighOrderRand(int iRange, int iRandValue);
int ReadUnalignedInt(const void *pData);
void LBAToMSF(uint32 uiLBA, uint8 *pbyMinute, uint8 *pbySecond, uint8 *pbyFrame);
void ROLLERGetAudioInfo();
void ROLLERStopTrack();
void ROLLERPlayTrack(int iTrack);
void ROLLERPlayTrack4(int iStartTrack);
void ROLLERSetAudioVolume(int iVolume);
void UpdateAudioTracks();
void CleanupAudioCD();

//-------------------------------------------------------------------------------------------------
#endif