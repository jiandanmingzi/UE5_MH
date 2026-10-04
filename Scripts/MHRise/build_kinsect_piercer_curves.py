#!/usr/bin/env python3
"""Export an auditable intermediate trajectory dataset for MHRise motion 201.

The output is deliberately per-attempt rather than a single averaged curve:
201 captures have different aim pitches and end times, so averaging their world
positions would destroy the input-dependent trajectory shape. A launch-local
orthonormal basis removes yaw while retaining pitch/gravity behavior.

This produces CSV analysis sources, not Unreal Curve assets or final gameplay
parameters. Suspected external pushes remain in the all-samples file and are
excluded only from the clean-candidate file; the raw game capture is untouched.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

from recorder_path import recorder_dir


PROJECT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = PROJECT / "Saved" / "_mhr_curves"
TARGET_ID = "201"
PREVIOUS_ID = "200"
NEXT_ID = "203"

# The current capture has a clear gap: clean trials peak at 6.6 cm / 812 cm/s,
# while the three strong trajectory-outlier candidates begin at 57.7 cm and
# 1,831 cm/s. Keep these as explicit, auditable screening thresholds.
MAX_CROSS_TRACK_CM = 25.0
MAX_CROSS_TRACK_SPEED_CM_S = 1000.0
MAX_SAMPLE_GAP_S = 0.03

CURVE_FIELDS = [
    "capture_id", "attempt_index", "motion_run_index", "frame_index",
    "capture_time_s", "elapsed_s", "elapsed_norm", "duration_s",
    "duration_frames", "previous_motion_id", "next_motion_id",
    "camera_pitch_deg", "launch_x", "launch_y", "launch_z",
    "position_x_cm", "position_y_cm", "position_z_cm",
    "forward_cm", "right_cm", "up_cm", "world_up_cm",
    "speed_cm_s", "forward_speed_cm_s", "right_speed_cm_s", "up_speed_cm_s",
    "max_cross_track_cm", "max_cross_track_speed_cm_s", "quality",
    "include_in_clean_curve",
]

AUDIT_FIELDS = [
    "capture_id", "attempt_index", "motion_run_index", "previous_motion_id",
    "next_motion_id", "sample_count", "duration_frames", "duration_s",
    "camera_pitch_median_deg", "max_cross_track_cm",
    "max_cross_track_speed_cm_s", "max_yaw_step_deg", "max_sample_gap_s",
    "quality", "include_in_clean_curve", "reason",
]


def number(row: dict[str, str], key: str) -> float:
    return float(row[key])


def motion_id(row: dict[str, str]) -> str:
    try:
        return str(int(float(row.get("player_motion_old_id") or "")))
    except (TypeError, ValueError):
        return ""


def vector(row: dict[str, str], prefix: str) -> tuple[float, float, float]:
    return tuple(number(row, f"{prefix}_{axis}") for axis in "xyz")


def dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def magnitude(v: tuple[float, float, float]) -> float:
    return math.sqrt(dot(v, v))


def normalize(v: tuple[float, float, float]) -> tuple[float, float, float]:
    length = magnitude(v)
    if length < 1e-9:
        raise ValueError("Cannot normalize a zero-length launch vector")
    return tuple(x / length for x in v)


def basis_from_launch(v: tuple[float, float, float]):
    """Return orthonormal (forward, right, up) in the game's X/Y-up/Z axes."""
    forward = normalize(v)
    horizontal = math.hypot(forward[0], forward[2])
    if horizontal < 1e-6:
        raise ValueError("Launch direction is too close to vertical for a stable yaw basis")
    right = (forward[2] / horizontal, 0.0, -forward[0] / horizontal)
    # With game axes X/Z horizontal and +Y up, forward × right points upward.
    up = (
        forward[1] * right[2] - forward[2] * right[1],
        forward[2] * right[0] - forward[0] * right[2],
        forward[0] * right[1] - forward[1] * right[0],
    )
    return forward, right, normalize(up)


def contiguous_runs(rows: list[dict[str, str]]):
    runs: list[tuple[str, list[dict[str, str]]]] = []
    for row in rows:
        key = motion_id(row)
        if not runs or runs[-1][0] != key:
            runs.append((key, [row]))
        else:
            runs[-1][1].append(row)
    return runs


def launch_basis(chunk: list[dict[str, str]]):
    # Skip frame zero (which can still carry the prior action's inherited
    # velocity) and average the first five recorded velocity vectors.
    source = chunk[1:6] or chunk[:1]
    v = tuple(statistics.mean(vector(r, "velocity")[axis] for r in source)
              for axis in range(3))
    return basis_from_launch(v)


