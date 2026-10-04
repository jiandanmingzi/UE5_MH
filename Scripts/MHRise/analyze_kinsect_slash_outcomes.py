#!/usr/bin/env python3
"""Inspect Kinsect Slash hit/miss captures without changing runtime assets."""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import statistics
from collections import Counter
from pathlib import Path

from recorder_path import recorder_dir

LAYER_FIELD_COUNT = 5 * 7
PROJECT = Path(__file__).resolve().parents[2]
CURVE_ROOT = PROJECT / "Saved" / "_mhr_curves"
REPORT_ROOT = PROJECT / "Saved" / "Analysis" / "KinsectSlashOutcomes"
SUPPLEMENT_BEGIN = "<!-- BEGIN KINSECT_SLASH_OUTCOMES_20260928 -->"
SUPPLEMENT_END = "<!-- END KINSECT_SLASH_OUTCOMES_20260928 -->"
DASH_SUPPLEMENT_BEGIN = "<!-- BEGIN KINSECT_SLASH_DASH_20260929 -->"
DASH_SUPPLEMENT_END = "<!-- END KINSECT_SLASH_DASH_20260929 -->"
MHW_TURN_TRUTH_BEGIN = "<!-- BEGIN MHW_AERIAL_DIVE_TURN_TRUTH_20261003 -->"
MHW_TURN_TRUTH_END = "<!-- END MHW_AERIAL_DIVE_TURN_TRUTH_20261003 -->"


def preserved_supplement(document: Path) -> str:
    """Keep scoped new-device evidence when legacy full-history tables rebuild."""
    if not document.is_file():
        return ""
    text = document.read_text(encoding="utf-8")
    blocks = []
    for begin_marker, end_marker in (
        (SUPPLEMENT_BEGIN, SUPPLEMENT_END),
        (DASH_SUPPLEMENT_BEGIN, DASH_SUPPLEMENT_END),
    ):
        if begin_marker not in text:
            continue
        start = text.index(begin_marker)
        # Fail rather than silently erase an incomplete evidence block.
        end = text.index(end_marker, start) + len(end_marker)
        blocks.append((start, text[start:end]))
    return "\n\n".join(block for _, block in sorted(blocks))


def preserved_tail_supplement(document: Path) -> str:
    """Keep non-Rise truth records at the end of the generated Rise table."""
    if not document.is_file():
        return ""
    text = document.read_text(encoding="utf-8")
    if MHW_TURN_TRUTH_BEGIN not in text:
        return ""
    start = text.index(MHW_TURN_TRUTH_BEGIN)
    # Fail rather than silently erase an incomplete evidence block.
    end = text.index(MHW_TURN_TRUTH_END, start) + len(MHW_TURN_TRUTH_END)
    return text[start:end]


def summarize(reports: list[dict]) -> dict:
    runs = [run for report in reports for run in report["runs"]]
    dance = [run for run in runs if run["id"] == 189]
    natural = [run for run in dance if run["next_id"] == 157]
    misses = [run for run in runs if run["id"] == 188]

    def values_summary(values: list[float]) -> dict | None:
        if not values:
            return None
        return {"median": statistics.median(values), "min": min(values), "max": max(values)}

    return {
        "sources": [Path(report["source"]).name for report in reports],
        "raw_samples": sum(report["sample_count"] for report in reports),
        "dance_runs": len(dance), "natural_189_to_157_runs": len(natural),
        "dance_predecessors": dict(Counter(run["prev_id"] for run in dance)),
        "dance_successors": dict(Counter(run["next_id"] for run in dance)),
        "miss_runs": len(misses), "miss_successors": dict(Counter(run["next_id"] for run in misses)),
        "first_positive_height_s": values_summary([run["rise_time_s"] for run in dance
                                                    if run["rise_time_s"] is not None]),
        "launch_model": {field: values_summary([run["flight_model"][field] for run in dance
                                                 if run["flight_model"]])
                         for field in ("launch_zero_height_time_s", "launch_vertical_speed_cm_s",
                                       "launch_forward_speed_cm_s", "apex_since_launch_s")},
        "dance_up_acceleration_cm_s2": values_summary([run["up_fit"]["acceleration_cm_s2"]
                                                        for run in dance if run["up_fit"]]),
        "dance_forward_acceleration_cm_s2": values_summary([run["forward_fit"]["acceleration_cm_s2"]
                                                             for run in dance if run["forward_fit"]]),
        "natural_189_occupancy_s": values_summary([run["occupancy_s"] for run in natural]),
        "long_miss_fits": [{"recording": Path(report["source"]).name, "run_index": run["index"],
                            "up_acceleration_cm_s2": run["up_fit"]["acceleration_cm_s2"],
                            "up_r2": run["up_fit"]["r2"],
                            "forward_acceleration_cm_s2": run["forward_fit"]["acceleration_cm_s2"]}
                           for report in reports for run in report["runs"]
                           if run["id"] == 188 and run["next_id"] == 182 and run["up_fit"]
                           and run["span_s"] > 0.3],
    }


