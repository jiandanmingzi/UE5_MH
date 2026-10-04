"""Read-only M1 audit of current dive assets and existing falling-loop options.

Run with UE 5.6 UnrealEditor-Cmd and PythonScriptPlugin. This script never
saves Content assets. It samples RAW poses, records on-disk package hashes,
    ranks advisory repeated-pose windows in the 380 source sequence, and
    compares existing falling-loop poses against 380 without retargeting.
"""

from __future__ import annotations

import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import unreal


ASSETS = {
    "runtime_49532_lmt380": {
        "path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling",
        "fbx": "Saved/MHW_AerialDive_FullBody_Export_20261002/MHW_WP10_Candidate_LMT380_FullBody.fbx",
    },
    "runtime_49533_lmt381": {
        "path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_Over",
        "fbx": "Saved/MHW_AerialDive_FullBody_Export_20261002/MHW_WP10_Candidate_LMT381_FullBody.fbx",
    },
    "existing_unsh_fall_loop": {
        "path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_Loop",
        "fbx": None,
    },
    "existing_unsh_jianglong_fall_loop": {
        "path": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiangLong_Fall_Loop",
        "fbx": None,
    },
}
OUT_DIR = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/DescendingThrust"
TOOL_VERSION = "descending-thrust-m1-pose-audit/2"
BODY_TOKENS = (
    "waist", "cog", "spine", "neck", "head", "clavicle", "shoulder",
    "upperarm", "forearm", "arm_", "hand", "leg", "thigh", "calf", "foot",
    "weapon_00",
)
EXCLUDE_TOKENS = ("finger", "thumb", "twist", "_end", "ik_", "lookat", "parts")


def is_cycle_pose_bone(name):
    lowered = name.lower()
    if not any(token in lowered for token in BODY_TOKENS):
        return False
    if any(token in lowered for token in EXCLUDE_TOKENS):
        return False
    if re.search(r"_(?:ik|t|w)$", lowered):
        return False
    return lowered != "root"


def digest(path):
    if not path.is_file():
        return {"exists": False, "path": str(path)}
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return {"exists": True, "path": str(path), "bytes": path.stat().st_size,
            "sha256": hasher.hexdigest()}


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def quat(value):
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def pose_distance(a, b):
    positions, rotations = [], []
    for name in a.keys() & b.keys():
        pa, pb = a[name]["p"], b[name]["p"]
        positions.append(math.sqrt(sum((x - y) ** 2 for x, y in zip(pa, pb))))
        qa, qb = a[name]["q"], b[name]["q"]
        dot = min(1.0, max(-1.0, abs(sum(x * y for x, y in zip(qa, qb)))))
        rotations.append(math.degrees(2.0 * math.acos(dot)))
    return (sum(positions) / len(positions), max(positions),
            sum(rotations) / len(rotations), max(rotations))


def pose_distance_per_bone(a, b):
    result = {}
    for name in a.keys() & b.keys():
        pa, pb = a[name]["p"], b[name]["p"]
        position = math.sqrt(sum((x - y) ** 2 for x, y in zip(pa, pb)))
        qa, qb = a[name]["q"], b[name]["q"]
        dot = min(1.0, max(-1.0, abs(sum(x * y for x, y in zip(qa, qb)))))
        rotation = math.degrees(2.0 * math.acos(dot))
        result[name] = (position, rotation)
    return result


def rotation_delta_degrees(a, b):
    qa, qb = a["q"], b["q"]
    dot = min(1.0, max(-1.0, abs(sum(x * y for x, y in zip(qa, qb)))))
    return math.degrees(2.0 * math.acos(dot))


