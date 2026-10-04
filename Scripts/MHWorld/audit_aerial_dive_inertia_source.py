"""Compare pre-action identity and measured RT-edge speed against dive inertia.

Reads the existing 84-attempt capture and audits without altering source files.
Fits all 400/900 x 0/300 trajectory candidates without using an input-speed
threshold, then reports which predictor can distinguish the fitted groups.
MHW recorded coordinates are Y-up; planar velocities use XZ in native units/s.
"""

from __future__ import annotations

import argparse
from array import array
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np


FIELDS = [
    "sample_index", "quest_time_s", "action_lmt_id", "action_fsm_id",
    "action_frame", "facing_yaw_yup_local_z_deg",
    "velocity_x_per_game_second", "velocity_z_per_game_second",
    "internal_increment_x", "internal_increment_z",
    "input_rt_pressed", "input_rt_released", "input_rt_down",
    "input_l_stick_digital_x", "input_l_stick_digital_y",
    "input_axes_supported", "hunter_state_available",
]


def read_rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def load_capture(path):
    columns = {name: array("d") for name in FIELDS}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            for name, column in columns.items():
                try:
                    column.append(float(row.get(name, "")))
                except (ValueError, TypeError):
                    column.append(float("nan"))
    return {name: np.asarray(values) for name, values in columns.items()}


def unit_yaw(degrees):
    radians = math.radians(degrees)
    return np.array([math.sin(radians), math.cos(radians)])


def number(value):
    result = float(value)
    return result if math.isfinite(result) else None


def speed_at(velocities, index):
    return number(np.linalg.norm(velocities[index])) if index >= 0 else None


def snapshot(data, velocities, index, entry, direction):
    if index is None:
        return None
    preceding = max(0, index - 1)
    return {
        "row_index": index,
        "sample_index": int(data["sample_index"][index]),
        "time_s": number(data["quest_time_s"][index]),
        "age_before_entry_s": number(data["quest_time_s"][entry] - data["quest_time_s"][index]),
        "lmt_id": int(data["action_lmt_id"][index]),
        "fsm_id": int(data["action_fsm_id"][index]),
        "action_frame": number(data["action_frame"][index]),
        "vx": number(velocities[index, 0]),
        "vz": number(velocities[index, 1]),
        "internal_increment_x": number(data["internal_increment_x"][index]),
        "internal_increment_z": number(data["internal_increment_z"][index]),
        "speed": speed_at(velocities, index),
        "projected_speed_along_fitted_inertia_direction": number(velocities[index] @ direction),
        "previous_sample_speed": speed_at(velocities, preceding),
        "median_preceding_6_sample_speed": number(np.linalg.norm(
            np.nanmedian(velocities[max(0, index - 6):index], axis=0))) if index else None,
        "digital_stick_x": number(data["input_l_stick_digital_x"][index]),
        "digital_stick_y": number(data["input_l_stick_digital_y"][index]),
    }


def range_summary(values):
    valid = np.array([value for value in values if value is not None], dtype=float)
    if not len(valid):
        return None
    return {"n": len(valid), "min": float(valid.min()), "median": float(np.median(valid)),
            "max": float(valid.max())}


