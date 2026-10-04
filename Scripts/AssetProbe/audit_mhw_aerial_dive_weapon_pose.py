"""Compare MHW LMT381's final weapon/grip pose with native MHR idle poses.

Read-only for Content assets. Writes a diagnostic report under Saved/AssetProbe.
Run with the UE 5.6 editor Python commandlet while the editor is closed.
"""

import json
import math
from pathlib import Path

import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
TARGETS = {
    "381_fingerfixed": ROOT + "/Retargeted_FingerFixed/AS_MHW_IG_AerialDive_LMT381_MHR_FingerFixed_Review",
    "381_source": ROOT + "/AS_MHW_IG_AerialDive_LMT381_Review",
}
IDLES = {
    "unsheathed_ig_idle_ip": "/Game/Characters/Demo/Anims/LocomotionRefactor/RuntimeInPlace/Unsheathed_IG/AS_UnSh_Idle_IP",
    "unsheathed_ig_idle": "/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Idle",
    "sheathed_idle_ip": "/Game/Characters/Demo/Anims/LocomotionRefactor/RuntimeInPlace/Sheathed/AS_Shth_Idle_IP",
}
TARGET_MESH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
BONES = (
    "Spine_01", "R_Arm_00", "R_Arm_01", "R_Arm_02", "R_Arm_03", "R_Weapon_00",
    "R_Weapon_00_end", "R_Grip_00", "L_Arm_00", "L_Arm_01", "L_Arm_02", "L_Arm_03",
    "L_Weapon_00", "L_Weapon_00_end",
)
POSE_BONES = ("R_Arm_02", "R_Arm_03", "R_Weapon_00", "L_Arm_02", "L_Arm_03", "L_Weapon_00")
FPS = 60.0


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError("Missing asset: " + path)
    return asset


def options():
    result = unreal.AnimPoseEvaluationOptions()
    result.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    result.set_editor_property("should_retarget", False)
    result.set_editor_property("extract_root_motion", False)
    result.set_editor_property("incorporate_root_motion_into_pose", False)
    return result


def xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def transform(value):
    return {
        "translation": xyz(value.translation),
        "rotation_xyzw": [float(value.rotation.x), float(value.rotation.y),
                          float(value.rotation.z), float(value.rotation.w)],
        "scale": xyz(value.scale3d),
    }


def quaternion_inverse_rotate(q, vector):
    x, y, z, w = q
    vx, vy, vz = vector
    # Rotate vector by conjugate(q), assuming q is normalized.
    tx = -2.0 * (y * vz - z * vy)
    ty = -2.0 * (z * vx - x * vz)
    tz = -2.0 * (x * vy - y * vx)
    return [vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx)]


def quaternion_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def root_relative(bone, root):
    bone_p, root_p = bone["translation"], root["translation"]
    delta = [bone_p[i] - root_p[i] for i in range(3)]
    root_q = root["rotation_xyzw"]
    inv_root_q = [-root_q[0], -root_q[1], -root_q[2], root_q[3]]
    return {"translation": quaternion_inverse_rotate(root_q, delta),
            "rotation_xyzw": quaternion_multiply(inv_root_q, bone["rotation_xyzw"]),
            "scale": [bone["scale"][i] / root["scale"][i] for i in range(3)]}


def quaternion_degrees(a, b):
    dot = abs(sum(float(x) * float(y) for x, y in zip(
        (a.rotation.x, a.rotation.y, a.rotation.z, a.rotation.w),
        (b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w))))
    return math.degrees(2.0 * math.acos(min(1.0, max(-1.0, dot))))


def components(value, key, attribute):
    if isinstance(value, dict):
        return value[key]
    return xyz(getattr(value, attribute))


def transform_error(a, b):
    a_translation = components(a, "translation", "translation")
    b_translation = components(b, "translation", "translation")
    a_rotation = a["rotation_xyzw"] if isinstance(a, dict) else (
        float(a.rotation.x), float(a.rotation.y), float(a.rotation.z), float(a.rotation.w))
    b_rotation = b["rotation_xyzw"] if isinstance(b, dict) else (
        float(b.rotation.x), float(b.rotation.y), float(b.rotation.z), float(b.rotation.w))
    a_scale = components(a, "scale", "scale3d")
    b_scale = components(b, "scale", "scale3d")
    delta = math.sqrt(sum((x - y) ** 2 for x, y in zip(a_translation, b_translation)))
    dot = abs(sum(x * y for x, y in zip(a_rotation, b_rotation)))
    rotation = math.degrees(2.0 * math.acos(min(1.0, max(0.0, dot))))
    return {"translation_cm": delta, "rotation_degrees": rotation,
            "scale_max_abs": max(abs(x - y) for x, y in zip(a_scale, b_scale))}


def sample(asset, time, names):
    pose = asset.get_anim_pose_at_time(max(0.0, min(float(time), float(asset.get_play_length()))), options())
    result = {}
    root = transform(pose.get_bone_pose(unreal.Name("Root"), unreal.AnimPoseSpaces.WORLD))
    for name in names:
        component = transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD))
        result[name] = {
            "local": transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.LOCAL)),
            "component": component,
            "root_relative_component": root_relative(component, root),
        }
    return result


def sample_source(asset, time):
    pose = asset.get_anim_pose_at_time(max(0.0, min(float(time), float(asset.get_play_length()))), options())
    rows = {}
    for name in ("bone_008", "bone_030", "bone_012", "bone_047"):
        rows[name] = {
            "local": transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.LOCAL)),
            "component": transform(pose.get_bone_pose(unreal.Name(name), unreal.AnimPoseSpaces.WORLD)),
        }
    return rows


