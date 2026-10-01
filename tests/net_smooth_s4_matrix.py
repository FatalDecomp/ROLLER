"""Reproducible multi-process NET-SMOOTH-S4 delivery matrix."""

import argparse
import json
from pathlib import Path

from net_harness import Node
from net_race_scenarios import RaceScenario


FIXED = {
    "lan5": (5, 5, 0, 0, 0, 0),
    "steady60": (60, 60, 0, 0, 0, 0),
    "steady100": (100, 100, 0, 0, 0, 0),
    "steady150": (150, 150, 0, 0, 0, 0),
    "asymmetric20_150": (20, 150, 0, 0, 0, 0),
    "asymmetric150_20": (150, 20, 0, 0, 0, 0),
    "variable60": (60, 60, 20, 10, 0, 0),
    "reorder_duplicate": (60, 60, 0, 0, 50, 50),
}
DYNAMIC = ("burst1", "burst2", "burst3", "transition", "outage500",
           "frame_pacing")


def set_route(scenario, down_ms, up_ms, jitter=0, loss=0, duplicate=0,
              reorder=0):
    values = (jitter, loss, duplicate, reorder)
    scenario.proxy.command(
        f"route 0 1 {down_ms} {' '.join(map(str, values))}")
    scenario.proxy.command(
        f"route 1 0 {up_ms} {' '.join(map(str, values))}")


def summarize(before, after):
    a, b = before[1], after[1]
    modes = [right - left for left, right in zip(
        a["buffered_display_mode_ms"], b["buffered_display_mode_ms"])]
    active_ms = sum(modes[1:])
    assert active_ms > 0, modes
    result = {
        "measurement_ms": active_ms,
        "display_mode_ms": modes,
        "interpolating_percent": round(100 * modes[1] / active_ms, 3),
        "extrapolating_percent": round(100 * modes[2] / active_ms, 3),
        "holding_percent": round(100 * modes[3] / active_ms, 3),
        "warmup_ms": modes[0],
        "advancing_arrivals": b["advancing_arrivals"] -
                              a["advancing_arrivals"],
        "dropped_deltas": b["dropped_deltas"] - a["dropped_deltas"],
        "source_gap_max_ticks": b["source_gap_max_ticks"],
        "arrival_gap_max_ms": b["arrival_gap_max_ms"],
        "reserve_ms": b["buffered_reserve_ms"],
        "reserve_saturations": b["buffered_reserve_saturations"] -
                               a["buffered_reserve_saturations"],
        "forward_resyncs": b["buffered_forward_resyncs"] -
                           a["buffered_forward_resyncs"],
        "timeline_regressions": b["buffered_timeline_regressions"] -
                                a["buffered_timeline_regressions"],
        "visual_recovery_blends": b["visual_recovery_blends"] -
                                  a["visual_recovery_blends"],
        "visual_discontinuities": b["visual_discontinuities"] -
                                   a["visual_discontinuities"],
        "visual_cut_hold_ms": b["visual_cut_hold_ms"] -
                              a["visual_cut_hold_ms"],
        "visual_recovery_error_world_max":
            b["visual_recovery_error_world_max"],
        "visual_recovery_yaw_error_deg_max":
            b["visual_recovery_yaw_error_deg_max"],
        "frame_max_ms": b["frame_max_ms"],
        "rtt_ms": b["rtt_ms"],
        "route": b["route"],
        "legacy_calls": b["legacy_calls"],
    }
    assert result["timeline_regressions"] == 0, result
    assert result["legacy_calls"] == 0, result
    assert 50 <= result["reserve_ms"] <= 250, result
    return result


