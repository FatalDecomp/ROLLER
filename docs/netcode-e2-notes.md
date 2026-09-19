# NET-E2 implementation notes

## E2-S1 session configuration

`net_config.c` builds `tNetSessionConfig` from the existing setup globals and
host-owned session options. `net_config_codec.c` owns its platform-independent
wire validation and serialization. The legacy timer cheats select 36, 50, or
100 Hz; the decoder rejects a tick rate that disagrees with those flags. Stock
and community track CRCs are computed through the existing track CRC routine.

The 312-byte payload has an explicit little-endian codec. Decode uses a local
staging value, validates every field and string, and only then publishes the
configuration. Apply repeats validation before changing any legacy global, so
an invalid payload cannot partially change setup state. The apply step updates
only the setup bits represented by the configuration and preserves unrelated
cheat and texture flags.

The host installs its validated configuration before accepting players. After
the reliable ordered join accept, it sends `NET_MSG_SESSION_CONFIG` on the same
connection. The client decodes into staging and publishes the configuration
only after full validation; applying it remains an explicit caller step.

The focused acceptance test covers stock and community tracks, exact
build/encode/decode/apply agreement, little-endian scalar layout, dedicated
pause policy, an in-memory host/client join and configuration exchange,
malformed fields, unchanged decode output on rejection, and unchanged globals
when apply rejects a configuration.

## E2-S2 host and client lobby state

`net_lobby.c` owns the host-authoritative lobby roster. Session joins become
`LOBBY` entries, clients can update their car/control selection and ready flag,
and each accepted change advances a 16-bit revision and broadcasts a reliable,
ordered complete player list. Clients validate the entire list into staging
before publishing it, so malformed or stale updates cannot partially alter
lobby state.

Ready messages carry the client's local track CRC. A mismatch changes the
authenticated session to refused with
`NET_JOIN_REFUSE_TRACK_CRC_MISMATCH`; it never reaches `READY`. Once every
joined player is ready, the host broadcasts one revision and start tick in the
countdown and moves the canonical roster to `RACING`.

Legacy strategy selections use the chat envelope with a distinct strategy
kind and values 0 through 3. The host supplies the authenticated sender index,
validates an optional target, and rebroadcasts the canonical message. Reserved
bytes and strategy text must be zero.

The transport simulator now routes up to eight addressed endpoints, retaining
independent deterministic outgoing link settings. The focused acceptance test
uses one host and three clients to exercise join, identical player lists,
ready, strategy propagation, and an identical start tick, plus a separate
track-CRC refusal and malformed-strategy rejection.
