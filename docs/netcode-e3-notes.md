# NET-E3 implementation notes

## E3-S1 host tick: inputs in, simulation, snapshots out

`net_host.c` owns the authoritative host tick. `net_input.c` (new, `KEEP` in
`roller-core`) owns the input-batch and input-feedback codecs and the D9 clamp,
so E4-S1's client can share them.

### API

```c
tNetHost *NetHostCreate(tNetSessionHost *pSession, tNetLobbyHost *pLobby);
int NetHostBeginRace(tNetHost *pHost);            /* once the lobby has released */
void NetHostPump(tNetHost *pHost);                /* frame loop: input feedback */
int NetHostTick(tNetHost *pHost, uint32 uiTick);  /* one host tick */
uint32 NetHostNextTick(const tNetHost *pHost);
int NetHostSnapshotAt(const tNetHost *pHost, uint32 uiTick, tNetSnapshot *pSnapshot);
int NetHostPlayerStats(const tNetHost *pHost, uint8 byPlayerIdx, tNetHostPlayerStats *pStats);
```

The expected frame order is `NetPump()`, then the session pump, the lobby pump,
and `NetHostPump()`, followed by as many `NetHostTick` calls as the clock hands
out. `NetHostTick` only accepts the next consecutive tick, so every labelled
tick is simulated, the E2-S4 rule.

### Ownership at race start (4.11)

`NetHostBeginRace` requires `net_mode == NET_MODE_MODERN` and a released lobby.
It reads the frozen roster and sets `human_control[car]` to the player's
`byHumanControl` for every car of every `RACING` player, and to 0 for every
other car. The number of cars (1 or 2) must match the `byLocalPlayers` the
player joined with, and every car must be below `numcars` and owned only once;
otherwise the call fails and changes nothing. The cars of a player dropped at
the loading barrier (R1 NET-FIX-3) therefore start under AI, and the rest of the
takeover bookkeeping is still E5-S2's. The input queues are keyed by player and
read from that player's car list, so a two-car player (E2-S5) is already routed.
Only the one-car case is exercised.

### Input queues, range check, and D9 clamp

Each racing player has a 64-slot queue indexed by `tick % 64`. A batch is
rejected whole (`uiRejectedBatches`) if it is malformed or if its
`byLocalPlayers` differs from the player's car count. For each tick in an
accepted batch, the range check runs before anything is indexed:

- `tick < uiNextTick`: ignored and not counted. Every redundant batch carries
  old ticks.
- `tick >= uiNextTick + 48`: dropped and counted in `uiFutureInputs` and
  `g_netStats.iFutureInputs`.
- otherwise: stored, unless the slot already holds that tick. Redundant copies
  do not overwrite the first arrival.

The D9 clamp (`NetInputClamp`) is applied on acceptance:

- Flags outside `ACCEL | BRAKE | UPGEAR | DOWNGEAR | SPECIAL | PHONE_THROTTLE`
  are stripped. That covers the legacy `FLAG_DISCONNECT`, `FLAG_MASTER_CHANGE`
  and `FLAG_FINISHED` bits and F1 to F4, which are chat in modern mode.
- Steering is limited to +/- `iSteeringSensitivity << 8` for the car's engine,
  the range `readuserdata` itself clamps `rud_swheel` to.
- Clamped inputs are counted in `uiClampedInputs`.

"Forbidden flags rejected" is implemented as this clamp, per D9: the tick still
runs with the permitted bits.

At tick T, a player with no input for T repeats its last input, and the miss is
counted in `uiLateInputs` and `g_netStats.iLateInputs` (4.3 step 1). The tick's
inputs go through `NetSimWriteTickInputs` and then `control_one_tick()`. AI cars
get zero input.

### Snapshots, the ring, and own-car state

Snapshots are built on every tick where
`(T - uiStartTick) % bySnapshotInterval == 0`, after `control_one_tick()`, with
`NetSnapshotBuild`. They carry the tick context and `uiRandomState`.

- **Ring.** The host keeps them in a 64-entry ring. `NetHostSnapshotAt` serves
  anything within 600 ms of the newest, rounded up to ticks (22 at 36 Hz), for
  E3-S4's delta baselines.
