# Netcode E6 notes

## Loopback candidate fix (2026-09-28)

The Pixel's captured failure showed a direct `JOIN_REQUEST` sent to `[::1]:7777`
and received back from that same address. The desktop advertised loopback as a
candidate; on the phone this addressed the phone itself. Its own probe
acknowledgement selected a false direct route, and its own channel
acknowledgement removed the reliable join request. The subsequent relay route
had no join request left to send (`pending=0`, relay TX/RX `6/0`).

- Interface enumeration now omits loopback and inactive interfaces. Discovery
  also filters loopback supplied directly by callers before advertising it.
- Replies from a public rendezvous cannot select IPv4 loopback (`127/8`), IPv6
  loopback (`::1`), or IPv4-mapped loopback. Matching probe/acknowledgement
  packets from those sources are ignored too. Candidates matching the local
  endpoint are also excluded. Usable candidates in the same reply remain, and an
  all-rejected reply still reaches the relay deadline.
- Explicit direct loopback connections remain supported. A loopback rendezvous
  can still discover processes on different local ports for development; the
  frontend's own bound port is excluded.
- There is no wire-format change or rendezvous redeployment requirement. An
  updated client handles loopback candidates from an older desktop host.

A new real-UDP frontend regression reproduced the phone's sequence before the
fix: self-directed join, repeated relay allocations, no pending join, timeout.
Coverage includes IPv6, IPv4 and mapped IPv4 loopback candidates injected into
directory replies. The discovery test separately exercises public-rendezvous
filtering, rejected inbound probes/acks, mixed usable/unusable candidates and an
all-rejected set. The physical phone's 5G path still needs a retest.

Validation passed: `test-net-foundations` (including transport, discovery and
frontend startup), relay races at 36/100 Hz, source/manifest checks, the native
Windows ReleaseSafe build and Android `assembleDebug` for arm64-v8a and x86_64.
The refreshed artifacts are `zig-out/bin/roller.exe`, its diagnostic copy
`zig-out/bin/roller-netdiag.exe`, and
`android/app/build/outputs/apk/debug/app-debug.apk`. SDL remains at 3.2.22.

## Live relay follow-up and diagnostics (2026-09-28)

The user confirmed that `rvz.fatal.racing` runs this project's rendezvous daemon
on a DigitalOcean droplet. The earlier handoff's "deployment not run" entries
are historical; the deployment method and remote administration details have not
been recorded here.

The updated Android client reached `GAME FOUND VIA RELAY`, then timed out. From
this Windows PC, a temporary diagnostic registration received both relay
notifications and forwarded packets in both directions through the live daemon.
A second test used the actual transport, discovery, channel and session sources
and completed an authenticated join through that daemon. Both test endpoints
were on this PC; this does not validate the phone's mobile-data path. Temporary
listings were unregistered, and no deployment or server configuration changed.

The frontend now logs `[NET]` status transitions, relay allocation/offer
notifications, and the first join/control packet in each direction on both
direct and relay paths. A failure includes packet counts, socket-error counts,
join/route state, pending reliable messages and whether an identity was
accepted. Logs contain endpoint addresses and message metadata, never payloads
or tokens. Packet tracing stops once the race starts. These diagnostics captured
the loopback failure described above.

The Android debug APK was rebuilt with these diagnostics and SDL 3.2.22. The 16
KB compatibility work was reverted at the user's request. The Windows diagnostic
build is `zig-out/bin/roller-netdiag.exe`, alongside the running host's original
executable. Frontend join/loading regressions, Windows and Android builds, and
source/manifest checks passed.

For the next test, restart the desktop host with `roller-netdiag.exe`, install
the new debug APK, and retry with phone Wi-Fi off. Keep USB connected for ADB;
it does not require switching the phone back to Wi-Fi. After the attempt:

```powershell
adb logcat -d -s ROLLER:I '*:S' | Select-String '\[NET\]'
```

## Internet join recovery (2026-09-28)

A desktop listen host could be listed on an Android phone over mobile data, but
joining stayed at `CONNECTING TO HOST`. LAN joining worked. The reported
transition from `CONNECTING TO SELECTED GAME` took about half a second, before
the three-second relay deadline.

