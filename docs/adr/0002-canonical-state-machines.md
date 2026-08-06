# ADR 0002: Canonical state machines and the portable simulation boundary

- **Status:** Accepted
- **Date:** 2026-08-06
- **Related:** ADR 0001
- **Supersedes:** none
- **Superseded by:** none

## Context

ROLLER has one explicit frontend state machine and many state-machine-shaped
workflows implemented independently with enums, counters, flags, and
conditionals. The repeated mechanics include transition execution,
enter/update/exit ordering, validation, tracing, and temporary overlays. Without
a canonical pattern, extracting the Race Session would add another bespoke
implementation and future features would continue the duplication.

The current race loop also mixes authoritative simulation state with rendering,
sound, input, timing, and network transport. A future engine should be able to
compile and execute the same simulation while supplying those platform services
itself. This ADR preserves that extraction path; it does not perform the
extraction now.

## Decision

### Use one flat state-machine runner

Explicit lifecycle state machines use one small in-tree transition runner.
Domain modules own their states, guards, commands, context, and semantic events.
The runner owns transition execution, callback ordering, validation, and
tracing.

Machines are flat and compose by ownership: a parent state may own and step a
child machine. Native hierarchical-state semantics such as ancestor event
bubbling, history states, and recursive entry/exit are not part of the runner.

Temporary suspension and restoration, such as the pause overlay, use an optional
state-stack adapter around the flat runner. Stack behavior is not part of every
machine.

Transitions are deferred to a defined step boundary. Applying a transition exits
the old state, installs the new state, and enters the new state before the next
update. Invalid states and transitions are rejected rather than silently
repaired.

### Support two profiles with the same runner

The transition runner is policy-neutral, but its clients use one of two
profiles.

**Simulation profile**

- Receives all mutable state through explicit context rather than ambient game
  globals.
- Advances from fixed-tick commands and simulation facts, never wall-clock time.
- May mutate simulation context and append sequenced semantic events.
- Must not render, play sound, poll input, read a platform clock, or send
  network packets.
- Exposes immutable snapshots for continuous observations such as vehicle
  positions.
- Is runnable headlessly from an initial context and a command stream.

**Host profile**

- Coordinates simulation with rendering, audio, input, clocks, and network
  adapters.
- May perform platform effects through explicit adapters held by its host
  context.
- Controls tick availability and supplies commands without turning platform
  readiness into simulation state.

A simulation-facing boundary therefore has the conceptual shape:

```text
commands + fixed tick -> simulation -> snapshot + semantic events
```

This is a behavioral constraint, not a commitment to exact C types.

### Keep the simulation host-independent

Host independence allows the same authoritative behavior to be:

- exercised by headless tests and regression tools;
- hosted by alternative renderers and game engines;
- reused for replay validation, debugging, servers, and development tools;
- ported across desktop, web, and mobile without changing race rules; and
- evolved independently from presentation and platform integrations.

Opaque ownership boundaries and fixed-width interchange data should keep
wrappers for other languages straightforward. A stable binary plugin ABI and a
cross-process protocol are not required now.

Legacy migration code may translate existing globals into explicit commands and
context, but simulation-profile code must not acquire new dependencies on those
globals or on platform APIs.

### Require cross-platform simulation compatibility

From the first extraction, the same initial state, feature set, random seed, and
command stream must produce network-equivalent authoritative behavior across
supported platforms. State transitions, semantic events, race results, and
normalized network-relevant state must agree.

This requirement keeps the simulation theoretically compatible with the original
game when both expose the same features and inputs. It does not require
wire-protocol compatibility with an original executable or identical rendering.

Network-visible behavior must use fixed ticks, explicit random state,
fixed-width data, canonical serialization, and defined arithmetic.
Platform-specific clocks, input identifiers, byte order, and presentation
behavior remain outside the simulation boundary.

### Model Race Session simulation separately from hosting

