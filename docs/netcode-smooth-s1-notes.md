# NET-SMOOTH-S1: reproducible presentation baseline

Date: 2026-10-01. Scope: measurements and diagnosis only. The simulation,
snapshot cadence, puppet sampling policy, local prediction, and wire format are
unchanged. The recorded values come from the Windows multi-process harness with
TRACK5.TRK at 36 Hz and a 16 ms frame command cadence. They are synthetic link
conditions, not measurements of the reported Android connection.

## Result

The suspected timing failure was reproduced. After a five-second warm-up, the
same 10.24-second moving-race segment gave these elapsed-time-weighted remote
display modes. The remote car changed yaw 20 times in every segment, so the
fixture includes turns. The host ran 369 ticks and sent 184 snapshots per client
over the measured segment; the 552 host count in the JSON is the sum across its
three clients.

| Simulated route                            | Interpolating | Extrapolating | Holding | Dropped deltas | Max arrival gap | Frame max |
| ------------------------------------------ | ------------: | ------------: | ------: | -------------: | --------------: | --------: |
| 5 ms one way, no variation/loss            |          100% |            0% |      0% |              0 |           64 ms |     16 ms |
| 100 ms one way, no variation/loss          |            0% |         70.8% |   29.2% |              0 |           64 ms |     16 ms |
| 60 ms one way, +/-20 ms variation, 1% loss |         38.9% |         61.1% |      0% |              0 |           96 ms |     16 ms |

The high-latency link had regular advancing arrivals, zero dropped deltas, and
no measured frame stall. Host snapshot sends and advancing client arrivals both
had 48-64 ms gaps at the harness's 16 ms time resolution. Its final legacy
target was 83.3 ms ahead of the newest accepted snapshot despite a 55.6 ms
configured interpolation delay. This is direct evidence that the host-relative
puppet target often requests unreceived history under steady delivery. The
measured 224 ms RTT includes the proxy's scheduled delay and 16 ms command
pacing; it is not the configured 200 ms round trip exactly. The LAN control had
27.8 ms of headroom at its final sample.

The variable-delivery profile switched from extrapolation to interpolation 89
times. The maximum discrepancy between the old bounded extrapolation and the new
snapshot trajectory, evaluated at the same target tick, was 463.4 world units
and 13.8 degrees of yaw. There is no remote recovery blend in the S1 path, so
its recorded blend duration is zero. World units are track units, not assumed
metres. The simulator's reported maximum positive delivery variation was 40 ms;
the largest source tick gap was four ticks. A separate synchronized 100 ms frame
stall registered as a 100 ms frame maximum and three host ticks on that frame.
Because the harness advances all processes together, that stall does not isolate
a phone-only render pause.

## Measurements and interpretation

- `uiSnapshotAgeMs` and the overlay's "Newest received" line mean time since
  receipt of the newest snapshot. They do not include delivery age relative to
  host time. The baseline's high-latency row ended with only 16 ms since receipt
  while display starvation occupied the entire measured interval.
- The legacy puppet hook and extrapolation counts still include rollback replay.
  `NetClientPresentationFrame` accounts mode time once after the live tick drain
  per frame; replay never increments those time counters. The mode is the most
  recent live puppet sample used by the existing drawing path.
- `older_than_history` means a target before the retained oldest snapshot.
  Extrapolating and holding mean the target passed the newest snapshot. These
  are separate failure directions. A car that has reached the 100 ms
  extrapolation cap is recorded as holding.
- Presentation tick in the diagnostics is a fractional offset from the race
  start tick. `L` is the absolute newest uint32 snapshot tick. Desired reserve
  is the current legacy delay; actual reserve is `L - target`, in milliseconds,
  and may be negative. Playback speed is the change in the last live applied
  tick divided by elapsed frame time, so repeated render frames show zero and
  simulation-tick steps show a larger value. It is diagnostic, not a new clock.
- The arrival buckets have limits of 50, 75, 100, 150, and 250 ms plus a final
  overflow bucket. Only advancing valid snapshots update them. Duplicates and
  reordered old snapshots may fill history but do not move the arrival anchor.
  The reported delivery variation is the positive part of arrival-gap time minus
  source-tick-gap time. Configured snapshot spacing is 55.6 ms.
- Recovery error compares the previous extrapolation trajectory with the newly
  bracketed state at one target tick; it is not a comparison with the host's
  newer current pose. Mode time assigns each frame interval to the mode sampled
  at the end of that interval, giving 16 ms resolution here. Time spent paused
  or in checkpoint installation is tracked separately from active mode time.

## Reproduce

```powershell
zig build measure-net-smooth-s1 -Doptimize=ReleaseSafe '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
```

The command runs the real host, three predictive clients, and `roller-netsim`,
then writes [machine-readable baseline](netcode-smooth-s1-baseline.json). Seeds
and link settings are in that JSON. The test requires a changing remote yaw,
advancing snapshot arrivals, valid per-frame statistics, and no legacy calls.
The built client acceptance also checks that one forced rollback does not add
presentation frames or display-mode time.

## Follow-up boundary

This is the **before** row for S2's independent presentation clock. There is no
after-smoothing measurement yet. The harness models the game's current in-tick
pose and does not rasterize Android frames or measure visual/collision
separation. Physical LAN and 5G validation, route selection on the reported
phone, relay throttling, and phone-only frame pacing remain unmeasured. S2
should initially reserve at least two snapshot intervals behind the observed
arrival timeline, then use direct arrival variation rather than ACK jitter
alone; final tuning needs S2's controller and device measurements.
