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

### Compare byte-exact, indexed pixels

The harness reads the indexed framebuffer (`scrbuf`) directly and
byte-compares the decoded pixel index buffers from baseline and actual
PNGs. No tolerance, no perceptual diff. Indexed-pixel equality is a
stronger signal than RGB equality: any rasterizer change that picks a
different palette index — even one that round-trips to the same on-screen
RGB after the palette is applied — is still a regression.

Rejected alternatives:

- Tolerance-based RGB comparison would mask single-index off-by-one
  rasterizer regressions that shift palette swatches.
- Perceptual diffs (SSIM, ΔE) introduce thresholds and tunables that
  produce flaky tests and obscure the underlying signal.

### Indexed PNG with PLTE chunk as the storage format

Baselines and actuals are stored as 8-bit indexed PNGs with the active
256-entry palette in the PLTE chunk. The PNG's pixel buffer is the
indexed framebuffer one-to-one (one byte per pixel = one palette index).

Rejected alternatives:

- Raw `.bin` framebuffers would be byte-exact but unreviewable in PRs.
- RGB PNGs require the writer to expand the palette and the comparer to
  contract it back, introducing an opportunity for the colour transform
  itself to be wrong. The library `stb_image_write` is RGB-only, which is
  what pushed us off it onto vendored lodepng.

### Single-host pixel pinning for the first iteration

Baselines are pinned to whichever single host architecture the implementing
developer is using when the baseline lands (Apple Silicon macOS at first
authoring). Floating-point drift between architectures is acknowledged and
explicitly out of scope for this iteration.

Rationale: the harness's primary value is catching same-host regressions
during a refactor. Cross-platform pixel-exact parity is a separate problem
(involves audit of every floating-point callsite that affects rendered
output) with its own ADR later.

### Update mechanism is a build-system flag

Intentional pixel changes are recorded by running
`zig build test-snapshots -Dupdate-snapshots`. The harness overwrites the
baselines under `tests/snapshots/expected/` with the current actuals. The
PR diff then shows the binary PNG changes for the reviewer to confirm the
change was intended.

Rejected alternatives:

- An environment variable (`UPDATE_SNAPSHOTS=1`) is easier to forget and
  harder to discover. A `-Dflag` is consistent with the rest of the build
  surface.
- A separate "blessed" branch that the harness compares against would
  require a network round-trip and a merge step we don't otherwise need.

### Hand-picked frames per replay, not every frame

Baselines are taken at hand-picked tick numbers per replay (3-4 per
replay). Adjacent ticks rarely add information; spread frames are more
useful for localising regressions and keep the baseline directory small
(~1.5 MB total at this scale).

The current per-replay frame lists live in `build.zig`'s
`snapshot_replays` table. Each replay covers ~4 frames spanning meaningful
scene differences; intro3 is short (~200 frames) and gets four
densely-spaced ticks instead.

### Diff-on-mismatch with red-where-different, grayscale-where-same

When a baseline and actual disagree, the walker produces a third PNG under
`zig-out/snapshot-diff/<name>.png` where pixels that differ map to a fixed
red palette index and pixels that match map to a desaturated grayscale of
the expected pixel. The reviewer can open the diff PNG in any image viewer
and immediately see *what* changed.

The diff PNG uses a synthetic palette (255 grayscale entries plus a red
sentinel at index 255), not the game palette, so the matching pixels are
visually muted and the differences pop.

## Consequences

- Every PR touching the rendering pipeline picks up the gate automatically.
- Intentional pixel changes require a `-Dupdate-snapshots` step and a
  reviewer-visible binary PNG diff.
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

## Implementation pointers

- Capture path: `--snapshot REPLAY --frames N[,M,...] --out DIR` flag on
  the existing `roller` binary. See `PROJECTS/ROLLER/snapshot.{c,h}` and
  `PROJECTS/ROLLER/png_writer.{c,h}`.
- Diff generator: `PROJECTS/ROLLER/png_diff.{c,h}`. Pure function over
  indexed buffers. Unit-tested in `tools/snapshot_walker.zig`.
- Comparison walker: `tools/snapshot_walker.zig`. Decodes via vendored
  lodepng, byte-compares the index buffers, writes diff PNGs on mismatch,
  exits non-zero on any mismatch.
- Build step: `zig build test-snapshots`. With `-Dupdate-snapshots` the
  walker rewrites the baseline directory instead of comparing.