def quality_metrics(chunk: list[dict[str, str]], right) -> dict[str, float]:
    p0 = vector(chunk[0], "world")
    cross_track = []
    cross_track_speed = []
    yaw = []
    for row in chunk:
        p = vector(row, "world")
        delta_cm = tuple((p[i] - p0[i]) * 100.0 for i in range(3))
        cross_track.append(dot(delta_cm, right))
        v = vector(row, "velocity")
        cross_track_speed.append(dot(tuple(x * 100.0 for x in v), right))
        if math.hypot(v[0], v[2]) > 1e-6:
            yaw.append(math.degrees(math.atan2(v[0], v[2])))
    gaps = [number(b, "capture_time_s") - number(a, "capture_time_s")
            for a, b in zip(chunk, chunk[1:])]
    yaw_steps = [abs((b - a + 180.0) % 360.0 - 180.0) for a, b in zip(yaw, yaw[1:])]
    return {
        "max_cross_track_cm": max(map(abs, cross_track), default=0.0),
        "max_cross_track_speed_cm_s": max(map(abs, cross_track_speed), default=0.0),
        "max_yaw_step_deg": max(yaw_steps, default=0.0),
        "max_sample_gap_s": max(gaps, default=0.0),
    }


def classify(metrics: dict[str, float]) -> tuple[str, bool, str]:
    reasons = []
    if metrics["max_cross_track_cm"] > MAX_CROSS_TRACK_CM:
        reasons.append(f"cross-track offset > {MAX_CROSS_TRACK_CM:g} cm")
    if metrics["max_cross_track_speed_cm_s"] > MAX_CROSS_TRACK_SPEED_CM_S:
        reasons.append(f"cross-track speed > {MAX_CROSS_TRACK_SPEED_CM_S:g} cm/s")
    if metrics["max_sample_gap_s"] > MAX_SAMPLE_GAP_S:
        reasons.append(f"sample gap > {MAX_SAMPLE_GAP_S:g} s")
    if metrics["max_cross_track_cm"] > MAX_CROSS_TRACK_CM or \
            metrics["max_cross_track_speed_cm_s"] > MAX_CROSS_TRACK_SPEED_CM_S:
        return "suspected_external_push", False, "; ".join(reasons)
    if metrics["max_sample_gap_s"] > MAX_SAMPLE_GAP_S:
        return "recording_hitch", False, "; ".join(reasons)
    return "clean_candidate", True, "within current trajectory and sampling thresholds"


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {"player_motion_old_id", "capture_time_s", "world_x", "world_y",
                    "world_z", "velocity_x", "velocity_y", "velocity_z"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"Capture missing required columns: {', '.join(sorted(missing))}")
        return list(reader)


