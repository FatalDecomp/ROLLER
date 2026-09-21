# NET-E4 implementation notes

## E4-S1 client tick timeline, input send, clock sync, lead control, rings

`net_client.c` owns the client side of a race: the monotonic tick timeline,
the host-clock estimator, the lead controller and its dilated accumulator,
canonical input sampling and sending, and the three rings of plan 4.5. It
shares `net_input.c` (the batch codec and feedback codec) with the host.

### API

```c
tNetClient *NetClientCreate(tNetSessionClient *pSession, tNetLobbyClient *pLobby);
int NetClientBeginRace(tNetClient *pClient);        /* once the lobby has released */
void NetClientPump(tNetClient *pClient);            /* frame loop: clock, lead, accumulator */
int NetClientTicksDue(const tNetClient *pClient);
int NetClientTick(tNetClient *pClient, const tCarInputData *pLocalInputs);
uint32 NetClientCurrentTick(const tNetClient *pClient);
int NetClientGroup(const tNetClient *pClient, uint8 *pbyCars);
int NetClientStats(const tNetClient *pClient, tNetClientStats *pStats);
int NetClientInputAt(...);       /* the three rings, indexed by client tick */
int NetClientPredictionAt(...);
int NetClientContextAt(...);
int NetClientSnapshotAt(...);    /* received host state, 600 ms of retention */
int NetClientOwnCarStateAt(...);
```

The frame order is `NetPump()`, the session pump, then `NetClientPump()`,
then exactly `NetClientTicksDue()` calls to `NetClientTick`. The accumulator
owns the tick count: `NetClientTick` fails when no tick is due, so the
timeline cannot be advanced by the caller's frame rate.

### Timeline and the accumulator (4.4)

`uiClientTick` is the newest simulated tick and moves by exactly one per
`NetClientTick`. `NetClientBeginRace` puts it at `uiStartTick - 1`, takes the
rollback group from the roster and writes `human_control[]` exactly as
`NetHostBeginRace` does, and clears all three rings.

Each pump adds `elapsed * tickRate * fTickScale` ticks to the accumulator.
Nothing else moves it, so ticks are never skipped or repeated. The one
exception is the join, where the accumulator is preloaded with the distance
to the target (capped at `NET_INPUT_HORIZON`): the client has been released
about half a round trip after the host started ticking, so it owes those
ticks. They are all simulated, just in a burst over the next few frames.

### Host clock estimate and lead

The estimate is an offset from the transport clock, updated by an EMA
(gain 0.1) from two kinds of observation, each corrected by half the
channel's RTT:

- a snapshot for tick T, which the host sends as soon as it has simulated T,
  so the host's clock was T when it was sent;
- input feedback, whose `uiHostTick` is the *next* tick the host will
  consume, so the host's clock was somewhere in the preceding interval and
  the sample is `uiHostTick - 0.5`.

The lead is `ceil((RTT/2 + jitter + one tick period + frame interval) /
period)`, with hysteresis so it only drops once the formula is 1.25 ticks
below it. Two deviations from the plan's formula, both deliberate:

- **The frame interval is included.** A batch queued during a tick is only
  transmitted by the next frame's `NetPump`, so the wait is part of the
  input's one-way latency. The measured frame time (EMA) is used, not a
  constant.
- **A feedback-driven bias.** If feedback reports the host's cumulative late
  count rising while the timeline is on target (within one tick of the
  target), the lead gains a tick, up to four. It decays a tick per 10 s of
  quiet. This covers links whose real one-way input delay is worse than
  `RTT/2 + jitter` predicts. The 100 Hz acceptance run settles at +2, and
  the mutation that removes input redundancy pushes it to +4.

The controller is proportional: `fTickScale = 1 - 0.05 * error`, clamped to
[0.9, 1.1], where the error is the timeline position minus
`host estimate + lead`. Time dilation is the only adjustment (4.4).