def fit_polynomial(points: list[tuple[float, float]], degree: int = 2) -> dict | None:
    """Least-squares position fit, avoiding noisy second differences."""
    if len(points) < max(8, degree + 2):
        return None
    order = degree + 1
    matrix = [[sum(t ** (i + j) for t, _ in points) for j in range(order)]
              + [sum(y * t ** i for t, y in points)] for i in range(order)]
    for column in range(order):
        pivot = max(range(column, order), key=lambda index: abs(matrix[index][column]))
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        scale = matrix[column][column]
        if abs(scale) < 1e-12:
            return None
        matrix[column] = [value / scale for value in matrix[column]]
        for row in range(order):
            if row != column:
                scale = matrix[row][column]
                matrix[row] = [left - scale * right
                               for left, right in zip(matrix[row], matrix[column])]
    coefficients = [row[-1] for row in matrix]
    residuals = [y - sum(c * t ** power for power, c in enumerate(coefficients))
                 for t, y in points]
    mean_y = statistics.mean(y for _, y in points)
    variance = sum((y - mean_y) ** 2 for _, y in points)
    return {"coefficients": coefficients,
            "acceleration_cm_s2": 2 * coefficients[2] if degree == 2 else None,
            "r2": 1 - sum(error * error for error in residuals) / max(variance, 1e-12),
            "rmse_cm": math.sqrt(statistics.mean(error * error for error in residuals)),
            "samples": len(points), "from_s": points[0][0], "to_s": points[-1][0]}


def local_curve(chunk: list[dict], anchor: dict | None = None) -> list[dict]:
    anchor = anchor or chunk[0]
    fx, fz = number(anchor, "forward_x"), number(anchor, "forward_z")
    norm = math.hypot(fx, fz)
    assert norm > 1e-6
    fx, fz = fx / norm, fz / norm
    origin = [number(anchor, f"world_{axis}") for axis in "xyz"]
    time0 = number(chunk[0], "capture_time_s")
    curve = []
    for index, row in enumerate(chunk):
        x, y, z = [(number(row, f"world_{axis}") - origin[i]) * 100
                   for i, axis in enumerate("xyz")]
        vx, vy, vz = [number(row, f"velocity_{axis}") * 100 for axis in "xyz"]
        curve.append({"sample": index, "capture_time_s": number(row, "capture_time_s"),
                      "time_s": number(row, "capture_time_s") - time0,
                      "sample_time_120hz_s": index / 120,
                      "forward_cm": x * fx + z * fz, "right_cm": x * fz - z * fx,
                      "up_cm": y, "world_dx_cm": x, "world_dy_cm": y,
                      "world_dz_cm": z, "forward_speed_cm_s": vx * fx + vz * fz,
                      "right_speed_cm_s": vx * fz - vz * fx, "up_speed_cm_s": vy,
                      "horizontal_speed_cm_s": math.hypot(vx, vz),
                      "left_stick_magnitude": math.hypot(number(row, "input_gamepad_axis_l_x"),
                                                           number(row, "input_gamepad_axis_l_y")),
                      "motion_l0_id": row.get("motion_l0_id"),
                      "motion_l0_frame": row.get("motion_l0_frame"),
                      "player_motion_old_id": motion(row)})
    return curve


