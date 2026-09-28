# Modern networking menus

Modern networking is the default. `--net-mode legacy` still selects lockstep.
WASM still has no network-play menu.

In the backtick menu, **Rendezvous server** is a single-line address field,
initially `rvz.fatal.racing:7778`. Enter or leaving the field saves it in
`ROLLER.INI` as `RendezvousServer`. Changes take effect the next time a host or
browser is opened. `--rendezvous HOST[:PORT]` overrides the saved value;
hostnames, numeric IPv4, bracketed IPv6, and `udp://` addresses are accepted. An
omitted rendezvous port means 7778. An empty field enables LAN-only discovery.
The game socket port remains `--port N` (default 7777).

Use **PLAYERS -> NETWORK -> HOST SERVER** to create a lobby, or **JOIN GAME** to
browse. Browsing does not initiate a join. Click a server or select it with
Enter to connect and show the original player/car list. The list retains all
directory pages; use the mouse wheel, Up/Down, Page Up/Page Down, Home/End, or
the on-screen Up/Down controls to navigate. Escape/Back returns to the role
choice and closes browsing. Full and racing servers cannot be selected. An
explicit CLI `--peer` appears as a selectable DIRECT CONNECTION entry.

The player list displays all 16 names and car selections above the message/quit
prompts. M opens the original message composer. ALL PLAYERS broadcasts;
individual messages are sent only to the selected peer. The recipient list
includes the host when used by a client. The existing received message display
is used, consuming each text once, including identical repeated messages.

The host's **GAME TYPE -> CARS** choice sets both the competitor count and
session capacity, initially 16. Split-screen players consume two places. Host
configuration changes before the race are sent to existing clients and the
discovery advertisement; clients acknowledge the new track configuration before
starting. A reduction that cannot hold the current roster or its car selections
is rejected.

Cars can be changed after hosting or joining. The car menu's old positive
`car_request` branch waited for a legacy handshake that modern networking never
completes. Modern selections now run the normal selection animation and send
their updated player information to the host. Both the menu and the host enforce
two copies of each type with 16 cars, one with 8 (or 2). Two local split-screen
players may use both copies. Actual race car slots remain unique.

The existing PLAY/loading flow and the distinction between lobby car designs and
allocated race car slots are preserved. No wire layout, replay format,
`replay.c`, or `rollercomms.c` changed. An updated rendezvous daemon is not
required.

## Validation

Windows, Zig 0.15.2, ReleaseSafe:

- Native game build.
- Foundations, full-state coherence, host, client, and multi-process race
  harness suites with the TRACK5 demo fixture.
- Real UDP frontend test: 17 directory entries, explicit selection of the last
  entry, no automatic connection, rejection of a malformed server name, retry
  after a lost page, and closing/reopening the browser.
- Seven real UDP host/client loading cases covering 8/16 competitors,
  single/split-screen players, both loading orders, roster names, broadcast and
  private text, car changes after joining, and startup camera assignments.
- Lobby coverage for repeated/private/malformed text, two places consumed by
  split-screen, full refusal, live capacity changes, invalid reductions, and two
  same-design local players occupying both available copies.
- Address tests include localhost resolution, hostname syntax, IPv4/IPv6, UDP
  scheme, default/explicit ports, and invalid ports.
- Source-set and roller-core manifest checks and 19 Python configuration tests.
- Rendered and inspected `menu-network-options` and `menu-network-players`
  snapshots (the latter contains 16 fixture players).
- `menu-network-car-change` drives the real menu's Enter handling, changes cars
  twice, accepts a second copy, and refuses a third. Capture at frame 600 so all
  three selections run.

Native snapshots use `--snapshot-scene`, `--frames`, an absolute `--out`
directory, and the usual `--whiplash-root` asset directory. Use frame 30 for
static menus and frame 600 for the car-change regression. The fixture directory
in `test-net-frontend-start` is local UDP; this validation does not claim a live
public-server, physical two-machine, Android, macOS, or Linux play-through.
