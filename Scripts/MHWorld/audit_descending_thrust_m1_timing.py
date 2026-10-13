"""Build a derived timing audit for MHW runtime actions 49532/49533.

Reads the original recorder sample CSV and the UE 5.6 M1 pose audit, then
maps captured native action frames to 60 Hz source-sequence seconds. It does
not edit the source CSV, game files, or Unreal assets. It writes only the two
derived JSON reports declared by OUT and MANIFEST_OUT below.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


PROJECT = Path(__file__).resolve().parents[2]
SOURCE = Path(
    "F:/games/Steam/steamapps/common/Monster Hunter World/Lua/"
    "MHGZ_MHW_AerialTrajectoryRecorder_samples_20261003_115232_01.csv"
)
POSE_AUDIT = PROJECT / "Saved/AssetProbe/DescendingThrust/pose_and_cycle_audit.json"
OUT = PROJECT / "Saved/AssetProbe/DescendingThrust/turn_clock_audit.json"
MANIFEST_OUT = PROJECT / "Saved/AssetProbe/DescendingThrust/turn_timing_manifest.json"
ACTION_IDS = {49532, 49533}
SEQUENCE_FPS = {49532: 60.0, 49533: 60.0}
PROBE_FRAMES = (0.0, 4.4, 5.5, 59.0, 70.5, 160.0)
TOOL_VERSION = "descending-thrust-m1-timing-audit/2"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def as_float(row, key):
    value = row.get(key, "")
    return float(value) if value not in (None, "") else None


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    position = (len(values) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    if low == high:
        return values[low]
    return values[low] + (values[high] - values[low]) * (position - low)


def interpolate_root(root_track, frame):
    if not root_track:
        return None
    index = max(0, min(len(root_track) - 1, int(math.floor(frame))))
    next_index = min(len(root_track) - 1, index + 1)
    alpha = max(0.0, min(1.0, frame - index))
    a = root_track[index]["translation_cm"]
    b = root_track[next_index]["translation_cm"]
    return [a[axis] + (b[axis] - a[axis]) * alpha for axis in range(3)]


def projected_forward_path(rows, stop_index):
    total = 0.0
    for previous, current in zip(rows[:stop_index], rows[1:stop_index + 1]):
        dx = as_float(current, "position_x") - as_float(previous, "position_x")
        dz = as_float(current, "position_z") - as_float(previous, "position_z")
        ax = as_float(previous, "facing_forward_x")
        az = as_float(previous, "facing_forward_z")
        bx = as_float(current, "facing_forward_x")
        bz = as_float(current, "facing_forward_z")
        if None in (dx, dz, ax, az, bx, bz):
            continue
        length_a, length_b = math.hypot(ax, az), math.hypot(bx, bz)
        if length_a <= 1e-6 or length_b <= 1e-6:
            continue
        fx, fz = ax / length_a + bx / length_b, az / length_a + bz / length_b
        length_f = math.hypot(fx, fz)
        if length_f > 1e-6:
            total += dx * fx / length_f + dz * fz / length_f
    return total


def frame_probe(rows, target, start_time, fps, root_track=None, allow_root_comparison=False):
    candidates = [row for row in rows if as_float(row, "action_frame") is not None]
    if not candidates:
        return None
    row = min(candidates, key=lambda item: abs(as_float(item, "action_frame") - target))
    observed_frame = as_float(row, "action_frame")
    observed_time = as_float(row, "sample_elapsed_game_s") - start_time
    result = {
        "target_action_frame": target,
        "observed_action_frame": observed_frame,
        "action_frame_error": abs(observed_frame - target),
        "relative_recording_seconds": observed_time,
        "source_sequence_seconds_at_60hz_candidate_only": observed_frame / fps,
        "recorded_minus_frame_over_60_candidate_residual_seconds": observed_time - observed_frame / fps,
        "sample_index": as_float(row, "sample_index"),
        "action_frame_speed": as_float(row, "action_frame_speed"),
        "action_frame_speed_multiplier": as_float(row, "action_frame_speed_multiplier"),
    }
    if allow_root_comparison and root_track:
        position = [as_float(row, axis) for axis in ("position_x", "position_y", "position_z")]
        start_position = [as_float(rows[0], axis) for axis in ("position_x", "position_y", "position_z")]
        root_position = interpolate_root(root_track, observed_frame)
        root_start = root_track[0]["translation_cm"]
        game_forward = projected_forward_path(rows, candidates.index(row))
        root_forward_cm = root_position[1] - root_start[1]
        result["position_world_native_xyz"] = position
        result["world_horizontal_chord_native_units"] = math.hypot(
            position[0] - start_position[0], position[2] - start_position[2])
        result["world_forward_path_projected_to_facing_native_units"] = game_forward
        result["ue_root_forward_delta_cm_at_same_native_action_frame"] = root_forward_cm
        result["native_world_units_per_ue_cm_forward_projection"] = (
            game_forward / root_forward_cm if abs(root_forward_cm) > 1e-5 else None)
    return result


def summarize_run(action_id, rows):
    start_time = as_float(rows[0], "sample_elapsed_game_s")
    end_time = as_float(rows[-1], "sample_elapsed_game_s")
    frames = [as_float(row, "action_frame") for row in rows]
    frames = [value for value in frames if value is not None]
    fps = SEQUENCE_FPS[action_id]
    frame_rates = []
    for previous, current in zip(rows, rows[1:]):
        dt = as_float(current, "sample_elapsed_game_s") - as_float(previous, "sample_elapsed_game_s")
        df = as_float(current, "action_frame") - as_float(previous, "action_frame")
        if dt > 0 and df >= 0:
            frame_rates.append(df / dt)
    frame_speeds = sorted({as_float(row, "action_frame_speed") for row in rows
                           if as_float(row, "action_frame_speed") is not None})
    multipliers = sorted({as_float(row, "action_frame_speed_multiplier") for row in rows
                          if as_float(row, "action_frame_speed_multiplier") is not None})
    action_id = int(as_float(rows[0], "action_lmt_id"))
    observed_rate = statistics.median(frame_rates) if frame_rates else None
    mapping_validated = (action_id == 49533 and observed_rate is not None
                         and abs(observed_rate - fps) <= 1.0
                         and abs(frames[0]) <= 0.1)
    root_track = None
    if action_id == 49533:
        root_track = json.loads(POSE_AUDIT.read_text(encoding="utf-8"))["sequences"][
            "runtime_49533_lmt381"]["root_track_local_samples"]
    probes = [frame_probe(rows, target, start_time, fps, root_track, mapping_validated)
              for target in PROBE_FRAMES]
    valid_probes = [item for item in probes if item is not None]
    timing_residuals = [item["recorded_minus_frame_over_60_candidate_residual_seconds"] for item in valid_probes
                        if item["action_frame_error"] < 0.1]
    return {
        "runtime_action_id": action_id,
        "user_confirmed_source_sequence_id": action_id - 49152,
        "sample_count": len(rows),
        "sample_start_index": as_float(rows[0], "sample_index"),
        "sample_end_index": as_float(rows[-1], "sample_index"),
        "recording_duration_seconds": end_time - start_time,
        "action_frame_start": frames[0],
        "action_frame_end": frames[-1],
        "action_frame_rate_observed_hz": {
            "median": statistics.median(frame_rates) if frame_rates else None,
            "p05": percentile(frame_rates, 0.05),
            "p95": percentile(frame_rates, 0.95),
            "distinct_action_frame_speed_values": frame_speeds,
            "distinct_speed_multiplier_values": multipliers,
        },
        "native_action_frame_to_ue_sequence_time_mapping_validated": mapping_validated,
        "mapping_validation_basis": (
            "Per-run action-frame derivative is within 1 Hz of 60 Hz, first recorded frame is 0, and current UE source sequence is sampled at 60 Hz."
            if mapping_validated else
            "Not validated: action-frame derivative differs from the UE source sampling rate; do not convert frame/60 to source time."),
        "source_sequence_sampling_hz_assumed_from_current_ue_audit": fps,
        "probes": probes,
        "recorded_minus_frame_over_60_residual_seconds_for_probes": {
            "median": statistics.median(timing_residuals) if timing_residuals else None,
            "min": min(timing_residuals) if timing_residuals else None,
            "max": max(timing_residuals) if timing_residuals else None,
        },
    }


def median_probe_seconds(summaries, action_id, target_frame):
    values = []
    for run in summaries:
        if run["runtime_action_id"] != action_id:
            continue
        for probe in run["probes"]:
            if (probe is not None and probe["target_action_frame"] == target_frame
                    and probe["action_frame_error"] < 0.1):
                values.append(probe["relative_recording_seconds"])
                break
    return statistics.median(values) if values else None


def probe_alignment_summary(summaries, action_id, target_frame):
    residuals = []
    for run in summaries:
        if run["runtime_action_id"] != action_id:
            continue
        for probe in run["probes"]:
            if (probe is not None and probe["target_action_frame"] == target_frame
                    and probe["action_frame_error"] < 0.1):
                residuals.append(probe["recorded_minus_frame_over_60_candidate_residual_seconds"])
                break
    return {
        "sample_count": len(residuals),
        "median_elapsed_minus_frame_over_60_seconds": statistics.median(residuals) if residuals else None,
        "p95_elapsed_minus_frame_over_60_seconds": percentile(residuals, 0.95),
        "min_elapsed_minus_frame_over_60_seconds": min(residuals) if residuals else None,
        "max_elapsed_minus_frame_over_60_seconds": max(residuals) if residuals else None,
        "within_0p02s_count": sum(abs(value) <= 0.02 for value in residuals),
        "between_0p03s_and_0p07s_count": sum(0.03 <= value <= 0.07 for value in residuals),
        "interpretation": "Recorder elapsed-time alignment residual only; do not apply it as a sequence-time offset. Cause is not identified.",
    }


def make_turn_timing_manifest(report):
    runs = report["runs"]
    first_end = median_probe_seconds(runs, 49532, 4.4)
    second_end = median_probe_seconds(runs, 49533, 5.5)
    third_start_observed = median_probe_seconds(runs, 49533, 59.0)
    third_end_observed = median_probe_seconds(runs, 49533, 70.5)
    sequence_fps = 60.0
    third_start_sequence_s = 59.0 / sequence_fps
    third_end_sequence_s = 70.5 / sequence_fps
    return {
        "status": "PROJECT_RULES_FROZEN_NATIVE_MECHANISM_PARTIALLY_UNOBSERVED",
        "source_csv": report["source_csv"],
        "source_sequence_map": {
            "runtime_action_49532": {
                "user_confirmed_source_sequence_id": 380,
                "asset_path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling",
                "sampling_rate_hz": sequence_fps,
                "runtime_frame_to_source_time_validated": False,
                "note": "Observed action-frame rate differs from 60 Hz. Turn 1 is attached to the authored 380 montage entry event; do not derive source time from runtime action_frame / 60.",
            },
            "runtime_action_49533": {
                "user_confirmed_source_sequence_id": 381,
                "asset_path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_Over",
                "sampling_rate_hz": sequence_fps,
                "runtime_frame_to_source_time_validated": True,
                "validation_run_count": 19,
                "median_observed_action_frame_rate_hz": statistics.median(
                    run["action_frame_rate_observed_hz"]["median"]
                    for run in runs if run["runtime_action_id"] == 49533
                ),
                "third_turn_elapsed_alignment_at_frame_59": probe_alignment_summary(runs, 49533, 59.0),
                "third_turn_elapsed_alignment_at_frame_70_5": probe_alignment_summary(runs, 49533, 70.5),
                "note": "All 19 per-run median action-frame rates are near 60 Hz and first frame is 0, agreeing with the current 381 source sampling rate. Elapsed-time residuals form two clusters; do not use those residuals to shift montage-local time. The observed yaw window still does not prove the hidden input-latch edge.",
            },
        },
        "provenance": {
            "tool_version": TOOL_VERSION,
            "python_version": sys.version.split()[0],
            "timing_source_csv_sha256": report["source_csv"]["sha256"],
            "pose_audit_path": report["current_ue_asset_audit"],
            "pose_audit_sha256": report["current_ue_asset_audit_sha256"],
            "source_assets": report["source_assets"],
            "excluded_action_runs": [],
            "exclusion_note": "All 19 complete 49532->49533 chains were retained. The elapsed-time residual clusters are reported, not filtered out.",
        },
        "turns": [
            {
                "ordinal": 1,
                "runtime_action_id": 49532,
                "observed_yaw_change_action_frames": [0.0, 4.4],
                "observed_median_elapsed_seconds_from_action_entry": first_end,
                "source_sequence_event": "Enter 380 Start section",
                "source_sequence_frame": 0.0,
                "source_sequence_seconds": 0.0,
                "project_montage_asset": "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi",
                "project_montage_section": "Start",
                "montage_local_seconds": 0.0,
                "input_snapshot": "game frame N-1 immediately before section entry",
                "max_correction_degrees": 180.0,
                "project_correction_duration_seconds": 0.1,
                "timing_evidence_limit": "The event time is the user's frozen montage-entry contract; runtime 49532 action frames are not mapped to 380 source seconds.",
            },
            {
                "ordinal": 2,
                "runtime_action_id": 49533,
                "observed_yaw_change_action_frames": [0.0, 5.5],
                "observed_median_elapsed_seconds_from_action_entry": second_end,
                "source_sequence_event": "Enter 381 Over section on actual landing",
                "source_sequence_frame": 0.0,
                "source_sequence_seconds": 0.0,
                "project_montage_asset": "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over",
                "project_montage_section": "Over",
                "montage_local_seconds": 0.0,
                "input_snapshot": "game frame N-1 immediately before Over section entry",
                "max_correction_degrees": 180.0,
                "project_correction_duration_seconds": 0.1,
                "timing_evidence_limit": "The event time is the user's frozen actual-landing/section-entry contract; the recorded yaw output window does not independently expose the hidden latch edge.",
            },
            {
                "ordinal": 3,
                "runtime_action_id": 49533,
                "observed_yaw_change_action_frames": [59.0, 70.5],
                "observed_median_elapsed_seconds_from_action_entry": {
                    "start": third_start_observed,
                    "end": third_end_observed,
                    "duration": third_end_observed - third_start_observed,
                },
                "elapsed_time_alignment_at_start": probe_alignment_summary(runs, 49533, 59.0),
                "elapsed_time_alignment_at_end": probe_alignment_summary(runs, 49533, 70.5),
                "source_sequence_frame": {"start": 59.0, "end": 70.5},
                "source_sequence_seconds": {
                    "start": third_start_sequence_s,
                    "end": third_end_sequence_s,
                    "duration": third_end_sequence_s - third_start_sequence_s,
                },
                "project_montage_asset": "/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over",
                "project_montage_section": "Over",
                "montage_local_seconds": {
                    "start": third_start_sequence_s,
                    "end": third_end_sequence_s,
                    "duration": third_end_sequence_s - third_start_sequence_s,
                },
                "input_snapshot": "game frame N-1 immediately before third correction event",
                "max_correction_degrees": 60.0,
                "project_correction_duration_seconds": 0.2,
                "timing_evidence_limit": "The sequence-time mapping is supported by 19 runs. Observed yaw window is about 0.1917 s; 0.2 s is the frozen project correction duration, not a claim of exact original engine interpolation.",
            },
        ],
        "frozen_project_rules": {
            "lmt380_runtime_loop": "49532 wraps over the observed source-frame interval approximately [39.397, 69); pose-boundary audit is accepted for M1.",
            "horizontal_input_component": {
                "status": "USER_FROZEN_PROJECT_APPROXIMATION",
                "stick_magnitude_exclusive_threshold": 0.5,
                "directional_component": 300.0,
                "zero_component_at_or_below_threshold": True,
                "direction_source": "frozen first-correction target",
            },
            "base_inertia_by_source": {
                "IG_AirDodge": 900.0,
                "all_other_explicit_named_implemented_aerial_actions": 400.0,
                "status": "USER_FROZEN_PROJECT_APPROXIMATION",
            },
        },
        "remaining_m1_blocks": [],
        "native_evidence_limits": [
            "The hidden native input-latch edges are not directly exposed by yaw output; M1 records the user's frozen project event/snapshot contract instead of claiming native-engine proof.",
            "The recorder does not capture analog stick magnitude; the >0.50 and fixed 300-unit branch is a user-approved project approximation, not a native MHW rule.",
            "Runtime action 49532 frame numbers are not validated as 380 source-sequence time and must not be divided by 60.",
        ],
    }


def main():
    if not SOURCE.is_file():
        raise FileNotFoundError(f"Source recorder CSV missing: {SOURCE}")
    if not POSE_AUDIT.is_file():
        raise FileNotFoundError(f"Current UE asset audit missing: {POSE_AUDIT}")
    pose_audit = json.loads(POSE_AUDIT.read_text(encoding="utf-8"))
    sequence_rates = {
        49532: pose_audit["sequences"]["runtime_49532_lmt380"]["sampling_rate_hz"],
        49533: pose_audit["sequences"]["runtime_49533_lmt381"]["sampling_rate_hz"],
    }
    if any(abs(float(sequence_rates[key]) - 60.0) > 1e-4 for key in sequence_rates):
        raise RuntimeError(f"Expected current 380/381 sequences to sample at 60 Hz: {sequence_rates}")

    runs = []
    active_id = None
    active_rows = []
    with SOURCE.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            raw_id = as_float(row, "action_lmt_id")
            action_id = int(raw_id) if raw_id is not None else None
            if action_id != active_id:
                if active_id in ACTION_IDS and active_rows:
                    runs.append((active_id, active_rows))
                active_id = action_id
                active_rows = []
            if action_id in ACTION_IDS:
                active_rows.append(row)
    if active_id in ACTION_IDS and active_rows:
        runs.append((active_id, active_rows))

    summaries = [summarize_run(action_id, rows) for action_id, rows in runs]
    action_counts = {str(action_id): sum(1 for item in summaries if item["runtime_action_id"] == action_id)
                     for action_id in sorted(ACTION_IDS)}
    report = {
        "status": "COMPLETE_PER_RUN_NATIVE_ACTION_FRAME_AUDIT_WITH_ELAPSED_ALIGNMENT_VARIANCE",
        "tool_version": TOOL_VERSION,
        "python_version": sys.version.split()[0],
        "source_csv": {"path": str(SOURCE), "sha256": sha256(SOURCE)},
        "current_ue_asset_audit": str(POSE_AUDIT),
        "current_ue_asset_audit_sha256": sha256(POSE_AUDIT),
        "source_assets": {
            str(action_id - 49152): {
                "runtime_action_id": action_id,
                "asset_path": pose_audit["sequences"][f"runtime_{action_id}_lmt{action_id - 49152}"]["asset_path"],
                "uasset_sha256": pose_audit["sequences"][f"runtime_{action_id}_lmt{action_id - 49152}"]["uasset"]["sha256"],
                "source_fbx_sha256": pose_audit["sequences"][f"runtime_{action_id}_lmt{action_id - 49152}"]["source_fbx"]["sha256"],
                "sampling_rate_hz": sequence_rates[action_id],
            }
            for action_id in sorted(ACTION_IDS)
        },
        "mapping_contract": {
            "user_confirmed_runtime_49532_to_sequence_380": True,
            "user_confirmed_runtime_49533_to_sequence_381": True,
            "both_current_ue_sequences_sample_at_60hz": True,
            "conversion": "Only runtime 49533 currently validates source seconds = native action_frame / 60; 49532 does not, so its frame number is not mapped to sequence time.",
            "limitation": "Yaw-change output windows do not prove hidden input-lock/correction-trigger edges; this maps the observed action-frame clock to source-sequence time only.",
        },
        "runtime_action_run_counts": action_counts,
        "runs": summaries,
    }
    report["mapping_contract"]["conversion"] = (
        "Runtime 49533 per-run median frame rates are checked against 60 Hz and the first action frame; only runs passing that check map source seconds = action_frame / 60. Runtime 49532 is not validated and must not be converted this way."
    )
    report["mapping_contract"]["limitation"] = (
        "Elapsed-time alignment residuals are bimodal; use action_frame / 60 for 49533 source sequence time, not sample_elapsed_game_s plus a global offset. Yaw-change output windows do not prove hidden input-lock/correction-trigger edges."
    )
    manifest = make_turn_timing_manifest(report)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    MANIFEST_OUT.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"DESCENDING_THRUST_TURN_CLOCK_AUDIT={OUT}")
    print(f"DESCENDING_THRUST_TURN_TIMING_MANIFEST={MANIFEST_OUT}")
    print(f"RUNS={len(summaries)} COUNTS={action_counts}")
    for action_id in sorted(ACTION_IDS):
        selected = [item for item in summaries if item["runtime_action_id"] == action_id]
        rate_values = [item["action_frame_rate_observed_hz"]["median"] for item in selected]
        mapping_validated = all(item["native_action_frame_to_ue_sequence_time_mapping_validated"]
                                for item in selected)
        if mapping_validated:
            alignment = probe_alignment_summary(selected, action_id, 59.0)
            print(f"ACTION={action_id} runs={len(selected)} median_frame_hz={statistics.median(rate_values):.5f} "
                  f"frame_to_sequence_time=VALIDATED frame59_elapsed_residual_median="
                  f"{alignment['median_elapsed_minus_frame_over_60_seconds']:.5f}s "
                  f"p95={alignment['p95_elapsed_minus_frame_over_60_seconds']:.5f}s "
                  f"clusters={alignment['within_0p02s_count']}near_8ms/"
                  f"{alignment['between_0p03s_and_0p07s_count']}near_50ms")
        else:
            print(f"ACTION={action_id} runs={len(selected)} median_frame_hz={statistics.median(rate_values):.5f} "
                  "frame_to_sequence_time=NOT_VALIDATED; do not use frame/60")


main()