def build_outputs(capture: Path, output_dir: Path) -> tuple[Path, Path, Path, Path, dict]:
    rows = read_rows(capture)
    runs = contiguous_runs(rows)
    segments = [(index, chunk) for index, (key, chunk) in enumerate(runs)
                if key == TARGET_ID and len(chunk) >= 3]
    if not segments:
        raise ValueError(f"No usable motion {TARGET_ID} segments in {capture}")

    capture_id = capture.stem.removesuffix("_samples")
    output_dir.mkdir(parents=True, exist_ok=True)
    all_path = output_dir / f"强化操虫穿刺_201_中间曲线_全样本_{capture_id}.csv"
    clean_path = output_dir / f"强化操虫穿刺_201_中间曲线_纯样本_{capture_id}.csv"
    audit_path = output_dir / f"强化操虫穿刺_201_样本审计_{capture_id}.csv"
    report_path = output_dir / f"强化操虫穿刺_201_中间曲线说明_{capture_id}.md"
    for path in (all_path, clean_path, audit_path, report_path):
        if path.exists():
            raise FileExistsError(f"Refusing to overwrite existing output: {path}")

    all_curve_rows = []
    clean_curve_rows = []
    audit_rows = []
    for attempt_index, (run_index, chunk) in enumerate(segments, start=1):
        forward, right, up = launch_basis(chunk)
        metrics = quality_metrics(chunk, right)
        quality, include, reason = classify(metrics)
        duration = number(chunk[-1], "capture_time_s") - number(chunk[0], "capture_time_s")
        duration_frames = len(chunk) - 1
        previous_id = runs[run_index - 1][0] if run_index > 0 else ""
        next_id = runs[run_index + 1][0] if run_index + 1 < len(runs) else ""
        pitches = [float(r["cam_pitch_deg"]) for r in chunk if r.get("cam_pitch_deg")]
        pitch = statistics.median(pitches) if pitches else ""
        p0 = vector(chunk[0], "world")

        audit_rows.append({
            "capture_id": capture_id,
            "attempt_index": attempt_index,
            "motion_run_index": run_index,
            "previous_motion_id": previous_id,
            "next_motion_id": next_id,
            "sample_count": len(chunk),
            "duration_frames": duration_frames,
            "duration_s": f"{duration:.6f}",
            "camera_pitch_median_deg": f"{pitch:.3f}" if pitch != "" else "",
            **{key: f"{metrics[key]:.3f}" for key in metrics},
            "quality": quality,
            "include_in_clean_curve": int(include),
            "reason": reason,
        })

        for frame_index, row in enumerate(chunk):
            p = vector(row, "world")
            v = vector(row, "velocity")
            delta_cm = tuple((p[i] - p0[i]) * 100.0 for i in range(3))
            velocity_cm_s = tuple(x * 100.0 for x in v)
            elapsed = number(row, "capture_time_s") - number(chunk[0], "capture_time_s")
            curve_row = {
                "capture_id": capture_id,
                "attempt_index": attempt_index,
                "motion_run_index": run_index,
                "frame_index": frame_index,
                "capture_time_s": f"{number(row, 'capture_time_s'):.6f}",
                "elapsed_s": f"{elapsed:.6f}",
                "elapsed_norm": f"{elapsed / duration:.6f}" if duration > 0 else "0",
                "duration_s": f"{duration:.6f}",
                "duration_frames": duration_frames,
                "previous_motion_id": previous_id,
                "next_motion_id": next_id,
                "camera_pitch_deg": f"{float(row['cam_pitch_deg']):.3f}" if row.get("cam_pitch_deg") else "",
                "launch_x": f"{forward[0]:.7f}",
                "launch_y": f"{forward[1]:.7f}",
                "launch_z": f"{forward[2]:.7f}",
                "position_x_cm": f"{delta_cm[0]:.3f}",
                "position_y_cm": f"{delta_cm[1]:.3f}",
                "position_z_cm": f"{delta_cm[2]:.3f}",
                "forward_cm": f"{dot(delta_cm, forward):.3f}",
                "right_cm": f"{dot(delta_cm, right):.3f}",
                "up_cm": f"{dot(delta_cm, up):.3f}",
                "world_up_cm": f"{delta_cm[1]:.3f}",
                "speed_cm_s": f"{magnitude(velocity_cm_s):.3f}",
                "forward_speed_cm_s": f"{dot(velocity_cm_s, forward):.3f}",
                "right_speed_cm_s": f"{dot(velocity_cm_s, right):.3f}",
                "up_speed_cm_s": f"{dot(velocity_cm_s, up):.3f}",
                "max_cross_track_cm": f"{metrics['max_cross_track_cm']:.3f}",
                "max_cross_track_speed_cm_s": f"{metrics['max_cross_track_speed_cm_s']:.3f}",
                "quality": quality,
                "include_in_clean_curve": int(include),
            }
            all_curve_rows.append(curve_row)
            if include:
                clean_curve_rows.append(curve_row)

    for path, data, fields in (
        (all_path, all_curve_rows, CURVE_FIELDS),
        (clean_path, clean_curve_rows, CURVE_FIELDS),
        (audit_path, audit_rows, AUDIT_FIELDS),
    ):
        with path.open("w", newline="", encoding="utf-8-sig") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(data)

    counts = defaultdict(int)
    for row in audit_rows:
        counts[row["quality"]] += 1
    excluded = [row for row in audit_rows if row["include_in_clean_curve"] == 0]
    included_pitches = [float(row["camera_pitch_median_deg"]) for row in audit_rows
                        if row["include_in_clean_curve"] == 1 and row["camera_pitch_median_deg"]]
    included_rows = [row for row in audit_rows if row["include_in_clean_curve"] == 1]
    clean_max_cross_track = max(float(row["max_cross_track_cm"]) for row in included_rows)
    clean_max_cross_track_speed = max(
        float(row["max_cross_track_speed_cm_s"]) for row in included_rows
    )
    report = [
        "# 强化操虫穿刺 201 段中间曲线",
        "",
        f"- 原始录制：`{capture}`",
        f"- 录制标识：`{capture_id}`",
        f"- 201 段：{len(segments)} 次；纯样本候选：{counts['clean_candidate']} 次；",
        f"  疑似外力扰动：{counts['suspected_external_push']} 次；录制卡帧：{counts['recording_hitch']} 次。",
        f"- 纯样本的段内相机俯仰中位数范围：{min(included_pitches):.2f}° 至 {max(included_pitches):.2f}°。",
        "",
        "## 文件",
        "",
        f"- `{all_path.name}`：39 次全部逐帧曲线；用 `quality` / `include_in_clean_curve` 区分筛选结果。",
        f"- `{clean_path.name}`：仅纯样本候选，供后续曲线拟合或运行时参数设计使用。",
        f"- `{audit_path.name}`：每次动作一行的质量指标、相邻动作 ID、持续时间和筛除理由。",
        "",
        "## 坐标和采样口径",
        "",
        "- 世界位置原始单位为米；曲线位移转换为 cm，速度转换为 cm/s。世界竖直轴是 `+Y`。",
        "- `forward` 是 201 段起始后第 1–5 个速度样本的平均方向；`right` 是其水平右向轴；",
        "  `up` 是垂直于前两轴、指向上方的局部轴。位置均相对每次 201 段首帧。",
        "- `world_up_cm` 额外保留世界 Y 位移，便于核对不同瞄准俯仰角下的真实升降。",
        "- `elapsed_s` 使用录制时间戳；`elapsed_norm` 按各自 201 段持续时间归一化。",
        "  每条试次独立保留，不把不同瞄准角/不同结束时刻强行平均成一条曲线。",
        "",
        "## 样本筛选",
        "",
        f"- 横向最大偏移阈值：{MAX_CROSS_TRACK_CM:g} cm；横向最大速度阈值：",
        f"  {MAX_CROSS_TRACK_SPEED_CM_S:g} cm/s；相邻采样间隔上限：{MAX_SAMPLE_GAP_S:g} s。",
        f"- 本次纯样本最大横偏为 {clean_max_cross_track:.2f} cm、最大横向速度为 "
        f"{clean_max_cross_track_speed:.0f} cm/s；疑似扰动样本与其有明显间隔。",
        "- 阈值是本次录制的可审计筛查规则，不是游戏碰撞事件的直接读取；异常标签表示疑似，",
        "  原始全样本仍完整保留，需结合录制观察确认。",
    ]
    if excluded:
        report.extend(["", "被排除的试次："])
        report.extend(
            f"- 第 {row['attempt_index']} 次（motion run {row['motion_run_index']}）："
            f"横偏 {float(row['max_cross_track_cm']):.1f} cm，"
            f"横向速度 {float(row['max_cross_track_speed_cm_s']):.0f} cm/s；{row['reason']}。"
            for row in excluded
        )
    report.extend([
        "",
        "## 边界",
        "",
        "这是 MHRise 遥测生成的中间 CSV，不是 UE `UCurveVector` / `UCurveFloat` 资产，",
        "也不代表已决定最终位移所有者、曲线拟合方式或生产速度参数。",
        "",
    ])
    report_path.write_text("\n".join(report), encoding="utf-8")

    return all_path, clean_path, audit_path, report_path, {
        "capture_id": capture_id,
        "attempts": len(segments),
        "clean_attempts": counts["clean_candidate"],
        "suspected_push_attempts": counts["suspected_external_push"],
        "hitch_attempts": counts["recording_hitch"],
        "clean_rows": len(clean_curve_rows),
        "all_rows": len(all_curve_rows),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, help="Specific *_samples.csv; defaults to newest recorder capture")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR)
    args = parser.parse_args()
    capture = args.capture
    if capture is None:
        candidates = sorted(recorder_dir().glob("*_samples.csv"), key=lambda p: p.stat().st_mtime)
        if not candidates:
            parser.error(f"No *_samples.csv files in {recorder_dir()}")
        capture = candidates[-1]
    if not capture.is_file():
        parser.error(f"Capture file does not exist: {capture}")
    try:
        all_path, clean_path, audit_path, report_path, summary = build_outputs(capture, args.output_dir)
    except (OSError, ValueError, KeyError) as exc:
        parser.error(str(exc))
    print(f"Capture: {capture}")
    print("Summary:", ", ".join(f"{key}={value}" for key, value in summary.items()))
    print(f"All trajectories: {all_path}")
    print(f"Clean candidates: {clean_path}")
    print(f"Sample audit: {audit_path}")
    print(f"Notes: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
