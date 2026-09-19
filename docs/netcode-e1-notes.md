# NET-E1 implementation notes

## E1-S1 UDP transport

The real transport uses one non-blocking IPv6 UDP socket with `IPV6_V6ONLY`
disabled. IPv4 destinations are sent as IPv4-mapped IPv6 addresses, and mapped
source addresses are normalized back to `NET_ADDR_IPV4`. Received IPv6 scope
IDs are retained so enumerated link-local addresses remain usable.

`NetAddressParse` and `NetAddressFormat` accept numeric IPv4 and IPv6 forms.
IPv4 ports use `127.0.0.1:port`; IPv6 ports use `[::1]:port`. An IPv6 scope can
be a numeric ID or an interface name when parsing and is formatted as a numeric
ID. Local address enumeration uses `GetAdaptersAddresses` on Windows and
`getifaddrs` on BSD-socket platforms.

The default transport clock is `QueryPerformanceCounter` on Windows and
`CLOCK_MONOTONIC` elsewhere. `NetTransportUdpSetClock` replaces it with a host
callback so the multi-process harness can supply virtual time.

The transport is a native platform source and remains excluded from
`roller-core`, as required by the E0-S1 source partition. The native game and
dedicated server compile it directly. The transport acceptance executable
covers IPv4 and IPv6 loopback, address parsing and formatting, local address
enumeration, non-blocking receive, payload bounds, and the clock override.

## E1-S2 packet channels

Each channel owns a transport and up to 16 connections. Before a join is
authenticated, token zero is address-bound. After authentication, lookup is
token-first and the source address may change. `NetPump` services every
registered channel from the frame loop;
it does not inspect or depend on simulation tick state or pause state.

Packet sequences and the 32-bit ACK window use wrap-safe 16-bit comparisons.
Reliable entries remain queued until any packet carrying them is acknowledged,
and are resent after 100 ms. Reliable-ordered messages use a 64-message send
and receive window, which bounds reordering memory while applying backpressure
when an old message remains unacknowledged. Duplicate packets and reliable
messages are discarded. A generation mismatch is dropped before ACK or
message processing.

Connections send an empty ACK-only packet when needed and a channel-internal
`PING` every second while otherwise idle. The ping is acknowledged at packet
level to keep RTT current, while empty ACK packets do not request another ACK,
avoiding an ACK ping-pong. A connection expires after 10 seconds without a
valid packet. RTT and jitter use acknowledged packet timestamps on the
transport's monotonic or harness-supplied clock.

## E1-S3 join handshake and session tokens

The session layer owns the join control messages while the channel continues
to own packet framing and reliability. A client starts with a token-zero,
generation-zero connection to the host. The host channel listener accepts only
a framed `NET_MSG_JOIN_REQUEST` into a provisional connection. A valid request
is promoted to generation 1 and receives a per-player 64-bit session token.
The accept packet carries that token in both its packet header and its payload;
the provisional client verifies the match before promoting its connection.

The E1-S3 wire payloads are packed and fixed size:

- `tNetJoinRequest`: protocol version, local-player count, reserved byte, and
  a NUL-terminated player name of at most eight characters (13 bytes).
- `tNetJoinAccept`: 64-bit token, generation, player index, and zero padding
  (12 bytes).
- `tNetJoinRefuse`: reason, zero padding, and the host's expected protocol
  version (4 bytes).

All multibyte fields are encoded little-endian rather than copied from native
struct storage. Requests reject invalid local-player counts, reserved bytes,
and unterminated or empty names. Accepts reject zero tokens, generation zero,
out-of-range player indices, nonzero padding, or a packet token/generation that
does not match the payload.

`NetSessionHostCreate` requires a random callback and tests it before enabling
the channel listener. Failure or an all-zero result refuses hosting. The native
provider is `NetPlatformRandomBytes`, backed by Windows CNG on Windows and the
kernel `/dev/urandom` source on POSIX platforms. Tests inject a deterministic
provider and a failing stub.

Once authenticated, a matching nonzero token and generation identifies the
player. A packet from a new IPv4/IPv6 address updates the existing connection's
peer address, retaining the same player slot and connection. A generation
mismatch is dropped before any address update. The simulation transport can
change an endpoint address so this behavior is covered without real sockets.
