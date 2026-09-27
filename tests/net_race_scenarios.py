"""Reusable multi-process race scenarios for NET-E8-S1."""

import time


class RaceScenario:
    """One authoritative host and three predictive clients behind netsim."""

    def __init__(self, node_factory, server, proxy, track, assets, seed=0xE8A1):
        self._node_factory = node_factory
        self._server = server
        self._proxy_path = proxy
        self._track = track
        self._assets = assets
        self._seed = seed
        self.proxy = None
        self.host = None
        self.clients = []
        self.nodes = []
        self.now_ms = 0
        self.last = []

    def start(self):
        self.proxy = self._node_factory([self._proxy_path, str(self._seed)])
        self.nodes.append(self.proxy)
        for _ in range(4):
            node = self._node_factory([
                self._server, "--net-harness", "0", "--track-path",
                self._track, "--assets-path", self._assets,
            ])
            self.nodes.append(node)
        self.host = self.nodes[1]
        self.clients = self.nodes[2:]
        for endpoint, node in enumerate([self.host, *self.clients]):
            self.proxy.command(f"peer {endpoint} {node.ready['udp']}")
            self.proxy.command(f"link {endpoint} 20 2 10 0 0")
        self.host.command(
            f"race host {self.proxy.ready['udp']} 0 {len(self.clients)}")
        for endpoint, client in enumerate(self.clients, 1):
            client.command(
                f"race client {self.proxy.ready['udp']} {endpoint}")
        return self

    def close(self):
        for node in reversed(self.nodes):
            node.close()
        self.nodes.clear()

    def _frame(self, frame_ms=16):
        self.now_ms += frame_ms
        self.last = [node.command(f"frame {self.now_ms}")
                     for node in [self.host, *self.clients]]
        self.proxy.command(f"advance {self.now_ms}")
        return self.last

    def _run_until(self, predicate, timeout_ms, message):
        deadline = self.now_ms + timeout_ms
        while self.now_ms < deadline:
            result = self._frame()
            if predicate(result):
                return result
        raise AssertionError((message, self.now_ms, self.last))

    def advance(self, duration_ms):
        end = self.now_ms + duration_ms
        while self.now_ms < end:
            self._frame()
        return self.last

    def run_race(self, running_ticks=5000):
        """Start all nodes, pass the start gate, then measure running ticks."""
        wall_start = time.monotonic()
        self._run_until(
            lambda states: states[0]["running"] and
            all(state["running"] for state in states[1:]),
            15000, "race did not release")
        self._run_until(lambda states: states[0]["game_frame"] > 145,
                        10000, "host did not pass the start gate")
        measurement_start_ms = self.now_ms
        measurement_start = self.stats()
        first_tick = self.last[0]["tick"]
        self._run_until(lambda states: states[0]["tick"] >=
                        first_tick + running_ticks,
                        (running_ticks * 1000 // 36) + 10000,
                        "race did not reach requested running ticks")
        report = self.stats()
        elapsed = time.monotonic() - wall_start
        assert report[0]["tick"] >= first_tick + running_ticks, report
        assert report[0]["players"] == 3, report
        assert all(node["legacy_calls"] == 0 and
                   node["legacy_violations"] == 0 for node in report), report
        for client in report[1:]:
            assert client["running"], client
            assert client["snapshots"] >= running_ticks // 3, client
            assert client["rejected_messages"] == 0, client
            for field in ("replay_depth", "replay_ms_total",
                          "replay_ms_worst", "prediction_mode"):
                assert field in client, client
        assert elapsed < 90, elapsed
        return {"nodes": report, "wall_seconds": elapsed,
                "running_ticks": running_ticks,
                "measurement_ms": self.now_ms - measurement_start_ms,
                "measurement_start": measurement_start}

    def disturb(self, client_index, car, dx=1.0, dy=0.0, dz=0.0):
        return self.clients[client_index].command(
            f"disturb {car} {dx} {dy} {dz}")

    def disturb_engine(self, client_index, car, delta=1.0):
        return self.clients[client_index].command(
            f"disturb_engine {car} {delta}")

    def disturb_lap_time(self, client_index, car, delta=1.0):
        return self.clients[client_index].command(
            f"disturb_lap_time {car} {delta}")

    def set_link(self, client_index, latency_ms, jitter_ms=0,
                 loss_permille=0, duplicate_permille=0,
                 reorder_permille=0):
        endpoint = client_index + 1
        values = (latency_ms, jitter_ms, loss_permille,
                  duplicate_permille, reorder_permille)
        self.proxy.command(
            f"route 0 {endpoint} {' '.join(map(str, values))}")
        self.proxy.command(
            f"route {endpoint} 0 {' '.join(map(str, values))}")

    def hold_rtt(self, client_index, rtt_ms, duration_ms):
        self.set_link(client_index, rtt_ms // 2)
        end = self.now_ms + duration_ms
        while self.now_ms < end:
            self._frame()

    def drop_client(self, client_index, duration_ms=15000):
        self.set_link(client_index, 0, loss_permille=1000)
        end = self.now_ms + duration_ms
        while self.now_ms < end:
            self._frame()

    def rejoin_client(self, client_index, latency_ms=20):
        generation = self.stats()[client_index + 1]["generation"] + 1
        self.set_link(client_index, latency_ms)
        self.clients[client_index].command("rejoin")
        return self._run_until(
            lambda states: states[client_index + 1]["generation"] >=
            generation and states[client_index + 1]["recovery"] == 0,
            15000, "client did not rejoin")[client_index + 1]

    def pause(self, paused=True):
        return self.host.command(f"pause {1 if paused else 0}")

    def strategy_button(self, client_index, strategy, target=-1):
        return self.clients[client_index].command(
            f"strategy {target} {strategy}")

    def ability(self, client_index):
        return self.clients[client_index].command("ability")

    def suppress_own_car_state(self, client_index, suppressed=True):
        endpoint = client_index + 1
        return self.host.command(
            f"suppress_own_car_state {endpoint} {1 if suppressed else 0}")

    def force_correction(self, client_index, car, distance=25.0,
                         timeout_ms=3000):
        before = self.stats()[client_index + 1]["corrections"]
        self.disturb(client_index, car, distance)
        return self._run_until(
            lambda states: states[client_index + 1]["corrections"] > before,
            timeout_ms, "forced correction did not arrive")[client_index + 1]

    def worldpose(self, node_index, car):
        return [self.host, *self.clients][node_index].command(
            f"worldpose {car}")

    def context(self, node_index):
        return [self.host, *self.clients][node_index].command("context")

    def rng(self, node_index):
        return [self.host, *self.clients][node_index].command("rng")

    def stats(self):
        return [node.command("race_stats")
                for node in [self.host, *self.clients]]

    def exercise_controls(self):
        """Smoke every scenario control against the live four-process race."""
        report = self.stats()
        car = report[1]["cars"][0]
        assert car < 16, report[1]
        assert {"x", "y", "z", "yaw"} <= self.worldpose(1, car).keys()
        assert {"game_frame", "readptr", "writeptr"} <= self.context(1).keys()
        assert {"state", "draws"} <= self.rng(1).keys()

        host_tick = report[0]["tick"]
        self.pause(True)
        self.advance(500)
        assert self.stats()[0]["tick"] == host_tick
        self.pause(False)
        self.advance(500)

        self.strategy_button(0, 1)
        self.ability(0)
        self.disturb_engine(0, car, 2.0)
        self.disturb_lap_time(0, car, 0.5)
        self.suppress_own_car_state(0, True)
        self.advance(250)
        self.suppress_own_car_state(0, False)
        self.force_correction(0, car)
        self.hold_rtt(1, 120, 250)

        self.drop_client(0, 11000)
        rejoined = self.rejoin_client(0)
        assert rejoined["generation"] == 2 and rejoined["recovery"] == 0
        report = self.stats()
        assert all(node["legacy_calls"] == 0 and
                   node["legacy_violations"] == 0 for node in report), report
        return report