### Input, and what goes on the wire

Every tick samples the group's input, passes it through
`NetSimCanonicaliseInput` (4.13), records it in the input history, and sends
a `NET_MSG_INPUT` batch of the last `NET_INPUT_REDUNDANCY` ticks.

The batch always carries eight ticks, so `uiFirstTick` advances by exactly
one per client tick from the first batch onwards. Ticks before the start
carry neutral input, which the host ignores as old. The alternative -
shorter batches at the start - would have repeated `uiFirstTick` for the
first eight batches, which the acceptance forbids.

`uiLastDecodedSnapshotTick` is the newest snapshot the client has decoded
(4.8). E3-S4 will use it as the delta baseline.

### The three rings (4.5), recorded after the tick

All three are `NET_INPUT_HISTORY` (256) deep and indexed by client tick:

- the canonical `tCarInputData` per group car, recorded **before** the tick,
  because it is what the tick consumes;
- the prediction history, one `tNetCarFullState` per group car from
  `NetSnapshotEncodeCarFull`, recorded **after** the tick;
- the context ring, `tNetSimTickContext` from `NetSimCaptureContext`,
  recorded **after** the tick.

Recording both after the tick is what makes `context[N]` and `pred[N]` the
state the simulation enters tick N+1 with (4.3 step 7). The test asserts
exactly that, by capturing the live context immediately before tick N+1 and
comparing the whole struct against `context[N]`.

The tick itself runs `NetSimWriteTickInputs` and then `control_one_tick()`
with `net_sim_authority == NET_AUTHORITY_REMOTE` (4.14), restoring the
previous authority afterwards.

### Received host state

Snapshots and own-car states go into 64-entry buffers keyed by tick, with
anything older than `NET_SNAPSHOT_RETENTION_MS` behind the newest snapshot
refused (4.6). Own-car extras are stored in rollback-group order, and a
message whose cars are not the group's is rejected whole. `uiRampTick` is
kept for E4-S2's divergence check; it equals the newest simulated tick,
since ramps advance once per tick inside `control_ticks`.

