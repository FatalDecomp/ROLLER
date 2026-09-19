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
