# NET-SMOOTH-S4: interruption recovery

Date: 2026-10-01. S4 adds a bounded, render-only recovery offset to the S3
visual car cache. The in-tick collision puppet, local reconciliation, host
authority, input scheduling, RNG, snapshot format, and replay output are not
changed. The source measurements are in
[the S1 baseline](netcode-smooth-s1-baseline.json) and
[the S4 matrix](netcode-smooth-s4-matrix.json).

## Recovery and reset policy

- When the presentation cursor runs beyond received history, the visual pose
  extrapolates position from the last two snapshots for at most 100 ms and
  then holds. Orientation holds at the newest snapshot. The recovery state
  saves world poses and ticks, not pointers into the snapshot ring.
- On the first bracketed frame, it evaluates that old trajectory and the new
  snapshot trajectory at the same cursor, then applies their difference as a
  visual offset. The offset decays over 100 ms of active render-frame time.
  Yaw, pitch, roll, and actual yaw use shortest-arc 14-bit angle differences.
  A repeated starvation/recovery during an active blend retargets its remaining
  time without restarting the 100 ms window. Pausing freezes the visual cache
  and recovery time; resuming skips the elapsed pause duration.
- A source pair with changed lives, a known respawn event, or a world-position
  difference above 4096 track units is a discontinuity. The renderer holds
  the older source until the new snapshot's tick, then displays the new state
  directly. A raw pose jump detected between frames also clears a blend when
  a low frame rate skips the source pair. A recovery offset above 4096 world
  units is displayed directly. This provisional cutoff is about 8.8 times
  the S1 ordinary maximum recovery error of 463.4 world units and 5.4 times
  the test car's 758.2-unit diagonal. World units are track units, not metres.
  The visual-discontinuity counter counts reset observations and can count a
  source boundary twice, first at the bracket and again at the raw pose jump.
- Race start, rejoin, checkpoint install, forward clock resynchronization,
  ownership changes, and entry/exit from delayed prediction clear affected
  recovery state. The Android foreground path already requests a rejoin;
  the resulting rejoin and checkpoint clear the visual cache. Ramp and airborne
  chunk changes alone do not trigger a cut; their source chunk metadata stays
  with the sampled world pose.

The debug overlay and harness expose blend count, discontinuity reset
observations, source-cut hold time, maximum recovery error in world units and
yaw degrees, and remaining blend time. These are visual diagnostics. The
buffered mode percentages describe history availability; source-cut hold time
is recorded separately, so a bracketed timeline is not taken to mean every
car was interpolated throughout the interval.

## Before and after

The S1 side measures the original in-tick puppet in a 10.24 s seeded segment.
The S4 side measures the render cache's buffered timeline in separate 30 s
seeded segments, each after 5 s warm-up. Both are Windows multi-process
simulated links at 36 Hz and 16 ms frames. They are comparable conditions,
not paired frames or pixel-level motion measurements.

| Simulated one-way route | S1 extrapolating/holding | S4 extrapolating/holding |
| --- | ---: | ---: |
| Stable 5 ms | 0% | 0% |
| Stable 100 ms | 100% (70.8% / 29.2%) | 0% |
| 60 ms +/-20 ms, 1% loss | 61.1% | 0% |

The 28-profile S4 matrix uses 14 profiles at each of 36 and 100 Hz. Stable 5,
60, 100, and 150 ms, both 20/150 ms asymmetric paths, variable delivery, and
5% reorder plus 5% duplicates all measured 100% bracketed timeline time at
both rates. The variable profile exceeded the plan's 99% target; its final
reserve was 136 ms at 36 Hz and 98 ms at 100 Hz. The exact one/two/three
snapshot burst profiles reported the requested missing-snapshot counts. Two
and three drops caused 2.029% and 3.488% extrapolation at 36 Hz and 0.523%
and 1.039% at 100 Hz. No profile had measured timeline holding or regression.

The 20-to-120-to-20 ms transition measured 0.080% extrapolation at 36 Hz and
1.680% at 100 Hz, with no timeline regression. The 500 ms outage measured
12.727% and 13.636% extrapolation; reserve reached its 250 ms cap and each
run recorded one forward clock resynchronization. The 30/60/120 FPS profile
with a synchronized 100 ms stall measured 0.661% extrapolation at both tick
rates. All profiles had zero legacy calls and reserve within the configured
50-250 ms bounds. The JSON preserves per-profile seeds, active duration,
arrival gaps, drop counts, frame maximums, source-cut holds, and recovery
counts. The harness advances host and clients together, so its stall is not
an isolated phone render stall.

The 100 Hz stable runs recorded 32 ms of per-car source-cut holding from the
scripted race, despite 100% buffered timeline interpolation. The 500 ms outage
recorded 112 ms and 80 ms of source-cut holding at 36 and 100 Hz. These
discrete cuts are excluded from the recovery blend. They are reported
separately from the history mode to avoid overstating visual smoothness.

## Renderer and simulation checks

The real client seam test uses a moving, turning remote car whose sampled
chunk changes across ramp and airborne phases. At 36 Hz, a 500 ms loss window
followed by delivery recovery started one visual blend. The first corrected
draw matched the old trajectory within the existing 0.75 summed-world-unit
conversion tolerance (observed 0.000); maximum correction was 25.1 world
units, and the offset ended within 100 ms. It checks both 60 and 120 FPS
frames without a simulation tick and verifies repeated reads, all live cars,
the simulation context, and RNG are unchanged by frame preparation. A
50 Hz client race smoke check passed. The existing 36/100 Hz client checks
exercise rollback, delayed mode, ownership, 30 s pause, host commits,
checkpoint/rejoin, and replay isolation. The pause test now checks the visual
cache remains unchanged. The pure recovery test checks wrapped angles,
retargeting without restart, oversize and life-change cuts, and explicit reset.

During the synthetic moving-car segment, maximum measured separation between
the drawn remote pose and its live collision proxy was 14.0 world units,
against a 758.2-unit car diagonal. This is a quantitative seam observation,
not a near-contact collision assessment or an Android render measurement.

Verified locally on Windows with `zig build test-net-presentation
test-net-client -j1 -Doptimize=ReleaseSafe`, the full 28-profile
`measure-net-smooth-s4` target, native ReleaseSafe build, source-set and
core-manifest checks, 19 build/source Python unit tests, and `git diff
--check`. The Android `:app:assembleDebug --offline --no-daemon` build passed
for arm64-v8a and x86_64. A broader native foundation run passed host,
client, coherence, harness, and multi-process tests, but `net_transport_test`
failed at its existing Windows limited-broadcast loopback assertion
(`tests/net_transport_test.c:149`); the failure reproduces when that executable
is run alone and is unrelated to the S4 changes.

`adb devices -l` found no attached device. The APK was compiled but not run on
Android. A same-host, same-track LAN/5G phone comparison, device frame-time
and CPU measurement, background/foreground observation, and visual review of
turns and near-contact collision separation remain pending. The automated
matrix is synthetic evidence and cannot close those physical checks.
