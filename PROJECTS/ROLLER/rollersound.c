#include "rollersound.h"
#include "roller.h"
#include "snapshot.h"
#include <limits.h>
#include <stdarg.h>
#include <wildmidi_lib.h>
//-------------------------------------------------------------------------------------------------

#ifdef ROLLER_AUDIO_DEBUG
static void AudioDebugLog(const char *szFormat, ...)
{
  char szMessage[512];
  va_list args;
  va_start(args, szFormat);
  vsnprintf(szMessage, sizeof(szMessage), szFormat, args);
  va_end(args);
  szMessage[sizeof(szMessage) - 1] = '\0';
  SDL_Log("[AUD] %s", szMessage);
}
#else
#define AudioDebugLog(...) ((void)0)
#endif

#pragma region MIDI

#define MIDI_RATE 44100 // not sure if this is the correct rate
SDL_AudioStream *midi_stream;
float midi_volume;
midi *midi_music;

static void MIDI_CloseMidiBufferUnlocked(void)
{
  if (midi_music) {
    WildMidi_Close(midi_music);
    midi_music = NULL;
  }
}

void MIDI_AudioStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
  //int available = SDL_GetAudioStreamAvailable(stream);
  //SDL_Log("MIDI_AudioStreamCallback[%i]: %i - %i", available, additional_amount, total_amount);

  //if (available != 0) return;

  if (midi_music) {
    void *output_buffer;
    int32_t res = 0;
    uint32_t samples = total_amount;

    output_buffer = malloc(samples);
    if (output_buffer != NULL)
      memset(output_buffer, 0, samples);

    if ((res = WildMidi_GetOutput(midi_music, output_buffer, samples)) > 0) {
      SDL_PutAudioStreamData(stream, output_buffer, res);
    }

    free(output_buffer);
  }
}

bool MIDI_Init(const char *config_file)
{
  long version = WildMidi_GetVersion();
  SDL_Log("MIDI_Init: Initializing libWildMidi %ld.%ld.%ld",
                      (version >> 16) & 255,
                      (version >> 8) & 255,
                      (version) & 255);

  uint16_t rate = MIDI_RATE;
  uint16_t mixer_options = 0;

  if (WildMidi_Init(config_file, rate, mixer_options) == -1) {
    SDL_Log("MIDI_Init: WildMidi_GetError: %s", WildMidi_GetError());
    WildMidi_ClearError();
    return false;
  }

  SDL_AudioSpec wav_spec;
  wav_spec.channels = 2;
  wav_spec.freq = MIDI_RATE;
  wav_spec.format = SDL_AUDIO_S16;

  midi_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav_spec, MIDI_AudioStreamCallback, NULL);
  midi_volume = 1.0f; // Default volume

  return true;
}

void MIDI_Shutdown()
{
  if (midi_stream) {
    SDL_PauseAudioStreamDevice(midi_stream);
    SDL_LockAudioStream(midi_stream);
    SDL_SetAudioStreamGetCallback(midi_stream, NULL, NULL);
    MIDI_CloseMidiBufferUnlocked();
    SDL_UnlockAudioStream(midi_stream);
    SDL_DestroyAudioStream(midi_stream);
    midi_stream = NULL;
  }
  MIDI_CloseMidiBufferUnlocked();
  WildMidi_Shutdown();
}

void MIDIInitStream()
{
  if (!midi_stream) {
    SDL_Log("MIDIInitStream: initialize 'midi_stream'.");
    SDL_AudioSpec wav_spec;
    wav_spec.channels = 2;
    wav_spec.freq = MIDI_RATE;
    wav_spec.format = SDL_AUDIO_S16;
    midi_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav_spec, MIDI_AudioStreamCallback, NULL);
  }

  // Set Volume Stream
  float master_volume = (float)MIDIGetMasterVolume() / 127.0f; // Normalize to [0.0, 1.0] range
  SDL_LockAudioStream(midi_stream);
  SDL_SetAudioStreamGain(midi_stream, midi_volume * master_volume); // Set the gain for the audio stream
  SDL_UnlockAudioStream(midi_stream);
  SDL_Log("MIDIInitSong: Volume: %f", midi_volume * master_volume);
}

void MIDIClearStream()
{
  if (midi_stream) {
    SDL_PauseAudioStreamDevice(midi_stream);
    SDL_LockAudioStream(midi_stream);
    SDL_SetAudioStreamGetCallback(midi_stream, NULL, NULL);
    SDL_ClearAudioStream(midi_stream);
    SDL_UnlockAudioStream(midi_stream);
    SDL_DestroyAudioStream(midi_stream);
    midi_stream = NULL;
  }
}

