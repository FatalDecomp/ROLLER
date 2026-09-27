# Frontend race-start camera regression

## Cause and fix

Lobby player indices and race car slots are different namespaces. For example,
lobby player 0 choosing design 3 becomes `Car[6]` after `AllocateCars()`.
`frontend_main_menu_prepare_race_start()` performs that allocation before the
lobby fade and loading screen. The main loop continues pumping networking.

Previously, `NetFrontendPump()` kept copying the lobby display roster into
`player1_car` and `player2_car` until the loading barrier released. This restored
display indices after allocation, while `ViewType[]` still held race car slots.
The track loader seeded the correct camera using `ViewType[]`, but
`play_game_init()` then reseeded it using the overwritten local player indices.
Consequently, the camera could begin at a different car or in the pits, then
swing around as the actual car moved. Other local-car setup also read those
overwritten indices.

Freeze the legacy display roster when the race is scheduled, before allocation,
not when race ownership is mapped after loading. Session and transport pumping
continue throughout loading. No chase-camera math or startup yaw override is
needed; `view.c` is unchanged.

The new test also exposed two split-screen allocation issues: `check_cars()`
reduced the complete modern network roster to two local players, and allocation
could compare later remote player indices against the already-converted
`player2_car`. Preserve the complete modern roster and the original second-local
player index during allocation. Legacy/offline roster sizing is unchanged.

## Regression coverage

```powershell
zig build test-net-frontend-start -Doptimize=ReleaseSafe -j1
```

The test starts separate listen-host and client processes over real loopback
UDP. Each follows the production order: join, readiness, schedule, frontend
allocation, fade-frame network pumps, real track loading and car placement,
camera initialization, loading barrier, and stationary camera updates. It uses
the actual networking, allocation, loading and camera functions, without a
window or audio device; it is not a screenshot/full-GUI test.

Seven cases cover different designs and seeds, 8/16 competitors, either peer
loading first, and one/two local players per peer. Assertions check allocated
local car indices across every pump and world-space camera positions before
either car moves. The split-screen case with a converted second-car slot equal
to a later player's roster index is included.

On TRACK3, temporarily restoring the old roster guard reproduced the failure
on both sides: host car 6 became player index 0, client car 2 became index 1,
and stationary camera error was approximately 8,993/8,994 world units. With the
fix, all seven cases passed with worst error below 0.02 units.
All seven cases also passed on TRACK16 (worst error below 0.005 units) and
TRACK5 (worst error below 0.01 units). The startup gate is also a dependency
of `test-net-foundations` so it cannot be skipped by the normal foundation run.

The equivalent CMake/CTest test is `net-frontend-start`, enabled with
`ROLLER_NET_TEST_ASSETS`. `test-net-lobby` also checks that 8-slot sessions allow
one player per design and 16-slot sessions allow two, on both host and clients.
Competitor count and session player capacity are separate settings.

Validation also passed the foundation, host, and real-UDP multi-process race
suites, and the client suite on its usual TRACK5 fixture. The client suite on
TRACK3 fails the pre-existing remote-puppet yaw check at
`tests/net_client_test.c:982`; it fails identically with the allocation changes
removed (the frontend adapter is not linked into that test). That separate
track-specific interpolation failure has not been changed or hidden.

## Manual follow-up

Use the rebuilt `zig-out/bin/ROLLER.exe` on both machines. No rendezvous daemon
update is required. Start a host/client game, including one with a non-Auto car,
and inspect the initial chase camera without touching the controls. Repeat with
the other side loading first and after returning to the menus. Both cars should
remain correctly assigned and their cameras should begin behind them. Check the
session-browser refresh visually too; its existing text/cache changes are
independent of this camera fix.
