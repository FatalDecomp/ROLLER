# Netcode E6 notes

## NET-E6-S1: rendezvous daemon

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`net_rendezvous.c/.h` now implements the fixed-capacity rendezvous service
behind `tNetTransport`. It uses the existing 22-byte packet and 6-byte message
framing with protocol id `RVZ1`, explicit little-endian encoding, and exactly
one control message per datagram. Registration is idempotent by a caller's
64-bit nonce. The service returns a random nonzero 32-bit session id and a
random nonzero 64-bit lease token, accepts authenticated heartbeats and
unregisters, and expires an unrefreshed registration after 30 seconds.

Listings contain at most 12 `tRvzSessionInfo` entries per page and can be
filtered by the complete 16-byte build hash. The advertised port is replaced
with the registration datagram's observed source port. This preserves the
public NAT mapping for the E6-S3 punching story; E6-S2 must register through
the same UDP endpoint that will receive game and punch traffic.

The service has no per-request allocation. Its single creation allocation
contains fixed arrays for 512 sessions and 1,024 source rate buckets. It
enforces four live sessions per source IP and a 20 packet/s token bucket with
a burst of 40. Malformed packets are dropped, rate-limited packets are
silently dropped, all decoded sizes, reserved fields, flags, counts, tick
rates, text fields, ids and authentication tokens are checked before state is
changed, and registration fails closed when the platform CSPRNG is
unavailable. Relay state and budgets remain E6-S4 work; no punch or relay
packet is handled by this story.

`roller-rendezvous` is a dual-stack UDP executable with port 7778 as its
default, `--port` for an override, and signal-driven shutdown. Zig exposes
`build-net-rendezvous` and installs the requested artifact under
`zig-out/bin`; CMake exposes the same binary as `net-rendezvous` with output
name `roller-rendezvous`.

`test-net-rendezvous`, also included by `test-net-foundations`, covers wire
validation, retry idempotence, authentication, refresh, unregister, expiry,
build filtering, large page numbers, 12-entry pagination, the four-per-IP
and 512-global caps, CSPRNG failure, and exact token-bucket refill. Its
synthetic soak advances a virtual clock through 86,400 one-second
registrations across 512 source addresses. Live registrations stay at a
30-slot high-water mark, rate state stays within its fixed 1,024 slots, all
registrations expire at the end, and one response is produced for every
accepted request.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-rendezvous build-net-rendezvous -Doptimize=ReleaseSafe
zig build test-net-foundations test-net-full-state-coherence test-net-host `
  test-net-client test-net-bot test-net-dedicated test-net-harness `
  -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build test-net-multiprocess -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build build-net-rendezvous -Dtarget=x86_64-linux-gnu `
  -Doptimize=ReleaseSafe
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The focused test and virtual 24-hour soak, standalone Windows daemon build
and CLI smoke, Linux cross-compile, complete focused netcode regressions,
51.97-second real-UDP multi-process race, full native build, source/manifest
checks, and all 18 selected Python tests pass. Source counts remain Linux 98,
macOS 98, Windows 101, Android 98 and Emscripten 95; roller-core remains 114
translation units because this story fills its existing reserved source
slot.

Not run: deployment on `fatal.racing`, a CMake configure, Android, or a
native macOS build. In-game registration/listing is E6-S2, NAT punching is
E6-S3, and relay allocation and byte/packet budgets are E6-S4.

## NET-E6-S4: relay fallback with relay budgets

Implemented on 2026-09-24. The changes are intentionally uncommitted.

After the three-second direct-punch deadline, discovery now requests a relay
instead of ending the join attempt. The daemon allocates one of 64 fixed relay
slots, creates a nonzero CSPRNG relay id and 64-bit token, and sends an
authenticated offer to the registered host plus the allocation to the client.
Repeating the request is idempotent, so a lost offer or answer is recovered by
the one-second retry. Each endpoint installs a channel route for the original
logical peer address; the existing session and channel layers therefore keep
their normal peer identity while game packets travel inside a 20-byte `RLY1`
envelope through the rendezvous endpoint.

The daemon authenticates the relay id, token and exact source endpoint before
forwarding. Each direction has independent token buckets capped at
`4 * unTickRateHz` packets/s and 96 KiB/s with a one-second burst. An
over-budget packet is dropped and produces a rate-limited
`RELAY_THROTTLED` control message to its sender. The frontend reports
`RELAY BANDWIDTH LIMIT REACHED`. Relay packet and payload-byte totals,
throttled packets, live/high-water counts and expiry counts are exposed in
`tNetRendezvousStats`. Relays expire after 30 seconds without game traffic and
are removed with their host registration; host heartbeat address migration
also updates every attached relay endpoint.

`test-net-discovery` forces direct punching to time out, completes the relay
allocation and sends a reliable channel message through the daemon while both
channels retain the real logical peer addresses. `test-net-rendezvous` proves
that three clients sharing one simulated public IP receive distinct relays,
that the 36 Hz and 100 Hz packet ceilings pass exactly, that the independent
96 KiB byte ceiling rejects its first excess packet, that both directions
forward, and that throttle messages and accounting are exact.

The existing two-bot dedicated acceptance also has a forced-relay mode.
`test-net-relay-race` runs a complete one-lap TRACK5 race at both 36 Hz and
100 Hz through two separate relays. Both runs completed in 1,715 authoritative
ticks with no relay throttling. The endpoint-only bots do not run prediction,
so prediction mode is recorded as not applicable rather than inventing a
client-mode result. The 36 Hz run forwarded 5,352/5,400 packets and
282,370/531,072 payload bytes; the 100 Hz run forwarded 5,230/5,248 packets
and 279,686/492,806 payload bytes (client-to-host/host-to-client).

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-discovery test-net-rendezvous test-net-dedicated `
  test-net-relay-race -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
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