The direct-path check treated receipt of a probe as success. That proved only
that host-to-client packets arrived; the client's replies could still be
blocked. Success stopped the probe timeout, so the session handshake could wait
forever without requesting a relay. A real-UDP regression reproduced that exact
status by dropping direct client-to-host traffic.

- Direct selection now requires a probe acknowledgement. On an incoming probe,
  the endpoint also probes its observed source, which can differ from the
  daemon's candidate when NAT assigns ports per destination.
- The frontend requests a relay if the direct game handshake still has not
  completed after three seconds. Route changes preserve queued reliable
  messages. An initial handshake or relay-allocation timeout reports
  `CONNECTION TIMED OUT - PLEASE TRY AGAIN` and closes the attempt.
- Relay allocation retries continue until the session/configuration arrives
  (bounded to ten seconds). Receipt of the client's allocation alone does not
  prove the host received its offer. Only clients request relays; timed-out
  hosts no longer allocate relays to themselves.
- Repeated candidate notifications do not extend the original punch deadline. No
  wire layout or rendezvous-daemon change is required.

`test-net-frontend-start` now runs real UDP cases for a one-way direct path plus
a lost first host relay offer, successful probes followed by a blocked game
handshake, and an unavailable relay. Successful cases complete the real
session/configuration/player-info/ready exchange; the unavailable relay exits
with a visible timeout. Existing LAN/direct loading and race tests remain. These
fixtures reproduce network failures locally; a physical Android 5G retest is
still required to confirm the user's network.

Validation passed: foundations, full-state coherence, host, client, harness,
frontend startup, relay races at both rates, native Windows ReleaseSafe build,
Android `assembleDebug` (arm64-v8a and x86_64), source/manifest checks, 19
Python configuration tests, Markdown formatting, and `git diff --check`.

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
with the registration datagram's observed source port. This preserves the public
NAT mapping for the E6-S3 punching story; E6-S2 must register through the same
UDP endpoint that will receive game and punch traffic.

The service has no per-request allocation. Its single creation allocation
contains fixed arrays for 512 sessions and 1,024 source rate buckets. It
enforces four live sessions per source IP and a 20 packet/s token bucket with a
burst of 40. Malformed packets are dropped, rate-limited packets are silently
dropped, all decoded sizes, reserved fields, flags, counts, tick rates, text
fields, ids and authentication tokens are checked before state is changed, and
registration fails closed when the platform CSPRNG is unavailable. Relay state
and budgets remain E6-S4 work; no punch or relay packet is handled by this
story.

`roller-rendezvous` is a dual-stack UDP executable with port 7778 as its
default, `--port` for an override, and signal-driven shutdown. Zig exposes
`build-net-rendezvous` and installs the requested artifact under `zig-out/bin`;
CMake exposes the same binary as `net-rendezvous` with output name
`roller-rendezvous`.

`test-net-rendezvous`, also included by `test-net-foundations`, covers wire
validation, retry idempotence, authentication, refresh, unregister, expiry,
build filtering, large page numbers, 12-entry pagination, the four-per-IP and
512-global caps, CSPRNG failure, and exact token-bucket refill. Its synthetic
soak advances a virtual clock through 86,400 one-second registrations across 512
source addresses. Live registrations stay at a 30-slot high-water mark, rate
state stays within its fixed 1,024 slots, all registrations expire at the end,
and one response is produced for every accepted request.

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

The focused test and virtual 24-hour soak, standalone Windows daemon build and
CLI smoke, Linux cross-compile, complete focused netcode regressions,
51.97-second real-UDP multi-process race, full native build, source/manifest
checks, and all 18 selected Python tests pass. Source counts remain Linux 98,
macOS 98, Windows 101, Android 98 and Emscripten 95; roller-core remains 114
translation units because this story fills its existing reserved source slot.

Not run: deployment on `fatal.racing`, a CMake configure, Android, or a native
macOS build. In-game registration/listing is E6-S2, NAT punching is E6-S3, and
relay allocation and byte/packet budgets are E6-S4.

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
over-budget packet is dropped and produces a rate-limited `RELAY_THROTTLED`
control message to its sender. The frontend reports
`RELAY BANDWIDTH LIMIT REACHED`. Relay packet and payload-byte totals, throttled
packets, live/high-water counts and expiry counts are exposed in
`tNetRendezvousStats`. Relays expire after 30 seconds without game traffic and
are removed with their host registration; host heartbeat address migration also
updates every attached relay endpoint.