def load_rows(path: Path) -> tuple[list[dict], list[str], dict]:
    """Recover the fixed layer tail of the recorder's sparse HID-edge rows.

    Lua csv_bool_number(nil) returned nil; table.insert omitted unset HID
    edge cells. First 43 transform/axis/old-id fields and last 35 layer fields
    are fixed, so only those are recoverable. Do not invent button values.
    """
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        fields = next(reader)
        base_count = fields.index("player_motion_old_id") + 1
        layer_start = fields.index("motion_l0_bank_id")
        assert len(fields) - layer_start == LAYER_FIELD_COUNT
        rows = []
        malformed = 0
        for values in reader:
            if len(values) == len(fields):
                rows.append(dict(zip(fields, values)))
            else:
                assert base_count + LAYER_FIELD_COUNT <= len(values) < len(fields)
                malformed += 1
                row = dict.fromkeys(fields, "")
                row.update(zip(fields[:base_count], values[:base_count]))
                row.update(zip(fields[-LAYER_FIELD_COUNT:], values[-LAYER_FIELD_COUNT:]))
                rows.append(row)
    return rows, fields, {"short_rows": malformed, "total_rows": len(rows),
                          "button_edge_cells_recoverable": malformed == 0,
                          "fixed_transform_and_motion_tail_recovered": True}


def number(row: dict, key: str, default: float = 0.0) -> float:
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return default


def motion(row: dict) -> int:
    return int(number(row, "player_motion_old_id", -1))


def horizontal_speed(row: dict) -> float:
    return math.hypot(number(row, "velocity_x"), number(row, "velocity_z")) * 100


def inspect_pie(directory: Path) -> dict:
    """Census actual movement settings, using one named CMC phase per frame."""
    with (directory / "Animation" / "Playback.csv").open(encoding="utf-8-sig", newline="") as stream:
        playback = {row["Frame"]: row for row in csv.DictReader(stream)}
    falling = []
    with (directory / "Character" / "MovementPhases.csv").open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            pose = playback.get(row["Frame"], {})
            if (row["Phase"] == "CMC.PerformMovement.Post"
                    and row["MovementMode"] == "3" and "KinsectSlash" in row["ActiveActions"]
                    and "KinsectSlash" in pose.get("ActiveMontage", "")
                    and "Dance" not in pose.get("ActiveMontage", "")
                    and number(pose, "ActiveMontageTime") > 0.965):
                falling.append(row)
    dance = [row for row in playback.values() if "KinsectSlash_Dance" in row["ActiveMontage"]]
    sources = []
    with (directory / "Character" / "RootMotionSources.csv").open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            if "JumpForce" in row["SourceType"] and "KinsectSlash" in playback.get(row["Frame"], {}).get("ActiveActions", ""):
                sources.append(row)
    return {"source": str(directory), "miss_post_phase_samples": len(falling),
            "miss_unique_frames": len({row["Frame"] for row in falling}),
            "miss_runtime_profiles": dict(Counter(
                f"gravity={row['GravityScale']}, braking={row['BrakingDecelerationFalling']}"
                for row in falling)),
            "miss_first_example": {key: falling[0][key] for key in
                                   ("WorldTimeSeconds", "Frame", "Phase", "VelocityZ", "GravityScale",
                                    "BrakingDecelerationFalling")} if falling else None,
            "dance_montage_lengths_s": sorted({number(row, "ActiveMontageLength") for row in dance}),
            "dance_jump_source_durations_s": sorted({number(row, "Duration") for row in sources})}


