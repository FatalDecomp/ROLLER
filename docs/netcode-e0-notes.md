# NET-E0 implementation notes

These notes are the audit deliverables for the E0 foundation stories in
`ROLLER-Netcode-Plan_10.md`.

## Headless race recipe

`NetHeadlessInit` initializes the legacy globals, calls `InitCarStructs`, seeds
the shared RNG, assigns the grid and driver mappings, loads a track through
`loadtrack_from_path_with_assets_ex`, initializes near-car state, resets the
start gate to `game_frame = -1`, and clears the 512-slot input ring. The E0-S8
test advances no more than 200 ticks to pass `game_frame == 145`, then measures
100 running ticks. The cap turns a stalled start gate into a test failure.

The `roller-core` test executable uses the existing manifest stub swaps:
`rollercomms_stub.c`, `crashdump_stub.c`, `debug_overlay_stub.c`,
`sound_stub.c`, `rollersound_stub.c`, and `cdx_stub.c`. Audio playback and
platform communication are inert; simulation state, the input ring, RNG, and
track loading remain real.

## Tick context enumeration

The local rollback ring stores the following globals in `tNetSimTickContext`:

- Race timing and gates: `game_frame`, `countdown`, `race_started`, `racing`,
  `start_race`, `fudge_wait`, `updates`, and `nearcarcheck`.
- View and control phases: `warp_angle`, `view0_cnt`, `view1_cnt`,
  `Quit_Count`, `cheat_control`, and `disable_messages`.
- Race results: `finishers`, `human_finishers`, `finished_car`, `carorder`,
  `Fatality`, `Fatality_Count`, `Destroyed`, `Victim`, and the record arrays.
- Effects that a tick reads or mutates: `CarSpray`, `SLight`, `lastsample`,
  `game_overs`, `champ_count`, `speechinfo`, `readsample`, `writesample`,
  `game_count`, `sub_on`, `game_scale`, and `PULLZ`.
- Input and randomness: `readptr`, `writeptr`, RNG state, and RNG draw count.
- Near-car state: the complete `nearcall` array.

`frames` and `ticks` are scheduler counters. `control_one_tick` enters
`control_ticks(1, 1)` and does not read either counter in the simulated tick,
so they are outside the rollback context.

The recovery wire remains 12 bytes: `game_frame`, `countdown`,
`race_started`, `racing`, and `warp_angle`. RNG is already a separate snapshot
field. Input pointers and contents come from the newly installed input history;
result and world state come from snapshots/events; the remaining entries above
are local rollback or presentation state and are initialized afresh during a
full recovery. No additional recovery-only global was found.

## Replay suppression audit

Replay suppression is limited to output sinks:

| Site | Suppressed operation | RNG justification |
| --- | --- | --- |
| `control.c`, both `DoReplayData` calls | Replay-file writes | Serialization performs no RNG draw. |
| `sound.c`, `speechsample` | Speech queue insertion | Selection occurs before this call; game-over bookkeeping remains active, and insertion draws no RNG. |
| `sound.c`, `dospeechsample` | Speech playback | Playback is an output sink with no RNG draw. |
| `sound.c`, `sfxsample` | Immediate effect playback | Playback is an output sink with no RNG draw. |
| `sound.c`, `sfxpend` | Pending effect queue insertion | Selection and replay metadata happen before the guarded insertion; insertion draws no RNG. |

Particles and smoke are not suppressed because their creation consumes the
shared RNG stream. The acceptance suite checks RNG state and draw count for a
single replayed tick, a 20-tick replay, and all rollback variants. The 20-tick
test also checks that replay-file size and sound/speech queues do not change.

## Authority and puppet write audit

The puppet gates cover the top-level car loop and start initialization in
`control_ticks`, `humancar`, `autocar2`, `updatecar2`, movement/landing paths,
near-car and AI helpers, special-car target writes, `function.c` damage, and
`colision.c` response. `testcoll` redirects a puppet side into a copy when only
one participant is a puppet and skips the response when both are puppets. The
post-hook assertion compares every puppet's world pose at hook exit and tick
exit. The predicting-to-puppet test requires the complete `tCar` to remain
unchanged on the next tick.

