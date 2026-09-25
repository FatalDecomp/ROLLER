# ADR 0006: Host-authoritative online netcode and legacy retirement

- **Status:** Accepted
- **Date:** 2026-09-24
- **Related:** ADR 0002, NET-E0 through NET-E8
- **Supersedes:** none
- **Superseded by:** none

## Context

ROLLER's original multiplayer synchronises inputs in lockstep. That works on a
small, stable LAN, but it makes every participant wait for the slowest one and
does not tolerate ordinary internet latency, jitter, loss, address changes, or
temporary disconnects. It also lets networking concerns leak into the timer
thread and assumes that every peer can advance the race in agreement.

The replacement must preserve the 1995 simulation and replay format, support
listen and dedicated hosting, and remain usable on desktop and mobile networks.
The simulation is stateful, uses chunk-relative coordinates and moving track
geometry, and draws gameplay and cosmetic randomness from one stream. A car is
therefore not enough state to predict or restore a tick. Cross-platform
bitwise determinism is also not a safe protocol assumption.

This ADR records decisions D1 through D21 from the online-netcode plan. D22 and
D23 are later verification and input-safety refinements and are recorded under
"Hardening amendments" so the implemented architecture has one durable record.

## Decision

### Protocol, hosting, and scope

| ID | Decision |
|----|----------|
| D1 | Use a host-authoritative simulation. Clients interpolate remote cars, predict their locally owned cars when possible, and correct prediction from host snapshots. |
| D2 | Ship both listen-server and headless dedicated-server hosts. |
| D3 | Use UDP through an in-tree C transport with sequencing, acknowledgements, reliable and unreliable channels, and connection generations. |
| D4 | Keep transport behind an interface. WASM networking and web cross-play are outside this project. |
| D5 | Use a self-hosted rendezvous service under `fatal.racing` for online discovery. Direct connection and LAN discovery remain available. |
| D6 | Keep the legacy replay format and `replay.c` unchanged. Re-simulation suppresses replay-file output so each live tick is recorded once. |
| D7 | Only a listen host may pause a multiplayer race, and its pause freezes every node. A dedicated server never pauses. |
| D8 | Transfer a disconnected player's car to AI. A player returning inside the grace window recovers through an authenticated checkpoint and a new connection generation. |
| D9 | Validate host inputs by masking defined buttons and clamping defined ranges. This project adds no broader anti-cheat system. |
| D10 | Spectators, late join, and host migration are outside this project. |
| D11 | Keep the original lockstep path runtime-selectable until the modern path has shipped, accumulated real use, and passed the retirement gates below. |
| D12 | Put new netcode in new files and preserve names inherited from original debug symbols. Existing translation units are touched only at narrow, documented seams. |
| D13 | Keep one simulation world per process. Tests needing several worlds use several processes. |
| D14 | On a client, simulation predicts movement only. Race results and world changes are host commits; ramps run locally and are corrected from snapshots. |
| D15 | Encode every wire position and orientation in world coordinates. Convert against the geometry belonging to the represented tick. |

The network pump runs from the frame loop on a monotonic clock, independently
of simulation ticks. Ticks consume already-delivered messages and inputs; they
do not perform socket work. A host consumes tick-stamped canonical input and
publishes snapshots, per-client full car state, semantic events, world changes,
and recovery checkpoints. A client maintains one monotonic tick timeline and
uses time dilation, never skipped or repeated ticks, to control its lead.

### Correction is simulation, not a second physics implementation

**D16: correct by re-simulation.** When prediction differs from authoritative
movement state at tick T, restore the state for T and run `control_one_tick`
for `(T, current]`. The netcode must not derive position, speed, timers, or car
behavior with its own formulas. The only permitted approximation is bounded,
visual-only extrapolation of a stalled puppet.

This replaced an earlier hand-derived correction model. The E0 audit showed
that a tick reads far more than a car: race globals, previous input, moving
geometry, car order, particle occupancy, sound pointers, and random state all
matter. The original simulation is the only complete model of those
interactions.

