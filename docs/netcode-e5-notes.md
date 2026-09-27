# Netcode E5 notes

## NET-E5-S1: race state, pause and results

Implemented on 2026-09-21 and committed in `cd162d2`.

### Race lifecycle

`net_race_state.c/.h` owns the small on-track lifecycle shared by host and
client. A race object starts in `NET_RACE_PRE_START`; the authority publishes
the transition to `NET_RACE_RUNNING` when the simulated `game_frame` reaches
145, then publishes `NET_RACE_OUTCOME_SETTLED` when every active player's car or
cars have finished or been destroyed. `NET_RACE_STOPPED` is reserved for a later
explicit race-exit workflow.

Both transitions are reliable ordered `NET_EV_RACE_STATE` commits. The host then
publishes one `NET_EV_RESULTS` commit containing the authoritative total
finishers and human finishers. These events use the same `uiEventSeq` as lap,
kill, finish, destruction and world-change commits, and snapshots carry their
watermark plus the current lifecycle state.

The client accepts only consecutive lifecycle transitions. A results commit is
accepted only after Outcome-Settled and only when its totals agree with the
retained preceding finish/destruction commits. Result publication remains
idempotent: the retained host commits rebuild `finished_car[]`, `carorder`,
`finishers`, `human_finishers` and `Destroyed` after a rollback or puppet write.

### Pause contract

`NET_MSG_PAUSE` now has an explicit little-endian codec. It is reliable ordered
and carries a nonzero 16-bit revision, the paused bit and the host's next tick.
Revisions use serial-number comparison; a stale revision never mutates client
state. A listen host can call `NetHostSetPaused`; a dedicated configuration or
one with pause disabled is refused.

Pause freezes tick availability, not the frame pump. `NetHostTick`, local host
input placement, `NetClientTicksDue` and `NetClientTick` all refuse while
paused. The client continues pumping its channel/session and clock estimator,
but clears accumulated tick debt and does not add wall time to the simulation
accumulator. On resume the next tick is exactly the previous tick plus one, so
the input, prediction and context rings contain no artificial pause gap.

The game seam mirrors the race object's pause bit into the legacy `paused`
global, clears pending ticks on both host and client, and lets only the listen
host's pause key request a transition. A remote pause key cannot pause the
session. Networking continues to pump from the frame loop throughout.

### Acceptance topology

- The host acceptance now publishes Running, three human finishes,
  Outcome-Settled and Results through the real shared commit sequence. Every
  message-only client receives the same gap-free stream and final snapshot
  watermark; result totals are 3/3.
- The client acceptance extends the existing three-lap/collision result probe
  with Outcome-Settled and Results behind the same deliberate sequence gap. It
  verifies exact result totals and idempotent re-publication.
- At both 36 and 100 Hz, the integrated client test pauses for 30 seconds of
  virtual time with 5 percent loss and the normal 10 second connection timeout.
  Both connections remain live, neither timeline advances, the first resumed
  context is consecutive, and delayed revision 3 is ignored after revision 4.
- The foundations suite checks lifecycle transition guards, pause encoding,
  stale revisions, and dedicated-server refusal.