def measure(args, name, rate, seed):
    scenario = RaceScenario(Node, args.server, args.proxy, args.track,
                            args.assets, seed=seed, tick_rate=rate).start()
    try:
        if name in FIXED:
            down, up, jitter, loss, duplicate, reorder = FIXED[name]
            set_route(scenario, down, up, jitter, loss, duplicate, reorder)
        else:
            set_route(scenario, 60, 60)
        scenario._run_until(
            lambda states: states[0]["game_frame"] > 145 and
            all(state["running"] for state in states),
            15000, "race did not reach moving play")
        scenario.advance(5000)
        before = scenario.stats()
        phase = {}
        if name in FIXED:
            scenario.advance(args.duration_ms)
        elif name.startswith("burst"):
            count = int(name[-1])
            sent_before = scenario.host.command(
                "race_stats")["snapshot_send_events"]
            while scenario.host.command("race_stats")[
                    "snapshot_send_events"] == sent_before:
                scenario._frame(1)
            scenario._frame(1)
            sent_before = scenario.host.command(
                "race_stats")["snapshot_send_events"]
            drop_start_ms = scenario.now_ms
            scenario.proxy.command("route 0 1 60 0 1000 0 0")
            while scenario.host.command("race_stats")[
                    "snapshot_send_events"] - sent_before < count:
                scenario._frame(1)
            # The host queues a snapshot after its channel pump; that queued
            # packet is transmitted on the following frame.
            scenario._frame(1)
            scenario.proxy.command("route 0 1 60 0 0 0 0")
            scenario.advance(3000)
            phase["drop_window_ms"] = (
                scenario.now_ms - drop_start_ms - 3000)
            phase["requested_dropped_snapshots"] = count
        elif name == "transition":
            set_route(scenario, 120, 120)
            scenario.advance(10000)
            phase["high_latency"] = summarize(before, scenario.stats())
            set_route(scenario, 20, 20)
            restore_before = scenario.stats()
            scenario.advance(10000)
            phase["restored"] = summarize(restore_before, scenario.stats())
        elif name == "outage500":
            set_route(scenario, 60, 60, loss=1000)
            scenario.advance(500)
            phase["outage"] = summarize(before, scenario.stats())
            set_route(scenario, 60, 60)
            scenario.advance(3000)
        elif name == "frame_pacing":
            for label, step in (("30fps", 33), ("60fps", 17),
                                ("120fps", 8)):
                start = scenario.stats()
                end_ms = scenario.now_ms + 5000
                while scenario.now_ms < end_ms:
                    scenario._frame(step)
                phase[label] = summarize(start, scenario.stats())
            scenario._frame(100)
        after = scenario.stats()
        result = {
            "profile": name, "tick_rate_hz": rate, "seed": seed,
            "configured_down_ms": FIXED[name][0] if name in FIXED else 60,
            "configured_up_ms": FIXED[name][1] if name in FIXED else 60,
            **summarize(before, after),
        }
        result["max_missing_snapshots"] = max(
            0, result["source_gap_max_ticks"] // 2 - 1)
        if phase:
            result["phases"] = phase
        if name.startswith("burst"):
            assert result["max_missing_snapshots"] == int(name[-1]), result
        if name in ("lan5", "steady60", "steady100", "steady150",
                    "asymmetric20_150", "asymmetric150_20"):
            assert result["extrapolating_percent"] == 0, result
            assert result["holding_percent"] == 0, result
        if name == "variable60":
            assert result["interpolating_percent"] >= 99, result
        return result
    finally:
        scenario.close()


def main():
    parser = argparse.ArgumentParser()
    for key in ("server", "proxy", "track", "assets"):
        parser.add_argument("--" + key, required=True)
    parser.add_argument("--profile", choices=[*FIXED, *DYNAMIC, "all"],
                        default="all")
    parser.add_argument("--rate", choices=("36", "100", "both"),
                        default="both")
    parser.add_argument("--duration-ms", type=int, default=30000)
    parser.add_argument("--output")
    args = parser.parse_args()
    for key in ("server", "proxy", "track", "assets"):
        setattr(args, key, str(Path(getattr(args, key)).resolve()))
    names = [*FIXED, *DYNAMIC] if args.profile == "all" else [args.profile]
    rates = (36, 100) if args.rate == "both" else (int(args.rate),)
    profiles = [measure(args, name, rate, 0x5400 + index * 2 + rate // 100)
                for index, (rate, name) in enumerate(
                    (rate, name) for rate in rates for name in names)]
    result = {"story": "NET-SMOOTH-S4", "warmup_ms": 5000,
              "mode_order": ["warmup", "interpolating", "extrapolating",
                             "holding", "older_than_history"],
              "profiles": profiles}
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        Path(args.output).write_text(encoded, encoding="ascii")
        print(f"NET-SMOOTH-S4 matrix passed {len(profiles)} profiles; "
              f"saved {args.output}")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