REMOTE authority gates cover finish detection and result counters, damage,
death and respawn, track-changing and other-car special abilities, cooldown
ownership, record updates, and fatality/finish presentation triggers. MAYTE's
movement boost remains local for a predicted group car. `updatestunts`, the
puppet hook, and running-lap-time advancement remain active. Tests cover three
finish-line crossings, LOVEBUN track colors, one ramp advancement per tick,
running lap time, one-sided collision, and a moving-ramp puppet for 200 ticks.

## Car-field audit

`tests/net_car_fields.inc` is the executable one-entry-per-field table. The
assignments are:

| Assignment | Fields |
| --- | --- |
| Movement | `pos`, `nCurrChunk`, `nReferenceChunk`, `nRoll`, `nPitch`, `nYaw`, `fFinalSpeed`, `fHorizontalSpeed`, `direction`, `nActualYaw`, `iJumpMomentum`, `iControlType`, `iSteeringInput`, `iBankingSteerOffset`, `fBaseSpeed`, `fSpeedOverflow`, `fRPMRatio`, `fPower`, `byGearAyMax`, `iRollMotion`, `iPitchMotion`, `iYawMotion`, `iRollMomentum`, `iLastValidChunk`, `nTargetChunk`, `nChangeMateCooldown`, `byThrottlePressed`, `byAccelerating`, `byAIThrottleControl`, `byEngineStartTimer`, `byPitLaneActiveFlag`, `byCollisionTimer`, `iEngineState` |
| Authoritative | `fHealth`, `nDeathTimer`, `byLives`, `byLapNumber`, `byLap`, `byRacePosition`, `byStatusFlags`, `fDurability`, `byDebugDamage`, `byAttacker`, `byKills`, `byDamageSourceTimer`, `iStunned`, `fRunningLapTime`, `fBestLapTime`, `fPreviousLapTime`, `fTotalRaceTime`, `byDamageToggle`, `byLastDamageToggle`, `byDamageIntensity`, `byCheatAmmo`, `byCheatCooldown`, `iDamageState` |
| Configuration or AI-only | `iDriverIdx`, `fCarHalfWidth`, `fCarWidthBankingProjection`, `iTrackedCarIdx`, `iUnused`, `byCarDesignIdx`, `iAICurrentLine`, `iAIUpdateTimer`, `iBobMode`, `iSelectedStrategy`, `iAITargetLine`, `iAITargetCar`, `iLaneType`, `iLeftTargetIdx`, `fLeftTargetTime`, `iRightTargetIdx`, `fRightTargetTime`, `fTargetX`, `fTargetY` |
| Presentation | `posLastFrame`, `nExplosionSoundTimer`, `fLastAnimationSpeed`, `iPitchDynamicOffset`, `iRollDynamicOffset`, `iPitchBackup`, `iCameraOscillationPhase`, `iPitchCameraOffset`, `iRollDampingMomentum`, `iRollCameraOffset`, `byWheelAnimationFrame`, `nLastCommentaryChunk`, `nReverseWarnCooldown`, `fWheelSpinAccumulation`, `fWheelSpinFactor`, `iEngineVibrateOffset`, `byRepairSpeechPlayed`, `byLappedStatus`, `bySfxCooldown` |

The zero-field replay implicated `byThrottlePressed` in RNG branching and
confirmed `iControlType`; both are carried. `iSteeringInput` and
`iBankingSteerOffset` are carried because the first replayed human tick reads
steering history. `byCollisionTimer` is carried because it gates collision
response. `byEngineStartTimer` is explicitly movement state: it gates
acceleration and is an RNG-dependent host decision, so it is carried and has a
nonzero encode/decode range test. The additions make `tNetCarExtra` 84 bytes
and `tNetCarFullState` 148 bytes. Seven checkpoint cars fit in one packet.

## Replay cost

On the Windows ReleaseSafe E0 runs, one human car plus fifteen puppets measured
0.015 to 0.016 ms per simulated tick over 1000 ticks. The test prints the measurement on
every run so later E4-S3 work can compare it with the 500 ms replay budget.

## Multi-process harness

Both the game and server route `--net-harness` to the same headless TCP command
loop. Each virtual tick drains pending UDP datagrams before stepping; E1-S2
will replace this raw E0 pump with the channel-level `NetPump`. The E0 proxy and
Python smoke test cover transport and stepping only, as required by E0-S6.