void MIDIInitSong(tInitSong *data)
{
  if (g_bSnapshotMode) return;
  MIDIStopSong();

  SDL_Log("MIDIInitSong: Midi - Length: %i", data->iLength);

  if (midi_stream)
    SDL_LockAudioStream(midi_stream);
  MIDI_CloseMidiBufferUnlocked();

  midi_music = WildMidi_OpenBuffer(((uint8 *)data->pData), data->iLength);
  if (!midi_music) {
    if (midi_stream)
      SDL_UnlockAudioStream(midi_stream);
    SDL_Log("MIDIInitSong: WildMidi_OpenBuffer failed: %s", WildMidi_GetError());
    return;
  }
  // Enable WildMidi_GetOutput Loop
  WildMidi_SetOption(midi_music, WM_MO_LOOP, WM_MO_LOOP);

  // Get Info
  struct _WM_Info *wm_info = WildMidi_GetInfo(midi_music);
  if (wm_info) {

    int apr_mins = wm_info->approx_total_samples / (MIDI_RATE * 60);
    int apr_secs = (wm_info->approx_total_samples % (MIDI_RATE * 60)) / MIDI_RATE;

    SDL_Log("MIDIInitSong: Approx %2um %2us Total", apr_mins, apr_secs);

    SDL_Log("MIDIInitSong: Total Samples %i", wm_info->approx_total_samples);
    SDL_Log("MIDIInitSong: Current Samples %i", wm_info->current_sample);
    SDL_Log("MIDIInitSong: Total Midi time %i", wm_info->total_midi_time);
    SDL_Log("MIDIInitSong: Mix Options %i", wm_info->mixer_options);
  }
  if (midi_stream)
    SDL_UnlockAudioStream(midi_stream);

  MIDIInitStream();
}

void MIDIStartSong()
{
  if (!midi_stream) {
    SDL_Log("MIDIStartSong: 'midi_stream' is not initialized.");
    return;
  }

  SDL_Log("MIDIStartSong: Play Audio Stream.");
  SDL_ResumeAudioStreamDevice(midi_stream);
}

void MIDIStopSong()
{
  if (!midi_stream) {
    SDL_Log("MIDIStopSong: 'midi_stream' is not initialized.");
    return;
  }

  SDL_Log("MIDIStopSong: Pause Audio Stream.");
  SDL_PauseAudioStreamDevice(midi_stream);
}

int8 MIDIMasterVolume = 127; // Default master volume (0-127)
/// <summary>
/// Set the master volume for MIDI playback. (0-127)
/// </summary>
void MIDISetMasterVolume(int8 volume)
{
  if (volume > 127) volume = 127;
  if (volume < 0) volume = 0;
  MIDIMasterVolume = volume;

  float master_volume = (float)volume / 127.0f; // Normalize to [0.0, 1.0] range

  // Change the gain for the MIDI stream
  if (midi_stream) {
    SDL_LockAudioStream(midi_stream);
    SDL_SetAudioStreamGain(midi_stream, midi_volume * master_volume);
    SDL_UnlockAudioStream(midi_stream);
  }
}

/// <summary>
/// Get the current master volume level for MIDI playback. (0-127)
/// </summary>
int MIDIGetMasterVolume()
{
  return MIDIMasterVolume;
}

#pragma endregion
//-------------------------------------------------------------------------------------------------
#pragma region DIGI
#define NUM_DIGI_STREAMS 32
#define DIGI_SAMPLE_LOOP_FLAGS 18176
SDL_AudioStream *digi_stream[NUM_DIGI_STREAMS];
float digi_volume[NUM_DIGI_STREAMS];
float digi_pan[NUM_DIGI_STREAMS]; // -1.0 (full left) to 1.0 (full right), 0.0 = center
int digi_generation[NUM_DIGI_STREAMS];
tSampleData digi_sample_data[NUM_DIGI_STREAMS];
extern int DIGIMasterVolume;

#ifdef ROLLER_AUDIO_DEBUG
static int s_iDigiMissingStreamLogs;

static int DIGIDebugActiveCountUnlocked(void)
{
  int iActive = 0;
  for (int i = 0; i < NUM_DIGI_STREAMS; ++i) {
    if (digi_stream[i])
      ++iActive;
  }
  return iActive;
}

