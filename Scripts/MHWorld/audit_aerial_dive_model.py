"""Numerical inspection of the first-phase exports; no game/asset writes."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def read(path):
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("directory", type=Path)
    p.add_argument("--attempts", default="1,2,4,8,11,17,18,20,25,30,40,50,60,67,68,71,75,79")
    args = p.parse_args()
    audits = read(args.directory / "attempts.csv")
    rows = read(args.directory / "phase380_trials.csv")
    grouped = {}
    for row in rows:
        grouped.setdefault(int(row["attempt"]), []).append(row)
    print("NEUTRAL", json.dumps([{k: a[k] for k in ["attempt", "prev_id", "samples", "entry_speed_xz",
        "entry_v_forward", "entry_v_right", "first_speed_xz", "end_speed_xz", "forward", "right", "dy"]}
        for a in audits if float(a["stick_on_fraction"]) == 0]))
    for attempt in map(int, args.attempts.split(",")):
        a = audits[attempt - 1]
        series = grouped[attempt]
        t = np.array([float(r["time_s"]) for r in series])
        detail = []
        for query in [0, .008333, .016667, .05, .1, .15, .2, .3, .4, .5, .6, .8, 1.]:
            i = np.argmin(np.abs(t - query))
            if query > t[-1] + .01:
                continue
            r = series[i]
            detail.append({k: round(float(r[k]), 4) for k in ["time_s", "action_frame", "forward", "right", "up",
                "v_forward", "v_right", "vy", "speed_xz", "stick_x", "stick_y", "yaw_deg"]})
        print("ATTEMPT", attempt, "PRE", {k: a[k] for k in ["prev_id", "entry_v_forward", "entry_v_right", "entry_vy", "sample_span_s"]})
        for r in detail:
            print(json.dumps(r))


if __name__ == "__main__":
    main()
