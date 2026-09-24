# Netcode M4 performance report

Status: NET-E8-S5 measurement, 2026-09-24

## Summary

The 16-car, three-client M4 race stays comfortably below the pre-delta
22 KB/s-per-client download estimate. On the measured Windows system, each
client received about 13.06 KB/s and sent about 2.67 KB/s. The host sent
39.55 KB/s across all three clients. Prediction changed to delayed mode at a
configured 400 ms RTT (416 ms after smoothing) and returned to full prediction
after the link was restored to 120 ms.

All byte rates are decimal bytes per second and include the 22-byte packet
header, message headers, acknowledgements and retransmissions. They exclude
UDP/IP link-layer overhead and the harness-only four-byte routing envelope.

## Method

The reproducible runner is:

```powershell
zig build measure-net-performance -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
```

It runs the real channel, session, lobby, authoritative host and predictive
client code in four separate processes behind the netsim proxy. The race has
16 cars and three one-car clients. Measurement begins after `game_frame > 145`
and covers 5,000 host ticks at 36 Hz (138.88 seconds of virtual race time).
The baseline link is 20 ms one-way with 2 ms jitter and one percent loss.

After the bandwidth run, client 1 is held at increasing configured RTTs for
2.5 seconds per step. The reported crossover is the first step at which the
client completes the required two seconds above its replay budget and enters
`NET_PREDICT_DELAYED`. The runner then holds 120 ms RTT for four seconds and
requires the client to return to `NET_PREDICT_FULL` after its three-second
hysteresis interval.

Replay CPU cost uses the existing direct `roller-core` benchmark in
`test-net-foundations`: one human-controlled car and 15 puppets, 1,000 real
simulation ticks, ReleaseSafe. The race harness uses a deterministic virtual
network clock, so its `replay_ms_*` counters deliberately cannot serve as a
CPU timer. Replay work is therefore reported from direct tick cost and the
observed replay-tick counts.

## Windows results

Measured from commit `2cbbd95` plus the uncommitted E8-S5 instrumentation,
using Zig 0.15.2 on Windows 10.0.19045, x86-64 (AMD Family 23 Model 8).

| Measurement | Result |
|---|---:|
| Client download, client 1 | 13,063.75 B/s |
| Client download, client 2 | 13,064.03 B/s |
| Client download, client 3 | 13,055.94 B/s |
| Client upload, client 1 | 2,666.69 B/s |
| Client upload, client 2 | 2,666.69 B/s |
| Client upload, client 3 | 2,670.02 B/s |
| Host aggregate upload | 39,553.51 B/s |
| Host aggregate download | 7,922.16 B/s |
| Full snapshots | 207 |
| Delta snapshots | 7,293 |
| Delta share | 97.24% |
| Average snapshot payload | 548.80 bytes |
| Wall time for the 5,000-tick run | 7.02 s |
| First delayed-prediction step | 400 ms configured / 416.00 ms measured RTT |
| Full-prediction recovery | 120 ms configured / 128.00 ms measured RTT |

The clients made 2,134, 2,196 and 2,171 corrections and replayed 8,511,
8,779 and 8,475 ticks respectively. That is 3.95 replay ticks per correction
on average. The direct benchmark measured 0.016 ms per simulated tick, giving
an estimated average replay CPU cost of 0.063 ms per correction. A full
18-tick, 500 ms-budget replay costs about 0.288 ms on this system. These are
CPU-cost estimates from the direct tick benchmark, not virtual-clock timings.

The average encoded snapshot payload is 50.3% of the 1,104-byte full snapshot.
The wire-rate reduction is smaller because own-car state, input, feedback,
packet headers, acknowledgements and retransmissions are not delta-compressed.

## Platform matrix

The runner and direct benchmark are portable, but only Windows was available
for this measurement. Missing rows are explicit; no values are extrapolated
from Windows.

| Platform | Replay tick cost | RTT crossover | Status |
|---|---:|---:|---|
| Windows x86-64 | 0.016 ms | 416 ms measured | Measured |
| Linux x86-64 | - | - | Runner not executed locally |
| macOS | - | - | Runner not executed locally |
| Android arm64 | - | - | Multi-process runner not available on device |

The prediction crossover is primarily protocol geometry rather than CPU
speed: at 36 Hz, the client enters delayed mode when its expected replay depth
exceeds the 18-tick budget for two seconds. The measured step can vary with
RTT smoothing, 16 ms harness frame cadence, jitter and the 20 ms sweep size.

## Raw Windows result

```json
{
  "client_download_Bps": [13063.75, 13064.03, 13055.94],
  "client_upload_Bps": [2666.69, 2666.69, 2670.02],
  "host_download_Bps": 7922.16,
  "host_upload_Bps": 39553.51,
  "snapshot_full": 207,
  "snapshot_delta": 7293,
  "snapshot_payload_bytes": 4116006,
  "snapshot_average_payload_bytes": 548.8008,
  "corrections": [2134, 2196, 2171],
  "replay_ticks": [8511, 8779, 8475],
  "crossover_configured_rtt_ms": 400,
  "crossover_measured_rtt_ms": 415.999115
}
```