`g_netStats` gets RTT, jitter, tick scale, snapshot age, and the host's
cumulative late and future counts from feedback (E0-S5's overlay).

### Host and channel changes

- **`NetHostSetSimulation`** (`net_host.c`): replaces the simulation step of
  `NetHostTick` (`NetSimWriteTickInputs` then `control_one_tick`). This
  exists for the acceptance test below; NULL is the shipping behaviour.
- **Batch timeline statistics** in `tNetHostPlayerStats`:
  `uiInputBatches`, `uiBatchTickGaps`, `uiBatchReorders`,
  `uiFirstBatchTick`, `uiNewestFirstTick`. Because `uiFirstTick` advances by
  one per client tick, `batches + gaps - reorders` is exactly the span
  `newest - first + 1`, and `gaps - reorders` is the number of batches lost
  on the link.
- **`NetConnectionNowMs`** (`net_channel.c`): the transport clock of a
  connection's channel, which is simulated in tests. The client needs it for
  the same reason the lobby needed `NetChannelNowMs` in R1.

### Acceptance test

`zig build test-net-client` (CMake `net-client`, under
`ROLLER_NET_TEST_ASSETS`) runs `tests/net_client_test.c`. It takes about 3 s
and runs twice: 36 Hz (20 s of steady state, then the latency step, then
10 s) and 100 Hz (8 s and 8 s).

Link: 60 ms one way (120 ms RTT), 10 ms of jitter each way (so 20 ms on the
RTT), 3 percent loss each way. The client runs a 60 fps frame loop; the host
pumps every millisecond and ticks on its own clock.

What it asserts, per run:

- **Steady state under 1 percent late, zero future-dropped.** Measured at
  the host from two seconds in to the latency step: 0 of 648 at 36 Hz and
  1 of 600 at 100 Hz. `uiFutureInputs` is 0 for the whole race, as are
  rejected and clamped inputs.
- **The host simulated the scripted input.** For every tick the host did not
  count late, the input it would have simulated equals the canonicalised
  script for that tick.
- **The timeline as the host sees it.** One batch per client tick,
  `uiFirstTick` consecutive, the span identity above holds exactly, and the
  leftover gaps (lost batches) stay under the loss budget.
- **The latency step.** A +60 ms step in each direction: the inputs in
  flight arrive late, and no input is late more than five seconds after the
  step. `fTickScale` is asserted inside [0.9, 1.1] on every frame, and the
  final lead error is under 1.5 ticks.
- **The three rings.** Per tick: the input ring holds the canonicalised
  input, the prediction ring equals `NetSnapshotEncodeCarFull` for the car
  as it stands, and `context[N]` equals the live context entering N+1, whole
  struct. At the end: every one of the last 256 ticks has a context whose
  `iGameFrame` is strictly consecutive, and the tick 256 back is no longer
  served.
- **Received host state.** No rejected messages, snapshots and own-car
  states arriving, a snapshot and own-car state paired for a good share of
  the retention window (a correction needs both), the newest snapshot older
  than the client's tick, and the host reporting a recent
  `uiLastDecodedSnapshotTick`.
- **The client really drove.** `human_control` is 1 for its car, the car
  advanced `iLastValidChunk` since the race started, and it reached a
  nonzero top speed.

Mutation checks, run once by hand and reverted:

- Forcing `fTickScale` to 1 (no time dilation) leaves inputs arriving late
  for the rest of the race after the step; the recovery assertion fails.
- Recording the context before the tick instead of after fails the
  "entering N+1" comparison on the first tick.
- Sending one tick per batch instead of eight raises the steady-state late
  rate to 2.6 percent at 36 Hz and fails the 1 percent bar.

### Judgement calls and deviations

- **One world, and it is the client's (D13).** The acceptance measures the
  client's rings, which need the client to simulate, and the host's input
  timing, which does not need the host to simulate. So the test gives the
  process's one world to the client and replaces the host's simulation step
  through `NetHostSetSimulation` with a recorder. Everything else about the
  host is real: session, lobby, input queues with the range check and the
  D9 clamp, the snapshot cadence, the ring, and input feedback. The
  snapshots the host builds describe the client's world, which is
  immaterial to E4-S1 because the client does not yet install any of it
  (E4-S2 and E4-S3 do). A genuinely two-world version is multi-process work
  that belongs with E8-S1's scenario library, once the E0-S6 harness has
  session, lobby and input commands.
- **"uiFirstTick strictly consecutive at the host" is checked as an
  accounting identity, not as arrival order.** At 60 fps with 10 ms of
  jitter, two frames' packets genuinely swap order on the link, so arrival
  order is not monotonic and should not be. What the acceptance is really
  about - the client emitting a consecutive, gap-free, never-repeated tick
  sequence - is what the identity proves.
- **The 1 percent late bar is measured in steady state.** The latency step
  is a deliberate disturbance, and the plan gives it its own criterion
  (recovery within 5 s). At 100 Hz the step costs about 40 late ticks,
  because +60 ms is 6 ticks of lead to regain and dilation is capped at 10
  percent; counting those against the steady-state bar would make the two
  criteria contradict each other at high tick rates.
- **A second rate is covered.** The plan only asks for the default rate.
  100 Hz is run as well because the lead, the horizon and the redundancy
  window all scale with the tick rate, and NUCLEAR sessions are the case
  4.16 warns about.

### Not done here

- **Game wiring.** `game_tick_step` still runs the E2-S4 local path in
  modern mode. Wiring the client means `tick_clock_step` no longer adding to
  `iTicksPending` in modern client mode, the frame loop driving
  `NetClientPump` and `NetClientTicksDue`, and `readuserdata` feeding
  `NetClientTick`. That belongs with E3-S3, which switches the listen host
  to `NetHostTick`, so that both sides of a local race go live together.
- **Corrections, interpolation and degraded mode.** The client predicts and
  records, but never compares against a snapshot or corrects (E4-S3), and
  remote cars are not yet puppets (E4-S2), so they sit still under the
  client's own simulation. `NET_MSG_EVENT` and `NET_MSG_WORLD_CHANGE` are
  ignored (E3-S2 and E4-S4).
- **Two local players.** The group is sized for two throughout - rings,
  batch, own-car state - but only the one-car case is exercised (E2-S5).
- **Legacy-call trap.** Still cannot be checked; it arrives with E7-S1. The
  client path calls nothing in `network.c` or `rollercomms.c`.

## E4-S2 remote interpolation and ramp correction

Implemented on 2026-09-20 in `net_client.c` and `net_snapshot.c`.

### Race wiring and snapshot validation

`NetClientBeginRace` marks every car outside the local rollback group as a
puppet and installs the process's single `net_sim_puppet_hook`. This follows
D13: one process owns one world and therefore at most one racing client owns
the global hook. `NetClientDestroy` removes the hook and clears all puppet
flags. The listen-host path remains unchanged and explicitly keeps the hook
NULL.

Before retaining a decoded snapshot, the client now also checks it against
the loaded world: its car and ramp counts must equal `numcars` and
`totalramps`, every chunk must be below `TRAK_LEN`, and every ramp timing
triple must validate against the corresponding loaded ramp. The zero-step
`NetSimAdvanceRampStateCopy` check validates without mutating timing or
geometry.

### In-tick puppet placement

The hook runs at E0-S3's existing seam, after `updatestunts` and before the
car loop. For tick N its render cursor is

```text
N - lead - interpolation_delay_in_ticks
```

so it advances once per simulated tick even when one frame drains several
ticks. The delay is one configured snapshot interval plus twice measured
jitter, clamped to 50..250 ms.

The client scans only the retained 600 ms window, finds the two snapshots
bracketing the cursor, and calls `NetSnapshotInterpolate`. Position, speed
and wire velocity are linear in world space; all four 14-bit angles use the
existing shortest-arc interpolation; discrete fields come from the newer
snapshot. `NetSnapshotApplyPuppet` copies the display-grade state into a
temporary `tCar`, converts the world pose against the current post-ramp
geometry, and commits only after validation and conversion succeed. It does
not touch any `tNetCarExtra`-only field.

If the cursor is older than the retained window it clamps to the oldest
snapshot. If it is newer than the newest snapshot, position advances from
the last two received world positions for at most 100 ms and then holds;
orientation and discrete state hold at the newest snapshot. With only one
snapshot there is no velocity sample, so both pose and the reported applied
render tick hold at that snapshot. This is visual only and never feeds the
local prediction group.

`tNetClientStats` now exposes hook/application counts, interpolation delay,
requested and applied render ticks, underflow/extrapolation counts, the stall
indicator, and ramp corrections. The existing overlay fields receive the
stall and ramp-correction values.

### Ramp correction

At the start of every client tick, before the one live `updatestunts` call,
the client selects the newest retained snapshot T no newer than
`uiRampTick`. It advances a copy of each received timing triple by
`uiRampTick - T` with `NetSimAdvanceRampStateCopy`. Exact agreement does
nothing. On any mismatch it installs the complete validated set through
`NetSimRestoreRamps`, which rebuilds geometry, and increments one correction.
A future-only snapshot is skipped. No netcode path advances the live ramps;
`updatestunts` remains the sole advancer in live and replay ticks.

### Acceptance coverage and measurements

`zig build test-net-client` now keeps the E4-S1 network assertions and adds:

- a five-minute virtual 36 Hz soak with one real client and all fifteen
  non-local cars puppeted, plus the 100 Hz run;
- a scripted host puppet whose continuous world pose crosses between chunk
  frames 0 and 1 and airborne every 40 host ticks; its end-of-tick world pose
  and interpolated angles are checked after every application;
- a foundations check that a valid display state installs in world space and
  an out-of-track chunk is rejected without changing the car or ownership;
- `uiPuppetHookCalls == uiTicks`, end-of-tick placement equal to the hook's
  pose, and zero steady-state ramp corrections;
- a world-displacement check over more than 100 samples. The scripted median
  is 1.6008 track units per tick; the observed maximum stays below the plan's
  3x bound of 4.8024 through chunk and airborne transitions;
- a two-second stopped-client probe. A full snapshot is delivered at the
  matching post-tick boundary, ramp timing is deliberately disturbed, and
  the first resumed tick records exactly one correction and ends at snapshot
  timing advanced exactly once;
- a synthetic moving-ramp measurement. At the 100 Hz run's 50 ms live
  interpolation delay, the worst displacement of a fixed local ramp pose is
  **318.274 track units**. This is the bounded placement error from using
  current ramp geometry for an older puppet pose.

The five-minute soak produced 10,808 client ticks, 5,240 received snapshots,
zero steady ramp corrections, and no placement or displacement-spike failure.
The second rate produced 1,620 ticks and 766 snapshots before the focused
stall probe.

### Test topology judgement

The plan names one host, three clients and twelve AI. D13 forbids three
client worlds in this in-process test, and the E0 harness still has no
session/lobby/race commands. The acceptance therefore uses the same real
session, lobby, channel, host snapshot cadence and client world as E4-S1,
with one client puppeting fifteen non-local cars. The host simulation seam
scripts one remote world pose before snapshot construction; the remaining
remote cars also traverse the ordinary hook. A true simultaneous
three-client soak remains E8-S1 multi-process work rather than creating
multiple worlds in this process.

Not yet checkable: convention 11's legacy-call trap, which arrives in E7-S1.

## E4-S3 rollback, replay and prediction modes

Implemented on 2026-09-20 in `net_client.c`, with validation and
authoritative-field helpers in `net_snapshot.c`, presentation smoothing in
`net_sim_seam.c`, and the draw-only application seam in `drawtrk3.c`.

### Reconciliation and replay

The client considers the newest retained snapshot no newer than its current
tick and newer than `uiLastReconciledTick`. A correction is deferred once per
snapshot tick until the snapshot, all group extras, all predictions, and the
post-tick context are present. The complete group is validated before any
member is written.

`NetClientMovementWithin` follows the E0-S4 assignment. Position is compared
in world space at 0.5 track units, all four world angles at two degrees (91
units of the 14-bit circle), and the wire velocity and headline speeds at one
legacy speed unit. Discrete movement fields are exact. Exact `fHealth` is
included because it controls branching even though health is also host-owned.
Lap/result/damage/timing fields do not cause a correction.

A correction restores the moment in D18 order:

1. install all snapshot ramp timings, rebuilding geometry;
2. install the whole rollback group from snapshot state plus own-car extras;
3. debug-check the restored world pose;
4. restore the matched post-tick context;
5. rewrite the input ring from T through the current client tick, including
   T as the previous-input slot;
6. set `net_sim_replaying`, replay `(T, uiClientTick]` through
   `control_one_tick`, and regenerate prediction and context history after
   every tick.

The input ring is physically preloaded to the end of the span. Intermediate
context records therefore replace that physical future `writeptr` with the
logical post-tick value a live run had; at the end, physical `readptr` and
`writeptr` have caught up naturally. `uiClientTick` is never assigned by a
correction or mode switch.

`NetSnapshotApplyAuthoritative` validates the complete full state and writes
only the E0-S4 host-owned fields. A paired state applies on receipt/current
tick and again after replay. The newest prediction is re-recorded after that
application, so the live group and newest history agree after correction.

### Visual correction

Before replay, each group car's current world pose is saved. The difference
from the replay result becomes an eight-tick `tNetCorrection`. The simulation
always sees the corrected car; only the car pose queued by `drawtrk3.c` gets
the decaying world-position and shortest-arc yaw offset. Camera, collisions,
history, and future prediction never read the offset. Entry to delayed mode
clears it.

### Prediction modes

The replay budget is derived once per race as

```text
clamp((500 ms * tick rate) / 1000, 16, 64)
```

which gives 18, 25, and 50 ticks at 36, 50, and 100 Hz. A static assertion
keeps the older 18-tick Contract B horizon tied specifically to 36 Hz.

An over-budget disagreement raises pressure; three skips enter
`NET_PREDICT_DELAYED`. Independently, expected replay depth (lead plus half
RTT) above the budget for two seconds enters delayed mode. All group cars
become ordinary E4-S2 puppets, prediction history and render offsets clear,
and input/context history continues. No correction pass runs while delayed.

Expected depth below 85 percent of budget for three seconds makes exit
eligible. Exit un-puppets the group and performs the ordinary D18 restore and
replay from the newest complete snapshot. Failure leaves the group delayed;
success changes the mode without changing `uiClientTick`. The overlay's
existing fields now receive correction count and magnitude, deferred count,
replay depth/ticks/cost, prediction mode/transitions, and delayed time.

### Acceptance coverage and measurements

`test-net-client` now covers both 36 and 100 Hz and additionally checks:

- a forced 15-tick, two-unit world-space disturbance causes one correction,
  regenerates live/history equality, and creates an eight-tick visual offset;
- a 0.5-second lap-time-only difference applies for display and causes no
  correction;
- dropping the own-car state counts exactly one defer, leaves correction
  count unchanged, and corrects after the matching extra arrives;
- 100 consecutive synthetic snapshots each force a 14-tick replay for the
  cost sample;
- 600 ms RTT enters delayed mode at both rates, stays there for 20 seconds
  with zero replay while every tick still sends input and the snapshot puppet
  remains visibly moving, and makes at most one high-latency transition;
- 120 ms RTT returns to full prediction inside five seconds through a bounded
  exit replay, with monotonic client tick, matching ramp tick, and the group
  no longer puppeted.

The E0 foundations suite remains the strict local-determinism half of this
acceptance: eight byte-exact rollback variants include the start gate, the
previous-input slot, two local cars, effects/RNG, and engine state; the five
Contract B cases cover speed 250, airborne, braking, gear change, and
fractional-health engine start with RNG identity.

The 100 ms RTT collision feel is unchanged from full local prediction: the
group still runs local one-sided collisions and remote cars are interpolated
puppets. During a correction replay those puppets hold the latest render pose,
so a collision inside the corrected span can be felt once and then be refined
by the next snapshot. In delayed mode collision response arrives with the
host pose and therefore has the same full-round-trip lag as steering.

The replay-output acceptance now has a separate executable which links the
real `sound.c`; only `rollersound_stub.c`, the low-level playback boundary,
is mocked. It proves a 20-tick replay leaves replay-file length, audio queues,
speech pointers, complete context, cars, and RNG equal to the live run. Both
Zig and CMake define the target; `test-net-client` depends on it in Zig.

### Test topology judgement

D13 still gives this process one client world. Before an in-process host
snapshot is built, the test temporarily exposes the client's recorded state
for that host tick, then restores the live client car. This makes ordinary
traffic coherent without constructing a second world. In delayed mode, where
there is intentionally no prediction record, the same host seam advances a
temporary pose by four world units so the snapshot-puppet path can prove the
local car remains visibly live. This is test scaffolding, not production
physics. True controllability-with-lag and multi-client collision feel remain
E8-S1 multi-process scenarios.

Still not checkable: the E7-S1 legacy-call trap. Split-screen correction uses
the group-wide production path but remains unexercised until E2-S5.