- **Sending.** Each connected racing player gets the full snapshot (unreliable),
  then `NET_MSG_OWN_CAR_STATE` for the same tick (unreliable) covering every car
  it owns. E3-S1 sends full snapshots only; deltas are E3-S4.
- **Own-car wire format.** `NetSnapshotEncodeOwnCarState` and
  `NetSnapshotDecodeOwnCarState` (in `net_snapshot.c`) implement plan 5.6 with
  an explicit little-endian walk of `tNetCarExtra`. That is ten floats, seven
  `int32`, two `int16`, sixteen bytes and four `int16`, with static-asserted
  offsets. The decoder checks structure only: count 1 or 2, car below
  `MAX_CARS`, zero padding, no duplicate car. An extra is never written to a
  `tCar` by this decoder. Installing one goes through
  `NetSnapshotDecodeCarFull`, which carries the D23 validation.
- **Placeholder fields.** `byRaceState` carries the race-clock phase
  (`NET_RACE_START_PRE_START` or `NET_RACE_START_RUNNING`, chosen from
  `game_frame >= 145`) until E5-S1 defines the race state machine. E3-S2 now
  supplies `uiLastEventSeq`; `byPaused` is 0 until E5-S1.

### Input feedback

`NetHostPump` sends `NET_MSG_INPUT_FEEDBACK` (unreliable, 12 bytes) to every
connected racing player every 250 ms of transport time. The fields are:

- `uiHostTick`: the next tick the host will consume, which is the lower edge of
  the accept window.
- `unLateInputs` and `unFutureInputs`: **cumulative since race start**,
  saturating at 65535, so a lost feedback packet loses no information. E4-S1
  diffs consecutive values.
- `nArrivalMarginTicks`: last tick of the most recently accepted batch minus
  `uiNextTick` at arrival, clamped to `int16`. It is positive when input arrives
  early.

### Session and lobby changes

- **Session.** The session pumps used to drop every message that was not
  reliable-ordered, which would have discarded all race traffic. Only join
  control is now held to that rule (`JOIN_REQUEST`, `JOIN_ACCEPT`,
  `JOIN_REFUSE`, `SESSION_CONFIG`). Other messages reach the callback with their
  flags.
- **Lobby.** The lobby now enforces reliable-ordered on its own five types
  itself. Any other message goes to a new race callback, but only once the race
  is released: `NetLobbyHostSetRaceCallback` only for players the roster has as
  `RACING`, and `NetLobbyClientSetRaceCallback` on the client. `NetHostCreate`
  registers the host's.

### Acceptance test

`zig build test-net-host` (CMake `net-host`, which needs
`ROLLER_NET_TEST_ASSETS`) runs `tests/net_host_test.c` against the headless
world. It uses `NetHeadlessInit` with 16 cars on `TRACK5.TRK` and runs a host
with three clients on the transport simulator at 100 ms one-way latency. It goes
through join, ready, the loading barrier and release, then Pre-Start and 2000
running ticks at 36 Hz. Each client sends an 8-tick redundant batch 8 ticks
ahead of the host after every host tick. The race takes about 1 s of wall time.

What it asserts:

- **Remote cars follow the scripted inputs.** After a 16-tick warm-up, the input
  the host wrote for every simulated tick equals that client's scripted input
  after the D9 clamp. The expected clamp is restated independently in the test.
  The late count stops growing after warm-up (8 late ticks per client, all
  before the first batch lands), and every scripted car advances its
  `iLastValidChunk`.
- **Snapshot cadence exact.** Each client receives every snapshot from the start
  tick onwards at the interval (1073 per client), with no gaps. The newest is
  within one interval of the host's last tick.
- **Context.** Every snapshot's context `iGameFrame` and `uiRandomState` equal
  the host's values at that tick, and `iGameFrame` steps by exactly the interval
  between consecutive snapshots.
- **Own-car state.** It arrives for every snapshot tick, for the client's car,
  and is byte-identical to the host's full-state extra for that tick.
- **Forbidden flags.** No forbidden bit ever reaches `copy_multiple`. Client 2
  sends `FLAG_DISCONNECT`, F1 and the two undefined high bits every 7th tick and
  full-scale steering every 11th, and 475 of its inputs are clamped.
- **Horizon.** A batch 200 ticks ahead adds exactly 8 to `uiFutureInputs` and to
  the client's feedback. A batch 100 ticks old changes no counter. A batch
  claiming two local players for a one-car player is rejected whole.
