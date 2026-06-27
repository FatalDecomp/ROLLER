#ifndef _ROLLER_ROLLERSOUND_H
#define _ROLLER_ROLLERSOUND_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "sound.h"
#include <SDL3/SDL_events.h>
//-------------------------------------------------------------------------------------------------

bool MIDI_Init(const char *config_file);
void MIDI_Shutdown();

void MIDIInitSong(tInitSong *data);
void MIDIStartSong();
void MIDIStopSong();

void MIDISetMasterVolume(int8 volume);
int MIDIGetMasterVolume();

bool MIDI_OS_Init(void);
void MIDI_OS_Shutdown(void);
void MIDI_OS_InitSong(const tInitSong *data);
void MIDI_OS_StartSong(void);
void MIDI_OS_StopSong(void);
void MIDI_OS_SetMasterVolume(int8 volume);

bool MIDI_OPL_Init(void);
void MIDI_OPL_Shutdown(void);
void MIDI_OPL_InitSong(const tInitSong *data);
void MIDI_OPL_StartSong(void);
void MIDI_OPL_StopSong(void);
void MIDI_OPL_SetMasterVolume(int8 volume);

int DIGISampleStart(tSampleData *data);
bool DIGISampleDone(int index);
int DIGISampleGeneration(int index);
void DIGIStopSample(int index);
void DIGIClearAllStream();
void DIGISetMasterVolume(int volume);
int DIGIGetMasterVolume();
void DIGISetSampleVolume(int iHandle, int iVolume);
void DIGISetPitch(int iHandle, int iPitch);
void DIGISetPanLocation(int iHandle, int iPan);

void UpdateSDLAudioEvents(SDL_Event e);

//-------------------------------------------------------------------------------------------------
#endif