static void DIGIDebugDumpSlotsUnlocked(const char *szReason)
{
  AudioDebugLog("DIGI slots: %s active=%d master=%d",
                szReason, DIGIDebugActiveCountUnlocked(), DIGIMasterVolume);
  for (int i = 0; i < NUM_DIGI_STREAMS; ++i) {
    if (digi_stream[i]) {
      int iQueued = SDL_GetAudioStreamAvailable(digi_stream[i]);
      AudioDebugLog("  slot=%d stream=%p gen=%d sample=%d len=%d queued=%d vol=%.3f pan=%.3f loop=%d",
                    i,
                    (void *)digi_stream[i],
                    digi_generation[i],
                    digi_sample_data[i].iSampleIndex,
                    digi_sample_data[i].iLength,
                    iQueued,
                    digi_volume[i],
                    digi_pan[i],
                    digi_sample_data[i].pSample != NULL);
    }
  }
}

static void DIGIDebugLogMissingStream(const char *szOp, int iHandle)
{
  ++s_iDigiMissingStreamLogs;
  if (s_iDigiMissingStreamLogs <= 64 || (s_iDigiMissingStreamLogs % 256) == 0) {
    AudioDebugLog("%s ignored: handle=%d has no live stream missing_logs=%d",
                  szOp, iHandle, s_iDigiMissingStreamLogs);
  }
}
#else
#define DIGIDebugActiveCountUnlocked() 0
#define DIGIDebugDumpSlotsUnlocked(reason) ((void)0)
#define DIGIDebugLogMissingStream(op, handle) ((void)0)
#endif

static void DIGILock(void)
{
  if (g_pDigiMutex)
    SDL_LockMutex(g_pDigiMutex);
}

static void DIGIUnlock(void)
{
  if (g_pDigiMutex)
    SDL_UnlockMutex(g_pDigiMutex);
}

// Convert mono U8 to stereo U8 with panning applied. out must be in_length * 2 bytes.
static void mono_to_stereo_pan_u8(const Uint8 *in, int in_length, Uint8 *out, float pan)
{
  float left_gain  = (pan <= 0.0f) ? 1.0f : 1.0f - pan;
  float right_gain = (pan >= 0.0f) ? 1.0f : 1.0f + pan;
  for (int i = 0; i < in_length; i++) {
    int s = (int)in[i] - 128;
    int l = (int)(s * left_gain);
    int r = (int)(s * right_gain);
    if (l >  127) l =  127; if (l < -128) l = -128;
    if (r >  127) r =  127; if (r < -128) r = -128;
    out[2 * i]     = (Uint8)(l + 128);
    out[2 * i + 1] = (Uint8)(r + 128);
  }
}

static void DIGIResetSlotState(int index)
{
  memset(&digi_sample_data[index], 0, sizeof(tSampleData));
  digi_volume[index] = 0.0f;
  digi_pan[index] = 0.0f;
}

static void DIGIReleaseStreamSlot(int index)
{
  SDL_AudioStream *stream = digi_stream[index];
  if (stream) {
    SDL_PauseAudioStreamDevice(stream);
    SDL_LockAudioStream(stream);
    SDL_SetAudioStreamGetCallback(stream, NULL, NULL);
    SDL_ClearAudioStream(stream);
    SDL_UnlockAudioStream(stream);
  }
  DIGIResetSlotState(index);
}

static void DIGIDestroyStreamSlot(int index)
{
  SDL_AudioStream *stream = digi_stream[index];
  if (!stream) {
    DIGIResetSlotState(index);
    return;
  }

  DIGIReleaseStreamSlot(index);
  digi_stream[index] = NULL;
  SDL_DestroyAudioStream(stream);
}

static bool DIGIQueueSampleData(SDL_AudioStream *stream, const tSampleData *data, float pan)
{
  if (!stream || !data || !data->pSample || data->iLength <= 0 || data->iLength > INT_MAX / 2) {
    AudioDebugLog("DIGIQueueSampleData rejected: stream=%p data=%p sample=%p len=%d pan=%.3f",
                  (void *)stream,
                  (const void *)data,
                  data ? data->pSample : NULL,
                  data ? data->iLength : 0,
                  pan);
    return false;
  }

  int stereo_len = data->iLength * 2;
  Uint8 *stereo_buf = (Uint8 *)SDL_malloc(stereo_len);
  if (!stereo_buf) {
    AudioDebugLog("DIGIQueueSampleData failed: malloc len=%d sample=%d",
                  stereo_len, data->iSampleIndex);
    return false;
  }

  mono_to_stereo_pan_u8((const Uint8 *)data->pSample, data->iLength, stereo_buf, pan);
  bool bQueued = SDL_PutAudioStreamData(stream, stereo_buf, stereo_len);
  if (!bQueued) {
    AudioDebugLog("DIGIQueueSampleData failed: SDL_PutAudioStreamData sample=%d len=%d stream=%p error=%s",
                  data->iSampleIndex, stereo_len, (void *)stream, SDL_GetError());
  }
  SDL_free(stereo_buf);
  return bQueued;
}