def threshold_comparison(trials, key):
    valid = [trial for trial in trials if trial.get(key) is not None]
    distinct = sorted(set(trial[key] for trial in valid))
    thresholds = [-math.inf] + [(a + b) / 2 for a, b in zip(distinct, distinct[1:])] + [math.inf]
    errors = [sum((400 if trial[key] < threshold else 900) != trial["fitted_base_speed"]
                  for trial in valid) for threshold in thresholds]
    best = min(errors) if errors else None
    best_thresholds = [threshold for threshold, error in zip(thresholds, errors) if error == best]
    low = [trial[key] for trial in valid if trial["fitted_base_speed"] == 400]
    high = [trial[key] for trial in valid if trial["fitted_base_speed"] == 900]
    gap = {"above_low_max": max(low), "at_or_below_high_min": min(high)} if low and high and max(low) < min(high) else None
    closest = sorted(((abs(a[key] - b[key]), a, b)
                      for a in valid if a["fitted_base_speed"] == 400
                      for b in valid if b["fitted_base_speed"] == 900), key=lambda item: item[0])
    return {
        "predictor": key, "n": len(valid), "low_base_group": range_summary(low),
        "high_base_group": range_summary(high), "best_threshold_errors": best,
        "best_threshold_accuracy": (len(valid) - best) / len(valid) if valid else None,
        "perfect_separation_threshold_interval": gap,
        "best_sample_threshold_candidates": [number(value) for value in best_thresholds[:12]],
        "closest_cross_group_pairs": [{"speed_gap": gap_value,
            "low_attempt": a["attempt"], "low_speed": a[key], "low_prev_lmt": a["prev_lmt_id"],
            "high_attempt": b["attempt"], "high_speed": b[key], "high_prev_lmt": b["prev_lmt_id"]}
            for gap_value, a, b in closest[:6]],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--analysis-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    attempts = read_rows(args.analysis_dir / "attempts.csv")
    quality = {int(row["attempt"]): row for row in read_rows(args.analysis_dir / "trajectory_quality.csv")}
    data = load_capture(args.capture)
    velocity = np.column_stack([data["velocity_x_per_game_second"], data["velocity_z_per_game_second"]])
    time = data["quest_time_s"]
    lmt = data["action_lmt_id"].astype(int)
    fsm = data["action_fsm_id"].astype(int)
    boundaries = np.r_[0, np.flatnonzero((lmt[1:] != lmt[:-1]) | (fsm[1:] != fsm[:-1])) + 1, len(lmt)]
    runs = [(int(start), int(end)) for start, end in zip(boundaries[:-1], boundaries[1:])]
    rt_press_edges = np.flatnonzero(data["input_rt_pressed"] == 1)
    rt_release_edges = np.flatnonzero(data["input_rt_released"] == 1)
    trials = []
    for attempt in attempts:
        k = int(attempt["attempt"])
        start, end = int(attempt["start_row"]), int(attempt["end_row_exclusive"])
        assert lmt[start] == 49532 and int(data["sample_index"][start]) == int(attempt["start_sample"])
        local_time = time[start:end] - time[start]
        early = (local_time >= .05) & (local_time <= .55)
        late = (local_time >= .72) & (local_time <= min(.85, local_time[-1] - .03))
        early_velocity = np.nanmedian(velocity[start:end][early], axis=0)
        late_velocity = np.nanmedian(velocity[start:end][late], axis=0)
        target = early_velocity / .6
        pre_velocity = np.nanmedian(velocity[max(0, start - 6):start], axis=0)
        old_yaw = float(data["facing_yaw_yup_local_z_deg"][start - 1])
        motion_yaw = math.degrees(math.atan2(*pre_velocity))
        offset = (motion_yaw - old_yaw + 180) % 360 - 180
        inertia_yaw = old_yaw + 90 * round(offset / 90)
        inertia_direction = unit_yaw(inertia_yaw)
        # Circular mean avoids failures where the recorded facing crosses +/-180.
        facing_radians = np.deg2rad(data["facing_yaw_yup_local_z_deg"][start:end][early])
        stable_yaw = math.degrees(math.atan2(np.mean(np.sin(facing_radians)), np.mean(np.cos(facing_radians))))
        directed = unit_yaw(stable_yaw)
        candidates = sorted(({"base": base, "addition": addition,
            "vector_error": float(np.linalg.norm(base * inertia_direction + addition * directed - target))}
            for base in (400, 900) for addition in (0, 300)), key=lambda candidate: candidate["vector_error"])
        best = candidates[0]
        free_candidates = sorted(({"base": base, "addition": addition,
            "magnitude_error": float(abs(np.linalg.norm(target - addition * directed) - base))}
            for base in (400, 900) for addition in (0, 300)), key=lambda candidate: candidate["magnitude_error"])
        prior_runs = [(begin, finish) for begin, finish in runs if finish <= start]
        preceding_chain = [{"lmt_id": int(lmt[begin]), "fsm_id": int(fsm[begin]),
            "start_sample": int(data["sample_index"][begin]),
            "end_sample": int(data["sample_index"][finish - 1]),
            "start_frame": number(data["action_frame"][begin]),
            "end_frame": number(data["action_frame"][finish - 1]),
            "duration_s": number(time[finish - 1] - time[begin])}
            for begin, finish in prior_runs[-6:]]
        presses = rt_press_edges[(rt_press_edges <= start) & (time[rt_press_edges] >= time[start] - 2)]
        press = int(presses[-1]) if len(presses) else None
        releases = rt_release_edges[(rt_release_edges > press) & (rt_release_edges < end)] if press is not None else np.array([], dtype=int)
        release = int(releases[0]) if len(releases) else None
        snapshots = {"rt_press": snapshot(data, velocity, press, start, inertia_direction),
            "rt_release": snapshot(data, velocity, release, start, inertia_direction),
            "entry_previous_sample": snapshot(data, velocity, start - 1, start, inertia_direction)}
        direct_press_errors = [float(np.linalg.norm(velocity[press] + addition * directed - target))
                               for addition in (0, 300)] if press is not None else []
        result = {
            "attempt": k, "source_start_sample": int(data["sample_index"][start]),
            "fit_usable": quality[k]["fit_usable"] == "True", "quality_reason": quality[k]["reason"],
            "prev_lmt_id": int(lmt[start - 1]), "prev_fsm_id": int(fsm[start - 1]),
            "prev_action_frame": number(data["action_frame"][start - 1]),
            "entry_previous_6_speed": float(np.linalg.norm(pre_velocity)),
            "early_vx": float(early_velocity[0]), "early_vz": float(early_velocity[1]),
            "normalized_early_speed": float(np.linalg.norm(target)),
            "late_speed": float(np.linalg.norm(late_velocity)),
            "early_vs_late_vector_delta": float(np.linalg.norm(target - late_velocity)),
            "stable_yaw": stable_yaw, "fitted_inertia_yaw": inertia_yaw,
            "fitted_base_speed": best["base"], "fitted_addition": best["addition"],
            "fit_vector_error": best["vector_error"],
            "fit_second_best_margin": candidates[1]["vector_error"] - best["vector_error"],
            "free_direction_fitted_base": free_candidates[0]["base"],
            "free_direction_fitted_addition": free_candidates[0]["addition"],
            "free_direction_magnitude_error": free_candidates[0]["magnitude_error"],
            "fitted_free_base_magnitude": float(np.linalg.norm(target - best["addition"] * directed)),
            "direct_rt_press_velocity_min_vector_error": min(direct_press_errors) if direct_press_errors else None,
            "rt_release_precedes_entry": bool(release is not None and release < start),
            "candidate_errors": candidates, "snapshots": snapshots, "preceding_chain": preceding_chain,
        }
        for name, value in snapshots.items():
            for suffix in ("speed", "previous_sample_speed", "median_preceding_6_sample_speed", "projected_speed_along_fitted_inertia_direction"):
                result[name + "_" + suffix] = value[suffix] if value else None
        trials.append(result)

    valid = [trial for trial in trials if trial["fit_usable"]]
    action_groups = []
    for identity in sorted(set((trial["prev_lmt_id"], trial["prev_fsm_id"]) for trial in valid)):
        group = [trial for trial in valid if (trial["prev_lmt_id"], trial["prev_fsm_id"]) == identity]
        counts = {str(base): sum(trial["fitted_base_speed"] == base for trial in group) for base in (400, 900)}
        action_groups.append({"prev_lmt_id": identity[0], "prev_fsm_id": identity[1], "n": len(group),
            "fitted_base_counts": counts, "attempts": [trial["attempt"] for trial in group],
            "rt_press_speed": range_summary([trial["rt_press_speed"] for trial in group]),
            "rt_press_previous_speed": range_summary([trial["rt_press_previous_sample_speed"] for trial in group]),
            "entry_previous_speed": range_summary([trial["entry_previous_sample_speed"] for trial in group]),
            "previous_action_frame": range_summary([trial["prev_action_frame"] for trial in group]),
            "free_base_magnitude": range_summary([trial["fitted_free_base_magnitude"] for trial in group])})
    predictors = ["rt_press_speed", "rt_press_previous_sample_speed", "rt_press_median_preceding_6_sample_speed",
        "entry_previous_sample_speed", "entry_previous_6_speed",
        "rt_press_projected_speed_along_fitted_inertia_direction"]
    identity_errors = {}
    for key in ("prev_lmt_id", "prev_fsm_id"):
        identity_errors[key] = sum(len(group) - max(sum(trial["fitted_base_speed"] == base for trial in group)
                                                   for base in (400, 900))
                                  for identity in set(trial[key] for trial in valid)
                                  for group in [[trial for trial in valid if trial[key] == identity]])
    summary = {
        "status": "READ_ONLY_OBSERVATIONAL_COMPARISON_NO_CAUSAL_IDENTIFICATION",
        "source_capture": str(args.capture), "source_sha256": hashlib.sha256(args.capture.read_bytes()).hexdigest(),
        "source_rows": len(time), "trial_count": len(trials), "usable_trial_count": len(valid),
        "excluded_attempts": [trial["attempt"] for trial in trials if not trial["fit_usable"]],
        "method": "Fit all 400/900 x 0/300 combinations to observed early planar velocity / 0.6. Input-speed magnitude is not used to select base group. Prior planar direction is rounded to the nearest facing-relative 90 degrees, matching the prior candidate model; also fit base/addition by vector-residual magnitude with unconstrained inertia direction as a sensitivity check.",
        "coordinate_units": "MHW native world units/s; world +Y is up and XZ is planar.",
        "fitted_group_counts": {str(base): sum(trial["fitted_base_speed"] == base for trial in valid) for base in (400, 900)},
        "fit_vector_error": range_summary([trial["fit_vector_error"] for trial in valid]),
        "fit_second_best_margin": range_summary([trial["fit_second_best_margin"] for trial in valid]),
        "unconstrained_direction_base_disagreements": [trial["attempt"] for trial in valid
            if trial["free_direction_fitted_base"] != trial["fitted_base_speed"]],
        "action_groups": action_groups,
        "action_only_majority_errors": sum(group["n"] - max(group["fitted_base_counts"].values()) for group in action_groups),
        "identity_majority_errors": identity_errors,
        "direct_rt_press_velocity_min_vector_error": range_summary([
            trial["direct_rt_press_velocity_min_vector_error"] for trial in valid]),
        "release_timing_counts": {
            "before_entry": sum(trial["rt_release_precedes_entry"] for trial in valid),
            "at_or_after_entry": sum(trial["snapshots"]["rt_release"] is not None and not trial["rt_release_precedes_entry"] for trial in valid),
            "not_found": sum(trial["snapshots"]["rt_release"] is None for trial in valid)},
        "speed_threshold_comparisons": [threshold_comparison(valid, key) for key in predictors],
        "neutral_fitted_examples": [trial["attempt"] for trial in valid if trial["fitted_addition"] == 0],
        "rt_press_age_s": range_summary([trial["snapshots"]["rt_press"]["age_before_entry_s"] for trial in valid if trial["snapshots"]["rt_press"]]),
        "limits": ["All input speeds are observed root-position finite differences, not the game's internal inertial momentum variable.",
            "The 400/900 x 0/300 decomposition is a candidate fit, not directly observed engine fields.",
            "Raw analog stick axes are absent; fitting a 0 or 300 addition does not test the newly user-approved >50% project rule.",
            "When action identity and measured speed are perfectly correlated, this capture cannot determine which one causally selects inertia."],
        "trials": trials,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    flat = [{key: value for key, value in trial.items() if not isinstance(value, (dict, list))} for trial in trials]
    with (args.output / "trials.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(flat[0]))
        writer.writeheader()
        writer.writerows(flat)
    print(json.dumps({key: value for key, value in summary.items() if key not in (
        "trials", "limits", "neutral_fitted_examples", "action_groups", "speed_threshold_comparisons")},
        ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