def inspect(path: Path) -> dict:
    rows, fields, schema = load_rows(path)
    runs = [(key, list(group)) for key, group in itertools.groupby(rows, motion)]
    details = []
    for index, (key, chunk) in enumerate(runs):
        prev = runs[index - 1][0] if index else None
        if key not in (186, 187, 188, 189) and not (key == 157 and prev == 189):
            continue
        first, last = chunk[0], chunk[-1]
        t0 = number(first, "capture_time_s")
        p0 = [number(first, f"world_{axis}") for axis in "xyz"]
        layer_keys = [name for name in fields if name.startswith("motion_l0_")]
        sample_indices = sorted(set([0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60,
                                     90, 120, len(chunk) - 1]))
        rise = next((i for i, row in enumerate(chunk)
                     if (number(row, "world_y") - p0[1]) * 100 > 0.05), None)
        ys = [(number(row, "world_y") - p0[1]) * 100 for row in chunk]
        peak = max(range(len(ys)), key=ys.__getitem__)
        curve = local_curve(chunk)
        fit_start = number(chunk[rise], "capture_time_s") - t0 + 0.1 if key == 189 and rise is not None else 0.05
        fit_rows = [point for point in curve[1:-3] if point["time_s"] >= fit_start
                    and number(chunk[point["sample"]], "world_y") * 100 > 60]
        up_fit = fit_polynomial([(point["time_s"], point["up_cm"]) for point in fit_rows])
        forward_fit = fit_polynomial([(point["time_s"], point["forward_cm"]) for point in fit_rows])
        flight_model = None
        if key == 189 and up_fit and forward_fit:
            intercept, velocity, quadratic = up_fit["coefficients"]
            discriminant = velocity * velocity - 4 * quadratic * intercept
            if discriminant > 0 and quadratic < 0:
                roots = sorted(((-velocity + math.sqrt(discriminant)) / (2 * quadratic),
                                (-velocity - math.sqrt(discriminant)) / (2 * quadratic)))
                launch_time = roots[0]
                f0, fv, fq = forward_fit["coefficients"]
                flight_model = {"launch_zero_height_time_s": launch_time,
                                "launch_vertical_speed_cm_s": velocity + 2 * quadratic * launch_time,
                                "apex_since_launch_s": -velocity / (2 * quadratic) - launch_time,
                                "launch_forward_speed_cm_s": fv + 2 * fq * launch_time,
                                "launch_forward_offset_cm": f0 + fv * launch_time + fq * launch_time ** 2}
        combined_fit = None
        if key == 189 and index + 1 < len(runs) and runs[index + 1][0] == 157:
            continued_rows = chunk + runs[index + 1][1]
            combined = local_curve(continued_rows)
            fit_combined = [point for point in combined[1:-3] if point["time_s"] >= fit_start
                            and number(continued_rows[point["sample"]], "world_y") * 100 > 60]
            combined_fit = {"up": fit_polynomial([(point["time_s"], point["up_cm"]) for point in fit_combined]),
                            "forward": fit_polynomial([(point["time_s"], point["forward_cm"]) for point in fit_combined])}
        freeze_intervals = []
        freeze_start = None
        for i in range(1, min(rise or len(curve), len(curve))):
            stationary = max(abs(curve[i][field] - curve[i - 1][field])
                             for field in ("world_dx_cm", "world_dy_cm", "world_dz_cm")) < 1e-4
            if stationary and freeze_start is None:
                freeze_start = i - 1
            if not stationary and freeze_start is not None:
                freeze_intervals.append({"from_s": curve[freeze_start]["time_s"],
                                         "to_s": curve[i - 1]["time_s"],
                                         "duration_s": curve[i - 1]["time_s"] - curve[freeze_start]["time_s"]})
                freeze_start = None
        if freeze_start is not None:
            end = (rise or len(curve)) - 1
            freeze_intervals.append({"from_s": curve[freeze_start]["time_s"],
                                     "to_s": curve[end]["time_s"],
                                     "duration_s": curve[end]["time_s"] - curve[freeze_start]["time_s"]})
        details.append({
            "index": index, "id": key, "samples": len(chunk),
            "prev_id": prev,
            "next_id": runs[index + 1][0] if index + 1 < len(runs) else None,
            "start_s": t0,
            "span_s": number(last, "capture_time_s") - t0,
            "occupancy_s": number(runs[index + 1][1][0], "capture_time_s") - t0
            if index + 1 < len(runs) else None,
            "chain": [runs[j][0] for j in range(max(0, index - 2),
                                                min(len(runs), index + 4))],
            "rise_index": rise,
            "rise_time_s": number(chunk[rise], "capture_time_s") - t0 if rise is not None else None,
            "peak_y_cm": ys[peak],
            "peak_time_s": number(chunk[peak], "capture_time_s") - t0,
            "end_y_cm": ys[-1],
            "end_vh_cm_s": horizontal_speed(last),
            "up_fit": up_fit, "forward_fit": forward_fit,
            "flight_model": flight_model,
            "combined_189_157_fit": combined_fit,
            "stationary_intervals_before_rise": freeze_intervals,
            "max_gap_s": max(number(b, "capture_time_s") - number(a, "capture_time_s")
                             for a, b in zip(chunk, chunk[1:])) if len(chunk) > 1 else 0,
            "left_stick_active_fraction": sum(point["left_stick_magnitude"] > 0.15 for point in curve) / len(curve),
            "end_forward_cm": curve[-1]["forward_cm"],
            "curve": curve,
            "layer_start": {name: first[name] for name in layer_keys},
            "layer_end": {name: last[name] for name in layer_keys},
            "layer0_matches_action": all(int(number(row, "motion_l0_id", -1)) == key for row in chunk),
            "key_samples": [{
                "i": i, "t": number(chunk[i], "capture_time_s") - t0,
                "h_cm": math.hypot(number(chunk[i], "world_x") - p0[0],
                                    number(chunk[i], "world_z") - p0[2]) * 100,
                "y_cm": (number(chunk[i], "world_y") - p0[1]) * 100,
                "vh": horizontal_speed(chunk[i]),
                "vy": number(chunk[i], "velocity_y") * 100,
                "anim_frame": chunk[i].get("motion_l0_frame"),
            } for i in sample_indices if i < len(chunk)],
        })
    return {"source": str(path), "sample_count": len(rows), "fields": fields,
            "schema": schema,
            "field_populated": {name: sum(row.get(name) not in (None, "") for row in rows)
                                for name in ("motion_l0_frame", "input_gamepad_axis_l_x",
                                             "motion_l0_id", "input_gamepad_button_RRight_down")},
            "sample_hz": 1 / statistics.median(number(row, "delta_time_s") for row in rows[1:]),
            "motion_counts": {str(key): sum(len(chunk) for other, chunk in runs if other == key)
                              for key in sorted(set(key for key, _ in runs))},
            "runs": details}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", type=Path)
    parser.add_argument("--latest", type=int, default=2)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--summary", action="store_true", help="Print aggregate results, not every observed run")
    parser.add_argument("--pie", type=Path, help="Optional RuntimeTelemetry directory; read-only runtime profile audit")
    parser.add_argument("--write", action="store_true", help="Export all observed curves and a provenance report")
    args = parser.parse_args()
    captures = args.captures or sorted(recorder_dir().glob("*_samples.csv"),
                                      key=lambda path: path.stat().st_mtime,
                                      reverse=True)[:args.latest]
    reports = [inspect(path) for path in captures]
    if not reports:
        parser.error("No sample captures found; supply paths or check MHGZ_RISE_RECORDER_DIR")
    summary = summarize(reports)
    if args.pie:
        summary["pie"] = inspect_pie(args.pie)
    if args.write:
        CURVE_ROOT.mkdir(parents=True, exist_ok=True)
        REPORT_ROOT.mkdir(parents=True, exist_ok=True)
        latest_stamp = max(path.name.removeprefix("mhrise_").removesuffix("_samples.csv") for path in captures)
        report_path = REPORT_ROOT / f"{latest_stamp}.json"
        report_path.write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding="utf-8")
        (REPORT_ROOT / f"{latest_stamp}_summary.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        for key, name in ((189, "操虫斩_189"), (188, "操虫斩_188"), (157, "操虫斩_189后157")):
            target = CURVE_ROOT / f"{name}_{latest_stamp}.csv"
            matching = [(report, run) for report in reports for run in report["runs"] if run["id"] == key]
            if not matching:
                continue
            fields = ["recording", "run_index", "prev_id", "next_id"] + list(matching[0][1]["curve"][0])
            with target.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                for report, run in matching:
                    for point in run["curve"]:
                        writer.writerow({"recording": Path(report["source"]).name,
                                         "run_index": run["index"], "prev_id": run["prev_id"],
                                         "next_id": run["next_id"], **point})
            print("export", target, "runs", len(matching))
        print("report", report_path)
    if args.verbose:
        print(json.dumps(reports, ensure_ascii=False, indent=2))
        return
    if args.summary or args.pie:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return
    for report in reports:
        print(Path(report["source"]).name, report["sample_count"],
              "Hz", round(report["sample_hz"], 2), "populated", report["field_populated"])
        for run in report["runs"]:
            if run["id"] in (186, 187):
                continue
            print("run", run["index"], "id", run["id"], "t", round(run["start_s"], 3),
                  "samples", run["samples"], "duration", round(run["occupancy_s"] or 0, 4),
                  "chain", run["chain"], "rise", run["rise_index"],
                  None if run["rise_time_s"] is None else round(run["rise_time_s"], 4),
                  "peak", round(run["peak_y_cm"], 2), round(run["peak_time_s"], 4),
                  "end", round(run["end_y_cm"], 2), round(run["end_vh_cm_s"], 2))
            print("  g/forward-a", [(field, None if run[field] is None else
                  (round(run[field]["acceleration_cm_s2"], 2), round(run[field]["r2"], 6),
                   round(run[field]["rmse_cm"], 3))) for field in ("up_fit", "forward_fit")],
                  "forward-end", round(run["end_forward_cm"], 2),
                  "freeze", [(round(x["from_s"], 3), round(x["duration_s"], 3))
                             for x in run["stationary_intervals_before_rise"] if x["duration_s"] > 0.02])


if __name__ == "__main__":
    main()