A Race Session is an on-track experience whose kind is fixed at entry. Lobby
coordination, loading, and other preparation before the on-track experience
remain outside it.

The simulation distinguishes Pre-Start, Running, Outcome-Settled, and Stopped
behavior. Network readiness and rendering fades are host concerns composed with
those phases, not simulation phases.

Competitive Outcome Settlement occurs when no Active Human Competitors remain.
Outcome settlement does not necessarily stop simulation immediately: the host
may continue supplying ticks while finish presentation or network coordination
drains, then explicitly request stop. The final race-exit fade occurs after
simulation stops, matching current behavior.

### Keep snapshots and events distinct

Snapshots represent current state and may be read repeatedly, interpolated,
serialized, or checksummed. Rendering consumes snapshots.

Semantic events represent facts that occurred once, such as a start signal, a
competitor finishing, or an outcome settling. Audio, telemetry, and host
workflows consume sequenced events. Losing or replaying a presentation event
must not change authoritative simulation state.

### Do not turn every branch into an FSM

Use the canonical runner when code has durable named states, guarded
transitions, state-specific behavior, or meaningful enter/exit semantics.

Do not use it for ordinary branching, status enums, interpolation, timers with a
single completion condition, queues, render layers, or function dispatch that
has no lifecycle. These remain direct code or focused components.

## Relationship to ADR 0001

ADR 0001 keeps rendering regressions observable through host-pinned indexed-PNG
snapshots. This ADR establishes the boundary that can eventually provide the
same replay-driven simulation snapshots to SDL, another renderer, or a headless
host.

The two verification layers are complementary:

- Simulation command, transition, and semantic-event traces diagnose
  authoritative behavior.
- ADR 0001 snapshots verify that the integrated host and renderer still produce
  the intended pixels.

ADR 0001 deliberately pins pixel comparison to one host. This ADR imposes a
stronger cross-platform requirement only on authoritative simulation behavior;
it does not require cross-platform pixel parity. Simulation conformance tests
must therefore complement, rather than depend on, ADR 0001 snapshots.

## Considered options

- **Native hierarchical state machine:** rejected because current nesting is
  adequately represented by composed flat machines. Native hierarchy would add
  event bubbling, history, and recursive transition semantics with little
  demonstrated use.
- **Flat runner with composition and optional stack:** selected because it
  covers the frontend, Race Session, repeated screen lifecycles, replay
  transport, and overlays without forcing hierarchy on every client.
- **External FSM library:** rejected because ROLLER needs a small transition
  mechanism and strong project-specific simulation boundaries, not a general
  callback framework.
- **Direct platform callbacks from simulation states:** rejected because they
  prevent headless execution, deterministic replay, and reuse by another engine.
- **Require every machine to be side-effect-free:** rejected because host
  workflows legitimately coordinate platform effects and would gain unnecessary
  event plumbing.
- **Separate simulation and host FSM runners:** rejected because transition
  mechanics would be duplicated and could diverge.
- **Defer cross-platform simulation determinism:** rejected because networking
  compatibility is easier to preserve at the simulation boundary than recover
  after platform-dependent behavior has spread.

## Consequences

- New explicit lifecycle machines share transition ordering, validation, and
  tracing.
- Simulation behavior can be tested without graphics, audio, input, or network
  transport.
- Cross-platform conformance tests are required from the first simulation
  extraction.
- Network-visible arithmetic, random state, and serialization must have defined
  platform-independent behavior.
- Another engine can eventually host the same C simulation by mapping commands,
  snapshots, and semantic events to its own systems.
- Rendering and audio integrations require adapters instead of direct calls from
  simulation states.
- Migration requires explicit context, command, snapshot, and event types,
  adding some up-front structure.
- The existing race loop will remain mixed until a separately planned extraction
  migrates it incrementally.
- This ADR does not require a portable library, stable ABI, cross-process
  protocol, original wire-protocol compatibility, or cross-platform pixel parity
  to be implemented now.