static int DIGISampleAvailableUnlocked(int iHandle)
{
  if (iHandle < 0 || iHandle >= NUM_DIGI_STREAMS)
    return 0;
  if (digi_stream[iHandle])
    return SDL_GetAudioStreamAvailable(digi_stream[iHandle]);
  return 0;
}

static bool DIGISampleDoneUnlocked(int iHandle)
{
  if (iHandle < 0 || iHandle >= NUM_DIGI_STREAMS)
    return true;
  if (!digi_stream[iHandle])
    return true;
  if (digi_sample_data[iHandle].pSample)
    return false;
  return DIGISampleAvailableUnlocked(iHandle) == 0;
}

void DIGI_AudioStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
  tSampleData *data = (tSampleData *)userdata;
  if (!data || !data->pSample || additional_amount <= 0)
    return;
  tSampleData sampleData = *data;
  int index = (int)(data - digi_sample_data);
  float pan = (index >= 0 && index < NUM_DIGI_STREAMS) ? digi_pan[index] : 0.0f;
  DIGIQueueSampleData(stream, &sampleData, pan);
}

int DIGISampleStart(tSampleData *data)
{
  if (g_bSnapshotMode) return -1;
  if (!data || !data->pSample || data->iLength <= 0) {
    AudioDebugLog("DIGISampleStart rejected: data=%p sample=%p len=%d",
                  (void *)data, data ? data->pSample : NULL, data ? data->iLength : 0);
    return -1;
  }

  DIGILock();

  int iActiveBefore = DIGIDebugActiveCountUnlocked();
  int index = -1;
  for (int i = 0; i < NUM_DIGI_STREAMS; ++i) {
    if (!digi_stream[i] || DIGISampleDoneUnlocked(i)) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    AudioDebugLog("DIGISampleStart failed: no slots sample=%d len=%d vol=%d pitch=%d flags=%d pan=%d active=%d",
                  data->iSampleIndex, data->iLength, data->iVolume,
                  data->iPitch, data->iFlags, data->iPan, iActiveBefore);
    DIGIDebugDumpSlotsUnlocked("no available slot");
    DIGIUnlock();
    return index; // No available stream slots
  }

  float volume = (float)data->iVolume / 0x7FFF; // Convert volume to [0.0, 1.0] range
  bool bLoop = data->iFlags == DIGI_SAMPLE_LOOP_FLAGS;
  int iPan = data->iPan;

  if (digi_stream[index]) {
    AudioDebugLog("DIGISampleStart reusing done slot=%d sample=%d queued=%d",
                  index, data->iSampleIndex, SDL_GetAudioStreamAvailable(digi_stream[index]));
    DIGIReleaseStreamSlot(index);
  }
  ++digi_generation[index];

  // Compute initial pan: raw iPan [0, 0x10000], 0x8000 = center → [-1.0, 1.0]
  float fInitialPan = ((float)((int32)iPan) / (int32)0x8000) - 1.0f;
  if (fInitialPan < -1.0f) fInitialPan = -1.0f;
  if (fInitialPan >  1.0f) fInitialPan =  1.0f;
  digi_pan[index] = fInitialPan;

  if (!digi_stream[index]) {
    SDL_AudioSpec spec;
    spec.channels = 2; // Stereo (panning applied manually per-sample)
    spec.freq = 11025; // Sample rate
    spec.format = SDL_AUDIO_U8; // 8-bit unsigned audio
    digi_stream[index] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    AudioDebugLog("DIGISampleStart open stream: slot=%d stream=%p sample=%d active_before=%d",
                  index, (void *)digi_stream[index], data->iSampleIndex, iActiveBefore);
  }

  if (!digi_stream[index]) {
    AudioDebugLog("DIGISampleStart failed: open stream sample=%d len=%d error=%s",
                  data->iSampleIndex, data->iLength, SDL_GetError());
    DIGIDebugDumpSlotsUnlocked("open stream failed");
    DIGIUnlock();
    return -1;
  }

  SDL_LockAudioStream(digi_stream[index]);

  if (bLoop) {
    digi_sample_data[index] = *data;
    SDL_SetAudioStreamGetCallback(digi_stream[index], DIGI_AudioStreamCallback, &digi_sample_data[index]);
  }

  // Set pitch in the stream
  SDL_SetAudioStreamFrequencyRatio(digi_stream[index], 1.0); // pitch
  SDL_SetAudioStreamFrequencyRatio(digi_stream[index], (float)data->iPitch / 0x10000);

  // Remember the volume for this stream
  digi_volume[index] = volume;

  // Set the gain for the audio stream
  float master_volume = (float)DIGIGetMasterVolume() / 0x7FFF; // Normalize to [0.0, 1.0] range
  SDL_SetAudioStreamGain(digi_stream[index], volume * master_volume);

  // Convert mono to stereo with pan applied and push to stream
  bool bQueued = DIGIQueueSampleData(digi_stream[index], data, fInitialPan);
  SDL_UnlockAudioStream(digi_stream[index]);
  SDL_ResumeAudioStreamDevice(digi_stream[index]);

  AudioDebugLog("DIGISampleStart ok: slot=%d gen=%d sample=%d len=%d vol=%d master=%d gain=%.3f pitch=%d flags=%d loop=%d pan=%d fpan=%.3f queued=%d active=%d",
                index,
                digi_generation[index],
                data->iSampleIndex,
                data->iLength,
                data->iVolume,
                DIGIMasterVolume,
                volume * master_volume,
                data->iPitch,
                data->iFlags,
                bLoop ? 1 : 0,
                data->iPan,
                fInitialPan,
                bQueued ? SDL_GetAudioStreamAvailable(digi_stream[index]) : -1,
                DIGIDebugActiveCountUnlocked());

  DIGIUnlock();
  return index;
}

