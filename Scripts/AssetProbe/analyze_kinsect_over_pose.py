"""Read-only pose-cycle analysis for AS_Unsh_CaoChongZhan_Over.

Run through UnrealEditor-Cmd with -run=pythonscript. Samples source poses,
then ranks repeated two-cycle windows by local-bone transform similarity.
"""

import json
import math
import os
import unreal


OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "AssetProbe")
OUT_JSON = os.path.join(OUT_DIR, "kinsect_over_pose_cycles.json")
SEQUENCE_PATHS = {
    "over": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhan_Over",
    "fall_loop": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_Loop",
    "fall": "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall",
}
INCLUDE_MARKERS = (
    "pelvis", "hips", "spine", "neck", "head", "clavicle", "shoulder",
    "upperarm", "lowerarm", "forearm", "elbow", "wrist", "arm", "hand",
    "thigh", "calf", "foot", "toe", "leg",
)
EXCLUDE_MARKERS = ("_ik", "_end", "finger", "thumb", "twist", "weapon", "parts")


def log(message):
    unreal.log("[KinsectPoseCycles] " + str(message))


def vec3(value):
    return [float(value.x), float(value.y), float(value.z)]


def quat4(value):
    return [float(value.x), float(value.y), float(value.z), float(value.w)]


def get_rate_and_key_count(sequence):
    rate_value = getattr(sequence, "get_sampling_frame_rate", None)
    keys_value = getattr(sequence, "get_number_of_sampled_keys", None)
    rate = rate_value() if callable(rate_value) else None
    keys = int(keys_value()) if callable(keys_value) else 0
    if rate is not None:
        numerator = float(getattr(rate, "numerator", 0.0))
        denominator = float(getattr(rate, "denominator", 1.0))
        fps = numerator / denominator if denominator else 0.0
    else:
        fps = 0.0
    if fps <= 0.0:
        fps = 30.0
    if keys < 2:
        keys = int(round(float(sequence.get_play_length()) * fps)) + 1
    return fps, keys


def sample_sequence(label, path):
    sequence = unreal.EditorAssetLibrary.load_asset(path)
    if sequence is None:
        log(label + " missing: " + path)
        return None

    length = float(sequence.get_play_length())
    rate_scale = float(sequence.get_editor_property("rate_scale"))
    fps, key_count = get_rate_and_key_count(sequence)
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)

    samples = []
    bone_names = None
    for frame_index in range(key_count):
        time = min(float(frame_index) / fps, length)
        pose = sequence.get_anim_pose_at_time(time, options)
        if bone_names is None:
            all_names = list(pose.get_bone_names())
            bone_names = [name for name in all_names
                          if any(marker in str(name).lower() for marker in INCLUDE_MARKERS)
                          and not any(marker in str(name).lower() for marker in EXCLUDE_MARKERS)]
            # Keep root separate: its translation may carry falling motion rather
            # than the cyclic body pose that identifies a reusable loop.
            root_names = [name for name in all_names if str(name).lower() == "root"]
            bone_names = root_names + bone_names
        transforms = {}
        for bone_name in bone_names:
            transform = pose.get_bone_pose(bone_name, unreal.AnimPoseSpaces.LOCAL)
            transforms[str(bone_name)] = {
                "p": vec3(transform.translation),
                "q": quat4(transform.rotation),
            }
        samples.append({"t": time, "bones": transforms})

    result = {
        "asset": sequence.get_path_name(),
        "length_seconds": length,
        "rate_scale": rate_scale,
        "effective_seconds_at_rate_scale": length / rate_scale if rate_scale else None,
        "sample_rate_hz": fps,
        "sampled_keys": key_count,
        "bones": [str(name) for name in bone_names or []],
        "samples": samples,
    }
    log("{} length={} rate_scale={} fps={} keys={} bones={}".format(
        label, length, rate_scale, fps, key_count, len(result["bones"])))
    return result


def transform_delta(a, b):
    pa, pb = a["p"], b["p"]
    position = math.sqrt(sum((x - y) ** 2 for x, y in zip(pa, pb)))
    qa, qb = a["q"], b["q"]
    dot = abs(sum(x * y for x, y in zip(qa, qb)))
    dot = min(1.0, max(-1.0, dot))
    angle = math.degrees(2.0 * math.acos(dot))
    return position, angle


def compare_samples(first, second, bone_names):
    positions = []
    rotations = []
    for bone_name in bone_names:
        a = first["bones"].get(bone_name)
        b = second["bones"].get(bone_name)
        if a is None or b is None:
            continue
        position, rotation = transform_delta(a, b)
        positions.append(position)
        rotations.append(rotation)
    if not positions:
        return None
    return {
        "mean_local_translation_cm": sum(positions) / len(positions),
        "max_local_translation_cm": max(positions),
        "mean_local_rotation_deg": sum(rotations) / len(rotations),
        "max_local_rotation_deg": max(rotations),
        "bones_compared": len(positions),
    }