The focused relay tests, direct dedicated regression, complete focused
netcode suite, full native ReleaseSafe build, source/manifest checks and all
18 selected Python tests pass. Source counts remain Linux 99, macOS 99,
Windows 102, Android 99 and Emscripten 96; roller-core remains 115 translation
units. CMake registrations were source-checked but not configured locally.
Android, native Linux/macOS and a real deployed-daemon relay race were not
run.

## NET-E6-S2: host registration and client listing in the game

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`net_discovery.c/.h` is the game-side rendezvous client. It shares the game
UDP endpoint through the channel's non-`RLR1` datagram callback, so a host's
registration, heartbeat and later punch traffic use the public mapping of the
socket that accepts the game connection. Registration retries once per second
until acknowledged, refreshes its 30-second lease every 10 seconds, updates
the advertised player/race state, and unregisters during clean shutdown.

The browser requests all 12-entry pages, optionally filters on the complete
build hash, retains at most the daemon's fixed 512-session ceiling, and
refreshes every five seconds. A protocol omission discovered in this story
was fixed without reducing the 12-entry page: `RESOLVE(session id)` returns
the selected registration's observed IPv4 or IPv6 endpoint. List entries had
only a port, so they could not otherwise be joined. The resolve result is
validated before it becomes a `tNetAddress`.

The native frontend accepts `--rendezvous IP[:PORT]`. Listen hosts register
after their authoritative session configuration exists. Clients without a
direct `--peer` browse on the player screen, render up to 12 session names,
occupancy and tracks, and resolve the first compatible result for the
existing lobby path. Direct connect remains available and unchanged. NAT
candidate exchange and selection remain E6-S3.

`test-net-discovery` runs the real discovery client, channel demultiplexer and
rendezvous daemon over the simulated addressed transport. It covers
registration, build-filtered listing, public endpoint resolution, heartbeat
metadata update and unregister. It is also part of `test-net-foundations`.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-discovery test-net-rendezvous -Doptimize=ReleaseSafe
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
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The focused discovery/daemon tests, complete focused netcode regressions and
the full native build pass. Source counts are Linux 99, macOS 99, Windows
102, Android 99 and Emscripten 96; roller-core contains 115 translation
units. The selected Python suite has 18 tests. A real-daemon two-network
manual check, CMake configure, Android and native macOS were not run.

## NET-E6-S3: UDP hole punching

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The rendezvous registration now carries up to seven ordered local interface
candidates. A punch request carries the joining endpoint's local candidates
and a CSPRNG 64-bit nonce. The daemon appends each endpoint's observed source
address, removes duplicates without changing order, sends an authenticated
offer to the registered host, and returns the host set to the joining client.
Candidate messages are fixed-width, explicitly little-endian, size-asserted,
and completely validated, including reserved and unused bytes, before use.

Peers then exchange 20-byte `PNC1` probes directly on the shared game socket;
punch packets never pass through the daemon. Local candidates are tried in
enumeration order before the server-observed candidate, one every 100 ms and
then round-robin until a three-second deadline. Either a matching probe or its
acknowledgement selects the packet's observed source address, which is the
address handed to the existing channel/session join. Rendezvous requests retry
once per second until an answer arrives. The existing direct-connect and
`RESOLVE` paths remain available and unchanged.

The native browser now enumerates local addresses with the bound game port,
starts punching for its selected listing, and does not create a game
connection until a direct path succeeds. It reports `DIRECT CONNECTION TIMED
OUT` when every candidate remains unanswered. This is the terminal outcome for
E6-S3; NET-E6-S4 owns relay allocation and fallback.

`test-net-discovery` covers the complete daemon/host/client exchange. Both
peers first try an unreachable local candidate, remain in progress, then
select the observed public endpoints in the next candidate slot. A second
attempt receives candidates while the host endpoint is deliberately not
pumped and reaches the exact timeout instead of retrying forever.

Manual NAT matrix (not run on this Windows development machine):

| Host NAT | Client NAT | Expected E6-S3 result | Status |
|---|---|---|---|
| Same LAN | Same LAN | Local candidate selected | Not run |
| Full-cone/restricted | Full-cone/restricted | Observed candidates select | Not run |
| Port-restricted | Port-restricted | Simultaneous probes select | Not run |
| IPv6 global | IPv6 global | IPv6 candidate selects | Not run |
| Symmetric | Any NAT | May time out; E6-S4 relay required | Not run |
| Any NAT | Symmetric | May time out; E6-S4 relay required | Not run |

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-discovery test-net-rendezvous -Doptimize=ReleaseSafe
zig build test-net-foundations test-net-full-state-coherence test-net-host `
  test-net-client test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
```

The focused candidate-order/timeout test, rendezvous limits and virtual 24-hour
soak, broader foundations, coherence, host, client and harness suite, full
native ReleaseSafe build, source/manifest checks, and all 18 selected Python
tests pass. Source counts remain Linux 99, macOS 99, Windows 102, Android 99
and Emscripten 96; roller-core remains 115 translation units. The manual NAT
matrix, CMake configure, Android, macOS, and the real-daemon two-network check
were not run.
