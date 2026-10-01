"""Measure legacy presentation and the independent buffered clock."""

import argparse
import json
from pathlib import Path

from net_harness import Node
from net_race_scenarios import RaceScenario


PROFILES = {
    "lan5": (5, 0, 0),
    "steady100": (100, 0, 0),
    "variable60": (60, 20, 10),
}


def difference(before, after, key):
    return after[key] - before[key]


def measure(args, name, seed):
    latency, jitter, loss = PROFILES[name]
    scenario = RaceScenario(Node, args.server, args.proxy, args.track,
                            args.assets, seed=seed).start()
    try:
        for index in range(3):
            scenario.set_link(index, latency, jitter, loss)
        scenario._run_until(
            lambda states: states[0]["game_frame"] > 145 and
            all(state["running"] for state in states),
            15000, "race did not reach moving play")
        scenario.advance(5000)
        before = scenario.stats()
        remote_car = before[2]["cars"][0]
        yaw = scenario.worldpose(0, remote_car)["yaw"]
        yaw_changes = 0
        measurement_start_ms = scenario.now_ms
        while scenario.now_ms - measurement_start_ms < args.duration_ms:
            scenario.advance(500)
            next_yaw = scenario.worldpose(0, remote_car)["yaw"]
            yaw_changes += next_yaw != yaw
            yaw = next_yaw
        after = scenario.stats()
        client_before, client_after = before[1], after[1]
        modes = [right - left for left, right in zip(
            client_before["display_mode_ms"],
            client_after["display_mode_ms"])]
        buffered_modes = [right - left for left, right in zip(
            client_before["buffered_display_mode_ms"],
            client_after["buffered_display_mode_ms"])]
        gaps = [right - left for left, right in zip(
            client_before["arrival_gap_buckets"],
            client_after["arrival_gap_buckets"])]
        measured_ms = scenario.now_ms - measurement_start_ms
        stalled_ms = modes[2] + modes[3]
        assert client_after["presentation_frames"] > client_before["presentation_frames"]
        assert client_after["advancing_arrivals"] > client_before["advancing_arrivals"]
        assert yaw_changes > 0, "the remote car did not turn during measurement"
        assert client_after["legacy_calls"] == 0
        if args.story == "S2":
            assert client_after["buffered_timeline_regressions"] == 0
            assert client_after["buffered_forward_resyncs"] == 0
            if name in ("lan5", "steady100"):
                assert buffered_modes[2] + buffered_modes[3] == 0
            if name == "variable60":
                assert buffered_modes[1] >= measured_ms * 0.99
        report = {
            "profile": name,
            "seed": seed,
            "configured_one_way_ms": latency,
            "configured_jitter_ms": jitter,
            "configured_loss_permille": loss,
            "route": client_after["route"],
            "measurement_ms": measured_ms,
            "host_ticks": difference(before[0], after[0], "tick"),
            "host_snapshots": difference(before[0], after[0], "full_snapshots") +
                              difference(before[0], after[0], "delta_snapshots"),
            "host_snapshot_send_events": difference(
                before[0], after[0], "snapshot_send_events"),
            "host_snapshot_send_gap_min_ms":
                after[0]["snapshot_send_gap_min_ms"],
            "host_snapshot_send_gap_max_ms":
                after[0]["snapshot_send_gap_max_ms"],
            "client_ticks": difference(client_before, client_after, "tick"),
            "advancing_arrivals": difference(client_before, client_after,
                                             "advancing_arrivals"),
            "dropped_deltas": difference(client_before, client_after,
                                         "dropped_deltas"),
            "arrival_gap_buckets": gaps,
            "arrival_gap_max_ms": client_after["arrival_gap_max_ms"],
            "source_gap_max_ticks": client_after["source_gap_max_ticks"],
            "delivery_variation_max_ms":
                client_after["delivery_variation_max_ms"],
            "rtt_ms": client_after["rtt_ms"],
            "jitter_ms": client_after["jitter_ms"],
            "configured_snapshot_interval_ms":
                client_after["configured_snapshot_interval_ms"],
            "snapshot_received_ago_ms":
                client_after["snapshot_received_ago_ms"],
            "latest_snapshot_tick": client_after["latest_snapshot_tick"],
            "presentation_tick_relative":
                client_after["presentation_tick_relative"],
            "desired_reserve_ms": client_after["desired_reserve_ms"],
            "actual_reserve_ms": client_after["actual_reserve_ms"],
            "history_coverage_ms": client_after["history_coverage_ms"],
            "display_mode_ms": modes,
            "display_starvation_fraction": stalled_ms / measured_ms,
            "buffered_display_mode_ms": buffered_modes,
            "buffered_starvation_fraction":
                (buffered_modes[2] + buffered_modes[3]) / measured_ms,
            "buffered_presentation_tick_relative":
                client_after["buffered_presentation_tick_relative"],
            "buffered_reserve_ms": client_after["buffered_reserve_ms"],
            "buffered_actual_reserve_ms":
                client_after["buffered_actual_reserve_ms"],
            "buffered_playback_speed":
                client_after["buffered_playback_speed"],
            "buffered_forward_resyncs": difference(
                client_before, client_after, "buffered_forward_resyncs"),
            "buffered_long_frames": difference(
                client_before, client_after, "buffered_long_frames"),
            "buffered_reserve_saturations": difference(
                client_before, client_after,
                "buffered_reserve_saturations"),
            "buffered_timeline_regressions": difference(
                client_before, client_after,
                "buffered_timeline_regressions"),
            "timeline_regressions": difference(client_before, client_after,
                                               "timeline_regressions"),
            "recovery_samples": difference(client_before, client_after,
                                           "recovery_samples"),
            "recovery_error_world_max":
                client_after["recovery_error_world_max"],
            "recovery_yaw_error_deg_max":
                client_after["recovery_yaw_error_deg_max"],
            "remote_blend_ms": client_after["remote_blend_ms"],
            "presentation_epochs": client_after["presentation_epochs"],
            "frame_max_ms": client_after["frame_max_ms"],
            "remote_yaw_changes": yaw_changes,
            "replay_ticks": difference(client_before, client_after,
                                      "replay_ticks"),
            "corrections": difference(client_before, client_after,
                                     "corrections"),
        }
        if name == "lan5":
            scenario._frame(100)
            stall_nodes = scenario.stats()
            stalled = stall_nodes[1]
            report["injected_joint_frame_stall_ms"] = 100
            report["frame_max_ms_after_stall"] = stalled["frame_max_ms"]
            report["host_ticks_on_stall_frame"] = (
                stall_nodes[0]["tick"] - after[0]["tick"])
            assert stalled["frame_max_ms"] >= 100
        if name == "steady100":
            report["hypothesis_reproduced"] = stalled_ms > 0
        return report
    finally:
        scenario.close()


def main():
    parser = argparse.ArgumentParser()
    for key in ("server", "proxy", "track", "assets"):
        parser.add_argument("--" + key, required=True)
    parser.add_argument("--profile", choices=[*PROFILES, "all"],
                        default="all")
    parser.add_argument("--duration-ms", type=int, default=10000)
    parser.add_argument("--output")
    parser.add_argument("--story", choices=["S1", "S2"], default="S1")
    args = parser.parse_args()
    for key in ("server", "proxy", "track", "assets"):
        setattr(args, key, str(Path(getattr(args, key)).resolve()))
    names = list(PROFILES) if args.profile == "all" else [args.profile]
    result = {
        "story": "NET-SMOOTH-" + args.story,
        "measurement": ("legacy in-tick puppet and independent buffered clock"
                        if args.story == "S2" else
                        "legacy in-tick puppet pose, sampled once per frame"),
        "warmup_ms": 5000,
        "mode_order": ["warmup", "interpolating", "extrapolating",
                       "holding", "older_than_history"],
        "profiles": [measure(args, name, 0x5100 + index)
                     for index, name in enumerate(names)],
    }
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        Path(args.output).write_text(encoded, encoding="ascii")
    print(encoded)


if __name__ == "__main__":
    main()
