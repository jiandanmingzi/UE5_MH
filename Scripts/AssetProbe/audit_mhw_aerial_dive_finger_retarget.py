"""Audit source and retargeted finger poses for MHW aerial-dive review clips.

Run in UE 5.6 with:
    UnrealEditor-Cmd.exe MHGZ.uproject -run=pythonscript \
      -script=Scripts/AssetProbe/audit_mhw_aerial_dive_finger_retarget.py

The script is read-only for Content assets and writes a diagnostic JSON report
under Saved/AssetProbe. It reports both animated change over time and the
retargeted first-frame pose's difference from the MHR reference pose, so a
static but retargeted grip is not mistaken for a missing finger track.
"""

import json
import math
from pathlib import Path

import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
SOURCE_DIR = ROOT
OUTPUT_DIR = ROOT + "/Retargeted_FingerFixed"
TARGET_MESH_PATH = "/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"
FPS = 60.0
CHAINS = (
    ("Finger_L_00", ("bone_031", "bone_032", "bone_033"),
     ("L_Finger_00", "L_Finger_01", "L_Finger_02", "L_Finger_02_end")),
    ("Finger_L_01", ("bone_034", "bone_035", "bone_036"),
     ("L_Finger_03", "L_Finger_04", "L_Finger_05", "L_Finger_05_end")),
    ("Finger_L_02", ("bone_037", "bone_038", "bone_039"),
     ("L_Finger_06", "L_Finger_07", "L_Finger_08", "L_Finger_08_end")),
    ("Finger_L_03", ("bone_040", "bone_041", "bone_042", "bone_043"),
     ("L_Finger_09", "L_Finger_10", "L_Finger_11", "L_Finger_12", "L_Finger_12_end")),
    ("Finger_L_04", ("bone_044", "bone_045", "bone_046"),
     ("L_Finger_13", "L_Finger_14", "L_Finger_15", "L_Finger_15_end")),
    ("Finger_R_00", ("bone_048", "bone_049", "bone_050"),
     ("R_Finger_00", "R_Finger_01", "R_Finger_02", "R_Finger_02_end")),
    ("Finger_R_01", ("bone_051", "bone_052", "bone_053"),
     ("R_Finger_03", "R_Finger_04", "R_Finger_05", "R_Finger_05_end")),
    ("Finger_R_02", ("bone_054", "bone_055", "bone_056"),
     ("R_Finger_06", "R_Finger_07", "R_Finger_08", "R_Finger_08_end")),
    # Source bone_057 is a branch hub absent from the target rig. Match its
    # two digit branches independently instead of inventing a target joint.
    ("Finger_R_03", ("bone_058", "bone_059", "bone_060"),
     ("R_Finger_09", "R_Finger_10", "R_Finger_11", "R_Finger_11_end")),
    ("Finger_R_04", ("bone_061", "bone_062", "bone_063"),
     ("R_Finger_12", "R_Finger_13", "R_Finger_14", "R_Finger_14_end")),
)


def require(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise FileNotFoundError(path)
    return asset


def pose_options(kind=unreal.AnimDataEvalType.RAW):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", kind)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    return options


def vector_delta(a, b):
    return math.sqrt(sum((float(x) - float(y)) ** 2
                         for x, y in zip((a.x, a.y, a.z), (b.x, b.y, b.z))))


def quaternion_delta_degrees(a, b):
    dot = abs(float(a.x) * float(b.x) + float(a.y) * float(b.y)
              + float(a.z) * float(b.z) + float(a.w) * float(b.w))
    dot = min(1.0, max(-1.0, dot))
    return math.degrees(2.0 * math.acos(dot))


def transform_delta(a, b):
    return {
        "translation_cm": vector_delta(a.translation, b.translation),
        "rotation_degrees": quaternion_delta_degrees(a.rotation, b.rotation),
        "scale": max(abs(float(x) - float(y))
                     for x, y in zip((a.scale3d.x, a.scale3d.y, a.scale3d.z),
                                     (b.scale3d.x, b.scale3d.y, b.scale3d.z))),
    }


def analyze_sequence(sequence, bones, reference_pose=None):
    length = float(sequence.get_play_length())
    count = int(round(length * FPS)) + 1
    options = pose_options()
    first = {}
    maxima = {bone: {"translation_cm": 0.0, "rotation_degrees": 0.0, "scale": 0.0}
              for bone in bones}
    ref_delta = {} if reference_pose else None
    for frame in range(count):
        time = min(frame / FPS, length)
        pose = sequence.get_anim_pose_at_time(time, options)
        for bone in bones:
            value = pose.get_bone_pose(unreal.Name(bone), unreal.AnimPoseSpaces.LOCAL)
            if frame == 0:
                first[bone] = value
            delta = transform_delta(first[bone], value)
            for channel, amount in delta.items():
                maxima[bone][channel] = max(maxima[bone][channel], amount)
            if reference_pose is not None and frame == 0:
                ref = reference_pose.get_ref_bone_pose(unreal.Name(bone),
                                                       unreal.AnimPoseSpaces.LOCAL)
                ref_delta[bone] = transform_delta(ref, value)
    return {"sample_rate_hz": FPS, "sample_count": count,
            "animated_delta_from_first_frame": maxima,
            "first_frame_delta_from_target_reference": ref_delta}


target_mesh = require(TARGET_MESH_PATH)
target_skeleton = target_mesh.get_editor_property("skeleton")
target_reference = unreal.AnimPoseExtensions.get_reference_pose(target_skeleton)
report = {
    "status": "FINGER_RETARGET_AUDIT",
    "source_and_target_preserved": True,
    "target_mesh": TARGET_MESH_PATH,
    "expected_chain_count_per_hand": 5,
    "animations": [],
}

for index in (380, 381):
    source = require(SOURCE_DIR + f"/AS_MHW_IG_AerialDive_LMT{index}_Review")
    target = require(OUTPUT_DIR + f"/AS_MHW_IG_AerialDive_LMT{index}_MHR_FingerFixed_Review")
    if target.get_editor_property("skeleton") != target_skeleton:
        raise RuntimeError("Finger-fixed animation uses the wrong target Skeleton")
    if abs(float(source.get_play_length()) - float(target.get_play_length())) > 1e-5:
        raise RuntimeError("Source and finger-fixed sequence duration differs")
    chain_rows = []
    for name, source_bones, target_bones in CHAINS:
        chain_rows.append({
            "chain": name,
            "source": analyze_sequence(source, source_bones),
            "target": analyze_sequence(target, target_bones, target_reference),
        })
    report["animations"].append({
        "source": source.get_path_name(),
        "target": target.get_path_name(),
        "length_seconds": float(target.get_play_length()),
        "rate_scale": float(target.get_editor_property("rate_scale")),
        "chains": chain_rows,
    })

output = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_finger_retarget_audit.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("MHW_FINGER_RETARGET_AUDIT=" + json.dumps(report, ensure_ascii=False))