def socket_rows(owner):
    rows = []
    try:
        sockets = owner.get_editor_property("sockets")
    except Exception:
        try:
            count = int(owner.num_sockets())
            sockets = [owner.get_socket_by_index(index) for index in range(count)]
        except Exception as exc:
            return {"error": repr(exc), "api": [name for name in dir(owner) if "socket" in name.lower()]}
    for socket in sockets:
        try:
            name = str(socket.get_editor_property("socket_name"))
            bone = str(socket.get_editor_property("bone_name"))
            location = socket.get_editor_property("relative_location")
            rotation = socket.get_editor_property("relative_rotation")
            scale = socket.get_editor_property("relative_scale")
            rows.append({"name": name, "parent_bone": bone,
                         "relative_location": xyz(location),
                         "relative_rotation_pitch_yaw_roll": [float(rotation.pitch),
                                                              float(rotation.yaw),
                                                              float(rotation.roll)],
                         "relative_scale": xyz(scale)})
        except Exception as exc:
            rows.append({"socket": str(socket), "error": repr(exc)})
    return rows


def pose_cost(a_pose, b_pose):
    # Component translation and rotation detect attachment-space displacement;
    # local rotation catches a wrong hand/weapon bone mapping.
    total = 0.0
    for name in POSE_BONES:
        a, b = a_pose[name], b_pose[name]
        total += transform_error(a["root_relative_component"], b["root_relative_component"])["translation_cm"]
        total += transform_error(a["root_relative_component"], b["root_relative_component"])["rotation_degrees"] * 0.1
        total += transform_error(a["local"], b["local"])["rotation_degrees"] * 0.05
    return total


def main():
    target_mesh = load(TARGET_MESH)
    skeleton = target_mesh.get_editor_property("skeleton")
    target = load(TARGETS["381_fingerfixed"])
    source = load(TARGETS["381_source"])
    if target.get_editor_property("skeleton") != skeleton:
        raise RuntimeError("FingerFixed 381 does not use the target demo Skeleton")
    if source.get_editor_property("skeleton") == skeleton:
        raise RuntimeError("Expected source 381 to remain on its MHW source Skeleton")

    target_end_time = float(target.get_play_length())
    target_begin_time = max(0.0, target_end_time - 1.0 / FPS)
    target_rows = {
        "last_sample": {"time": target_end_time, "bones": sample(target, target_end_time, BONES)},
        "one_frame_before_end": {"time": target_begin_time, "bones": sample(target, target_begin_time, BONES)},
    }
    source_end_time = float(source.get_play_length())
    source_rows = {
        "last_sample": {"time": source_end_time, "bones": sample_source(source, source_end_time)},
        "one_frame_before_end": {"time": max(0.0, source_end_time - 1.0 / FPS),
                                 "bones": sample_source(source, source_end_time - 1.0 / FPS)},
    }
    report = {
        "status": "READ_ONLY_WEAPON_POSE_COMPARISON",
        "target_skeleton": skeleton.get_path_name(),
        "target_mesh": target_mesh.get_path_name(),
        "target_381": target.get_path_name(),
        "target_381_duration_seconds": target_end_time,
        "target_381_endpoint_samples": target_rows,
        "source_381_endpoint_samples": source_rows,
        "weapon_sockets": {
            "target_mesh": socket_rows(target_mesh),
            "target_skeleton": socket_rows(skeleton),
        },
        "native_idles": {},
    }

    endpoint = target_rows["last_sample"]["bones"]
    for label, path in IDLES.items():
        idle = load(path)
        if idle.get_editor_property("skeleton") != skeleton:
            report["native_idles"][label] = {
                "asset": path,
                "skeleton": idle.get_editor_property("skeleton").get_path_name(),
                "warning": "different Skeleton; pose comparison skipped",
            }
            continue
        duration = float(idle.get_play_length())
        count = max(1, int(math.ceil(duration * FPS)))
        candidates = []
        sampled = {}
        for frame in range(count + 1):
            time = min(frame / FPS, duration)
            row = sample(idle, time, BONES)
            sampled[frame] = {"frame": frame, "time": time, "cost": pose_cost(endpoint, row), "bones": row}
        best = min(sampled.values(), key=lambda row: row["cost"])
        chosen_times = sorted(set([0, count // 4, count // 2, (3 * count) // 4, count, best["frame"]]))
        chosen = [sampled[frame] for frame in chosen_times]
        for row in chosen:
            candidates.append({"frame": row["frame"], "time": row["time"], "pose_cost": row["cost"],
                               "bones": row["bones"]})
        errors = {}
        for name in BONES:
            a, b = endpoint[name], best["bones"][name]
            errors[name] = {
                "local": transform_error(a["local"], b["local"]),
                "root_relative_component": transform_error(a["root_relative_component"],
                                                            b["root_relative_component"]),
            }
        report["native_idles"][label] = {
            "asset": idle.get_path_name(), "skeleton": skeleton.get_path_name(),
            "duration_seconds": duration, "frame_count_estimate": count,
            "best_matching_idle_frame": best["frame"], "best_matching_idle_time": best["time"],
            "best_pose_cost": best["cost"], "best_frame_transform_errors_381_end_vs_idle": errors,
            "reference_samples": candidates,
        }

    output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_weapon_pose_comparison.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("MHW_WEAPON_POSE_COMPARISON=" + str(output))


main()
