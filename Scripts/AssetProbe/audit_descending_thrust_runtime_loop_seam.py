"""Read-only pose audit for the runtime loop observed in MHW ActionNo 14.

Run under UE 5.6 UnrealEditor-Cmd with PythonScriptPlugin. It reads the
existing 380 AnimSequence and the specified MHW telemetry CSV, evaluates RAW
poses around each observed frame wrap, and writes a separate JSON report.
No Content assets are modified.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
from pathlib import Path
import sys

import unreal


ASSET_PATH = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling"
CAPTURE_PATH = Path(
    "F:/games/Steam/steamapps/common/Monster Hunter World/Lua/"
    "MHGZ_MHW_AerialTrajectoryRecorder_samples_20261003_225540_04.csv"
)
OUT_PATH = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/DescendingThrust/runtime_loop_seam_20261003_225540_04.json"
CONTROL_BONE_NAMES = {"root", "parts", "lookat", "pl_cam"}


def is_pose_bone(name: str) -> bool:
    lowered = name.lower()
    if lowered in CONTROL_BONE_NAMES:
        return False
    if lowered.endswith("_end") or "_ik" in lowered:
        return False
    return True


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def number(row: dict[str, str], key: str) -> float:
    value = row.get(key, "")
    if not value:
        return math.nan
    return float(value)


def pose_at_frame(sequence, frame: float, fps: float, length: float, bone_names: list[str]):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    time_seconds = min(max(frame / fps, 0.0), length)
    pose = sequence.get_anim_pose_at_time(time_seconds, options)
    bones = {}
    for name in bone_names:
        component = pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD)
        local = pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.LOCAL)
        bones[name] = {
            "component_p": [float(component.translation.x), float(component.translation.y), float(component.translation.z)],
            "component_q": [float(component.rotation.x), float(component.rotation.y), float(component.rotation.z), float(component.rotation.w)],
            "local_p": [float(local.translation.x), float(local.translation.y), float(local.translation.z)],
            "local_q": [float(local.rotation.x), float(local.rotation.y), float(local.rotation.z), float(local.rotation.w)],
        }
    return {"runtime_action_frame": frame, "sequence_time_seconds": time_seconds, "bones": bones}


def distance(a, b, prefix: str):
    positions, rotations, per_bone = [], [], []
    p_key, q_key = prefix + "_p", prefix + "_q"
    for name in a["bones"].keys() & b["bones"].keys():
        pa, pb = a["bones"][name][p_key], b["bones"][name][p_key]
        p = math.sqrt(sum((x - y) ** 2 for x, y in zip(pa, pb)))
        qa, qb = a["bones"][name][q_key], b["bones"][name][q_key]
        dot = min(1.0, max(-1.0, abs(sum(x * y for x, y in zip(qa, qb)))))
        q = math.degrees(2.0 * math.acos(dot))
        positions.append(p)
        rotations.append(q)
        per_bone.append({"bone": name, "position_delta_cm": p, "rotation_delta_deg": q})
    per_bone.sort(key=lambda item: max(item["position_delta_cm"] / 10.0,
                                      item["rotation_delta_deg"] / 10.0), reverse=True)
    return {
        "bone_count": len(positions),
        "mean_position_delta_cm": sum(positions) / len(positions),
        "max_position_delta_cm": max(positions),
        "mean_rotation_delta_deg": sum(rotations) / len(rotations),
        "max_rotation_delta_deg": max(rotations),
        "largest_per_bone_deltas_advisory": per_bone[:12],
    }


def main():
    if not CAPTURE_PATH.is_file():
        raise FileNotFoundError("Runtime capture not found: " + str(CAPTURE_PATH))

    with CAPTURE_PATH.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    dive_rows = []
    for row in rows:
        if int(round(number(row, "action_fsm_id"))) == 14 and int(round(number(row, "action_lmt_id"))) == 49532:
            dive_rows.append(row)
    if not dive_rows:
        raise RuntimeError("Capture contains no ActionNo 14 / LMT ID 49532 samples")

    wraps = []
    for previous, current in zip(dive_rows, dive_rows[1:]):
        before, after = number(previous, "action_frame"), number(current, "action_frame")
        if before - after > 20.0:
            wraps.append({
                "previous_sample_index": int(round(number(previous, "sample_index"))),
                "previous_time_s": number(previous, "quest_time_s"),
                "previous_frame": before,
                "restart_sample_index": int(round(number(current, "sample_index"))),
                "restart_time_s": number(current, "quest_time_s"),
                "restart_frame": after,
                "lmt_id": int(round(number(current, "action_lmt_id"))),
                "fsm_id": int(round(number(current, "action_fsm_id"))),
            })

    end_row = next((row for row in rows
                    if int(round(number(row, "sample_index"))) > int(round(number(dive_rows[-1], "sample_index")))
                    and int(round(number(row, "action_fsm_id"))) == 132
                    and int(round(number(row, "action_lmt_id"))) == 49533), None)
    if len(wraps) < 1:
        raise RuntimeError("No frame wrap detected within ActionNo 14 / LMT ID 49532")

    sequence = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if sequence is None:
        raise FileNotFoundError("Cannot load animation asset: " + ASSET_PATH)
    skeleton = sequence.get_editor_property("skeleton")
    length = float(sequence.get_play_length())
    rate_method = getattr(sequence, "get_sampling_frame_rate", None)
    rate = rate_method() if callable(rate_method) else None
    numerator = float(getattr(rate, "numerator", 0.0)) if rate else 0.0
    denominator = float(getattr(rate, "denominator", 1.0)) if rate else 1.0
    fps = numerator / denominator if denominator else 0.0
    if fps <= 0.0:
        fps = 60.0
    key_method = getattr(sequence, "get_number_of_sampled_keys", None)
    key_count = int(key_method()) if callable(key_method) else 0
    if key_count < 2:
        key_count = int(round(length * fps)) + 1
    if key_count < 2:
        raise RuntimeError("Animation sequence has fewer than two sampled keys")
    package_path = Path(unreal.Paths.project_content_dir()) / (ASSET_PATH.removeprefix("/Game/") + ".uasset")
    if not package_path.is_file():
        raise FileNotFoundError("Animation package is missing on disk: " + str(package_path))

    first_pose = sequence.get_anim_pose_at_time(0.0, unreal.AnimPoseEvaluationOptions())
    bone_names = [str(name) for name in first_pose.get_bone_names() if is_pose_bone(str(name))]
    if len(bone_names) < 8:
        raise RuntimeError("Insufficient visible pose bones for seam audit")

    seam_results = []
    evaluated = {}
    def get_pose(frame):
        cache_key = round(frame, 7)
        if cache_key not in evaluated:
            evaluated[cache_key] = pose_at_frame(sequence, frame, fps, length, bone_names)
        return evaluated[cache_key]

    for wrap in wraps:
        before = get_pose(wrap["previous_frame"])
        after = get_pose(wrap["restart_frame"])
        seam_results.append({
            "runtime_wrap": wrap,
            "component_space_pose_delta": distance(before, after, "component"),
            "local_space_pose_delta": distance(before, after, "local"),
        })

    uasset_digest = sha256_file(package_path)
    capture_digest = sha256_file(CAPTURE_PATH)
    report = {
        "status": "RUNTIME_LOOP_FRAME_WRAP_AND_POSE_SEAM_AUDITED",
        "tool_version": "descending-thrust-runtime-loop-seam-audit/1",
        "python_version": sys.version.split()[0],
        "ue_version": (unreal.SystemLibrary.get_engine_version()
                       if hasattr(unreal, "SystemLibrary") else "UE 5.6"),
        "asset": {
            "path": ASSET_PATH,
            "class": sequence.get_class().get_name(),
            "skeleton": skeleton.get_path_name() if skeleton else None,
            "play_length_seconds": length,
            "sample_rate_hz": fps,
            "sampled_key_count": key_count,
            "uasset_path": str(package_path),
            "uasset_bytes": package_path.stat().st_size,
            "uasset_sha256": uasset_digest,
        "pose_bone_selection": "All skeletal pose/deformation bones including fingers, twist/weight chains, clothing chains, and weapon bones; excludes Root, IK controls, endpoint helpers, Parts, LookAt, and PL_Cam.",
        "pose_bones": bone_names,
        },
        "runtime_capture": {
            "path": str(CAPTURE_PATH),
            "bytes": CAPTURE_PATH.stat().st_size,
            "sha256": capture_digest,
            "total_sample_count": len(rows),
            "dive_sample_count": len(dive_rows),
            "action_fsm_id": 14,
            "action_lmt_id": 49532,
            "observed_frame_end": number(dive_rows[0], "action_frame_end"),
            "wrap_count": len(wraps),
            "wraps": wraps,
            "following_landing_action": ({
                "sample_index": int(round(number(end_row, "sample_index"))),
                "time_s": number(end_row, "quest_time_s"),
                "fsm_id": int(round(number(end_row, "action_fsm_id"))),
                "lmt_id": int(round(number(end_row, "action_lmt_id"))),
                "action_frame": number(end_row, "action_frame"),
            } if end_row else None),
            "loop_start_estimate_frame": sum(wrap["restart_frame"] for wrap in wraps) / len(wraps),
            "loop_end_exclusive_frame": number(dive_rows[0], "action_frame_end"),
        },
        "pose_seam_results": seam_results,
        "interpretation_limits": [
            "The telemetry establishes runtime frame wrap(s) in one LMT/FSM state; the RAW asset evaluation measures the corresponding sequence-pose discontinuity.",
            "The audit does not capture the game's pose blend across the wrap, so a nonzero asset seam delta does not by itself prove a visible pop in MHW.",
            "Runtime action_frame is mapped to source sequence frame by the shared 69-frame end marker; this mapping is recorded as an assumption to be visually checked if the exact source mapping is disputed.",
            "No Content asset is modified.",
        ],
    }
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("DESCENDING_THRUST_RUNTIME_LOOP_SEAM_AUDIT=" + str(OUT_PATH))
    print("WRAP_COUNT=" + str(len(wraps)))
    for index, seam in enumerate(seam_results, start=1):
        component = seam["component_space_pose_delta"]
        local = seam["local_space_pose_delta"]
        print("WRAP_{}_COMPONENT mean_pos={:.4f} max_pos={:.4f} mean_rot={:.4f} max_rot={:.4f}".format(
            index, component["mean_position_delta_cm"], component["max_position_delta_cm"],
            component["mean_rotation_delta_deg"], component["max_rotation_delta_deg"]))
        print("WRAP_{}_LOCAL mean_pos={:.4f} max_pos={:.4f} mean_rot={:.4f} max_rot={:.4f}".format(
            index, local["mean_position_delta_cm"], local["max_position_delta_cm"],
            local["mean_rotation_delta_deg"], local["max_rotation_delta_deg"]))
    unreal.log("[DescendingThrustLoopSeam] Read-only runtime loop seam audit complete.")


main()