`test-net-discovery` forces direct punching to time out, completes the relay
allocation and sends a reliable channel message through the daemon while both
channels retain the real logical peer addresses. `test-net-rendezvous` proves
that three clients sharing one simulated public IP receive distinct relays, that
the 36 Hz and 100 Hz packet ceilings pass exactly, that the independent 96 KiB
byte ceiling rejects its first excess packet, that both directions forward, and
that throttle messages and accounting are exact.

The existing two-bot dedicated acceptance also has a forced-relay mode.
`test-net-relay-race` runs a complete one-lap TRACK5 race at both 36 Hz and 100
Hz through two separate relays. Both runs completed in 1,715 authoritative ticks
with no relay throttling. The endpoint-only bots do not run prediction, so
prediction mode is recorded as not applicable rather than inventing a
client-mode result. The 36 Hz run forwarded 5,352/5,400 packets and
282,370/531,072 payload bytes; the 100 Hz run forwarded 5,230/5,248 packets and
279,686/492,806 payload bytes (client-to-host/host-to-client).

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

The focused relay tests, direct dedicated regression, complete focused netcode
suite, full native ReleaseSafe build, source/manifest checks and all 18 selected
Python tests pass. Source counts remain Linux 99, macOS 99, Windows 102, Android
99 and Emscripten 96; roller-core remains 115 translation units. CMake
registrations were source-checked but not configured locally. Android, native
Linux/macOS and a real deployed-daemon relay race were not run.

## NET-E6-S2: host registration and client listing in the game

Implemented on 2026-09-24. The changes are intentionally uncommitted.

`net_discovery.c/.h` is the game-side rendezvous client. It shares the game UDP
endpoint through the channel's non-`RLR1` datagram callback, so a host's
registration, heartbeat and later punch traffic use the public mapping of the
socket that accepts the game connection. Registration retries once per second
until acknowledged, refreshes its 30-second lease every 10 seconds, updates the
advertised player/race state, and unregisters during clean shutdown.

The browser requests all 12-entry pages, optionally filters on the complete
build hash, retains at most the daemon's fixed 512-session ceiling, and
refreshes every five seconds. A protocol omission discovered in this story was
fixed without reducing the 12-entry page: `RESOLVE(session id)` returns the
selected registration's observed IPv4 or IPv6 endpoint. List entries had only a
port, so they could not otherwise be joined. The resolve result is validated
before it becomes a `tNetAddress`.

The native frontend accepts `--rendezvous IP[:PORT]`. Listen hosts register
after their authoritative session configuration exists. Clients without a direct
`--peer` browse on the player screen, render up to 12 session names, occupancy
and tracks, and resolve the first compatible result for the existing lobby path.
Direct connect remains available and unchanged. NAT candidate exchange and
selection remain E6-S3.

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

The focused discovery/daemon tests, complete focused netcode regressions and the
full native build pass. Source counts are Linux 99, macOS 99, Windows 102,
Android 99 and Emscripten 96; roller-core contains 115 translation units. The
selected Python suite has 18 tests. A real-daemon two-network manual check,
CMake configure, Android and native macOS were not run.

## NET-E6-S3: UDP hole punching

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The rendezvous registration now carries up to seven ordered local interface
candidates. A punch request carries the joining endpoint's local candidates and
a CSPRNG 64-bit nonce. The daemon appends each endpoint's observed source
address, removes duplicates without changing order, sends an authenticated offer
to the registered host, and returns the host set to the joining client.
Candidate messages are fixed-width, explicitly little-endian, size-asserted, and
completely validated, including reserved and unused bytes, before use.

Peers then exchange 20-byte `PNC1` probes directly on the shared game socket;
punch packets never pass through the daemon. Local candidates are tried in
enumeration order before the server-observed candidate, one every 100 ms and
then round-robin until a three-second deadline. Either a matching probe or its
acknowledgement selects the packet's observed source address, which is the
address handed to the existing channel/session join. Rendezvous requests retry
once per second until an answer arrives. The existing direct-connect and
`RESOLVE` paths remain available and unchanged.

