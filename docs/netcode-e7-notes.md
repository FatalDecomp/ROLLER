# Netcode E7 notes

## NET-E7-S3: ADR and retirement plan

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`docs/adr/0006-online-netcode.md` records the accepted host-authoritative
architecture and decisions D1 through D21. The correction section preserves the
design history behind D16 through D21: the E0 state audit rejected a second
physics model, rollback depends only on local determinism, restoration uses one
matched post-tick moment, results remain host commits, replay cannot alter
random draws, and over-budget correction changes prediction mode instead of
entering a hitch or resync loop.

The ADR also records D22 and D23 as hardening amendments because they are part
of the implemented contract in plan v9.3. They do not expand E7-S3's scope.

The retirement checklist is deliberately gated rather than scheduled. Modern
networking must first ship, complete a release cycle of real use, pass the
supported-platform multiplayer matrix, remain within its cost budgets, keep the
legacy-call trap silent, and preserve GSS replay compatibility. Removal is then
staged: make MODERN the default for a release, remove the user-facing selector,
remove audited lockstep code, remove the trap and mode only after no call sites
remain, and retain `replay.c` plus replay compatibility coverage. The deletion
series remains isolated and revertible.

Documentation-only verification checks the ADR and notes for ASCII text,
confirms all decision identifiers D1 through D23 occur in the ADR, and checks
the working-tree diff for whitespace errors. No game source, build manifest,
wire format, or replay implementation changed.

### Roller-core CMake CI follow-up

Fixed on 2026-09-25 after all three `roller-core CMake` jobs failed on
`ee4e543`.

- `net_legacy.c` used C11 `<stdatomic.h>`. MSVC requires a separate experimental
  compiler option for that header, while the rest of ROLLER already uses SDL's
  portable atomic boundary. The legacy-call counters now use `SDL_AtomicInt` and
  `SDL_AddAtomicInt`/`SDL_SetAtomicInt`/ `SDL_GetAtomicInt`; their relaxed
  diagnostic-counter semantics are unchanged.
- `ROLLER_EDITOR_CORE=1` was private to the `roller-core` target even though it
  changes public headers. Consequently `roller_server.c` saw `3d.h`'s
  three-argument game `main` declaration and then defined the normal
  two-argument server `main`. The definition is now PUBLIC so every
  `ROLLER::core` consumer parses the headers in the library's mode.
- `tests/test_cmake_roller_core.py` locks both boundaries: the core definition
  must remain PUBLIC and the trap must not return to direct C11 atomics.

The actual Visual Studio CMake core-only build completed and linked
`roller-core.lib`, `roller-server.exe`, `roller-bot.exe`, the editor link
consumer, and all CMake net test executables. The editor link consumer and
`roller-server --help` both ran successfully. The CMake-built foundations
acceptance also passed, including the legacy trap, headless stepping, rollback,
snapshot validation, and replay suppression. Linux x86_64 and macOS arm64
compiler checks of `roller_server.c` passed with the propagated core define. The
19 selected Python configuration, source-set, manifest, and build-matrix tests
pass; the source-set, manifest, CMake-CI policy, and whitespace checks also
pass.

One broader Zig foundations rerun was inconclusive for unrelated local reasons:
several component tests passed, then the existing transport test hit its
`iReceived == sizeof(szPayload)` assertion and Zig reported cache `AccessDenied`
errors while rebuilding compiler runtimes. Neither failure was in a changed
translation unit or the CMake path repaired here.

## NET-E7-S2: replay compatibility check

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The legacy replay implementation and format remain unchanged. A shared test
fixture writes the existing fixed-width GSS header, lets the real simulation
append frames through `DoReplayData`, and then reopens the file through
`startreplay` and reads its first frame through `DoReplayData`.

The host acceptance records one real `NetHostTick` and proves that its replay
contains one frame and loads through the legacy reader. The client acceptance
records the live `NetClientTick` which performs a forced 15-tick correction. The
correction re-simulates those 15 retained ticks with `net_sim_replaying` set,
but the resulting replay contains exactly one frame: the live tick. That file
also loads through the legacy reader. Playback is delayed until the test world's
other assertions are complete because replay loading intentionally mutates the
live car array.

`git diff master -- PROJECTS/ROLLER/replay.c` and the working-tree diff for that
file are empty. This checkout has no local branch named `main`; `master` is its
upstream baseline.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations test-net-full-state-coherence test-net-host `
  test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The complete focused netcode suite, the full native ReleaseSafe build,
source/manifest checks, and all 18 selected Python tests passed. Source counts
remain Linux 99, macOS 99, Windows 102, Android 99, and Emscripten 96;
roller-core remains 115 translation units.

Not run: CMake configure, Android, Linux, macOS, or a manual replay UI check.

## NET-E7-S1: runtime mode switch and legacy-call trap

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The existing `--net-mode legacy|modern` switch remains runtime-selectable and
LEGACY remains the default. Network command-line values are now validated and
retained until parsing finishes, then applied only to the selected transport.
This makes argument order irrelevant and prevents a later `--net-mode modern`
from inheriting an earlier legacy `--port` or `--peer` call. `--local-ip` is
reported as legacy-only when MODERN is selected.

`net_legacy.c/.h` puts one debug-build guard in front of every exported
`network.c` and `rollercomms.c` entry point. The original public names remain
unchanged. The implementation symbols are privately renamed at compile time, and
each wrapper records the entry before forwarding in LEGACY mode. A MODERN call
records a violation and asserts in builds where assertions are enabled. The real
`rollercomms.c` body was not edited; CMake and Zig apply its private
implementation rename while the browser stub opts in at its include seam.

The call-site audit removed unconditional legacy traffic from:

- command-line transport configuration and startup command-base setup;
- final frontend shutdown and results cleanup;
- race entry's legacy wait reset;
- the player-selection modern update and network-type selection paths;
- championship load and legacy connection restoration.

The foundation acceptance verifies that LEGACY is the default and that both
wrapper families preserve calls and return values in that mode. The existing
four-process race resets the counters when each node enters MODERN and reports
them in `race_stats`. It requires zero legacy entries and zero violations on the
host and all three clients after lobby, release, more than 5,000 running ticks,
pause/unpause, a strategy button, correction, disconnect, generation-2 rejoin,
and orderly process shutdown.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build test-net-capture test-net-full-state-coherence test-net-host `
  test-net-client test-net-bot test-net-dedicated test-net-multiprocess `
  -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The foundation and 5,000-tick modern harness acceptances passed, including the
zero-call trap assertion. The complete focused netcode regression set, 51-second
real-UDP multi-process race, full native build, source/manifest checks, and all
18 selected Python tests passed. Source counts are Linux 98, macOS 98, Windows
101, Android 98, and Emscripten 95; roller-core contains 114 translation units.

Not run: a manual two-instance legacy LAN race, a manual native frontend MODERN
race through an Escape exit, CMake configure, Android build, or macOS. The
original legacy implementation bodies and `replay.c` are unchanged.