### Verification

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core
```

The source-set counts are Linux 94, macOS 94, Windows 97, Android 94 and
Emscripten 92. The roller-core manifest contains 108 translation units. All 18
selected Python tests pass.

Not run: Android `assembleDebug`, a CMake configure, a manual two-instance
pause/results race, or the E7-S1 legacy-call trap (it does not exist yet).

## NET-E5-S2: disconnect to AI via ownership transfer

Implemented on 2026-09-21 and committed in `7f4a8a1`.

### Transfer contract

`NetHostPump` now checks connection expiry on the channel/session clock every
frame, including while simulation is paused. An expired active player is
validated as one ownership group, then all of its cars transfer together:

- `human_control[]` becomes zero for every owned car;
- `abyCarOwner[]` becomes `NET_EVENT_NO_PLAYER`, so later finishes are AI
  finishes and the dropped player is excluded from active settlement;
- queued and repeated player inputs are cleared, and the host player becomes
  inactive;
- the lobby roster retains the name and both reserved car indices in
  `NET_PLAYER_DROPPED` for E5-S3 rejoin.

No `tCar` field is changed by takeover. In particular, `iControlType` remains
the authoritative physics mode, so an airborne car continues its existing
flight, lands through the normal simulation, and only then reaches ordinary AI
driving. This also avoids calling `SetEngine` or rebuilding a car at the
ownership boundary.

The host publishes one reliable ordered `NET_EV_AI_TAKEOVER` commit for the
whole group. `byCarIdx` is the first car, `byPlayerIdx` is the dropped player,
`iArg0` is the second car or -1, and `iArg1` is the group size (1 or 2). The
codec validates that shape before client mutation. One commit makes the
split-screen transition atomic in the shared sequence.

Clients retain takeover per car and republish `human_control[car] = 0` with the
other host commits. An older puppet snapshot or a rollback therefore cannot
restore stale human ownership. E5-S3 must clear that retained bit when it
applies `NET_EV_PLAYER_REJOINED` and restores the checkpoint roster.

### Acceptance topology

The host acceptance adds a two-client race where the first client owns cars 0
and 1 and the observer owns car 2. At exactly 20 seconds both split-screen cars
are converted to airborne state, the host is paused, and all traffic from the
split client is cut. The normal 10-second channel timeout still fires while no
simulation ticks run. The test proves:

- both airborne `tCar` objects are byte-identical across the transfer;
- both ownership values change together and the roster retains both cars as
  dropped;
- the observer receives exactly one correctly shaped takeover commit;
- after resume both cars land and move under real AI simulation, then finish;
- the remaining human car settles the race, with authoritative results of three
  finishers and one human finisher;
- a later snapshot carries AI ownership for both dropped cars.

The client acceptance inserts an AI takeover into the existing deliberately
gapped commit stream at 36 and 100 Hz. It forces an older snapshot and a manual
stale ownership write after the commit, and verifies idempotent republication
returns the remote car to AI both times. The event codec test also rejects
inconsistent one-car/two-car payloads.

### Verification

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core
```

Source counts remain Linux 94, macOS 94, Windows 97, Android 94 and Emscripten
92\. The roller-core manifest remains 108 translation units. All 18 selected
Python tests pass.

Not run: Android `assembleDebug`, a CMake configure, a manual two-instance
disconnect/rejoin race, or the E7-S1 legacy-call trap (it does not exist yet).

## NET-E5-S3: rejoin via checkpoint and resync

Implemented on 2026-09-22 and committed in `310cfbc`.

### Authenticated generation replacement

A recovering client keeps its session token and opens a fresh channel connection
at generation + 1. `NET_MSG_REJOIN_REQUEST` repeats the token, generation and
player index from the packet identity. The channel accepts this one
credentialled replacement operation, resets every reliability window, and drops
later packets from the old generation before delivery. The session then
cross-checks all three values and asks the race host to authorize the retained
player against the channel clock and `NET_REJOIN_GRACE_MS`.

An active player may replace its connection before host-side timeout detection;
ownership stays unchanged and no AI-takeover commit is emitted. A dropped player
inside the grace window reclaims the same player index and complete car group,
clears old queued input, restores `human_control[]` and host ownership, and
publishes one group-shaped `NET_EV_PLAYER_REJOINED`. A token presented after the
grace window is refused and cannot claim a new roster index. The remote frontend
detects an expired race connection from the frame pump, opens the generation+1
connection to its retained peer, and starts the same client recovery path
without blocking rendering or network pumping.

### Ordered checkpoint

New `net_checkpoint.c/.h` owns explicit little-endian codecs for the header,
full-car batches, player roster, mutated-world batches and matching END tick.
The host captures checkpoint C at its newest completed tick and queues every
part reliable ordered. Cars are split at the existing seven-car payload bound;
world changes are batches of at most 64. The header carries the shared commit
watermark, wire tick context, pause revision, RNG state, all ramp timing and
part counts. A forced full snapshot follows on the next simulated tick.

