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

## E2-S3 lobby screen over the new session

`net_frontend_lobby.c` owns the native frontend lifetime for a modern direct
connection. The existing `--port`, `--peer`, and `--net-slot` options configure
it; `--net-mode modern` selects it explicitly, leaving LEGACY as the default.
A listen host opens its authoritative socket plus a separate loopback client,
so the host is represented by the same authenticated roster path as every
remote player.

The bridge starts the session when the existing car-selection flow enters the
lobby, applies the received session configuration before publishing UI state,
sends the selected car/control and local track CRC, and mirrors the validated
player list into the legacy lobby display globals. The start prompt is host
only and is enabled only when the authoritative roster is entirely Ready.
Escape tears down the modern session without sending any legacy packet.
The SDL timer's frontend networking branch is also mode-gated, so the modern
lobby does not pulse discovery or the legacy send queue in the background.

Manual localhost invocation:

```text
roller --net-mode modern --port 7777 --net-slot 0 --player1name HOST
roller --net-mode modern --port 7778 --peer 127.0.0.1:7777 --net-slot -1 --player1name CLIENT
```

In both instances select Network, choose a car, and enter the lobby. The host
and client screens use the same ordered roster and ready state. Screenshots
remain a manual release-check artifact because the CI frontend renderer does
not drive two interactive processes.

## E2-S4 race start over the network

The lobby countdown is now a two-phase reliable, ordered barrier. The initial
`NET_COUNTDOWN_LOADING` message freezes the roster and sends every node to the
loading screen with the same logical start tick. Once its race view is loaded
and visible, each node sends a second ready message. The host does not send
`NET_COUNTDOWN_RELEASE` until every racing player has reported loaded. Pending
SDL ticks are discarded while a modern node waits at this barrier, so a slow
loader cannot make another node advance the simulation early.

`net_race_start.c` owns the process-local logical race clock. Release enters
Pre-Start at the agreed start tick; the first simulated frame is labelled with
that tick. Each completed simulation tick advances the label once, and the
clock enters Running when the real simulation leaves `game_frame == 145`.
The lobby acceptance test proves three clients remain blocked until the final
load acknowledgement, enter Pre-Start at tick 4242, and all enter Running at
tick 4387. It also rejects an invalid countdown phase without changing state.

### Loading deadline (remediation R1 NET-FIX-3)

The barrier no longer waits forever for a player that never reports loaded.
On every host lobby pump after the loading countdown, a racing player without
a loaded report is dropped when either:

- its channel connection has expired (`NET_CONNECTION_TIMEOUT_MS`, 10 s of
  silence), since an expired connection can never deliver the message; or
- `NET_REJOIN_GRACE_MS` (60 s, the plan's D8 grace window from open question
  3) has passed since `NetLobbyHostStart`.

The chosen outcome is the fallback, not AI takeover. Giving the car to the AI at
the start line needs the player-to-car ownership bookkeeping that plan 4.11
assigns to E5-S2, and nothing in the modern race path re-reads ownership after
loading yet. A dropped player's session is refused with the new
`NET_JOIN_REFUSE_LOAD_TIMEOUT` reason (a no-op if the connection has already
expired), and the roster keeps the entry as `NET_PLAYER_DROPPED` with its car
index and name, so E5-S2 can hand that car to the AI and E5-S3 can find it for
rejoin. The drop advances the roster revision and is broadcast before the
release. Broadcasts now skip expired connections instead of failing, which
previously would also have blocked the release.

The dropped client is left on its loading screen with the refusal recorded in
its session; nothing yet tells it what happened or takes it back to the
frontend. Reason strings on the modern path are E5-S4. Whether Escape leaves
the barrier cleanly has not been checked interactively.

A track load that blocks the main loop for longer than
`NET_CONNECTION_TIMEOUT_MS` stops that client's keepalives, so the host expires
it and it is dropped even though it would have finished loading. Before this
change the same load hung every player. If cold-disk loads approach 10 s, the
loader needs to pump the network.

`tests/net_lobby_test.c` covers both paths with two clients on the
deterministic transport simulator: a connected client that never reports
loaded, released at exactly 60 000 ms after start, and a client that crashes
mid-load, released about 10 s after its last packet. Both assert the dropped
roster entry and, for the connected case, the refusal reason. The loaded
client's race clock then runs from the agreed start tick with consecutive
labels into Running. The remediation named `tests/net_harness.py`, but that
harness drives the E0 raw-UDP stepping loop and has no session or lobby
commands, so it follows the E2-S2 and E2-S4 lobby acceptance tests instead.