The native browser now enumerates local addresses with the bound game port,
starts punching for its selected listing, and does not create a game connection
until a direct path succeeds. It reports `DIRECT CONNECTION TIMED OUT` when
every candidate remains unanswered. This is the terminal outcome for E6-S3;
NET-E6-S4 owns relay allocation and fallback.

`test-net-discovery` covers the complete daemon/host/client exchange. Both peers
first try an unreachable local candidate, remain in progress, then select the
observed public endpoints in the next candidate slot. A second attempt receives
candidates while the host endpoint is deliberately not pumped and reaches the
exact timeout instead of retrying forever.

Manual NAT matrix (not run on this Windows development machine):

| Host NAT             | Client NAT           | Expected E6-S3 result              | Status  |
| -------------------- | -------------------- | ---------------------------------- | ------- |
| Same LAN             | Same LAN             | Local candidate selected           | Not run |
| Full-cone/restricted | Full-cone/restricted | Observed candidates select         | Not run |
| Port-restricted      | Port-restricted      | Simultaneous probes select         | Not run |
| IPv6 global          | IPv6 global          | IPv6 candidate selects             | Not run |
| Symmetric            | Any NAT              | May time out; E6-S4 relay required | Not run |
| Any NAT              | Symmetric            | May time out; E6-S4 relay required | Not run |

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
tests pass. Source counts remain Linux 99, macOS 99, Windows 102, Android 99 and
Emscripten 96; roller-core remains 115 translation units. The manual NAT matrix,
CMake configure, Android, macOS, and the real-daemon two-network check were not
run.

## NET-E6-S5: LAN discovery on the modern protocol

Implemented on 2026-09-24. The changes are intentionally uncommitted.

The game now creates discovery even when no rendezvous address or direct peer
was configured. `LND1` uses the existing game UDP socket and a 24-byte query
sent to the IPv4 limited-broadcast address on the configured game port. A listen
host answers the source address with a 93-byte advertisement containing the same
fixed-width session entry used by rendezvous. Queries retry every two seconds
and advertisements expire after six seconds without refresh.

The LAN wire structs are packed and size-asserted. Version, type, reserved
bytes, filter values, tick rate, player counts, flags and printable terminated
display fields are validated before a browser entry changes. The full 16-byte
build identifier remains valid without a terminator, matching the rendezvous
contract. A browser records the datagram's source address rather than trusting
an advertised address or port, and selecting that entry goes directly to the
existing authenticated session join path without punching or relay allocation.
Internet browsing, direct connect, punching and relay fallback remain unchanged;
LAN replies can coexist with rendezvous results.

The UDP transport enables `SO_BROADCAST`, and the addressed simulated transport
models limited-broadcast fan-out by destination port. The discovery acceptance
now covers a host and browser with no rendezvous object, build filtering,
malformed-advertisement rejection, direct address selection and stale-entry
expiry. The foundations suite also sends a real limited-broadcast datagram
between two Windows UDP endpoints, so the platform socket path is exercised in
addition to the simulated LAN.

Verification passed on Windows with Zig 0.15.2:

```powershell
zig build test-net-foundations -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build test-net-full-state-coherence test-net-host test-net-client `
  test-net-harness -Doptimize=ReleaseSafe `
  '-Dassets-path=D:/source/repos/ROLLER/zig-out/fatdata-demo' `
  '-Dsoak-track=TRACK5.TRK'
zig build -Doptimize=ReleaseSafe
python tools/check_source_set_drift.py
python tools/check_roller_core_manifest.py
python -m unittest tests.test_source_set_drift `
  tests.test_roller_core_manifest tests.test_game_build_matrix `
  tests.test_cmake_roller_core
```

The focused LAN/rendezvous test, real UDP broadcast check, complete foundations
suite, coherence, host, client and harness regressions, full native build,
source/manifest checks and all 18 selected Python tests pass. Source counts
remain Linux 99, macOS 99, Windows 102, Android 99 and Emscripten 96;
roller-core remains 115 translation units. Not run: the plan's physical
two-machine LAN check, CMake configure, Android, or native Linux/macOS.