The client stages all parts without touching the world. It rejects duplicate
cars or chunks, inconsistent counts, invalid roster ownership, hostile full car
state, invalid loaded-world bounds and invalid ramp timing. END installs only a
complete transaction, in D18 order: lifecycle/roster/world and RNG, ramps plus
rebuilt geometry, then every car. Re-encoded world poses are asserted against
the checkpoint. Non-local cars become puppets and the local group clears
retained AI-takeover state before human ownership is restored.

Phase 1 clears the input, prediction and context rings plus snapshots and render
corrections. `NetSimBootstrapContext` builds `context[C]` solely from
`tNetSimContextWire`, `uiRandomState` and a fresh input-ring position; it does
not read pre-drop history. The client enters Resyncing and issues a new
checkpoint request after two channel-clock seconds without a qualifying
snapshot.

### Simulated catch-up

The first paired snapshot and own-car state at S > C starts Phase 2. The client
installs ramps before the complete rollback group, bootstraps `context[S]` from
that snapshot, fills `[S, destination]` with neutral input including the
preceding slot, and replays `(S, destination]` through `control_one_tick` with
`net_sim_replaying` set. Prediction and context history are rebuilt after every
tick, so `uiRampTick == uiClientTick == destination` and `iGameFrame` is
consecutive across the span. Checkpoint world state is republished after the
catch-up because it is host-current state and never rollback state.

The destination is S plus half RTT plus the normal input lead. Recovery then
enters the prediction mode selected by that RTT; the acceptance's 1500 ms RTT
therefore enters `NET_PREDICT_DELAYED` immediately instead of attempting an
over-budget correction. No live ticks or input messages are produced during
Installing or Resyncing.

### Acceptance topology

- The session test replaces an active generation-1 connection with generation 2
  on the same channel. The host keeps the same connection/player identity,
  accepts traffic from generation 2, and counts but never delivers a scripted
  stale generation-1 packet.
- The integrated client acceptance runs at 36 and 100 Hz. It stops the client
  for 15 seconds, observes the ordinary 10-second drop-to-AI transition, rejoins
  with the retained token, and delays the checkpoint over a 750 ms one-way link.
  The ordered checkpoint restores a deliberately corrupted mutated-chunk grip,
  both recovery phases begin with empty history, neutral catch-up rebuilds
  consecutive contexts, and the recovered high-RTT client is playable in delayed
  mode.
- The host acceptance advances beyond the 60-second rejoin grace and verifies
  that generation 2 is refused while the dropped roster entry and both reserved
  split-screen cars remain unchanged.

The one-process client acceptance follows D13: the host uses its existing
simulation seam, queues the checkpoint and forced newer snapshot, then freezes
its test world while the client installs. A literal two-process drop at 20 s,
rejoin at 35 s and human finish remains a manual/E8-S1 scenario.

### Verification

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core
```

Source counts are Linux 95, macOS 95, Windows 98, Android 95 and Emscripten 93.
The roller-core manifest contains 109 translation units. All 18 selected Python
tests pass.

Not run: Android `assembleDebug`, a CMake configure, a manual two-instance
disconnect/rejoin race, or the E7-S1 legacy-call trap (it does not exist yet).

## NET-E5-S4: timeouts, errors and user messaging

Implemented on 2026-09-22. The code is in `95be6f1`; the final verification
detail below remains uncommitted.

### Modern-path status contract

`NetJoinRefuseReasonString` is the single user-facing mapping for every join
refusal. The modern lobby uses it instead of collapsing failures into a generic
message. A new `NET_JOIN_REFUSE_REJOIN_EXPIRED` reason distinguishes a retained
token presented after the 60-second grace window from a malformed request; the
decoder accepts the new bounded enum value and the existing packed refusal
message is unchanged.

`NetClientStatus` publishes one non-blocking in-race indicator by priority:

- `Connection lost, retrying` while the replacement generation is waiting;
- `Resynchronising` after authentication while checkpoint/resync is active;
- the specific refusal reason if recovery is refused;
- `High latency: delayed controls` whenever prediction is delayed.

The frontend also changes an unanswered retry to `Rejoin window expired` when
the channel clock passes the grace window. Rendering and both network pumps
continue throughout. The race HUD draws the active client indicator at the top
centre through the existing software HUD layer, which is also composited by the
GPU renderer.

### Host connection health

Each active host player now exposes smoothed channel RTT plus a one-second
late-input rate. The warning threshold is 10 percent of simulated ticks; its
window stops while paused because no input is consumed, and it is cleared when a
player successfully rejoins. The listen-host HUD renders one row per active
player as `NAME: N ms`; a trailing `!` is the warning icon while that player's
latest complete window is at or above the threshold.

The host acceptance proves the warm-up loss crosses the warning threshold, the
channel RTT is populated, and a later healthy window clears the warning. The
client acceptance checks the retry indicator during generation replacement and
the delayed-controls indicator throughout the existing 600 ms RTT hold at both
36 and 100 Hz. Session tests cover every refusal string, and the late rejoin
acceptance now requires the dedicated expiry reason.

### Verification

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core
```

