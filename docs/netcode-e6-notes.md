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
