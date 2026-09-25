# Netcode E8 notes

## NET-E8-S2: packet capture and playback

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`net_capture.c/.h` adds a transport decorator that records every successful send
and receive with its transport timestamp, direction, peer address, and exact
packet bytes. The versioned `RLRPCAP1` file format uses explicit little-endian
fields and validates the complete file before playback. Files are capped at 64
MiB, records at `NET_MAX_PAYLOAD`, timestamps must be monotonic, and malformed
directions, addresses, lengths, headers, or trailing records reject the capture
before any packet reaches the channel.

Playback exposes the same `tNetTransport` interface on a caller-controlled
monotonic clock. Inbound packets become available at their captured times.
Outbound calls must reproduce the next captured send with the same time,
destination, length, and bytes; a divergence makes playback fail instead of
silently accepting a different run. The decorator has no socket, SDL, or
platform-clock dependency and is included in `roller-core` and every game source
set.

`test-net-capture`, also included by `test-net-foundations`, records three
reliable ordered feedback messages through the real channel over the
deterministic transport. It then re-creates the client channel from the capture
alone, reproduces its acknowledgements, and requires the complete `g_netStats`
result to be byte-identical to the live run. The captured first and last
timestamps and end-of-stream state are checked as well.

Focused and regression verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-capture -Doptimize=ReleaseSafe
zig build test-net-foundations test-net-full-state-coherence test-net-host `
  test-net-client test-net-bot test-net-dedicated test-net-multiprocess `
  test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The focused test, complete netcode set, 51.20-second real-UDP race, full native
build, source/manifest checks, and all 18 selected Python tests pass. Source
counts are Linux 97, macOS 97, Windows 100, Android 97 and Emscripten 94;
roller-core contains 113 translation units. A CMake configure, Android build,
manual macOS run, and E7-S1 legacy-call trap were not run.

## NET-E8-S5: bandwidth, latency, and replay-cost report

Implemented on 2026-09-24 and committed in `a9a2cca`. Full methodology and
measurements are in `docs/netcode-performance.md`.

The race harness now counts actual protocol bytes at its transport boundary and
exposes the host's existing full/delta snapshot counters. The counters exclude
only the four-byte harness routing envelope. The reusable scenario returns its
exact post-start-gate baseline and virtual measurement duration, so setup
traffic is not mixed into race bandwidth.

`tests/net_performance.py`, exposed as `zig build measure-net-performance`, runs
the 16-car, three-client race for 5,000 ticks, calculates per-node byte rates
and snapshot compression, sweeps sustained RTT until prediction degrades, and
verifies recovery after the link improves.

On Windows the clients downloaded 13.06 KB/s and uploaded 2.67 KB/s each; host
aggregate traffic was 39.55 KB/s out and 7.92 KB/s in. Deltas were 97.24 percent
of 7,500 snapshots and the average snapshot payload was 548.80 bytes. The first
delayed-prediction step was 400 ms configured / 416 ms measured RTT, and full
prediction recovered at 120 ms configured / 128 ms measured RTT. The existing
direct ReleaseSafe benchmark measured 0.016 ms per replay tick.

The Windows performance run and `test-net-foundations` passed with Zig 0.15.2.
Linux, macOS and Android performance rows remain explicitly unmeasured; the
report does not invent cross-platform figures.

## NET-E8-S1: harness race scenarios library

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The E0 TCP harness now has a test-only modern race mode backed by the real
channel, session, lobby, host, client, snapshot, correction and recovery
objects. One process owns the authoritative world and each of three other
processes owns one predictive client world, preserving D13. The host and clients
automate join, ready, loading and release, then advance from the harness virtual
clock at 36 Hz. Measurement starts only after `game_frame > 145`.

The netsim proxy now supports all eight simulated endpoints, addressed harness
envelopes, per-route latency/jitter/loss/duplicate/reorder settings, and a
drain-and-advance command. Its original two-endpoint raw-datagram mode is
unchanged and still passes the deterministic E0 smoke test.

`tests/net_race_scenarios.py` provides the story's reusable operations:
`run_race`, `disturb`, `disturb_engine`, `disturb_lap_time`, `drop_client`,
`rejoin_client`, `set_link`, `hold_rtt`, `pause`, `strategy_button`, `ability`,
`suppress_own_car_state`, `force_correction`, `worldpose`, `context`, `rng`, and
`stats`. The client report includes replay depth, total and worst replay cost,
prediction mode and transitions, correction and defer counts, degraded time,
RTT, snapshot count and rejected-message count. Own-car-state suppression
happens in the harness transport after the real channel has packetised messages;
it does not add a production host seam. A post-race control smoke invokes every
operation, holds pause without advancing the host tick, forces a correction, and
completes an actual generation-2 drop/checkpoint/rejoin cycle.

