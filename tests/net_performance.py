"""Collect reproducible NET-E8-S5 M4 network measurements."""

import argparse
import json
from pathlib import Path

from net_harness import Node
from net_race_scenarios import RaceScenario


def byte_rates(before, after, duration_ms):
    seconds = duration_ms / 1000.0
    return {
        "out_bytes_per_second":
            (after["wire_bytes_sent"] - before["wire_bytes_sent"]) / seconds,
        "in_bytes_per_second":
            (after["wire_bytes_received"] -
             before["wire_bytes_received"]) / seconds,
    }


def main():
    parser = argparse.ArgumentParser()
    for name in ("server", "proxy", "track", "assets"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--ticks", type=int, default=5000)
    args = parser.parse_args()
    for name in ("server", "proxy", "track", "assets"):
        setattr(args, name, str(Path(getattr(args, name)).resolve()))

    scenario = RaceScenario(Node, args.server, args.proxy, args.track,
                            args.assets, seed=0xE8A5).start()
    try:
        race = scenario.run_race(args.ticks)
        before = race["measurement_start"]
        after = race["nodes"]
        duration_ms = race["measurement_ms"]
        host_rates = byte_rates(before[0], after[0], duration_ms)
        client_rates = [byte_rates(before[i], after[i], duration_ms)
                        for i in range(1, len(after))]
        full = after[0]["full_snapshots"] - before[0]["full_snapshots"]
        delta = after[0]["delta_snapshots"] - before[0]["delta_snapshots"]
        snapshot_bytes = (after[0]["snapshot_bytes"] -
                          before[0]["snapshot_bytes"])

        crossover = None
        for configured_rtt in range(360, 621, 20):
            scenario.hold_rtt(0, configured_rtt, 2500)
            current = scenario.stats()[1]
            if current["prediction_mode"] == 1:
                crossover = {
                    "configured_rtt_ms": configured_rtt,
                    "measured_rtt_ms": current["rtt_ms"],
                    "transition_count": current["prediction_transitions"],
                }
                break
        if crossover is None:
            raise AssertionError("client did not degrade by 620 ms RTT")

        scenario.hold_rtt(0, 120, 4000)
        recovered = scenario.stats()[1]
        if recovered["prediction_mode"] != 0:
            raise AssertionError(("client did not recover", recovered))

        result = {
            "running_ticks": args.ticks,
            "measurement_ms": duration_ms,
            "wall_seconds": race["wall_seconds"],
            "host": host_rates,
            "clients": client_rates,
            "snapshots": {
                "full": full,
                "delta": delta,
                "payload_bytes": snapshot_bytes,
                "average_payload_bytes": snapshot_bytes / (full + delta),
                "delta_share": delta / (full + delta),
            },
            "corrections": [node["corrections"] for node in after[1:]],
            "replay_ticks": [node["replay_ticks"] for node in after[1:]],
            "crossover": crossover,
            "recovery": {
                "configured_rtt_ms": 120,
                "measured_rtt_ms": recovered["rtt_ms"],
                "prediction_mode": recovered["prediction_mode"],
                "transition_count": recovered["prediction_transitions"],
            },
        }
        print(json.dumps(result, indent=2, sort_keys=True))
    finally:
        scenario.close()


if __name__ == "__main__":
    main()
