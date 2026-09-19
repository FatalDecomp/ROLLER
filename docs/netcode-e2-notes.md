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