def load_sequence(label, config):
    path = config["path"]
    sequence = unreal.EditorAssetLibrary.load_asset(path)
    if sequence is None:
        raise FileNotFoundError("Current animation package cannot load: " + path)
    skeleton = sequence.get_editor_property("skeleton")
    if skeleton is None:
        raise RuntimeError("Sequence has no Skeleton: " + path)

    length = float(sequence.get_play_length())
    rate_scale = float(sequence.get_editor_property("rate_scale"))
    rate_method = getattr(sequence, "get_sampling_frame_rate", None)
    rate = rate_method() if callable(rate_method) else None
    numerator = float(getattr(rate, "numerator", 0.0)) if rate else 0.0
    denominator = float(getattr(rate, "denominator", 1.0)) if rate else 1.0
    fps = numerator / denominator if denominator else 0.0
    key_method = getattr(sequence, "get_number_of_sampled_keys", None)
    keys = int(key_method()) if callable(key_method) else 0
    if fps <= 0.0:
        fps = 60.0
    if keys < 2:
        keys = int(round(length * fps)) + 1

    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)

    samples = []
    names = None
    for frame in range(keys):
        time = min(frame / fps, length)
        pose = sequence.get_anim_pose_at_time(time, options)
        if names is None:
            all_names = [str(name) for name in pose.get_bone_names()]
            names = [name for name in all_names if is_cycle_pose_bone(name)]
            if len(names) < 8:
                raise RuntimeError("Could not identify enough body/weapon pose bones: " + repr(all_names))
        bones = {}
        for name in names:
            local_transform = pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.LOCAL)
            component_transform = pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD)
            bones[name] = {
                "p": vec(component_transform.translation),
                "q": quat(component_transform.rotation),
                "local_p": vec(local_transform.translation),
                "local_q": quat(local_transform.rotation),
            }
        root = pose.get_bone_pose(unreal.Name("Root"), unreal.AnimPoseSpaces.LOCAL)
        samples.append({"frame": frame, "time": time, "bones": bones,
                        "root_p": vec(root.translation), "root_q": quat(root.rotation)})

    return sequence, {
        "label": label,
        "asset_path": path,
        "asset_class": sequence.get_class().get_name(),
        "skeleton": skeleton.get_path_name(),
        "play_length_seconds": length,
        "rate_scale": rate_scale,
        "effective_length_seconds_at_rate_scale": length / rate_scale if rate_scale else None,
        "sampling_rate_hz": fps,
        "sampled_key_count": keys,
        "sampled_final_time_seconds": samples[-1]["time"],
        "all_skeleton_bone_names": [str(name) for name in pose.get_bone_names()],
        "pose_bones_used_for_cycle_search": names,
        "root_start_local": {"translation": samples[0]["root_p"], "rotation_xyzw": samples[0]["root_q"]},
        "root_end_local": {"translation": samples[-1]["root_p"], "rotation_xyzw": samples[-1]["root_q"]},
        "root_delta_local_cm": [samples[-1]["root_p"][i] - samples[0]["root_p"][i]
                                 for i in range(3)],
        "root_track_local_samples": [
            {"frame": sample["frame"], "time_seconds": sample["time"],
             "translation_cm": sample["root_p"], "rotation_xyzw": sample["root_q"]}
            for sample in samples],
        "uasset": digest(Path(unreal.Paths.project_content_dir())
                         / (path.removeprefix("/Game/") + ".uasset")),
        "source_fbx": (digest(Path(unreal.Paths.project_dir()) / config["fbx"])
                       if config.get("fbx") else {"exists": False, "reason": "not configured for this audit"}),
        "asset_import_data_api": [name for name in dir(sequence)
                                  if "import" in name.lower() or "track" in name.lower()
                                  or "data_model" in name.lower()],
    }, samples


def frame_velocity(a, b, fps):
    result = []
    for name in a.keys() & b.keys():
        result.append([(b[name]["p"][axis] - a[name]["p"][axis]) * fps
                       for axis in range(3)])
    if not result:
        return 0.0
    return math.sqrt(sum(sum(component * component for component in row)
                         for row in result) / len(result))


