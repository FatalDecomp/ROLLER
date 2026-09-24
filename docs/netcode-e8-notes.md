# Netcode E8 notes

## NET-E8-S3: bot robustness

Implemented on 2026-09-24. The changes are intentionally uncommitted.

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
