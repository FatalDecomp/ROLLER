# Netcode E8 notes

## NET-E8-S3: bot robustness

Implemented on 2026-09-24 and committed in `ca47bf8`.

The endpoint-only bot now supports the same authenticated generation
replacement used by the native client. `NetBotBeginRejoin` retains the
accepted session token and player index, moves the session onto a caller-owned
generation+1 connection, clears pre-drop input and snapshot history, and stops
input production until recovery completes.

The bot stages the reliable ordered checkpoint without installing a simulation
world. It validates the header and part counts, every full car state, the
complete roster and retained one-car assignment, unique world chunks against
the loaded track, ramp timing, and the matching END tick. A complete checkpoint
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
snapshots and no rejected bot messages. Broader verification is recorded in
the external handoff.

## NET-E8-S4: CI multi-process race

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`roller-bot` is a real UDP process wrapper around the endpoint-only bot. It
loads its own headless track world, joins a numeric dedicated-server address,
automates lobby readiness and loading, and sends a four-tick-ahead stream of
the existing deterministic bot input at the session's 36 Hz rate. It exits
successfully only after its car finishes, the reliable event drain completes,
and no protocol message was rejected.

`tests/net_multiprocess_race.py` starts one `roller-server` and two
`roller-bot` processes on an ephemeral loopback UDP port. Each process owns a
separate simulation world. The acceptance requires a one-lap TRACK5 result
with exactly two finishers and two human finishers, a finish from each bot,
and zero rejected bot messages. Zig exposes it as `test-net-multiprocess`;
CMake registers it on Linux when the server and test assets are built. The
Linux CI dedicated-race step now runs both the virtual-clock dedicated test
and this real-socket process test.

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
virtual-clock dedicated race, and deterministic two-process harness passed;
the full native ReleaseSafe build passed. Source counts remain Linux 95,
macOS 95, Windows 98, Android 95 and Emscripten 93; roller-core remains 111
translation units. The Linux CI job itself, CMake configure, Android build,
manual macOS server run, and E7-S1 legacy-call trap were not run locally.
