# NET-SMOOTH-S2: independent playback clock

Date: 2026-10-01. S2 adds an arrival-anchored presentation controller. It is
integrated at accepted advancing snapshots and the existing once-per-frame
presentation hook. The renderer still draws the S1 in-tick puppet pose; S3 will
consume this clock for the visual pose. Input lead, collision placement, wire
messages, snapshot cadence, local prediction, and rollback remain unchanged.

## Controller policy

The clock uses relative double-precision server ticks, anchored to the newest
advancing accepted snapshot and its local monotonic arrival time. Duplicate and
older snapshots can fill client history but cannot move its arrival anchor.
The cursor is initialized only when retained history spans its reserve; before
that, its mode is warm-up. Normal updates advance the prior cursor and never
assign it directly from a packet. A retained-history overrun causes an explicit
counted forward resynchronization.

| Parameter | Initial S2 value |
| --- | ---: |
| Reserve floor | max(50 ms, two configured snapshot intervals) |
| Variation window | 5,000 ms, at most 256 advancing gaps |
| Delivery variation | 95th percentile of positive arrival-gap minus source-gap time |
| Missing-source margin | largest recent source gap beyond two intervals |
| Reserve ceiling | 250 ms; saturated frames counted |
| Reserve reduction | at most 10 ms per second |
| Normal playback speed | 0.95 to 1.05 |
| Phase controller | 10 ms deadband, then 0.005 speed per ms of error |
| Frame advance clamp | 100 ms; clamped frames counted |
| Outage cursor cap | newest source tick plus 100 ms |

The reserve controller uses delivery variation and source gaps separately.
Constant one-way latency is intentionally absent from this arrival-anchored
reserve. The 250 ms ceiling is below the 600 ms snapshot retention window, with
room for an older interpolation endpoint. Source-gap and variation statistics
can still reveal unhealthy delivery while reserve is saturated. The current
implementation recomputes a bounded 256-sample percentile once per frame;
presentation CPU cost on the target phone remains unmeasured.

Pause freezes the cursor and re-anchors the latest tick on resume, so a paused
wall-time interval does not become playback debt. Race start, rejoin, and
checkpoint installation reset the controller epoch. Normal playback is
monotonic; a long interruption may make the next retained history newer than
the cursor, and that forward jump is counted. The S1 legacy display counters
are preserved. New `buffered_*` fields and the debug overlay describe the
controller, not the currently rendered mesh.

## Before and after measurement

The before row is [S1's saved baseline](netcode-smooth-s1-baseline.json).
The after data is [S2's machine-readable rerun](netcode-smooth-s2-measurement.json).
Both use the 36 Hz, 16 ms frame cadence, TRACK5.TRK, 5 s warm-up, and the same
seeded 10.24 s moving-race schedule with turns. Percentages are elapsed-time
weighted. The S2 after column is the independent clock's history mode; it is
not a claim that the currently drawn car has changed.

| Simulated route | S1 legacy extrapolate/hold | S2 buffered extrapolate/hold | Final reserve | Clock regressions |
| --- | ---: | ---: | ---: | ---: |
| 5 ms one way, no loss/variation | 0% | 0% | 119.6 ms | 0 |
| 100 ms one way, no loss/variation | 100% | 0% | 119.6 ms | 0 |
| 60 ms one way, +/-20 ms, 1% loss | 61.1% | 0% | 135.6 ms | 0 |

All three S2 measured segments were 100% bracketed at the controller's cursor,
with no forward resynchronization or reserve saturation. The stable high-latency
case demonstrates that constant delivery delay no longer forces the controller
ahead of received history. The existing drawn puppet still follows the S1
timeline, which is why the same rerun also reports the legacy starvation.

## Verification

The focused controller test covers startup history, 36/50/100 Hz, 5/60/100/150
ms stable delivery, duplicates and old packets, uint32 wrap, delayed delivery,
missing source ticks, reserve growth and shrinkage, pause/resume, retention
overrun, forward resync, and the 100 ms frame clamp. In each stable profile,
1,875 post-warm-up 16 ms frames had zero cursor-ahead frames; at 36 Hz only
17-20 frames per profile needed a speed adjustment, and at 50/100 Hz none did.
The real client acceptance at 36 and 100 Hz checks live diagnostics, bounded
cursor position, and that a forced rollback does not consume presentation time
or move the buffered cursor.

Passed locally on Windows:

```powershell
zig build test-net-presentation test-net-client -Doptimize=ReleaseSafe '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build measure-net-smooth-s2 -Doptimize=ReleaseSafe '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift tests.test_roller_core_manifest tests.test_game_build_matrix tests.test_cmake_roller_core
git diff --check
```

Android `assembleDebug` passed for arm64-v8a and x86_64 using Android Studio's
bundled JDK. The APK was compiled but not run on a phone. The physical LAN/5G
comparison and visual/collision separation measurement remain for S3/S4.
No commit, push, service deployment, or protocol change was made.