**D17: require local determinism only.** Replay uses the same executable, on
the same machine, from state that machine just saved. Nodes exchange
authoritative state and never rely on different platforms producing identical
floating-point results. This makes rollback testable without turning
cross-node lockstep into a hidden requirement.

**D18: restore a moment, not a car.** A correction restores a matched post-tick
boundary: ramp timing and rebuilt geometry first, then the whole local
rollback group, tick context, input history including the preceding slot,
input pointers, and random state. Split-screen cars restore and replay as one
group. Geometry precedes conversion of world poses into legacy chunk-relative
coordinates. Replay regenerates prediction and context history at the same
post-tick boundary as live simulation.

This ordering follows from failures in earlier designs. Combining a pre-tick
context with a post-tick car left globals one tick behind. Restoring a car
without its geometry silently changed its world pose when ramps moved. Omitting
the preceding input was incorrect because steering reads the prior ring slot.

**D19: movement is predicted; results are received.** Corrections compare and
restore only the movement set. Lap counts, lap times, kills, finish state,
records, ownership outcomes, and other authoritative results arrive as
sequenced host state or events and are installed after replay. A locally
predicted finish therefore cannot commit a result the host rejected.

**D20: replay is invisible to simulation.** Replay mode may suppress only
external output that consumes no randomness: replay-file writes and sound or
speech queueing after any random selection. It must not change a simulation
branch or random draw. Particles and smoke continue to spawn during replay
because they share the gameplay random stream. Restoring the seed after a
divergent replay is not sufficient: an earlier changed draw can already have
changed engine start, vibration, respawn, or later control flow.

**D21: prediction is a privilege.** Correction work has a time-based budget.
When smoothed latency implies a replay deeper than that budget, the client
enters delayed prediction: its local group becomes snapshot-driven puppets,
while input continues to be stamped and sent at the normal lead. Hysteresis
prevents oscillation. Leaving delayed mode is an ordinary D18 correction from
the newest usable snapshot, not a tick-counter reset.

This deliberately trades immediate controls for stable motion at extreme
latency. Repeatedly attempting an over-budget rollback, or resynchronising to
another state with the same excessive depth, would produce a hitch loop. In
delayed mode correction depth is zero by construction.

### Hardening amendments

The implementation also adopts two later decisions without changing D1-D21:

- **D22:** local save/restore is byte-exact, but a wire round trip is judged by
  bounded movement divergence plus exact random state and draw count. World
  conversion and explicitly quantised wire fields make byte equality across
  the wire neither achievable nor useful.
- **D23:** decoded data is hostile until every field that can reach simulation
  has been validated. A malformed message is rejected as a whole before world
  mutation; validating one representation and then overwriting it from an
  unchecked representation is forbidden.

## Legacy retirement plan

Retirement is a separate change after this ADR. It must not be bundled with a
protocol feature or a simulation change. D11 remains in force until all entry
gates are checked and the release owner explicitly approves retirement.

### Entry gates

- [ ] The modern path has shipped in a public release and has accumulated a
  full release cycle of real listen-server use. Dedicated hosting, direct
  connection, rendezvous discovery, relay fallback, and LAN discovery have
  each been exercised outside the test harness.
- [ ] Supported desktop and mobile platforms have completed the release
  multiplayer matrix: lobby, load barrier, race start, results, pause where
  applicable, disconnect, AI takeover, rejoin, address change, and orderly
  exit. Platform-specific exceptions are recorded and accepted by the release
  owner.
- [ ] Soak and telemetry evidence shows no unresolved correctness issue in
  snapshot decode, authoritative commits, correction, delayed prediction,
  or recovery. Bandwidth and replay cost fit the published platform budgets.
- [ ] The modern multi-process suite passes at all supported tick rates under
  latency, jitter, loss, duplication, and reordering. The legacy-call trap
  remains silent throughout the modern scenario.