- **Feedback.** It arrives at the 250 ms cadence, and its final counts equal the
  host's.
- **Ring.** The newest snapshot and one 22 ticks older are served. One 24 ticks
  older, a non-snapshot tick and a future tick are not.
- **Phone throttle.** The race is rerun from the same saved moment for 546 host
  ticks, with client 1 sending `BUTTON_FLAG_ACCEL` instead of
  `BUTTON_FLAG_PHONE_THROTTLE`. The world hash (every `tCar`, RNG state and draw
  count) is identical after every tick. So a phone-throttle client accelerates
  on a desktop host exactly as a throttle-bit client does.

Mutation checks, run once by hand and reverted:

- Skipping the host's clamp fails the forbidden-flag assertion.
- Switching `net_mode` back to legacy after `NetHostBeginRace` fails the
  phone-throttle car's progress assertion.
- Disabling the flag strip inside `NetInputClamp` fails the codec unit check.

**Deviation from the acceptance text.** The plan says "harness host and 3
clients", meaning the E0-S6 multi-process harness. That harness only drives the
E0 raw-UDP stepping loop and has no session, lobby, or input commands, and the
clients in this story simulate nothing. So the test follows the E2-S2, E2-S4 and
R1 pattern instead: one process, one world (the host's, D13), and the clients as
message endpoints on the deterministic transport simulator. A multi-process
version belongs with E4-S1, when clients have worlds of their own, or with
E8-S1's scenario library.

### Not done here

- **Game wiring.** `game_tick_step` still runs the E2-S4 local path in modern
  mode, and `net_frontend_lobby.c` does not create a `tNetHost`. Switching the
  listen host to `NetHostTick` now would leave the host's own player unable to
  drive, because its loopback client sends no input until E4-S1. Wiring the
  listen host is E3-S3, and the dedicated server loop is E2-S6.
- **Legacy-call trap.** The acceptance item "the legacy-call trap stays silent"
  cannot be checked because the trap does not exist yet (E7-S1). The host path
  calls no `network.c` or `rollercomms.c` function.
- **Mid-race drops.** A player dropped mid-race stops being forwarded inputs,
  and its car repeats the last input, counted as late, until E5-S2 hands it to
  the AI.
- **Loss and reordering.** The acceptance run uses no loss or reordering. The
  code paths for loss are the late-input repeat and the redundant batch, and
  E4-S1's acceptance (3 percent loss) is the first test that stresses them.

## E3-S2 semantic events and world changes

Implemented on 2026-09-21. `net_event.c` is a new `roller-core` source for the
explicit little-endian codecs; `net_host.c` owns post-tick detection, numbering,
reliable ordered broadcast, and the persistent mutated-world set.

### Event contract

Immediately after `control_one_tick`, the host diffs every car against its
pre-tick lap and finished state. It emits, in car order:

- `NET_EV_LAP_COMPLETE` for each completed lap. Crossing onto lap 1 starts the
  race and emits nothing; a transition to `byLap == N` emits completed lap
  `N - 1`. `iArg0` is that completed-lap number and `iArg1` is
  `fPreviousLapTime` in rounded milliseconds.
- `NET_EV_FINISHED` once when a live car enters `finished_car[]`. `iArg0` is its
  zero-based race position and `iArg1` is the host's finisher count.
- `NET_EV_DESTROYED` instead when the transition belongs to a car with no lives.
  Its arguments are the attacker and the host `Destroyed` count.
- `NET_EV_KILL` once for each increment of an attacker's `byKills`. `iArg0` is
  the victim car when the same tick's life decrement identifies it (or -1), and
  `iArg1` is the attacker's new cumulative kill count.

`byPlayerIdx` is the frozen roster owner, or 255 for an AI car. Events and world
changes share one session sequence starting at 1. Every message is
`NET_MSG_RELIABLE | NET_MSG_ORDERED`; snapshots built later in the same tick
carry the final `uiLastEventSeq`. Queue failure makes `NetHostTick` report
failure after advancing the already-simulated tick, while the sequence gap
visible in a snapshot remains available to E4-S4/E5-S3 recovery logic.

### World changes

At race start the host validates and captures all loaded `TRAK_LEN` chunks.
After every tick it compares all three mutable grips and all three complete
32-bit `TrakColour` words against that shadow. Changed chunks update a
persistent bitmap and latest-state array; `NetHostWorldChangeAt` exposes that
set for the future checkpoint implementation.

A successful LOVEBUN use is identified from its pre-tick design, cooldown, ammo
and chunk plus its post-tick ammo consumption. Each use gets one world change
containing exactly the changed chunks from current through current + 15,
wrapping at `TRAK_LEN`. Two uses on the same tick still get distinct sequence
numbers; overlapping entries carry the common final post-tick state. Any changed
chunk outside a detected LOVEBUN range is still sent in batches of at most 64,
so a future world mechanic cannot mutate the host silently. The persistent
bitmap never clears when a chunk later returns to its original values, because a
checkpoint must still carry its current state.

`NetWorldChangeCapture` rejects a loaded chunk whose grip cannot index the
14-entry `surface[]` table. The decoder rejects a zero sequence, non-zero
padding, a count outside 1..64, a length mismatch, duplicate chunks, a chunk
outside the caller's loaded track length, or any grip outside 0..13 before
publishing output. Track colours are not narrowed or masked. The event codec
likewise rejects unknown types, bad car/player bounds and malformed lap/finish
arguments without changing its output.

### Acceptance and topology

`zig build test-net-host -Doptimize=ReleaseSafe` retains the E3-S1 2000-tick
host race and adds the E3-S2 checks:

- deterministic lap and finish transitions prove one event per car per
  transition, while natural transitions over the full race are counted too;
- a scripted human car executes the real design-12 `humancar` LOVEBUN path;
  every client receives exactly one world-change message for that use, with
  precisely the host-diffed chunks, grips, low colours and high flag bits;
- all three clients observe the same gap-free shared commit sequence, and every
  snapshot's `uiLastEventSeq` is monotonic and reaches the final host value;
- the host's retained mutated-chunk entries equal the live post-tick world;
- codec probes cover little-endian layout, padding, bounds, grip range,
  duplicate chunks, malformed types and rejection without partial output.

The test remains one host world with three message-only clients per D13. Its
existing simulation seam calls the real `NetSimWriteTickInputs` and
`control_one_tick`; it only brackets deterministic event probes so a synthetic
early finisher does not alter the later race. No `control.c` seam or original
function body changed.

### Not done here

E4-S4 owns buffering and applying these commits on a client. Checkpoint
serialization of the retained world set remains E5-S3. Race-state, results,
join/leave, takeover and rejoin event types remain with their lifecycle stories.
The E7-S1 legacy-call trap still does not exist; this path calls no legacy
networking function.

## E3-S3 listen host local player

Implemented on 2026-09-20. The native modern race now leaves the E2-S4
placeholder path and runs the host or client race object created by
`net_frontend_lobby.c` after the loading barrier releases.

### Lifetime and frame loop

- A listen host creates and begins `tNetHost`; a remote node creates and begins
  `tNetClient`. Race objects are destroyed before their lobby and session
  owners.
- `UpdateSDL` still pumps the channel, session and lobby every rendered frame.
  `NetFrontendPump` then calls `NetHostPump` or `NetClientPump`.
- The listen host keeps the SDL timer as its authoritative tick source. A remote
  client no longer lets `tick_clock_step` add to `iTicksPending`; each frame
  mirrors `NetClientTicksDue()` into it, so the E4-S1 dilated accumulator owns
  the count while the existing four-tick drain cap stays in force.
- `game_tick_step` obtains the next consecutive E2-S4 race-clock label, samples
  `readuserdata`, and dispatches exactly once to `NetHostTick` or
  `NetClientTick`. It does not call `control_one_tick` again afterward.

### Listen-host input and one-world rule

The listen host's authenticated loopback session still owns a normal frozen
roster entry. Its input does not make a UDP round trip: the new
`NetHostSetLocalInputs` writes the current tick into that roster player's normal
host input queue, applies the same D9 clamp as remote input, and then
`NetHostTick` consumes it. It rejects the wrong tick, player, or local-player
count before indexing the queue.

This is deliberately not a `NetClientTick` followed by `NetHostTick`. A process
has one world (D13), and doing both would simulate that world twice. The listen
host renders its authoritative world, so it needs no prediction or correction.
Race start clears the net overlay and explicitly leaves it at RTT 0, zero
corrections and `NET_PREDICT_FULL`; it clears all puppet flags and sets
`net_sim_puppet_hook` to NULL.

The same `readuserdata` path now routes F5-F8 strategy messages through
`NET_MSG_CHAT` in modern mode. Those bits are removed from tick input, as
required by canonical input.

### Verification

Passed on Windows in ReleaseSafe:

```powershell
zig build -Doptimize=ReleaseSafe
zig build test-net-foundations test-net-full-state-coherence test-net-host test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix
```

`test-net-host` now also probes the listen-host input seam during both host
runs: the wrong tick and wrong player count are rejected, and current-tick input
is accepted without changing the established world-hash comparison.

The plan's acceptance is manual. No interactive two-instance race was run in
this implementation session, so the overlay criteria still need an in-game smoke
check. The legacy-call trap still does not exist; it belongs to E7-S1.

## E3-S4 delta snapshots with explicit baselines

Implemented on 2026-09-22. The changes are intentionally uncommitted.

### Baseline selection and fallback

Every input batch already carries the client's newest successfully decoded
snapshot tick. The host now uses that value per player and looks up the exact
snapshot in its existing 600 ms shared ring. If the entry is present, the host
encodes `NET_MSG_SNAPSHOT_DELTA` against it. If it is missing, expired, too
large, or not smaller than the 1104-byte full state, the host sends
`NET_MSG_SNAPSHOT` instead.

Full snapshots are also forced on the first snapshot, every two seconds, and on
the first tick after a checkpoint. `NET_MSG_OWN_CAR_STATE` remains a separate
full message for every snapshot tick; it is never delta-encoded. Host player
stats expose full and delta counts plus snapshot payload bytes so the bandwidth
contract can be measured without including unrelated traffic.

### Client reconstruction

`NetSnapshotDeltaTicks` reads the current and base tick from the explicit
little-endian delta header without publishing either value on a structurally
invalid message. The client locates the exact base in its retained snapshot
ring, then calls the existing full delta decoder. The reconstructed snapshot
passes the same loaded-world, commit-watermark, staleness and D23 validation as
a full snapshot before it enters the ring.

A delta with an absent or expired base is counted and discarded. It does not
advance `uiNewestSnapshotTick`, which is the value reported in the next input
batch. A malformed delta is counted as rejected. Accepted full and delta
messages share the existing reconciliation, authoritative-commit, ramp and
puppet paths.

### Acceptance

The live 16-car host acceptance reconstructs every delta and compares it
byte-for-byte with the full snapshot retained by the host for that tick. The
three clients each received 1073 snapshots. The two uninterrupted clients saw 33
full and 1040 delta snapshots and used 51.7 percent fewer snapshot payload bytes
than an all-full stream. The blackout client saw 79 full and 994 delta snapshots
and still used 48.8 percent fewer bytes, above the required 40 percent
reduction.

Client 0 has a deterministic three-second input/acknowledgement blackout while
its driving input is supplied through the normal host queue seam. Its reported
baseline ages out, the host falls back to a full snapshot after 18 ticks, and
deltas resume 5 ticks after the link is restored, inside one 200 ms RTT. The
test also checks the two-second maximum between forced full snapshots.

The real client acceptance at both 36 and 100 Hz omits one encoded delta and
then reconstructs a later delta from the same base. It next delivers a delta
before its base, verifies that the reported tick does not move, delivers the
full base, and reconstructs the same delta exactly. The existing 3 percent loss
run now uses live delta traffic for the full E4/E5 prediction, pause, commit and
rejoin suite. Checkpoint recovery verifies that a forced full snapshot arrives
after the client clears its history.

Passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-host test-net-client -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build test-net-harness -Doptimize=ReleaseSafe `
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

The test cache was copied into the repository-local ignored cache because the
sandbox could read but not update the normal user cache. The temporary copied
cache was removed after verification. The full native ReleaseSafe build and the
harness passed. Source counts remain Linux 95, macOS 95, Windows 98, Android 95
and Emscripten 93; the roller-core manifest remains 109 translation units; all
18 selected Python tests passed. The Android debug APK rebuilt for arm64-v8a and
x86_64. A CMake configure was not run because this machine still cannot find
`SDL3Config.cmake`; its source-list test passed. The E7-S1 legacy-call trap
still does not exist.