def find_cycle_windows(sequence_data, minimum_start_ratio=0.20):
    samples = sequence_data["samples"]
    fps = sequence_data["sample_rate_hz"]
    count = len(samples)
    bone_names = [name for name in sequence_data["bones"] if name.lower() != "root"]
    if count < 16 or len(bone_names) < 4:
        return []
    min_start = int(count * minimum_start_ratio)
    # Ignore tiny windows that merely measure a slow or nearly-held pose. A
    # plausible fall cycle should recur over at least 0.30 s of source time.
    min_period = max(9, int(round(0.30 * fps)))
    max_period = min(int(round(1.20 * fps)), (count - min_start) // 2)
    candidates = []
    for period in range(min_period, max_period + 1):
        for start in range(min_start, count - 2 * period + 1):
            # Compare each available pair of adjacent cycles, not a single pose.
            pair_metrics = []
            complete_cycles = (count - start) // period
            compared_cycles = min(complete_cycles, 3)
            for cycle in range(compared_cycles - 1):
                base_a = start + cycle * period
                base_b = base_a + period
                for offset in range(period):
                    pair_metrics.append(compare_samples(
                        samples[base_a + offset], samples[base_b + offset], bone_names))
            pair_metrics = [item for item in pair_metrics if item]
            if not pair_metrics:
                continue
            mean_position = sum(x["mean_local_translation_cm"] for x in pair_metrics) / len(pair_metrics)
            mean_rotation = sum(x["mean_local_rotation_deg"] for x in pair_metrics) / len(pair_metrics)
            # Ranking only; raw cm/degree errors are retained for interpretation.
            rank_score = mean_position + 0.20 * mean_rotation
            span_end = min(count - 1, start + compared_cycles * period)
            root_names = [name for name in sequence_data["bones"] if name.lower() == "root"]
            root_metrics = []
            if root_names:
                for cycle in range(compared_cycles - 1):
                    base_a = start + cycle * period
                    base_b = base_a + period
                    for offset in range(period):
                        root_metrics.append(compare_samples(
                            samples[base_a + offset], samples[base_b + offset], root_names))
            root_pos = (sum(x["mean_local_translation_cm"] for x in root_metrics) / len(root_metrics)
                        if root_metrics else None)
            candidates.append({
                "rank_score_translation_plus_0p20_rotation": rank_score,
                "mean_translation_cm": mean_position,
                "mean_rotation_deg": mean_rotation,
                "root_mean_translation_cm": root_pos,
                "start_frame": start,
                "start_time_source_seconds": samples[start]["t"],
                "period_frames": period,
                "period_source_seconds": period / fps,
                "candidate_repeating_window_end_frame": span_end,
                "candidate_repeating_window_end_source_seconds": samples[span_end]["t"],
                "compared_cycles": compared_cycles,
                "compared_bones": len(bone_names),
            })
    candidates.sort(key=lambda item: item["rank_score_translation_plus_0p20_rotation"])
    # Keep the best window for every period. Keeping only the global top-N
    # would over-report short near-static windows and hide longer cycle tests.
    selected_by_period = {}
    for candidate in candidates:
        selected_by_period.setdefault(candidate["period_frames"], candidate)
    return sorted(selected_by_period.values(),
                  key=lambda item: item["rank_score_translation_plus_0p20_rotation"])


def endpoint_seam(sequence_data):
    samples = sequence_data["samples"]
    body_bones = [name for name in sequence_data["bones"] if name.lower() != "root"]
    if len(samples) < 2:
        return None
    return compare_samples(samples[0], samples[-1], body_bones)


def best_tail_loop_boundaries(sequence_data, minimum_seconds=0.25, maximum_seconds=1.2):
    samples = sequence_data["samples"]
    fps = sequence_data["sample_rate_hz"]
    body_bones = [name for name in sequence_data["bones"] if name.lower() != "root"]
    minimum_frames = max(2, int(round(minimum_seconds * fps)))
    maximum_frames = min(len(samples) - 1, int(round(maximum_seconds * fps)))
    candidates = []
    for duration_frames in range(minimum_frames, maximum_frames + 1):
        start = len(samples) - 1 - duration_frames
        seam = compare_samples(samples[start], samples[-1], body_bones)
        if seam is None:
            continue
        candidates.append({
            "start_frame": start,
            "start_time_source_seconds": samples[start]["t"],
            "end_time_source_seconds": samples[-1]["t"],
            "duration_source_seconds": samples[-1]["t"] - samples[start]["t"],
            "mean_local_translation_cm": seam["mean_local_translation_cm"],
            "mean_local_rotation_deg": seam["mean_local_rotation_deg"],
            "max_local_rotation_deg": seam["max_local_rotation_deg"],
        })
    return sorted(candidates, key=lambda item: item["mean_local_rotation_deg"])[:8]


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    output = {"analysis": {
        "method": "Raw local pose sampled at the sequence target sample rate; ranks tail windows by adjacent-cycle transform similarity.",
        "interpretation": "Low body translation/rotation error over a multi-frame period supports a repeated pose cycle. Root is reported separately because falling displacement can drift while body pose loops.",
    }, "sequences": {}}
    for label, path in SEQUENCE_PATHS.items():
        data = sample_sequence(label, path)
        if data is None:
            output["sequences"][label] = {"error": "asset not found"}
            continue
        data["whole_sequence_endpoint_seam"] = endpoint_seam(data)
        data["best_tail_loop_boundaries"] = best_tail_loop_boundaries(data)
        data["cycle_candidates"] = find_cycle_windows(data)
        output["sequences"][label] = data
        for rank, candidate in enumerate(data["cycle_candidates"][:8], start=1):
            log("{} candidate #{} {}".format(label, rank, candidate))
        # Keep the full sampled transforms in Saved for reproducibility; summarize
        # the same data in the log so it is visible even without opening JSON.
    with open(OUT_JSON, "w", encoding="utf-8") as output_file:
        json.dump(output, output_file, ensure_ascii=False, indent=2)
    log("wrote " + OUT_JSON)


main()
