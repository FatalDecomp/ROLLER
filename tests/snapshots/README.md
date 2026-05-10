# Rendering snapshot tests

This directory holds the byte-exact PNG baselines that gate the rendering
pipeline. The harness runs the existing `roller` binary in a headless
snapshot mode across the seven intro replays in `fatdata/` and a small
set of deterministic named frontend scenes, captures the indexed
framebuffer to PNGs, writes them straight here, and uses `git diff` to
detect any drift.

**Why this matters:** any change that alters a rasterizer, a palette
path, or replay-driven game logic shows up as a tracked-file diff in
your PR. The reviewer sees the side-by-side image diff in GitHub. No
custom diff viewer required.

For the *why* behind the design (byte-exact comparison, indexed PNG
storage, single-host pinning, Git LFS scope, etc.) see
[`docs/adr/0001-rendering-snapshot-tests.md`](../../docs/adr/0001-rendering-snapshot-tests.md).

## Prerequisites

```sh
# One-time, per checkout:
git lfs install
git lfs pull   # if you cloned without LFS first
```

The baselines under `baselines/` are stored via Git LFS (scoped via
`.gitattributes`). Without `git lfs` your working tree contains
~134-byte pointer files instead of real PNGs, and the harness will
fail loudly.

You also need a populated `fatdata/` directory at the project root (or
pass `-Dassets-path=...`); the snapshot binary loads the intro replay
files from there, same as a normal `zig build run`.

## Running the harness

```sh
zig build test-snapshots
```

On the canonical host (Apple Silicon macOS at the time of writing) this:

1. Builds the `roller` binary if needed.
2. Runs `roller --snapshot introN.gss --frames ... --out
   tests/snapshots/baselines/` once per intro replay, serially, then
   runs `roller --snapshot-scene NAME --frames 1 --out
   tests/snapshots/baselines/` once per named scene.
3. Runs `git diff --exit-code --stat -- tests/snapshots/baselines/`.

If the captures match HEAD, exit 0. If anything diverged, the build
fails and `git status` shows you what changed.

## I changed rendering code and want to bless the new pixels

1. Run `zig build test-snapshots`. Expect it to fail.
2. Inspect the working-tree diff:
   ```sh
   git diff --stat tests/snapshots/baselines/
   ```
   For visual review, open the PNGs in your image viewer of choice or
   push the changes to a draft PR — GitHub renders LFS-backed PNG
   diffs as a side-by-side image viewer.
3. If the changes look intentional and correct:
   ```sh
   git add tests/snapshots/baselines/
   git commit -m "..."
   ```
   The commit *is* the explicit blessing — there's no separate flag.
4. If the changes look wrong, revert:
   ```sh
   git checkout -- tests/snapshots/baselines/
   ```
   then debug the rendering change that caused the divergence.

## I'm on Linux/Windows and want to sanity-check the capture path

The baselines are pinned to one host architecture; running on a
divergent host would write divergent pixels into the LFS-tracked
files. To sanity-check captures without mutating the baselines:

```sh
zig build test-snapshots -Dscratch
# Captures land in zig-out/snapshot-scratch/, the working tree stays clean.
```

To eyeball your platform's pixels against the canonical baselines:

```sh
diff -rq tests/snapshots/baselines zig-out/snapshot-scratch
# Or open the corresponding PNGs side by side in your viewer.
```

If you get pixel-exact equality on a non-canonical host, your platform
is rendering identically to the pinned host (rare and worth noting).
Otherwise differences are expected; treat the canonical baselines as
authoritative for now. Cross-platform pixel parity is an open question
tracked separately in a future ADR.

## What the build options do

| Flag | Effect |
|------|--------|
| (none) | Run captures into `tests/snapshots/baselines/`, then `git diff --exit-code` against HEAD. Fail on divergence. |
| `-Dscratch` | Run captures into `zig-out/snapshot-scratch/` and skip the diff check. Working tree stays clean. |
| `-Dassets-path=PATH` | Use `PATH` instead of `./fatdata` as the data root. |

