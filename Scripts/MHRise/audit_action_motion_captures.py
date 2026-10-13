"""Audit MHRise recorder CSVs for M6 action-ID episodes and frame telemetry.

This is a read-only offline report. It never modifies recorder captures or the
installed game. `player_motion_old_id` is reported as captured; this tool does
not assume it is a source animation frame or infer missing animation progress.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Iterable, TextIO

sys.path.insert(0, str(Path(__file__).resolve().parent))
from recorder_path import recorder_dir


DEFAULT_ACTION_IDS = (193, 195, 196, 199, 206)
LAYER_COUNT = 5
WORLD_TO_CM = 100.0
VECTOR_AXES = ("x", "y", "z")


def parse_action_id(value: object) -> int | None:
    """Parse recorder IDs like ``199`` or ``199.00000000`` without rounding."""
    if value is None or str(value).strip() == "":
        return None
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(numeric) or not numeric.is_integer():
        return None
    return int(numeric)


def parse_number(value: object) -> float | None:
    if value is None or str(value).strip() == "":
        return None
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return None
    return numeric if math.isfinite(numeric) else None


def _new_episode(action_id: int, row: dict[str, str]) -> dict:
    position = {axis: parse_number(row.get(f"world_{axis}")) for axis in VECTOR_AXES}
    return {
        "action_id": action_id,
        "sample_start": row.get("sample_index", ""),
        "sample_end": row.get("sample_index", ""),
        "time_start_s": parse_number(row.get("capture_time_s")),
        "time_end_s": parse_number(row.get("capture_time_s")),
        "next_sample_time_s": None,
        "transition_to_id": None,
        "position_start_world": position,
        "position_end_world": position.copy(),
        "velocity_start_world_s": None,
        "velocity_end_world_s": None,
        "velocity_sum_world_s": {axis: 0.0 for axis in VECTOR_AXES},
        "velocity_magnitude_sum_world_s": 0.0,
        "velocity_sample_count": 0,
        "velocity_time_series_cm_s": [],
        "camera_forward_sum": {axis: 0.0 for axis in VECTOR_AXES},
        "camera_forward_count": 0,
        "camera_forward_start": None,
        "camera_forward_end": None,
        "actor_forward_start": None,
        "actor_forward_end": None,
        "facing_yaw_start_deg": None,
        "facing_yaw_end_deg": None,
        "camera_pitch_start_deg": None,
        "camera_pitch_end_deg": None,
        "camera_pitch_sum_deg": 0.0,
        "camera_pitch_count": 0,
        "stick_sum": {"x": 0.0, "y": 0.0},
        "stick_sample_count": 0,
        "samples": 0,
        "frame_coverage": {f"motion_l{layer}_frame": 0 for layer in range(LAYER_COUNT)},
        "layer_motion_ids": {f"motion_l{layer}_id": set() for layer in range(LAYER_COUNT)},
    }


def _accumulate_episode_row(episode: dict, row: dict[str, str]) -> None:
    episode["sample_end"] = row.get("sample_index", "")
    episode["time_end_s"] = parse_number(row.get("capture_time_s"))
    episode["samples"] += 1
    position = {axis: parse_number(row.get(f"world_{axis}")) for axis in VECTOR_AXES}
    if all(value is not None for value in position.values()):
        episode["position_end_world"] = position

    velocity = {axis: parse_number(row.get(f"velocity_{axis}")) for axis in VECTOR_AXES}
    if all(value is not None for value in velocity.values()):
        if episode["velocity_start_world_s"] is None:
            episode["velocity_start_world_s"] = velocity
        episode["velocity_end_world_s"] = velocity
        for axis in VECTOR_AXES:
            episode["velocity_sum_world_s"][axis] += velocity[axis]
        episode["velocity_magnitude_sum_world_s"] += math.sqrt(sum(value * value for value in velocity.values()))
        episode["velocity_sample_count"] += 1
        sample_time = parse_number(row.get("capture_time_s"))
        episode_start_time = episode["time_start_s"]
        if sample_time is not None and episode_start_time is not None:
            episode["velocity_time_series_cm_s"].append(
                [sample_time - episode_start_time]
                + [velocity[axis] * WORLD_TO_CM for axis in VECTOR_AXES]
            )

    camera_forward = {axis: parse_number(row.get(f"cam_forward_{axis}")) for axis in VECTOR_AXES}
    if all(value is not None for value in camera_forward.values()):
        if episode["camera_forward_start"] is None:
            episode["camera_forward_start"] = camera_forward
        episode["camera_forward_end"] = camera_forward
        for axis in VECTOR_AXES:
            episode["camera_forward_sum"][axis] += camera_forward[axis]
        episode["camera_forward_count"] += 1

    actor_forward = {axis: parse_number(row.get(f"forward_{axis}")) for axis in VECTOR_AXES}
    if all(value is not None for value in actor_forward.values()):
        if episode["actor_forward_start"] is None:
            episode["actor_forward_start"] = actor_forward
        episode["actor_forward_end"] = actor_forward

    facing_yaw = parse_number(row.get("facing_yaw_deg"))
    if facing_yaw is not None:
        if episode["facing_yaw_start_deg"] is None:
            episode["facing_yaw_start_deg"] = facing_yaw
        episode["facing_yaw_end_deg"] = facing_yaw

    camera_pitch = parse_number(row.get("cam_pitch_deg"))
    if camera_pitch is not None:
        if episode["camera_pitch_start_deg"] is None:
            episode["camera_pitch_start_deg"] = camera_pitch
        episode["camera_pitch_end_deg"] = camera_pitch
        episode["camera_pitch_sum_deg"] += camera_pitch
        episode["camera_pitch_count"] += 1
    stick_x = parse_number(row.get("input_gamepad_axis_l_x"))
    stick_y = parse_number(row.get("input_gamepad_axis_l_y"))
    if stick_x is not None and stick_y is not None:
        episode["stick_sum"]["x"] += stick_x
        episode["stick_sum"]["y"] += stick_y
        episode["stick_sample_count"] += 1

    for layer in range(LAYER_COUNT):
        frame_column = f"motion_l{layer}_frame"
        if parse_number(row.get(frame_column)) is not None:
            episode["frame_coverage"][frame_column] += 1
        motion_column = f"motion_l{layer}_id"
        motion_id = parse_action_id(row.get(motion_column))
        if motion_id is not None:
            episode["layer_motion_ids"][motion_column].add(motion_id)


def _finish_episode(episode: dict, next_row: dict[str, str] | None = None) -> dict:
    start = episode.pop("time_start_s")
    end = episode.pop("time_end_s")
    episode["time_start_s"] = start
    episode["time_end_s"] = end
    episode["sampled_span_s"] = None if start is None or end is None else max(0.0, end - start)
    if next_row is not None:
        episode["next_sample_time_s"] = parse_number(next_row.get("capture_time_s"))
        episode["transition_to_id"] = parse_action_id(next_row.get("player_motion_old_id"))
    next_time = episode["next_sample_time_s"]
    episode["interval_until_transition_s"] = (
        None if start is None or next_time is None else max(0.0, next_time - start)
    )

    position_start = episode.pop("position_start_world")
    position_end = episode.pop("position_end_world")
    if all(position_start[axis] is not None and position_end[axis] is not None for axis in VECTOR_AXES):
        delta_world = {axis: position_end[axis] - position_start[axis] for axis in VECTOR_AXES}
        delta_cm = {
            axis: delta_world[axis] * WORLD_TO_CM
            for axis in VECTOR_AXES
        }
        episode["displacement_cm"] = delta_cm
        episode["horizontal_distance_cm"] = math.hypot(delta_cm["x"], delta_cm["z"])
        delta_length = math.sqrt(sum(value * value for value in delta_world.values()))
    else:
        episode["displacement_cm"] = None
        episode["horizontal_distance_cm"] = None
        delta_world = None
        delta_length = 0.0

    velocity_start = episode.pop("velocity_start_world_s")
    velocity_end = episode.pop("velocity_end_world_s")
    velocity_count = episode.pop("velocity_sample_count")
    velocity_sum = episode.pop("velocity_sum_world_s")
    velocity_magnitude_sum = episode.pop("velocity_magnitude_sum_world_s")
    episode["velocity_start_cm_s"] = (
        None if velocity_start is None else {axis: velocity_start[axis] * WORLD_TO_CM for axis in VECTOR_AXES}
    )
    episode["velocity_end_cm_s"] = (
        None if velocity_end is None else {axis: velocity_end[axis] * WORLD_TO_CM for axis in VECTOR_AXES}
    )
    episode["velocity_mean_cm_s"] = (
        None if velocity_count == 0 else {
            axis: velocity_sum[axis] * WORLD_TO_CM / velocity_count for axis in VECTOR_AXES
        }
    )
    episode["speed_mean_cm_s"] = (
        None if velocity_count == 0 else velocity_magnitude_sum * WORLD_TO_CM / velocity_count
    )

    # REFramework reports the velocity on the first row of an old-motion-ID
    # transition as zero in many captures, while displacement begins on the
    # following sample. Keep both fits so the transition sample is visible,
    # but use the post-entry fit for estimating a phase's sustained dynamics.
    velocity_time_series = episode.pop("velocity_time_series_cm_s")

    def fit_velocity(samples: list[list[float]]) -> dict | None:
        if len(samples) < 3:
            return None
        mean_t = sum(sample[0] for sample in samples) / len(samples)
        result = {}
        for axis_index, axis in enumerate(VECTOR_AXES, start=1):
            mean_v = sum(sample[axis_index] for sample in samples) / len(samples)
            sxx = sum((sample[0] - mean_t) ** 2 for sample in samples)
            if sxx <= 1e-12:
                return None
            slope = sum(
                (sample[0] - mean_t) * (sample[axis_index] - mean_v)
                for sample in samples
            ) / sxx
            intercept = mean_v - slope * mean_t
            residual = sum(
                (sample[axis_index] - (intercept + slope * sample[0])) ** 2
                for sample in samples
            )
            total = sum((sample[axis_index] - mean_v) ** 2 for sample in samples)
            r_squared = 1.0 - residual / max(total, 1e-12)
            result[axis] = {
                "velocity_at_episode_start_cm_s": intercept,
                "acceleration_cm_s2": slope,
                "r_squared": r_squared,
            }
        result["sample_count"] = len(samples)
        return result

    episode["velocity_linear_fit_including_entry"] = fit_velocity(velocity_time_series)
    episode["velocity_linear_fit_after_entry"] = fit_velocity(
        velocity_time_series[1:] if len(velocity_time_series) > 1 else []
    )

    camera_forward_count = episode.pop("camera_forward_count")
    camera_forward_sum = episode.pop("camera_forward_sum")
    camera_start = episode.pop("camera_forward_start")
    camera_end = episode.pop("camera_forward_end")

    def direction_error(vector: dict[str, float] | None,
                        forward: dict[str, float] | None) -> float | None:
        if vector is None or forward is None:
            return None
        vector_length = math.sqrt(sum(vector[axis] * vector[axis] for axis in VECTOR_AXES))
        forward_length = math.sqrt(sum(forward[axis] * forward[axis] for axis in VECTOR_AXES))
        if vector_length <= 1e-9 or forward_length <= 1e-9:
            return None
        dot = sum(vector[axis] * forward[axis] for axis in VECTOR_AXES) / (vector_length * forward_length)
        return math.degrees(math.acos(max(-1.0, min(1.0, dot))))

    average_camera_forward = None
    if camera_forward_count > 0:
        average_camera_forward = {
            axis: camera_forward_sum[axis] / camera_forward_count for axis in VECTOR_AXES
        }
    # Alignment of full displacement to camera rays; retain the historical field
    # semantics while adding explicit start/end vectors below.
    episode["camera_start_alignment_error_deg"] = direction_error(delta_world, camera_start)
    episode["camera_mean_alignment_error_deg"] = direction_error(delta_world, average_camera_forward)
    episode["camera_end_alignment_error_deg"] = direction_error(delta_world, camera_end)
    episode["camera_forward_start"] = camera_start
    episode["camera_forward_end"] = camera_end
    episode["camera_forward_mean"] = average_camera_forward
    actor_forward_start = episode["actor_forward_start"]
    actor_forward_end = episode["actor_forward_end"]
    episode["actor_forward_start_alignment_error_deg"] = direction_error(delta_world, actor_forward_start)
    episode["actor_forward_end_alignment_error_deg"] = direction_error(delta_world, actor_forward_end)
    pitch_count = episode.pop("camera_pitch_count")
    pitch_sum = episode.pop("camera_pitch_sum_deg")
    episode["camera_pitch_mean_deg"] = None if pitch_count == 0 else pitch_sum / pitch_count
    stick_count = episode.pop("stick_sample_count")
    stick_sum = episode.pop("stick_sum")
    episode["left_stick_mean"] = None if stick_count == 0 else {
        axis: stick_sum[axis] / stick_count for axis in ("x", "y")
    }

    post_entry_fit = episode["velocity_linear_fit_after_entry"]
    fitted_launch_velocity = None if post_entry_fit is None else {
        axis: post_entry_fit[axis]["velocity_at_episode_start_cm_s"]
        for axis in VECTOR_AXES
    }
    episode["fitted_launch_velocity_cm_s"] = fitted_launch_velocity
    episode["launch_velocity_camera_alignment_error_deg"] = direction_error(
        fitted_launch_velocity, camera_start
    )
    episode["launch_velocity_actor_alignment_error_deg"] = direction_error(
        fitted_launch_velocity, actor_forward_start
    )

    episode["layer_motion_ids"] = {
        name: sorted(values) for name, values in episode["layer_motion_ids"].items() if values
    }
    episode["frame_columns_with_data"] = {
        name: count for name, count in episode["frame_coverage"].items() if count
    }
    episode.pop("frame_coverage")
    return episode


def audit_stream(stream: TextIO, action_ids: Iterable[int]) -> dict:
    """Summarize target-ID episodes from one recorder CSV stream."""
    target_ids = set(action_ids)
    reader = csv.DictReader(stream)
    fieldnames = set(reader.fieldnames or ())
    if "player_motion_old_id" not in fieldnames:
        raise ValueError("CSV has no player_motion_old_id column")

    episodes: list[dict] = []
    total_rows = 0
    frame_rows = Counter()
    frame_field_rows = Counter()
    active: dict | None = None

    def finish_active(next_row: dict[str, str] | None = None) -> None:
        nonlocal active
        if active is not None:
            episodes.append(_finish_episode(active, next_row))
            active = None

    for row in reader:
        total_rows += 1
        for layer in range(LAYER_COUNT):
            frame_column = f"motion_l{layer}_frame"
            frame_rows[frame_column] += 1
            if parse_number(row.get(frame_column)) is not None:
                frame_field_rows[frame_column] += 1

        action_id = parse_action_id(row.get("player_motion_old_id"))
        if action_id not in target_ids:
            finish_active(row)
            continue
        if active is None or active["action_id"] != action_id:
            finish_active(row)
            active = _new_episode(action_id, row)
        _accumulate_episode_row(active, row)

    finish_active()
    return {
        "rows": total_rows,
        "has_motion_layer_frame_columns": all(
            f"motion_l{layer}_frame" in fieldnames for layer in range(LAYER_COUNT)
        ),
        "motion_layer_frame_coverage": {
            name: {
                "rows_with_value": frame_field_rows[name],
                "total_rows": frame_rows[name],
            }
            for name in sorted(frame_rows)
        },
        "target_id_rows": {
            action_id: sum(ep["samples"] for ep in episodes if ep["action_id"] == action_id)
            for action_id in sorted(target_ids)
        },
        "episodes": episodes,
    }


def audit_file(path: Path, action_ids: Iterable[int]) -> dict:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        report = audit_stream(stream, action_ids)
    return {"file": path.name, **report}


def format_report(directory: Path, action_ids: tuple[int, ...], reports: list[dict]) -> str:
    lines = [
        "MHRise action-motion capture audit (read-only)",
        f"Directory: {directory}",
        f"Target player_motion_old_id values: {', '.join(map(str, action_ids))}",
        f"Capture files: {len(reports)}",
        "",
    ]
    if not reports:
        lines.append("No *_samples.csv files found.")
        return "\n".join(lines)

    totals = {action_id: sum(item["target_id_rows"][action_id] for item in reports) for action_id in action_ids}
    lines.append("Target ID row totals:")
    for action_id in action_ids:
        lines.append(f"  {action_id}: {totals[action_id]} rows")
    lines.append("")
    lines.append("Target episodes:")
    found_episode = False
    for report in reports:
        for episode in report["episodes"]:
            found_episode = True
            start = episode["time_start_s"]
            end = episode["time_end_s"]
            time_text = "unknown" if start is None or end is None else f"{start:.3f}..{end:.3f}s"
            transition_duration = episode["interval_until_transition_s"]
            if transition_duration is not None:
                time_text += f"; to next ID {transition_duration:.3f}s"
            frame_columns = episode["frame_columns_with_data"]
            frame_text = ", ".join(f"{name}={count}/{episode['samples']}" for name, count in frame_columns.items())
            if not frame_text:
                frame_text = "no motion_l*_frame values"
            layer_ids = episode["layer_motion_ids"]
            layer_text = ", ".join(f"{name}={values}" for name, values in layer_ids.items()) or "no layer IDs"
            displacement = episode["displacement_cm"]
            if displacement is None:
                displacement_text = "Δposition unavailable"
            else:
                displacement_text = (
                    "Δcm=(" + ",".join(f"{displacement[axis]:.1f}" for axis in VECTOR_AXES) + ")"
                )
            mean_velocity = episode["velocity_mean_cm_s"]
            if mean_velocity is not None:
                velocity_text = "meanVcm/s=(" + ",".join(
                    f"{mean_velocity[axis]:.1f}" for axis in VECTOR_AXES
                ) + ")"
            else:
                velocity_text = "mean velocity unavailable"
            mean_speed = episode["speed_mean_cm_s"]
            speed_text = "" if mean_speed is None else f"meanSpeed={mean_speed:.0f}cm/s"
            alignment = episode["camera_start_alignment_error_deg"]
            alignment_text = "" if alignment is None else f"cameraStartΔ={alignment:.1f}°"
            after_entry_fit = episode["velocity_linear_fit_after_entry"]
            fit_text = ""
            if after_entry_fit is not None and episode["action_id"] in (199, 206):
                vertical_fit = after_entry_fit["y"]
                launch_camera_error = episode["launch_velocity_camera_alignment_error_deg"]
                launch_alignment_text = (
                    "" if launch_camera_error is None
                    else f" launch↔cam={launch_camera_error:.1f}°"
                )
                fit_text = (
                    f" vy0≈{vertical_fit['velocity_at_episode_start_cm_s']:.0f}cm/s"
                    f" ay≈{vertical_fit['acceleration_cm_s2']:.0f}cm/s²"
                    f" R²={vertical_fit['r_squared']:.3f}{launch_alignment_text}"
                )
            lines.append(
                f"  {report['file']} | id={episode['action_id']} | {time_text} | "
                f"samples={episode['samples']} | {displacement_text} | {velocity_text} | "
                f"{speed_text} {alignment_text}{fit_text} | layer-frame fields: {frame_text} | {layer_text}"
            )
    if not found_episode:
        lines.append("  None in the scanned captures.")

    errors = [f"{report['file']}: {report['error']}" for report in reports if report.get("error")]
    if errors:
        lines.extend(("", "Input errors:", *(f"  {error}" for error in errors)))

    lines.append("")
    lines.append("Motion-layer frame-field coverage:")
    for report in reports:
        populated = [
            f"{column} {counts['rows_with_value']}/{counts['total_rows']}"
            for column, counts in report["motion_layer_frame_coverage"].items()
            if counts["rows_with_value"]
        ]
        if populated:
            lines.append(f"  {report['file']}: " + ", ".join(populated))
        else:
            lines.append(f"  {report['file']}: all motion_l*_frame columns empty")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--directory", type=Path, default=recorder_dir(),
        help="Recorder output directory (default: installed MHRise recorder path)",
    )
    parser.add_argument(
        "--ids", type=int, nargs="+", default=list(DEFAULT_ACTION_IDS),
        help="player_motion_old_id values to locate (default: M6.2 action set)",
    )
    parser.add_argument(
        "--json-out", type=Path,
        help="Optional path for a machine-readable JSON copy; no file is written by default",
    )
    args = parser.parse_args()
    directory = args.directory.expanduser().resolve()
    files = sorted(directory.glob("*_samples.csv")) if directory.is_dir() else []
    reports = []
    for path in files:
        try:
            reports.append(audit_file(path, args.ids))
        except (OSError, UnicodeError, csv.Error, ValueError) as error:
            reports.append({
                "file": path.name,
                "error": str(error),
                "target_id_rows": {i: 0 for i in args.ids},
                "episodes": [],
                "motion_layer_frame_coverage": {},
            })

    print(format_report(directory, tuple(args.ids), reports))
    if args.json_out is not None:
        output = args.json_out.expanduser()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps({"directory": str(directory), "action_ids": args.ids, "captures": reports}, indent=2), encoding="utf-8")
        print(f"JSON report: {output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