/// <summary>
/// Check if a digital sample is done playing.
/// </summary>
/// <param name="index"></param>
bool DIGISampleDone(int index)
{
  DIGILock();
  bool bDone = DIGISampleDoneUnlocked(index);
  DIGIUnlock();
  return bDone;
}

int DIGISampleGeneration(int index)
{
  if (index < 0 || index >= NUM_DIGI_STREAMS)
    return -1;

  DIGILock();
  int iGeneration = digi_generation[index];
  DIGIUnlock();
  return iGeneration;
}

int DIGIMasterVolume = 0x7FFF; // Default master volume (0-0x7FFF)
/// <summary>
/// Set the master volume for all digital audio streams.
/// </summary>
/// <param name="volume">Volume level (0-0x7FFF).</param>
void DIGISetMasterVolume(int volume)
{
  int iOldVolume = DIGIMasterVolume;
  if (volume > 0x7FFF) volume = 0x7FFF;
  if (volume < 0) volume = 0;
  DIGIMasterVolume = volume;
  
  float normalized_volume = (float)volume / 0x7FFF; // Normalize to [0.0, 1.0] range

  DIGILock();
  for (size_t i = 0; i < NUM_DIGI_STREAMS; i++) {
    if (digi_stream[i]) {
      SDL_LockAudioStream(digi_stream[i]);
      SDL_SetAudioStreamGain(digi_stream[i], digi_volume[i] * normalized_volume);
      SDL_UnlockAudioStream(digi_stream[i]);
    }
  }
  if (iOldVolume != volume) {
    AudioDebugLog("DIGISetMasterVolume: old=%d new=%d active=%d",
                  iOldVolume, volume, DIGIDebugActiveCountUnlocked());
  }
  DIGIUnlock();
}

/// <summary>
/// Get the current master volume level.
/// </summary>
/// <returns>The master volume level (0-0x7FFF).</returns>
int DIGIGetMasterVolume()
{
  return DIGIMasterVolume;
}

void DIGIStopSample(int index)
{
  if (index < 0 || index >= NUM_DIGI_STREAMS) {
    SDL_Log("DIGIStopSample: Invalid stream index: %d", index);
    return;
  }
  DIGILock();
  if (digi_stream[index]) {
    AudioDebugLog("DIGIStopSample: handle=%d gen=%d sample=%d queued=%d",
                  index,
                  digi_generation[index],
                  digi_sample_data[index].iSampleIndex,
                  SDL_GetAudioStreamAvailable(digi_stream[index]));
    DIGIReleaseStreamSlot(index);
    ++digi_generation[index];
  } else {
    DIGIDebugLogMissingStream("DIGIStopSample", index);
  }
  DIGIUnlock();
}

