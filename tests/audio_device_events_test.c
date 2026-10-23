#include "rollersound.h"
#include "roller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_bSnapshotMode = 0;
SDL_Mutex *g_pDigiMutex;
extern SDL_AudioStream *digi_stream[];
extern SDL_AudioStream *midi_stream;

#ifdef _WIN32
// This test uses PCM output only; OS MIDI must never be invoked by hotplug.
bool midi_player_init(void) { abort(); }
void midi_player_shutdown(void) { abort(); }
bool midi_player_load(const void *data, int len, bool loop) { abort(); }
void midi_player_start(void) { abort(); }
void midi_player_stop(void) { abort(); }
void midi_player_set_volume(int volume) { abort(); }
#endif

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "audio device event test failed at line %d: %s (%s)\n", \
          __LINE__, #condition, SDL_GetError()); return 1; \
} } while (0)

int main(void)
{
  CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  CHECK(SDL_Init(SDL_INIT_AUDIO));
  g_pDigiMutex = SDL_CreateMutex();
  CHECK(g_pDigiMutex);

  Uint8 samples[1024];
  memset(samples, 128, sizeof(samples));
  tSampleData sample = {0};
  sample.pSample = samples;
  sample.iLength = sizeof(samples);
  sample.iFlags = 18176; // looping engine sample
  sample.iVolume = 16000;
  sample.iPitch = 0x12000;
  sample.iPan = 0x8000;
  int handle = DIGISampleStart(&sample);
  CHECK(handle >= 0);
  SDL_AudioStream *engine = digi_stream[handle];
  CHECK(SDL_PauseAudioStreamDevice(engine));
  int generation = DIGISampleGeneration(handle);
  int engine_queued = SDL_GetAudioStreamQueued(engine);
  float engine_gain = SDL_GetAudioStreamGain(engine);

  // Use a PCM stream as the MIDI backend's output so this test needs no
  // instrument bank. Both playing and paused music must retain their state.
  SDL_AudioSpec spec = {SDL_AUDIO_U8, 2, 11025};
  midi_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, NULL, NULL);
  CHECK(midi_stream);
  SDL_AudioStream *music = midi_stream;
  const Uint32 events[] = {SDL_EVENT_AUDIO_DEVICE_ADDED,
                           SDL_EVENT_AUDIO_DEVICE_REMOVED,
                           SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED};
  for (int playing = 0; playing < 2; ++playing) {
    if (playing) CHECK(SDL_ResumeAudioStreamDevice(music));
    for (int recording = 0; recording < 2; ++recording) {
      for (int i = 0; i < SDL_arraysize(events); ++i) {
        CHECK(SDL_PutAudioStreamData(music, samples, sizeof(samples)));
        int music_queued = SDL_GetAudioStreamQueued(music);
        SDL_Event event = {0};
        event.type = events[i];
        event.adevice.which = SDL_GetAudioStreamDevice(engine);
        event.adevice.recording = recording;
        UpdateSDLAudioEvents(event);
        CHECK(midi_stream == music);
        CHECK(SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(music)) == !playing);
        if (!playing)
          CHECK(SDL_GetAudioStreamQueued(music) == music_queued);
        CHECK(digi_stream[handle] == engine);
        CHECK(DIGISampleGeneration(handle) == generation);
        CHECK(SDL_GetAudioStreamQueued(engine) == engine_queued);
        CHECK(SDL_GetAudioStreamGain(engine) == engine_gain);
        CHECK(SDL_GetAudioStreamFrequencyRatio(engine) == 1.125f);
      }
    }
  }

  DIGIClearAllStream();
  SDL_DestroyAudioStream(midi_stream);
  midi_stream = NULL;
  SDL_DestroyMutex(g_pDigiMutex);
  SDL_Quit();
  puts("audio device event tests passed");
  return 0;
}
