# Rendering Snapshot Tests

## Goal

Establish a byte-exact snapshot test harness for the indexed game framebuffer
(`scrbuf`), driven by the existing intro replays in `fatdata/`. The harness
becomes a regression net for the in-flight `game_render` refactor and for any
future change that touches the rasterizers, palette pipeline, or replay-driven
game logic.

## Design Decisions

- **Snapshot source: `scrbuf` directly.** The indexed framebuffer is the
  contract between game logic and presentation. Reading it bypasses every
  presentation concern (GPU, window, blits) and is independent of whether the
  rasterizers are called directly or routed through the new `game_render`
  abstraction.
- **Input: existing `intro1.gss`–`intro7.gss` replays.** Already checked in
  under `fatdata/`, already deterministic enough that the original game shipped
  with them, and they exercise real rendered scenes. Zero capture work.
- **Granularity: hand-picked frame numbers per replay.** A handful per replay
  (e.g. tick 0, 60, 180, 300) rather than every frame. Smaller baselines, more
  meaningful failures, easier review when one frame's PNG changes in a PR.
- **Format: indexed PNG with PLTE chunk.** Exactly mirrors `scrbuf` (one byte
  per pixel = one palette index). Compact (~50 KB worst case for 640×400),
  human-viewable in any image viewer, exact-byte-comparable on the IDAT.
  `stb_image_write` is RGB/RGBA only; `lodepng` supports indexed PNG and is
  vendored under `external/`.
- **Comparison policy: byte-exact match on pixel indices.** Indexed
  determinism is the whole point. No tolerance, no perceptual diff. On
  mismatch, write a third PNG (`*_diff.png`) where differing pixels are red
  and matching pixels are grayscaled, so reviewers can spot regressions
  visually.
- **Update mechanism: `zig build test-snapshots -Dupdate-snapshots`.** A
  `b.option(bool, "update-snapshots", ...)` threaded through to the harness as
  `--write-baseline`. No environment variables. Updated baselines surface as
  PNG diffs in the PR.
- **Baseline storage: checked into git under `tests/snapshots/expected/`.**
  Cheap (~1.4 MB total at 7 replays × 4 frames × 50 KB) and PRs that affect
  rendering surface as visible PNG churn during review.
- **Single binary, CLI-flag-gated.** `--snapshot REPLAY --frames N,M --out
  DIR` on the existing `roller` exe. Simpler than maintaining a sibling
  binary; can split later if `--snapshot`-only code paths get messy.
- **Host pinning, not cross-platform parity.** Floats in horizon and line
  clipping may drift by 1 ULP across architectures. The first iteration pins
  baselines to one CI host (Linux x86_64). Cross-platform parity is a
  separate concern with its own ADR.

## Sequencing

The harness reads `scrbuf` and is independent of `game_render_*`. So it lands
on master first, captures master's pixels as baselines, and then becomes the
ratchet for everything that follows.

1. Land the harness PR off `master`: `--snapshot` CLI mode, `lodepng`
   vendoring, `zig build test-snapshots` step, comparison harness, baselines
   for **one** intro replay (`intro1.gss`, ~4 frames). Proves the loop works
   end to end.
2. Subsequent PRs add the remaining six replays one at a time. Each PR's
   baseline addition is a small, reviewable PNG diff.
3. Rebase `feat/game-render` onto post-merge master. Every commit and PR in
   the refactor stack is now gated: did `scrbuf` change? Yes → reviewer sees
   diff PNGs and decides intentional (update baselines) vs regression (fix
   the rasterizer).
4. Existing in-flight work in `.worktrees/feat-122-quad-screen` is not
   disturbed; it picks up the snapshot gate when it rebases.

## Headless capture mode

In `--snapshot` mode the binary:

1. **Skips GPU init entirely** — no `SDL_CreateGPUDevice`, no swapchain, no
   transfer buffer. The palette is still set the normal way via the game's
   own loading path.
2. **Sets `SDL_VIDEO_DRIVER=dummy`** before `SDL_Init` and creates a hidden
   SDL window. Keeps any code that queries window dimensions happy without
   rendering anything.
3. **Skips audio init** — no MIDI player, no digi mixer.
4. **Replaces `UpdateSDLWindow` with a snapshot hook.** When the current tick
   is in `--frames`, write `<out>/<replay>_<tick>.png` as indexed PNG with
   the current palette in PLTE.
5. **Drives the game loop deterministically.** Replay playback already exists
   in `replay.c`. Snapshot mode forces a fixed `replayspeed` and ticks one
   frame per loop iteration with no wall-clock pacing.
6. **Exits cleanly** after the last requested frame.

## Determinism mitigations

- **Per-tick `memset(scrbuf, 0, winw*winh)`** at the top of the loop in
  snapshot mode. Prevents leaks from previous frames in regions the game
  doesn't redraw (e.g. unused HUD areas).
- **Constant RNG seed.** `srand(0)` once at boot in snapshot mode. Audit
  `rand()` callsites if particle effects or AI start producing flaky output.
- **No real-time clock.** Anything that branches on `time(NULL)` or
  `SDL_GetTicks` gets a fixed clock injected. Audit usage during
  implementation.
- **Acknowledged risk: floating-point in `graphics.c`.** Horizon and line
  clipping use doubles. Should be deterministic on a single host with
  consistent compiler flags but may drift by 1 ULP across architectures.
  Pin baselines to one CI host initially.

## Implementation surface

**New files**

- `tests/snapshots/expected/` — directory of baseline indexed PNGs.
- `tests/snapshot_compare.zig` — Zig harness: walks `expected/`, byte-
  compares IDAT against `zig-out/snapshot-actual/`, writes diff PNGs on
  mismatch, returns non-zero on failure.
- `external/lodepng/` — vendored single-file PNG library.
- `docs/adr/0001-rendering-snapshot-tests.md` — ADR documenting the
  byte-exact policy, host-pinning decision, and indexed PNG format choice.

**Modified files**

- `PROJECTS/ROLLER/roller.c` — argv parsing for `--snapshot REPLAY --frames
  N,M --out DIR`. Snapshot-mode init path that skips GPU/audio, installs the
  snapshot hook in place of `UpdateSDLWindow`, forces fixed `srand(0)` and
  `replayspeed`, `memset`s `scrbuf` per tick.
- `PROJECTS/ROLLER/replay.c` — minor: a load-from-file entry point if not
  already cleanly exposed; a way to drive replays at a fixed tick rate
  without wall-clock pacing.
- `build.zig` — `b.option(bool, "update-snapshots", ...)`, new
  `test-snapshots` step depending on the `roller` exe, runs it once per
  replay then runs `snapshot_compare`. Threads `-Dupdate-snapshots` through
  as `--write-baseline`.

## Out of scope

- Cross-platform baseline parity (own ADR later).
- CI integration (local first; once the harness works, CI is a config
  change).
- Unit-level snapshots of individual `game_render_*` calls (different
  granularity, different failure mode; revisit if integration snapshots
  prove too coarse).
- GPU-backend snapshots (the GPU backend doesn't exist yet; same harness
  will work when it does, since it reads `scrbuf` and the GPU backend's
  contract is "produce the same pixels").
