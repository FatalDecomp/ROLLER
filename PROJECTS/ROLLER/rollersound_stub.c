/* Low-level MIDI and digital sample backends are absent from roller-core. */
#include "rollersound.h"

static int s_iMIDIMasterVolume = 127;
static int s_iDIGIMasterVolume;

bool MIDI_Init(const char *szConfigFile)
{
  (void)szConfigFile;
  return false;
}

void MIDI_Shutdown(void)
{
}

void MIDIInitSong(tInitSong *pSong)
{
  (void)pSong;
}

void MIDIStartSong(void)
{
}

void MIDIStopSong(void)
{
}

void MIDISetMasterVolume(int8 byVolume)
{
  s_iMIDIMasterVolume = byVolume;
}

int MIDIGetMasterVolume(void)
{
  return s_iMIDIMasterVolume;
}

bool MIDI_OS_Init(void)
{
  return false;
}

void MIDI_OS_Shutdown(void)
{
}

void MIDI_OS_InitSong(const tInitSong *pSong)
{
  (void)pSong;
}

void MIDI_OS_StartSong(void)
{
}

void MIDI_OS_StopSong(void)
{
}

void MIDI_OS_SetMasterVolume(int8 byVolume)
{
  s_iMIDIMasterVolume = byVolume;
}

bool MIDI_OPL_Init(void)
{
  return false;
}

void MIDI_OPL_Shutdown(void)
{
}

void MIDI_OPL_InitSong(const tInitSong *pSong)
{
  (void)pSong;
}

void MIDI_OPL_StartSong(void)
{
}

void MIDI_OPL_StopSong(void)
{
}

void MIDI_OPL_SetMasterVolume(int8 byVolume)
{
  s_iMIDIMasterVolume = byVolume;
}

int DIGISampleStart(tSampleData *pSample)
{
  (void)pSample;
  return -1;
}

bool DIGISampleDone(int iHandle)
{
  (void)iHandle;
  return true;
}

int DIGISampleGeneration(int iHandle)
{
  (void)iHandle;
  return -1;
}

void DIGIStopSample(int iHandle)
{
  (void)iHandle;
}

void DIGIClearAllStream(void)
{
}

void DIGISetMasterVolume(int iVolume)
{
  s_iDIGIMasterVolume = iVolume;
}

int DIGIGetMasterVolume(void)
{
  return s_iDIGIMasterVolume;
}

void DIGISetSampleVolume(int iHandle, int iVolume)
{
  (void)iHandle;
  (void)iVolume;
}

void DIGISetPitch(int iHandle, int iPitch)
{
  (void)iHandle;
  (void)iPitch;
}

void DIGISetPanLocation(int iHandle, int iPan)
{
  (void)iHandle;
  (void)iPan;
}

void UpdateSDLAudioEvents(SDL_Event Event)
{
  (void)Event;
}
