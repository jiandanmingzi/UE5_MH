"""Inspect retargeted MHW dive assets and UE Python animation editing APIs.

This is read-only with respect to content assets; it writes one diagnostic JSON
report to Saved/AssetProbe.
"""

import json
from pathlib import Path
import unreal


ROOT = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
RETARGETED = ROOT + "/Retargeted"
OUTPUT = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_retarget_outputs.json"


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def transform(value):
    return {"p": vec(value.translation),
            "q": [float(value.rotation.x), float(value.rotation.y),
                  float(value.rotation.z), float(value.rotation.w)],
            "s": vec(value.scale3d)}


def sample_sequence(asset):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    length = float(asset.get_play_length())
    count = int(round(length * 60.0)) + 1
    samples = []
    scale_y = []
    root_positions = []
    pelvis_positions = []
    for i in range(count):
        time = min(i / 60.0, length)
        pose = asset.get_anim_pose_at_time(time, options)
        weapon = pose.get_bone_pose(unreal.Name("L_Weapon_00"), unreal.AnimPoseSpaces.LOCAL)
        root = pose.get_bone_pose(unreal.Name("Root"), unreal.AnimPoseSpaces.LOCAL)
        pelvis = pose.get_bone_pose(unreal.Name("Waist_00"), unreal.AnimPoseSpaces.LOCAL)
        scale_y.append(float(weapon.scale3d.y))
        root_positions.append(vec(root.translation))
        pelvis_positions.append(vec(pelvis.translation))
        if i in (0, count // 2, count - 1):
            samples.append({"frame": i, "time": time,
                            "L_Weapon_00": transform(weapon),
                            "Root": transform(root),
                            "Waist_00": transform(pelvis)})
    return {"sample_count": count,
            "weapon_scale_y_range": [min(scale_y), max(scale_y)],
            "root_local_position_range": [[min(row[j] for row in root_positions) for j in range(3)],
                                           [max(row[j] for row in root_positions) for j in range(3)]],
            "pelvis_local_position_range": [[min(row[j] for row in pelvis_positions) for j in range(3)],
                                            [max(row[j] for row in pelvis_positions) for j in range(3)]],
            "samples_start_mid_end": samples}


def names(obj, tokens):
    return [name for name in dir(obj) if any(token in name.lower() for token in tokens)]


report = {"assets": [], "python_api": {}}
for name in ("AS_MHW_IG_AerialDive_LMT380_MHR_UnitWeaponScale_Review",
             "AS_MHW_IG_AerialDive_LMT381_MHR_UnitWeaponScale_Review"):
    path = RETARGETED + "/" + name
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        report["assets"].append({"path": path, "error": "load failed"})
        continue
    skeleton = asset.get_editor_property("skeleton")
    skeleton_preview_mesh = None
    try:
        skeleton_preview_mesh = skeleton.get_editor_property("preview_mesh") if skeleton else None
    except Exception:
        pass
    try:
        preview_mesh = asset.get_editor_property("preview_skeletal_mesh")
    except Exception:
        preview_mesh = None
    for getter in ("get_preview_skeletal_mesh", "get_preview_mesh"):
        method = getattr(asset, getter, None)
        if method:
            preview_mesh = method()
            break
    row = {"path": asset.get_path_name(),
           "class": str(asset.get_class()),
           "skeleton": skeleton.get_path_name() if skeleton else None,
           "skeleton_preview_mesh": skeleton_preview_mesh.get_path_name() if skeleton_preview_mesh else None,
           "preview_mesh": preview_mesh.get_path_name() if preview_mesh else None,
           "asset_api": names(asset, ("preview", "mesh", "controller", "track", "bone", "key")),
           "length": float(asset.get_play_length()),
           "rate_scale": float(asset.get_editor_property("rate_scale")),
           "samples": sample_sequence(asset)}
    try:
        controller = asset.get_editor_property("controller")
        row["controller_class"] = str(controller.get_class()) if controller else None
        row["controller_api"] = names(controller, ("bone", "track", "key", "bracket", "modify")) if controller else []
        row["controller_method_docs"] = {
            method_name: getattr(getattr(controller, method_name, None), "__doc__", None)
            for method_name in ("set_bone_track_keys", "get_bone_track_names",
                                "get_bone_track_keys", "get_num_frames",
                                "get_sampling_frame_rate", "open_bracket", "close_bracket")
        }
    except Exception as exc:
        row["controller_error"] = repr(exc)
    row["sequence_api"] = names(asset, ("frame", "sampling", "play_length", "controller"))
    row["number_of_frames"] = getattr(asset, "get_number_of_frames", lambda: None)()
    try:
        row["sampling_frame_rate"] = str(asset.get_sampling_frame_rate())
    except Exception as exc:
        row["sampling_frame_rate_error"] = repr(exc)
    report["assets"].append(row)

for owner in (unreal.AnimSequence, getattr(unreal, "AnimationDataController", None),
              getattr(unreal, "AnimationBlueprintLibrary", None)):
    if owner:
        key = owner.__name__
        report["python_api"][key] = names(owner, ("bone", "track", "key", "controller", "duplicate", "pose"))

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("MHW_RETARGET_OUTPUT_INSPECTION=" + json.dumps(report, ensure_ascii=False))
