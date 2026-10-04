"""Read-only UE source/target hierarchy and animation-scale diagnostic.

Writes a report under Saved/AssetProbe. Does not save or mutate any asset.
"""

import json
from pathlib import Path
import unreal


SOURCE = "/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/Review/MHW/AerialDive"
OUTPUT = Path(unreal.Paths.project_saved_dir()) / "AssetProbe/mhw_aerial_dive_retarget_probe.json"


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def describe(transform):
    return {"p": vec(transform.translation),
            "q": [float(transform.rotation.x), float(transform.rotation.y),
                  float(transform.rotation.z), float(transform.rotation.w)],
            "s": vec(transform.scale3d)}


def limits(values):
    return {"min": [min(row[i] for row in values) for i in range(3)],
            "max": [max(row[i] for row in values) for i in range(3)]}


def mesh_info(path):
    mesh = unreal.load_asset(path)
    if mesh is None:
        raise RuntimeError("Missing mesh " + path)
    component = unreal.new_object(unreal.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)
    skeleton = mesh.get_editor_property("skeleton")
    refpose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    result = {"asset": path, "skeleton": skeleton.get_path_name(), "bones": []}
    for i in range(component.get_num_bones()):
        name = component.get_bone_name(i)
        result["bones"].append({"name": str(name), "parent": str(component.get_parent_bone(name)),
                                "ref_local": describe(refpose.get_ref_bone_pose(name, unreal.AnimPoseSpaces.LOCAL)),
                                "ref_component": describe(refpose.get_ref_bone_pose(name, unreal.AnimPoseSpaces.WORLD))})
    return result


def sequence_info(index):
    seq = unreal.load_asset(SOURCE + f"/AS_MHW_IG_AerialDive_LMT{index}_Review")
    # Diagnostic samples at the known Blender export frequency. UE 5.6 does
    # not expose GetSamplingFrameRate/GetDataModel as Python methods here.
    fps = 60.0
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    samples = []
    for i in range(int(round(seq.get_play_length() * fps)) + 1):
        time = min(i / fps, seq.get_play_length())
        pose = seq.get_anim_pose_at_time(time, options)
        samples.append({"t": time,
                        "local": describe(pose.get_bone_pose("bone_030", unreal.AnimPoseSpaces.LOCAL)),
                        "component": describe(pose.get_bone_pose("bone_030", unreal.AnimPoseSpaces.WORLD))})
    return {"asset": seq.get_path_name(), "length": seq.get_play_length(),
            "probe_sample_rate_hz": fps, "rate_scale": seq.get_editor_property("rate_scale"),
            "bone030_local_scale": limits([s["local"]["s"] for s in samples]),
            "bone030_component_scale": limits([s["component"]["s"] for s in samples]),
            "samples": samples}


report = {"source": mesh_info(SOURCE + "/SK_MHW_IG_AerialDive_ReviewRig"),
          "target": mesh_info("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body"),
          "animations": {str(i): sequence_info(i) for i in (380, 381)}}
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("MHW_RETARGET_PROBE=" + json.dumps({
    "source_bones": len(report["source"]["bones"]),
    "target_bones": len(report["target"]["bones"]),
    "animations": {k: {n: v for n, v in row.items() if n != "samples"}
                   for k, row in report["animations"].items()}}, ensure_ascii=False))
