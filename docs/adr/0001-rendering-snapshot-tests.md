# ADR 0001: Rendering snapshot tests — comparison policy and format

- **Status:** Accepted
- **Date:** 2026-05-07
- **Drives:** PRD #131; implementations #132, #133, #134
- **Supersedes:** none
- **Superseded by:** none

## Context

The rendering pipeline is mid-refactor. A `game_render` abstraction has
landed on `feat/game-render`, and a GPU backend is planned to follow. There
is no automated way to detect whether a change to a rasterizer, a palette
path, or replay-driven game logic has altered the pixels the game produces.
Reviewers can read code but cannot mechanically tell that a refactor
preserves visual output.

We want a regression net that:

- Runs locally on a developer's machine in a single command.
- Compares actual rendered output against a known-good baseline on every
  change.
- Surfaces differences as reviewable artifacts (binary PNG diffs in pull
  requests).
- Can be updated deliberately when a pixel change is intentional.
- Works on `master`, on `feat/game-render`, and on any future GPU backend
  whose contract is "produce the same pixels".

## Decision

### Compare via git itself, not a custom byte-comparator

The harness writes the captured PNGs straight into the checked-in
baseline directory (`tests/snapshots/baselines/`) and then runs
`git diff --exit-code -- tests/snapshots/baselines/`. Any pixel change
manifests as a tracked-file modification, which the build step turns into
a non-zero exit. The PR diff that lands in code review *is* the comparison
artifact — GitHub renders binary PNGs as side-by-side image diffs that the
reviewer can swipe, onion-skin, or 2-up.

Rejected alternatives:

- A custom in-tree byte-comparator and red-where-different diff PNG was
  the original design (see git history of this ADR). It worked but
  duplicated machinery git already provides, and the resulting
  red-dot diff PNG was a programmatic localisation artifact rather than
  a review-grade visual diff. GitHub's image diff is what reviewers
  actually use.
- Tolerance-based RGB comparison would mask single-index off-by-one
  rasterizer regressions that round-trip to the same on-screen RGB. The
  signal we want is exactly "did the indexed framebuffer change", which
  byte-equality of the indexed PNG already gives us.

### Indexed PNG with PLTE chunk as the storage format

Baselines are stored as 8-bit indexed PNGs with the active 256-entry
palette in the PLTE chunk. The PNG's pixel buffer is the indexed
framebuffer one-to-one (one byte per pixel = one palette index), and
viewers render it correctly without us re-baking colours.

Rejected alternatives:

- Raw `.bin` framebuffers would be byte-exact but unreviewable in PRs.
- RGB PNGs require the writer to expand the palette and the comparer to
  contract it back, opening an opportunity for the colour transform
  itself to be wrong. The library `stb_image_write` is RGB-only, which
  is what pushed us to use a palette-aware encoder (initially vendored
  lodepng, later migrated to `SDL_image`'s `IMG_SavePNG` since the
  project already links `SDL3_image`).

### Baseline storage uses Git LFS, scoped to `tests/snapshots/baselines/*.png`

The baselines live in Git LFS via a scoped `.gitattributes` rule.
Rationale: the new "harness overwrites baselines + git diff drives review"
workflow makes baselines a routinely-updated review substrate, not a
write-once asset. Each blessed pixel change adds a full PNG revision to
history (PNGs already use deflate, so git's pack delta gains very little).
LFS keeps the regular pack store flat regardless of churn, lets shallow
clones stay small, and adds zero friction to PR review (GitHub renders
LFS-backed PNGs in image diffs identically to plain ones).

Scope is deliberately narrow — only the snapshot baselines are LFS-tracked,
not the project's icons or screenshots in `images/`.

### Single-host pixel pinning for the first iteration

Baselines are pinned to whichever single host architecture the implementing
developer is using when the baseline lands (Apple Silicon macOS at first
authoring). Floating-point drift between architectures is acknowledged and
explicitly out of scope for this iteration.

Rationale: the harness's primary value is catching same-host regressions
during a refactor. Cross-platform pixel-exact parity is a separate problem
(involves audit of every floating-point callsite that affects rendered
output) with its own ADR later.

### Update mechanism is "run, review the diff, commit"

To bless an intentional pixel change the developer just runs
`zig build test-snapshots`, sees the build fail, reviews the resulting
working-tree diff, and commits the modified baseline PNGs alongside the
code change. The commit *is* the explicit blessing gesture.

### Non-canonical hosts use scratch capture

`zig build test-snapshots -Dscratch` routes the captures into
`zig-out/snapshot-scratch/` and skips the git-diff check entirely. This
lets a developer on a non-canonical host (Linux x86_64, Windows, etc.)
exercise the capture path and inspect their pixels without ever touching
the LFS-tracked baselines, so the working tree stays clean and the PR
remains scoped to whatever code change motivated the run.

Comparing scratch against the canonical baselines is left to whatever
tool the developer prefers (e.g.
`diff -rq tests/snapshots/baselines zig-out/snapshot-scratch`, or any
image-diff viewer) — the harness intentionally doesn't enshrine a
cross-platform compare path until the cross-platform parity ADR exists.

### Hand-picked frames per replay, not every frame

Baselines are taken at hand-picked tick numbers per replay (3-4 per
replay). Adjacent ticks rarely add information; spread frames are more
useful for localising regressions and keep the baseline directory small
(~1.5 MB total at this scale, before LFS deduplication).

The current per-replay frame lists live in `build.zig`'s
`snapshot_replays` table. Each replay covers ~4 frames spanning meaningful
scene differences; intro3 is short (~200 frames) and gets four
densely-spaced ticks instead.

## Consequences

- Every PR touching the rendering pipeline picks up the gate automatically.
- Intentional pixel changes require a `git add` + `git commit` step on the
  modified baselines, and the PR diff visualises them for the reviewer.
- Cross-platform CI is deferred. The harness will run locally and on a
  pinned-host CI runner; running the harness on a divergent host without
  re-pinning baselines will produce false positives.
- Long-running replay frames may exhibit non-determinism that is not
  caught by `srand(0)` alone. The current baseline frame list deliberately
  excludes intro7 frame 900 because it produces non-reproducible pixels
  across runs; this points at an unseeded RNG consumer or wallclock
  reachable in the deep replay path. To be tracked as a follow-up; the
  harness still gates the pipeline on the 27 stable frames in the
  meantime.
- Contributors and CI runners need `git lfs` installed (one-time
  `git lfs install`). Cloning without LFS still gets the source tree but
  not the actual PNG bytes, which would fail `zig build test-snapshots`
  loudly.

## Implementation pointers

- Capture path: `--snapshot REPLAY --frames N[,M,...] --out DIR` flag on
  the existing `roller` binary. See `PROJECTS/ROLLER/snapshot.{c,h}` and
  `PROJECTS/ROLLER/png_writer.{c,h}`.
- Build step: `zig build test-snapshots`. Drives the snapshot binary
  serially across each entry in `snapshot_replays`, writing PNGs straight
  into `tests/snapshots/baselines/`, then runs
  `git diff --exit-code --stat -- tests/snapshots/baselines/`. With
  `-Dscratch` the captures land in `zig-out/snapshot-scratch/` and the
  diff check is skipped.
- LFS scope: `.gitattributes` carries
  `tests/snapshots/baselines/*.png filter=lfs diff=lfs merge=lfs -text`.
