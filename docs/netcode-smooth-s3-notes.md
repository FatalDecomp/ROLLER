# NET-SMOOTH-S3: per-frame remote visuals

Date: 2026-10-01. S3 connects the S2 arrival-anchored cursor to rendered cars.
The modern client prepares one set of render-only car copies after live ticks
drain and before camera, visibility, and draw preparation. Both split-screen
views read the same copies. The in-tick puppet hook, collision proxies, input
lead, rollback, local prediction, and wire format are unchanged.

## Render path

- `NetClientPresentationFrame` finds one shared snapshot bracket at the
  fractional cursor and samples each snapshot-driven car once. During warm-up
  it holds the latest valid pose. It stores no pointers into snapshot slots.
- Each sample retains the selected snapshot's chunk metadata and converts its
  world pose using current ramp geometry in a temporary `tCar`. The converted
  copy is published through `NetSimRenderCarAt`; no live `Car[]`, context, RNG,
  ramp, or prediction state is written.
- Camera reads in `view.c`, visible track selection, car culling/depth order,
  software car effects, and hardware car/shadow/name rendering use the same
  frame copy. Typed and fallback car commands share `NetSimRenderPoseAt`.
  Cars without a prepared pose use their existing live pose and local render
  correction. Full-prediction owned cars therefore retain their old path;
  delayed-prediction owned cars follow the snapshot-driven visual path.
- Race start, rejoin, checkpoint installation, client destruction, and each
  new frame clear the cache. Replay ticks never advance or clear it. Reading
  the cache twice in one frame returns identical poses.

## Before and after

The synthetic delivery figures below come from the saved
[S1 baseline](netcode-smooth-s1-baseline.json) and
[S2 measurement](netcode-smooth-s2-measurement.json), both at 36 Hz with 16 ms
frame pacing after 5 s warm-up. S3 changes which timeline supplies the mesh;
it does not change the S2 controller or rerun this route measurement. The S3
acceptance test checks the renderer-facing pose seam directly, including frames
with no simulation tick.

| Simulated route | Old in-tick proxy extrapolate/hold | S3 visual timeline extrapolate/hold |
| --- | ---: | ---: |
| 5 ms one way, no loss or variation | 0% | 0% |
| 100 ms one way, no loss or variation | 100% | 0% |
| 60 ms one way, +/-20 ms, 1% loss | 61.1% | 0% |

The right column measures the S2 buffered cursor's history mode. The new
render cache samples at that same cursor. It is not a pixel-level or Android
motion measurement. The debug overlay now labels the older diagnostics
"In-tick proxy" and the buffered diagnostics "Visual" to keep those timelines
distinct.

## Verification

The real client acceptance uses a continuously moving, turning remote car
whose snapshot frame changes between track chunks and airborne. At 36 Hz it
exercises 60 and 120 FPS presentation phases and finds more than 100 frames in
each phase where the draw seam changes position despite no simulation tick.
Each sampled draw pose matches the source motion within 0.75 summed world
units, and repeated reads in one frame are bitwise equal. Preparing a frame
leaves all live cars and the simulation context, including RNG state,
unchanged. The existing 36/100 Hz client suite also covers forced rollback,
delayed prediction, rejoin, and replay isolation.

Passed locally on Windows:

```powershell
zig build test-net-client -j1 -Doptimize=ReleaseSafe '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' '-Dsoak-track=TRACK5.TRK'
zig build -j1 -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
git diff --check
```

Android `assembleDebug` passed for arm64-v8a and x86_64. The APK was compiled
but not run on a phone. Physical LAN/5G comparison, visual/collision separation
near contact, and interruption recovery blending remain for S4. No commit,
push, service deployment, or protocol change was made.
