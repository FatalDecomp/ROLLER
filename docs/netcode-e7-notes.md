# Netcode E7 notes

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
unchanged. The implementation symbols are privately renamed at compile time,
and each wrapper records the entry before forwarding in LEGACY mode. A MODERN
call records a violation and asserts in builds where assertions are enabled.
The real `rollercomms.c` body was not edited; CMake and Zig apply its private
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
them in `race_stats`. It requires zero legacy entries and zero violations on
the host and all three clients after lobby, release, more than 5,000 running
ticks, pause/unpause, a strategy button, correction, disconnect, generation-2
rejoin, and orderly process shutdown.

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

The foundation and 5,000-tick modern harness acceptances passed, including
the zero-call trap assertion. The complete focused netcode regression set,
51-second real-UDP multi-process race, full native build, source/manifest
checks, and all 18 selected Python tests passed. Source counts are Linux 98,
macOS 98, Windows 101, Android 98, and Emscripten 95; roller-core contains 114
translation units.

Not run: a manual two-instance legacy LAN race, a manual native frontend
MODERN race through an Escape exit, CMake configure, Android build, or macOS.
The original legacy implementation bodies and `replay.c` are unchanged.
