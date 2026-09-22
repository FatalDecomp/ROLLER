# Netcode E5 notes

## NET-E5-S1: race state, pause and results

Implemented on 2026-09-21 and committed in `cd162d2`.

### Race lifecycle

`net_race_state.c/.h` owns the small on-track lifecycle shared by host and
client. A race object starts in `NET_RACE_PRE_START`; the authority publishes
the transition to `NET_RACE_RUNNING` when the simulated `game_frame` reaches
145, then publishes `NET_RACE_OUTCOME_SETTLED` when every active player's car
or cars have finished or been destroyed. `NET_RACE_STOPPED` is reserved for a
later explicit race-exit workflow.

Both transitions are reliable ordered `NET_EV_RACE_STATE` commits. The host
then publishes one `NET_EV_RESULTS` commit containing the authoritative total
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
  with Outcome-Settled and Results behind the same deliberate sequence gap.
  It verifies exact result totals and idempotent re-publication.
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

Implemented on 2026-09-21. The changes are intentionally uncommitted.

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
flight, lands through the normal simulation, and only then reaches ordinary
AI driving. This also avoids calling `SetEngine` or rebuilding a car at the
ownership boundary.

The host publishes one reliable ordered `NET_EV_AI_TAKEOVER` commit for the
whole group. `byCarIdx` is the first car, `byPlayerIdx` is the dropped player,
`iArg0` is the second car or -1, and `iArg1` is the group size (1 or 2). The
codec validates that shape before client mutation. One commit makes the
split-screen transition atomic in the shared sequence.

Clients retain takeover per car and republish `human_control[car] = 0` with
the other host commits. An older puppet snapshot or a rollback therefore
cannot restore stale human ownership. E5-S3 must clear that retained bit when
it applies `NET_EV_PLAYER_REJOINED` and restores the checkpoint roster.

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
- the remaining human car settles the race, with authoritative results of
  three finishers and one human finisher;
- a later snapshot carries AI ownership for both dropped cars.

The client acceptance inserts an AI takeover into the existing deliberately
gapped commit stream at 36 and 100 Hz. It forces an older snapshot and a
manual stale ownership write after the commit, and verifies idempotent
republication returns the remote car to AI both times. The event codec test
also rejects inconsistent one-car/two-car payloads.

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

Source counts remain Linux 94, macOS 94, Windows 97, Android 94 and
Emscripten 92. The roller-core manifest remains 108 translation units. All 18
selected Python tests pass.

Not run: Android `assembleDebug`, a CMake configure, a manual two-instance
disconnect/rejoin race, or the E7-S1 legacy-call trap (it does not exist yet).