The acceptance runs 16 cars, one host and three predictive clients for 5,000
running ticks over 20 ms one-way links with 2 ms jitter and 1 percent loss. On
Windows it completed in 6.68 seconds, with 2,195 / 2,133 / 2,168 client
corrections, all clients in full prediction at the end, and no rejected race
messages. The large correction count is useful data rather than a harness
allowance: these are genuinely separate worlds, and the crowded client worlds
collide a predicted car with delayed puppets while the host simulates all 16
cars authoritatively.

Focused verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host `
  test-net-client test-net-bot test-net-dedicated test-net-multiprocess `
  test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The full focused set, the existing 50.94-second real-UDP race, the full native
ReleaseSafe build, source/manifest checks and all 18 selected Python tests
passed. Source counts are Linux 96, macOS 96, Windows 99, Android 96 and
Emscripten 93; the roller-core manifest contains 112 translation units. A CMake
configure, Android build, manual macOS run and E7-S1 legacy-call trap were not
run.

## NET-E8-S3: bot robustness

Implemented on 2026-09-24 and committed in `ca47bf8`.

The endpoint-only bot now supports the same authenticated generation replacement
used by the native client. `NetBotBeginRejoin` retains the accepted session
token and player index, moves the session onto a caller-owned generation+1
connection, clears pre-drop input and snapshot history, and stops input
production until recovery completes.

The bot stages the reliable ordered checkpoint without installing a simulation
world. It validates the header and part counts, every full car state, the
complete roster and retained one-car assignment, unique world chunks against the
loaded track, ramp timing, and the matching END tick. A complete checkpoint
clears its delta baselines. The bot returns to Racing only after decoding a
newer snapshot, resets its redundant-input history at that tick, and exposes
checkpoint and rejoin counters in `tNetBotStats`. If recovery stalls after the
join is accepted, it repeats the checkpoint request every two channel-clock
seconds.

The existing `test-net-bot` acceptance now carries the E5-S3 scenario through
the bot API. It starts a real headless TRACK5 race, cuts both simulated links
for 15 seconds, observes the normal ten-second drop-to-AI transfer, reconnects
with the retained token at generation 2, and holds a 750 ms one-way link while
the ordered checkpoint arrives. It asserts that the original roster slot and
human ownership are restored, exactly one checkpoint and rejoin complete, the
bot resumes ordinary input batches, and the car finishes the one-lap race.
Checkpoint backfill labels that the host already simulated are late by design;
the host accepts the batches without clamping or rejecting them.

Focused verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-bot -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
```

The run completed recovery and the race in 1,843 host ticks with 624 decoded
snapshots and no rejected bot messages. Broader verification is recorded in the
external handoff.

## NET-E8-S4: CI multi-process race

Implemented on 2026-09-24 and committed in `6922ec6`.

`roller-bot` is a real UDP process wrapper around the endpoint-only bot. It
loads its own headless track world, joins a numeric dedicated-server address,
automates lobby readiness and loading, and sends a four-tick-ahead stream of the
existing deterministic bot input at the session's 36 Hz rate. It exits
successfully only after its car finishes, the reliable event drain completes,
and no protocol message was rejected.

`tests/net_multiprocess_race.py` starts one `roller-server` and two `roller-bot`
processes on an ephemeral loopback UDP port. Each process owns a separate
simulation world. The acceptance requires a one-lap TRACK5 result with exactly
two finishers and two human finishers, a finish from each bot, and zero rejected
bot messages. Zig exposes it as `test-net-multiprocess`; CMake registers it on
Linux when the server and test assets are built. The Linux CI dedicated-race
step now runs both the virtual-clock dedicated test and this real-socket process
test.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-multiprocess -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build test-net-bot test-net-dedicated test-net-harness `
  -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
```

The real UDP race completed in 51.78 seconds. The existing recovery bot,
virtual-clock dedicated race, and deterministic two-process harness passed; the
full native ReleaseSafe build passed. Source counts remain Linux 95, macOS 95,
Windows 98, Android 95 and Emscripten 93; roller-core remains 111 translation
units. The Linux CI job itself, CMake configure, Android build, manual macOS
server run, and E7-S1 legacy-call trap were not run locally.
