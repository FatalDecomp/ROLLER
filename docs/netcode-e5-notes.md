# Netcode E5 notes

## NET-E5-S1: race state, pause and results

Implemented on 2026-09-21. The changes are intentionally uncommitted.

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