Modern `game_tick_step` records local input and steps one simulation tick only
after release. It does not enter `network_master_tick`, `network_slave_tick`,
or `network_orphan_tick`; those are the legacy lockstep path. The timer thread
likewise does not sample or transmit legacy lockstep input in modern mode.

### `network_on` audit

`network_on` still means that the game has multiple network players. It does
not select a protocol. Every audited use in the requested files falls into one
of these groups:

| File and use | Modern-path decision |
|---|---|
| `3d.c`: shutdown broadcast wait | Legacy only. Modern closes the frontend session directly. |
| `3d.c`: winner-mode save/clear/restore | Keep. This is local presentation state, not transport. |
| `3d.c`: race-entry `net_time[]`, ready send, timeout reset, `net_quit` | Legacy only. Modern start and liveness are session/channel state. |
| `3d.c`: loading countdown selection | Keep. `countdown = 144` is the shared Pre-Start simulation state. |
| `3d.c`: pause send and slave-pause handling | Legacy only until E5-S1 supplies modern pause revisions. |
| `3d.c`: setup of team/message indices and post-race local roster | Keep. These adapt the legacy UI and car ownership arrays without network calls. |
| `3d.c`: F1/F2 message-target selection and network HUD text | Keep as local UI state. No legacy packet is emitted. |
| `3d.c`: Escape readiness, GO sound, and waiting text | Protocol-aware. Modern uses the race barrier; legacy continues to use `active_nodes`. |
| `3d.c`: finished-player and pause presentation | Keep. These are display choices for any multiplayer session. |
| `control.c`: checksum/write-check block | Legacy only. Modern snapshots replace lockstep checksums. |
| `control.c`: race completion branch | Keep behind `net_sim_authority`; remote-authority clients cannot commit it. E4-S4 owns received results. |
| `control.c`: multiplayer respawn conditions | Keep. These are game rules, not transport behavior. |
| `frontend_config.c`: name/config broadcast waits | Legacy only. Modern applies the local edit immediately; authoritative session configuration is E2-S1. |
| `frontend_config.c`: showing and navigating the network options | Keep. This is UI visibility for either protocol. |

The related timer/input audit found the legacy `net_time[]` timeout and
`FLAG_DISCONNECT` synthesis in `sound.c`, rather than in `control_ticks` in the
current source layout. Modern mode cannot reach those lockstep functions.
Strategy-key sampling is also mode-gated so `readuserdata` cannot call the
legacy message sender during a modern race start.

## E2-S5 two local players on one node

Implemented on 2026-09-23.

The native frontend now accepts `--net-local-players 1|2` (default 1). A
two-player node presents that count in its join request, sends both selected
cars in `NET_MSG_PLAYER_INFO`, expands its roster entry into two legacy player
slots, and assigns `player1_car` and `player2_car` to those slots. This keeps
the original split-screen renderer and `readuserdata(0/1)` input mappings in
use during a modern race. If the cached selections name the same car, the
second local player takes the other of cars 0 and 1 rather than sending an
invalid duplicate claim.

The focused 36 Hz client acceptance now creates a real two-local-player
session and proves that the host consumes distinct canonical input for both
cars. A deliberately incomplete one-car own-state message cannot complete a
two-car correction. Once the full pair arrives, the correction restores the
group atomically, replays both cars, regenerates both prediction histories,
and reproduces the uninterrupted second car's complete D19 movement set;
only the documented sub-millimetre world/local position rounding is allowed.
Authoritative result and time fields are deliberately re-published from the
host state at the correction tick and are not part of that movement equality.

The same run raises RTT from 120 ms to 600 ms and checks that both cars become
puppets together, have no prediction history while delayed, and both return
to full prediction after RTT recovers. It exposed a replay bug in the shared
puppet hook: historical replay sampled remote cars using the live client tick
on every replayed step. The client now supplies the actual simulation tick to
the hook during live ticks, correction replay, and checkpoint catch-up.

The final E2-S5 drop requirement remains covered end to end by E5-S2's host
acceptance: expiring a split-screen connection transfers both airborne cars
to AI in one ownership commit without changing either car's physics state.
