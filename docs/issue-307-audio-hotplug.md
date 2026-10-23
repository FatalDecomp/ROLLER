# Issue 307: audio output changes

Ticket: <https://github.com/FatalDecomp/ROLLER/issues/307>

Dump: `roller-crash-20261022-160349/roller-crash-20261022-160349.dmp`. The dump
identifies version `0.1.1-dev.20261019`, commit
`82168d6b86bd7e7376dfb0c45192e877ff0ac6a1`. Analysis used the matching
`roller.exe` and `roller.pdb` from that release, rather than local symbols.

The fault is an access violation in `SDL_LockMutex_srw`, reached through:

```text
UpdateAudioStreamFormatsPhysical
SDL_OpenAudioDevice_REAL
SDL_OpenAudioDeviceStream_REAL
DIGISampleStart
loopsample
DrainEngineDelay
game_tick_step
race_update
```

SDL is traversing a freed audio stream: its mutex pointer is
`0x00790065006b0040`, and its leading fields contain UTF-16 text. This is
consistent with memory reused after destruction, rather than an invalid sample
buffer passed to the mixer.

The pinned SDL 3.2.22 source has a linked-list defect in
`SDL_DefaultAudioDeviceChanged`: when inserting migrated logical devices at the
destination list head, it does not set the former head's `prev` pointer. Later
closing migrated logical devices can leave stale entries. ROLLER's event handler
exposed this path by destroying every digital and software MIDI stream on both
device addition and removal, then recreating streams as samples played. It also
affected capture devices and restarted music regardless of its previous paused
state.

All ROLLER playback streams open `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`.
[SDL migrates default-device streams automatically](https://wiki.libsdl.org/SDL3/CategoryAudio).
The handler now leaves streams and playback state intact. The regression test
uses SDL's dummy audio driver and checks engine stream identity, generation,
queued data, pitch and gain, plus playing/paused MIDI output, for playback and
capture addition, removal and format-change events.

Run with `zig build test-audio-device-events --release=fast`. The regression
test and native Windows `zig build --release=fast` both passed.

The test injects events; it does not emulate Windows endpoint migration.
Physical headphone insertion/removal and shutdown after switching devices still
need manual verification. The SDL linked-list defect itself remains in the
dependency; removing event-driven teardown avoids the reported in-game trigger
but does not repair later teardown inside SDL.