Source counts remain Linux 95, macOS 95, Windows 98, Android 95 and Emscripten
93\. The roller-core manifest remains 109 translation units. All 18 selected
Python tests pass.

Not run: the story's manual two-instance proxy check at 100 percent mid-race
loss and sustained 600 ms, Android `assembleDebug`, a CMake configure, or the
E7-S1 legacy-call trap (it does not exist yet).

## NET-E5-S5: mobile and sleep robustness

Implemented on 2026-09-22. The changes are intentionally uncommitted.

### Android lifecycle contract

SDL3 delivers Android background and foreground notifications only through an
event watch, which may run on any thread. The Android watch therefore performs
only atomic bookkeeping. Entering the background clears pending simulation
ticks. The first main-thread `UpdateSDL` after foregrounding clears them again,
resets the render tick timestamp, releases all remembered touches, closes the
old accelerometer handle so it is reopened on demand, and notifies the modern
frontend before its normal network pumps run.

An active race always resumes through the existing authenticated
generation-replacement and checkpoint path, even if the old connection is still
inside the ten-second transport timeout. This prevents time asleep from becoming
hundreds of locally owed ticks on a short suspension. For a longer suspension
the same path takes the car back from AI. Listen hosts retain their loopback
peer address so their in-process client can use the same recovery. After the
session has moved to the replacement connection, the frontend removes the
retired generation from the channel. A channel acceptance cycles 32 replacements
through the bounded 16-connection table, so repeated app switches cannot exhaust
it.

The normal `Connection lost, retrying`, `Resynchronising`, and delayed-controls
statuses remain the only recovery UI states.

Android also disables the SDL screen saver for the game process and restores the
default at shutdown. This keeps the display awake without a Java-only window
flag and therefore covers the native SDL window for the whole run.

### Acceptance coverage

The existing host acceptance proves that `BUTTON_FLAG_PHONE_THROTTLE` received
from a phone is byte-identical to ordinary acceleration over 546 host ticks,
including RNG state. The client acceptance at 36 and 100 Hz proves that a 600 ms
RTT enters delayed prediction, holds it for 20 seconds with zero replay, then
recovers at 120 ms only after the three-second hysteresis. It also measures 100
forced 14-tick corrections; the final Windows run averaged 0.230 ms at 36 Hz and
0.240 ms at 100 Hz, respectively 0.83 and 2.40 percent of one tick. The
15-second generation-2 recovery case covers the longer-than-timeout sleep shape
and returns ownership through a checkpoint at both rates.

### Verification

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness test-phone-ui -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core

Push-Location android
$env:JAVA_HOME="$env:ProgramFiles\Android\Android Studio\jbr"
$env:GRADLE_USER_HOME="D:\source\repos\ROLLER\android\.gradle\user-home"
$env:ZIG_EXE="C:\Users\Steve\scoop\persist\zigup\zig\0.15.2\files\zig.exe"
.\gradlew.bat assembleDebug --rerun-tasks --no-daemon
Pop-Location
```

The clean Android rebuild compiled arm64-v8a and x86_64 and packaged the debug
APK. Source counts remain Linux 95, macOS 95, Windows 98, Android 95 and
Emscripten 93; the roller-core manifest remains 109 translation units; all 18
selected Python tests passed.

Not run: the manual device test that switches away for 10 seconds and returns,
or a real cellular drift across the prediction crossover. Those device checks
are still required before calling the story shipped.