The list of replays and which frames are captured per replay live in
`build.zig`'s `snapshot_replays` table.

Named scene snapshots live in `build.zig`'s `snapshot_scenes` table. Each
scene currently captures frame `1`, meaning the first `SnapshotPresent()`
call made by that scene's one-frame render driver. Scene PNG names use
`<scene-name>_<present-index>.png`, for example `menu-main_1.png` and
`winner-race_1.png`.

## File layout

```
tests/snapshots/
├── README.md                  ← this file
└── baselines/                 ← indexed PNGs, LFS-tracked
    ├── intro1_60.png          ← <replay-stem>_<frame-index>.png
    ├── intro1_240.png
    ├── intro1_480.png
    ├── intro1_720.png
    ├── intro2_60.png
    ├── ...
    ├── intro7_600.png
    ├── menu-main_1.png        ← <scene-name>_<present-index>.png
    ├── menu-select-car_1.png
    ├── menu-select-track_1.png
    ├── menu-select-type_1.png
    ├── menu-select-disk_1.png
    ├── winner-race_1.png
    └── winner-championship_1.png
```

Each PNG is a 640x400 8-bit indexed image with the active 256-entry
palette in the PLTE chunk. Any image viewer can open them.

## Troubleshooting

**`zig build test-snapshots` fails immediately with "git diff" output
showing every PNG modified.** Your captures match what the snapshot
binary produces today, but those bytes don't match what's in HEAD.
Either:
- You're running on a non-canonical host (use `-Dscratch` instead).
- You changed something on the rendering path that genuinely shifted
  pixels (intended or not — review the diff).
- Your LFS smudge filter didn't run; the working-tree files are LFS
  pointers, not real PNGs. Try `git lfs pull` and retry.

**`zig build test-snapshots` complains it can't find `fatdata/`.**
Either symlink your fatdata into the project root, or pass
`-Dassets-path=/path/to/fatdata`.

**Captures hang or never finish.** A given replay shouldn't take more
than ~30 seconds on a modern machine. If a run sits indefinitely,
suspect determinism: check that no recent change touched the snapshot
init path (especially anything that consumes `time(NULL)` or
`SDL_GetTicks` for branching).

**`intro7` only captures three frames where the others capture four.**
Intentional. Frame 900 of intro7 produces non-deterministic pixels
across runs even with `srand(0)` (suspected: an unseeded RNG consumer
or wall-clock read in the deep replay path). Excluded from the
baseline list pending investigation.

**You see `*.png filter=lfs diff=lfs merge=lfs -text` show up in
`.gitattributes` for the wrong directory.** Some editor / pre-commit
hooks auto-track binaries via LFS the first time you add one. Revert
that — the baselines are the *only* PNGs in the repo that should be
LFS-backed (the project's icons and screenshots in `images/` stay in
plain git).

## Where the policy is documented

- [`docs/adr/0001-rendering-snapshot-tests.md`](../../docs/adr/0001-rendering-snapshot-tests.md)
  — comparison policy, format choice, single-host pinning, LFS scope,
  scratch flow.
- [`docs/plans/2026-05-07-rendering-snapshot-tests-design.md`](../../docs/plans/2026-05-07-rendering-snapshot-tests-design.md)
  — original design document.

## Where the implementation is

- `PROJECTS/ROLLER/png_writer.{c,h}` — indexed-PNG writer (uses `IMG_SavePNG` from SDL3_image, which delegates to libpng)
- `PROJECTS/ROLLER/snapshot.{c,h}` — snapshot-mode state, CLI parsing,
  per-tick zero-screen hook, present hook, manual tick driver, fixed
  settings application
- `build.zig` — `test-snapshots` step, `snapshot_replays` and
  `snapshot_scenes` tables, `-Dscratch` flag