def cycle_candidates(samples, fps):
    count = len(samples)
    if count < 18:
        return []
    first_candidate = max(1, int(math.ceil(count * 0.08)))
    min_period = 6  # Candidate discovery floor only; not a production acceptance threshold.
    candidates = []
    for period in range(min_period, (count - first_candidate) // 2 + 1):
        for start in range(first_candidate, count - 2 * period + 1):
            repeated_cycles = 3 if start + 3 * period <= count else 2
            compared_pairs = repeated_cycles - 1
            pose_metrics = []
            per_bone_errors = {}
            for cycle in range(compared_pairs):
                base_a, base_b = start + cycle * period, start + (cycle + 1) * period
                for offset in range(period):
                    pose_a = samples[base_a + offset]["bones"]
                    pose_b = samples[base_b + offset]["bones"]
                    pose_metrics.append(pose_distance(pose_a, pose_b))
                    for bone, error in pose_distance_per_bone(pose_a, pose_b).items():
                        bucket = per_bone_errors.setdefault(bone, {"translation": [], "rotation": []})
                        bucket["translation"].append(error[0])
                        bucket["rotation"].append(error[1])
            mean_pos = sum(x[0] for x in pose_metrics) / len(pose_metrics)
            max_pos = max(x[1] for x in pose_metrics)
            mean_rot = sum(x[2] for x in pose_metrics) / len(pose_metrics)
            max_rot = max(x[3] for x in pose_metrics)
            activity = sum(pose_distance(samples[start + i]["bones"],
                                         samples[start + i + 1]["bones"])[0]
                           for i in range(period - 1)) / max(1, period - 1)
            # A convenience ranking only. Preserve raw position and rotation errors;
            # do not treat this mixed-unit value as an acceptance threshold.
            rank = mean_pos + 0.2 * mean_rot
            end_exclusive = start + period
            loop_seam = pose_distance(samples[end_exclusive - 1]["bones"],
                                      samples[start]["bones"])
            entry_motion = frame_velocity(samples[start - 1]["bones"],
                                          samples[start]["bones"], fps)
            loop_first_motion = frame_velocity(samples[start]["bones"],
                                               samples[start + 1]["bones"], fps)
            loop_last_motion = frame_velocity(samples[end_exclusive - 2]["bones"],
                                              samples[end_exclusive - 1]["bones"], fps)
            rotation_activity = []
            for frame in range(start, end_exclusive - 1):
                frame_a, frame_b = samples[frame]["bones"], samples[frame + 1]["bones"]
                rotation_activity.extend(rotation_delta_degrees(frame_a[bone], frame_b[bone])
                                         for bone in frame_a.keys() & frame_b.keys())
            per_bone_summary = {
                bone: {
                    "mean_repeated_translation_error_cm": sum(values["translation"]) / len(values["translation"]),
                    "max_repeated_translation_error_cm": max(values["translation"]),
                    "mean_repeated_rotation_error_deg": sum(values["rotation"]) / len(values["rotation"]),
                    "max_repeated_rotation_error_deg": max(values["rotation"]),
                }
                for bone, values in per_bone_errors.items()
            }
            worst_bones = sorted(
                per_bone_summary.items(),
                key=lambda item: max(item[1]["max_repeated_translation_error_cm"] / 10.0,
                                     item[1]["max_repeated_rotation_error_deg"] / 10.0),
                reverse=True)[:8]
            mean_translation_energy_per_frame = sum(
                pose_distance(samples[frame]["bones"], samples[frame + 1]["bones"])[0]
                for frame in range(start, end_exclusive - 1)) / max(1, period - 1)
            candidates.append({
                "rank_score_translation_cm_plus_0p20_rotation_deg_advisory": rank,
                "mean_repeated_pose_translation_error_cm": mean_pos,
                "max_repeated_pose_translation_error_cm": max_pos,
                "mean_repeated_pose_rotation_error_deg": mean_rot,
                "max_repeated_pose_rotation_error_deg": max_rot,
                "mean_local_translation_per_frame_activity_cm": activity,
                "mean_pose_translation_change_per_frame_cm": mean_translation_energy_per_frame,
                "mean_pose_rotation_change_per_bone_per_frame_deg": sum(rotation_activity) / max(1, len(rotation_activity)),
                "per_bone_repeated_pose_errors": per_bone_summary,
                "worst_bones_by_max_error_advisory": [
                    {"bone": name, **summary} for name, summary in worst_bones],
                "cycle_start_frame_inclusive": start,
                "cycle_end_frame_exclusive": end_exclusive,
                "source_seconds_start": samples[start]["time"],
                "source_seconds_end_exclusive": samples[end_exclusive - 1]["time"] + 1.0 / fps,
                "period_frames": period,
                "period_seconds": period / fps,
                "repeated_cycles_available": repeated_cycles,
                "compared_cycle_pairs": compared_pairs,
                "repeat_evidence_end_frame_inclusive": start + repeated_cycles * period - 1,
                "loop_pose_wrap_mean_translation_cm": loop_seam[0],
                "loop_pose_wrap_max_translation_cm": loop_seam[1],
                "loop_pose_wrap_mean_rotation_deg": loop_seam[2],
                "loop_pose_wrap_max_rotation_deg": loop_seam[3],
                "entry_velocity_pose_delta_norm_cm_per_s": entry_motion,
                "loop_first_velocity_pose_delta_norm_cm_per_s": loop_first_motion,
                "loop_last_velocity_pose_delta_norm_cm_per_s": loop_last_motion,
                "entry_vs_loop_first_velocity_norm_difference_cm_per_s": abs(entry_motion - loop_first_motion),
                "compared_complete_cycles": repeated_cycles,
            })
    candidates.sort(key=lambda item: item["rank_score_translation_cm_plus_0p20_rotation_deg_advisory"])
    return candidates


def cross_pose_candidates(reference_samples, candidate_samples):
    """Return the best component-pose matches for visual triage, not auto-approval."""
    results = []
    best_by_reference_frame = {}
    for reference in reference_samples:
        for candidate in candidate_samples:
            candidate_names = candidate["bones"].keys()
            shared = candidate_names & reference["bones"].keys()
            if len(shared) < 8:
                continue
            metrics = pose_distance(
                {name: candidate["bones"][name] for name in shared},
                {name: reference["bones"][name] for name in shared},
            )
            result = {
                "advisory_rank_score_translation_cm_plus_0p20_rotation_deg": metrics[0] + 0.2 * metrics[2],
                "mean_component_position_error_cm": metrics[0],
                "max_component_position_error_cm": metrics[1],
                "mean_component_rotation_error_deg": metrics[2],
                "max_component_rotation_error_deg": metrics[3],
                "shared_pose_bone_count": len(shared),
                "reference_380_frame": reference["frame"],
                "reference_380_seconds": reference["time"],
                "candidate_frame": candidate["frame"],
                "candidate_seconds": candidate["time"],
            }
            results.append(result)
            previous = best_by_reference_frame.get(reference["frame"])
            if (previous is None or result["advisory_rank_score_translation_cm_plus_0p20_rotation_deg"]
                    < previous["advisory_rank_score_translation_cm_plus_0p20_rotation_deg"]):
                best_by_reference_frame[reference["frame"]] = result
    results.sort(key=lambda item: item["advisory_rank_score_translation_cm_plus_0p20_rotation_deg"])
    falling_reference_frames = [best_by_reference_frame[frame]
                                for frame in (23, 30, 40, 50, 60, 69)
                                if frame in best_by_reference_frame]
    return {
        "best_matches_overall": results[:10],
        "best_loop_pose_for_selected_380_falling_frames": falling_reference_frames,
        "interpretation_limit": "Cross-asset pose similarity is a screening aid only; it does not prove visual fit, animation phase, or permission to substitute the source.",
    }


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    report = {
        "status": "POSE_AUDIT_COMPLETE_CYCLE_SELECTION_PENDING",
        "tool_version": TOOL_VERSION,
        "python_version": sys.version.split()[0],
        "ue_commandlet": "UE 5.6 UnrealEditor-Cmd PythonScriptCommandlet",
        "truth_mapping": "User-confirmed runtime 49532 -> LMT380, runtime 49533 -> LMT381; current package content is independently checked below.",
        "method": {
            "pose_evaluation": "RAW component-space visible bone transforms for cycle scoring, local transforms retained for audit, no retargeting or root-motion extraction",
            "loop_search": "rank every >=6-frame period with at least two repetitions available; compare component-space positions/quaternions across all available adjacent cycle pairs (two or three cycles)",
            "candidate_end_frame": "exclusive",
            "ranking": "translation_cm + 0.20*rotation_degrees, advisory only; raw metrics retained; no automatic acceptance threshold",
            "limits": ["Component-pose search does not prove the source gameplay loop event or exact contact/phase boundary.",
                       "Visual loop seam approval remains an E1 task.",
                       "Root transform is reported separately and excluded from body-cycle ranking.",
                       "Two-cycle candidates have lower repeat evidence than three-cycle candidates and are labeled explicitly.",
                       "No animation frames were excluded; helper bones are excluded from the pose metric by the recorded name rules."],
        },
        "exclusion_rules": {
            "excluded_frame_indices": [],
            "excluded_bone_name_tokens": list(EXCLUDE_TOKENS),
            "excluded_bone_suffixes": ["_ik", "_t", "_w"],
            "root_bone": "reported separately and excluded from body-cycle scoring",
        },
        "sequences": {},
    }
    all_candidates = []
    all_samples = {}
    for label, config in ASSETS.items():
        _, metadata, samples = load_sequence(label, config)
        report["sequences"][label] = metadata
        all_samples[label] = samples
        if label == "runtime_49532_lmt380":
            report["cycle_search"] = {
                "candidate_count": 0,
                "candidates": cycle_candidates(samples, metadata["sampling_rate_hz"]),
            }
            all_candidates = report["cycle_search"]["candidates"]
            report["cycle_search"]["candidate_count"] = len(all_candidates)
    report["existing_fall_loop_cross_pose_candidates"] = {}
    for label in ("existing_unsh_fall_loop", "existing_unsh_jianglong_fall_loop"):
        report["existing_fall_loop_cross_pose_candidates"][label] = cross_pose_candidates(
            all_samples["runtime_49532_lmt380"], all_samples[label],
        )
    report["status"] = "AUDIT_COMPLETE_CANDIDATES_REQUIRE_REVIEW"
    json_path = OUT_DIR / "pose_and_cycle_audit.json"
    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    csv_path = OUT_DIR / "cycle_candidates.csv"
    if all_candidates:
        with csv_path.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(all_candidates[0]))
            writer.writeheader()
            writer.writerows(all_candidates)
    print("DESCENDING_THRUST_M1_POSE_AUDIT=" + str(json_path))
    print("DESCENDING_THRUST_M1_CYCLE_CANDIDATES=" + str(len(all_candidates)))
    unreal.log("[DescendingThrustM1] Pose audit complete; no assets were modified.")


main()