void DIGIClearAllStream()
{
  DIGILock();
  AudioDebugLog("DIGIClearAllStream: active_before=%d master=%d",
                DIGIDebugActiveCountUnlocked(), DIGIMasterVolume);
  DIGIDebugDumpSlotsUnlocked("before clear all");
  for (int i = 0; i < NUM_DIGI_STREAMS; i++) {
    if (digi_stream[i]) {
      DIGIDestroyStreamSlot(i);
      ++digi_generation[i];
    }
  }
  AudioDebugLog("DIGIClearAllStream: active_after=%d", DIGIDebugActiveCountUnlocked());
  DIGIUnlock();
}

void DIGISetSampleVolume(int iHandle, int iVolume)
{
  if (iHandle < 0 || iHandle >= NUM_DIGI_STREAMS)
    return;

  DIGILock();
  if (!digi_stream[iHandle]) {
    DIGIDebugLogMissingStream("DIGISetSampleVolume", iHandle);
    DIGIUnlock();
    return; //DIGI stream not found
  }

  float fStreamVolume = (float)iVolume / 0x7FFF; // Convert volume to [0.0, 1.0] range

  // udpate saved volume
  digi_volume[iHandle] = fStreamVolume;

  // Set the gain for the audio stream
  float fMasterVolume = (float)DIGIGetMasterVolume() / 0x7FFF; // Normalize to [0.0, 1.0] range
  SDL_LockAudioStream(digi_stream[iHandle]);
  SDL_SetAudioStreamGain(digi_stream[iHandle], fStreamVolume * fMasterVolume);
  SDL_UnlockAudioStream(digi_stream[iHandle]);
  DIGIUnlock();
}

void DIGISetPitch(int iHandle, int iPitch)
{
  if (iHandle < 0 || iHandle >= NUM_DIGI_STREAMS)
    return;

  DIGILock();
  if (!digi_stream[iHandle]) {
    DIGIDebugLogMissingStream("DIGISetPitch", iHandle);
    DIGIUnlock();
    return; //DIGI stream not found
  }

  float fStreamPitch = (float)iPitch / 0x10000;
  SDL_LockAudioStream(digi_stream[iHandle]);
  SDL_SetAudioStreamFrequencyRatio(digi_stream[iHandle], fStreamPitch);
  SDL_UnlockAudioStream(digi_stream[iHandle]);
  DIGIUnlock();
}

void DIGISetPanLocation(int iHandle, int iPan)
{
  if (iHandle < 0 || iHandle >= NUM_DIGI_STREAMS)
    return;
  DIGILock();
  if (!digi_stream[iHandle]) {
    DIGIDebugLogMissingStream("DIGISetPanLocation", iHandle);
    DIGIUnlock();
    return;
  }
  float fNewPan = ((float)((int32)iPan) / (int32)0x8000) - 1.0f;
  if (fNewPan < -1.0f) fNewPan = -1.0f;
  if (fNewPan >  1.0f) fNewPan =  1.0f;
  SDL_LockAudioStream(digi_stream[iHandle]);
  digi_pan[iHandle] = fNewPan;
  // For looping samples (callback-driven), flush queued audio so the new pan
  // takes effect immediately rather than after the current loop finishes.
  tSampleData *data = &digi_sample_data[iHandle];
  if (data->pSample) {
    SDL_ClearAudioStream(digi_stream[iHandle]);
    DIGIQueueSampleData(digi_stream[iHandle], data, fNewPan);
  }
  SDL_UnlockAudioStream(digi_stream[iHandle]);
  DIGIUnlock();
}

#pragma endregion

//-------------------------------------------------------------------------------------------------
/// <summary>
/// Handle SDL audio device events for MIDI and digital audio streams.
/// </summary>
/// <param name="e"></param>
void UpdateSDLAudioEvents(SDL_Event e)
{
  if (e.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
    SDL_AudioDeviceEvent *ade = (SDL_AudioDeviceEvent *)&e;
    SDL_Log("UpdateSDLAudioEvents: Audio device removed: %d", ade->which);
    DIGIClearAllStream();
    MIDIClearStream();
  }
  if (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED) {
    SDL_AudioDeviceEvent *ade = (SDL_AudioDeviceEvent *)&e;
    SDL_Log("UpdateSDLAudioEvents: Audio device Added: %d", ade->which);
    DIGIClearAllStream();
    MIDIClearStream();
    MIDIInitStream();
    MIDIStartSong(); // Force music to continue playing
  }
}

//-------------------------------------------------------------------------------------------------