- [ ] Modern host and corrected-client recordings still load through the
  unchanged legacy replay reader, with one recorded frame per live tick.
- [ ] The migration and rollback release notes are ready. Players are warned
  that old lockstep peers are not protocol-compatible with the modern path.
- [ ] The repository has no supported workflow whose only implementation is
  legacy lockstep. Any intentionally retired workflow is named in the release
  notes rather than silently disappearing.

### Staged removal

1. Make MODERN the default for one release while retaining
   `--net-mode legacy`. Keep the trap and both legacy smoke tests, and collect
   regressions attributable to the default change.
2. After the entry gates remain green for that release, remove the user-facing
   LEGACY selection and collapse mode branches onto MODERN. Do not remove the
   trap yet; it is the audit for missed call sites.
3. With the modern suite still reporting zero legacy entries, remove the
   lockstep implementations in `network.c` and their transport surface in
   `rollercomms.c`, plus legacy-only globals, build renames, wrappers, and
   tests. Audit every symbol before deletion because these translation units
   also contain UI and compatibility state used outside packet transport.
4. Remove `net_legacy.c/.h`, `NET_MODE_LEGACY`, and `net_mode` only after all
   conditional call sites and build-system references are gone. Simplify the
   surviving MODERN branches in a reviewable follow-up rather than combining
   broad cleanup with deletion.
5. Keep `replay.c`, the GSS format, and replay compatibility tests. Network
   retirement does not retire legacy recordings.
6. Update command-line help, player documentation, source manifests, CMake and
   Zig build descriptions, architecture notes, and support diagnostics in the
   same retirement series.

### Removal verification and rollback

The retirement series must pass the complete native build, source-set and
manifest checks, all focused netcode tests, the real-UDP multi-process race,
rejoin and relay scenarios, and replay compatibility on every CI platform. A
repository search must show no lockstep packet entry point or LEGACY mode
branch remaining; similarly named replay or local UI code is not a deletion
target.

Ship retirement as an isolated, revertible series. If release validation finds
a modern-path regression, revert that series and restore the runtime selector;
do not change the modern wire protocol or replay format merely to recreate the
fallback. Security or data-validation failures block a release instead of
falling back automatically to unauthenticated lockstep networking.

## Considered options

- **Keep lockstep as the internet protocol:** rejected because latency and
  packet loss stall every peer and reconnect or host authority cannot be added
  cleanly around it.
- **Require deterministic lockstep across platforms:** rejected because the
  legacy simulation was not designed for cross-platform bitwise determinism
  and authoritative snapshots already provide a safer boundary.
- **Write a simplified client physics model:** rejected because the E0 audit
  demonstrated that simulation behavior depends on extensive coupled state.
- **Correct cars arithmetically without replay:** rejected because it repairs a
  pose but not the timers, RNG decisions, moving geometry, and later branches
  produced by the incorrect interval.
- **Always predict regardless of replay depth:** rejected because extreme RTT
  would create unbounded correction cost and visible hitch loops.
- **Never predict:** rejected as the default because ordinary internet play
  would pay round-trip input latency even when bounded local replay is cheap.
- **Delete lockstep as soon as MODERN passes CI:** rejected because CI does not
  prove deployment, NAT, platform, or real-player behavior. D11 requires a
  shipped and used replacement.

## Consequences

- The host is the single authority for race outcomes and shared world state.
- Clients remain responsive at ordinary latency without requiring cross-node
  determinism, and fail into explicit delayed controls at extreme latency.
- Corrections retain substantial local state and can replay effects, but their
  work is bounded and their behavior is tested against uninterrupted runs.
- Internet operation requires maintained rendezvous and optional relay
  services in addition to the game binary.
- The wire protocol, decoder validation, recovery path, and server deployment
  become long-lived compatibility and security surfaces.
- Legacy lockstep remains a temporary maintenance cost until the retirement
  gates are satisfied. Its presence is a rollback option, not permission for
  modern code to call it.
- Removing lockstep later does not change the replay format or invalidate old
  recordings.
